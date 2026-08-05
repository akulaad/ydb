#include "wasm_string.h"

#include "compartment_manager.h"

#include <ydb/library/wasm/api/allocation_registry.h>
#include <ydb/library/wasm/api/data_transfer.h>

#include <util/generic/yexception.h>
#include <util/stream/output.h>
#include <util/string/builder.h>
#include <util/system/compiler.h>
#include <util/system/env.h>

#include <bit>
#include <cstring>

namespace NKikimr::NUdfStore::NWasm {

using namespace NYql::NUdf;
using namespace NYdb::NWasm;
using EAbiValueType = NYdb::NUdfStore::NAbi::EValueType;

namespace {

bool IsWasmStringDebugEnabled() {
    static const bool enabled = [] {
        const TString v = GetEnv("YDB_WASM_STRING_DEBUG");
        return v == "1" || v == "true" || v == "yes";
    }();
    return enabled;
}

void WasmStringDebug(const TString& message) {
    if (IsWasmStringDebugEnabled()) {
        Cerr << "[WasmString] " << message << Endl;
    }
}

} // namespace

TUnboxedValuePod TWasmStringValue::Make(
    TStringRef data,
    IWebAssemblyCompartment* compartment,
    ui64 generation)
{
    if (!compartment) {
        ythrow yexception() << "TWasmStringValue::Make: compartment is null";
    }
    if (data.Size() == 0) {
        WasmStringDebug("Make: destination=embedded size=0");
        return TUnboxedValuePod::Embedded(0);
    }
    if (data.Size() <= TUnboxedValuePod::InternalBufferSize) {
        WasmStringDebug(TStringBuilder()
            << "Make: destination=embedded size=" << data.Size());
        return TUnboxedValuePod::Embedded(data);
    }

    const ui64 allocBytes = TStringValue::AllocationBytes(data.Size());
    auto buffer = TGuestBuffer::Allocate(compartment, allocBytes);
    auto* header = TStringValue::ConstructInPlace(buffer.HostData(), data.Size(), data.Size());
    std::memcpy(header->Data(), data.Data(), data.Size());

    const uintptr_t offset = buffer.Offset();
    TWasmAllocationRegistry::Instance().Register(
        header, compartment, offset, allocBytes, generation);
    buffer.Release();

    WasmStringDebug(TStringBuilder()
        << "Make: destination=wasm_linear_memory size=" << data.Size()
        << " offset=" << offset << " generation=" << generation);

    // Normal refcount: last UnRef → UdfTryFreeExternalString → registry TryFree.
    return TUnboxedValuePod(TStringValue(header));
}

TUnboxedValuePod TWasmStringValue::MakePreferWasm(TStringRef data)
{
    if (data.Size() <= TUnboxedValuePod::InternalBufferSize) {
        if (data.Size() == 0) {
            WasmStringDebug("MakePreferWasm: destination=embedded size=0");
            return TUnboxedValuePod::Embedded(0);
        }
        WasmStringDebug(TStringBuilder()
            << "MakePreferWasm: destination=embedded size=" << data.Size());
        return TUnboxedValuePod::Embedded(data);
    }

    // Prefer query-compartment TLS (set for the whole compute Activate / scan),
    // then fall back to GetCurrentCompartment (UDF Run only).
    IWebAssemblyCompartment* compartment = nullptr;
    ui64 generation = 0;
    const char* compartmentSource = "none";
    if (auto* handle = GetCurrentQueryCompartment()) {
        compartment = handle->Compartment.get();
        generation = handle->Generation;
        compartmentSource = "query_compartment";
    } else {
        compartment = GetCurrentCompartment();
        if (compartment) {
            compartmentSource = "current_compartment";
        }
    }

    if (!compartment) {
        // Host fallback via UDF allocator (no MiniKQL MakeString dependency).
        WasmStringDebug(TStringBuilder()
            << "MakePreferWasm: destination=host_fallback size=" << data.Size()
            << " (no active compartment)");
        return TUnboxedValuePod(TStringValue(data));
    }

    WasmStringDebug(TStringBuilder()
        << "MakePreferWasm: destination=wasm via " << compartmentSource
        << " size=" << data.Size() << " generation=" << generation);
    return Make(data, compartment, generation);
}

bool TWasmStringValue::TryGetResidentOffset(
    const TUnboxedValuePod& value,
    IWebAssemblyCompartment* compartment,
    ui64 expectedGeneration,
    uintptr_t& offset,
    ui32& length)
{
    if (!compartment || !value) {
        return false;
    }
    // Only heap/WASM-backed strings; embedded payloads are not in linear memory.
    if (!value.IsString()) {
        return false;
    }

    if (expectedGeneration != 0) {
        if (auto* handle = GetCurrentQueryCompartment()) {
            if (handle->Generation != expectedGeneration || handle->Compartment.get() != compartment) {
                return false;
            }
        }
    }

    const TStringRef ref = value.AsStringRef();
    length = ref.Size();
    if (length == 0) {
        offset = 0;
        return true;
    }

    try {
        offset = compartment->GetCompartmentOffset(const_cast<char*>(ref.Data()));
        compartment->GetHostPointer(offset, length);
        return true;
    } catch (...) {
        return false;
    }
}

void TWasmStringValue::FillAbiStringArg(
    IWebAssemblyCompartment* compartment,
    const TUnboxedValuePod& arg,
    TUnversionedValue& value,
    TCopyGuard& stringGuard)
{
    value.Type = EAbiValueType::String;

    uintptr_t residentOffset = 0;
    ui32 residentLength = 0;
    ui64 generation = 0;
    if (auto* handle = GetCurrentQueryCompartment()) {
        generation = handle->Generation;
    }

    if (TryGetResidentOffset(arg, compartment, generation, residentOffset, residentLength)) {
        value.Length = residentLength;
        value.Data.String = std::bit_cast<char*>(residentOffset);
        WasmStringDebug(TStringBuilder()
            << "FillAbiStringArg: destination=reuse_wasm_resident"
            << " length=" << residentLength << " offset=" << residentOffset);
        return;
    }

    const TStringBuf string = arg.AsStringRef();
    stringGuard = CopyIntoCompartment(string, compartment);
    value.Length = static_cast<ui32>(string.size());
    value.Data.String = std::bit_cast<char*>(stringGuard.GetCopiedOffset());
    WasmStringDebug(TStringBuilder()
        << "FillAbiStringArg: destination=CopyIntoCompartment"
        << " length=" << string.size()
        << " offset=" << stringGuard.GetCopiedOffset());
}

} // namespace NKikimr::NUdfStore::NWasm

// Must be a strong GLOBAL symbol so it overrides the Y_WEAK stub in
// yql/essentials/public/udf/udf_allocator.cpp. With hidden visibility the
// definition becomes LOCAL and UnRef never reaches TWasmAllocationRegistry.
extern "C" __attribute__((visibility("default"), used)) bool UdfTryFreeExternalString(void* mem, ui64 /*size*/) {
    return NYdb::NWasm::TWasmAllocationRegistry::Instance().TryFree(mem);
}
