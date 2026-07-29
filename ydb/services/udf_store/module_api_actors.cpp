#include "module_api_actors.h"

#include "blob_chunks.h"
#include "cpu_spec.h"
#include "kv_body_write_actor.h"
#include "metadata_subscription/storage_paths.h"
#include "metadata_subscription/udf_module.h"
#include "metadata_subscription/wasm_artifact.h"
#include "runtime_flags.h"
#include "wasm/manifest.h"

#include <ydb/library/aclib/aclib.h>
#include <ydb/library/actors/core/actor_bootstrapped.h>
#include <ydb/library/actors/core/hfunc.h>
#include <ydb/library/actors/core/log.h>
#include <ydb/services/metadata/request/request_actor_cb.h>

#include <library/cpp/digest/md5/md5.h>

#include <util/generic/guid.h>
#include <util/string/builder.h>

namespace NKikimr::NUdfStore {
namespace {

using TDialog = NMetadata::NRequest::TDialogYQLRequest;

Ydb::Table::ExecuteDataQueryRequest MakeYqlRequest(const TString& yql, bool readOnly) {
    Ydb::Table::ExecuteDataQueryRequest request;
    request.mutable_query()->set_yql_text(yql);
    request.mutable_query_cache_policy()->set_keep_in_cache(true);
    if (readOnly) {
        request.mutable_tx_control()->mutable_begin_tx()->mutable_snapshot_read_only();
    } else {
        request.mutable_tx_control()->mutable_begin_tx()->mutable_serializable_read_write();
        request.mutable_tx_control()->set_commit_tx(true);
    }
    return request;
}

void ExecuteYql(const NActors::TActorIdentity& selfId, Ydb::Table::ExecuteDataQueryRequest request) {
    auto controller = std::make_shared<NMetadata::NRequest::TNaiveExternalController<TDialog>>(selfId);
    NMetadata::NRequest::TYQLRequestExecutor::Execute(
        std::move(request),
        NACLib::TUserToken("metadata@system", {}),
        controller);
}

Ydb::UdfStore::ModuleType ModuleTypeFromString(TStringBuf value) {
    if (value == "WASM") {
        return Ydb::UdfStore::WASM;
    }
    if (value == "LIBRARY") {
        return Ydb::UdfStore::LIBRARY;
    }
    if (value == "NATIVE_UNSAFE") {
        return Ydb::UdfStore::NATIVE_UNSAFE;
    }
    return Ydb::UdfStore::MODULE_TYPE_UNSPECIFIED;
}

TString ModuleTypeToString(Ydb::UdfStore::ModuleType type) {
    switch (type) {
        case Ydb::UdfStore::WASM:
            return "WASM";
        case Ydb::UdfStore::LIBRARY:
            return "LIBRARY";
        case Ydb::UdfStore::NATIVE_UNSAFE:
            return "NATIVE_UNSAFE";
        default:
            return {};
    }
}

void FillModuleDescription(const NTableQuery::TModuleSourceRow& row, Ydb::UdfStore::ModuleDescription* out) {
    out->set_uid(row.Uid);
    out->set_md5(row.Md5);
    out->set_name(row.Name);
    out->set_module_type(ModuleTypeFromString(row.Type));
    out->set_version(row.Version);
    out->set_size(row.Size);
    out->set_chunk_count(row.ChunkCount);
    if (row.Type == "WASM" || row.Type == "LIBRARY") {
        out->set_compile_status(TUdfModule::CompileStatusToString(row.CompileStatus));
    }
    out->set_compile_error(row.CompileError);
    out->set_manifest(row.Manifest);
}

// ---------------------------------------------------------------------------
// Upload
// ---------------------------------------------------------------------------

class TUdfModuleUploadActor : public NActors::TActorBootstrapped<TUdfModuleUploadActor> {
    NActors::TActorId ReplyTo_;
    TUploadModuleParams Params_;

    enum class EStep {
        FindExisting,
        DeleteChunks,
        WriteChunk,
        WriteKvBody,
        UpsertModule,
    };

    EStep Step_ = EStep::FindExisting;
    TString Md5_;
    TString Uid_;
    TString Name_;
    TString TypeStr_;
    TVector<TString> Chunks_;
    ui64 NextChunk_ = 0;
    bool WithManifest_ = false;
    bool WithCompileStatus_ = false;
    TString CompileStatus_;

public:
    TUdfModuleUploadActor(NActors::TActorId replyTo, TUploadModuleParams params)
        : ReplyTo_(replyTo)
        , Params_(std::move(params))
    {}

    void Bootstrap() {
        Become(&TUdfModuleUploadActor::StateMain);
        if (!TUdfStoreRuntimeFlags::Enabled()) {
            return ReplyError(Ydb::StatusIds::UNSUPPORTED, "UDF Store is disabled");
        }
        if (Params_.Content.size() > MaxModuleContentSize) {
            return ReplyError(Ydb::StatusIds::BAD_REQUEST,
                TStringBuilder() << "content exceeds max size " << MaxModuleContentSize);
        }
        if (Params_.Content.empty()) {
            return ReplyError(Ydb::StatusIds::BAD_REQUEST, "content must not be empty");
        }

        TypeStr_ = ModuleTypeToString(Params_.Type);
        if (!TypeStr_) {
            return ReplyError(Ydb::StatusIds::BAD_REQUEST, "module type is required");
        }

        switch (Params_.Type) {
            case Ydb::UdfStore::WASM:
            case Ydb::UdfStore::LIBRARY:
                if (!TUdfStoreRuntimeFlags::EnableWasmUdf()) {
                    return ReplyError(Ydb::StatusIds::UNSUPPORTED, "EnableWasmUdf is not set");
                }
                break;
            case Ydb::UdfStore::NATIVE_UNSAFE:
                if (!TUdfStoreRuntimeFlags::EnableUnsafeNativeUdf()) {
                    return ReplyError(Ydb::StatusIds::UNSUPPORTED, "EnableUnsafeNativeUdf is not set");
                }
                break;
            default:
                return ReplyError(Ydb::StatusIds::BAD_REQUEST, "unsupported module type");
        }

        Md5_ = MD5::Calc(Params_.Content);
        if (Params_.ExpectedMd5 && Params_.ExpectedMd5 != Md5_) {
            return ReplyError(Ydb::StatusIds::BAD_REQUEST,
                TStringBuilder() << "expected_md5 mismatch: got " << Md5_
                    << " expected " << Params_.ExpectedMd5);
        }

        try {
            if (Params_.Type == Ydb::UdfStore::WASM) {
                if (!Params_.Manifest) {
                    return ReplyError(Ydb::StatusIds::BAD_REQUEST, "manifest is required for WASM modules");
                }
                const auto parsed = NWasm::ParseManifest(Params_.Manifest);
                Name_ = Params_.Name ? Params_.Name : parsed.ModuleName;
                if (!Name_) {
                    return ReplyError(Ydb::StatusIds::BAD_REQUEST, "module name is empty");
                }
                WithManifest_ = true;
                WithCompileStatus_ = true;
                CompileStatus_ = TUdfModule::CompileStatusToString(ECompileStatus::Pending);
            } else if (Params_.Type == Ydb::UdfStore::LIBRARY) {
                Name_ = Params_.Name;
                if (!Name_) {
                    return ReplyError(Ydb::StatusIds::BAD_REQUEST, "name is required for LIBRARY modules");
                }
                WithCompileStatus_ = true;
                CompileStatus_ = TUdfModule::CompileStatusToString(ECompileStatus::Pending);
            } else {
                Name_ = Params_.Name ? Params_.Name : Md5_;
                if (Params_.Manifest) {
                    NWasm::ParseNativeHostManifest(Params_.Manifest);
                    WithManifest_ = true;
                }
            }
        } catch (const yexception& e) {
            return ReplyError(Ydb::StatusIds::BAD_REQUEST, TStringBuilder() << "manifest error: " << e.what());
        }

        if (Params_.Type == Ydb::UdfStore::WASM || Params_.Type == Ydb::UdfStore::LIBRARY) {
            Chunks_ = SplitBlob(Params_.Content);
        }

        Step_ = EStep::FindExisting;
        StartFindExisting();
    }

private:
    void ReplyError(Ydb::StatusIds::StatusCode status, const TString& message) {
        auto ev = MakeHolder<TEvUploadModuleResult>();
        ev->Status = status;
        ev->ErrorMessage = message;
        Send(ReplyTo_, ev.Release());
        PassAway();
    }

    void ReplySuccess() {
        auto ev = MakeHolder<TEvUploadModuleResult>();
        ev->Result.set_md5(Md5_);
        ev->Result.set_uid(Uid_);
        ev->Result.set_size(Params_.Content.size());
        ev->Result.set_module_type(Params_.Type);
        ev->Result.set_compile_status(CompileStatus_);
        Send(ReplyTo_, ev.Release());
        PassAway();
    }

    void StartFindExisting() {
        auto request = MakeYqlRequest(
            Params_.Type == Ydb::UdfStore::LIBRARY
                ? NTableQuery::BuildSelectModuleRowByNameAndTypeQuery(GetModulesTablePath())
                : NTableQuery::BuildSelectModuleRowByMd5Query(GetModulesTablePath()),
            true);
        if (Params_.Type == Ydb::UdfStore::LIBRARY) {
            NTableQuery::SetSelectModuleRowByNameAndTypeParams(request, Name_, TypeStr_);
        } else {
            NTableQuery::SetSelectModuleRowByMd5Params(request, Md5_);
        }
        ExecuteYql(SelfId(), std::move(request));
    }

    void HandleQueryResult(NMetadata::NRequest::TEvRequestResult<TDialog>::TPtr& ev) {
        try {
            OnQuerySuccess(ev->Get()->GetResult());
        } catch (const yexception& e) {
            ReplyError(Ydb::StatusIds::INTERNAL_ERROR, e.what());
        }
    }

    void HandleQueryFailed(NMetadata::NRequest::TEvRequestFailed::TPtr& ev) {
        ReplyError(Ydb::StatusIds::INTERNAL_ERROR,
            TStringBuilder() << "YQL failed at upload step " << static_cast<int>(Step_)
                << ": " << ev->Get()->GetErrorMessage());
    }

    void HandleKvWrite(TEvKvBodyWriteResponse::TPtr& ev) {
        if (!ev->Get()->Success) {
            ReplyError(Ydb::StatusIds::INTERNAL_ERROR, ev->Get()->ErrorMessage);
            return;
        }
        Step_ = EStep::UpsertModule;
        StartUpsertModule();
    }

    void OnQuerySuccess(const Ydb::Table::ExecuteDataQueryResponse& response) {
        switch (Step_) {
            case EStep::FindExisting: {
                NTableQuery::TModuleSourceRow existing;
                if (NTableQuery::ParseModuleSourceResponse(response, existing)) {
                    Uid_ = existing.Uid;
                } else {
                    Uid_ = CreateGuidAsString();
                }
                if (Params_.Type == Ydb::UdfStore::NATIVE_UNSAFE) {
                    Step_ = EStep::WriteKvBody;
                    Register(CreateKvBodyWriteActor(
                        SelfId(),
                        GetBinariesVolumePath(),
                        Md5_,
                        Params_.Content));
                    return;
                }
                if (existing.Uid) {
                    Step_ = EStep::DeleteChunks;
                    auto request = MakeYqlRequest(
                        NTableQuery::BuildDeleteModuleChunksQuery(GetModuleChunksTablePath()),
                        false);
                    NTableQuery::SetDeleteModuleChunksParams(request, Uid_);
                    ExecuteYql(SelfId(), std::move(request));
                    return;
                }
                Step_ = EStep::WriteChunk;
                NextChunk_ = 0;
                StartWriteChunk();
                return;
            }
            case EStep::DeleteChunks: {
                Step_ = EStep::WriteChunk;
                NextChunk_ = 0;
                StartWriteChunk();
                return;
            }
            case EStep::WriteChunk: {
                ++NextChunk_;
                if (NextChunk_ < Chunks_.size()) {
                    StartWriteChunk();
                    return;
                }
                Step_ = EStep::UpsertModule;
                StartUpsertModule();
                return;
            }
            case EStep::UpsertModule:
                ReplySuccess();
                return;
            case EStep::WriteKvBody:
                Y_ABORT("unexpected YQL result in WriteKvBody");
        }
    }

    void StartWriteChunk() {
        Y_ABORT_UNLESS(NextChunk_ < Chunks_.size());
        auto request = MakeYqlRequest(
            NTableQuery::BuildUpsertModuleChunkQuery(GetModuleChunksTablePath()),
            false);
        NTableQuery::SetUpsertModuleChunkParams(request, Uid_, NextChunk_, Chunks_[NextChunk_]);
        ExecuteYql(SelfId(), std::move(request));
    }

    void StartUpsertModule() {
        NTableQuery::TUpsertModuleRow row;
        row.Uid = Uid_;
        row.Md5 = Md5_;
        row.Name = Name_;
        row.Type = TypeStr_;
        row.Version = Params_.Version ? Params_.Version : 1;
        row.Size = Params_.Content.size();
        row.ChunkCount = Chunks_.size();
        row.Manifest = WithManifest_ ? Params_.Manifest : TString();
        row.CompileStatus = CompileStatus_;
        auto request = MakeYqlRequest(
            NTableQuery::BuildUpsertModuleRowQuery(GetModulesTablePath(), WithManifest_, WithCompileStatus_),
            false);
        NTableQuery::SetUpsertModuleRowParams(request, row);
        ExecuteYql(SelfId(), std::move(request));
    }

    STRICT_STFUNC(StateMain,
        hFunc(NMetadata::NRequest::TEvRequestResult<TDialog>, HandleQueryResult);
        hFunc(NMetadata::NRequest::TEvRequestFailed, HandleQueryFailed);
        hFunc(TEvKvBodyWriteResponse, HandleKvWrite);
    )
};

// ---------------------------------------------------------------------------
// Delete
// ---------------------------------------------------------------------------

class TUdfModuleDeleteActor : public NActors::TActorBootstrapped<TUdfModuleDeleteActor> {
    NActors::TActorId ReplyTo_;
    TString Md5_;
    TString Name_;
    Ydb::UdfStore::ModuleType Type_;

    enum class EStep {
        Find,
        DeleteChunks,
        DeleteModule,
        DeleteArtifactChunks,
        DeleteArtifact,
    };

    EStep Step_ = EStep::Find;
    TString Uid_;
    TString TypeStr_;
    TString ArtifactId_;
    TString ArtifactKind_;
    TString ArtifactTablePath_;
    TString ArtifactChunksTablePath_;
    bool Found_ = false;

public:
    TUdfModuleDeleteActor(
        NActors::TActorId replyTo,
        TString md5,
        TString name,
        Ydb::UdfStore::ModuleType type)
        : ReplyTo_(replyTo)
        , Md5_(std::move(md5))
        , Name_(std::move(name))
        , Type_(type)
    {}

    void Bootstrap() {
        Become(&TUdfModuleDeleteActor::StateMain);
        if (!TUdfStoreRuntimeFlags::Enabled()) {
            return ReplyError(Ydb::StatusIds::UNSUPPORTED, "UDF Store is disabled");
        }
        if (Type_ == Ydb::UdfStore::LIBRARY) {
            if (!Name_) {
                return ReplyError(Ydb::StatusIds::BAD_REQUEST, "name is required to delete LIBRARY");
            }
            TypeStr_ = "LIBRARY";
            ArtifactId_ = Name_;
            ArtifactKind_ = WasmArtifactKindToString(EWasmArtifactKind::Library);
        } else if (Md5_) {
            TypeStr_ = ModuleTypeToString(Type_);
            ArtifactId_ = Md5_;
            ArtifactKind_ = WasmArtifactKindToString(EWasmArtifactKind::Module);
        } else {
            return ReplyError(Ydb::StatusIds::BAD_REQUEST, "md5 or name+type required for delete");
        }

        const TString cpuSpec = DetectLocalCpuSpec();
        ArtifactTablePath_ = GetArtifactTablePath(cpuSpec);
        ArtifactChunksTablePath_ = GetArtifactChunksTablePath(cpuSpec);

        Step_ = EStep::Find;
        auto request = MakeYqlRequest(
            Type_ == Ydb::UdfStore::LIBRARY
                ? NTableQuery::BuildSelectModuleRowByNameAndTypeQuery(GetModulesTablePath())
                : NTableQuery::BuildSelectModuleRowByMd5Query(GetModulesTablePath()),
            true);
        if (Type_ == Ydb::UdfStore::LIBRARY) {
            NTableQuery::SetSelectModuleRowByNameAndTypeParams(request, Name_, TypeStr_);
        } else {
            NTableQuery::SetSelectModuleRowByMd5Params(request, Md5_);
        }
        ExecuteYql(SelfId(), std::move(request));
    }

private:
    void ReplyError(Ydb::StatusIds::StatusCode status, const TString& message) {
        auto ev = MakeHolder<TEvDeleteModuleResult>();
        ev->Status = status;
        ev->ErrorMessage = message;
        Send(ReplyTo_, ev.Release());
        PassAway();
    }

    void ReplySuccess() {
        Send(ReplyTo_, new TEvDeleteModuleResult());
        PassAway();
    }

    void HandleQueryResult(NMetadata::NRequest::TEvRequestResult<TDialog>::TPtr& ev) {
        OnQuerySuccess(ev->Get()->GetResult());
    }

    void HandleQueryFailed(NMetadata::NRequest::TEvRequestFailed::TPtr& ev) {
        // Best-effort artifact deletes may fail if tables are absent.
        if (Step_ == EStep::DeleteArtifactChunks || Step_ == EStep::DeleteArtifact) {
            ReplySuccess();
            return;
        }
        ReplyError(Ydb::StatusIds::INTERNAL_ERROR,
            TStringBuilder() << "YQL failed at delete step " << static_cast<int>(Step_)
                << ": " << ev->Get()->GetErrorMessage());
    }

    void OnQuerySuccess(const Ydb::Table::ExecuteDataQueryResponse& response) {
        switch (Step_) {
            case EStep::Find: {
                NTableQuery::TModuleSourceRow row;
                Found_ = NTableQuery::ParseModuleSourceResponse(response, row);
                if (!Found_) {
                    ReplySuccess();
                    return;
                }
                Uid_ = row.Uid;
                if (!TypeStr_) {
                    TypeStr_ = row.Type;
                }
                if (TypeStr_ == "WASM" || TypeStr_ == "LIBRARY") {
                    Step_ = EStep::DeleteChunks;
                    auto request = MakeYqlRequest(
                        NTableQuery::BuildDeleteModuleChunksQuery(GetModuleChunksTablePath()),
                        false);
                    NTableQuery::SetDeleteModuleChunksParams(request, Uid_);
                    ExecuteYql(SelfId(), std::move(request));
                    return;
                }
                Step_ = EStep::DeleteModule;
                StartDeleteModule();
                return;
            }
            case EStep::DeleteChunks:
                Step_ = EStep::DeleteModule;
                StartDeleteModule();
                return;
            case EStep::DeleteModule:
                if (TypeStr_ == "WASM" || TypeStr_ == "LIBRARY") {
                    Step_ = EStep::DeleteArtifactChunks;
                    auto request = MakeYqlRequest(
                        NTableQuery::BuildDeleteArtifactChunksQuery(ArtifactChunksTablePath_),
                        false);
                    NTableQuery::SetDeleteArtifactChunksParams(request, ArtifactId_, ArtifactKind_);
                    ExecuteYql(SelfId(), std::move(request));
                    return;
                }
                ReplySuccess();
                return;
            case EStep::DeleteArtifactChunks: {
                Step_ = EStep::DeleteArtifact;
                auto request = MakeYqlRequest(
                    NTableQuery::BuildDeleteArtifactRowQuery(ArtifactTablePath_),
                    false);
                NTableQuery::SetDeleteArtifactRowParams(request, ArtifactId_, ArtifactKind_);
                ExecuteYql(SelfId(), std::move(request));
                return;
            }
            case EStep::DeleteArtifact:
                ReplySuccess();
                return;
        }
    }

    void StartDeleteModule() {
        auto request = MakeYqlRequest(
            NTableQuery::BuildDeleteModuleByUidQuery(GetModulesTablePath()),
            false);
        NTableQuery::SetDeleteModuleByUidParams(request, Uid_);
        ExecuteYql(SelfId(), std::move(request));
    }

    STRICT_STFUNC(StateMain,
        hFunc(NMetadata::NRequest::TEvRequestResult<TDialog>, HandleQueryResult);
        hFunc(NMetadata::NRequest::TEvRequestFailed, HandleQueryFailed);
    )
};

// ---------------------------------------------------------------------------
// Describe
// ---------------------------------------------------------------------------

class TUdfModuleDescribeActor : public NActors::TActorBootstrapped<TUdfModuleDescribeActor> {
    NActors::TActorId ReplyTo_;
    TString Md5_;
    TString Name_;
    Ydb::UdfStore::ModuleType Type_;

public:
    TUdfModuleDescribeActor(
        NActors::TActorId replyTo,
        TString md5,
        TString name,
        Ydb::UdfStore::ModuleType type)
        : ReplyTo_(replyTo)
        , Md5_(std::move(md5))
        , Name_(std::move(name))
        , Type_(type)
    {}

    void Bootstrap() {
        Become(&TUdfModuleDescribeActor::StateMain);
        if (!TUdfStoreRuntimeFlags::Enabled()) {
            return ReplyError(Ydb::StatusIds::UNSUPPORTED, "UDF Store is disabled");
        }
        if (Type_ == Ydb::UdfStore::LIBRARY || (Name_ && Type_ != Ydb::UdfStore::MODULE_TYPE_UNSPECIFIED && !Md5_)) {
            if (!Name_) {
                return ReplyError(Ydb::StatusIds::BAD_REQUEST, "name is required");
            }
            const TString typeStr = ModuleTypeToString(Type_ != Ydb::UdfStore::MODULE_TYPE_UNSPECIFIED
                ? Type_
                : Ydb::UdfStore::LIBRARY);
            auto request = MakeYqlRequest(
                NTableQuery::BuildSelectModuleRowByNameAndTypeQuery(GetModulesTablePath()),
                true);
            NTableQuery::SetSelectModuleRowByNameAndTypeParams(request, Name_, typeStr);
            ExecuteYql(SelfId(), std::move(request));
            return;
        }
        if (!Md5_) {
            return ReplyError(Ydb::StatusIds::BAD_REQUEST, "md5 or name+type required");
        }
        auto request = MakeYqlRequest(
            NTableQuery::BuildSelectModuleRowByMd5Query(GetModulesTablePath()),
            true);
        NTableQuery::SetSelectModuleRowByMd5Params(request, Md5_);
        ExecuteYql(SelfId(), std::move(request));
    }

private:
    void ReplyError(Ydb::StatusIds::StatusCode status, const TString& message) {
        auto ev = MakeHolder<TEvDescribeModuleResult>();
        ev->Status = status;
        ev->ErrorMessage = message;
        Send(ReplyTo_, ev.Release());
        PassAway();
    }

    void HandleQueryResult(NMetadata::NRequest::TEvRequestResult<TDialog>::TPtr& ev) {
        NTableQuery::TModuleSourceRow row;
        if (!NTableQuery::ParseModuleSourceResponse(ev->Get()->GetResult(), row)) {
            ReplyError(Ydb::StatusIds::NOT_FOUND, "module not found");
            return;
        }
        auto result = MakeHolder<TEvDescribeModuleResult>();
        FillModuleDescription(row, result->Result.mutable_module());
        Send(ReplyTo_, result.Release());
        PassAway();
    }

    void HandleQueryFailed(NMetadata::NRequest::TEvRequestFailed::TPtr& ev) {
        ReplyError(Ydb::StatusIds::INTERNAL_ERROR, ev->Get()->GetErrorMessage());
    }

    STRICT_STFUNC(StateMain,
        hFunc(NMetadata::NRequest::TEvRequestResult<TDialog>, HandleQueryResult);
        hFunc(NMetadata::NRequest::TEvRequestFailed, HandleQueryFailed);
    )
};

// ---------------------------------------------------------------------------
// List
// ---------------------------------------------------------------------------

class TUdfModuleListActor : public NActors::TActorBootstrapped<TUdfModuleListActor> {
    NActors::TActorId ReplyTo_;
    Ydb::UdfStore::ModuleType Type_;
    TString NamePrefix_;
    ui64 Limit_;

public:
    TUdfModuleListActor(
        NActors::TActorId replyTo,
        Ydb::UdfStore::ModuleType type,
        TString namePrefix,
        ui64 limit)
        : ReplyTo_(replyTo)
        , Type_(type)
        , NamePrefix_(std::move(namePrefix))
        , Limit_(limit ? limit : 1000)
    {}

    void Bootstrap() {
        Become(&TUdfModuleListActor::StateMain);
        if (!TUdfStoreRuntimeFlags::Enabled()) {
            return ReplyError(Ydb::StatusIds::UNSUPPORTED, "UDF Store is disabled");
        }
        const TString typeStr = ModuleTypeToString(Type_);
        const bool filterType = !typeStr.empty();
        const bool filterPrefix = !NamePrefix_.empty();
        auto request = MakeYqlRequest(
            NTableQuery::BuildListModulesQuery(GetModulesTablePath(), filterType, filterPrefix, Limit_),
            true);
        NTableQuery::SetListModulesParams(request, typeStr, NamePrefix_);
        ExecuteYql(SelfId(), std::move(request));
    }

private:
    void ReplyError(Ydb::StatusIds::StatusCode status, const TString& message) {
        auto ev = MakeHolder<TEvListModulesResult>();
        ev->Status = status;
        ev->ErrorMessage = message;
        Send(ReplyTo_, ev.Release());
        PassAway();
    }

    void HandleQueryResult(NMetadata::NRequest::TEvRequestResult<TDialog>::TPtr& ev) {
        TVector<NTableQuery::TModuleSourceRow> rows;
        if (!NTableQuery::ParseModuleRowsResponse(ev->Get()->GetResult(), rows)) {
            ReplyError(Ydb::StatusIds::INTERNAL_ERROR, "failed to parse modules list");
            return;
        }
        auto result = MakeHolder<TEvListModulesResult>();
        for (const auto& row : rows) {
            FillModuleDescription(row, result->Result.add_modules());
        }
        Send(ReplyTo_, result.Release());
        PassAway();
    }

    void HandleQueryFailed(NMetadata::NRequest::TEvRequestFailed::TPtr& ev) {
        ReplyError(Ydb::StatusIds::INTERNAL_ERROR, ev->Get()->GetErrorMessage());
    }

    STRICT_STFUNC(StateMain,
        hFunc(NMetadata::NRequest::TEvRequestResult<TDialog>, HandleQueryResult);
        hFunc(NMetadata::NRequest::TEvRequestFailed, HandleQueryFailed);
    )
};

} // namespace

NActors::IActor* CreateUdfModuleUploadActor(NActors::TActorId replyTo, TUploadModuleParams params) {
    return new TUdfModuleUploadActor(replyTo, std::move(params));
}

NActors::IActor* CreateUdfModuleDeleteActor(
    NActors::TActorId replyTo,
    TString md5,
    TString name,
    Ydb::UdfStore::ModuleType type)
{
    return new TUdfModuleDeleteActor(replyTo, std::move(md5), std::move(name), type);
}

NActors::IActor* CreateUdfModuleDescribeActor(
    NActors::TActorId replyTo,
    TString md5,
    TString name,
    Ydb::UdfStore::ModuleType type)
{
    return new TUdfModuleDescribeActor(replyTo, std::move(md5), std::move(name), type);
}

NActors::IActor* CreateUdfModuleListActor(
    NActors::TActorId replyTo,
    Ydb::UdfStore::ModuleType type,
    TString namePrefix,
    ui64 limit)
{
    return new TUdfModuleListActor(replyTo, type, std::move(namePrefix), limit);
}

} // namespace NKikimr::NUdfStore
