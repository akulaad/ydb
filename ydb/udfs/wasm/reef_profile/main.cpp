#include "unpack.h"

#include <ydb/services/udf_store/wasm/abi/bridge.h>
#include <ydb/services/udf_store/wasm/abi/bridge_abi.h>
#include <ydb/services/udf_store/wasm/abi/udf_cpp_abi.h>

#include <util/generic/strbuf.h>
#include <util/generic/string.h>

using namespace NYdb::NUdfStore::NAbi;

namespace {

constexpr TStringBuf OptionalMembers[] = {
    "user_id",
    "reqid",
    "codec",
    "packed_blockstat_data",
    "packed_blockstat_data_patch",
    "packed_redir_data",
    "packed_redir_data_patch",
    "packed_clicks_data",
    "packed_clicks_data_patch",
    "packed_common_data",
    "packed_common_data_patch",
    "packed_request_clicks_common_info",
    "packed_request_clicks_common_info_patch",
    "packed_techs_data",
    "packed_techs_data_patch",
    "packed_feeds_output_cache",
    "packed_feeds_output_cache_patch",
    "packed_tamus_worked_rules",
    "packed_tamus_worked_rules_patch",
};

TString ReadStructString(uint64_t structH, TStringBuf name) {
    const int32_t index = BridgeGetMemberIndex(
        structH,
        reinterpret_cast<uint64_t>(name.data()),
        static_cast<int64_t>(name.size()));
    if (index < 0) {
        return {};
    }
    TBridgeValue value(BridgeGetElement(structH, index), /*owned*/ true);
    if (!value) {
        return {};
    }
    // Traversal exposes Optional<Data> as its payload kind in the bridge ABI.
    const uint64_t offset = BridgeEnsureString(value.Get());
    const int64_t length = BridgeGetStringLen(value.Get());
    return TString(
        reinterpret_cast<const char*>(static_cast<uintptr_t>(offset)),
        static_cast<size_t>(length));
}

bool NullOnException(uint64_t optsH) {
    if (BridgeIsNull(optsH)) {
        return false;
    }
    TBridgeDict dict(BridgeGetOptional(optsH), /*owned*/ true);
    TBridgeValue key = MakeString("null_on_exception", 17);
    TBridgeValue payload = dict.Lookup(key);
    if (payload.Get() == 0) {
        return false;
    }
    if (!payload) {
        return false;
    }
    return BridgeGetInt64(payload.Get()) != 0;
}

void ReturnProfile(uint64_t* result,
    const NUserSessions::NRT::TReefRequestProfileProto& profile,
    const TString& error, bool nullOnException)
{
    if (!error.empty()) {
        if (nullOnException) {
            *result = MakeNull().Release();
        } else {
            ThrowException(error.c_str());
        }
        return;
    }
    const TString json = NWasmReefProfile::ProfileToJson(profile);
    *result = MakeOptional(MakeString(json.data(), static_cast<int64_t>(json.size()))).Release();
}

} // namespace

extern "C" {

//! ReefProfile::ParseReefRequestProfile(row, opts?) -> String?
//! Result is JSON of TReefRequestProfileProto, not the native YQL Struct.
__attribute__((visibility("default"))) void parse_reef_request_profile(
    TExpressionContext* /*ctx*/,
    uint64_t* result,
    uint64_t rowH,
    uint64_t optsH)
{
    const bool nullOnException = NullOnException(optsH);
    TString error;
    if (BridgeIsNull(rowH)) {
        ReturnProfile(result, {}, "ParseReefRequestProfile: expected struct row", nullOnException);
        return;
    }
    const auto profile = NWasmReefProfile::ParseReefRequestProfile(
        error,
        ReadStructString(rowH, OptionalMembers[0]),
        ReadStructString(rowH, OptionalMembers[1]),
        ReadStructString(rowH, OptionalMembers[2]),
        ReadStructString(rowH, OptionalMembers[3]),
        ReadStructString(rowH, OptionalMembers[4]),
        ReadStructString(rowH, OptionalMembers[5]),
        ReadStructString(rowH, OptionalMembers[6]),
        ReadStructString(rowH, OptionalMembers[7]),
        ReadStructString(rowH, OptionalMembers[8]),
        ReadStructString(rowH, OptionalMembers[9]),
        ReadStructString(rowH, OptionalMembers[10]),
        ReadStructString(rowH, OptionalMembers[11]),
        ReadStructString(rowH, OptionalMembers[12]),
        ReadStructString(rowH, OptionalMembers[13]),
        ReadStructString(rowH, OptionalMembers[14]),
        ReadStructString(rowH, OptionalMembers[15]),
        ReadStructString(rowH, OptionalMembers[16]),
        ReadStructString(rowH, OptionalMembers[17]),
        ReadStructString(rowH, OptionalMembers[18]));
    ReturnProfile(result, profile, error, nullOnException);
}

//! ReefProfile::ParseReefRequestProfileProto(wire?, opts?) -> String?
//! Result is JSON of TReefRequestProfileProto, not the native YQL Struct.
__attribute__((visibility("default"))) void parse_reef_request_profile_proto(
    TExpressionContext* /*ctx*/,
    uint64_t* result,
    uint64_t wireH,
    uint64_t optsH)
{
    const bool nullOnException = NullOnException(optsH);
    TString wire;
    if (!BridgeIsNull(wireH)) {
        TBridgeValue inner(BridgeGetOptional(wireH), /*owned*/ true);
        const uint64_t offset = BridgeEnsureString(inner.Get());
        const int64_t length = BridgeGetStringLen(inner.Get());
        wire.assign(
            reinterpret_cast<const char*>(static_cast<uintptr_t>(offset)),
            static_cast<size_t>(length));
    }
    TString error;
    const auto profile = NWasmReefProfile::ParseReefRequestProfileProto(wire, error);
    ReturnProfile(result, profile, error, nullOnException);
}

} // extern "C"
