#include "../unpack.h"

#include <library/cpp/testing/unittest/registar.h>
#include <library/cpp/blockcodecs/core/codecs.h>
#include <util/system/unaligned_mem.h>

using namespace NWasmReefProfile;

namespace {
auto ParseFeeds(TStringBuf codec, TStringBuf base, TStringBuf patch, TString& error) {
    return ParseReefRequestProfile(error, "u1", "r1", codec,
        {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, base, patch, {}, {});
}
const TString FeedsWire("\x0a\x02" "b1", 4);
}

Y_UNIT_TEST_SUITE(TWasmReefProfileInput) {
    Y_UNIT_TEST(ProtoInputs) {
        TString error;
        const auto profile = ParseReefRequestProfileProto(TString("\x0a\x02u1\x12\x02r1", 8), error);
        UNIT_ASSERT_C(error.empty(), error);
        UNIT_ASSERT_VALUES_EQUAL(profile.GetUserID(), "u1");
        UNIT_ASSERT_VALUES_EQUAL(profile.GetRequestID(), "r1");
        ParseReefRequestProfileProto("not-protobuf", error);
        UNIT_ASSERT(!error.empty());
        ParseReefRequestProfileProto("null", error);
        UNIT_ASSERT(error.empty());
        ParseReefRequestProfileProto({}, error);
        UNIT_ASSERT(error.empty());
    }

    Y_UNIT_TEST(IdentityAndZstd) {
        TString error;
        for (const TStringBuf codec : {"null", "zstd_6", "zstd08_6", "zstd_fast_1"}) {
            TString encoded;
            NBlockCodecs::Codec(codec)->Encode(FeedsWire, encoded);
            const auto profile = ParseFeeds(codec, encoded, {}, error);
            UNIT_ASSERT_C(error.empty(), error);
            UNIT_ASSERT_VALUES_EQUAL(profile.GetFeedsOutputCache().GetOutputShowBlockIds(0), "b1");
            UNIT_ASSERT_VALUES_EQUAL(profile.GetUserID(), "u1");
        }
    }

    Y_UNIT_TEST(InvalidCodecIds) {
        TString error;
        for (const TStringBuf codec : {",null", "null,", "null,,vcdiff", "a,b,c,d"}) {
            ParseCodecId(codec, error);
            UNIT_ASSERT_C(!error.empty(), codec);
        }
        ParseFeeds("missing-codec", FeedsWire, {}, error);
        UNIT_ASSERT(!error.empty());
        const auto codec = ParseCodecId("null,zstd_6,vcdiff", error);
        UNIT_ASSERT(error.empty());
        UNIT_ASSERT_VALUES_EQUAL(codec.BaseCompression, "null");
        UNIT_ASSERT_VALUES_EQUAL(codec.PatchCompression, "zstd_6");
    }

    Y_UNIT_TEST(MalformedColumnsAndPatches) {
        TString error;
        ParseFeeds("null", "not-protobuf", {}, error);
        UNIT_ASSERT(!error.empty());
        ParseFeeds("null", FeedsWire, "patch", error);
        UNIT_ASSERT(error.Contains("delta patch"));
        ParseFeeds("zstd_6", "short", {}, error);
        UNIT_ASSERT(!error.empty());
        TString encoded;
        NBlockCodecs::Codec("zstd_6")->Encode(FeedsWire, encoded);
        encoded.pop_back();
        ParseFeeds("zstd_6", encoded, {}, error);
        UNIT_ASSERT(!error.empty());
        NBlockCodecs::Codec("zstd_6")->Encode(FeedsWire, encoded);
        WriteUnaligned<ui64>(encoded.Detach(), FeedsWire.size() + 1);
        ParseFeeds("zstd_6", encoded, {}, error);
        UNIT_ASSERT(!error.empty());
    }
}
