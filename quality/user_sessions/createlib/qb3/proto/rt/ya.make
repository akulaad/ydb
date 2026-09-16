PROTO_LIBRARY()

EXCLUDE_TAGS(GO_PROTO JAVA_PROTO)

PEERDIR(
    library/cpp/yaff/proto
)

SRCS(
    util.proto
)

END()
