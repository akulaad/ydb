# ADR: Native host imports for WASM UDFs

## Status

Accepted (v1)

## Context

WASM modules need host functions beyond the static intrinsics (`AllocateBytes`,
`ThrowException`). Those host imports should be backed by **native UDF modules**
stored in UDF Store (`NATIVE_UNSAFE`), with load / unload / storage lifecycle —
not by new hard-coded symbols in `ydbd`.

Standard YQL UDF `.so` files only expose `AbiVersion` / `Register` (MiniKQL ABI).
That cannot be linked as a WAVM import `(param i64 i64) (result i64)`.

## Decision

1. Extend `NATIVE_UNSAFE` with an optional manifest `host_exports[]` listing C
   symbols and WASM numeric types (`i32`/`i64`/`f32`/`f64`).
2. On load: `dlopen` + `dlsym` into process-wide `TNativeHostModuleCatalog`;
   optionally still `LoadUdfs` when YQL ABI is present.
3. WASM manifests declare `required_native_modules: ["native_math"]`.
4. Per-query compartment: `Intrinsics::instantiateModule` under that name, then
   `TLinker` resolves `(import "native_math" "host_add" …)` like
   `createHostFunction` + custom `Resolver`.
5. Core host ABI (`AllocateBytes` / `ThrowException`) stays static under `"env"`.

## Consequences

- Host-only `.so` (no YQL `Register`) is supported when `host_exports` is set.
- Unload removes catalog entry, `FunctionRegistry::RemoveModule` for any YQL
  names, deletes the on-disk file, and unloads dependent WASM modules.
- v1 does **not** bridge MiniKQL `TUnboxedValue` UDF calls into WASM imports.
