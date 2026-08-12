# WASM UDFs

A [WASM UDF](../../../concepts/glossary.md#wasm-udf) is a custom [UDF](../../../concepts/glossary.md#udf) module in [WebAssembly](https://webassembly.org/) format, uploaded into [UDF Store](../../../concepts/glossary.md#udf-store) and called from YQL like a regular module function (`ModuleName::func`).

After upload, {{ ydb-short-name }} AOT-compiles the module for the node CPU, registers functions from the [manifest](manifest.md), and runs them in an isolated WebAssembly environment per query. State is not shared across queries.

## Quick start {#quick-start}

1. Enable the store and WASM: see [Managing modules](../managing.md#enable).
2. Prepare a binary (`.wasm` or `.wat`) and a JSON [manifest](manifest.md).
3. Upload the module (and [libraries](libraries.md) if needed).
4. Wait for `CompileStatus = "ready"` in [`.sys/udf_modules`](../../system-views.md#udf-modules).
5. Call the function from YQL:

```yql
SELECT Add::add(10, 32);
```

Minimal manifest without libraries:

```json
{
  "module_name": "Add",
  "calling_convention": "unversioned_value",
  "module_extension": "wasm",
  "required_libraries": [],
  "functions": [
    {
      "name": "add",
      "argument_types": [
        {"value": "int64", "tag": "concrete_type"},
        {"value": "int64", "tag": "concrete_type"}
      ],
      "result_type": {"value": "int64", "tag": "concrete_type"}
    }
  ]
}
```

## Sections

- [Manifest](manifest.md) — JSON module contract, functions, and TypeConfig objects
- [Libraries](libraries.md) — `LIBRARY` type, `required_libraries`, sdk order
- [Limitations](limitations.md) — limitations and common failures

## See also

- [Managing modules](../managing.md)
- [Native unsafe UDFs](../native-unsafe.md)
