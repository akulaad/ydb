#pragma once

#include <quality/user_sessions/rt/lib/reef_profile/proto/request_profile.pb.h>

#include <util/generic/strbuf.h>
#include <util/generic/string.h>

namespace NWasmReefProfile {

struct TCodecId {
    TString BaseCompression = "zstd_6";
    TString PatchCompression = "zstd_6";
    TString DeltaAlgorithm = "vcdiff";
};

TCodecId ParseCodecId(TStringBuf codecId);

// Decode one PFT_PACKABLE column (base + optional patch) to protobuf wire.
TString UnpackPackedColumn(TStringBuf base, TStringBuf patch, const TCodecId& codec);

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
    TStringBuf packedTamusPatch);

// Empty wire or literal "null" → empty message; otherwise ParseFromString.
NUserSessions::NRT::TReefRequestProfileProto ParseReefRequestProfileProto(TStringBuf wire);

TString ProfileToJson(const NUserSessions::NRT::TReefRequestProfileProto& proto);

} // namespace NWasmReefProfile
