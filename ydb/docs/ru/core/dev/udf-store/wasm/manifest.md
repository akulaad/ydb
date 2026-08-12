# Манифест WASM UDF

**Манифест** — JSON-описание [WASM UDF](index.md): имя модуля в YQL, формат исходника, список библиотек и сигнатуры экспортируемых функций. Без корректного манифеста сервис не зарегистрирует функции модуля.

Манифест передаётся при [загрузке](../managing.md#upload) вместе с бинарником.

## Поля верхнего уровня {#fields}

| Поле | Описание |
|---|---|
| `module_name` | Имя модуля в YQL (`ModuleName::func`) |
| `calling_convention` | Соглашение о вызове. Сейчас: `unversioned_value` |
| `module_extension` | Формат исходника: `wasm`, `wat` или `wast` |
| `required_libraries` | Упорядоченный список имён [библиотек](libraries.md) (`ModuleType = LIBRARY`) |
| `functions` | Список plain-функций (экспорт + типы аргументов и результата) |
| `objects` | Опционально: stateful-объекты с TypeConfig (см. ниже) |

## Функции {#functions}

Каждый элемент `functions` описывает одну YQL-функцию:

| Поле | Описание |
|---|---|
| `name` | Имя функции в YQL и (по умолчанию) имя wasm-экспорта |
| `export` | Опционально: имя экспорта в wasm, если оно отличается от `name` |
| `argument_types` | Список типов аргументов |
| `result_type` | Тип результата |

Тип задаётся объектом `{"value": "<type>", "tag": "concrete_type"}`. Поддерживаемые `value` для calling convention `unversioned_value`: `int64`, `uint64`, `double`, `bool`, `string`, `null`.

Пример:

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

Вызов:

```yql
SELECT WithHelpers::scale(7);
```

## Объекты и TypeConfig {#objects}

Поле `objects` описывает stateful UDF в стиле TypeConfigCallable: при первом вызове создаётся объект по blob конфигурации, далее методы вызываются с непрозрачным handle.

Пример манифеста:

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

Вызов из YQL (четвёртый аргумент `YQL::Udf` — TypeConfig):

```yql
$fn = YQL::Udf(AsAtom("Prefix.Apply"), Void(), Void(), AsAtom("pre-"));
SELECT $fn("x");  -- "pre-x"
```

Handle объекта валиден только в рамках текущего запроса. Имена методов становятся именами YQL-функций (`Prefix::Apply`) и должны быть уникальны в манифесте.

## См. также

- [Библиотеки](libraries.md)
- [Ограничения](limitations.md)
- [WASM UDF](index.md)
