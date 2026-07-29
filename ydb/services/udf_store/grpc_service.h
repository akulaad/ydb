#pragma once

#include <ydb/library/actors/core/actorsystem_fwd.h>
#include <ydb/library/actors/core/actorid.h>
#include <ydb/library/grpc/server/grpc_server.h>
#include <ydb/public/api/grpc/ydb_udf_store_v1.grpc.pb.h>

namespace NKikimr {
namespace NGRpcService {
class IRequestOpCtx;
class IFacilityProvider;
} // namespace NGRpcService
} // namespace NKikimr

namespace NKikimr::NUdfStore {

void DoUploadModuleRequest(std::unique_ptr<NGRpcService::IRequestOpCtx> p, const NGRpcService::IFacilityProvider& f);
void DoDeleteModuleRequest(std::unique_ptr<NGRpcService::IRequestOpCtx> p, const NGRpcService::IFacilityProvider& f);
void DoDescribeModuleRequest(std::unique_ptr<NGRpcService::IRequestOpCtx> p, const NGRpcService::IFacilityProvider& f);
void DoListModulesRequest(std::unique_ptr<NGRpcService::IRequestOpCtx> p, const NGRpcService::IFacilityProvider& f);

class TUdfStoreGRpcService
    : public NYdbGrpc::TGrpcServiceBase<Ydb::UdfStore::V1::UdfStoreService>
{
public:
    TUdfStoreGRpcService(
        NActors::TActorSystem* actorSystem,
        TIntrusivePtr<::NMonitoring::TDynamicCounters> counters,
        NActors::TActorId grpcRequestProxyId);
    ~TUdfStoreGRpcService();

    void InitService(grpc::ServerCompletionQueue* cq, NYdbGrpc::TLoggerPtr logger) override;

private:
    void SetupIncomingRequests(NYdbGrpc::TLoggerPtr logger);

    NActors::TActorSystem* ActorSystem_ = nullptr;
    TIntrusivePtr<::NMonitoring::TDynamicCounters> Counters_;
    NActors::TActorId GRpcRequestProxyId_;
    grpc::ServerCompletionQueue* CQ_ = nullptr;
};

} // namespace NKikimr::NUdfStore
