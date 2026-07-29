#pragma once

#include "types.h"

namespace NKikimr::NUdfStore::NWasm {

TWasmManifest ParseManifest(TStringBuf manifestJson);

//! Parses a native-host provider manifest (`module_name` + `host_exports`).
//! Returns empty HostExports when the JSON has no host_exports field.
TNativeHostManifest ParseNativeHostManifest(TStringBuf manifestJson);

bool HasHostExports(TStringBuf manifestJson);

} // namespace NKikimr::NUdfStore::NWasm
