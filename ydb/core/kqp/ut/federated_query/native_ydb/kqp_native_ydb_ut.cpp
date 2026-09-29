#include <ydb/core/kqp/ut/common/kqp_ut_common.h>
#include <ydb/core/kqp/ut/federated_query/common/common.h>
#include <ydb/library/yql/providers/s3/actors/yql_s3_actors_factory_impl.h>

#include <library/cpp/testing/unittest/registar.h>

namespace NKikimr::NKqp {
namespace {

using namespace NYdb;
using namespace NYdb::NQuery;
using namespace NFederatedQueryTest;

struct TNativeYdbFixture {
    TKikimrRunner Remote{TKikimrSettings().SetDomainRoot("Remote").SetWithSampleTables(false)};
    std::shared_ptr<TKikimrRunner> Consumer;

    explicit TNativeYdbFixture(bool enabled = true) {
        NKikimrConfig::TAppConfig config;
        config.MutableFeatureFlags()->SetEnableNativeYdbProvider(enabled);
        config.MutableQueryServiceConfig()->SetAllExternalDataSourcesAreAvailable(false);
        config.MutableQueryServiceConfig()->AddAvailableExternalDataSources("Ydb");
        // No ConnectorClient is provided, even when testing the disabled native flag.
        Consumer = MakeKikimrRunner(false, nullptr, nullptr, config,
            NYql::NDq::CreateS3ActorsFactory(),
            {.DomainRoot = "Consumer", .CredentialsFactory = CreateCredentialsFactory("root@builtin")});

        auto remote = Remote.GetQueryClient();
        const auto create = remote.ExecuteQuery(
            "CREATE TABLE `/Remote/items` (Key Uint64 NOT NULL, Value Utf8, Flag Bool, PRIMARY KEY (Key));",
            TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_C(create.IsSuccess(), create.GetIssues().ToString());

        auto consumer = Consumer->GetQueryClient();
        const auto secret = consumer.ExecuteQuery(
            "CREATE SECRET remote_token WITH (value = 'root@builtin');",
            TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_C(secret.IsSuccess(), secret.GetIssues().ToString());

        const TString source = TStringBuilder()
            << "CREATE EXTERNAL DATA SOURCE remote_db WITH (SOURCE_TYPE='Ydb', LOCATION='"
            << Remote.GetEndpoint() << "', DATABASE_NAME='/Remote', USE_TLS='false', "
            << "AUTH_METHOD='TOKEN', TOKEN_SECRET_PATH='remote_token');";
        const auto eds = consumer.ExecuteQuery(source, TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_C(eds.IsSuccess(), eds.GetIssues().ToString());
    }

    void Populate() {
        auto remote = Remote.GetQueryClient();
        const auto result = remote.ExecuteQuery(
            "UPSERT INTO `/Remote/items` (Key, Value, Flag) VALUES (1u, 'one', true), (2u, 'two', false), (3u, NULL, NULL);",
            TTxControl::BeginTx().CommitTx()).ExtractValueSync();
        UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
    }

    TExecuteQueryResult Read(const TString& sql) {
        return Consumer->GetQueryClient().ExecuteQuery(sql, TTxControl::BeginTx().CommitTx(),
            TExecuteQuerySettings().ClientTimeout(TDuration::Seconds(30))).ExtractValueSync();
    }
};

} // namespace

Y_UNIT_TEST_SUITE(KqpNativeYdb) {
    Y_UNIT_TEST(ReadWithoutConnector) {
        TNativeYdbFixture fixture;
        fixture.Populate();
        const auto result = fixture.Read("SELECT Key, Value FROM remote_db.`items` ORDER BY Key;");
        UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
        UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 1);
        auto rows = result.GetResultSetParser(0);
        UNIT_ASSERT_VALUES_EQUAL(rows.RowsCount(), 3);
        UNIT_ASSERT(rows.TryNextRow());
        UNIT_ASSERT_VALUES_EQUAL(rows.ColumnParser("Key").GetUint64(), 1);
        UNIT_ASSERT_VALUES_EQUAL(*rows.ColumnParser("Value").GetOptionalUtf8(), "one");
        UNIT_ASSERT(rows.TryNextRow());
        UNIT_ASSERT_VALUES_EQUAL(rows.ColumnParser("Key").GetUint64(), 2);
        UNIT_ASSERT_VALUES_EQUAL(*rows.ColumnParser("Value").GetOptionalUtf8(), "two");
        UNIT_ASSERT(rows.TryNextRow());
        UNIT_ASSERT_VALUES_EQUAL(rows.ColumnParser("Key").GetUint64(), 3);
        UNIT_ASSERT(!rows.ColumnParser("Value").GetOptionalUtf8());
        UNIT_ASSERT(!rows.TryNextRow());
    }

    Y_UNIT_TEST(FilterAndProjectionStayCorrectLocally) {
        TNativeYdbFixture fixture;
        fixture.Populate();
        const auto result = fixture.Read(
            "SELECT Value FROM remote_db.`items` WHERE Key = 2u;");
        UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
        auto rows = result.GetResultSetParser(0);
        UNIT_ASSERT_VALUES_EQUAL(rows.ColumnsCount(), 1);
        UNIT_ASSERT_VALUES_EQUAL(rows.RowsCount(), 1);
        UNIT_ASSERT(rows.TryNextRow());
        UNIT_ASSERT_VALUES_EQUAL(*rows.ColumnParser("Value").GetOptionalUtf8(), "two");
    }

    Y_UNIT_TEST(BoolAndNullMatchQueryServiceFormat) {
        TNativeYdbFixture fixture;
        fixture.Populate();
        const auto result = fixture.Read("SELECT Flag FROM remote_db.`items` ORDER BY Key;");
        UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
        auto rows = result.GetResultSetParser(0);
        UNIT_ASSERT_VALUES_EQUAL(rows.RowsCount(), 3);
        UNIT_ASSERT(rows.TryNextRow());
        UNIT_ASSERT_VALUES_EQUAL(rows.ColumnParser("Flag").GetOptionalBool().value(), true);
        UNIT_ASSERT(rows.TryNextRow());
        UNIT_ASSERT_VALUES_EQUAL(rows.ColumnParser("Flag").GetOptionalBool().value(), false);
        UNIT_ASSERT(rows.TryNextRow());
        UNIT_ASSERT(!rows.ColumnParser("Flag").GetOptionalBool());
    }

    Y_UNIT_TEST(EmptyTableCompletes) {
        TNativeYdbFixture fixture;
        const auto result = fixture.Read("SELECT * FROM remote_db.`items`;");
        UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
        UNIT_ASSERT_VALUES_EQUAL(result.GetResultSet(0).RowsCount(), 0);
    }

    Y_UNIT_TEST(CountWithoutProjectedColumns) {
        TNativeYdbFixture fixture;
        fixture.Populate();
        const auto result = fixture.Read("SELECT COUNT(*) AS Total FROM remote_db.`items`;");
        UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
        auto rows = result.GetResultSetParser(0);
        UNIT_ASSERT_VALUES_EQUAL(rows.RowsCount(), 1);
        UNIT_ASSERT(rows.TryNextRow());
        UNIT_ASSERT_VALUES_EQUAL(rows.ColumnParser("Total").GetUint64(), 3);
    }

    Y_UNIT_TEST(MissingRemoteTableFails) {
        TNativeYdbFixture fixture;
        const auto result = fixture.Read("SELECT * FROM remote_db.`does_not_exist`;");
        UNIT_ASSERT(!result.IsSuccess());
        UNIT_ASSERT_STRING_CONTAINS(result.GetIssues().ToString(), "DescribeTable failed");
    }

    Y_UNIT_TEST(DisabledFlagDoesNotSilentlyUseNative) {
        TNativeYdbFixture fixture(false);
        const auto result = fixture.Read("SELECT * FROM remote_db.`items`;");
        UNIT_ASSERT(!result.IsSuccess());
        UNIT_ASSERT_STRING_CONTAINS(result.GetIssues().ToString(), "generic");
    }

    Y_UNIT_TEST(WritesAreRejected) {
        TNativeYdbFixture fixture;
        for (const TString mode : {"INSERT", "UPSERT"}) {
            const auto result = fixture.Read(
                mode + " INTO remote_db.`items` (Key, Value) VALUES (1u, 'one');");
            UNIT_ASSERT(!result.IsSuccess());
            UNIT_ASSERT_STRING_CONTAINS(result.GetIssues().ToString(), "not supported");
        }
    }
}

} // namespace NKikimr::NKqp
