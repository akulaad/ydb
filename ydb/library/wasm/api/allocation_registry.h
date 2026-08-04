#pragma once

#include "compartment.h"

#include <util/system/types.h>

namespace NYdb::NWasm {

////////////////////////////////////////////////////////////////////////////////

//! Process-wide map of host pointers into WASM linear memory to their
//! (compartment, offset, generation). Clients Register on AllocateBytes;
//! TryFree calls FreeBytes when the owner drops the last reference.
class TWasmAllocationRegistry {
public:
    static TWasmAllocationRegistry& Instance();

    void Register(
        void* hostPtr,
        IWebAssemblyCompartment* compartment,
        uintptr_t offset,
        size_t size,
        ui64 generation);

    //! If |hostPtr| is registered, FreeBytes and erase; returns true.
    //! Unknown pointer → false (caller may use UdfFreeWithSize).
    //! After InvalidateGeneration for this ptr → erase without FreeBytes,
    //! returns true (so callers do not UdfFreeWithSize a WASM address).
    bool TryFree(void* hostPtr);

    //! Mark all allocations of |generation| orphaned (no FreeBytes).
    //! Call before destroying the compartment for that acquire.
    void InvalidateGeneration(ui64 generation);

private:
    TWasmAllocationRegistry() = default;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYdb::NWasm
