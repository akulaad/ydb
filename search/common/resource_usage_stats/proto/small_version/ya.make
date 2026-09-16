PROTO_LIBRARY()

EXCLUDE_TAGS(GO_PROTO JAVA_PROTO)

PEERDIR(
    yt/yt_proto/yt/formats
)

SRCS(
    small_version.proto
)

END()
