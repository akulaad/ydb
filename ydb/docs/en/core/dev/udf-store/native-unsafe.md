# Native unsafe UDFs

**Native unsafe UDF** is a custom [UDF](../../concepts/glossary.md#udf) module shipped as a native shared library (`.so`) and loaded through [UDF Store](../../concepts/glossary.md#udf-store) with type `NATIVE_UNSAFE`.

Unlike a [WASM UDF](wasm/index.md), such a module runs as ordinary native code inside the `ydbd` process and is not sandboxed by WebAssembly. It therefore requires a separate configuration flag and should only be used in trusted environments.

## Enabling {#enable}

```yaml
udf_store_config:
  enabled: true
  enable_unsafe_native_udf: true
  unsafe_native_udf_dir: "/path/to/udf/dir"
```

The service copies the binary from the KV volume `.metadata/udf_store/binaries` into `unsafe_native_udf_dir` and attaches it to the function registry.

## Differences from WASM {#vs-wasm}

| | WASM | Native unsafe |
|---|---|---|
| Format | `.wasm` / `.wat` + manifest | `.so` |
| Isolation | WebAssembly sandbox | server process code |
| Flag | `enable_wasm_udf` | `enable_unsafe_native_udf` |
| AOT compilation in the store | yes (`CompileStatus`) | no (binary load) |
| `LIBRARY` dependencies | yes | no |

Common upload steps and module listing are described in [Managing modules](managing.md).

{% note warning %}

Native unsafe UDFs can run arbitrary code with the privileges of the server process. Enable `enable_unsafe_native_udf` only if you trust the module source and control who can upload modules.

{% endnote %}

## See also

- [User-defined UDFs](index.md)
- [Managing modules](managing.md)
- [`udf_store_config`](../../reference/configuration/udf_store_config.md)
