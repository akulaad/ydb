#include <ydb/services/udf_store/wasm/abi/udf_cpp_abi.h>

using namespace NYdb::NUdfStore::NAbi;

// Resolved at query time from NATIVE_UNSAFE module "native_math"
// (required_native_modules: ["native_math"], host_exports host_add).
__attribute__((import_module("native_math"), import_name("host_add")))
extern "C" long long host_add(long long a, long long b);

extern "C" {
    __attribute__((visibility("default"))) void udf_add(
        TExpressionContext* /*context*/,
        TUnversionedValue* result,
        TUnversionedValue* arg0,
        TUnversionedValue* arg1)
    {
        if (arg0->Type == EValueType::Null || arg1->Type == EValueType::Null) {
            result->Type = EValueType::Null;
            return;
        }

        result->Type = EValueType::Int64;
        result->Data.Int64 = host_add(arg0->Data.Int64, arg1->Data.Int64);
    }
}
