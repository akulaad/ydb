#include "allocation_registry.h"

#include <util/generic/hash.h>
#include <util/generic/hash_set.h>
#include <util/generic/vector.h>
#include <util/stream/output.h>
#include <util/string/builder.h>
#include <util/system/env.h>
#include <util/system/spinlock.h>

namespace NYdb::NWasm {

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
                    WasmStringDebug("TryFree: destination=orphaned (no FreeBytes)");
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
            WasmStringDebug(TStringBuilder()
                << "TryFree: destination=FreeBytes"
                << " offset=" << record.Offset
                << " size=" << record.Size
                << " generation=" << record.Generation);
            try {
                record.Compartment->FreeBytes(record.Offset);
            } catch (...) {
                // Never propagate from free paths used by UnRef/dtors.
                WasmStringDebug(TStringBuilder()
                    << "TryFree: FreeBytes failed"
                    << " offset=" << record.Offset
                    << " size=" << record.Size
                    << " generation=" << record.Generation);
            }
        }
        return true;
    }

    void InvalidateGeneration(ui64 generation) {
        // Called from ~TQueryCompartmentHandle while Compartment is still alive.
        // FreeBytes now; leave host ptrs in OrphanedHosts_ so a late UnRef does
        // not UdfFreeWithSize a WASM address after the compartment is destroyed.
        TVector<TAllocationRecord> toFree;
        with_lock (Lock_) {
            TVector<void*> hostPtrs;
            for (const auto& [hostPtr, record] : Allocations_) {
                if (record.Generation == generation) {
                    hostPtrs.push_back(hostPtr);
                }
            }
            toFree.reserve(hostPtrs.size());
            for (void* hostPtr : hostPtrs) {
                toFree.push_back(Allocations_.at(hostPtr));
                OrphanedHosts_.insert(hostPtr);
                Allocations_.erase(hostPtr);
            }
        }
        for (const auto& record : toFree) {
            WasmStringDebug(TStringBuilder()
                << "InvalidateGeneration: FreeBytes"
                << " offset=" << record.Offset
                << " size=" << record.Size
                << " generation=" << generation);
            try {
                record.Compartment->FreeBytes(record.Offset);
            } catch (...) {
                WasmStringDebug(TStringBuilder()
                    << "InvalidateGeneration: FreeBytes failed"
                    << " offset=" << record.Offset
                    << " size=" << record.Size
                    << " generation=" << generation);
            }
        }
        if (!toFree.empty()) {
            WasmStringDebug(TStringBuilder()
                << "InvalidateGeneration: generation=" << generation
                << " freed=" << toFree.size());
        }
    }

    size_t CountGeneration(ui64 generation) const {
        with_lock (Lock_) {
            size_t count = 0;
            for (const auto& [_, record] : Allocations_) {
                if (record.Generation == generation) {
                    ++count;
                }
            }
            return count;
        }
    }

private:
    mutable TAdaptiveLock Lock_;
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

size_t TWasmAllocationRegistry::CountGeneration(ui64 generation) const {
    return TWasmAllocationRegistryImpl::Instance().CountGeneration(generation);
}

} // namespace NYdb::NWasm
