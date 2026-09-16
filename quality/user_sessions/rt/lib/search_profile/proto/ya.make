PROTO_LIBRARY()

EXCLUDE_TAGS(GO_PROTO JAVA_PROTO)

PEERDIR(
    quality/user_sessions/createlib/qb3/proto
    quality/user_sessions/createlib/qb3/proto/rt
    quality/user_sessions/rt/lib/rab_proto
    quality/user_sessions/rt/protos
    quality/user_sessions/rt/protos/detail/search
)

SRCS(
    unpacked_profile.proto
)

END()
