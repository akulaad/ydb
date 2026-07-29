#pragma once

#include <ydb/library/actors/core/event_local.h>
#include <ydb/library/actors/core/events.h>
#include <ydb/public/api/protos/ydb_status_codes.pb.h>
#include <ydb/public/api/protos/ydb_udf_store.pb.h>

#include <util/generic/string.h>
#include <util/generic/vector.h>

namespace NKikimr::NUdfStore {

enum EEv {
    EvStoreInitialized = EventSpaceBegin(NActors::TEvents::ES_PRIVATE),
    EvStoreInitFailed,
    EvArtifactTableInitialized,
    EvReadBodyResponse,
    EvWasmCompileResponse,
    EvLibraryCompileResponse,
    EvKvBodyWriteResponse,
    EvUploadModuleResult,
    EvDeleteModuleResult,
    EvDescribeModuleResult,
    EvListModulesResult,
    EvEnd
};

struct TEvStoreInitialized : public NActors::TEventLocal<TEvStoreInitialized, EvStoreInitialized> {
    TEvStoreInitialized(const TString& kvVolumePath)
        : KvVolumePath(kvVolumePath)
    {}
    TString KvVolumePath;
};

struct TEvArtifactTableInitialized : public NActors::TEventLocal<TEvArtifactTableInitialized, EvArtifactTableInitialized> {
    explicit TEvArtifactTableInitialized(TString artifactTablePath)
        : ArtifactTablePath(std::move(artifactTablePath))
    {}
    TString ArtifactTablePath;
};

struct TEvStoreInitFailed : public NActors::TEventLocal<TEvStoreInitFailed, EvStoreInitFailed> {
    explicit TEvStoreInitFailed(TString errorMessage)
        : ErrorMessage(std::move(errorMessage))
    {}
    TString ErrorMessage;
};

struct TEvReadBodyResponse : public NActors::TEventLocal<TEvReadBodyResponse, EvReadBodyResponse> {
    bool Success;
    TString Name;
    TString ErrorMessage;
    TString HostModuleName;
    TVector<TString> YqlModuleNames;

    TEvReadBodyResponse(bool success, const TString& name, const TString& errorMessage = {})
        : Success(success)
        , Name(name)
        , ErrorMessage(errorMessage)
    {}
};

struct TEvWasmCompileResponse : public NActors::TEventLocal<TEvWasmCompileResponse, EvWasmCompileResponse> {
    bool Success;
    bool Deferred = false;
    TString Md5;
    TString ErrorMessage;

    TEvWasmCompileResponse(
        bool success,
        const TString& md5,
        const TString& errorMessage = {},
        bool deferred = false)
        : Success(success)
        , Deferred(deferred)
        , Md5(md5)
        , ErrorMessage(errorMessage)
    {}
};

struct TEvLibraryCompileResponse : public NActors::TEventLocal<TEvLibraryCompileResponse, EvLibraryCompileResponse> {
    bool Success;
    TString LibraryName;
    TString ErrorMessage;

    TEvLibraryCompileResponse(bool success, const TString& libraryName, const TString& errorMessage = {})
        : Success(success)
        , LibraryName(libraryName)
        , ErrorMessage(errorMessage)
    {}
};

struct TEvKvBodyWriteResponse : public NActors::TEventLocal<TEvKvBodyWriteResponse, EvKvBodyWriteResponse> {
    bool Success = false;
    TString ErrorMessage;

    TEvKvBodyWriteResponse(bool success, TString errorMessage = {})
        : Success(success)
        , ErrorMessage(std::move(errorMessage))
    {}
};

struct TEvUploadModuleResult : public NActors::TEventLocal<TEvUploadModuleResult, EvUploadModuleResult> {
    Ydb::StatusIds::StatusCode Status = Ydb::StatusIds::SUCCESS;
    TString ErrorMessage;
    Ydb::UdfStore::UploadModuleResult Result;
};

struct TEvDeleteModuleResult : public NActors::TEventLocal<TEvDeleteModuleResult, EvDeleteModuleResult> {
    Ydb::StatusIds::StatusCode Status = Ydb::StatusIds::SUCCESS;
    TString ErrorMessage;
};

struct TEvDescribeModuleResult : public NActors::TEventLocal<TEvDescribeModuleResult, EvDescribeModuleResult> {
    Ydb::StatusIds::StatusCode Status = Ydb::StatusIds::SUCCESS;
    TString ErrorMessage;
    Ydb::UdfStore::DescribeModuleResult Result;
};

struct TEvListModulesResult : public NActors::TEventLocal<TEvListModulesResult, EvListModulesResult> {
    Ydb::StatusIds::StatusCode Status = Ydb::StatusIds::SUCCESS;
    TString ErrorMessage;
    Ydb::UdfStore::ListModulesResult Result;
};

} // namespace NKikimr::NUdfStore
