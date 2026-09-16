#include "unpack.h"

#include <ydb/services/udf_store/wasm/abi/bridge.h>
#include <ydb/services/udf_store/wasm/abi/bridge_abi.h>
#include <ydb/services/udf_store/wasm/abi/udf_cpp_abi.h>

#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/yexception.h>

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
    TBridgeDict dict(optsH, /*owned*/ false);
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
    try {
        if (BridgeIsNull(rowH)) {
            ythrow yexception() << "ParseReefRequestProfile: expected struct row";
        }
        const TString json = NWasmReefProfile::ProfileToJson(
            NWasmReefProfile::ParseReefRequestProfile(
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
                ReadStructString(rowH, OptionalMembers[18])));
        *result = MakeOptional(MakeString(json.data(), static_cast<int64_t>(json.size()))).Release();
    } catch (const std::exception& ex) {
        if (nullOnException) {
            *result = MakeNull().Release();
            return;
        }
        ThrowException(ex.what());
    }
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
    try {
        TString wire;
        if (!BridgeIsNull(wireH)) {
            const uint64_t offset = BridgeEnsureString(wireH);
            const int64_t length = BridgeGetStringLen(wireH);
            wire.assign(
                reinterpret_cast<const char*>(static_cast<uintptr_t>(offset)),
                static_cast<size_t>(length));
        }
        const TString json = NWasmReefProfile::ProfileToJson(
            NWasmReefProfile::ParseReefRequestProfileProto(wire));
        *result = MakeOptional(MakeString(json.data(), static_cast<int64_t>(json.size()))).Release();
    } catch (const std::exception& ex) {
        if (nullOnException) {
            *result = MakeNull().Release();
            return;
        }
        ThrowException(ex.what());
    }
}

} // extern "C"
