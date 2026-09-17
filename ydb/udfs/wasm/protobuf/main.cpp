#include "codec.h"

#include <ydb/services/udf_store/wasm/abi/udf_cpp_abi.h>
#include <ydb/services/udf_store/wasm/abi/bridge.h>
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

TStringBuf AsStringBuf(TBridgeHandle value, const char* what) {
    if (BridgeIsNull(value)) {
        ThrowException(what);
    }
    return TStringBuf(reinterpret_cast<const char*>(BridgeEnsureString(value)), BridgeGetStringLen(value));
}

TProtobufObject* GetObject(uint64_t handle) {
    auto* object = static_cast<TProtobufObject*>(
        ObjectFrameworkGet(handle, &ProtobufType));
    if (!object) {
        ThrowException("unknown Protobuf handle");
    }
    return object;
}

void SetStringResult(TExpressionContext*, TBridgeHandle* result, TStringBuf data) {
    *result = MakeString(data.data(), data.size()).Release();
}

void SetNullResult(TBridgeHandle* result) {
    *result = MakeNull().Release();
}

} // namespace

extern "C" {

__attribute__((visibility("default"))) void protobuf_create(
    TExpressionContext* /*context*/,
    TBridgeHandle* result,
    TBridgeHandle config)
{
    const auto configBytes = AsStringBuf(config, "protobuf_create: expected string TypeConfig");
    const char* blob = configBytes.data();
    const size_t blobLen = configBytes.size();

    const TObjectHandle handle = ObjectFrameworkCreate(&ProtobufType, blob, blobLen);
    if (handle == 0) {
        ThrowException("protobuf_create failed");
    }
    *result = MakeUint64(handle).Release();
}

__attribute__((visibility("default"))) void protobuf_destroy(
    TExpressionContext* /*context*/,
    TBridgeHandle* result,
    TBridgeHandle handleArg)
{
    ObjectFrameworkDestroy(BridgeGetUint64(handleArg));
    SetNullResult(result);
}

__attribute__((visibility("default"))) void protobuf_parse(
    TExpressionContext* context,
    TBridgeHandle* result,
    TBridgeHandle handleArg,
    TBridgeHandle inputArg)
{
    auto* object = GetObject(BridgeGetUint64(handleArg));
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
    TBridgeHandle* result,
    TBridgeHandle handleArg,
    TBridgeHandle inputArg)
{
    auto* object = GetObject(BridgeGetUint64(handleArg));
    if (BridgeIsNull(inputArg)) {
        SetNullResult(result);
        return;
    }
    const TStringBuf wire = AsStringBuf(inputArg, "Protobuf.TryParse: expected string");
    const TMaybe<TString> json = NWasmProtobuf::TryParseToJson(object->Codec, wire);
    if (!json) {
        SetNullResult(result);
        return;
    }
    SetStringResult(context, result, *json);
}

__attribute__((visibility("default"))) void protobuf_serialize(
    TExpressionContext* context,
    TBridgeHandle* result,
    TBridgeHandle handleArg,
    TBridgeHandle inputArg)
{
    auto* object = GetObject(BridgeGetUint64(handleArg));
    const TStringBuf json = AsStringBuf(inputArg, "Protobuf.Serialize: expected string");
    try {
        const TString wire = NWasmProtobuf::SerializeFromJson(object->Codec, json);
        SetStringResult(context, result, wire);
    } catch (const std::exception& ex) {
        ThrowException(ex.what());
    }
}

} // extern "C"
