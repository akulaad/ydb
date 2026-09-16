#include "line_break.h"
#include "parse_tskv.h"
#include "protoseq.h"

#include <ydb/services/udf_store/wasm/abi/bridge.h>
#include <ydb/services/udf_store/wasm/abi/bridge_abi.h>
#include <ydb/services/udf_store/wasm/abi/udf_cpp_abi.h>

#include <util/generic/hash.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

#include <cstdlib>

using namespace NYdb::NUdfStore::NAbi;

namespace {

uint64_t* AllocHandles(size_t count) {
    if (count == 0) {
        return nullptr;
    }
    auto* handles = static_cast<uint64_t*>(malloc(count * sizeof(uint64_t)));
    if (!handles) {
        ThrowException("LogParsing: malloc failed");
    }
    return handles;
}

TBridgeValue MakeStringBuf(TStringBuf s) {
    return MakeString(s.data(), static_cast<int64_t>(s.size()));
}

//! Optional<String> arg: null → absent; MiniKQL may omit the Optional wrapper.
bool TryReadOptionalString(uint64_t arg, TStringBuf* out) {
    if (BridgeIsNull(arg)) {
        return false;
    }
    uint64_t payload = arg;
    if (BridgeGetKind(arg) == BRIDGE_KIND_OPTIONAL) {
        payload = BridgeGetOptional(arg);
        if (BridgeIsNull(payload)) {
            return false;
        }
    }
    const uint64_t offset = BridgeEnsureString(payload);
    const int64_t length = BridgeGetStringLen(payload);
    *out = TStringBuf(
        reinterpret_cast<const char*>(static_cast<uintptr_t>(offset)),
        static_cast<size_t>(length));
    return true;
}

bool TryReadString(uint64_t arg, TStringBuf* out) {
    if (BridgeIsNull(arg)) {
        return false;
    }
    const uint64_t offset = BridgeEnsureString(arg);
    const int64_t length = BridgeGetStringLen(arg);
    *out = TStringBuf(
        reinterpret_cast<const char*>(static_cast<uintptr_t>(offset)),
        static_cast<size_t>(length));
    return true;
}

uint64_t MakeStringList(const TVector<TStringBuf>& items) {
    if (items.empty()) {
        return BridgeMakeList(/*itemsOff*/ 0, 0);
    }
    uint64_t* handles = AllocHandles(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
        handles[i] = MakeStringBuf(items[i]).Release();
    }
    const uint64_t list = BridgeMakeList(
        reinterpret_cast<uint64_t>(handles),
        static_cast<int32_t>(items.size()));
    free(handles);
    return list;
}

uint64_t MakeStringDict(const THashMap<TString, TString>& fields, uint64_t dictType) {
    if (fields.empty()) {
        return BridgeMakeDict(dictType, /*pairsOff*/ 0, 0);
    }
    uint64_t* pairs = AllocHandles(fields.size() * 2);
    size_t i = 0;
    for (const auto& [key, value] : fields) {
        pairs[i * 2] = MakeStringBuf(key).Release();
        pairs[i * 2 + 1] = MakeStringBuf(value).Release();
        ++i;
    }
    const uint64_t dict = BridgeMakeDict(
        dictType,
        reinterpret_cast<uint64_t>(pairs),
        static_cast<int32_t>(fields.size()));
    free(pairs);
    return dict;
}

enum ESplitField : int32_t {
    SplitSuccessed = 0,
    SplitRecords = 1,
    SplitContext = 2,
    SplitError = 3,
    SplitRawChunk = 4,
    SplitFieldCount = 5,
};

uint64_t MakeSplitSuccess(const TVector<TStringBuf>& records) {
    uint64_t* members = AllocHandles(SplitFieldCount);
    members[SplitSuccessed] = MakeBool(true).Release();
    members[SplitRecords] = MakeOptional(TBridgeValue(MakeStringList(records), true)).Release();
    members[SplitContext] = MakeNull().Release();
    members[SplitError] = MakeNull().Release();
    members[SplitRawChunk] = MakeNull().Release();
    const uint64_t result = BridgeMakeStruct(
        reinterpret_cast<uint64_t>(members),
        SplitFieldCount);
    free(members);
    return result;
}

uint64_t MakeSplitError(TStringBuf message, bool hasRaw, TStringBuf rawChunk) {
    uint64_t* members = AllocHandles(SplitFieldCount);
    members[SplitSuccessed] = MakeBool(false).Release();
    members[SplitRecords] = MakeNull().Release();
    members[SplitContext] = MakeNull().Release();
    members[SplitError] = MakeOptional(MakeStringBuf(message)).Release();
    if (hasRaw) {
        members[SplitRawChunk] = MakeOptional(MakeStringBuf(rawChunk)).Release();
    } else {
        members[SplitRawChunk] = MakeNull().Release();
    }
    const uint64_t result = BridgeMakeStruct(
        reinterpret_cast<uint64_t>(members),
        SplitFieldCount);
    free(members);
    return result;
}

enum ETskvField : int32_t {
    TskvSuccessed = 0,
    TskvFields = 1,
    TskvError = 2,
    TskvRawRecord = 3,
    TskvFieldCount = 4,
};

uint64_t MakeTskvResult(const NLogParsing::TTskvParseResult& parsed, TStringBuf raw) {
    TBridgeValue resultType(BridgeGetResultType(), /*owned*/ true);
    TBridgeValue fieldsType(BridgeTypeMember(resultType.Get(), TskvFields), /*owned*/ true);

    uint64_t* members = AllocHandles(TskvFieldCount);
    members[TskvSuccessed] = MakeBool(parsed.Successed).Release();
    members[TskvFields] = MakeStringDict(parsed.Fields, fieldsType.Get());
    if (parsed.Successed) {
        members[TskvError] = MakeNull().Release();
        members[TskvRawRecord] = MakeNull().Release();
    } else {
        members[TskvError] = MakeOptional(MakeStringBuf(parsed.Error)).Release();
        members[TskvRawRecord] = MakeOptional(MakeStringBuf(raw)).Release();
    }
    const uint64_t result = BridgeMakeStruct(
        reinterpret_cast<uint64_t>(members),
        TskvFieldCount);
    free(members);
    return result;
}

} // namespace

extern "C" {

//! LogParsing::LineBreak(chunk: String?) -> SplitResult
__attribute__((visibility("default"))) void line_break(
    TExpressionContext* /*ctx*/,
    uint64_t* result,
    uint64_t arg0)
{
    TStringBuf chunk;
    if (!TryReadOptionalString(arg0, &chunk)) {
        *result = MakeSplitError("no raw chunk", /*hasRaw*/ false, {});
        return;
    }
    TVector<TStringBuf> records;
    if (!NLogParsing::SplitLineBreak(chunk, &records)) {
        *result = MakeSplitError("cannot split by line break", /*hasRaw*/ true, chunk);
        return;
    }
    *result = MakeSplitSuccess(records);
}

//! LogParsing::Protoseq(chunk: String?) -> SplitResult
__attribute__((visibility("default"))) void protoseq(
    TExpressionContext* /*ctx*/,
    uint64_t* result,
    uint64_t arg0)
{
    TStringBuf chunk;
    if (!TryReadOptionalString(arg0, &chunk)) {
        *result = MakeSplitError("no raw chunk", /*hasRaw*/ false, {});
        return;
    }
    TVector<TStringBuf> frames;
    if (!NLogParsing::SplitProtoseq(chunk, &frames)) {
        *result = MakeSplitError("cannot split", /*hasRaw*/ true, chunk);
        return;
    }
    *result = MakeSplitSuccess(frames);
}

//! LogParsing::ParseTskv(raw: String) -> TskvResult
__attribute__((visibility("default"))) void parse_tskv(
    TExpressionContext* /*ctx*/,
    uint64_t* result,
    uint64_t arg0)
{
    TStringBuf raw;
    if (!TryReadString(arg0, &raw)) {
        NLogParsing::TTskvParseResult parsed;
        parsed.Successed = false;
        parsed.Error = "no raw record";
        *result = MakeTskvResult(parsed, {});
        return;
    }
    *result = MakeTskvResult(NLogParsing::ParseTskv(raw), raw);
}

} // extern "C"
