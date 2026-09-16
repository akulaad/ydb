PROTO_LIBRARY()

EXCLUDE_TAGS(GO_PROTO JAVA_PROTO)

PEERDIR(
    kernel/blender/factor_storage/protos
    quality/user_sessions/createlib/qb3/proto/rt
    quality/user_sessions/createlib/qb3/proto/util
    scarab/api/proto/common
    scarab/api/proto/report
    scarab/api/proto/report/rearr
    scarab/api/proto/report/relev
    scarab/api/proto/report/search_props
    yt/yt_proto/yt/formats
)

SRCS(
    access.proto
    blockstat.proto
    clicks.proto
    common.proto
    direct.proto
    profile_log.proto
    reqans.proto
    techs.proto
)

END()
