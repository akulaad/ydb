PROTO_LIBRARY()

EXCLUDE_TAGS(GO_PROTO JAVA_PROTO)

PEERDIR(
    quality/user_sessions/rt/protos/common
    scarab/api/proto/report/rearr
    scarab/api/proto/report/relev
    scarab/api/proto/report/search_props
    yt/yt_proto/yt/core/yson/proto
    yt/yt_proto/yt/formats
)

SRCS(
    profile.proto
)

END()
