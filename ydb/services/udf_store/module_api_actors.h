#pragma once

#include "events.h"
#include "table_query.h"

#include <ydb/library/actors/core/actor.h>
#include <ydb/public/api/protos/ydb_udf_store.pb.h>

#include <util/generic/string.h>

namespace NKikimr::NUdfStore {

constexpr ui64 MaxModuleContentSize = 64ull * 1024 * 1024;

struct TUploadModuleParams {
    Ydb::UdfStore::ModuleType Type = Ydb::UdfStore::MODULE_TYPE_UNSPECIFIED;
    TString Name;
    TString Manifest;
    TString Content;
    ui64 Version = 1;
    TString ExpectedMd5;
};

NActors::IActor* CreateUdfModuleUploadActor(NActors::TActorId replyTo, TUploadModuleParams params);
NActors::IActor* CreateUdfModuleDeleteActor(
    NActors::TActorId replyTo,
    TString md5,
    TString name,
    Ydb::UdfStore::ModuleType type);
NActors::IActor* CreateUdfModuleDescribeActor(
    NActors::TActorId replyTo,
    TString md5,
    TString name,
    Ydb::UdfStore::ModuleType type);
NActors::IActor* CreateUdfModuleListActor(
    NActors::TActorId replyTo,
    Ydb::UdfStore::ModuleType type,
    TString namePrefix,
    ui64 limit);

} // namespace NKikimr::NUdfStore
