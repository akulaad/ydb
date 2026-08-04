#include "allocation_registry.h"

#include <util/generic/hash.h>
#include <util/generic/hash_set.h>
#include <util/generic/vector.h>
#include <util/system/spinlock.h>

namespace NYdb::NWasm {

namespace {

struct TAllocationRecord {
    IWebAssemblyCompartment* Compartment = nullptr;
    uintptr_t Offset = 0;
    size_t Size = 0;
    ui64 Generation = 0;
};

class TWasmAllocationRegistryImpl {
public:
    static TWasmAllocationRegistryImpl& Instance() {
        static TWasmAllocationRegistryImpl registry;
        return registry;
    }

    void Register(
        void* hostPtr,
        IWebAssemblyCompartment* compartment,
        uintptr_t offset,
        size_t size,
        ui64 generation)
    {
        if (!hostPtr || !compartment || offset == 0) {
            return;
        }
        with_lock (Lock_) {
            OrphanedHosts_.erase(hostPtr);
            Allocations_[hostPtr] = TAllocationRecord{
                .Compartment = compartment,
                .Offset = offset,
                .Size = size,
                .Generation = generation,
            };
        }
    }

    bool TryFree(void* hostPtr) {
        if (!hostPtr) {
            return false;
        }

        TAllocationRecord record;
        bool doFree = false;
        {
            with_lock (Lock_) {
                // Compartment already torn down for this ptr — swallow free.
                if (OrphanedHosts_.erase(hostPtr)) {
                    return true;
                }
                auto it = Allocations_.find(hostPtr);
                if (it == Allocations_.end()) {
                    return false;
                }
                record = it->second;
                Allocations_.erase(it);
                doFree = true;
            }
        }

        if (doFree) {
            try {
                record.Compartment->FreeBytes(record.Offset);
            } catch (...) {
                // Never propagate from free paths used by UnRef/dtors.
            }
        }
        return true;
    }

    void InvalidateGeneration(ui64 generation) {
        with_lock (Lock_) {
            TVector<void*> toOrphan;
            for (const auto& [hostPtr, record] : Allocations_) {
                if (record.Generation == generation) {
                    toOrphan.push_back(hostPtr);
                }
            }
            for (void* hostPtr : toOrphan) {
                OrphanedHosts_.insert(hostPtr);
                Allocations_.erase(hostPtr);
            }
        }
    }

private:
    TAdaptiveLock Lock_;
    THashMap<void*, TAllocationRecord> Allocations_;
    //! Host pointers whose generation was invalidated before TryFree.
    //! TryFree returns true without FreeBytes so UnRef does not call UdfFreeWithSize
    //! on a WASM address after the compartment is gone.
    THashSet<void*> OrphanedHosts_;
};

} // namespace

TWasmAllocationRegistry& TWasmAllocationRegistry::Instance() {
    static TWasmAllocationRegistry facade;
    return facade;
}

void TWasmAllocationRegistry::Register(
    void* hostPtr,
    IWebAssemblyCompartment* compartment,
    uintptr_t offset,
    size_t size,
    ui64 generation)
{
    TWasmAllocationRegistryImpl::Instance().Register(
        hostPtr, compartment, offset, size, generation);
}

bool TWasmAllocationRegistry::TryFree(void* hostPtr) {
    return TWasmAllocationRegistryImpl::Instance().TryFree(hostPtr);
}

void TWasmAllocationRegistry::InvalidateGeneration(ui64 generation) {
    TWasmAllocationRegistryImpl::Instance().InvalidateGeneration(generation);
}

} // namespace NYdb::NWasm
