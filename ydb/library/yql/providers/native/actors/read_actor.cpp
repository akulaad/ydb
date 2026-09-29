#include <ydb/library/yql/providers/native/read_stream.h>

#include <ydb/library/actors/core/actor_bootstrapped.h>
#include <ydb/library/actors/core/actorsystem.h>
#include <ydb/library/actors/core/hfunc.h>
#include <yql/essentials/core/yql_expr_type_annotation.h>
#include <yql/essentials/minikql/computation/mkql_computation_node_holders.h>
#include <yql/essentials/utils/yql_panic.h>

#include <arrow/api.h>

#include <algorithm>
#include <optional>

namespace NYql::NNative {
namespace {

using namespace NActors;
using namespace NDq;

class TNativeReadActor final : public TActorBootstrapped<TNativeReadActor>, public IDqComputeActorAsyncInput {
    struct TEvRead : TEventLocal<TEvRead, EventSpaceBegin(TEvents::ES_PRIVATE)> {
        explicit TEvRead(TReadResult result) : Result(std::move(result)) {}
        TReadResult Result;
    };

public:
    TNativeReadActor(TReadStreamFactory factory, TReadActorSettings settings, IDqAsyncIoFactory::TSourceArguments&& args)
        : Factory_(std::move(factory))
        , Settings_(std::move(settings))
        , InputIndex_(args.InputIndex)
        , ComputeActorId_(args.ComputeActorId)
        , HolderFactory_(args.HolderFactory)
        , Alloc_(std::move(args.Alloc))
        , Quota_(std::move(args.MemoryQuotaManager))
        , ValidationMode_(args.DatumValidationMode)
    {
        IngressStats_.Level = args.StatsLevel;
        auto names = Settings_.Columns;
        names.emplace_back(BlockLengthColumnName);
        std::sort(names.begin(), names.end());
        for (const auto& name : Settings_.Columns) {
            ColumnPositions_.push_back(std::lower_bound(names.begin(), names.end(), name) - names.begin());
        }
        LengthPosition_ = std::lower_bound(names.begin(), names.end(), TString(BlockLengthColumnName)) - names.begin();
    }

    void Bootstrap() {
        Become(&TNativeReadActor::StateFunc);
        Context_ = {TActivationContext::Now() + Settings_.Timeout, Settings_.MaxBatchBytes};
        Initialized_ = true;
        Schedule(Settings_.Timeout, new TEvents::TEvWakeup(DeadlineTag));
        Notify(); // The first DQ pull grants permission to start I/O.
    }

    static constexpr char ActorName[] = "NATIVE_READ_ACTOR";

    STRICT_STFUNC(StateFunc,
        hFunc(TEvRead, Handle);
        hFunc(TEvents::TEvWakeup, Handle);
        cFunc(TEvents::TEvPoison::EventType, PassAway);
    )

    i64 GetAsyncInputData(NKikimr::NMiniKQL::TUnboxedValueBatch& buffer, TMaybe<TInstant>&,
                         bool& finished, i64 freeSpace) override {
        finished = Finished_ && !Ready_;
        // The compute actor may poll immediately after RegisterWithSameMailbox,
        // before our Bootstrap event initializes the operation deadline.
        if (!Initialized_ || Stopping_ || Failed_ || freeSpace <= 0 || finished) {
            return 0;
        }
        YQL_ENSURE(!buffer.IsWide(), "Native read expects a stream of block structs");
        Demand_ = true;
        ui64 bytes = 0;
        if (Ready_) {
            const auto& batch = *Ready_->Batch;
            NUdf::TUnboxedValue* items = nullptr;
            auto value = HolderFactory_.CreateDirectArrayHolder(Settings_.Columns.size() + 1, items);
            for (size_t i = 0; i < ColumnPositions_.size(); ++i) {
                items[ColumnPositions_[i]] = HolderFactory_.CreateArrowBlock(arrow::Datum(batch.column(i)), ValidationMode_);
            }
            items[LengthPosition_] = HolderFactory_.CreateArrowBlock(
                arrow::Datum(std::make_shared<arrow::UInt64Scalar>(batch.num_rows())), ValidationMode_);
            buffer.emplace_back(std::move(value));
            bytes = Ready_->Bytes;
            Delivered_ = Delivered_ || batch.num_rows() != 0;
            IngressStats_.Bytes += bytes;
            IngressStats_.Rows += batch.num_rows();
            ++IngressStats_.Chunks;
            Ready_.reset();
            Demand_ = freeSpace > static_cast<i64>(bytes);
        }
        Pull();
        return bytes;
    }

    void PassAway() override {
        if (Stopping_) {
            return;
        }
        Stopping_ = true;
        Ready_.reset();
        if (Stream_) {
            Stream_->Cancel();
        }
        // Keep the reservation until the pending Next result has reached this mailbox.
        // The SDK may still be unwinding its completion callback at that point.
        // No holder factory / compute actor access is allowed after this point.
        if (!InFlight_) {
            FinishActor();
        }
    }

    void SaveState(const NDqProto::TCheckpoint&, TSourceState&) override {}
    void LoadState(const TSourceState&) override {}
    void CommitState(const NDqProto::TCheckpoint&) override {}
    ui64 GetInputIndex() const override { return InputIndex_; }
    const TDqAsyncStats& GetIngressStats() const override { return IngressStats_; }

private:
    void FinishActor() {
        Stream_.reset();
        Factory_ = {};
        if (Reserved_) {
            Quota_->FreeQuota(Settings_.MemoryReservation);
            Reserved_ = false;
        }
        TActorBootstrapped::PassAway();
    }

    void Notify() {
        Send(ComputeActorId_, new TEvNewAsyncInputDataArrived(InputIndex_));
    }

    void Fail(const TString& message) {
        if (Failed_ || Stopping_) {
            return;
        }
        Failed_ = true;
        Ready_.reset();
        if (Stream_) {
            Stream_->Cancel();
        }
        Send(ComputeActorId_, new TEvAsyncInputError(InputIndex_, TIssues{TIssue(message)},
             NDqProto::StatusIds::EXTERNAL_ERROR));
    }

    void Pull() {
        if (!Demand_ || InFlight_ || Ready_ || RetryPending_ || Finished_ || Failed_ || Stopping_) {
            return;
        }
        if (TActivationContext::Now() >= Context_.Deadline) {
            Fail("Native source read deadline exceeded");
            return;
        }
        if (!Reserved_) {
            if (!Quota_ || !Quota_->AllocateQuota(Settings_.MemoryReservation, false)) {
                Fail("Native source could not reserve memory for a bounded read");
                return;
            }
            Reserved_ = true;
        }
        try {
            if (!Stream_) {
                Stream_ = Factory_(Context_);
            }
            InFlight_ = true;
            Stream_->Next().Subscribe([system = TActivationContext::ActorSystem(), self = SelfId()](const auto& future) {
                TReadResult result;
                try {
                    result = future.GetValue();
                } catch (...) {
                    // Exceptions from remote clients can include credentials or SQL text.
                    result.Error = "Native source read failed unexpectedly";
                }
                system->Send(self, new TEvRead(std::move(result)));
            });
        } catch (...) {
            InFlight_ = false;
            Fail("Native source could not start the read");
        }
    }

    void Handle(TEvRead::TPtr& ev) {
        InFlight_ = false;
        if (Stopping_) {
            ev->Get()->Result = {};
            FinishActor();
            return;
        }
        if (Failed_) {
            return;
        }
        auto result = std::move(ev->Get()->Result);
        if (result.Error) {
            Ready_.reset();
            if (Stream_) {
                Stream_->Cancel();
                Stream_.reset();
            }
            // Restarting opens a new snapshot. It is safe only before the first DQ row.
            if (result.Retryable && !Delivered_ && Retries_ < Settings_.MaxRetries &&
                TActivationContext::Now() < Context_.Deadline) {
                ++Retries_;
                RetryPending_ = true;
                Schedule(TDuration::MilliSeconds(100 * (1u << Retries_)), new TEvents::TEvWakeup(RetryTag));
                return;
            }
            Fail(result.Error);
            return;
        }
        if (result.Batch && result.Batch->num_rows()) {
            if (result.Bytes > Settings_.MaxBatchBytes || result.Batch->num_columns() != static_cast<int>(Settings_.Columns.size())) {
                Fail("Native source returned a batch exceeding its contract");
                return;
            }
            Ready_ = std::move(result);
            Notify();
        } else if (result.Finished) {
            Finished_ = true;
            Stream_.reset();
            Notify();
        } else {
            Pull();
        }
    }

    void Handle(TEvents::TEvWakeup::TPtr& ev) {
        if (Stopping_ || Failed_ || Finished_) {
            return;
        }
        if (ev->Get()->Tag == DeadlineTag) {
            Fail("Native source read deadline exceeded");
        } else {
            RetryPending_ = false;
            Pull();
        }
    }

    static constexpr ui64 DeadlineTag = 1;
    static constexpr ui64 RetryTag = 2;
    TReadStreamFactory Factory_;
    const TReadActorSettings Settings_;
    TReadContext Context_;
    const ui64 InputIndex_;
    const TActorId ComputeActorId_;
    const NKikimr::NMiniKQL::THolderFactory& HolderFactory_;
    const std::shared_ptr<NKikimr::NMiniKQL::TScopedAlloc> Alloc_;
    const IMemoryQuotaManager::TPtr Quota_;
    const EDatumValidationMode ValidationMode_;
    TVector<size_t> ColumnPositions_;
    size_t LengthPosition_ = 0;
    TDqAsyncStats IngressStats_;
    std::shared_ptr<IReadStream> Stream_;
    std::optional<TReadResult> Ready_;
    ui32 Retries_ = 0;
    bool Initialized_ = false;
    bool Reserved_ = false;
    bool InFlight_ = false;
    bool Demand_ = false;
    bool Delivered_ = false;
    bool RetryPending_ = false;
    bool Finished_ = false;
    bool Failed_ = false;
    bool Stopping_ = false;
};

} // namespace

std::pair<NDq::IDqComputeActorAsyncInput*, NActors::IActor*> CreateNativeReadActor(
    TReadStreamFactory factory, TReadActorSettings settings, NDq::IDqAsyncIoFactory::TSourceArguments&& args) {
    auto* actor = new TNativeReadActor(std::move(factory), std::move(settings), std::move(args));
    return {actor, actor};
}

} // namespace NYql::NNative
