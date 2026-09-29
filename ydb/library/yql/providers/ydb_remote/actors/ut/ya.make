UNITTEST_FOR(ydb/library/yql/providers/ydb_remote/actors)

SRCS(read_stream_ut.cpp)
PEERDIR(
    library/cpp/testing/unittest
    yql/essentials/public/udf/service/stub
    yql/essentials/sql/pg_dummy
)
ADDINCL(contrib/libs/flatbuffers/include)

YQL_LAST_ABI_VERSION()
END()
