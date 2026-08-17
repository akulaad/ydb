# Библиотеки WASM

**Библиотека** в [UDF Store](../../../concepts/glossary.md#udf-store) — запись с `ModuleType = LIBRARY`: WebAssembly-модуль, от которого зависят [WASM UDF](index.md). Библиотека не регистрирует YQL-функции сама по себе; она линкуется в окружение исполнения UDF.

Типичные роли:

- **runtime / sdk** — первая библиотека в `required_libraries`: libc/malloc и прочий runtime (в линковке становится модулем `"env"`);
- **вспомогательные библиотеки** — общие функции, импортируемые UDF по имени модуля (например, `"helpers"`).

## Поле required_libraries {#required-libraries}

В [манифесте](manifest.md) UDF список `required_libraries` **упорядочен**:

1. Первый элемент — runtime (sdk), подключается как `"env"`.
2. Остальные — дополнительные модули под своими именами.

Пустой список `[]` допустим для минимальных модулей без libc; см. [ограничения](limitations.md).

## Порядок загрузки {#upload-order}

1. Загрузите каждую библиотеку с `--kind library --library-name <name>`.
2. Дождитесь `CompileStatus = "ready"` у библиотек в [`.sys/udf_modules`](../../system-views.md#udf-modules).
3. Загрузите WASM UDF с манифестом, где `required_libraries` перечисляет эти имена.
4. Дождитесь `CompileStatus = "ready"` у UDF.

Компиляция UDF не начинается, пока все указанные библиотеки не готовы.

Пример для модуля с sdk и helpers:

```bash
# 1. runtime
upload_udf ... --kind library --library-name sdk --udf-file libsdk.so
# 2. вспомогательная библиотека
upload_udf ... --kind library --library-name helpers --udf-file libhelpers.so
# 3. UDF с required_libraries: ["sdk", "helpers"]
upload_udf ... --kind udf --type WASM --udf-file libwith_helpers.so --manifest manifest.json
```

```yql
SELECT WithHelpers::scale(7);  -- 21
```

Как собрать sdk, вспомогательную библиотеку и UDF, который импортирует символы через `import_module`, описано в разделе [Написание и сборка](writing.md#build).

## Удаление и зависимости {#delete}

При удалении или замене библиотеки зависящие WASM UDF могут быть выгружены и потребовать повторной компиляции после восстановления зависимостей. Перед удалением библиотеки убедитесь, что на неё больше не ссылаются манифесты активных модулей.

## См. также

- [Написание и сборка](writing.md)
- [Манифест](manifest.md)
- [Управление модулями](../managing.md)
- [Ограничения](limitations.md)
