PROTO_LIBRARY()

EXCLUDE_TAGS(GO_PROTO JAVA_PROTO)

PEERDIR(
    bigrt/lib/serializable_profile/proto
    quality/user_sessions/rt/lib/search_profile/proto
    quality/user_sessions/rt/protos/detail/feeds
    quality/user_sessions/rt/protos/detail/metrika
)

SRCS(
    request_profile.proto
)

END()
