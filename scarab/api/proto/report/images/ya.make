PROTO_LIBRARY()

EXCLUDE_TAGS(GO_PROTO JAVA_PROTO)

PEERDIR(
    scarab/api/proto/common
    yt/yt_proto/yt/formats
)

SRCS(
    bubble.proto
    cbir.proto
    doc_markers.proto
    images_document.proto
    relev.proto
)

END()
