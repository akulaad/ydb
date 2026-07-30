#include <ydb/library/wasm/api/compartment.h>

#include <library/cpp/http/simple/http_client.h>

#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/stream/str.h>
#include <util/string/cast.h>
#include <util/string/split.h>

#include <cstdint>
#include <cstring>

namespace WAVM {
namespace Runtime {
struct ContextRuntimeData;
} // namespace Runtime
} // namespace WAVM

namespace {

constexpr i64 ErrNoCompartment = -1;
constexpr i64 ErrBadArgs = -2;
constexpr i64 ErrRequestFailed = -3;
constexpr i64 ErrBufferTooSmall = -4;

i64 PackResult(ui32 httpCode, ui32 bytesWritten) {
    return (static_cast<i64>(httpCode) << 32) | static_cast<i64>(bytesWritten);
}

bool WriteToGuest(
    NYdb::NWasm::IWebAssemblyCompartment* compartment,
    ui64 outPtr,
    ui32 outCap,
    TStringBuf body,
    ui32* written)
{
    if (body.size() > outCap) {
        return false;
    }
    if (body.empty()) {
        *written = 0;
        return true;
    }
    auto* hostOut = static_cast<char*>(compartment->GetHostPointer(outPtr, body.size()));
    std::memcpy(hostOut, body.data(), body.size());
    *written = static_cast<ui32>(body.size());
    return true;
}

i64 HandleMockUrl(
    NYdb::NWasm::IWebAssemblyCompartment* compartment,
    TStringBuf url,
    ui64 outPtr,
    ui32 outCap)
{
    // mock://ok → fixed body; mock://echo/<text> → echo text after prefix.
    static constexpr TStringBuf Prefix = "mock://";
    if (!url.StartsWith(Prefix)) {
        return ErrBadArgs;
    }
    const TStringBuf rest = url.Tail(Prefix.size());
    TString body;
    if (rest == "ok") {
        body = "hello-from-native-http";
    } else if (rest.StartsWith("echo/")) {
        body = TString(rest.Tail(TStringBuf("echo/").size()));
    } else {
        body = "unknown-mock";
    }

    ui32 written = 0;
    if (!WriteToGuest(compartment, outPtr, outCap, body, &written)) {
        return ErrBufferTooSmall;
    }
    return PackResult(200, written);
}

bool ParseHttpUrl(TStringBuf url, TString* host, ui16* port, TString* path, bool* https) {
    *https = false;
    TStringBuf rest = url;
    if (rest.StartsWith("https://")) {
        *https = true;
        rest.SkipPrefix("https://");
        *port = 443;
    } else if (rest.StartsWith("http://")) {
        rest.SkipPrefix("http://");
        *port = 80;
    } else {
        return false;
    }

    const size_t slash = rest.find('/');
    TStringBuf authority = slash == TStringBuf::npos ? rest : rest.Head(slash);
    *path = slash == TStringBuf::npos ? TString("/") : TString(rest.Tail(slash));

    TStringBuf hostPart;
    TStringBuf portPart;
    if (authority.TrySplit(':', hostPart, portPart)) {
        *host = TString(hostPart);
        if (!TryFromString(portPart, *port)) {
            return false;
        }
    } else {
        *host = TString(authority);
    }
    return !host->empty();
}

i64 HandleHttpUrl(
    NYdb::NWasm::IWebAssemblyCompartment* compartment,
    TStringBuf url,
    ui64 outPtr,
    ui32 outCap)
{
    TString host;
    ui16 port = 80;
    TString path;
    bool https = false;
    if (!ParseHttpUrl(url, &host, &port, &path, &https)) {
        return ErrBadArgs;
    }
    if (https) {
        // Keep the example surface small: use mock:// or plain http://.
        return ErrBadArgs;
    }

    try {
        TStringStream bodyStream;
        TKeepAliveHttpClient client(host, port, TDuration::Seconds(2), TDuration::Seconds(5));
        const auto code = client.DoGet(path, &bodyStream);
        const TString body = bodyStream.Str();
        ui32 written = 0;
        if (!WriteToGuest(compartment, outPtr, outCap, body, &written)) {
            return ErrBufferTooSmall;
        }
        return PackResult(static_cast<ui32>(code), written);
    } catch (...) {
        return ErrRequestFailed;
    }
}

} // namespace

//! host_http_get(url_ptr, url_len, out_ptr, out_cap) -> i64
//! Guest pointers are linear-memory offsets. Result: (http_code << 32) | nbytes, or negative error.
extern "C" __attribute__((visibility("default"))) int64_t native_http_get(
    WAVM::Runtime::ContextRuntimeData* /*ctx*/,
    int64_t urlPtr,
    int32_t urlLen,
    int64_t outPtr,
    int32_t outCap)
{
    auto* compartment = NYdb::NWasm::GetCurrentCompartment();
    if (!compartment) {
        return ErrNoCompartment;
    }
    if (urlLen < 0 || outCap < 0 || urlPtr < 0 || outPtr < 0) {
        return ErrBadArgs;
    }
    if (urlLen == 0) {
        return ErrBadArgs;
    }

    const char* urlHost = static_cast<const char*>(
        compartment->GetHostPointer(static_cast<uintptr_t>(urlPtr), static_cast<size_t>(urlLen)));
    const TStringBuf url(urlHost, static_cast<size_t>(urlLen));

    if (url.StartsWith("mock://")) {
        return HandleMockUrl(compartment, url, static_cast<ui64>(outPtr), static_cast<ui32>(outCap));
    }
    return HandleHttpUrl(compartment, url, static_cast<ui64>(outPtr), static_cast<ui32>(outCap));
}
