# Написание и сборка WASM UDF

[WASM UDF](index.md) для [UDF Store](../../../concepts/glossary.md#udf-store) является модулем [WebAssembly](https://webassembly.org/) с JSON-[манифестом](manifest.md). Модуль экспортирует функции по соглашению `unversioned_value`. После [загрузки](../managing.md#upload) сервер компилирует бинарник (AOT) и вызывает экспорты из YQL как `ModuleName::func`.

Эта статья показывает, как реализовать такие функции на C++, собрать `.so` для рантайма WAVM через Ya Make и Emscripten и подготовить модуль к загрузке. Сборка **не** выдаёт текстовый [WAT](https://webassembly.github.io/spec/core/text/index.html): WAT пишут вручную только для самых простых случаев без libc.

Готовые эталоны лежат в исходниках: `ydb/tests/functional/udf_store/examples/`.

## Предварительные условия {#prerequisites}

1. Включите store и WASM, как в разделе [Управление модулями](../managing.md#enable). Проверка: в конфигурации узла заданы `udf_store_config.enabled` и `enable_wasm_udf`.
2. Собирайте модули с платформой Emscripten **wasm64**. Смешивать wasm32 с рантаймом {{ ydb-short-name }} нельзя: указатели в ABI имеют ширину 64 бита.
3. После сборки загрузите получившийся `.so` и манифест утилитой из `ydb/tests/functional/udf_store/upload_udf`. Публичной команды `ydb` CLI для загрузки пока нет.

## Соглашение о вызове {#abi}

Соглашение о вызове `unversioned_value` задаёт сигнатуру каждого wasm-экспорта, который вызывается из YQL.

Заголовок ABI: `ydb/services/udf_store/wasm/abi/udf_cpp_abi.h`.

Экспорт объявляют как `extern "C"` с видимостью `default`. Первые два аргумента всегда контекст и слот результата, дальше по одному указателю на каждый аргумент YQL:

```cpp
void func(
    TExpressionContext* context,
    TUnversionedValue* result,
    TUnversionedValue* arg0,
    TUnversionedValue* arg1);
```

`TUnversionedValue` несёт тип, длину (для строк) и полезную нагрузку:

| `Type` (`EValueType`) | Поле данных | Соответствие в манифесте |
|---|---|---|
| `Null` | | `null` |
| `Int64` | `Data.Int64` | `int64` |
| `Uint64` | `Data.Uint64` | `uint64` |
| `Double` | `Data.Double` | `double` |
| `Boolean` | `Data.Boolean` | `bool` |
| `String` | `Data.String` и `Length` | `string` |

Хост предоставляет две функции (их не реализуют в модуле):

- `AllocateBytes(context, size)` выделяет буфер для строки-результата в памяти текущего вызова.
- `ThrowException(message)` прерывает вызов UDF с текстом ошибки. Сообщение должно указывать на NUL-терминированную строку в памяти wasm.

Строковые аргументы указывают в линейную память wasm: читайте `arg->Data.String` длиной `arg->Length`. Для строки-результата выделите буфер через `AllocateBytes`, скопируйте байты и заполните `result->Type`, `result->Length` и `result->Data.String`.

{% note info %}

Пустой `required_libraries: []` подходит только для арифметики и других путей без wasm-`malloc`. Строки, C++-куча и объекты требуют [библиотеку](libraries.md) runtime (sdk) с рабочими `malloc` / `free`.

{% endnote %}

## Сборка: платформа и линковка {#build}

Модули собирают таргетом `DLL()` на платформе `clang20-emscripten-wasm64`. Команда из корня репозитория:

```bash
ya make --target-platform=clang20-emscripten-wasm64 --build profile \
  ydb/tests/functional/udf_store/examples/sdk \
  ydb/tests/functional/udf_store/examples/add
```

В выходном каталоге таргета появляется `lib<имя>.so` (для `add` это `libadd.so`). Это **бинарный** модуль WebAssembly (wasm64) для рантайма [WAVM](https://github.com/WAVM/WAVM), а не текстовый WAT и не нативная библиотека типа [NATIVE_UNSAFE](../native-unsafe.md). Расширение `.so` здесь соглашение Ya Make для `DLL()`. Содержимое файла — WASM-бинарник (магическая последовательность `\0asm`). Его передают в `--udf-file` при загрузке.

В манифесте для такого артефакта укажите `"module_extension": "wasm"`. Значение `"wat"` нужно только если исходник действительно текстовый WAT, его C++-сборка не производит.

{% note info %}

Не путайте этот `.so` с native unsafe UDF: тот же суффикс, другой `ModuleType`, другой загрузчик. WASM-`.so` исполняется в песочнице WAVM после AOT на стороне сервера.

{% endnote %}

Три роли бинарников и три схемы линковки:

| Роль | `ModuleType` при загрузке | Как линковать |
|---|---|---|
| Runtime (sdk) | `LIBRARY`, имя `sdk` | Целиком libc, allocator и C++ runtime (см. `examples/sdk`) |
| Вспомогательная библиотека | `LIBRARY`, своё имя | Отдельный `DLL()`, экспортирует символы под именем модуля |
| UDF | `WASM` | **Не** вшивать sdk статически: sdk подаётся из store как `"env"` |

UDF, которым нужен sdk, подключают include `ydb/tests/functional/udf_store/examples/sdk/webassembly_udf.inc`. Он включает `LD_PLUGIN` (`ld_plugin.py`), который вырезает архивы sdk из командной строки линкера. Иначе libc окажется и в sdk, и в UDF, и линковка в запросе сломается.

Вспомогательную библиотеку **нельзя** подключать через `PEERDIR` в UDF. `PEERDIR` статически вшьёт код в один `.so`, а store линкует библиотеку отдельно по `required_libraries`. Из UDF импортируйте символ:

```cpp
__attribute__((import_module("helpers"), import_name("helpers_scale")))
extern "C" long long helpers_scale(long long value);
```

Имя в `import_module` должно совпадать с `--library-name` (и с записью в `required_libraries` после sdk). Первая библиотека становится модулем `"env"`, её не импортируют как `"sdk"`.

Статический `object_framework` (`ydb/services/udf_store/wasm/object_framework`) наоборот подключают через `PEERDIR`. Это не wasm-библиотека store: реестр объектов живёт внутри образа UDF.

## Минимальный модуль без библиотек {#minimal}

Задача: сложить два `int64` без libc. Манифест с пустым `required_libraries` и функция `Add::add`.

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

Имена экспортов (`add`) должны совпадать с `functions[].name` в манифесте, либо задайте `functions[].export`. Обрабатывайте `Null`: типы в YQL для скаляров ABI объявляются как optional.

Фрагмент `ya.make` (полный файл: `examples/add/ya.make`):

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

Манифест и вызов:

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

## Модуль с runtime sdk {#sdk}

Задача: посчитать MD5 строки. Нужны куча, `TString` и библиотека `library/cpp/digest/md5`, поэтому в манифесте `required_libraries: ["sdk"]`.

Сначала соберите и загрузите sdk как `LIBRARY` с именем `sdk` (таргет `examples/sdk`). Поля `WHOLE_ARCHIVE` / `PEERDIR` там включают musl, dlmalloc, standalone wasm, libc++ и `util`.

UDF линкуется с ABI и прикладными библиотеками, но без повторного вшивания sdk:

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

Строку-результат кладут в буфер хоста:

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

Порядок загрузки: sdk, затем UDF. Компиляция UDF не начнётся, пока библиотеки из манифеста не получат `CompileStatus = "ready"`. Подробнее: [Библиотеки](libraries.md#upload-order).

## Вспомогательные библиотеки {#helpers}

Задача: вынести общую функцию в отдельный wasm и линковать её в запросе, а не вшивать в каждый UDF.

Библиотека экспортирует обычный C-символ (это не YQL-функция):

```cpp
extern "C" {
__attribute__((visibility("default"))) long long helpers_scale(long long value)
{
    return value * 3;
}
}
```

В `ya.make` библиотеки явно экспортируйте символ (`-Wl,--export=helpers_scale`) и не подключайте ABI UDF, если он не нужен. Загрузка: `--kind library --library-name helpers`.

UDF импортирует символ из модуля `"helpers"` и оборачивает его в `unversioned_value`. Фрагмент манифеста:

```json
"required_libraries": ["sdk", "helpers"]
```

Порядок: sdk как `"env"`, затем `"helpers"`, затем сам UDF. Вызов: `SELECT WithHelpers::scale(7);` возвращает `21`.

## Объекты и TypeConfig {#objects}

UDF с состоянием в стиле TypeConfigCallable: при первом вызове хост создаёт объект из непрозрачного blob конфигурации и дальше передаёт в методы только `uint64` handle.

Реестр объектов: статическая библиотека `object_framework` (`ObjectFrameworkCreate` / `Get` / `Destroy`). Её линкуют `PEERDIR` в UDF, в `required_libraries` не добавляют.

В манифесте секция `objects[]`:

- `create_export` / `destroy_export`: wasm-имена конструктора и деструктора.
- `methods[]` с `yql_binding: "type_config_callable"`: из YQL передают только аргументы метода. Handle подставляет хост.
- `methods[]` с `yql_binding: "plain"`: YQL передаёт аргументы как есть, включая handle.

Если имя `New` свободно, хост регистрирует `Module::New()` как plain-функцию **без аргументов**, результат `uint64`. Используйте `New()` только когда `create_export` не читает TypeConfig (как `Ctx::New()`). Для объекта с конфигурацией вызывайте метод через `YQL::Udf` и четвёртый аргумент TypeConfig.

Пример: префикс строки из blob `"pre-"`. Полный код: `examples/prefix/`. Схема экспортов:

1. `prefix_create(config)` кладёт blob в экземпляр через `ObjectFrameworkCreate` и возвращает handle.
2. `prefix_apply(handle, input)` читает экземпляр через `ObjectFrameworkGet` и пишет строку-результат в `AllocateBytes`.
3. `prefix_destroy(handle)` вызывает `ObjectFrameworkDestroy`.

```yql
$fn = YQL::Udf(AsAtom("Prefix.Apply"), Void(), Void(), AsAtom("pre-"));
SELECT $fn("x");  -- "pre-x"
```

Handle и экземпляр живут только в текущем запросе. На следующем запросе объект создаётся заново. Имена методов становятся именами YQL-функций (`Prefix::Apply`) и должны быть уникальны в манифесте.

## Общий контекст запроса {#shared-ctx}

Несколько функций одного модуля могут разделять mutable-состояние через один handle `object_framework`. Хост не видит это состояние, пока модуль не вернёт его обычным значением (например строкой).

Паттерн из `examples/ctx/`:

1. `$ctx = Ctx::New();` создаёт объект без TypeConfig.
2. `Ctx::CountRow` / `Ctx::CountPositive` принимают `uint64` первым аргументом и обновляют счётчики.
3. `Ctx::Snapshot($ctx)` сериализует счётчики в строку для `SELECT`.

YQL вычисляет выражения лениво. Сначала форсируйте прогон фильтров, затем Snapshot. Не смешивайте накопление и Snapshot в одном списке колонок `SELECT` по строкам входа: Snapshot может выполниться раньше, чем все фильтры.

```yql
$ctx = Ctx::New();
$vals = AsList(-1, 2, 3);
$afterRows = ListMap($vals, ($x) -> { RETURN Ctx::CountRow($ctx, $x) });
$afterPos = ListMap($afterRows, ($x) -> { RETURN Ctx::CountPositive($ctx, $x) });
SELECT $afterPos AS rows, Ctx::Snapshot($ctx) AS stats;
```

Ожидаемый результат: `rows = [-1, 2, 3]`, `stats = "rows_seen=3;positives=2"`.

Handle нельзя передавать в другой WASM-модуль: у каждого образа UDF свой реестр `object_framework`.

## Ошибки {#errors}

Для прикладной ошибки вызовите `ThrowException("...")`. Хост превращает её в завершение UDF с текстом вида `fail(); ex: … boom-from-wasm` и стеком wasm, если символы экспортированы.

Не глотайте сбои `malloc` в объектах: без памяти вызывайте `ThrowException`, а не возвращайте нулевой handle молча, если дальше по коду handle обязателен.

Типичные поломки линковки и аллокации собраны в разделе [Ограничения](limitations.md).

## Примеры в исходниках {#examples}

Каталог `ydb/tests/functional/udf_store/examples/`:

| Каталог | Роль | `required_libraries` |
|---|---|---|
| `sdk/` | Runtime libc/util, загрузка как library `"sdk"` | |
| `add/` | Минимальный UDF без библиотек | `[]` |
| `md5/` | UDF со строками и libc | `["sdk"]` |
| `throw/` | `ThrowException` и стек wasm | `["sdk"]` |
| `helpers/` | Промежуточная library `"helpers"` | |
| `with_helpers/` | UDF с импортом из `"helpers"` | `["sdk", "helpers"]` |
| `prefix/` | TypeConfig и `object_framework` | `["sdk"]` |
| `ctx/` | Общий ctx, Snapshot | `["sdk"]` |

Для CI без Emscripten есть WAT-фикстуры в `ydb/tests/functional/udf_store/data/wasm/`. Их пишут вручную, сборка C++ их не генерирует. Они реализуют тот же ABI (wasm64-указатели на `TUnversionedValue`), но для прикладных модулей удобнее C++ → `.so` для WAVM.

## Загрузка собранного модуля {#upload}

1. Соберите sdk и зависимости-библиотеки, затем UDF.
2. Загрузите библиотеки и модуль, как в разделах [Управление модулями](../managing.md#upload) и [Библиотеки](libraries.md#upload-order).
3. Дождитесь `CompileStatus = "ready"` в [`.sys/udf_modules`](../../system-views.md#udf-modules).
4. Вызовите функции из YQL.

## См. также

- [Манифест](manifest.md)
- [Библиотеки](libraries.md)
- [Ограничения](limitations.md)
- [Управление модулями](../managing.md)
