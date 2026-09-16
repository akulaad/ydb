#include "unpack.h"

#include <library/cpp/blockcodecs/core/codecs.h>
#include <contrib/libs/zstd/include/zstd.h>
#include <util/system/unaligned_mem.h>
#include <library/cpp/protobuf/json/proto2json.h>

#include <util/generic/vector.h>

namespace NWasmReefProfile {
namespace {

constexpr TStringBuf DefaultCodec = "zstd_6";

// This guest has no C++ exception unwinding. Expected input errors must be
// returned explicitly so null_on_exception can be implemented at the bridge.
TString DecodeBlob(TStringBuf encoded, TStringBuf codecName, TString& error) {
    if (encoded.empty()) {
        return {};
    }
    if (codecName == "null") {
        return TString(encoded);
    }
    bool knownZstd = false;
    for (const auto name : NBlockCodecs::ListAllCodecs()) {
        if (name == codecName && name.StartsWith("zstd")) {
            knownZstd = true;
            break;
        }
    }
    if (!knownZstd) {
        error = "unknown compression codec: " + TString(codecName);
        return {};
    }
    // NBlockCodecs' zstd wire format prefixes the frame with its ui64 length.
    if (encoded.size() < sizeof(ui64)) {
        error = "compressed column is missing its length prefix";
        return {};
    }
    const ui64 length = ReadUnaligned<ui64>(encoded.data());
    if (length > NBlockCodecs::GetMaxPossibleDecompressedLength()) {
        error = "compressed column exceeds maximum decompressed length";
        return {};
    }
    if (!length) {
        return {};
    }
    encoded.Skip(sizeof(ui64));
    TString decoded;
    decoded.resize(length);
    const size_t actual = ZSTD_decompress(decoded.Detach(), decoded.size(), encoded.data(), encoded.size());
    if (ZSTD_isError(actual)) {
        error = "decompress zstd error: " + TString(ZSTD_getErrorName(actual));
        return {};
    }
    if (actual != length) {
        error = "compressed column length mismatch";
        return {};
    }
    return decoded;
}

template <typename TMessage>
bool ParsePackedInto(
    TStringBuf base,
    TStringBuf patch,
    const TCodecId& codec,
    TMessage* dst,
    TString& error)
{
    const TString wire = DecodeBlob(base, codec.BaseCompression, error);
    if (!error.empty()) {
        return false;
    }
    const TString decodedPatch = DecodeBlob(patch, codec.PatchCompression, error);
    if (!error.empty()) {
        return false;
    }
    if (!decodedPatch.empty()) {
        error = "delta patch apply is not linked (" + codec.DeltaAlgorithm
            + "); packed_*_patch must be empty in this WASM build";
        return false;
    }
    if (!wire.empty() && !dst->ParseFromString(wire)) {
        error = "failed to parse packed column protobuf";
        return false;
    }
    return true;
}

} // namespace

TCodecId ParseCodecId(TStringBuf codecId, TString& error) {
    error.clear();
    TCodecId parsed;
    if (codecId.empty()) {
        parsed.BaseCompression = TString(DefaultCodec);
        parsed.PatchCompression = TString(DefaultCodec);
        return parsed;
    }

    TVector<TStringBuf> tokens;
    // Keep empty fields, including a trailing one. StringSplitter's external
    // sentinel cannot be linked as a relative data import by wasm-ld.
    while (true) {
        const size_t comma = codecId.find(',');
        tokens.push_back(codecId.SubStr(0, comma));
        if (comma == TStringBuf::npos) {
            break;
        }
        codecId.Skip(comma + 1);
    }
    if (tokens.size() > 3) {
        error = "invalid codec id";
        return parsed;
    }
    for (const auto token : tokens) {
        if (token.empty()) {
            error = "empty field in codec id";
            return parsed;
        }
    }

    switch (tokens.size()) {
        case 1:
            parsed.BaseCompression = TString(tokens[0]);
            parsed.PatchCompression = TString(tokens[0]);
            break;
        case 2:
            parsed.BaseCompression = TString(tokens[0]);
            parsed.PatchCompression = TString(tokens[0]);
            parsed.DeltaAlgorithm = TString(tokens[1]);
            break;
        case 3:
            parsed.BaseCompression = TString(tokens[0]);
            parsed.PatchCompression = TString(tokens[1]);
            parsed.DeltaAlgorithm = TString(tokens[2]);
            break;
    }
    return parsed;
}

NUserSessions::NRT::TReefRequestProfileProto ParseReefRequestProfile(
    TString& error,
    TStringBuf userId,
    TStringBuf requestId,
    TStringBuf codecId,
    TStringBuf packedBlockstat,
    TStringBuf packedBlockstatPatch,
    TStringBuf packedRedir,
    TStringBuf packedRedirPatch,
    TStringBuf packedClicks,
    TStringBuf packedClicksPatch,
    TStringBuf packedCommon,
    TStringBuf packedCommonPatch,
    TStringBuf packedRequestClicks,
    TStringBuf packedRequestClicksPatch,
    TStringBuf packedTechs,
    TStringBuf packedTechsPatch,
    TStringBuf packedFeeds,
    TStringBuf packedFeedsPatch,
    TStringBuf packedTamus,
    TStringBuf packedTamusPatch)
{
    error.clear();
    const TCodecId codec = ParseCodecId(codecId, error);
    NUserSessions::NRT::TReefRequestProfileProto proto;
    if (!error.empty()) {
        return proto;
    }
    if (!userId.empty()) {
        proto.SetUserID(TString(userId));
    }
    if (!requestId.empty()) {
        proto.SetRequestID(TString(requestId));
    }
    if (!packedBlockstat.empty() || !packedBlockstatPatch.empty()) {
        if (!ParsePackedInto(packedBlockstat, packedBlockstatPatch, codec, proto.MutableBlockstatData(), error)) {
            return proto;
        }
    }
    if (!packedRedir.empty() || !packedRedirPatch.empty()) {
        if (!ParsePackedInto(packedRedir, packedRedirPatch, codec, proto.MutableRedirData(), error)) {
            return proto;
        }
    }
    if (!packedClicks.empty() || !packedClicksPatch.empty()) {
        if (!ParsePackedInto(packedClicks, packedClicksPatch, codec, proto.MutableClicksData(), error)) {
            return proto;
        }
    }
    if (!packedCommon.empty() || !packedCommonPatch.empty()) {
        if (!ParsePackedInto(packedCommon, packedCommonPatch, codec, proto.MutableCommonData(), error)) {
            return proto;
        }
    }
    if (!packedRequestClicks.empty() || !packedRequestClicksPatch.empty()) {
        if (!ParsePackedInto(
            packedRequestClicks,
            packedRequestClicksPatch,
            codec,
            proto.MutableRequestClicksCommonInfo(), error)) {
            return proto;
        }
    }
    if (!packedTechs.empty() || !packedTechsPatch.empty()) {
        if (!ParsePackedInto(packedTechs, packedTechsPatch, codec, proto.MutableTechsData(), error)) {
            return proto;
        }
    }
    if (!packedFeeds.empty() || !packedFeedsPatch.empty()) {
        if (!ParsePackedInto(packedFeeds, packedFeedsPatch, codec, proto.MutableFeedsOutputCache(), error)) {
            return proto;
        }
    }
    if (!packedTamus.empty() || !packedTamusPatch.empty()) {
        if (!ParsePackedInto(packedTamus, packedTamusPatch, codec, proto.MutableTamusWorkedRules(), error)) {
            return proto;
        }
    }
    return proto;
}

NUserSessions::NRT::TReefRequestProfileProto ParseReefRequestProfileProto(TStringBuf wire, TString& error) {
    error.clear();
    NUserSessions::NRT::TReefRequestProfileProto proto;
    if (wire.empty() || wire == TStringBuf("null")) {
        return proto;
    }
    if (!proto.ParseFromString(wire)) {
        error = "Can't parse profile protobuf";
    }
    return proto;
}

TString ProfileToJson(const NUserSessions::NRT::TReefRequestProfileProto& proto) {
    TString json;
    NProtobufJson::Proto2Json(proto, json, NProtobufJson::TProto2JsonConfig());
    return json;
}

} // namespace NWasmReefProfile
