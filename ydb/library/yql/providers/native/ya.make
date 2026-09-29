LIBRARY()

SRCS(
    actors/read_actor.cpp
)

PEERDIR(
    ydb/library/yql/dq/actors/compute
    yql/essentials/minikql/computation
    yql/essentials/public/udf/arrow
)

YQL_LAST_ABI_VERSION()
END()

RECURSE_FOR_TESTS(ut)
