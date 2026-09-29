#pragma once

#include "yql_ydb_remote_provider.h"

#include <ydb/public/api/protos/ydb_value.pb.h>
#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/table/table.h>
#include <yql/essentials/core/yql_graph_transformer.h>
#include <yql/essentials/providers/common/transform/yql_visit.h>

namespace NYql::NYdbRemote {

struct TCluster {
    TString Endpoint;
    TString Database;
    bool UseTls = false;
};

struct TTable {
    const TStructExprType* RowType = nullptr;
    TVector<TString> ColumnOrder;
    THashMap<TString, Ydb::Type> ColumnTypes;
};

struct TState : public TThrRefBase {
    using TPtr = TIntrusivePtr<TState>;
    using TTableKey = std::pair<TString, TString>;

    TState(TTypeAnnotationContext* types, const NYdb::TDriver& driver,
           IStructuredTokenCredentialsFactory::TPtr credentialsFactory)
        : Types(types)
        , Driver(driver)
        , CredentialsFactory(std::move(credentialsFactory))
    {
    }

    TTypeAnnotationContext* const Types;
    const NYdb::TDriver Driver;
    const IStructuredTokenCredentialsFactory::TPtr CredentialsFactory;
    THashMap<TString, TCluster> Clusters;
    THashMap<TString, TString> Tokens;
    THashSet<TString> ValidClusters;
    THashMap<TTableKey, TTable> Tables;
};

void AddCluster(TState& state, const TString& name, const THashMap<TString, TString>& properties);
const TTypeAnnotationNode* ParseColumnType(const Ydb::Type& type, TExprContext& ctx);
THolder<IGraphTransformer> CreateLoadMetadataTransformer(TState::TPtr state);
THolder<TVisitorTransformerBase> CreateTypeAnnotationTransformer(TState::TPtr state);
THolder<IGraphTransformer> CreatePhysicalOptimizer();
THolder<IDqIntegration> CreateDqIntegration(TState::TPtr state);

} // namespace NYql::NYdbRemote
