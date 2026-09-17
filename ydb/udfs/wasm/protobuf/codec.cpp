#include "codec.h"

#include <library/cpp/json/json_reader.h>
#include <library/cpp/json/json_value.h>
#include <library/cpp/protobuf/json/json2proto.h>
#include <library/cpp/protobuf/json/proto2json.h>

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/wire_format_lite.h>

#include <util/generic/yexception.h>

namespace NWasmProtobuf {
namespace {

bool IsValidWireFormat(TStringBuf wire) {
    google::protobuf::io::CodedInputStream input(
        reinterpret_cast<const ui8*>(wire.data()), wire.size());
    return google::protobuf::internal::WireFormatLite::SkipMessage(&input) &&
        input.ConsumedEntireMessage();
}

} // namespace

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
    // The guest build has C++ exceptions disabled. Validate the wire stream
    // before entering protobuf reflection so malformed tags cannot trap the
    // WASM instance, and use the non-throwing parser for the expected failure.
    if (!IsValidWireFormat(wire)) {
        return Nothing();
    }

    TAutoPtr<NProtoBuf::Message> message = codec.Info->MakeProto();
    if (!message->ParseFromArray(wire.data(), wire.size())) {
        return Nothing();
    }
    return MessageToJson(*message);
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
