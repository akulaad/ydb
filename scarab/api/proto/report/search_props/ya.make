PROTO_LIBRARY()

EXCLUDE_TAGS(GO_PROTO JAVA_PROTO)

PEERDIR(
    scarab/api/proto/common
    scarab/api/proto/report/search_props/UPPER
    scarab/api/proto/report/search_props/common
    search/common/resource_usage_stats/proto/small_version
    yt/yt_proto/yt/formats
)

SRCS(
    AUTO2.proto
    FASTRES2.proto
    IMAGESP.proto
    IMAGESQUICKP.proto
    IMAGESULTRAP.proto
    QUICK.proto
    REPORT.proto
    UNITSCONVERTER.proto
    UPPER.proto
    VIDEOP.proto
    VIDEOQUICKP.proto
    WEB.proto
    WIZARD.proto
)

END()
