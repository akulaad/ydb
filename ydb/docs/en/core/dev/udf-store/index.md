# User-defined UDFs

[UDF Store](../../concepts/glossary.md#udf-store) is a {{ ydb-short-name }} mechanism that lets you upload custom [UDF](../../concepts/glossary.md#udf) modules into a database and call them from [YQL](../../concepts/glossary.md#yql) alongside [built-in UDFs](../../yql/reference/udf/list/index.md).

Unlike built-in libraries, UDF Store modules are not shipped with the server: you load them into the store system tables, after which the cluster registers the functions and makes them available in queries.

## Module types {#module-types}

| Type (`ModuleType`) | Purpose |
|---|---|
| `WASM` | Executable [WASM UDF](../../concepts/glossary.md#wasm-udf): WebAssembly bytecode and a [manifest](wasm/manifest.md) |
| `LIBRARY` | Dependency library for WASM (runtime/sdk and helper modules). Not callable as `Module::func` by itself |
| `NATIVE_UNSAFE` | Native `.so` UDF module. Requires a separate flag and carries different risks than WASM |

## Sections

- [Managing modules](managing.md) — enabling the store, upload and delete, monitoring via `.sys/udf_modules`
- [WASM UDFs](wasm/index.md): lifecycle, [writing and building](wasm/writing.md), manifest, libraries, and WASM limitations
- [Native unsafe UDFs](native-unsafe.md) — native modules

## See also

- [Built-in UDFs](../../yql/reference/udf/list/index.md)
- [`udf_store_config` configuration](../../reference/configuration/udf_store_config.md)
- [`.sys/udf_modules` system view](../system-views.md#udf-modules)
