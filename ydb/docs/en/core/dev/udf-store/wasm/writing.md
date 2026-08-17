# Writing and building WASM UDFs

A [WASM UDF](index.md) for [UDF Store](../../../concepts/glossary.md#udf-store) is a [WebAssembly](https://webassembly.org/) module plus a JSON [manifest](manifest.md). The module exports functions using the `unversioned_value` calling convention. After [upload](../managing.md#upload), the server AOT-compiles the binary and calls the exports from YQL as `ModuleName::func`.

This article shows how to implement those functions in C++, build a `.wasm` file with Ya Make and Emscripten, and prepare the module for upload. For the simplest cases without libc, you can write [WAT](https://webassembly.github.io/spec/core/text/index.html) by hand instead.

Reference implementations live in the source tree under `ydb/tests/functional/udf_store/examples/`.

## Prerequisites {#prerequisites}

1. Enable the store and WASM as described in [Managing modules](../managing.md#enable). Check that the node config sets `udf_store_config.enabled` and `enable_wasm_udf`.
2. Build modules for the Emscripten **wasm64** platform. Do not mix wasm32 with the {{ ydb-short-name }} runtime: ABI pointers are 64-bit.
3. After the build, upload the `.wasm` file and the manifest with the helper at `ydb/tests/functional/udf_store/upload_udf`. There is no public `ydb` CLI upload command yet.

## Calling convention {#abi}

The `unversioned_value` calling convention defines the signature of every wasm export that YQL can call.

ABI header: `ydb/services/udf_store/wasm/abi/udf_cpp_abi.h`.

Declare the export as `extern "C"` with default visibility. The first two parameters are always the invocation context and the result slot, followed by one pointer per YQL argument:

```cpp
void func(
    TExpressionContext* context,
    TUnversionedValue* result,
    TUnversionedValue* arg0,
    TUnversionedValue* arg1);
```

`TUnversionedValue` carries the type, the length (for strings), and the payload:

| `Type` (`EValueType`) | Data field | Manifest `value` |
|---|---|---|
| `Null` | | `null` |
| `Int64` | `Data.Int64` | `int64` |
| `Uint64` | `Data.Uint64` | `uint64` |
| `Double` | `Data.Double` | `double` |
| `Boolean` | `Data.Boolean` | `bool` |
| `String` | `Data.String` and `Length` | `string` |

The host provides two functions (do not implement them in the module):

- `AllocateBytes(context, size)` allocates a buffer for a string result in the current invocation.
- `ThrowException(message)` aborts the UDF call with an error. The message must point to a NUL-terminated string in wasm memory.

String arguments point into wasm linear memory: read `arg->Data.String` of length `arg->Length`. For a string result, allocate with `AllocateBytes`, copy the bytes, then set `result->Type`, `result->Length`, and `result->Data.String`.

{% note info %}

Empty `required_libraries: []` is only suitable for arithmetic and other paths that never call wasm `malloc`. Strings, the C++ heap, and objects need a runtime [library](libraries.md) (sdk) that exports working `malloc` / `free`.

{% endnote %}

## Build: platform and linking {#build}

Build modules as a `DLL()` target for `clang20-emscripten-wasm64`. From the repository root:

```bash
ya make --target-platform=clang20-emscripten-wasm64 --build profile \
  ydb/tests/functional/udf_store/examples/sdk \
  ydb/tests/functional/udf_store/examples/add
```

The target output directory contains a `.wasm` file. Pass that path as `--udf-file` on upload.

Three binary roles use three linking schemes:

| Role | `ModuleType` on upload | How to link |
|---|---|---|
| Runtime (sdk) | `LIBRARY`, name `sdk` | Whole libc, allocator, and C++ runtime (see `examples/sdk`) |
| Helper library | `LIBRARY`, custom name | Separate `DLL()` that exports symbols under the module name |
| UDF | `WASM` | Do **not** statically bake the sdk in: the store supplies sdk as `"env"` |

UDF targets that need the sdk should `INCLUDE` `ydb/tests/functional/udf_store/examples/sdk/webassembly_udf.inc`. That file enables an `LD_PLUGIN` (`ld_plugin.py`) which strips sdk archives from the linker command. Otherwise libc would appear both in the sdk and in the UDF, and query-time linking would fail.

Do **not** `PEERDIR` a helper library into the UDF. `PEERDIR` statically merges the code into one `.wasm`, while the store links the library separately from `required_libraries`. Import the symbol from the UDF instead:

```cpp
__attribute__((import_module("helpers"), import_name("helpers_scale")))
extern "C" long long helpers_scale(long long value);
```

The `import_module` name must match `--library-name` (and the `required_libraries` entry after sdk). The first library becomes the `"env"` module; do not import it as `"sdk"`.

The static `object_framework` (`ydb/services/udf_store/wasm/object_framework`) is the opposite: `PEERDIR` it into the UDF. It is not a store wasm library. The object registry lives inside the UDF image.

## Minimal module without libraries {#minimal}

Goal: add two `int64` values without libc. Use a manifest with empty `required_libraries` and a function `Add::add`.

```cpp
#include <ydb/services/udf_store/wasm/abi/udf_cpp_abi.h>

using namespace NYdb::NUdfStore::NAbi;

extern "C" {
__attribute__((visibility("default"))) void add(
    TExpressionContext* /*context*/,
    TUnversionedValue* result,
    TUnversionedValue* arg0,
    TUnversionedValue* arg1)
{
    if (arg0->Type == EValueType::Null || arg1->Type == EValueType::Null) {
        result->Type = EValueType::Null;
        return;
    }

    result->Type = EValueType::Int64;
    result->Data.Int64 = arg0->Data.Int64 + arg1->Data.Int64;
}
}
```

Export names (`add`) must match `functions[].name` in the manifest, unless you set `functions[].export`. Handle `Null`: scalar ABI types are exposed to YQL as optionals.

`ya.make` fragment (full file: `examples/add/ya.make`):

```
BUILD_ONLY_IF(OS_EMSCRIPTEN)

DLL()

LD_PLUGIN(ydb/tests/functional/udf_store/examples/sdk/ld_plugin.py)

NO_UTIL()
NO_RUNTIME()
NO_LIBC()
STRIP()

SRCS(
    main.cpp
)

PEERDIR(
    ydb/services/udf_store/wasm/abi
)
```

Manifest and call:

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

```yql
SELECT Add::add(10, 32);
```

## Module with a runtime sdk {#sdk}

Goal: compute the MD5 of a string. That needs a heap, `TString`, and `library/cpp/digest/md5`, so the manifest lists `required_libraries: ["sdk"]`.

Build and upload the sdk first as a `LIBRARY` named `sdk` (target `examples/sdk`). Its `WHOLE_ARCHIVE` / `PEERDIR` lists pull in musl, dlmalloc, standalone wasm, libc++, and `util`.

The UDF links the ABI and application libraries but does not embed the sdk again:

```
DLL()

INCLUDE(${ARCADIA_ROOT}/ydb/tests/functional/udf_store/examples/sdk/webassembly_udf.inc)

STRIP()

SRCS(
    main.cpp
)

PEERDIR(
    ydb/services/udf_store/wasm/abi
    library/cpp/digest/md5
)
```

Write the string result into a host-allocated buffer:

```cpp
#include <ydb/services/udf_store/wasm/abi/udf_cpp_abi.h>
#include <library/cpp/digest/md5/md5.h>
#include <util/generic/string.h>
#include <cstring>

using namespace NYdb::NUdfStore::NAbi;

extern "C" {
__attribute__((visibility("default"))) void md5(
    TExpressionContext* context,
    TUnversionedValue* result,
    TUnversionedValue* arg0)
{
    if (arg0->Type == EValueType::Null) {
        result->Type = EValueType::Null;
        return;
    }

    const TString hash = MD5::Calc(TStringBuf(arg0->Data.String, arg0->Length));
    result->Type = EValueType::String;
    result->Length = hash.size();
    result->Data.String = AllocateBytes(context, result->Length);
    if (result->Length > 0) {
        memcpy(result->Data.String, hash.data(), result->Length);
    }
}
}
```

```yql
SELECT Md5::md5("hello");
```

Upload order: sdk, then the UDF. UDF compilation does not start until every library in the manifest has `CompileStatus = "ready"`. Details: [Libraries](libraries.md#upload-order).

## Helper libraries {#helpers}

Goal: keep a shared function in a separate wasm module and link it at query time instead of baking it into every UDF.

The library exports a plain C symbol (this is not a YQL function):

```cpp
extern "C" {
__attribute__((visibility("default"))) long long helpers_scale(long long value)
{
    return value * 3;
}
}
```

In the library `ya.make`, export the symbol explicitly (`-Wl,--export=helpers_scale`). You do not need the UDF ABI header unless the library uses it. Upload with `--kind library --library-name helpers`.

The UDF imports the symbol from module `"helpers"` and wraps it in `unversioned_value`. Manifest fragment:

```json
"required_libraries": ["sdk", "helpers"]
```

Order: sdk as `"env"`, then `"helpers"`, then the UDF. `SELECT WithHelpers::scale(7);` returns `21`.

## Objects and TypeConfig {#objects}

A stateful UDF in the TypeConfigCallable style: on the first call the host creates an object from an opaque config blob, then passes only a `uint64` handle into methods.

Object registry: the static `object_framework` library (`ObjectFrameworkCreate` / `Get` / `Destroy`). `PEERDIR` it into the UDF. Do not add it to `required_libraries`.

Manifest section `objects[]`:

- `create_export` / `destroy_export`: wasm names of the constructor and destructor.
- `methods[]` with `yql_binding: "type_config_callable"`: YQL passes only the method arguments. The host injects the handle.
- `methods[]` with `yql_binding: "plain"`: YQL passes the arguments as listed, including the handle.

If the name `New` is free, the host also registers `Module::New()` as a plain function with **no arguments** that returns `uint64`. Use `New()` only when `create_export` does not read TypeConfig (as in `Ctx::New()`). For a configured object, call the method through `YQL::Udf` and pass TypeConfig as the fourth argument.

Example: prefix a string using the blob `"pre-"`. Full code: `examples/prefix/`. Export layout:

1. `prefix_create(config)` stores the blob via `ObjectFrameworkCreate` and returns a handle.
2. `prefix_apply(handle, input)` looks up the instance with `ObjectFrameworkGet` and writes the result string into `AllocateBytes`.
3. `prefix_destroy(handle)` calls `ObjectFrameworkDestroy`.

```yql
$fn = YQL::Udf(AsAtom("Prefix.Apply"), Void(), Void(), AsAtom("pre-"));
SELECT $fn("x");  -- "pre-x"
```

The handle and the instance exist only for the current query. The next query creates the object again. Method names become YQL function names (`Prefix::Apply`) and must be unique in the manifest.

## Shared query context {#shared-ctx}

Several functions in one module can share mutable state through a single `object_framework` handle. The host cannot observe that state until the module returns it as a normal value (for example a string).

Pattern from `examples/ctx/`:

1. `$ctx = Ctx::New();` creates an object without TypeConfig.
2. `Ctx::CountRow` / `Ctx::CountPositive` take `uint64` as the first argument and update counters.
3. `Ctx::Snapshot($ctx)` serializes the counters into a string for `SELECT`.

YQL evaluates expressions lazily. Force the filters to run first, then call Snapshot. Do not mix accumulation and Snapshot in the same `SELECT` column list over input rows: Snapshot may run before every filter has finished.

```yql
$ctx = Ctx::New();
$vals = AsList(-1, 2, 3);
$afterRows = ListMap($vals, ($x) -> { RETURN Ctx::CountRow($ctx, $x) });
$afterPos = ListMap($afterRows, ($x) -> { RETURN Ctx::CountPositive($ctx, $x) });
SELECT $afterPos AS rows, Ctx::Snapshot($ctx) AS stats;
```

Expected result: `rows = [-1, 2, 3]`, `stats = "rows_seen=3;positives=2"`.

Do not pass the handle into a different WASM module: each UDF image has its own `object_framework` registry.

## Errors {#errors}

For an application error, call `ThrowException("...")`. The host turns it into UDF termination with a message such as `fail(); ex: … boom-from-wasm` and a wasm stack, if the frames are exported.

Do not swallow `malloc` failures in objects: if allocation fails, call `ThrowException` instead of returning a zero handle that later code treats as valid.

Typical linkage and allocation failures are listed under [Limitations](limitations.md).

## Examples in the source tree {#examples}

Directory `ydb/tests/functional/udf_store/examples/`:

| Directory | Role | `required_libraries` |
|---|---|---|
| `sdk/` | Runtime libc/util, upload as library `"sdk"` | |
| `add/` | Minimal UDF with no libraries | `[]` |
| `md5/` | UDF with strings and libc | `["sdk"]` |
| `throw/` | `ThrowException` and wasm stack | `["sdk"]` |
| `helpers/` | Intermediate library `"helpers"` | |
| `with_helpers/` | UDF that imports from `"helpers"` | `["sdk", "helpers"]` |
| `prefix/` | TypeConfig and `object_framework` | `["sdk"]` |
| `ctx/` | Shared ctx, Snapshot | `["sdk"]` |

CI without Emscripten uses WAT fixtures in `ydb/tests/functional/udf_store/data/wasm/`. They implement the same ABI (wasm64 pointers to `TUnversionedValue`). For application modules, C++ is the practical path.

## Uploading a built module {#upload}

1. Build the sdk and helper libraries, then the UDF.
2. Upload libraries and the module as described in [Managing modules](../managing.md#upload) and [Libraries](libraries.md#upload-order).
3. Wait for `CompileStatus = "ready"` in [`.sys/udf_modules`](../../system-views.md#udf-modules).
4. Call the functions from YQL.

## See also

- [Manifest](manifest.md)
- [Libraries](libraries.md)
- [Limitations](limitations.md)
- [Managing modules](../managing.md)
