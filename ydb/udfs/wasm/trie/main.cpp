#include <ydb/services/udf_store/wasm/abi/udf_cpp_abi.h>
#include <ydb/services/udf_store/wasm/object_framework/object_framework.h>

#include "binary_trie.h"

#include <util/generic/strbuf.h>

#include <cstdlib>
#include <cstring>
#include <exception>

using namespace NYdb::NUdfStore::NAbi;
using namespace NBinaryTrie;

namespace {

ui32 ExtractValidSize(TStringBuf dict, ui64 offset) {
    if (offset > dict.size() - sizeof(ui32)) {
        throw yexception() << "Corrupt trie: out of range (size)";
    }
    auto size = ReadUnaligned<ui32>(dict.data() + offset);
    if (size > dict.size() - offset - sizeof(ui32)) {
        throw yexception() << "Corrupt trie: out of range (content)";
    }
    return size;
}

bool AnyNull(const TUnversionedValue* a, const TUnversionedValue* b) {
    return (a && a->Type == EValueType::Null) || (b && b->Type == EValueType::Null);
}

TStringBuf AsStringBuf(const TUnversionedValue* value, const char* where) {
    if (!value || value->Type != EValueType::String) {
        ThrowException(where);
        return {};
    }
    return TStringBuf(value->Data.String, value->Length);
}

void SetInt64(TUnversionedValue* result, i64 value) {
    result->Type = EValueType::Int64;
    result->Data.Int64 = value;
}

void SetNull(TUnversionedValue* result) {
    result->Type = EValueType::Null;
}

void SetString(TExpressionContext* context, TUnversionedValue* result, TStringBuf value) {
    result->Type = EValueType::String;
    result->Length = static_cast<uint32_t>(value.size());
    result->Data.String = AllocateBytes(context, result->Length);
    if (result->Length > 0) {
        memcpy(result->Data.String, value.data(), result->Length);
    }
}

struct TTrieDict {
    char* Data = nullptr;
    size_t Len = 0;
};

void TrieDictInit(void* self, const void* blob, size_t blobLen) {
    auto* dict = static_cast<TTrieDict*>(self);
    if (blobLen == 0) {
        dict->Data = nullptr;
        dict->Len = 0;
        return;
    }
    dict->Data = static_cast<char*>(malloc(blobLen));
    if (!dict->Data) {
        ThrowException("TrieDictInit: malloc failed");
    }
    memcpy(dict->Data, blob, blobLen);
    dict->Len = blobLen;
}

void TrieDictDestroy(void* self) {
    auto* dict = static_cast<TTrieDict*>(self);
    free(dict->Data);
    dict->Data = nullptr;
    dict->Len = 0;
}

const TObjectType TrieDictType = {
    "TrieDict",
    sizeof(TTrieDict),
    &TrieDictInit,
    &TrieDictDestroy,
};

uint64_t AsHandle(const TUnversionedValue* value) {
    if (!value || value->Type == EValueType::Null) {
        return 0;
    }
    if (value->Type == EValueType::Uint64) {
        return value->Data.Uint64;
    }
    if (value->Type == EValueType::Int64) {
        return static_cast<uint64_t>(value->Data.Int64);
    }
    ThrowException("expected int64/uint64 handle");
    return 0;
}

} // namespace

extern "C" {

__attribute__((visibility("default"))) void Lookup(
    TExpressionContext* /*context*/,
    TUnversionedValue* result,
    TUnversionedValue* haystackArg,
    TUnversionedValue* dictArg)
{
    if (AnyNull(haystackArg, dictArg)) {
        SetNull(result);
        return;
    }
    const TStringBuf haystack = AsStringBuf(haystackArg, "Lookup: expected string haystack");
    const TStringBuf dict = AsStringBuf(dictArg, "Lookup: expected string dict");
    try {
        SetInt64(result, LookupTrie(haystack, dict));
    } catch (const std::exception& e) {
        ThrowException(e.what());
    }
}

__attribute__((visibility("default"))) void LookupWithString(
    TExpressionContext* context,
    TUnversionedValue* result,
    TUnversionedValue* haystackArg,
    TUnversionedValue* dictArg)
{
    if (AnyNull(haystackArg, dictArg)) {
        SetNull(result);
        return;
    }
    const TStringBuf haystack = AsStringBuf(haystackArg, "LookupWithString: expected string haystack");
    const TStringBuf dict = AsStringBuf(dictArg, "LookupWithString: expected string dict");
    try {
        const i64 offset = LookupTrie(haystack, dict);
        if (offset < 0) {
            SetNull(result);
            return;
        }
        const ui32 size = ExtractValidSize(dict, static_cast<ui64>(offset));
        SetString(context, result, TStringBuf(dict.data() + offset + sizeof(ui32), size));
    } catch (const std::exception& e) {
        ThrowException(e.what());
    }
}

__attribute__((visibility("default"))) void trie_create(
    TExpressionContext* /*context*/,
    TUnversionedValue* result,
    TUnversionedValue* config)
{
    const char* blob = nullptr;
    size_t blobLen = 0;
    if (config && config->Type == EValueType::String) {
        blob = config->Data.String;
        blobLen = config->Length;
    } else if (config && config->Type != EValueType::Null) {
        ThrowException("trie_create: expected string config");
    }

    const TObjectHandle handle = ObjectFrameworkCreate(&TrieDictType, blob, blobLen);
    if (handle == 0) {
        ThrowException("trie_create failed");
    }
    result->Type = EValueType::Uint64;
    result->Data.Uint64 = handle;
}

__attribute__((visibility("default"))) void trie_lookup_cached(
    TExpressionContext* /*context*/,
    TUnversionedValue* result,
    TUnversionedValue* handleArg,
    TUnversionedValue* haystackArg)
{
    const uint64_t handle = AsHandle(handleArg);
    auto* dict = static_cast<TTrieDict*>(ObjectFrameworkGet(handle, &TrieDictType));
    if (!dict) {
        ThrowException("trie_lookup_cached: unknown handle");
    }
    if (!haystackArg || haystackArg->Type == EValueType::Null) {
        SetNull(result);
        return;
    }
    const TStringBuf haystack = AsStringBuf(haystackArg, "trie_lookup_cached: expected string haystack");
    const TStringBuf blob(dict->Data, dict->Len);
    try {
        SetInt64(result, LookupTrie(haystack, blob));
    } catch (const std::exception& e) {
        ThrowException(e.what());
    }
}

__attribute__((visibility("default"))) void trie_destroy(
    TExpressionContext* /*context*/,
    TUnversionedValue* result,
    TUnversionedValue* handleArg)
{
    ObjectFrameworkDestroy(AsHandle(handleArg));
    SetNull(result);
}

} // extern "C"
