#include "wasm_string.h"

#include "compartment_manager.h"

#include <ydb/library/wasm/api/allocation_registry.h>
#include <ydb/library/wasm/api/data_transfer.h>

#include <util/generic/yexception.h>

#include <bit>
#include <cstring>

namespace NKikimr::NUdfStore::NWasm {

using namespace NYql::NUdf;
using namespace NYdb::NWasm;
using EAbiValueType = NYdb::NUdfStore::NAbi::EValueType;

TUnboxedValuePod TWasmStringValue::Make(
    TStringRef data,
    IWebAssemblyCompartment* compartment,
    ui64 generation)
{
    if (!compartment) {
        ythrow yexception() << "TWasmStringValue::Make: compartment is null";
    }
    if (data.Size() == 0) {
        return TUnboxedValuePod::Embedded(0);
    }
    if (data.Size() <= TUnboxedValuePod::InternalBufferSize) {
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

    // Normal refcount: last UnRef → UdfTryFreeExternalString → registry TryFree.
    return TUnboxedValuePod(TStringValue(header));
}

TUnboxedValuePod TWasmStringValue::MakePreferWasm(TStringRef data)
{
    if (data.Size() <= TUnboxedValuePod::InternalBufferSize) {
        if (data.Size() == 0) {
            return TUnboxedValuePod::Embedded(0);
        }
        return TUnboxedValuePod::Embedded(data);
    }

    // Prefer query-compartment TLS (set for the whole compute Activate / scan),
    // then fall back to GetCurrentCompartment (UDF Run only).
    IWebAssemblyCompartment* compartment = nullptr;
    ui64 generation = 0;
    if (auto* handle = GetCurrentQueryCompartment()) {
        compartment = handle->Compartment.get();
        generation = handle->Generation;
    } else {
        compartment = GetCurrentCompartment();
    }

    if (!compartment) {
        // Host fallback via UDF allocator (no MiniKQL MakeString dependency).
        return TUnboxedValuePod(TStringValue(data));
    }

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
        return;
    }

    const TStringBuf string = arg.AsStringRef();
    stringGuard = CopyIntoCompartment(string, compartment);
    value.Length = static_cast<ui32>(string.size());
    value.Data.String = std::bit_cast<char*>(stringGuard.GetCopiedOffset());
}

} // namespace NKikimr::NUdfStore::NWasm

extern "C" bool UdfTryFreeExternalString(void* mem, ui64 /*size*/) {
    return NYdb::NWasm::TWasmAllocationRegistry::Instance().TryFree(mem);
}
