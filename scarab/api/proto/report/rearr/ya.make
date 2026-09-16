PROTO_LIBRARY()

EXCLUDE_TAGS(GO_PROTO JAVA_PROTO)

PEERDIR(
    scarab/api/proto/common
    yt/yt_proto/yt/formats
)

SRCS(
    off.proto
    root.proto
    scheme_blender.proto
    scheme_blender_storages.proto
    scheme_local.proto
)

END()
