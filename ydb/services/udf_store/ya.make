LIBRARY()
YQL_LAST_ABI_VERSION()

SRCS(
    service.cpp
    store_initializer.cpp
    artifact_table_initializer.cpp
    kv_body_store.cpp
    kv_body_write_actor.cpp
    table_query.cpp
    runtime_flags.cpp
    module_api_actors.cpp
    wasm_compile_actor.cpp
    wasm_library_compile_actor.cpp
    wasm_artifact_load_actor.cpp
    blob_chunks.cpp
    grpc_service.cpp
    rpc_udf_store.cpp
)

PEERDIR(
    ydb/library/actors/core
    ydb/core/base
    ydb/core/kqp/common
    ydb/core/keyvalue
    ydb/core/tx/scheme_cache
    ydb/core/grpc_services
    ydb/library/aclib
    ydb/library/grpc/server
    ydb/library/table_creator
    ydb/public/api/grpc
    ydb/public/api/protos
    ydb/services/udf_store/metadata_subscription
    ydb/services/udf_store/wasm
    ydb/services/metadata/request
    ydb/services/metadata/abstract
    ydb/services/metadata/manager
    ydb/services/metadata
    ydb/services/ydb
    yql/essentials/minikql
    library/cpp/digest/md5
    library/cpp/json
)

END()

RECURSE(
    wasm
)

RECURSE_FOR_TESTS(
    ut
)
