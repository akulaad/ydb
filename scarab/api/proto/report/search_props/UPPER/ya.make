PROTO_LIBRARY()

EXCLUDE_TAGS(GO_PROTO JAVA_PROTO)

PEERDIR(
    kernel/blender/factor_storage/protos
    kernel/blender/online_learning/protos
    scarab/api/proto/common
    yt/yt_proto/yt/formats
)

SRCS(
    adv.proto
    blender.proto
    images.proto
    misc.proto
    root.proto
    rtx.proto
    stateful.proto
    turbo.proto
    video.proto
    wizards.proto
)

END()
