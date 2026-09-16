PROTO_LIBRARY()

EXCLUDE_TAGS(GO_PROTO JAVA_PROTO)

PEERDIR(
    quality/user_sessions/createlib/qb3/proto/rt
    quality/user_sessions/rt/protos/common
    yt/yt_proto/yt/formats
)

SRCS(
    shows_state.proto
)

END()
