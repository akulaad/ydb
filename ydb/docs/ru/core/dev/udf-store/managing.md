# Управление модулями UDF Store

Чтобы загружать и использовать [пользовательские UDF](index.md), нужно включить [UDF Store](../../concepts/glossary.md#udf-store) в конфигурации кластера, записать модуль в хранилище и дождаться, пока сервис зарегистрирует его для вызовов из YQL.

## Включение {#enable}

Секция конфигурации — [`udf_store_config`](../../reference/configuration/udf_store_config.md). Минимально необходим флаг `enabled`.

Для [WASM UDF](wasm/index.md) дополнительно включите `enable_wasm_udf`:

```yaml
udf_store_config:
  enabled: true
  enable_wasm_udf: true
```

Для [native unsafe UDF](native-unsafe.md) — `enable_unsafe_native_udf` и каталог `unsafe_native_udf_dir`.

По умолчанию все флаги выключены.

## Жизненный цикл {#lifecycle}

1. Загрузить бинарник и метаданные модуля в таблицы под `.metadata/udf_store`.
2. Дождаться обработки сервисом UDF Store (для WASM — AOT-компиляции до `CompileStatus = ready`).
3. Вызвать функции модуля из YQL (например, `ModuleName::func(...)`).
4. При необходимости удалить модуль из store — функции перестанут быть доступны.

Метаданные и исходники WASM/LIBRARY хранятся в `.metadata/udf_store/modules` и `.metadata/udf_store/module_chunks`. Нативные модули дополнительно используют KV-том `.metadata/udf_store/binaries`.

## Загрузка и удаление {#upload}

Как написать и собрать `.so` для WAVM, описано в разделе [Написание и сборка WASM UDF](wasm/writing.md).

Публичной команды `ydb` CLI для загрузки модулей пока нет. На практике используется утилита из дерева исходников `ydb/tests/functional/udf_store/upload_udf` (dev/tooling), которая пишет строки в таблицы store.

Основные параметры:

| Параметр | Назначение |
|---|---|
| `--action upload\|delete` | Загрузить или удалить |
| `--kind udf\|library` | UDF-модуль или библиотека |
| `--type WASM\|NATIVE_UNSAFE` | Тип модуля (для `--kind udf`) |
| `--udf-file` | Путь к бинарнику. Для C++ WASM UDF это `.so` сборки Ya Make (`lib<имя>.so` для WAVM), не WAT. Также `.wat` для текстовых фикстур и нативный `.so` для `NATIVE_UNSAFE` |
| `--manifest` | Путь к JSON-[манифесту](wasm/manifest.md) (для WASM) |
| `--library-name` | Имя библиотеки (для `--kind library`) |
| `--md5` | MD5 для удаления UDF (если не передан `--udf-file`) |

Пример загрузки минимального WASM-модуля:

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

Пример загрузки библиотеки:

```bash
upload_udf \
  --action upload \
  --endpoint grpc://localhost:2135 \
  --database /Root/db \
  --kind library \
  --library-name sdk \
  --udf-file libsdk.so
```

Для WASM с зависимостями сначала загрузите все `LIBRARY` из `required_libraries`, затем сам UDF. Подробнее — в разделе [Библиотеки](wasm/libraries.md).

## Мониторинг {#monitoring}

Список модулей без прямого доступа к `.metadata` доступен через системное представление [`.sys/udf_modules`](../system-views.md#udf-modules):

```yql
SELECT Uid, Name, ModuleType, CompileStatus, Md5, CompileError
FROM `.sys/udf_modules`;
```

Для WASM дожидайтесь `CompileStatus = "ready"` перед вызовом функций в запросах. При ошибке компиляции смотрите `CompileError`.

## См. также

- [WASM UDF](wasm/index.md)
- [Написание и сборка WASM UDF](wasm/writing.md)
- [Native unsafe UDF](native-unsafe.md)
- [`udf_store_config`](../../reference/configuration/udf_store_config.md)
