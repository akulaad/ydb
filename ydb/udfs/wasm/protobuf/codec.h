#pragma once

#include <library/cpp/protobuf/yql/descriptor.h>

#include <util/generic/maybe.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>

namespace NWasmProtobuf {

//! Holds a TypeConfig-backed dynamic prototype (same JSON as native Protobuf UDF).
struct TCodec {
    TDynamicInfoRef Info;
};

//! Build codec from native TypeConfig JSON blob. Throws on bad config.
TCodec CreateCodec(TStringBuf typeConfig);

//! Wire (per TypeConfig format) → JSON object string. Throws on parse failure.
TString ParseToJson(const TCodec& codec, TStringBuf wire);

//! Like ParseToJson, but returns Nothing on failure.
TMaybe<TString> TryParseToJson(const TCodec& codec, TStringBuf wire);

//! JSON object string → wire (per TypeConfig format). Throws on failure.
TString SerializeFromJson(const TCodec& codec, TStringBuf json);

} // namespace NWasmProtobuf
