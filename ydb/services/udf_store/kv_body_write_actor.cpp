#include "kv_body_write_actor.h"

#include <ydb/core/base/path.h>
#include <ydb/library/actors/core/log.h>
#include <ydb/library/services/services.pb.h>

#include <util/string/builder.h>

namespace NKikimr::NUdfStore {

namespace {

class TKvBodyWriteActor : public NActors::TActorBootstrapped<TKvBodyWriteActor> {
    NActors::TActorId ReplyTo_;
    TString VolumePath_;
    TString Md5Key_;
    TString Content_;

    ui64 KVTabletId_ = 0;
    NActors::TActorId PipeClient_;

public:
    TKvBodyWriteActor(
        NActors::TActorId replyTo,
        TString volumePath,
        TString md5Key,
        TString content)
        : ReplyTo_(replyTo)
        , VolumePath_(std::move(volumePath))
        , Md5Key_(std::move(md5Key))
        , Content_(std::move(content))
    {}

    void Bootstrap() {
        Become(&TKvBodyWriteActor::StateResolve);
        SendNavigateRequest();
    }

private:
    void ReplyError(const TString& message) {
        Send(ReplyTo_, new TEvKvBodyWriteResponse(false, message));
        PassAway();
    }

    void ReplyOk() {
        Send(ReplyTo_, new TEvKvBodyWriteResponse(true));
        PassAway();
    }

    void PassAway() override {
        if (PipeClient_) {
            NTabletPipe::CloseClient(SelfId(), PipeClient_);
            PipeClient_ = {};
        }
        TActorBootstrapped::PassAway();
    }

    void SendNavigateRequest() {
        auto req = MakeHolder<NSchemeCache::TSchemeCacheNavigate>();
        auto& entry = req->ResultSet.emplace_back();
        entry.Path = SplitPath(VolumePath_);
        entry.RequestType = NSchemeCache::TSchemeCacheNavigate::TEntry::ERequestType::ByPath;
        entry.ShowPrivatePath = true;
        entry.SyncVersion = false;
        Send(MakeSchemeCacheID(), new TEvTxProxySchemeCache::TEvNavigateKeySet(req.Release()));
    }

    void HandleNavigateResult(TEvTxProxySchemeCache::TEvNavigateKeySetResult::TPtr& ev) {
        NSchemeCache::TSchemeCacheNavigate* request = ev->Get()->Request.Get();
        if (request->ResultSet.size() != 1) {
            ReplyError("SchemeCache returned unexpected result set size for KV volume");
            return;
        }
        auto& entry = request->ResultSet[0];
        if (entry.Status != NSchemeCache::TSchemeCacheNavigate::EStatus::Ok) {
            ReplyError(TStringBuilder() << "SchemeCache resolve failed for path '" << VolumePath_
                << "', status: " << static_cast<int>(entry.Status));
            return;
        }
        if (!entry.SolomonVolumeInfo) {
            ReplyError(TStringBuilder() << "Path '" << VolumePath_ << "' is not a KeyValue volume");
            return;
        }
        const auto& desc = entry.SolomonVolumeInfo->Description;
        if (desc.PartitionsSize() == 0) {
            ReplyError("KeyValue volume has no partitions");
            return;
        }
        KVTabletId_ = desc.GetPartitions(0).GetTabletId();
        if (!KVTabletId_) {
            ReplyError("Failed to get tablet ID for partition 0");
            return;
        }
        Become(&TKvBodyWriteActor::StateWrite);
        SendWrite();
    }

    void SendWrite() {
        NTabletPipe::TClientConfig cfg;
        cfg.RetryPolicy = {.RetryLimitCount = 3u};
        PipeClient_ = Register(NTabletPipe::CreateClient(SelfId(), KVTabletId_, cfg));

        auto req = std::make_unique<TEvKeyValue::TEvExecuteTransaction>();
        req->Record.set_tablet_id(KVTabletId_);
        auto* write = req->Record.add_commands()->mutable_write();
        write->set_key(Md5Key_);
        write->set_value(Content_);
        write->set_storage_channel(1);
        write->set_priority(NKikimrKeyValue::Priorities::PRIORITY_REALTIME);
        NTabletPipe::SendData(SelfId(), PipeClient_, req.release());
    }

    void HandleWriteResponse(TEvKeyValue::TEvExecuteTransactionResponse::TPtr& ev) {
        const auto& record = ev->Get()->Record;
        if (record.status() != NKikimrKeyValue::Statuses::RSTATUS_OK) {
            ReplyError(TStringBuilder()
                << "KV write failed for key='" << Md5Key_ << "': status="
                << static_cast<int>(record.status()) << " msg=" << record.msg());
            return;
        }
        ReplyOk();
    }

    void HandlePipeDestroyed(TEvTabletPipe::TEvClientDestroyed::TPtr&) {
        ReplyError("KV tablet pipe destroyed during write");
    }

    void HandlePipeConnected(TEvTabletPipe::TEvClientConnected::TPtr& ev) {
        if (ev->Get()->Status != NKikimrProto::OK) {
            ReplyError(TStringBuilder() << "Failed to connect to KV tablet, status="
                << static_cast<int>(ev->Get()->Status));
        }
    }

    STRICT_STFUNC(StateResolve,
        hFunc(TEvTxProxySchemeCache::TEvNavigateKeySetResult, HandleNavigateResult);
    )

    STRICT_STFUNC(StateWrite,
        hFunc(TEvKeyValue::TEvExecuteTransactionResponse, HandleWriteResponse);
        hFunc(TEvTabletPipe::TEvClientDestroyed, HandlePipeDestroyed);
        hFunc(TEvTabletPipe::TEvClientConnected, HandlePipeConnected);
    )
};

} // namespace

NActors::IActor* CreateKvBodyWriteActor(
    NActors::TActorId replyTo,
    TString volumePath,
    TString md5Key,
    TString content)
{
    return new TKvBodyWriteActor(
        replyTo,
        std::move(volumePath),
        std::move(md5Key),
        std::move(content));
}

} // namespace NKikimr::NUdfStore
