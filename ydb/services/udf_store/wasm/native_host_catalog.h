#pragma once

#include "types.h"

#include <util/generic/hash.h>
#include <util/generic/hash_set.h>
#include <util/generic/ptr.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/system/dynlib.h>
#include <util/system/mutex.h>

#include <memory>

namespace NMiniKQL {
class IMutableFunctionRegistry;
} // namespace NMiniKQL

namespace NKikimr::NUdfStore::NWasm {

struct TNativeHostExport {
    TString Name;
    TString Symbol;
    void* NativeFn = nullptr;
    TVector<EWasmHostValueType> Params;
    TVector<EWasmHostValueType> Results;
};

struct TNativeHostModule {
    TString ModuleName;
    TString Md5;
    TString LibraryPath;
    THolder<TDynamicLibrary> Lib;
    TVector<TNativeHostExport> Exports;
    //! YQL module names registered via LoadUdfs (may be empty for host-only .so).
    TVector<TString> YqlModuleNames;
};

using TNativeHostModulePtr = std::shared_ptr<const TNativeHostModule>;

class TNativeHostModuleCatalog {
public:
    void Register(TNativeHostModulePtr module);
    void UnregisterByMd5(const TString& md5);

    TNativeHostModulePtr FindByMd5(const TString& md5) const;
    TNativeHostModulePtr FindByModuleName(const TString& moduleName) const;

    bool ContainsModuleName(const TString& moduleName) const;

private:
    mutable TMutex Mutex_;
    THashMap<TString, TNativeHostModulePtr> ByMd5_;
    THashMap<TString, TNativeHostModulePtr> ByModuleName_;
};

TNativeHostModuleCatalog& GetNativeHostModuleCatalog();

struct TLoadNativeUdfResult {
    bool Ok = false;
    TString Error;
    TString HostModuleName;
    TVector<TString> YqlModuleNames;
};

//! Load a NATIVE_UNSAFE .so: optional host_exports into catalog + optional YQL LoadUdfs.
//! Succeeds if at least one of host catalog registration or YQL LoadUdfs succeeds.
TLoadNativeUdfResult LoadNativeUdfFromPath(
    const TString& path,
    const TString& md5,
    TStringBuf manifestJson,
    NMiniKQL::IMutableFunctionRegistry* functionRegistry);

} // namespace NKikimr::NUdfStore::NWasm
