# WASM UDF manifest

A **manifest** is a JSON description of a [WASM UDF](index.md): the YQL module name, source format, library list, and signatures of exported functions. Without a valid manifest, the service will not register the module functions.

The manifest is supplied at [upload](../managing.md#upload) time together with the binary.

## Top-level fields {#fields}

| Field | Description |
|---|---|
| `module_name` | Module name in YQL (`ModuleName::func`) |
| `calling_convention` | Calling convention. Currently: `unversioned_value` |
| `module_extension` | Source format: `wasm`, `wat`, or `wast` |
| `required_libraries` | Ordered list of [library](libraries.md) names (`ModuleType = LIBRARY`) |
| `functions` | List of plain functions (export + argument and result types) |
| `objects` | Optional: stateful TypeConfig objects (see below) |

## Functions {#functions}

Each `functions` entry describes one YQL function:

| Field | Description |
|---|---|
| `name` | Function name in YQL and (by default) the wasm export name |
| `export` | Optional: wasm export name if it differs from `name` |
| `argument_types` | List of argument types |
| `result_type` | Result type |

A type is an object `{"value": "<type>", "tag": "concrete_type"}`. Supported `value` types for `unversioned_value`: `int64`, `uint64`, `double`, `bool`, `string`, `null`.

Example:

```json
{
  "module_name": "WithHelpers",
  "calling_convention": "unversioned_value",
  "module_extension": "wasm",
  "required_libraries": ["sdk", "helpers"],
  "functions": [
    {
      "name": "scale",
      "argument_types": [{"value": "int64", "tag": "concrete_type"}],
      "result_type": {"value": "int64", "tag": "concrete_type"}
    }
  ]
}
```

Call:

```yql
SELECT WithHelpers::scale(7);
```

## Objects and TypeConfig {#objects}

The `objects` field describes stateful UDFs in the TypeConfigCallable style: on the first call an object is created from a config blob, then methods are invoked with an opaque handle.

Example manifest:

```json
{
  "module_name": "Prefix",
  "required_libraries": ["sdk"],
  "objects": [
    {
      "name": "Prefix",
      "create_export": "prefix_create",
      "destroy_export": "prefix_destroy",
      "methods": [
        {
          "name": "Apply",
          "export": "prefix_apply",
          "yql_binding": "type_config_callable",
          "argument_types": [{"value": "string", "tag": "concrete_type"}],
          "result_type": {"value": "string", "tag": "concrete_type"}
        }
      ]
    }
  ]
}
```

YQL call (the fourth `YQL::Udf` argument is TypeConfig):

```yql
$fn = YQL::Udf(AsAtom("Prefix.Apply"), Void(), Void(), AsAtom("pre-"));
SELECT $fn("x");  -- "pre-x"
```

The object handle is valid only within the current query. Method names become YQL function names (`Prefix::Apply`) and must be unique in the manifest.

## See also

- [Writing and building](writing.md)
- [Libraries](libraries.md)
- [Limitations](limitations.md)
- [WASM UDFs](index.md)
