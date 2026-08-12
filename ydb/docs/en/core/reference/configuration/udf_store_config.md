# udf_store_config

The `udf_store_config` section enables [UDF Store](../../concepts/glossary.md#udf-store) — storage for custom [UDF](../../concepts/glossary.md#udf) modules — and related capabilities (WASM and native unsafe).

Practical guide: [{#T}](../../dev/udf-store/index.md).

## Configuration parameters

| Parameter | Type | Default | Description |
|:----------|:-----|:--------|:------------|
| `enabled` | bool | `false` | Enables UDF Store and creation of system tables/volume under `.metadata/udf_store` |
| `kv_storage_media` | string | `"ssd"` | Media type for the binaries KV volume |
| `enable_wasm_udf` | bool | `false` | Allows uploading and running [WASM UDFs](../../dev/udf-store/wasm/index.md) |
| `wasm_cpu_spec_override` | string | — | Overrides the CPU spec for WASM AOT artifacts (usually unset) |
| `enable_unsafe_native_udf` | bool | `false` | Allows [native unsafe UDFs](../../dev/udf-store/native-unsafe.md) (`.so`) |
| `unsafe_native_udf_dir` | string | — | Per-node directory where native `.so` files are copied before load |

## Configuration examples

```yaml
udf_store_config:
  enabled: true
  kv_storage_media: "ssd"
  enable_wasm_udf: true
```

Example with native unsafe:

```yaml
udf_store_config:
  enabled: true
  enable_unsafe_native_udf: true
  unsafe_native_udf_dir: "/var/lib/ydb/udfs"
```

{% note warning %}

`enable_unsafe_native_udf` allows arbitrary native code to run in the server process. Enable it only in trusted environments.

{% endnote %}

## See also

- [{#T}](../../dev/udf-store/index.md)
- [{#T}](../../dev/udf-store/managing.md)
- [{#T}](../../dev/system-views.md#udf-modules)
