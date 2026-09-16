PROTO_LIBRARY()

EXCLUDE_TAGS(GO_PROTO JAVA_PROTO)

PEERDIR(
    yt/yt_proto/yt/formats
)

SRCS(
    model.proto
    request_data.proto
    train_params.proto
    util.proto
)

END()
