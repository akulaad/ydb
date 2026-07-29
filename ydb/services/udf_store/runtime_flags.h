#pragma once

#include <ydb/core/protos/config.pb.h>

namespace NKikimr::NUdfStore {

//! Process-wide snapshot of UdfStoreConfig flags for gRPC / upload actors.
class TUdfStoreRuntimeFlags {
public:
    static void Apply(const NKikimrConfig::TUdfStoreConfig& config);
    static bool Enabled();
    static bool EnableWasmUdf();
    static bool EnableUnsafeNativeUdf();
};

} // namespace NKikimr::NUdfStore
