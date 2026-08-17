# WASM libraries

A **library** in [UDF Store](../../../concepts/glossary.md#udf-store) is a row with `ModuleType = LIBRARY`: a WebAssembly module that [WASM UDFs](index.md) depend on. A library does not register YQL functions by itself; it is linked into the UDF execution environment.

Typical roles:

- **runtime / sdk** — the first library in `required_libraries`: libc/malloc and other runtime (linked as the `"env"` module);
- **helper libraries** — shared functions imported by the UDF under a module name (for example, `"helpers"`).

## required_libraries {#required-libraries}

In the UDF [manifest](manifest.md), `required_libraries` is **ordered**:

1. The first entry is the runtime (sdk), attached as `"env"`.
2. The rest are additional modules under their own names.

An empty list `[]` is allowed for minimal modules without libc; see [limitations](limitations.md).

## Upload order {#upload-order}

1. Upload each library with `--kind library --library-name <name>`.
2. Wait for `CompileStatus = "ready"` for the libraries in [`.sys/udf_modules`](../../system-views.md#udf-modules).
3. Upload the WASM UDF with a manifest whose `required_libraries` lists those names.
4. Wait for `CompileStatus = "ready"` for the UDF.

UDF compilation does not start until all listed libraries are ready.

Example for a module that needs sdk and helpers:

```bash
# 1. runtime
upload_udf ... --kind library --library-name sdk --udf-file sdk.wasm
# 2. helper library
upload_udf ... --kind library --library-name helpers --udf-file helpers.wasm
# 3. UDF with required_libraries: ["sdk", "helpers"]
upload_udf ... --kind udf --type WASM --udf-file with_helpers.wasm --manifest manifest.json
```

```yql
SELECT WithHelpers::scale(7);  -- 21
```

How to build the sdk, a helper library, and a UDF that imports symbols with `import_module` is described in [Writing and building](writing.md#build).

## Delete and dependencies {#delete}

When a library is deleted or replaced, dependent WASM UDFs may be unloaded and may need recompilation after dependencies are restored. Before deleting a library, make sure no active module manifests still reference it.

## See also

- [Writing and building](writing.md)
- [Manifest](manifest.md)
- [Managing modules](../managing.md)
- [Limitations](limitations.md)
