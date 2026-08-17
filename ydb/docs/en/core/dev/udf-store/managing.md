# Managing UDF Store modules

To upload and use [user-defined UDFs](index.md), enable [UDF Store](../../concepts/glossary.md#udf-store) in the cluster configuration, write the module into the store, and wait until the service registers it for YQL calls.

## Enabling the store {#enable}

The configuration section is [`udf_store_config`](../../reference/configuration/udf_store_config.md). At minimum, set `enabled` to `true`.

For [WASM UDFs](wasm/index.md), also enable `enable_wasm_udf`:

```yaml
udf_store_config:
  enabled: true
  enable_wasm_udf: true
```

For [native unsafe UDFs](native-unsafe.md), set `enable_unsafe_native_udf` and `unsafe_native_udf_dir`.

All flags are off by default.

## Lifecycle {#lifecycle}

1. Upload the module binary and metadata into tables under `.metadata/udf_store`.
2. Wait for UDF Store to process it (for WASM — AOT compilation until `CompileStatus = ready`).
3. Call module functions from YQL (for example, `ModuleName::func(...)`).
4. Delete the module from the store when needed — its functions become unavailable.

WASM/LIBRARY metadata and sources live in `.metadata/udf_store/modules` and `.metadata/udf_store/module_chunks`. Native modules also use the KV volume `.metadata/udf_store/binaries`.

## Upload and delete {#upload}

How to write and build a `.so` for WAVM is described in [Writing and building WASM UDFs](wasm/writing.md).

There is no public `ydb` CLI command for uploading modules yet. In practice, use the helper from the source tree at `ydb/tests/functional/udf_store/upload_udf` (dev/tooling), which writes rows into the store tables.

Main parameters:

| Parameter | Purpose |
|---|---|
| `--action upload\|delete` | Upload or delete |
| `--kind udf\|library` | UDF module or library |
| `--type WASM\|NATIVE_UNSAFE` | Module type (for `--kind udf`) |
| `--udf-file` | Path to the binary. For a C++ WASM UDF this is the Ya Make `.so` (`lib<name>.so` for WAVM), not WAT. Also `.wat` for text fixtures and a native `.so` for `NATIVE_UNSAFE` |
| `--manifest` | Path to the JSON [manifest](wasm/manifest.md) (for WASM) |
| `--library-name` | Library name (for `--kind library`) |
| `--md5` | MD5 for deleting a UDF (if `--udf-file` is not given) |

Example: upload a minimal WASM module:

```bash
upload_udf \
  --action upload \
  --endpoint grpc://localhost:2135 \
  --database /Root/db \
  --kind udf \
  --type WASM \
  --udf-file libadd.so \
  --manifest manifest.json
```

Example: upload a library:

```bash
upload_udf \
  --action upload \
  --endpoint grpc://localhost:2135 \
  --database /Root/db \
  --kind library \
  --library-name sdk \
  --udf-file libsdk.so
```

For WASM modules with dependencies, upload all `LIBRARY` entries from `required_libraries` first, then the UDF. See [Libraries](wasm/libraries.md).

## Monitoring {#monitoring}

You can list modules without direct access to `.metadata` via the [`.sys/udf_modules`](../system-views.md#udf-modules) system view:

```yql
SELECT Uid, Name, ModuleType, CompileStatus, Md5, CompileError
FROM `.sys/udf_modules`;
```

For WASM, wait until `CompileStatus = "ready"` before calling functions in queries. On compile failure, check `CompileError`.

## See also

- [WASM UDFs](wasm/index.md)
- [Writing and building WASM UDFs](wasm/writing.md)
- [Native unsafe UDFs](native-unsafe.md)
- [`udf_store_config`](../../reference/configuration/udf_store_config.md)
