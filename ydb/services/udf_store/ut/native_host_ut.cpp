#include <ydb/services/udf_store/wasm/abi/udf_cpp_abi.h>
#include <ydb/services/udf_store/wasm/compile.h>
#include <ydb/services/udf_store/wasm/host.h>
#include <ydb/services/udf_store/wasm/registry_helpers.h>

#include <ydb/library/wasm/api/compartment.h>
#include <ydb/library/wasm/api/pointer.h>

#include <library/cpp/testing/unittest/registar.h>

#include <bit>
#include <cstdint>

using namespace NKikimr::NUdfStore::NWasm;
using namespace NYdb::NWasm;

namespace WAVM {
namespace Runtime {
struct ContextRuntimeData;
} // namespace Runtime
} // namespace WAVM

namespace {

extern "C" int64_t TestNativeAdd(
    WAVM::Runtime::ContextRuntimeData* /*ctx*/,
    int64_t a,
    int64_t b)
{
    return a + b;
}

constexpr TStringBuf SdkStubWast = R"(
    (module
        (import "env" "memory" (memory i64 8 2097152))
        (global $heap (mut i64) (i64.const 1024))
        (func $malloc (param $n i64) (result i64)
            (local $p i64)
            (local.set $p (global.get $heap))
            (global.set $heap
                (i64.and
                    (i64.add (i64.add (local.get $p) (local.get $n)) (i64.const 7))
                    (i64.const -8)))
            (local.get $p)
        )
        (func $free (param $p i64))
        (export "malloc" (func $malloc))
        (export "free" (func $free))
    )
)";

constexpr TStringBuf WithNativeHostWast = R"(
    (module
        (import "env" "memory" (memory i64 8 2097152))
        (import "native_math" "host_add" (func $host_add (param i64 i64) (result i64)))

        (func $udf_add (param $context i64) (param $result i64) (param $left i64) (param $right i64)
            (i32.store8
                (i64.add (local.get $result) (i64.const 2))
                (i32.const 3))
            (i64.store
                (i64.add (local.get $result) (i64.const 8))
                (call $host_add
                    (i64.load (i64.add (local.get $left) (i64.const 8)))
                    (i64.load (i64.add (local.get $right) (i64.const 8)))))
        )

        (export "udf_add" (func $udf_add))
    )
)";

TNamedModuleBytecode MakeNamedLibrary(TStringBuf name, TStringBuf wast)
{
    const auto objectCode = CompileModuleObjectCode(wast, EBytecodeFormat::HumanReadable);
    return TNamedModuleBytecode{
        .Name = TString(name),
        .Bytecode = MakeModuleBytecode(wast, objectCode, EBytecodeFormat::HumanReadable),
    };
}

} // namespace

Y_UNIT_TEST_SUITE(TWasmNativeHostImportTest) {
    Y_UNIT_TEST(CallNativeHostFromWasm) {
        EnsureUdfHostIntrinsicsRegistered();

        auto compartment = CreateEmptyImage();
        compartment->AddSdk(MakeNamedLibrary("sdk", SdkStubWast).Bytecode);

        TVector<TNativeHostFunctionBinding> bindings;
        {
            TNativeHostFunctionBinding binding;
            binding.Name = "host_add";
            binding.NativeFunction = reinterpret_cast<void*>(&TestNativeAdd);
            binding.Params = {
                EWebAssemblyValueType::Int64,
                EWebAssemblyValueType::Int64,
            };
            binding.Results = {EWebAssemblyValueType::Int64};
            bindings.push_back(std::move(binding));
        }
        compartment->AddNativeHostModule("native_math", bindings);

        TCurrentCompartmentGuard compartmentGuard(compartment.get());

        const auto moduleObjectCode = CompileModuleObjectCode(
            WithNativeHostWast,
            EBytecodeFormat::HumanReadable);
        AddPrecompiledModule(
            compartment.get(),
            MakeModuleBytecode(WithNativeHostWast, moduleObjectCode, EBytecodeFormat::HumanReadable),
            "WithNativeHost");

        const auto leftOffset = compartment->AllocateBytes(sizeof(TUnversionedValue));
        const auto rightOffset = compartment->AllocateBytes(sizeof(TUnversionedValue));
        const auto resultOffset = compartment->AllocateBytes(sizeof(TUnversionedValue));

        TUnversionedValue left = MakeEmptyValue();
        left.Type = EAbiValueType::Int64;
        left.Data.Int64 = 10;
        TUnversionedValue right = MakeEmptyValue();
        right.Type = EAbiValueType::Int64;
        right.Data.Int64 = 20;
        StoreValue(compartment.get(), leftOffset, left);
        StoreValue(compartment.get(), rightOffset, right);
        StoreValue(compartment.get(), resultOffset, MakeEmptyValue());

        InvokeUdfExport(
            compartment.get(),
            "udf_add",
            /*context*/ 0,
            resultOffset,
            {leftOffset, rightOffset});

        const auto result = *PtrFromVM(
            compartment.get(),
            std::bit_cast<TUnversionedValue*>(resultOffset));
        UNIT_ASSERT_VALUES_EQUAL(static_cast<int>(result.Type), static_cast<int>(EAbiValueType::Int64));
        UNIT_ASSERT_VALUES_EQUAL(result.Data.Int64, 30);
    }
}
