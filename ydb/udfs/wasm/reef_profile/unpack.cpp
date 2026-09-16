#include "unpack.h"

#include <library/cpp/blockcodecs/core/codecs.h>
#include <library/cpp/protobuf/json/proto2json.h>

#include <util/generic/buffer.h>
#include <util/generic/vector.h>
#include <util/generic/yexception.h>
#include <util/string/split.h>

namespace NWasmReefProfile {
namespace {

constexpr TStringBuf DefaultCodec = "zstd_6";

TString DecodeBlob(TStringBuf encoded, TStringBuf codecName) {
    if (encoded.empty()) {
        return {};
    }
    const NBlockCodecs::ICodec* codec = NBlockCodecs::Codec(codecName);
    TString decoded;
    codec->Decode(encoded, decoded);
    return decoded;
}

TString ApplyPatch(TStringBuf base, TStringBuf patch, TStringBuf deltaAlgo) {
    if (patch.empty()) {
        return TString(base);
    }
    ythrow yexception() << "delta patch apply is not linked (" << deltaAlgo
                        << "); packed_*_patch must be empty in this WASM build";
}

template <typename TMessage>
void ParsePackedInto(
    TStringBuf base,
    TStringBuf patch,
    const TCodecId& codec,
    TMessage* dst)
{
    if (base.empty() && patch.empty()) {
        return;
    }
    const TString decodedBase = DecodeBlob(base, codec.BaseCompression);
    const TString decodedPatch = DecodeBlob(patch, codec.PatchCompression);
    const TString wire = ApplyPatch(decodedBase, decodedPatch, codec.DeltaAlgorithm);
    if (wire.empty()) {
        return;
    }
    if (!dst->ParseFromString(wire)) {
        ythrow yexception() << "failed to parse packed column protobuf";
    }
}

} // namespace

TCodecId ParseCodecId(TStringBuf codecId) {
    TCodecId parsed;
    if (codecId.empty()) {
        parsed.BaseCompression = TString(DefaultCodec);
        parsed.PatchCompression = TString(DefaultCodec);
        return parsed;
    }

    TVector<TStringBuf> tokens;
    StringSplitter(codecId).Split(',').Collect(&tokens);
    Y_ENSURE(tokens.size() >= 1 && tokens.size() <= 3, "invalid codec id");

    switch (tokens.size()) {
        case 1:
            Y_ENSURE(!tokens[0].empty());
            parsed.BaseCompression = TString(tokens[0]);
            parsed.PatchCompression = TString(tokens[0]);
            break;
        case 2:
            Y_ENSURE(!tokens[0].empty() && !tokens[1].empty());
            parsed.BaseCompression = TString(tokens[0]);
            parsed.PatchCompression = TString(tokens[0]);
            parsed.DeltaAlgorithm = TString(tokens[1]);
            break;
        case 3:
            Y_ENSURE(!tokens[0].empty() && !tokens[1].empty() && !tokens[2].empty());
            parsed.BaseCompression = TString(tokens[0]);
            parsed.PatchCompression = TString(tokens[1]);
            parsed.DeltaAlgorithm = TString(tokens[2]);
            break;
    }
    return parsed;
}

TString UnpackPackedColumn(TStringBuf base, TStringBuf patch, const TCodecId& codec) {
    if (base.empty() && patch.empty()) {
        return {};
    }
    const TString decodedBase = DecodeBlob(base, codec.BaseCompression);
    const TString decodedPatch = DecodeBlob(patch, codec.PatchCompression);
    return ApplyPatch(decodedBase, decodedPatch, codec.DeltaAlgorithm);
}

NUserSessions::NRT::TReefRequestProfileProto ParseReefRequestProfile(
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
    const TCodecId codec = ParseCodecId(codecId);
    NUserSessions::NRT::TReefRequestProfileProto proto;
    if (!userId.empty()) {
        proto.SetUserID(TString(userId));
    }
    if (!requestId.empty()) {
        proto.SetRequestID(TString(requestId));
    }
    if (!packedBlockstat.empty() || !packedBlockstatPatch.empty()) {
        ParsePackedInto(packedBlockstat, packedBlockstatPatch, codec, proto.MutableBlockstatData());
    }
    if (!packedRedir.empty() || !packedRedirPatch.empty()) {
        ParsePackedInto(packedRedir, packedRedirPatch, codec, proto.MutableRedirData());
    }
    if (!packedClicks.empty() || !packedClicksPatch.empty()) {
        ParsePackedInto(packedClicks, packedClicksPatch, codec, proto.MutableClicksData());
    }
    if (!packedCommon.empty() || !packedCommonPatch.empty()) {
        ParsePackedInto(packedCommon, packedCommonPatch, codec, proto.MutableCommonData());
    }
    if (!packedRequestClicks.empty() || !packedRequestClicksPatch.empty()) {
        ParsePackedInto(
            packedRequestClicks,
            packedRequestClicksPatch,
            codec,
            proto.MutableRequestClicksCommonInfo());
    }
    if (!packedTechs.empty() || !packedTechsPatch.empty()) {
        ParsePackedInto(packedTechs, packedTechsPatch, codec, proto.MutableTechsData());
    }
    if (!packedFeeds.empty() || !packedFeedsPatch.empty()) {
        ParsePackedInto(packedFeeds, packedFeedsPatch, codec, proto.MutableFeedsOutputCache());
    }
    if (!packedTamus.empty() || !packedTamusPatch.empty()) {
        ParsePackedInto(packedTamus, packedTamusPatch, codec, proto.MutableTamusWorkedRules());
    }
    return proto;
}

NUserSessions::NRT::TReefRequestProfileProto ParseReefRequestProfileProto(TStringBuf wire) {
    NUserSessions::NRT::TReefRequestProfileProto proto;
    if (wire.empty() || wire == TStringBuf("null")) {
        return proto;
    }
    if (!proto.ParseFromString(wire)) {
        ythrow yexception() << "Can't parse profile protobuf";
    }
    return proto;
}

TString ProfileToJson(const NUserSessions::NRT::TReefRequestProfileProto& proto) {
    TString json;
    NProtobufJson::Proto2Json(proto, json, NProtobufJson::TProto2JsonConfig());
    return json;
}

} // namespace NWasmReefProfile
