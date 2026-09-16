PROTO_LIBRARY()

EXCLUDE_TAGS(GO_PROTO JAVA_PROTO)

PEERDIR(
    scarab/api/proto/common
    yt/yt_proto/yt/formats
)

SRCS(
    conf_rearr.proto
)

END()
