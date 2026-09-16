PROTO_LIBRARY()

EXCLUDE_TAGS(GO_PROTO JAVA_PROTO)

PEERDIR(
    yt/yt_proto/yt/formats
)

SRCS(
    all.proto
    compat.proto
    generic.proto
    options.proto
)

END()
