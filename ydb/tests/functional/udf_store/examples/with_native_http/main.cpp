#include <ydb/services/udf_store/wasm/abi/udf_cpp_abi.h>

using namespace NYdb::NUdfStore::NAbi;

// Resolved at query time from NATIVE_UNSAFE module "native_http"
// (required_native_modules: ["native_http"], host_exports host_http_get).
// Signature: (url_ptr, url_len, out_ptr, out_cap) -> packed (code<<32)|nbytes
__attribute__((import_module("native_http"), import_name("host_http_get")))
extern "C" long long host_http_get(
    long long urlPtr,
    int urlLen,
    long long outPtr,
    int outCap);

namespace {

constexpr int kMaxResponseBytes = 64 * 1024;

} // namespace

extern "C" {
    __attribute__((visibility("default"))) void http_get(
        TExpressionContext* context,
        TUnversionedValue* result,
        TUnversionedValue* urlArg)
    {
        if (urlArg->Type == EValueType::Null) {
            result->Type = EValueType::Null;
            return;
        }
        if (urlArg->Type != EValueType::String || !urlArg->Data.String) {
            ThrowException("http_get: url must be a non-null string");
        }

        char* outBuf = AllocateBytes(context, kMaxResponseBytes);
        const long long packed = host_http_get(
            reinterpret_cast<long long>(urlArg->Data.String),
            static_cast<int>(urlArg->Length),
            reinterpret_cast<long long>(outBuf),
            kMaxResponseBytes);

        if (packed < 0) {
            ThrowException("http_get: native host request failed");
        }

        const unsigned nbytes = static_cast<unsigned>(packed & 0xffffffffu);
        // http code available as (packed >> 32) if needed by callers later.
        result->Type = EValueType::String;
        result->Length = nbytes;
        result->Data.String = outBuf;
    }
}
