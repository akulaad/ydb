#pragma once

#include <ydb/library/wasm/api/compartment.h>
#include <ydb/library/wasm/api/data_transfer.h>
#include <ydb/services/udf_store/wasm/abi/udf_cpp_abi.h>

#include <yql/essentials/public/udf/udf_value.h>

namespace NKikimr::NUdfStore::NWasm {

////////////////////////////////////////////////////////////////////////////////

//! String payload resident in the current query compartment's linear memory.
//! Returned UnboxedValue is a normal refcounted String (AsStringRef works);
//! last UnRef frees via TWasmAllocationRegistry (UdfTryFreeExternalString).
class TWasmStringValue {
public:
    //! Allocate |data| in |compartment| and return a String UnboxedValue.
    //! Soft OOM / null compartment: throws.
    static NYql::NUdf::TUnboxedValuePod Make(
        NYql::NUdf::TStringRef data,
        NYdb::NWasm::IWebAssemblyCompartment* compartment,
        ui64 generation);

    //! Like Make, but uses the current query compartment (or GetCurrentCompartment);
    //! falls back to host TStringValue when no compartment is active or the string
    //! fits in the embedded buffer.
    static NYql::NUdf::TUnboxedValuePod MakePreferWasm(NYql::NUdf::TStringRef data);

    //! If |value| bytes already lie in |compartment| linear memory, set offset/length
    //! and return true (skip CopyIntoCompartment).
    static bool TryGetResidentOffset(
        const NYql::NUdf::TUnboxedValuePod& value,
        NYdb::NWasm::IWebAssemblyCompartment* compartment,
        ui64 expectedGeneration,
        uintptr_t& offset,
        ui32& length);

    //! Fill ABI string value from UnboxedValue, reusing WASM-resident bytes when possible.
    static void FillAbiStringArg(
        NYdb::NWasm::IWebAssemblyCompartment* compartment,
        const NYql::NUdf::TUnboxedValuePod& arg,
        TUnversionedValue& value,
        NYdb::NWasm::TCopyGuard& stringGuard);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NKikimr::NUdfStore::NWasm
