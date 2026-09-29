#include <ydb/library/yql/providers/native/read_stream.h>
#include <ydb/library/yql/providers/common/ut_helpers/dq_fake_ca.h>
#include <ydb/library/yql/dq/proto/dq_tasks.pb.h>
#include <library/cpp/testing/unittest/registar.h>
#include <arrow/api.h>

#include <atomic>
#include <mutex>
#include <optional>

namespace NYql::NNative {
namespace {

using namespace NDq;
const auto WaitTimeout = TDuration::Seconds(10);

class TQuota final : public IMemoryQuotaManager {
public:
    bool AllocateQuota(ui64 bytes, bool) override {
        if (Reject) {
            return false;
        }
        Allocated += bytes;
        return true;
    }
    void FreeQuota(ui64 bytes) override {
        Allocated -= bytes;
        Released.SetValue();
    }
    ui64 GetCurrentQuota() const override { return Allocated; }
    ui64 GetMaxMemorySize() const override { return Allocated; }
    i64 GetMemoryAvailability() const override { return 1000000; }
    TString MemoryConsumptionDetails() const override { return {}; }
    std::atomic<ui64> Allocated = 0;
    bool Reject = false;
    NThreading::TPromise<void> Released = NThreading::NewPromise();
};

class TStream final : public IReadStream {
public:
    NThreading::TFuture<TReadResult> Next() override {
        auto promise = NThreading::NewPromise<TReadResult>();
        {
            std::lock_guard lock(Mutex);
            UNIT_ASSERT(!Pending);
            Pending = promise;
        }
        if (++Calls == 1) {
            Started.SetValue();
        }
        return promise.GetFuture();
    }

    void Resolve(TReadResult result) {
        NThreading::TPromise<TReadResult> promise;
        {
            std::lock_guard lock(Mutex);
            UNIT_ASSERT(Pending);
            promise = *Pending;
            Pending.reset();
        }
        promise.SetValue(std::move(result));
    }

    void Cancel() override {
        if (Cancelled.exchange(true)) {
            return;
        }
        std::optional<NThreading::TPromise<TReadResult>> promise;
        {
            std::lock_guard lock(Mutex);
            promise.swap(Pending);
        }
        if (promise) {
            promise->SetValue({.Error = "cancelled"});
        }
    }

    std::atomic<ui32> Calls = 0;
    std::atomic<bool> Cancelled = false;
    NThreading::TPromise<void> Started = NThreading::NewPromise();
private:
    std::mutex Mutex;
    std::optional<NThreading::TPromise<TReadResult>> Pending;
};

void Init(TFakeCASetup& setup, TReadStreamFactory factory, const std::shared_ptr<TQuota>& quota,
          TDuration timeout = TDuration::Seconds(30), bool pollBeforeBootstrap = false) {
    setup.Execute([&](TFakeActor& actor) {
        NDqProto::TTaskInput input;
        THashMap<TString, TString> params;
        TVector<TString> ranges;
        auto [asyncInput, readActor] = CreateNativeReadActor(std::move(factory),
            {.Timeout = timeout, .MaxBatchBytes = 1024, .MemoryReservation = 4096, .MaxRetries = 2, .Columns = {"value"}},
            IDqAsyncIoFactory::TSourceArguments{
                .InputDesc = input,
                .InputIndex = 0,
                .StatsLevel = {},
                .TxId = {},
                .TaskId = 1,
                .SecureParams = params,
                .TaskParams = params,
                .ReadRanges = ranges,
                .ComputeActorId = actor.SelfId(),
                .TypeEnv = actor.TypeEnv,
                .HolderFactory = actor.HolderFactory,
                .ProgramBuilder = actor.ProgramBuilder,
                .MemoryQuotaManager = quota,
            });
        actor.InitAsyncInput(asyncInput, readActor);
        if (pollBeforeBootstrap) {
            // Reproduce the CA's synchronous initial poll. The child shares this
            // mailbox, so its Bootstrap event cannot run until this callback returns.
            NKikimr::NMiniKQL::TUnboxedValueBatch batch;
            TMaybe<TInstant> watermark;
            bool finished = false;
            UNIT_ASSERT_VALUES_EQUAL(asyncInput->GetAsyncInputData(batch, watermark, finished, 1024), 0);
            UNIT_ASSERT_VALUES_EQUAL(batch.RowCount(), 0);
            UNIT_ASSERT(!finished);
            UNIT_ASSERT_VALUES_EQUAL(quota->Allocated.load(), 0);
        }
    });
    setup.Execute([](TFakeActor&) {}); // Drain bootstrap before capturing notification promises.
}

struct TPull {
    ui64 Rows = 0;
    i64 Bytes = 0;
    bool Finished = false;
    NThreading::TFuture<void> Notification;
};

TPull Pull(TFakeCASetup& setup, i64 freeSpace) {
    TPull result;
    setup.Execute([&](TFakeActor& actor) {
        NKikimr::NMiniKQL::TUnboxedValueBatch batch;
        TMaybe<TInstant> watermark;
        result.Bytes = actor.DqAsyncInput->GetAsyncInputData(batch, watermark, result.Finished, freeSpace);
        result.Rows = batch.RowCount();
        result.Notification = setup.AsyncInputPromises->NewAsyncInputDataArrived.GetFuture();
    });
    return result;
}

TReadResult Batch(ui64 value) {
    arrow::UInt64Builder builder;
    UNIT_ASSERT(builder.Append(value).ok());
    auto array = builder.Finish().ValueOrDie();
    return {.Batch = arrow::RecordBatch::Make(arrow::schema({arrow::field("value", arrow::uint64())}), 1, {array}), .Bytes = 8};
}

} // namespace

Y_UNIT_TEST_SUITE(NativeReadActor) {
    Y_UNIT_TEST(InitialPollBeforeBootstrapWaitsForInitialization) {
        TFakeCASetup setup;
        auto stream = std::make_shared<TStream>();
        auto quota = std::make_shared<TQuota>();
        auto error = setup.AsyncInputPromises->FatalError.GetFuture();
        Init(setup, [stream](const auto&) { return stream; }, quota, TDuration::Seconds(30), true);
        UNIT_ASSERT(!error.HasValue());
        UNIT_ASSERT_VALUES_EQUAL(stream->Calls.load(), 0);
        auto first = Pull(setup, 1);
        UNIT_ASSERT(stream->Started.GetFuture().Wait(WaitTimeout));
        stream->Resolve({.Finished = true});
        UNIT_ASSERT(first.Notification.Wait(WaitTimeout));
        UNIT_ASSERT(Pull(setup, 0).Finished);
        UNIT_ASSERT(!error.HasValue());
    }

    Y_UNIT_TEST(CancellationBeforeDemandDoesNotStartRemoteOperation) {
        TFakeCASetup setup;
        auto quota = std::make_shared<TQuota>();
        std::atomic<ui32> attempts = 0;
        Init(setup, [&](const auto&) { ++attempts; return std::make_shared<TStream>(); }, quota);
        setup.Terminate();
        UNIT_ASSERT_VALUES_EQUAL(attempts.load(), 0);
        UNIT_ASSERT_VALUES_EQUAL(quota->Allocated.load(), 0);
    }

    Y_UNIT_TEST(PositiveDemandBoundsOneReadAndOneBatch) {
        TFakeCASetup setup;
        auto stream = std::make_shared<TStream>();
        auto quota = std::make_shared<TQuota>();
        Init(setup, [stream](const auto&) { return stream; }, quota);
        UNIT_ASSERT_VALUES_EQUAL(Pull(setup, 0).Rows, 0);
        UNIT_ASSERT_VALUES_EQUAL(Pull(setup, -1).Rows, 0);
        UNIT_ASSERT_VALUES_EQUAL(stream->Calls.load(), 0);
        UNIT_ASSERT_VALUES_EQUAL(quota->Allocated.load(), 0);
        auto next = Pull(setup, 1);
        UNIT_ASSERT_VALUES_EQUAL(stream->Calls.load(), 1);
        UNIT_ASSERT_VALUES_EQUAL(quota->Allocated.load(), 4096);
        Pull(setup, 1);
        UNIT_ASSERT_VALUES_EQUAL(stream->Calls.load(), 1);
        stream->Resolve(Batch(42));
        UNIT_ASSERT(next.Notification.Wait(WaitTimeout));
        UNIT_ASSERT_VALUES_EQUAL(Pull(setup, 0).Rows, 0);
        UNIT_ASSERT_VALUES_EQUAL(stream->Calls.load(), 1);
        auto delivered = Pull(setup, 1);
        UNIT_ASSERT_VALUES_EQUAL(delivered.Rows, 1);
        UNIT_ASSERT_VALUES_EQUAL(delivered.Bytes, 8);
        UNIT_ASSERT_VALUES_EQUAL(stream->Calls.load(), 1);
        Pull(setup, 1);
        UNIT_ASSERT_VALUES_EQUAL(stream->Calls.load(), 2);
        setup.Terminate();
        UNIT_ASSERT(stream->Cancelled);
        UNIT_ASSERT(quota->Released.GetFuture().Wait(WaitTimeout));
        UNIT_ASSERT_VALUES_EQUAL(quota->Allocated.load(), 0);
    }

    Y_UNIT_TEST(RetryBeforeDeliveryKeepsOriginalDeadline) {
        TFakeCASetup setup;
        auto first = std::make_shared<TStream>();
        auto second = std::make_shared<TStream>();
        auto quota = std::make_shared<TQuota>();
        std::atomic<ui32> attempts = 0;
        TInstant deadline;
        Init(setup, [&](const auto& context) {
            if (++attempts == 1) {
                deadline = context.Deadline;
                return first;
            }
            UNIT_ASSERT_VALUES_EQUAL(context.Deadline, deadline);
            return second;
        }, quota);
        Pull(setup, 1);
        first->Resolve({.Error = "temporary", .Retryable = true});
        UNIT_ASSERT(second->Started.GetFuture().Wait(WaitTimeout));
        UNIT_ASSERT(first->Cancelled);
        UNIT_ASSERT_VALUES_EQUAL(attempts.load(), 2);
        setup.Terminate();
        UNIT_ASSERT(quota->Released.GetFuture().Wait(WaitTimeout));
    }

    Y_UNIT_TEST(FailureAfterDeliveryCannotOpenAnotherSnapshot) {
        TFakeCASetup setup;
        auto stream = std::make_shared<TStream>();
        auto quota = std::make_shared<TQuota>();
        std::atomic<ui32> attempts = 0;
        Init(setup, [&](const auto&) { ++attempts; return stream; }, quota);
        auto first = Pull(setup, 1);
        stream->Resolve(Batch(42));
        UNIT_ASSERT(first.Notification.Wait(WaitTimeout));
        UNIT_ASSERT_VALUES_EQUAL(Pull(setup, 1).Rows, 1);
        Pull(setup, 1);
        auto error = setup.AsyncInputPromises->FatalError.GetFuture();
        stream->Resolve({.Error = "temporary", .Retryable = true});
        UNIT_ASSERT(error.Wait(WaitTimeout));
        UNIT_ASSERT_VALUES_EQUAL(attempts.load(), 1);
        UNIT_ASSERT(stream->Cancelled);
    }

    Y_UNIT_TEST(DeadlineCancelsPendingRead) {
        TFakeCASetup setup;
        auto stream = std::make_shared<TStream>();
        auto quota = std::make_shared<TQuota>();
        Init(setup, [stream](const auto&) { return stream; }, quota, TDuration::MilliSeconds(200));
        auto error = setup.AsyncInputPromises->FatalError.GetFuture();
        Pull(setup, 1);
        UNIT_ASSERT(error.Wait(WaitTimeout));
        UNIT_ASSERT(stream->Cancelled);
        UNIT_ASSERT_VALUES_EQUAL(stream->Calls.load(), 1);
        setup.Terminate();
        UNIT_ASSERT(quota->Released.GetFuture().Wait(WaitTimeout));
    }

    Y_UNIT_TEST(QuotaDenialPreventsRemoteOperation) {
        TFakeCASetup setup;
        auto quota = std::make_shared<TQuota>();
        quota->Reject = true;
        std::atomic<ui32> attempts = 0;
        Init(setup, [&](const auto&) { ++attempts; return std::make_shared<TStream>(); }, quota);
        auto error = setup.AsyncInputPromises->FatalError.GetFuture();
        Pull(setup, 1);
        UNIT_ASSERT(error.Wait(WaitTimeout));
        UNIT_ASSERT_VALUES_EQUAL(attempts.load(), 0);
    }

    Y_UNIT_TEST(OnlyEofCompletesTheInput) {
        TFakeCASetup setup;
        auto stream = std::make_shared<TStream>();
        auto quota = std::make_shared<TQuota>();
        Init(setup, [stream](const auto&) { return stream; }, quota);
        auto first = Pull(setup, 1);
        UNIT_ASSERT(!first.Finished);
        stream->Resolve({.Finished = true});
        UNIT_ASSERT(first.Notification.Wait(WaitTimeout));
        UNIT_ASSERT(Pull(setup, 0).Finished);
    }
}

} // namespace NYql::NNative
