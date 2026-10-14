# IRQ Registry

*[English](#english) · [Русский](#русский)*

## English

### Model and dispatch

A peripheral uses one IIRQHandler object. Its HandleIRQ() checks enabled
peripheral/DMA sources, clears flags and invokes per-source callbacks. The same
object may register on separate IRQ lines, for example its peripheral and DMA
vectors. Distinct peripherals can share a vector and have separate registrations.

Generated ISR stubs call IRQ_Registry::Dispatch(irqn). The dispatcher reads the
fixed pointer table and calls every non-null slot in slot order. Removed slots
are reused; this order does not guarantee registration chronology. An invalid
IRQ or an empty row produces no calls. Only non-negative peripheral IRQs are
supported, not Cortex-M exception numbers.

The table is zero-initialized in .bss. No heap allocation is used. A plain
function gets an IIRQHandler adapter from a separate static pool. Object and
context lifetimes must cover their entire registration.

### Registration and errors

```cpp
static SysInitStatus Register(IRQn_Type irqn, IIRQHandler* handler);
static SysInitStatus Register(IRQn_Type irqn, IRQHandlerFn fn, void* ctx = nullptr);
```

Both overloads return InitOK on a new registration and InitError on failure:

- Invalid IRQ number or null object/function pointer.
- The same object or function is already registered on this IRQ.
- No free slot on the IRQ row, or no free function adapter.

Changing ctx does not allow registering the same function again on one IRQ.
First unregister the old function, then register it with the new context. The
same object/function can register on a different IRQ. Errors do not add entries,
replace existing registrations or permanently consume adapter slots. There is
no BKPT or infinite loop on exhaustion.

### Object registration

```cpp
// Configure the peripheral, with this IRQ still disabled.
if (IRQ_Registry::Register(TIM3_IRQn, &handler) == SysInitStatus::InitOK)
    NVIC_EnableIRQ(TIM3_IRQn);
```

Register once for the object's IRQ line. Additional sources of this peripheral
are routed by that same HandleIRQ(); enabling another source needs no new entry.
The registry itself does not enable NVIC or change peripheral interrupt bits.

### Registration without inheritance

```cpp
void OnIrq(void* ctx)
{
    auto* count = static_cast<uint32_t*>(ctx);
    ++(*count);
}

uint32_t count = 0; // Must remain valid while registered.
// Run from main while EXTI0_IRQn is masked.
if (IRQ_Registry::Register(EXTI0_IRQn, OnIrq, &count) == SysInitStatus::InitOK)
    NVIC_EnableIRQ(EXTI0_IRQn);
```

ctx is an opaque user-data pointer passed unchanged to fn(ctx). It can be
nullptr. Ordinary functions and non-capturing lambdas need no IIRQHandler class.
Function identity on a given IRQ is fn, regardless of ctx.

### Cancellation and shared IRQs

```cpp
static void Unregister(IRQn_Type irqn, IIRQHandler* handler);
static void Unregister(IRQn_Type irqn, IRQHandlerFn fn);
static bool HasHandlers(IRQn_Type irqn);
```

Addressed cancellation removes only the specified object/function. Removing a
function also releases its adapter for reuse. An invalid IRQ, null pointer or
missing registration changes nothing. HasHandlers() checks the entire row;
it returns false for invalid IRQs. Neither method changes NVIC.

When disabling one owner on a shared line, disable that owner's peripheral
sources and mask the NVIC line before cancellation. Restore the line if other
owners remain:

```cpp
// Main context; the removed owner's interrupt sources are already disabled.
NVIC_DisableIRQ(irqn);
IRQ_Registry::Unregister(irqn, &handler);
if (IRQ_Registry::HasHandlers(irqn)) NVIC_EnableIRQ(irqn);
```

For a function, use Unregister(irqn, OnIrq). To change its context, unregister
and register it again while the line is masked, checking the returned status.
The existing one-argument Unregister(irqn) explicitly clears the whole row and
releases all its adapters. Use addressed cancellation to remove one owner.

### Callback contract

Register and every Unregister overload are forbidden from HandleIRQ and its
callbacks, including modification of another IRQ line. Dispatch walks the live
table. Register/unregister from main while the affected IRQ is masked; keep
pool/table modifications serialized. This is a documented API requirement,
without runtime context checks, locks or deferred modifications.

A callback may disable the relevant peripheral interrupt source without
changing its registry entry. Registration is then cancelled later from main.
Changes to existing peripheral drivers are handled in their separate reviews.

### Capacity and memory

IRQ_MAX_SHARED defaults to 3 for F4/F7/G0. It limits handler registrations on
one IRQ line. Three slots accommodate ADC1/ADC2/ADC3 on devices sharing ADC_IRQn.
G0 may need more: G071 shares DMA1 channels 4..7 with DMAMUX overrun; G0B1 adds
DMA2 channels 1..5 on that line. Count registered objects/functions needed by
the project, and increase the common limit when necessary.

The pointer table uses IRQ_TABLE_SIZE * IRQ_MAX_SHARED * sizeof(void*) bytes.
On these 32-bit MCUs, one extra slot adds IRQ_TABLE_SIZE * 4 bytes in .bss.
IRQ_MAX_FUNCTION_HANDLERS defaults to 8 and separately limits simultaneously
registered function adapters across all lines. Repeated registration/cancellation
can reuse them; failed registration does not consume a slot.

### 5. Include order and IRQ_MAX_SHARED

The settings live in `Drivers/src/irq_registry_settings.hpp`. This header is
included by irq_registry.hpp, so the dispatcher, peripheral drivers and
application all read the same settings. No CMakeLists.txt edits are needed.

Default per-IRQ capacity:

```cpp
#ifndef IRQ_MAX_SHARED
#define IRQ_MAX_SHARED 3
#endif
```

To support five registered handlers per IRQ, change the value **in this settings
header**:

```cpp
#define IRQ_MAX_SHARED 5
```

Rebuild the driver library and all firmware images using it. A local #define in
main.cpp or a peripheral driver does not configure the other source files and
can produce inconsistent class/table definitions.

The same settings header contains IRQ_MAX_FUNCTION_HANDLERS, default 8. It
controls the separate function-adapter pool shared by all IRQ lines.

The generated irq_registry_config.h defines the IRQ count, table and ISR stubs;
it does not define the handler capacity. When updating an existing project,
replace its cached template `cmsis-core/download_files/cmake/irq_registry_config.h.in`
with the updated STM32-base_files version, then reconfigure. download_one()
keeps existing files, so reconfiguration alone does not refresh an old template
that still defines IRQ_MAX_SHARED.

## Русский

### Модель и диспетчеризация

У периферии один объект IIRQHandler. Его HandleIRQ() проверяет разрешённые
источники периферии/DMA, очищает флаги и вызывает callback соответствующего
события. Один объект может быть зарегистрирован на отдельных IRQ-линиях,
например IRQ самой периферии и IRQ её DMA. Разные периферийные объекты могут
делить одну линию и иметь отдельные регистрации.

Сгенерированные ISR вызывают IRQ_Registry::Dispatch(irqn). Диспетчер обходит
таблицу указателей по порядку слотов и вызывает все ненулевые записи. Освободившиеся
слоты используются повторно; порядок обхода не гарантирует хронологию регистрации.
При неверном IRQ или пустой строке вызовов нет. Поддерживаются неотрицательные
номера периферийных IRQ; исключения ядра Cortex-M не регистрируются.

Таблица обнуляется в .bss. Динамической памяти нет. Для обычной функции выдаётся
адаптер IIRQHandler из отдельного статического пула. Объект и данные контекста
должны оставаться действительными всё время регистрации.

### Регистрация и ошибки

```cpp
static SysInitStatus Register(IRQn_Type irqn, IIRQHandler* handler);
static SysInitStatus Register(IRQn_Type irqn, IRQHandlerFn fn, void* ctx = nullptr);
```

Обе перегрузки возвращают InitOK при новой регистрации и InitError при ошибке:

- Неверный IRQ или нулевой указатель на объект/функцию.
- Тот же объект или функция уже зарегистрированы на этом IRQ.
- В строке IRQ нет места либо закончились свободные адаптеры функций.

Другой ctx не позволяет повторно зарегистрировать ту же функцию на одном IRQ.
Сначала отмените старую регистрацию, затем зарегистрируйте функцию с новым
контекстом. Тот же объект/функция допускается на другом IRQ. Ошибка не добавляет
запись, не заменяет существующую регистрацию и не расходует адаптер навсегда.
При заполнении нет BKPT или бесконечного цикла.

### Регистрация объекта

```cpp
// Периферия настроена, эта IRQ-линия пока отключена.
if (IRQ_Registry::Register(TIM3_IRQn, &handler) == SysInitStatus::InitOK)
    NVIC_EnableIRQ(TIM3_IRQn);
```

На своей IRQ-линии объект регистрируется один раз. Дополнительные источники
этой периферии обслуживает тот же HandleIRQ(); новая запись для них не требуется.
Сам реестр не включает NVIC и не изменяет биты прерываний периферии.

### Регистрация функции

```cpp
void OnIrq(void* ctx)
{
    auto* count = static_cast<uint32_t*>(ctx);
    ++(*count);
}

uint32_t count = 0; // Должен существовать всё время регистрации.
// Вызов из main при замаскированном EXTI0_IRQn.
if (IRQ_Registry::Register(EXTI0_IRQn, OnIrq, &count) == SysInitStatus::InitOK)
    NVIC_EnableIRQ(EXTI0_IRQn);
```

ctx — пользовательский указатель на данные, передаваемый без изменений в fn(ctx).
Он может быть nullptr. Обычная функция или лямбда без захвата работают без
наследования от IIRQHandler. На конкретном IRQ функция определяется адресом fn,
независимо от ctx.

### Отмена регистрации и общие IRQ

```cpp
static void Unregister(IRQn_Type irqn, IIRQHandler* handler);
static void Unregister(IRQn_Type irqn, IRQHandlerFn fn);
static bool HasHandlers(IRQn_Type irqn);
```

Адресная отмена удаляет только указанный объект/функцию. При удалении функции её
адаптер освобождается для повторного использования. Неверный IRQ, нулевой указатель
или отсутствующая регистрация ничего не меняют. HasHandlers() проверяет всю строку;
для неверного IRQ возвращает false. Эти методы не управляют NVIC.

При отключении одного владельца общей линии отключите его источники периферии
и замаскируйте NVIC перед отменой регистрации. Если другие владельцы остались,
восстановите линию:

```cpp
// Из main; источники прерываний удаляемого владельца уже отключены.
NVIC_DisableIRQ(irqn);
IRQ_Registry::Unregister(irqn, &handler);
if (IRQ_Registry::HasHandlers(irqn)) NVIC_EnableIRQ(irqn);
```

Для функции используется Unregister(irqn, OnIrq). Контекст меняется через отмену
и новую регистрацию при замаскированной линии с проверкой результата.
Существующая перегрузка Unregister(irqn) явно очищает всю строку и освобождает
все её адаптеры. Для удаления одного владельца используется адресная отмена.

### Правила для callback

Register и все перегрузки Unregister запрещено вызывать из HandleIRQ и его
callback, в том числе для изменения другой IRQ-линии. Dispatch обходит живую
таблицу. Регистрация и отмена выполняются из main при замаскированной затронутой
линии; изменения пула и таблицы должны выполняться последовательно. Это условие
API, описанное в документации и комментариях. Проверки контекста, блокировки
и отложенные изменения не вводятся.

В callback допускается отключение соответствующего источника прерывания
периферии с сохранением регистрации. Отмена выполняется позже из main.
Приведение существующих периферийных драйверов к этому порядку выполняется
при их отдельном разборе.

### Вместимость и память

IRQ_MAX_SHARED по умолчанию равен 3 для F4/F7/G0. Он ограничивает число
регистраций на одной IRQ-линии. Трёх мест хватает для ADC1/ADC2/ADC3 на МК
с общей ADC_IRQn. G0 может потребовать больше: G071 объединяет DMA1 channels
4..7 и DMAMUX overrun; G0B1 добавляет DMA2 channels 1..5 к той же линии.
Считайте число нужных проекту зарегистрированных объектов/функций и при
необходимости увеличивайте общий лимит.

Таблица указателей занимает IRQ_TABLE_SIZE * IRQ_MAX_SHARED * sizeof(void*) байт.
На этих 32-битных МК одно дополнительное место требует IRQ_TABLE_SIZE * 4 байта
в .bss. Отдельный IRQ_MAX_FUNCTION_HANDLERS, по умолчанию 8, ограничивает число
одновременно зарегистрированных адаптеров функций на всех линиях. Циклы
регистрации/отмены используют освобождённые адаптеры; ошибка не расходует слот.

### 5. Порядок include и IRQ_MAX_SHARED

Настройки находятся в `Drivers/src/irq_registry_settings.hpp`. Заголовок
подключается из irq_registry.hpp: диспетчер, периферийные драйверы и приложение
получают одинаковые настройки. Правки CMakeLists.txt не требуются.

Лимит на один IRQ по умолчанию:

```cpp
#ifndef IRQ_MAX_SHARED
#define IRQ_MAX_SHARED 3
#endif
```

Чтобы разрешить пять зарегистрированных обработчиков на один IRQ, измените
значение **в этом заголовке настроек**:

```cpp
#define IRQ_MAX_SHARED 5
```

Пересоберите библиотеку драйверов и все использующие её образы. Локальный #define
в main.cpp или периферийном драйвере не задаёт настройки остальным исходным
файлам и может дать разные определения класса и таблицы.

В том же заголовке находится IRQ_MAX_FUNCTION_HANDLERS, по умолчанию 8. Он задаёт
размер отдельного пула адаптеров функций, общего для всех IRQ-линий.

Сгенерированный irq_registry_config.h определяет число IRQ, таблицу и ISR;
лимит обработчиков он не задаёт. При обновлении существующего проекта замените
сохранённый шаблон `cmsis-core/download_files/cmake/irq_registry_config.h.in`
новой версией из STM32-base_files, затем повторите конфигурацию. download_one()
сохраняет существующие файлы: одна переконфигурация не обновит старый шаблон,
который ещё определяет IRQ_MAX_SHARED.

### Files and execution cost / Файлы и стоимость исполнения

- `src/irq_registry.hpp`: interfaces and API contracts / интерфейсы и условия API.
- `src/irq_registry_settings.hpp`: shared user settings / общие пользовательские настройки.
- `src/irq_registry.cpp`: registration, cancellation, dispatch and adapter pool / реестр и пул.
- `STM32-base_files/cmake/irq_registry_config.h.in`: generated table and ISR template / шаблон таблицы и ISR.

The dispatcher adds table traversal and virtual calls to ISR execution. Exact
cycle counts depend on the MCU, compiler, optimization, memory and cache state;
no cycle or percentage claims are made without measuring a concrete build.

Диспетчер добавляет обход таблицы и виртуальные вызовы. Точные такты зависят
от МК, компилятора, оптимизации, памяти и кеша; без измерений конкретной сборки
числа тактов и процентов не приводятся.
