#include "yql_ydb_remote_provider_impl.h"

#include <yql/essentials/core/sql_types/block.h>

#include <ydb/library/yql/providers/ydb_remote/expr_nodes/yql_ydb_remote_expr_nodes.h>
#include <yql/essentials/core/yql_expr_optimize.h>
#include <yql/essentials/providers/common/provider/yql_provider.h>

#include <library/cpp/threading/future/future.h>

#include <atomic>
#include <optional>

namespace NYql::NYdbRemote {
namespace {

using namespace NNodes;

bool ParseRead(const TYdbRemoteRead& read, TString& table, TExprContext& ctx) {
    const auto& node = read.Ref();
    if (node.ChildrenSize() < 3 || node.ChildrenSize() > 5) {
        ctx.AddError(TIssue(ctx.GetPosition(read.Pos()), "Native YDB supports a single table read"));
        return false;
    }
    // SQL emits a direct Key in some translation modes, while KQP's external
    // table rewrite and query-mode SQL wrap the same key in MrTableConcat.
    const auto* key = node.Child(2);
    if (key->IsCallable("MrTableConcat")) {
        if (key->ChildrenSize() != 1) {
            ctx.AddError(TIssue(ctx.GetPosition(read.Pos()), "Native YDB supports a single table read"));
            return false;
        }
        key = key->Child(0);
    }
    if (!key->IsCallable("Key") || key->ChildrenSize() != 1 ||
        !key->Head().IsList() || key->Head().ChildrenSize() != 2 ||
        !key->Head().Head().IsAtom("table") || !key->Head().Tail().IsCallable("String") ||
        key->Head().Tail().ChildrenSize() != 1 || !key->Head().Tail().Head().IsAtom()) {
        ctx.AddError(TIssue(ctx.GetPosition(read.Pos()), "Native YDB requires a literal table path; ranges and table functions are unsupported"));
        return false;
    }
    if (node.ChildrenSize() > 4 && (!node.Child(4)->IsList() || node.Child(4)->ChildrenSize())) {
        ctx.AddError(TIssue(ctx.GetPosition(read.Pos()), "Native YDB read settings, views and explicit schemas are not supported yet"));
        return false;
    }
    table = key->Head().Tail().Head().Content();
    if (table.empty() || table.Contains('\0')) {
        ctx.AddError(TIssue(ctx.GetPosition(read.Pos()), "Native YDB requires a nonempty table path"));
        return false;
    }
    return true;
}

class TLoadMetadataTransformer final : public TGraphTransformerBase {
    struct TPending {
        std::atomic<bool> Cancelled = false;
        std::optional<NYdb::NTable::TTableDescription> Description;
        TString Error;
    };

public:
    explicit TLoadMetadataTransformer(TState::TPtr state)
        : State_(std::move(state))
    {
    }

    ~TLoadMetadataTransformer() override {
        Rewind();
    }

    TStatus DoTransform(TExprNode::TPtr input, TExprNode::TPtr& output, TExprContext& ctx) override {
        output = input;
        if (ctx.Step.IsDone(TExprStep::LoadTablesMetadata)) {
            return TStatus::Ok;
        }
        const auto reads = FindReads(input);
        std::vector<NThreading::TFuture<void>> futures;
        for (const auto& node : reads) {
            const TYdbRemoteRead read(node);
            TString table;
            if (!ParseRead(read, table, ctx)) {
                return TStatus::Error;
            }
            TState::TTableKey key(read.DataSource().Cluster().StringValue(), table);
            if (State_->Tables.contains(key) || Pending_.contains(key)) {
                continue;
            }
            auto pending = std::make_shared<TPending>();
            Pending_.emplace(key, pending);
            try {
                futures.emplace_back(Describe(key, pending));
            } catch (...) {
                // SDK credential factories can throw; their messages must not expose credentials.
                ctx.AddError(TIssue(ctx.GetPosition(read.Pos()), "Native YDB metadata client initialization failed"));
                Rewind();
                return TStatus::Error;
            }
        }
        if (futures.empty()) {
            return Rewrite(input, output, ctx);
        }
        AsyncFuture_ = NThreading::WaitExceptionOrAll(futures);
        return TStatus::Async;
    }

    NThreading::TFuture<void> DoGetAsyncFuture(const TExprNode&) override {
        return AsyncFuture_;
    }

    TStatus DoApplyAsyncChanges(TExprNode::TPtr input, TExprNode::TPtr& output, TExprContext& ctx) override {
        output = input;
        AsyncFuture_.GetValue();
        for (const auto& [key, pending] : Pending_) {
            if (pending->Error) {
                ctx.AddError(TIssue({}, pending->Error));
                return TStatus::Error;
            }
            if (!pending->Description || pending->Description->GetStoreType() != NYdb::NTable::EStoreType::Row) {
                ctx.AddError(TIssue({}, "Native YDB currently supports only row tables"));
                return TStatus::Error;
            }
            TTable table;
            TVector<const TItemExprType*> items;
            for (const auto& column : pending->Description->GetTableColumns()) {
                Ydb::Type type = column.Type.GetProto();
                if (column.NotNull.value_or(false) && type.has_optional_type()) {
                    type = Ydb::Type(type.optional_type().item());
                }
                const auto* annotation = ParseColumnType(type, ctx);
                if (!annotation || column.Name == BlockLengthColumnName) {
                    ctx.AddError(TIssue({}, TStringBuilder() << "Native YDB does not support the type or name of column " << column.Name));
                    return TStatus::Error;
                }
                if (!table.ColumnTypes.emplace(TString(column.Name), std::move(type)).second) {
                    ctx.AddError(TIssue({}, "Native YDB received duplicate column names"));
                    return TStatus::Error;
                }
                table.ColumnOrder.emplace_back(column.Name);
                items.emplace_back(ctx.MakeType<TItemExprType>(column.Name, annotation));
            }
            if (items.empty()) {
                ctx.AddError(TIssue({}, "Native YDB received an empty table schema"));
                return TStatus::Error;
            }
            table.RowType = ctx.MakeType<TStructExprType>(items);
            State_->Tables.emplace(key, std::move(table));
        }
        Pending_.clear();
        return Rewrite(input, output, ctx);
    }

    void Rewind() override {
        for (const auto& [key, pending] : Pending_) {
            Y_UNUSED(key);
            pending->Cancelled = true;
        }
        Pending_.clear();
        AsyncFuture_ = {};
    }

private:
    static TExprNode::TListType FindReads(const TExprNode::TPtr& input) {
        return FindNodes(input, [](const TExprNode::TPtr& node) {
            return TYdbRemoteRead::Match(node.Get()) && node->ChildrenSize() > 1 &&
                TYdbRemoteDataSource::Match(node->Child(1));
        });
    }

    NThreading::TFuture<void> Describe(const TState::TTableKey& key, const std::shared_ptr<TPending>& pending) {
        const auto& cluster = State_->Clusters.at(key.first);
        const auto credentials = State_->CredentialsFactory->Create(State_->Tokens.at(key.first), false);
        auto client = std::make_shared<NYdb::NTable::TTableClient>(State_->Driver,
            NYdb::NTable::TClientSettings()
                .Database(cluster.Database)
                .DiscoveryEndpoint(cluster.Endpoint)
                .DiscoveryMode(NYdb::EDiscoveryMode::Async)
                .SslCredentials(NYdb::TSslCredentials(cluster.UseTls))
                .CredentialsProviderFactory(credentials)
                .SessionPoolSettings(NYdb::NTable::TSessionPoolSettings().MaxActiveSessions(1).MinPoolSize(0).RetryLimit(0)));
        const TString tablePath = key.second.StartsWith('/') ? key.second : cluster.Database + "/" + key.second;
        const auto deadline = TInstant::Now() + TDuration::Seconds(60);
        auto promise = NThreading::NewPromise<void>();
        std::weak_ptr<TPending> weak = pending;
        client->GetSession(NYdb::NTable::TCreateSessionSettings().ClientTimeout(TDuration::Seconds(60))
            .OperationTimeout(TDuration::Seconds(60)))
            .Subscribe([weak, client, promise, deadline, tablePath](const NYdb::NTable::TAsyncCreateSessionResult& future) mutable {
                const auto result = weak.lock();
                if (!result || result->Cancelled) {
                    promise.SetValue();
                    return;
                }
                try {
                    const auto& response = future.GetValue();
                    if (!response.IsSuccess()) {
                        result->Error = TStringBuilder() << "Native YDB metadata session failed: " << response.GetStatus();
                        promise.SetValue();
                        return;
                    }
                    if (TInstant::Now() >= deadline) {
                        result->Error = "Native YDB metadata deadline exceeded";
                        promise.SetValue();
                        return;
                    }
                    auto session = response.GetSession();
                    const auto remaining = deadline - TInstant::Now();
                    session.DescribeTable(tablePath, NYdb::NTable::TDescribeTableSettings()
                        .ClientTimeout(remaining).OperationTimeout(remaining))
                        .Subscribe([weak, client, session, promise](const NYdb::NTable::TAsyncDescribeTableResult& described) mutable {
                            Y_UNUSED(client);
                            Y_UNUSED(session);
                            if (const auto result = weak.lock(); result && !result->Cancelled) {
                                try {
                                    const auto& response = described.GetValue();
                                    if (response.IsSuccess()) {
                                        result->Description = response.GetTableDescription();
                                    } else {
                                        result->Error = TStringBuilder() << "Native YDB DescribeTable failed: " << response.GetStatus();
                                    }
                                } catch (...) {
                                    result->Error = "Native YDB DescribeTable failed";
                                }
                            }
                            promise.SetValue();
                        });
                } catch (...) {
                    result->Error = "Native YDB metadata request failed";
                    promise.SetValue();
                }
            });
        return promise.GetFuture();
    }

    TStatus Rewrite(const TExprNode::TPtr& input, TExprNode::TPtr& output, TExprContext& ctx) {
        TNodeOnNodeOwnedMap replacements;
        for (const auto& node : FindReads(input)) {
            const TYdbRemoteRead read(node);
            TString table;
            if (!ParseRead(read, table, ctx)) {
                return TStatus::Error;
            }
            replacements.emplace(node.Get(), Build<TYdbRemoteReadTable>(ctx, read.Pos())
                .World(read.World())
                .DataSource(read.DataSource())
                .Table().Value(table).Build()
                .Columns(read.Ref().ChildrenSize() > 3 ? read.Ref().ChildPtr(3) : ctx.NewCallable(read.Pos(), "Void", {}))
                .Done().Ptr());
        }
        return RemapExpr(input, output, replacements, ctx, TOptimizeExprSettings(nullptr));
    }

    const TState::TPtr State_;
    THashMap<TState::TTableKey, std::shared_ptr<TPending>> Pending_;
    NThreading::TFuture<void> AsyncFuture_;
};

} // namespace

THolder<IGraphTransformer> CreateLoadMetadataTransformer(TState::TPtr state) {
    return MakeHolder<TLoadMetadataTransformer>(std::move(state));
}

} // namespace NYql::NYdbRemote
