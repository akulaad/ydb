UNITTEST()

SRCS(
    unpack_ut.cpp
    ../unpack.cpp
)

PEERDIR(
    contrib/libs/zstd
    library/cpp/blockcodecs/core
    library/cpp/blockcodecs/codecs/zstd
    library/cpp/protobuf/json
    ydb/udfs/wasm/reef_profile/proto
)

END()
