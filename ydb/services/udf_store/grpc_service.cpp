#include "grpc_service.h"

#include <ydb/core/grpc_services/grpc_helper.h>
#include <ydb/core/grpc_services/base/base.h>
#include <ydb/library/grpc/server/grpc_method_setup.h>

namespace NKikimr::NUdfStore {

TUdfStoreGRpcService::TUdfStoreGRpcService(
    NActors::TActorSystem* actorSystem,
    TIntrusivePtr<::NMonitoring::TDynamicCounters> counters,
    NActors::TActorId grpcRequestProxyId)
    : ActorSystem_(actorSystem)
    , Counters_(std::move(counters))
    , GRpcRequestProxyId_(grpcRequestProxyId)
{
}

TUdfStoreGRpcService::~TUdfStoreGRpcService() = default;

void TUdfStoreGRpcService::InitService(grpc::ServerCompletionQueue* cq, NYdbGrpc::TLoggerPtr logger) {
    CQ_ = cq;
    SetupIncomingRequests(std::move(logger));
}

void TUdfStoreGRpcService::SetupIncomingRequests(NYdbGrpc::TLoggerPtr logger) {
    using namespace Ydb::UdfStore;
    using namespace NGRpcService;
    auto getCounterBlock = CreateCounterCb(Counters_, ActorSystem_);

#ifdef SETUP_UDF_STORE_METHOD
#error SETUP_UDF_STORE_METHOD macro already defined
#endif

#define SETUP_UDF_STORE_METHOD(methodName, methodCallback, rlMode, requestType, auditMode) \
    SETUP_METHOD(methodName, methodCallback, rlMode, requestType, udf_store, auditMode, \
        EEmptyDatabaseMode::EmptyDatabaseForbidden)

    SETUP_UDF_STORE_METHOD(UploadModule, DoUploadModuleRequest, RLMODE(Rps), UDF_STORE_UPLOAD_MODULE,
        TAuditMode::Modifying(TAuditMode::TLogClassConfig::Ddl));
    SETUP_UDF_STORE_METHOD(DeleteModule, DoDeleteModuleRequest, RLMODE(Rps), UDF_STORE_DELETE_MODULE,
        TAuditMode::Modifying(TAuditMode::TLogClassConfig::Ddl));
    SETUP_UDF_STORE_METHOD(DescribeModule, DoDescribeModuleRequest, RLMODE(Rps), UDF_STORE_DESCRIBE_MODULE,
        TAuditMode::NonModifying());
    SETUP_UDF_STORE_METHOD(ListModules, DoListModulesRequest, RLMODE(Rps), UDF_STORE_LIST_MODULES,
        TAuditMode::NonModifying());

#undef SETUP_UDF_STORE_METHOD
}

} // namespace NKikimr::NUdfStore
