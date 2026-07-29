# Примеры и тесты

## Emscripten examples

Путь: `ydb/tests/functional/udf_store/examples/`.

| Каталог | Роль | required_libraries |
|---|---|---|
| `sdk/` | Runtime (libc/util), upload как library `"sdk"` | — |
| `helpers/` | Промежуточная библиотека, export `helpers_scale` | — |
| `with_helpers/` | UDF `WithHelpers::scale` | `["sdk", "helpers"]` |
| `native_math/` | Native host `.so` (`host_add` / `native_add`) | — (NATIVE_UNSAFE) |
| `with_native_host/` | UDF `WithNativeHost::udf_add` → import `native_math.host_add` | `required_native_modules: ["native_math"]` |
| `md5/` | UDF с libc (MD5) | `["sdk"]` |
| `add/` | Минимальный UDF без libs | `[]` |
| `throw/` | Host `ThrowException` + wasm call stack | `["sdk"]` |
| `prefix/` | Objects + TypeConfig (`Prefix::Apply`) | `["sdk"]` + PEERDIR `object_framework` |
| `ctx/` | Shared ctx + filters (`Ctx::New` / `CountRow` / `CountPositive` / `Snapshot`) | `["sdk"]` + PEERDIR `object_framework` |

Сборка:

```bash
ya make --target-platform=clang20-emscripten-wasm64 --build profile \
  ydb/tests/functional/udf_store/examples/sdk \
  ydb/tests/functional/udf_store/examples/helpers \
  ydb/tests/functional/udf_store/examples/with_helpers \
  ydb/tests/functional/udf_store/examples/with_native_host \
  ydb/tests/functional/udf_store/examples/md5 \
  ydb/tests/functional/udf_store/examples/add \
  ydb/tests/functional/udf_store/examples/throw \
  ydb/tests/functional/udf_store/examples/prefix \
  ydb/tests/functional/udf_store/examples/ctx
```

`webassembly_udf.inc` + `sdk/ld_plugin.py` вырезают sdk-архивы из линковки UDF (sdk подаётся отдельно через store).

Порядок upload для with_helpers:

1. `--kind library --library-name sdk` (бинарник examples/sdk)
2. `--kind library --library-name helpers`
3. UDF with_helpers + `manifest.json` (`required_libraries: ["sdk","helpers"]`)
4. Дождаться `compile_status=ready` у библиотек и модуля
5. `SELECT WithHelpers::scale(7);` → `21`

Порядок upload для with_native_host:

1. Собрать native host (host platform, не emscripten):
   `ya make ydb/tests/functional/udf_store/examples/native_math`
2. Upload NATIVE_UNSAFE: `libnative_math_host.so` + `examples/native_math/manifest.json` (`--manifest`)
3. Собрать WASM UDF:
   `ya make --target-platform=clang18-emscripten-wasm64 --build profile \
     ydb/tests/functional/udf_store/examples/with_native_host`
4. Upload WASM: `with_native_host` `.wasm` + `examples/with_native_host/manifest.json`
5. `SELECT WithNativeHost::udf_add(10, 20);` → `30`

Prefix (objects):

1. upload library `sdk`
2. upload UDF `prefix` + `manifest.json` (`objects[]`)
3. `$fn = YQL::Udf(AsAtom("Prefix.Apply"), Void(), Void(), AsAtom("pre-")); SELECT $fn("x");` → `pre-x`

Shared context + Snapshot:

1. upload library `sdk`
2. upload UDF `ctx` + `manifest.json` (`objects[]` + `functions[]` CountRow/CountPositive)
3. See `examples/ctx/query.sql` — ListMap over `AsList(-1, 2, 3)`, then `Ctx::Snapshot($ctx)` → `rows_seen=3;positives=2`

---

## WAT-фикстуры для CI

`ydb/tests/functional/udf_store/data/wasm/`:

| Файл | Назначение |
|---|---|
| `local_udf.wat` + `local_udf_manifest.json` | модуль без библиотек (`LocalUdf::udf_add`) |
| `sdk_stub.wat` | stub sdk с bump-malloc (library `"sdk"`) |
| `helpers.wat` | library `"helpers"` |
| `with_helpers.wat` + `with_helpers_manifest.json` | UDF с `["sdk","helpers"]` |
| `native_math_manifest.json` | host_exports для native `.so` |
| `with_native_host.wat` + `with_native_host_manifest.json` | UDF с `required_native_modules: ["native_math"]` |

---

## Functional tests

`ydb/tests/functional/udf_store/test_udf_store.py`:

- `test_udf_store_feature_flag` — таблицы/KV при enable/disable
- `test_using_native_unsafe_udf` — native .so path
- `test_using_wasm_udf` — upload WAT, compile, `LocalUdf::udf_add(1,2)==3`
- `test_using_wasm_udf_with_sdk_and_library` — sdk + helpers + module, `WithHelpers::scale(7)==21`
- `test_using_wasm_udf_with_native_host` — native host_exports + WASM import, then unload
- `test_delete_wasm_udf_and_library` — delete module/libraries via `upload_udf --action delete`, UDF unloaded
- `test_grpc_upload_describe_delete_wasm` — gRPC `UploadModule` / `DescribeModule` / `DeleteModule` + bad manifest / md5 mismatch
- `test_grpc_upload_native_and_disabled_flag` — gRPC native upload + WASM → `UNSUPPORTED` when flag off

Запуск (из корня ydbwork/ydb):

```bash
./ya make --build relwithdebinfo -tA ydb/tests/functional/udf_store -F '*wasm_udf_with_sdk*'
./ya make --build relwithdebinfo -tA ydb/tests/functional/udf_store -F '*grpc_upload*'
```

### Два способа upload

| Способ | Когда |
|---|---|
| **gRPC `UdfStoreService`** (рекомендуемый) | продуктовый API; см. ADR upload API |
| **Legacy `upload_udf`** | тесты/отладка; SQL UPSERT + `kv_volume_tool`; не удаляется |

`ENV(YDB_UPLOAD_UDF_PATH=...)`, helper поддерживает `--kind library` и `--manifest` для NATIVE_UNSAFE.

---

## Unit tests

`ydb/services/udf_store/ut/`:

| Тест | Что проверяет |
|---|---|
| `manifest_ut` | parse `required_libraries` / `required_native_modules` / `host_exports`, functions, `objects` |
| `compartment_manager_ut` | catalog register/resolve, TLS guard |
| `object_framework_ut` | static registry create/get/destroy, 2 objects |
| `objects_abi_ut` | WAT create/call exports (ui64 handles) |
| `shared_ctx_ut` | two filters mutate shared ctx; Snapshot → `a=2;b=1` |
| `throw_exception_ut` | host ThrowException → reason `fail(); ex: … boom-from-wasm` + wasm call stack frames |
| `with_helpers_ut` | Empty+AddSdk(sdk_stub)+helpers+module, `scale(7)==21` |
| `native_host_ut` | `AddNativeHostModule` + WAT import → `udf_add(10,20)==30` |
| `blob_chunks_ut` | chunk split/join |
| `upload_api_ut` | chunk split for upload; md5; manifest validation helpers |

```bash
./ya make --build relwithdebinfo -tA ydb/services/udf_store/ut -F '*WithHelpers*'
./ya make --build relwithdebinfo -tA ydb/services/udf_store/ut -F '*ThrowException*'
./ya make --build relwithdebinfo -tA ydb/services/udf_store/ut -F '*ObjectFramework*'
./ya make --build relwithdebinfo -tA ydb/services/udf_store/ut -F '*SharedCtx*'
./ya make --build relwithdebinfo -tA ydb/services/udf_store/ut -F '*UploadApi*'
```

---

## Минимальный ручной сценарий на локальном ydbd

1. Включить `udf_store_config.enabled` + `enable_wasm_udf`.
2. Upload library/module через gRPC `UploadModule` (или legacy `upload_udf`).
3. Дождаться `compile_status=ready` в `DescribeModule` / `modules`.
4. Выполнить YQL с `Module::func`.
5. При ошибке смотреть CA log (`Failed to acquire WASM query compartment` / linkage Missing) и issues ответа — не verification stats.
