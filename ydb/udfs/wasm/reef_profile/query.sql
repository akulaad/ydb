/* syntax version 1 */
-- ReefProfile WASM: Parse* returns JSON String, not native Struct.
-- Proto schema is the full Arcadia import-closure (scarab/search nested types).
-- packed_*_patch must be empty (delta codecs are not linked in this guest).
-- Upload sdk + ReefProfile WASM modules before running.

-- TFeedsOutputCache { OutputShowBlockIds = ["b1"] } protobin, codec = identity "null".
$feeds = String::HexDecode("0a026231");

$row = AsStruct(
    "u1" AS user_id,
    "r1" AS reqid,
    "null" AS codec,
    NULL AS packed_blockstat_data,
    NULL AS packed_blockstat_data_patch,
    NULL AS packed_redir_data,
    NULL AS packed_redir_data_patch,
    NULL AS packed_clicks_data,
    NULL AS packed_clicks_data_patch,
    NULL AS packed_common_data,
    NULL AS packed_common_data_patch,
    NULL AS packed_request_clicks_common_info,
    NULL AS packed_request_clicks_common_info_patch,
    NULL AS packed_techs_data,
    NULL AS packed_techs_data_patch,
    $feeds AS packed_feeds_output_cache,
    NULL AS packed_feeds_output_cache_patch,
    NULL AS packed_tamus_worked_rules,
    NULL AS packed_tamus_worked_rules_patch
);

$bad = AsStruct(
    "u1" AS user_id,
    "r1" AS reqid,
    "null" AS codec,
    "not-protobuf" AS packed_blockstat_data,
    NULL AS packed_blockstat_data_patch,
    NULL AS packed_redir_data,
    NULL AS packed_redir_data_patch,
    NULL AS packed_clicks_data,
    NULL AS packed_clicks_data_patch,
    NULL AS packed_common_data,
    NULL AS packed_common_data_patch,
    NULL AS packed_request_clicks_common_info,
    NULL AS packed_request_clicks_common_info_patch,
    NULL AS packed_techs_data,
    NULL AS packed_techs_data_patch,
    NULL AS packed_feeds_output_cache,
    NULL AS packed_feeds_output_cache_patch,
    NULL AS packed_tamus_worked_rules,
    NULL AS packed_tamus_worked_rules_patch
);

-- TReefRequestProfileProto { UserID="u1", RequestID="r1" } protobin.
$wire_keys = String::HexDecode("0a02753112027231");

SELECT
    ReefProfile::ParseReefRequestProfile($row, NULL) AS ok,
    ReefProfile::ParseReefRequestProfile($bad, AsDict(AsTuple("null_on_exception", 1l))) AS null_on_error,
    ReefProfile::ParseReefRequestProfileProto(NULL, NULL) AS proto_null,
    ReefProfile::ParseReefRequestProfileProto("null", NULL) AS proto_null_str,
    ReefProfile::ParseReefRequestProfileProto($wire_keys, NULL) AS proto_keys,
    ReefProfile::ParseReefRequestProfileProto(
        "not-protobuf",
        AsDict(AsTuple("null_on_exception", 1l))) AS proto_null_on_error;
