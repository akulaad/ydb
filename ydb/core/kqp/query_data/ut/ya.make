UNITTEST_FOR(ydb/core/kqp/query_data)

SIZE(SMALL)

SRCS(
    kqp_predictor_ut.cpp
)

PEERDIR(
    ydb/core/kqp/common
    ydb/core/kqp/expr_nodes
    ydb/core/kqp/query_data
    yql/essentials/ast
    yql/essentials/core
    yql/essentials/core/expr_nodes
    yql/essentials/minikql
    yql/essentials/public/udf
    yql/essentials/public/udf/service/exception_policy
    yql/essentials/sql/pg_dummy
    library/cpp/testing/unittest
)

YQL_LAST_ABI_VERSION()

END()
