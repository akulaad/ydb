#include "runtime_flags.h"

#include <util/system/spinlock.h>

namespace NKikimr::NUdfStore {
namespace {

struct TFlagsState {
    TSpinLock Lock;
    bool Enabled = false;
    bool EnableWasmUdf = false;
    bool EnableUnsafeNativeUdf = false;
};

TFlagsState& State() {
    return *Singleton<TFlagsState>();
}

} // namespace

void TUdfStoreRuntimeFlags::Apply(const NKikimrConfig::TUdfStoreConfig& config) {
    with_lock (State().Lock) {
        State().Enabled = config.GetEnabled();
        State().EnableWasmUdf = config.GetEnableWasmUdf();
        State().EnableUnsafeNativeUdf = config.GetEnableUnsafeNativeUdf();
    }
}

bool TUdfStoreRuntimeFlags::Enabled() {
    with_lock (State().Lock) {
        return State().Enabled;
    }
}

bool TUdfStoreRuntimeFlags::EnableWasmUdf() {
    with_lock (State().Lock) {
        return State().EnableWasmUdf;
    }
}

bool TUdfStoreRuntimeFlags::EnableUnsafeNativeUdf() {
    with_lock (State().Lock) {
        return State().EnableUnsafeNativeUdf;
    }
}

} // namespace NKikimr::NUdfStore
