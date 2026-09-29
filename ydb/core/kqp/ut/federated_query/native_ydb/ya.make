UNITTEST_FOR(ydb/core/kqp)

SIZE(MEDIUM)
FORK_SUBTESTS()

SRCS(
    kqp_native_ydb_ut.cpp
)

PEERDIR(
    ydb/core/kqp/ut/common
    ydb/core/kqp/ut/federated_query/common
    ydb/library/yql/providers/s3/actors
    yql/essentials/sql/pg_dummy
)

YQL_LAST_ABI_VERSION()

END()
