# WASM UDF limitations

The following limitations and common failures apply when working with [WASM UDFs](index.md). Each item follows from the current execution model (per-query WebAssembly sandbox and library linking). How to write modules with an sdk, without one, and with `object_framework` is covered in [Writing and building](writing.md).

## Configuration and availability {#config}

- UDF Store and WASM are disabled by default. You need `udf_store_config.enabled` and `enable_wasm_udf` — see [configuration](../../../reference/configuration/udf_store_config.md).
- Functions are available in YQL only after `CompileStatus = "ready"`. Status and error text are in [`.sys/udf_modules`](../../system-views.md#udf-modules).

## Libraries and allocation {#libraries}

- If the module allocates memory through the runtime, the first library in `required_libraries` must export working `malloc` / `free`. An empty stub sdk without `malloc` crashes on allocation.
- Empty `required_libraries: []` fits only narrow scenarios without wasm `malloc` (for example, minimal integer arithmetic). Paths with strings and results usually need an sdk.
- In one query that uses several WASM modules, the first encountered "first" library becomes the shared runtime. Different sdks for different modules in the same query are not supported predictably — use a shared runtime.

## Isolation and state {#isolation}

- WASM state (including TypeConfig object handles) is not shared across queries: the object is recreated in each query.
- A TypeConfig handle is valid only in the current query; do not store it across queries.

## Upload and names {#upload}

- Re-uploading a module with the same `module_name` without deleting the previous one causes a name conflict in the registry. Delete the old module first.
- There is no public product CLI for upload yet; use tooling that writes to the store tables — see [Managing modules](../managing.md#upload).

## See also

- [Writing and building](writing.md)
- [Libraries](libraries.md)
- [Manifest](manifest.md)
- [Native unsafe UDFs](../native-unsafe.md)
