# Native unsafe UDF

**Native unsafe UDF** — пользовательский [UDF](../../concepts/glossary.md#udf)-модуль в виде нативной разделяемой библиотеки (`.so`), загружаемый через [UDF Store](../../concepts/glossary.md#udf-store) с типом `NATIVE_UNSAFE`.

В отличие от [WASM UDF](wasm/index.md), такой модуль исполняется как обычный нативный код процесса `ydbd` и не проходит песочницу WebAssembly. Поэтому для него нужен отдельный флаг конфигурации, а использование ограничено доверенными окружениями.

## Включение {#enable}

```yaml
udf_store_config:
  enabled: true
  enable_unsafe_native_udf: true
  unsafe_native_udf_dir: "/path/to/udf/dir"
```

Сервис копирует бинарник из KV-тома `.metadata/udf_store/binaries` в `unsafe_native_udf_dir` и подключает его к реестру функций.

## Отличия от WASM {#vs-wasm}

| | WASM | Native unsafe |
|---|---|---|
| Формат | `.so` (бинарный WASM для WAVM) или `.wat` + манифест | нативный `.so` |
| Изоляция | песочница WebAssembly | код процесса сервера |
| Флаг | `enable_wasm_udf` | `enable_unsafe_native_udf` |
| AOT-компиляция в store | да (`CompileStatus`) | нет (загрузка бинарника) |
| Библиотеки `LIBRARY` | да | нет |

Общие шаги загрузки и просмотра списка модулей описаны в разделе [Управление модулями](managing.md).

{% note warning %}

Native unsafe UDF могут выполнять произвольный код с правами процесса сервера. Включайте `enable_unsafe_native_udf` только если доверяете источнику модулей и контролируете доступ к загрузке.

{% endnote %}

## См. также

- [Пользовательские UDF](index.md)
- [Управление модулями](managing.md)
- [`udf_store_config`](../../reference/configuration/udf_store_config.md)
