PROTO_LIBRARY()

EXCLUDE_TAGS(GO_PROTO JAVA_PROTO)

PEERDIR(
    quality/user_sessions/createlib/qb3/proto/rt
    quality/user_sessions/rt/protos/public/metrika
)

SRCS(
    request_profile.proto
)

END()
