PROTO_LIBRARY()

EXCLUDE_TAGS(GO_PROTO JAVA_PROTO)

PEERDIR(
    quality/user_sessions/createlib/qb3/proto/rt
    quality/user_sessions/createlib/qb3/proto/util
    scarab/api/proto/common
    scarab/api/proto/report/search_props
    yt/yt_proto/yt/formats
)

SRCS(
    suggest.proto
    web_middle_reqans.proto
)

END()
