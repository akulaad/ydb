# WASM UDF

[WASM UDF](../../../concepts/glossary.md#wasm-udf) — пользовательский [UDF](../../../concepts/glossary.md#udf)-модуль в формате [WebAssembly](https://webassembly.org/), загружаемый в [UDF Store](../../../concepts/glossary.md#udf-store) и вызываемый из YQL как обычная функция модуля (`ModuleName::func`).

После загрузки {{ ydb-short-name }} компилирует модуль (AOT) под CPU узла, регистрирует функции из [манифеста](manifest.md) и на каждом запросе исполняет их в изолированном окружении WebAssembly. Состояние не разделяется между запросами.

## Быстрый старт {#quick-start}

1. Включите store и WASM: см. [Управление модулями](../managing.md#enable).
2. Напишите и соберите модуль: [Написание и сборка](writing.md). Нужны бинарник (`.so` для WAVM или вручную написанный `.wat`) и JSON-[манифест](manifest.md).
3. Загрузите модуль (и при необходимости [библиотеки](libraries.md)).
4. Дождитесь `CompileStatus = "ready"` в [`.sys/udf_modules`](../../system-views.md#udf-modules).
5. Вызовите функцию из YQL:

```yql
SELECT Add::add(10, 32);
```

Минимальный манифест без библиотек:

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

## Разделы

- [Написание и сборка](writing.md): C++ ABI, Ya Make, Emscripten, примеры модулей
- [Манифест](manifest.md): JSON-контракт модуля, функции и объекты с TypeConfig
- [Библиотеки](libraries.md): тип `LIBRARY`, `required_libraries`, порядок sdk
- [Ограничения](limitations.md): ограничения и типичные ошибки

## См. также

- [Управление модулями](../managing.md)
- [Native unsafe UDF](../native-unsafe.md)
