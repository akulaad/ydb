# ADR: Public gRPC Upload API for UDF Store

## Status

Accepted (v1)

## Context

Module upload into YDB UDF Store historically went through an out-of-band helper
(`ydb/tests/functional/udf_store/upload_udf`): Query `UPSERT` into
`.metadata/udf_store/modules` (+ `module_chunks`) and `kv_volume_tool` for
`NATIVE_UNSAFE` bodies. There was no public API; clients needed direct access
to system tables / KV volume.

## Decision

1. Add public service `Ydb.UdfStore.V1.UdfStoreService` with unary RPCs:
   `UploadModule`, `DeleteModule`, `DescribeModule`, `ListModules`.
2. Server validates manifests / feature flags, chunks WASM/LIBRARY bodies at
   8 MiB, writes native bodies via internal KV `ExecuteTransaction`, and
   upserts the same `modules` rows the helper uses (`compile_status=pending`
   for WASM/LIBRARY).
3. Compile remains asynchronous: clients poll `DescribeModule` or
   `.sys/udf_modules`.
4. Access in v1: `IsAdministrator` (or empty admin SIDs in test clusters).
5. Gate: `UdfStoreConfig.Enabled` plus `EnableWasmUdf` / `EnableUnsafeNativeUdf`.
6. **Keep** legacy `upload_udf` unchanged for tests/debug; both paths write the
   same tables and `TUdfStoreService` picks up the snapshot identically.

## Consequences

- Product clients no longer need `.metadata` write access.
- Unary upload is capped at 64 MiB; streaming / resumable upload is out of scope.
- Long-running `Operation` for compile is out of scope (Describe is enough).
