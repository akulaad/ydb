#pragma once

#include "events.h"

#include <ydb/library/actors/core/actor_bootstrapped.h>
#include <ydb/library/actors/core/hfunc.h>
#include <ydb/core/base/tablet_pipe.h>
#include <ydb/core/keyvalue/keyvalue_events.h>
#include <ydb/core/tx/scheme_cache/scheme_cache.h>

namespace NKikimr::NUdfStore {

//! Writes a native UDF body into the KV volume `.metadata/udf_store/binaries` under key=md5.
NActors::IActor* CreateKvBodyWriteActor(
    NActors::TActorId replyTo,
    TString volumePath,
    TString md5Key,
    TString content);

} // namespace NKikimr::NUdfStore
