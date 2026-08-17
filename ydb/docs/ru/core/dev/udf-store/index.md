# Пользовательские UDF

[UDF Store](../../concepts/glossary.md#udf-store) — механизм {{ ydb-short-name }}, позволяющий загружать в базу данных пользовательские [UDF](../../concepts/glossary.md#udf)-модули и вызывать их из [YQL](../../concepts/glossary.md#yql) наряду со [встроенными UDF](../../yql/reference/udf/list/index.md).

В отличие от встроенных библиотек, модули UDF Store не поставляются вместе с сервером: их загружают в системные таблицы хранилища, после чего кластер регистрирует функции и делает их доступными в запросах.

## Типы модулей {#module-types}

| Тип (`ModuleType`) | Назначение |
|---|---|
| `WASM` | Исполняемый [WASM UDF](../../concepts/glossary.md#wasm-udf): байткод WebAssembly и [манифест](wasm/manifest.md) |
| `LIBRARY` | Библиотека-зависимость для WASM (runtime/sdk и вспомогательные модули). Сама по себе не вызывается как `Module::func` |
| `NATIVE_UNSAFE` | Нативный `.so`-модуль UDF. Требует отдельного флага и несёт иные риски, чем WASM |

## Разделы

- [Управление модулями](managing.md) — включение store, загрузка и удаление, мониторинг через `.sys/udf_modules`
- [WASM UDF](wasm/index.md): жизненный цикл, [написание и сборка](wasm/writing.md), манифест, библиотеки и ограничения WASM
- [Native unsafe UDF](native-unsafe.md) — нативные модули

## См. также

- [Встроенные UDF](../../yql/reference/udf/list/index.md)
- [Конфигурация `udf_store_config`](../../reference/configuration/udf_store_config.md)
- [Системное представление `.sys/udf_modules`](../system-views.md#udf-modules)
