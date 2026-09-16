DLL()

INCLUDE(${ARCADIA_ROOT}/ydb/udfs/wasm/common/webassembly_udf.inc)

STRIP()

SRCS(
    codec.cpp
    main.cpp
)

PEERDIR(
    library/cpp/protobuf/json
    library/cpp/protobuf/yql
    ydb/services/udf_store/wasm/abi
    ydb/services/udf_store/wasm/object_framework
)

END()
