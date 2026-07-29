#pragma once

//! Native host ABI for UDF modules that expose C functions as WASM imports.
//!
//! Each host_exports[] entry in the native module manifest maps to a C symbol
//! with WAVM intrinsic calling convention:
//!   - First parameter is WAVM::Runtime::ContextRuntimeData* (injected by runtime;
//!     the WASM guest does not pass it).
//!   - Remaining parameters / result use i32/i64/f32/f64 matching the manifest.
//!
//! Example (manifest):
//!   {"module_name":"native_math","host_exports":[
//!      {"name":"host_add","symbol":"native_add","params":["i32","i32"],"results":["i32"]}]}
//!
//! Example (C):
//!   #include <WAVM/Runtime/Intrinsics.h>  // or forward-declare ContextRuntimeData
//!   extern "C" int32_t native_add(
//!       WAVM::Runtime::ContextRuntimeData* /*ctx*/,
//!       int32_t a, int32_t b) { return a + b; }
//!
//! WASM guest:
//!   (import "native_math" "host_add" (func (param i32 i32) (result i32)))

namespace NKikimr::NUdfStore::NWasm::NNativeHostAbi {

// Marker namespace for documentation / include discovery.

} // namespace NKikimr::NUdfStore::NWasm::NNativeHostAbi
