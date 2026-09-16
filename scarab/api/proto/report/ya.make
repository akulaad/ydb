PROTO_LIBRARY()

EXCLUDE_TAGS(GO_PROTO JAVA_PROTO)

PEERDIR(
    scarab/api/proto/common
    scarab/api/proto/report/images
    scarab/api/proto/report/rearr
    scarab/api/proto/report/relev
    scarab/api/proto/report/search_props
    yt/yt_proto/yt/formats
)

SRCS(
    all.proto
    common.proto
    doc_markers.proto
)

END()
