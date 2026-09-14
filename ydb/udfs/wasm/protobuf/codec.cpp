#include "codec.h"

#include <library/cpp/json/json_reader.h>
#include <library/cpp/json/json_value.h>
#include <library/cpp/protobuf/json/json2proto.h>
#include <library/cpp/protobuf/json/proto2json.h>

#include <util/generic/yexception.h>

namespace NWasmProtobuf {

TCodec CreateCodec(TStringBuf typeConfig) {
    TCodec codec;
    codec.Info = TDynamicInfo::Create(typeConfig);
    return codec;
}

TString MessageToJson(const NProtoBuf::Message& message) {
    TString json;
    NProtobufJson::Proto2Json(message, json, NProtobufJson::TProto2JsonConfig());
    return json;
}

TString ParseToJson(const TCodec& codec, TStringBuf wire) {
    const TAutoPtr<NProtoBuf::Message> message = codec.Info->Parse(wire);
    if (!message) {
        ythrow yexception() << "Protobuf.Parse: empty message";
    }
    return MessageToJson(*message);
}

TMaybe<TString> TryParseToJson(const TCodec& codec, TStringBuf wire) {
    try {
        return ParseToJson(codec, wire);
    } catch (const std::exception&) {
        return Nothing();
    }
}

TString SerializeFromJson(const TCodec& codec, TStringBuf json) {
    NJson::TJsonValue value;
    if (!NJson::ReadJsonFastTree(json, &value)) {
        ythrow yexception() << "Protobuf.Serialize: invalid json";
    }
    TAutoPtr<NProtoBuf::Message> message = codec.Info->MakeProto();
    NProtobufJson::Json2Proto(value, *message);
    return codec.Info->Serialize(*message);
}

} // namespace NWasmProtobuf
