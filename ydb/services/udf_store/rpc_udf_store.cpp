#include "grpc_service.h"
#include "module_api_actors.h"
#include "runtime_flags.h"

#include <ydb/core/base/auth.h>
#include <ydb/core/grpc_services/rpc_deferrable.h>
#include <ydb/core/grpc_services/base/base.h>
#include <ydb/public/api/protos/ydb_udf_store.pb.h>

namespace NKikimr::NUdfStore {
namespace {

using namespace NActors;
using namespace NGRpcService;
using namespace Ydb;

using TEvUploadModuleRequest = TGrpcRequestOperationCall<
    Ydb::UdfStore::UploadModuleRequest, Ydb::UdfStore::UploadModuleResponse>;
using TEvDeleteModuleRequest = TGrpcRequestOperationCall<
    Ydb::UdfStore::DeleteModuleRequest, Ydb::UdfStore::DeleteModuleResponse>;
using TEvDescribeModuleRequest = TGrpcRequestOperationCall<
    Ydb::UdfStore::DescribeModuleRequest, Ydb::UdfStore::DescribeModuleResponse>;
using TEvListModulesRequest = TGrpcRequestOperationCall<
    Ydb::UdfStore::ListModulesRequest, Ydb::UdfStore::ListModulesResponse>;

bool CheckAdminAccess(IRequestOpCtx* request, Ydb::StatusIds::StatusCode& status, NYql::TIssues& issues) {
    if (!IsAdministrator(AppData(), request->GetInternalToken().Get())) {
        status = Ydb::StatusIds::UNAUTHORIZED;
        issues.AddIssue("UDF Store management is allowed only for administrators");
        return false;
    }
    return true;
}

bool CheckUdfStoreEnabled(Ydb::StatusIds::StatusCode& status, NYql::TIssues& issues) {
    if (!TUdfStoreRuntimeFlags::Enabled()) {
        status = Ydb::StatusIds::UNSUPPORTED;
        issues.AddIssue("UDF Store is disabled");
        return false;
    }
    return true;
}

template <typename TDerived, typename TRequest>
class TUdfStoreRpcBase : public TRpcOperationRequestActor<TDerived, TRequest> {
protected:
    using TBase = TRpcOperationRequestActor<TDerived, TRequest>;

public:
    using TBase::TBase;

    void Bootstrap(const TActorContext& ctx) {
        TBase::Bootstrap(ctx);
        Ydb::StatusIds::StatusCode status = Ydb::StatusIds::STATUS_CODE_UNSPECIFIED;
        NYql::TIssues issues;
        if (!CheckUdfStoreEnabled(status, issues) || !CheckAdminAccess(this->Request_.get(), status, issues)) {
            this->Reply(status, issues, ctx);
            return;
        }
        static_cast<TDerived*>(this)->StartWork(ctx);
    }
};

class TUploadModuleRPC : public TUdfStoreRpcBase<TUploadModuleRPC, TEvUploadModuleRequest> {
public:
    using TBase = TUdfStoreRpcBase<TUploadModuleRPC, TEvUploadModuleRequest>;
    using TBase::TBase;

    void StartWork(const TActorContext&) {
        Become(&TUploadModuleRPC::StateWork);
        const auto* req = GetProtoRequest();
        TUploadModuleParams params;
        params.Type = req->type();
        params.Name = req->name();
        params.Manifest = req->manifest();
        params.Content = req->content();
        params.Version = req->version() ? req->version() : 1;
        params.ExpectedMd5 = req->expected_md5();
        Register(CreateUdfModuleUploadActor(SelfId(), std::move(params)));
    }

private:
    STRICT_STFUNC(StateWork,
        HFunc(TEvUploadModuleResult, Handle);
    )

    void Handle(TEvUploadModuleResult::TPtr& ev, const TActorContext& ctx) {
        if (ev->Get()->Status != Ydb::StatusIds::SUCCESS) {
            NYql::TIssues issues;
            issues.AddIssue(ev->Get()->ErrorMessage);
            return Reply(ev->Get()->Status, issues, ctx);
        }
        return ReplyWithResult(Ydb::StatusIds::SUCCESS, ev->Get()->Result, ctx);
    }
};

class TDeleteModuleRPC : public TUdfStoreRpcBase<TDeleteModuleRPC, TEvDeleteModuleRequest> {
public:
    using TBase = TUdfStoreRpcBase<TDeleteModuleRPC, TEvDeleteModuleRequest>;
    using TBase::TBase;

    void StartWork(const TActorContext&) {
        Become(&TDeleteModuleRPC::StateWork);
        const auto* req = GetProtoRequest();
        Register(CreateUdfModuleDeleteActor(SelfId(), req->md5(), req->name(), req->type()));
    }

private:
    STRICT_STFUNC(StateWork,
        HFunc(TEvDeleteModuleResult, Handle);
    )

    void Handle(TEvDeleteModuleResult::TPtr& ev, const TActorContext& ctx) {
        if (ev->Get()->Status != Ydb::StatusIds::SUCCESS) {
            NYql::TIssues issues;
            issues.AddIssue(ev->Get()->ErrorMessage);
            return Reply(ev->Get()->Status, issues, ctx);
        }
        Ydb::UdfStore::DeleteModuleResult result;
        return ReplyWithResult(Ydb::StatusIds::SUCCESS, result, ctx);
    }
};

class TDescribeModuleRPC : public TUdfStoreRpcBase<TDescribeModuleRPC, TEvDescribeModuleRequest> {
public:
    using TBase = TUdfStoreRpcBase<TDescribeModuleRPC, TEvDescribeModuleRequest>;
    using TBase::TBase;

    void StartWork(const TActorContext&) {
        Become(&TDescribeModuleRPC::StateWork);
        const auto* req = GetProtoRequest();
        Register(CreateUdfModuleDescribeActor(SelfId(), req->md5(), req->name(), req->type()));
    }

private:
    STRICT_STFUNC(StateWork,
        HFunc(TEvDescribeModuleResult, Handle);
    )

    void Handle(TEvDescribeModuleResult::TPtr& ev, const TActorContext& ctx) {
        if (ev->Get()->Status != Ydb::StatusIds::SUCCESS) {
            NYql::TIssues issues;
            issues.AddIssue(ev->Get()->ErrorMessage);
            return Reply(ev->Get()->Status, issues, ctx);
        }
        return ReplyWithResult(Ydb::StatusIds::SUCCESS, ev->Get()->Result, ctx);
    }
};

class TListModulesRPC : public TUdfStoreRpcBase<TListModulesRPC, TEvListModulesRequest> {
public:
    using TBase = TUdfStoreRpcBase<TListModulesRPC, TEvListModulesRequest>;
    using TBase::TBase;

    void StartWork(const TActorContext&) {
        Become(&TListModulesRPC::StateWork);
        const auto* req = GetProtoRequest();
        Register(CreateUdfModuleListActor(SelfId(), req->type(), req->name_prefix(), req->limit()));
    }

private:
    STRICT_STFUNC(StateWork,
        HFunc(TEvListModulesResult, Handle);
    )

    void Handle(TEvListModulesResult::TPtr& ev, const TActorContext& ctx) {
        if (ev->Get()->Status != Ydb::StatusIds::SUCCESS) {
            NYql::TIssues issues;
            issues.AddIssue(ev->Get()->ErrorMessage);
            return Reply(ev->Get()->Status, issues, ctx);
        }
        return ReplyWithResult(Ydb::StatusIds::SUCCESS, ev->Get()->Result, ctx);
    }
};

} // namespace

void DoUploadModuleRequest(std::unique_ptr<NGRpcService::IRequestOpCtx> p, const NGRpcService::IFacilityProvider& f) {
    f.RegisterActor(new TUploadModuleRPC(p.release()));
}

void DoDeleteModuleRequest(std::unique_ptr<NGRpcService::IRequestOpCtx> p, const NGRpcService::IFacilityProvider& f) {
    f.RegisterActor(new TDeleteModuleRPC(p.release()));
}

void DoDescribeModuleRequest(std::unique_ptr<NGRpcService::IRequestOpCtx> p, const NGRpcService::IFacilityProvider& f) {
    f.RegisterActor(new TDescribeModuleRPC(p.release()));
}

void DoListModulesRequest(std::unique_ptr<NGRpcService::IRequestOpCtx> p, const NGRpcService::IFacilityProvider& f) {
    f.RegisterActor(new TListModulesRPC(p.release()));
}

} // namespace NKikimr::NUdfStore
