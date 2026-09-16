#include "codec.h"

#include <ydb/services/udf_store/wasm/abi/udf_cpp_abi.h>
#include <ydb/services/udf_store/wasm/object_framework/object_framework.h>

#include <util/generic/strbuf.h>
#include <util/generic/string.h>

#include <new>
#include <string.h>

using namespace NYdb::NUdfStore::NAbi;

namespace {

struct TProtobufObject {
    NWasmProtobuf::TCodec Codec;
};

void ProtobufInit(void* self, const void* blob, size_t blobLen) {
    auto* object = new (self) TProtobufObject();
    try {
        object->Codec = NWasmProtobuf::CreateCodec(
            TStringBuf(static_cast<const char*>(blob), blobLen));
    } catch (const std::exception& ex) {
        object->~TProtobufObject();
        ThrowException(ex.what());
    }
}

void ProtobufDestroy(void* self) {
    static_cast<TProtobufObject*>(self)->~TProtobufObject();
}

const TObjectType ProtobufType = {
    "Protobuf",
    sizeof(TProtobufObject),
    &ProtobufInit,
    &ProtobufDestroy,
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

TStringBuf AsStringBuf(const TUnversionedValue* value, const char* what) {
    if (!value || value->Type == EValueType::Null) {
        ThrowException(what);
    }
    if (value->Type != EValueType::String) {
        ThrowException(what);
    }
    return TStringBuf(value->Data.String, value->Length);
}

TProtobufObject* GetObject(uint64_t handle) {
    auto* object = static_cast<TProtobufObject*>(
        ObjectFrameworkGet(handle, &ProtobufType));
    if (!object) {
        ThrowException("unknown Protobuf handle");
    }
    return object;
}

void SetStringResult(TExpressionContext* context, TUnversionedValue* result, TStringBuf data) {
    result->Type = EValueType::String;
    result->Length = static_cast<uint32_t>(data.size());
    result->Data.String = AllocateBytes(context, data.size());
    if (data.size() > 0) {
        memcpy(result->Data.String, data.data(), data.size());
    }
}

void SetNullResult(TUnversionedValue* result) {
    result->Type = EValueType::Null;
    result->Length = 0;
    result->Data.String = nullptr;
}

} // namespace

extern "C" {

__attribute__((visibility("default"))) void protobuf_create(
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
        ThrowException("protobuf_create: expected string TypeConfig");
    }

    const TObjectHandle handle = ObjectFrameworkCreate(&ProtobufType, blob, blobLen);
    if (handle == 0) {
        ThrowException("protobuf_create failed");
    }
    result->Type = EValueType::Uint64;
    result->Data.Uint64 = handle;
}

__attribute__((visibility("default"))) void protobuf_destroy(
    TExpressionContext* /*context*/,
    TUnversionedValue* result,
    TUnversionedValue* handleArg)
{
    ObjectFrameworkDestroy(AsHandle(handleArg));
    SetNullResult(result);
}

__attribute__((visibility("default"))) void protobuf_parse(
    TExpressionContext* context,
    TUnversionedValue* result,
    TUnversionedValue* handleArg,
    TUnversionedValue* inputArg)
{
    auto* object = GetObject(AsHandle(handleArg));
    const TStringBuf wire = AsStringBuf(inputArg, "Protobuf.Parse: expected string");
    try {
        const TString json = NWasmProtobuf::ParseToJson(object->Codec, wire);
        SetStringResult(context, result, json);
    } catch (const std::exception& ex) {
        ThrowException(ex.what());
    }
}

__attribute__((visibility("default"))) void protobuf_try_parse(
    TExpressionContext* context,
    TUnversionedValue* result,
    TUnversionedValue* handleArg,
    TUnversionedValue* inputArg)
{
    auto* object = GetObject(AsHandle(handleArg));
    if (!inputArg || inputArg->Type == EValueType::Null) {
        SetNullResult(result);
        return;
    }
    if (inputArg->Type != EValueType::String) {
        SetNullResult(result);
        return;
    }
    const TStringBuf wire(inputArg->Data.String, inputArg->Length);
    const TMaybe<TString> json = NWasmProtobuf::TryParseToJson(object->Codec, wire);
    if (!json) {
        SetNullResult(result);
        return;
    }
    SetStringResult(context, result, *json);
}

__attribute__((visibility("default"))) void protobuf_serialize(
    TExpressionContext* context,
    TUnversionedValue* result,
    TUnversionedValue* handleArg,
    TUnversionedValue* inputArg)
{
    auto* object = GetObject(AsHandle(handleArg));
    const TStringBuf json = AsStringBuf(inputArg, "Protobuf.Serialize: expected string");
    try {
        const TString wire = NWasmProtobuf::SerializeFromJson(object->Codec, json);
        SetStringResult(context, result, wire);
    } catch (const std::exception& ex) {
        ThrowException(ex.what());
    }
}

} // extern "C"
