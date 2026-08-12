# udf_store_config

Секция `udf_store_config` включает [UDF Store](../../concepts/glossary.md#udf-store) — хранилище пользовательских [UDF](../../concepts/glossary.md#udf)-модулей — и связанные возможности (WASM и native unsafe).

Практическое руководство: [{#T}](../../dev/udf-store/index.md).

## Параметры конфигурации

| Параметр | Тип | По умолчанию | Описание |
|:---------|:----|:-------------|:---------|
| `enabled` | bool | `false` | Включает UDF Store и создание служебных таблиц/тома под `.metadata/udf_store` |
| `kv_storage_media` | string | `"ssd"` | Тип медиа для KV-тома бинарников |
| `enable_wasm_udf` | bool | `false` | Разрешает загрузку и исполнение [WASM UDF](../../dev/udf-store/wasm/index.md) |
| `wasm_cpu_spec_override` | string | — | Переопределение CPU-спецификации для AOT-артефактов WASM (обычно не задаётся) |
| `enable_unsafe_native_udf` | bool | `false` | Разрешает [native unsafe UDF](../../dev/udf-store/native-unsafe.md) (`.so`) |
| `unsafe_native_udf_dir` | string | — | Каталог на узле, куда копируются нативные `.so` перед загрузкой |

## Пример конфигурации

```yaml
udf_store_config:
  enabled: true
  kv_storage_media: "ssd"
  enable_wasm_udf: true
```

Пример с native unsafe:

```yaml
udf_store_config:
  enabled: true
  enable_unsafe_native_udf: true
  unsafe_native_udf_dir: "/var/lib/ydb/udfs"
```

{% note warning %}

`enable_unsafe_native_udf` позволяет исполнять произвольный нативный код в процессе сервера. Включайте только в доверенных окружениях.

{% endnote %}

## Смотрите также

- [{#T}](../../dev/udf-store/index.md)
- [{#T}](../../dev/udf-store/managing.md)
- [{#T}](../../dev/system-views.md#udf-modules)
