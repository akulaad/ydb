DLL()

INCLUDE(${ARCADIA_ROOT}/ydb/udfs/wasm/common/webassembly_udf.inc)

STRIP()

SRCS(
    main.cpp
    unpack.cpp
)

PEERDIR(
    library/cpp/blockcodecs/core
    library/cpp/blockcodecs/codecs/zstd
    library/cpp/protobuf/json
    ydb/services/udf_store/wasm/abi
    ydb/udfs/wasm/reef_profile/proto
)

END()
