#include "native_host_catalog.h"
#include "manifest.h"

#include <yql/essentials/minikql/mkql_function_registry.h>

#include <util/generic/yexception.h>
#include <util/stream/str.h>
#include <util/string/builder.h>

#ifndef _win32_
#include <dlfcn.h>
#endif

namespace NKikimr::NUdfStore::NWasm {

namespace {

ui32 OpenFlags() {
#ifdef _win32_
    return 0;
#else
    return RTLD_NOW | RTLD_LOCAL;
#endif
}

} // namespace

void TNativeHostModuleCatalog::Register(TNativeHostModulePtr module) {
    Y_ENSURE(module);
    Y_ENSURE(!module->Md5.empty());
    Y_ENSURE(!module->ModuleName.empty());

    with_lock (Mutex_) {
        if (auto it = ByModuleName_.find(module->ModuleName); it != ByModuleName_.end()) {
            if (it->second->Md5 != module->Md5) {
                ByMd5_.erase(it->second->Md5);
            }
        }
        if (auto it = ByMd5_.find(module->Md5); it != ByMd5_.end()) {
            ByModuleName_.erase(it->second->ModuleName);
        }
        ByMd5_[module->Md5] = module;
        ByModuleName_[module->ModuleName] = module;
    }
}

void TNativeHostModuleCatalog::UnregisterByMd5(const TString& md5) {
    with_lock (Mutex_) {
        auto it = ByMd5_.find(md5);
        if (it == ByMd5_.end()) {
            return;
        }
        ByModuleName_.erase(it->second->ModuleName);
        ByMd5_.erase(it);
    }
}

TNativeHostModulePtr TNativeHostModuleCatalog::FindByMd5(const TString& md5) const {
    with_lock (Mutex_) {
        auto it = ByMd5_.find(md5);
        return it == ByMd5_.end() ? nullptr : it->second;
    }
}

TNativeHostModulePtr TNativeHostModuleCatalog::FindByModuleName(const TString& moduleName) const {
    with_lock (Mutex_) {
        auto it = ByModuleName_.find(moduleName);
        return it == ByModuleName_.end() ? nullptr : it->second;
    }
}

bool TNativeHostModuleCatalog::ContainsModuleName(const TString& moduleName) const {
    return FindByModuleName(moduleName) != nullptr;
}

TNativeHostModuleCatalog& GetNativeHostModuleCatalog() {
    static TNativeHostModuleCatalog catalog;
    return catalog;
}

TLoadNativeUdfResult LoadNativeUdfFromPath(
    const TString& path,
    const TString& md5,
    TStringBuf manifestJson,
    NMiniKQL::IMutableFunctionRegistry* functionRegistry)
{
    TLoadNativeUdfResult result;

    TNativeHostManifest hostManifest;
    try {
        hostManifest = ParseNativeHostManifest(manifestJson);
    } catch (const std::exception& e) {
        result.Error = TStringBuilder() << "invalid native host manifest: " << e.what();
        return result;
    }

    const bool wantHost = !hostManifest.HostExports.empty();
    bool hostOk = false;
    bool yqlOk = false;

    if (wantHost) {
        try {
            auto module = std::make_shared<TNativeHostModule>();
            module->ModuleName = hostManifest.ModuleName;
            module->Md5 = md5;
            module->LibraryPath = path;
            module->Lib = MakeHolder<TDynamicLibrary>();
            module->Lib->Open(path.data(), OpenFlags());

            for (const auto& exportDesc : hostManifest.HostExports) {
                void* sym = module->Lib->SymOptional(exportDesc.Symbol.data());
                if (!sym) {
                    ythrow yexception()
                        << "symbol '" << exportDesc.Symbol
                        << "' not found in '" << path << "' for host export '"
                        << exportDesc.Name << "'";
                }
                TNativeHostExport exp;
                exp.Name = exportDesc.Name;
                exp.Symbol = exportDesc.Symbol;
                exp.NativeFn = sym;
                exp.Params = exportDesc.Params;
                exp.Results = exportDesc.Results;
                module->Exports.push_back(std::move(exp));
            }

            if (functionRegistry) {
                NMiniKQL::TUdfModuleRemappings remappings;
                THashSet<TString> modules;
                try {
                    functionRegistry->LoadUdfs(path, remappings, 0, {}, &modules);
                    for (const auto& name : modules) {
                        module->YqlModuleNames.push_back(name);
                    }
                    yqlOk = !modules.empty();
                } catch (const std::exception&) {
                    // Host-only .so without YQL ABI is allowed.
                }
            }

            result.HostModuleName = module->ModuleName;
            result.YqlModuleNames = module->YqlModuleNames;
            GetNativeHostModuleCatalog().Register(std::move(module));
            hostOk = true;
        } catch (const std::exception& e) {
            result.Error = TStringBuilder() << "failed to load native host module: " << e.what();
            return result;
        }
    } else if (functionRegistry) {
        try {
            NMiniKQL::TUdfModuleRemappings remappings;
            THashSet<TString> modules;
            functionRegistry->LoadUdfs(path, remappings, 0, {}, &modules);
            if (modules.empty()) {
                result.Error = TStringBuilder()
                    << "no UDF modules were registered from '" << path
                    << "' and host_exports is empty";
                return result;
            }
            for (const auto& name : modules) {
                result.YqlModuleNames.push_back(name);
            }
            yqlOk = true;
        } catch (const std::exception& e) {
            result.Error = TStringBuilder()
                << "failed to load UDF into function registry: " << e.what();
            return result;
        }
    } else {
        result.Error = "function registry is not available and host_exports is empty";
        return result;
    }

    if (!hostOk && !yqlOk) {
        if (result.Error.empty()) {
            result.Error = "native UDF load produced neither host exports nor YQL modules";
        }
        return result;
    }

    result.Ok = true;
    result.Error.clear();
    return result;
}

} // namespace NKikimr::NUdfStore::NWasm
