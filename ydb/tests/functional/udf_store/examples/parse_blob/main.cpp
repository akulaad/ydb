#include <ydb/services/udf_store/wasm/abi/udf_cpp_abi.h>

#include <cstdio>
#include <cstring>

using namespace NYdb::NUdfStore::NAbi;

namespace {

//! Scan the guest-resident blob and return a compact summary.
//! When the query stage has a WASM UDF, large string columns are materialized
//! directly into linear memory; arg0->Data.String is already a WASM offset.
void FormatSummary(const char* data, uint32_t length, char* out, size_t outCap)
{
    uint8_t xorSum = 0;
    for (uint32_t i = 0; i < length; ++i) {
        xorSum ^= static_cast<uint8_t>(data[i]);
    }

    char head[9] = {};
    const uint32_t headLen = length < 4 ? length : 4;
    for (uint32_t i = 0; i < headLen; ++i) {
        std::snprintf(head + i * 2, 3, "%02x", static_cast<uint8_t>(data[i]));
    }

    std::snprintf(
        out,
        outCap,
        "len=%u;xor=%02x;head=%s",
        static_cast<unsigned>(length),
        static_cast<unsigned>(xorSum),
        head);
}

} // namespace

extern "C" {

//! ParseBlob::parse_blob(blob: String) -> String
//!
//! Demonstrates Host→Guest zero-copy for heavy blob args:
//! scan writes the column into WASM linear memory once; this export receives
//! only (offset, length) and reads the bytes in place — no second host→guest copy.
__attribute__((visibility("default"))) void parse_blob(
    TExpressionContext* context,
    TUnversionedValue* result,
    TUnversionedValue* arg0)
{
    if (!arg0 || arg0->Type == EValueType::Null) {
        result->Type = EValueType::Null;
        return;
    }
    if (arg0->Type != EValueType::String) {
        ThrowException("parse_blob: expected String argument");
    }

    char summary[96];
    FormatSummary(arg0->Data.String, arg0->Length, summary, sizeof(summary));
    const size_t summaryLen = std::strlen(summary);

    result->Type = EValueType::String;
    result->Length = static_cast<uint32_t>(summaryLen);
    result->Data.String = AllocateBytes(context, summaryLen);
    if (summaryLen > 0) {
        std::memcpy(result->Data.String, summary, summaryLen);
    }
}

} // extern "C"
