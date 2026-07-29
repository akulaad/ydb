#include "compartment_manager.h"

#include "host.h"
#include "native_host_catalog.h"
#include "registry_helpers.h"
#include "types.h"

#include <util/generic/hash.h>
#include <util/generic/hash_set.h>
#include <util/generic/yexception.h>
#include <util/string/builder.h>

#include <atomic>
#include <string>

namespace NKikimr::NUdfStore::NWasm {

namespace {

thread_local TQueryCompartmentHandle* CurrentQueryCompartment = nullptr;

ui64 NextCompartmentGeneration() {
    static std::atomic<ui64> counter{0};
    return ++counter;
}

NYdb::NWasm::EWebAssemblyValueType ToWebAssemblyValueType(EWasmHostValueType type) {
    switch (type) {
        case EWasmHostValueType::I32:
            return NYdb::NWasm::EWebAssemblyValueType::Int32;
        case EWasmHostValueType::I64:
            return NYdb::NWasm::EWebAssemblyValueType::Int64;
        case EWasmHostValueType::F32:
            return NYdb::NWasm::EWebAssemblyValueType::Float32;
        case EWasmHostValueType::F64:
            return NYdb::NWasm::EWebAssemblyValueType::Float64;
    }
    ythrow yexception() << "Unsupported native host value type";
}

void BindExport(
    TQueryCompartmentHandle& handle,
    const TString& moduleName,
    const TString& exportName)
{
    if (exportName.empty()) {
        return;
    }
    const auto key = MakeExportKey(moduleName, exportName);
    if (handle.Exports.contains(key)) {
        return;
    }
    auto* exportPtr = handle.Compartment->GetFunction(std::string(exportName));
    if (!exportPtr) {
        ythrow yexception()
            << "Missing WASM export '" << exportName
            << "' in module '" << moduleName << "'";
    }
    handle.Exports.emplace(key, exportPtr);
}

void InstallNativeHostModules(
    NYdb::NWasm::IWebAssemblyCompartment* compartment,
    const TVector<TString>& nativeModuleNames)
{
    auto& catalog = GetNativeHostModuleCatalog();
    for (const auto& name : nativeModuleNames) {
        const auto module = catalog.FindByModuleName(name);
        if (!module) {
            ythrow yexception()
                << "Required native host module '" << name
                << "' is not loaded in the native host catalog";
        }

        TVector<NYdb::NWasm::TNativeHostFunctionBinding> bindings;
        bindings.reserve(module->Exports.size());
        for (const auto& exp : module->Exports) {
            NYdb::NWasm::TNativeHostFunctionBinding binding;
            binding.Name = exp.Name;
            binding.NativeFunction = exp.NativeFn;
            for (auto param : exp.Params) {
                binding.Params.push_back(ToWebAssemblyValueType(param));
            }
            for (auto result : exp.Results) {
                binding.Results.push_back(ToWebAssemblyValueType(result));
            }
            bindings.push_back(std::move(binding));
        }
        compartment->AddNativeHostModule(module->ModuleName, bindings);
    }
}

} // namespace

TString MakeExportKey(TStringBuf moduleName, TStringBuf functionName) {
    return TStringBuilder() << moduleName << "::" << functionName;
}

TQueryCompartmentHandlePtr TWasmCompartmentManager::Acquire(
    const TVector<TString>& moduleNames) const
{
    EnsureUdfHostIntrinsicsRegistered();

    if (moduleNames.empty()) {
        return {};
    }

    const auto artifacts = Catalog_.ResolveModules(moduleNames);

    THashSet<TString> loadedLibraries;
    TVector<TNamedModuleBytecode> libraries;
    THashSet<TString> nativeModuleNamesSet;
    TVector<TString> nativeModuleNames;

    for (const auto& artifact : artifacts) {
        // Index catalog libraries by name, then emit them in RequiredLibraries
        // order so sdk (env) is always linked before the UDF module below.
        THashMap<TString, TNamedModuleBytecode> byName;
        for (const auto& library : artifact->Libraries) {
            if (library.Name.empty()) {
                ythrow yexception()
                    << "WASM UDF '" << artifact->ModuleName
                    << "' has a library entry with empty name";
            }
            if (!library.Bytecode.ObjectCode) {
                ythrow yexception()
                    << "WASM UDF '" << artifact->ModuleName
                    << "' is missing object code for required library '"
                    << library.Name << "'";
            }
            byName.emplace(library.Name, library);
        }
        for (const auto& required : artifact->Manifest.RequiredLibraries) {
            const auto* library = byName.FindPtr(required);
            if (!library) {
                ythrow yexception()
                    << "WASM UDF '" << artifact->ModuleName
                    << "' requires library '" << required
                    << "' but it was not loaded into the module catalog";
            }
            if (loadedLibraries.insert(required).second) {
                libraries.push_back(*library);
            }
        }
        for (const auto& nativeName : artifact->Manifest.RequiredNativeModules) {
            if (nativeModuleNamesSet.insert(nativeName).second) {
                nativeModuleNames.push_back(nativeName);
            }
        }
    }

    auto handle = std::make_unique<TQueryCompartmentHandle>();
    handle->Generation = NextCompartmentGeneration();
    // CreateRegistryCompartment clones SDK from CreateImageFromSdk cache ("env");
    // only then do we AddPrecompiledModule the UDF (e.g. Md5).
    handle->Compartment = CreateRegistryCompartment(libraries);

    InstallNativeHostModules(handle->Compartment.get(), nativeModuleNames);

    for (const auto& artifact : artifacts) {
        AddPrecompiledModule(
            handle->Compartment.get(),
            artifact->ModuleBytecode,
            artifact->ModuleName);

        for (const auto& function : artifact->Manifest.Functions) {
            if (function.Binding == EWasmUdfBinding::TypeConfigCallable) {
                BindExport(*handle, artifact->ModuleName, function.CreateExport);
                BindExport(*handle, artifact->ModuleName, function.CallExport);
                BindExport(*handle, artifact->ModuleName, function.DestroyExport);
            } else {
                BindExport(*handle, artifact->ModuleName, TString(PlainWasmExport(function)));
            }
        }
    }

    return handle;
}

TWasmCompartmentManager& GetWasmCompartmentManager() {
    static TWasmCompartmentManager manager;
    return manager;
}

TCurrentQueryCompartmentGuard::TCurrentQueryCompartmentGuard(TQueryCompartmentHandle* handle)
    : Previous_(CurrentQueryCompartment)
    , Active_(true)
{
    CurrentQueryCompartment = handle;
}

TCurrentQueryCompartmentGuard::~TCurrentQueryCompartmentGuard() {
    if (Active_) {
        CurrentQueryCompartment = Previous_;
    }
}

TCurrentQueryCompartmentGuard::TCurrentQueryCompartmentGuard(
    TCurrentQueryCompartmentGuard&& other) noexcept
    : Previous_(other.Previous_)
    , Active_(other.Active_)
{
    other.Active_ = false;
}

TCurrentQueryCompartmentGuard& TCurrentQueryCompartmentGuard::operator=(
    TCurrentQueryCompartmentGuard&& other) noexcept
{
    if (this != &other) {
        if (Active_) {
            CurrentQueryCompartment = Previous_;
        }
        Previous_ = other.Previous_;
        Active_ = other.Active_;
        other.Active_ = false;
    }
    return *this;
}

TQueryCompartmentHandle* GetCurrentQueryCompartment() {
    return CurrentQueryCompartment;
}

} // namespace NKikimr::NUdfStore::NWasm
