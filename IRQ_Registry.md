# IRQ Registry — Detailed Description

*[English](#english) · [Русский](#русский)*

---

## English

## Table of Contents

1. [Why This Is Needed](#1-why-this-is-needed)
2. [Model and dispatch](#2-model-and-dispatch)
3. [Registration and errors](#3-registration-and-errors)
4. [Object registration](#4-object-registration)
5. [Registration without inheritance](#5-registration-without-inheritance)
6. [Cancellation and shared IRQs](#6-cancellation-and-shared-irqs)
7. [Callback contract](#7-callback-contract)
8. [Capacity and memory](#8-capacity-and-memory)
9. [Include order and IRQ_MAX_SHARED](#9-include-order-and-irq_max_shared)
10. [Integration Details](#10-integration-details)
11. [Usage Examples](#11-usage-examples)
12. [Edge Cases](#12-edge-cases)
13. [See Also](#see-also)

---

## 1. Why This Is Needed

In plain CMSIS projects every interrupt has a fixed function name (`USART3_IRQHandler`), and only one piece of code can own it. The registry removes that limitation:

- every hardware vector gets a generated stub that calls `IRQ_Registry::Dispatch(irqn)`;
- drivers (or your code) **register** a handler for a vector at run time;
- several handlers can share one vector (e.g. ADC1/ADC2/ADC3 on `ADC_IRQn`);
- removing one handler leaves the others on that vector untouched.

This is what lets the C++ drivers own their interrupts without you writing any `*_IRQHandler` functions.

---

## 2. Model and dispatch

**One handler object per peripheral.** A driver implements `IIRQHandler::HandleIRQ()`. Inside, it checks which of its enabled sources fired, clears their flags and calls the matching callback (`OnRx`, `OnTC`, …).

- The same object may be registered on several vectors — e.g. the peripheral's own vector and the vector of its DMA stream.
- Different peripherals can share one vector, each with its own registration.

**Dispatch:**

```text
hardware IRQ → generated stub → IRQ_Registry::Dispatch(irqn)
                                   └─ for each non-null slot in table[irqn]: slot->HandleIRQ()
```

- Slots are visited in slot order. Freed slots are reused, so the order is **not** registration order.
- An invalid IRQ number or an empty row calls nothing.
- Only peripheral IRQs (non-negative numbers) are supported, not Cortex-M exceptions (SysTick, HardFault…).

**Memory:**

- The table is a zero-initialized array in `.bss`; no heap is used.
- A plain function (not a class) gets an `IIRQHandler` adapter from a separate static pool.
- The registered object and any context data must stay alive for as long as they are registered.

---

## 3. Registration and errors

```cpp
static SysInitStatus Register(IRQn_Type irqn, IIRQHandler* handler);
static SysInitStatus Register(IRQn_Type irqn, IRQHandlerFn fn, void* ctx = nullptr);
```

Both return `InitOK` for a new registration and `InitError` when:

- the IRQ number is invalid, or the object/function pointer is null;
- the same object or function is already registered on this IRQ;
- the IRQ's row is full (`IRQ_MAX_SHARED`), or the function-adapter pool is empty.

Notes:

- The same function with a **different `ctx`** still counts as a duplicate on the same IRQ. To change the context, unregister first, then register again.
- The same object/function may be registered on a **different** IRQ.
- A failed call adds nothing, replaces nothing and does not leak an adapter.
- A full table is reported by the return value — there is no breakpoint or endless loop.

---

## 4. Object registration

```cpp
// Peripheral is configured; this IRQ is still disabled in NVIC.
if (IRQ_Registry::Register(TIM3_IRQn, &handler) == SysInitStatus::InitOK)
    NVIC_EnableIRQ(TIM3_IRQn);
```

- Register **once** per vector. Enabling another source of the same peripheral needs no new entry — the same `HandleIRQ()` handles it.
- The registry never enables NVIC and never touches peripheral interrupt-enable bits. That is the caller's job.

---

## 5. Registration without inheritance

A plain function or a non-capturing lambda can be registered without writing an `IIRQHandler` class:

```cpp
void OnIrq(void* ctx)
{
    auto* count = static_cast<uint32_t*>(ctx);
    ++(*count);
}

uint32_t count = 0; // Must stay valid while registered.
// From main, while EXTI0_IRQn is masked:
if (IRQ_Registry::Register(EXTI0_IRQn, OnIrq, &count) == SysInitStatus::InitOK)
    NVIC_EnableIRQ(EXTI0_IRQn);
```

- `ctx` is your own pointer, passed unchanged to `fn(ctx)`. It may be `nullptr`.
- On a given IRQ the function is identified by `fn` alone, regardless of `ctx`.

---

## 6. Cancellation and shared IRQs

```cpp
static void Unregister(IRQn_Type irqn, IIRQHandler* handler);
static void Unregister(IRQn_Type irqn, IRQHandlerFn fn);
static bool HasHandlers(IRQn_Type irqn);
```

- `Unregister(irqn, handler/fn)` removes **only** that object or function. A function's adapter goes back to the pool.
- An invalid IRQ, a null pointer or a missing registration changes nothing.
- `HasHandlers(irqn)` tells whether anything is left on the vector (`false` for an invalid IRQ).
- None of these touch NVIC.

**Removing one owner from a shared vector:**

1. Disable that owner's interrupt sources in the peripheral.
2. Mask the vector in NVIC.
3. Unregister.
4. Re-enable the vector if other owners remain.

```cpp
// From main; the removed owner's interrupt sources are already disabled.
NVIC_DisableIRQ(irqn);
IRQ_Registry::Unregister(irqn, &handler);
if (IRQ_Registry::HasHandlers(irqn)) NVIC_EnableIRQ(irqn);
```

For a function use `Unregister(irqn, OnIrq)`. To change its context, unregister and register again while the vector is masked, and check the result.

The one-argument `Unregister(irqn)` clears the **whole** row and releases all its adapters. Use the addressed form to remove a single owner.

---

## 7. Callback contract

> **Never call `Register` or any `Unregister` from `HandleIRQ()` or its callbacks** — not even for a different vector.

`Dispatch` walks the live table; changing it from inside an interrupt can skip or double-call handlers.

- Register and unregister from `main`, with the affected vector masked.
- Keep all table and pool changes serialized (one at a time).
- This is a documented API rule. There are no run-time context checks, locks or deferred changes.

What a callback **may** do: disable its own peripheral interrupt source while keeping the registration. Unregister later from `main`.

---

## 8. Capacity and memory

**`IRQ_MAX_SHARED`** (default **3** on F4/F7/G0) — maximum handlers on one vector.

- 3 is enough for ADC1/ADC2/ADC3 sharing `ADC_IRQn`.
- G0 may need more: on G071 one vector serves DMA1 channels 4…7 plus DMAMUX overrun; G0B1 adds DMA2 channels 1…5 to that vector.
- Count the objects/functions your project registers on each vector and raise the limit if needed.

**RAM cost of the table:**

```text
IRQ_TABLE_SIZE × IRQ_MAX_SHARED × 4 bytes   (.bss, 32-bit pointers)
```

Each extra slot per vector costs `IRQ_TABLE_SIZE × 4` bytes — roughly 0.4 KB on a device with ~100 vectors.

**`IRQ_MAX_FUNCTION_HANDLERS`** (default **8**) — size of the function-adapter pool shared by all vectors. Unregistering returns adapters to the pool; a failed registration does not consume one.

---

## 9. Include order and IRQ_MAX_SHARED

Both settings live in **`Drivers/src/irq_registry_settings.hpp`**. It is included by `irq_registry.hpp`, so the dispatcher, all drivers and your application see the same values. No `CMakeLists.txt` change is needed.

Default:

```cpp
#ifndef IRQ_MAX_SHARED
#define IRQ_MAX_SHARED 3
#endif
```

To allow five handlers per vector, change the value **in that settings header**:

```cpp
#define IRQ_MAX_SHARED 5
```

Then rebuild the driver library and every firmware image that uses it.

> **Do not** `#define IRQ_MAX_SHARED` locally in `main.cpp` or a driver. Other source files would not see it, and you would get different class/table layouts in different files.

The generated `irq_registry_config.h` defines the IRQ count, the table and the ISR stubs — but **not** the handler capacity. When updating an older project, replace the cached template `cmsis-core/download_files/cmake/irq_registry_config.h.in` with the current STM32-base_files version and reconfigure. `download_one()` keeps existing files, so reconfiguring alone will not refresh an old template that still defines `IRQ_MAX_SHARED`.

---

## 10. Integration Details

### Files and execution cost

| File | Responsibility |
| --- | --- |
| `src/irq_registry.hpp` | Interfaces and API rules |
| `src/irq_registry_settings.hpp` | User capacity settings |
| `src/irq_registry.cpp` | Registration, cancellation, dispatch, function-adapter pool |
| `STM32-base_files/cmake/irq_registry_config.h.in` | Template for the generated table and ISR stubs |

Cost: every interrupt adds a table walk and a virtual call per registered handler. The exact cycle count depends on the MCU, compiler, optimization level, memory and cache, so no fixed numbers are claimed here — measure your build if it matters.

---

## 11. Usage Examples

A minimal custom handler:

```cpp
#include "irq_registry.hpp"

class Handler : public IIRQHandler {
public:
    void HandleIRQ() override { /* check and clear this peripheral's flags */ }
};

static Handler handler;

// In main, with the vector masked; irq is the chosen hardware vector:
if (IRQ_Registry::Register(irq, &handler) == SysInitStatus::InitOK) {
    // configure the peripheral's interrupt sources, then:
    NVIC_EnableIRQ(irq);
}

// Later, from main:
// NVIC_DisableIRQ(irq);
// IRQ_Registry::Unregister(irq, &handler);
// if (IRQ_Registry::HasHandlers(irq)) NVIC_EnableIRQ(irq);
```

Registration does not configure peripheral interrupt sources or NVIC for you.

---

## 12. Edge Cases

| Situation | Behaviour |
| --- | --- |
| Same object/function registered twice on one IRQ | `InitError` |
| More than `IRQ_MAX_SHARED` handlers on a vector | `InitError` |
| Function pool exhausted | `InitError` |
| `Register`/`Unregister` called from a callback | Forbidden — undefined dispatch order |
| Registration changes while the vector is live | Caller must mask the vector / serialize with `Dispatch` |

---

## See Also

- [System.md](System.md)
- [EXTI.md](EXTI.md)
- [UART.md](UART.md)
- [TIM.md](TIM.md)

---

## Русский

## Содержание

1. [Зачем это нужно](#1-зачем-это-нужно)
2. [Модель и диспетчеризация](#2-модель-и-диспетчеризация)
3. [Регистрация и ошибки](#3-регистрация-и-ошибки)
4. [Регистрация объекта](#4-регистрация-объекта)
5. [Регистрация функции](#5-регистрация-функции)
6. [Отмена регистрации и общие IRQ](#6-отмена-регистрации-и-общие-irq)
7. [Правила для callback](#7-правила-для-callback)
8. [Вместимость и память](#8-вместимость-и-память)
9. [Порядок include и IRQ_MAX_SHARED](#9-порядок-include-и-irq_max_shared)
10. [Подробности интеграции](#10-подробности-интеграции)
11. [Примеры использования](#11-примеры-использования)
12. [Угловые случаи](#12-угловые-случаи)
13. [См. также](#см-также)

---

## 1. Зачем это нужно

В обычном CMSIS-проекте у каждого прерывания фиксированное имя функции (`USART3_IRQHandler`), и владеть им может только один кусок кода. Реестр снимает это ограничение:

- для каждого аппаратного вектора генерируется заглушка, вызывающая `IRQ_Registry::Dispatch(irqn)`;
- драйверы (или ваш код) **регистрируют** обработчик вектора во время работы;
- один вектор могут делить несколько обработчиков (например, ADC1/ADC2/ADC3 на `ADC_IRQn`);
- удаление одного обработчика не затрагивает остальных на этом векторе.

Благодаря этому C++-драйверы сами владеют своими прерываниями, и вам не нужно писать функции `*_IRQHandler`.

---

## 2. Модель и диспетчеризация

**Один объект-обработчик на периферию.** Драйвер реализует `IIRQHandler::HandleIRQ()`. Внутри он проверяет, какие из разрешённых источников сработали, сбрасывает их флаги и вызывает нужный callback (`OnRx`, `OnTC`, …).

- Один объект можно зарегистрировать на нескольких векторах — например, на векторе самой периферии и на векторе её потока DMA.
- Разные периферийные блоки могут делить один вектор, у каждого своя регистрация.

**Диспетчеризация:**

```text
аппаратное IRQ → сгенерированная заглушка → IRQ_Registry::Dispatch(irqn)
                                              └─ для каждого ненулевого слота table[irqn]: slot->HandleIRQ()
```

- Слоты обходятся по порядку номеров. Освободившиеся слоты используются повторно, поэтому порядок **не совпадает** с порядком регистрации.
- При неверном номере IRQ или пустой строке ничего не вызывается.
- Поддерживаются только периферийные IRQ (неотрицательные номера), не исключения Cortex-M (SysTick, HardFault…).

**Память:**

- Таблица — обнулённый массив в `.bss`; динамическая память не используется.
- Обычной функции (не классу) выдаётся адаптер `IIRQHandler` из отдельного статического пула.
- Зарегистрированный объект и данные контекста должны существовать всё время регистрации.

---

## 3. Регистрация и ошибки

```cpp
static SysInitStatus Register(IRQn_Type irqn, IIRQHandler* handler);
static SysInitStatus Register(IRQn_Type irqn, IRQHandlerFn fn, void* ctx = nullptr);
```

Обе возвращают `InitOK` при новой регистрации и `InitError`, если:

- номер IRQ неверный или указатель на объект/функцию нулевой;
- тот же объект или функция уже зарегистрированы на этом IRQ;
- строка IRQ заполнена (`IRQ_MAX_SHARED`) или кончились адаптеры функций.

Замечания:

- Та же функция с **другим `ctx`** на том же IRQ всё равно считается повтором. Чтобы сменить контекст, сначала отмените регистрацию, затем зарегистрируйте заново.
- Тот же объект/функцию можно зарегистрировать на **другом** IRQ.
- Неудачный вызов ничего не добавляет, ничего не заменяет и не теряет адаптер.
- О заполненной таблице сообщает код возврата — без точки останова и бесконечного цикла.

---

## 4. Регистрация объекта

```cpp
// Периферия настроена, эта линия в NVIC пока выключена.
if (IRQ_Registry::Register(TIM3_IRQn, &handler) == SysInitStatus::InitOK)
    NVIC_EnableIRQ(TIM3_IRQn);
```

- Регистрируйтесь **один раз** на вектор. Включение ещё одного источника той же периферии новой записи не требует — его обработает тот же `HandleIRQ()`.
- Реестр никогда не включает NVIC и не трогает биты разрешения прерываний периферии. Это делает вызывающий код.

---

## 5. Регистрация функции

Обычную функцию или лямбду без захвата можно зарегистрировать без класса-наследника `IIRQHandler`:

```cpp
void OnIrq(void* ctx)
{
    auto* count = static_cast<uint32_t*>(ctx);
    ++(*count);
}

uint32_t count = 0; // Должен существовать всё время регистрации.
// Из main, пока EXTI0_IRQn замаскирован:
if (IRQ_Registry::Register(EXTI0_IRQn, OnIrq, &count) == SysInitStatus::InitOK)
    NVIC_EnableIRQ(EXTI0_IRQn);
```

- `ctx` — ваш указатель, передаётся в `fn(ctx)` без изменений. Может быть `nullptr`.
- На конкретном IRQ функция определяется только адресом `fn`, независимо от `ctx`.

---

## 6. Отмена регистрации и общие IRQ

```cpp
static void Unregister(IRQn_Type irqn, IIRQHandler* handler);
static void Unregister(IRQn_Type irqn, IRQHandlerFn fn);
static bool HasHandlers(IRQn_Type irqn);
```

- `Unregister(irqn, handler/fn)` удаляет **только** этот объект или функцию. Адаптер функции возвращается в пул.
- Неверный IRQ, нулевой указатель или отсутствующая регистрация ничего не меняют.
- `HasHandlers(irqn)` сообщает, остался ли на векторе кто-нибудь (`false` для неверного IRQ).
- Ни один из методов не трогает NVIC.

**Удаление одного владельца общего вектора:**

1. Отключите источники прерываний этого владельца в периферии.
2. Замаскируйте вектор в NVIC.
3. Отмените регистрацию.
4. Если остались другие владельцы — снова разрешите вектор.

```cpp
// Из main; источники прерываний удаляемого владельца уже отключены.
NVIC_DisableIRQ(irqn);
IRQ_Registry::Unregister(irqn, &handler);
if (IRQ_Registry::HasHandlers(irqn)) NVIC_EnableIRQ(irqn);
```

Для функции используйте `Unregister(irqn, OnIrq)`. Контекст меняется отменой и повторной регистрацией при замаскированном векторе, с проверкой результата.

Перегрузка `Unregister(irqn)` с одним аргументом очищает **всю** строку и освобождает все её адаптеры. Для удаления одного владельца используйте адресную форму.

---

## 7. Правила для callback

> **Никогда не вызывайте `Register` или `Unregister` из `HandleIRQ()` и его callback** — даже для другого вектора.

`Dispatch` обходит живую таблицу; изменение её изнутри прерывания может пропустить обработчик или вызвать его дважды.

- Регистрируйте и отменяйте из `main` при замаскированном затронутом векторе.
- Изменения таблицы и пула выполняйте строго по одному.
- Это задокументированное правило API. Проверок контекста, блокировок и отложенных изменений нет.

Что callback **может**: отключить свой источник прерывания в периферии, сохранив регистрацию. Отмену регистрации выполните позже из `main`.

---

## 8. Вместимость и память

**`IRQ_MAX_SHARED`** (по умолчанию **3** на F4/F7/G0) — максимум обработчиков на одном векторе.

- 3 хватает для ADC1/ADC2/ADC3 на общем `ADC_IRQn`.
- G0 может потребовать больше: на G071 один вектор обслуживает каналы 4…7 DMA1 и переполнение DMAMUX; на G0B1 к нему добавляются каналы 1…5 DMA2.
- Посчитайте, сколько объектов/функций ваш проект регистрирует на каждом векторе, и при необходимости увеличьте лимит.

**Расход RAM на таблицу:**

```text
IRQ_TABLE_SIZE × IRQ_MAX_SHARED × 4 байта   (.bss, 32-битные указатели)
```

Каждый дополнительный слот на вектор стоит `IRQ_TABLE_SIZE × 4` байт — примерно 0,4 КБ на МК с ~100 векторами.

**`IRQ_MAX_FUNCTION_HANDLERS`** (по умолчанию **8**) — размер пула адаптеров функций, общего для всех векторов. Отмена регистрации возвращает адаптер в пул; неудачная регистрация его не расходует.

---

## 9. Порядок include и IRQ_MAX_SHARED

Обе настройки находятся в **`Drivers/src/irq_registry_settings.hpp`**. Его подключает `irq_registry.hpp`, поэтому диспетчер, все драйверы и приложение видят одни и те же значения. Правки `CMakeLists.txt` не нужны.

По умолчанию:

```cpp
#ifndef IRQ_MAX_SHARED
#define IRQ_MAX_SHARED 3
#endif
```

Чтобы разрешить пять обработчиков на вектор, измените значение **в этом заголовке настроек**:

```cpp
#define IRQ_MAX_SHARED 5
```

Затем пересоберите библиотеку драйверов и все использующие её прошивки.

> **Не** определяйте `IRQ_MAX_SHARED` локально в `main.cpp` или драйвере. Остальные файлы этого не увидят, и в разных файлах получатся разные раскладки класса и таблицы.

Сгенерированный `irq_registry_config.h` задаёт число IRQ, таблицу и заглушки ISR — но **не** вместимость. При обновлении старого проекта замените сохранённый шаблон `cmsis-core/download_files/cmake/irq_registry_config.h.in` актуальной версией из STM32-base_files и переконфигурируйте. `download_one()` не перезаписывает существующие файлы, поэтому одна переконфигурация не обновит старый шаблон, где ещё определён `IRQ_MAX_SHARED`.

---

## 10. Подробности интеграции

### Файлы и стоимость исполнения

| Файл | Назначение |
| --- | --- |
| `src/irq_registry.hpp` | Интерфейсы и правила API |
| `src/irq_registry_settings.hpp` | Пользовательские настройки вместимости |
| `src/irq_registry.cpp` | Регистрация, отмена, диспетчеризация, пул адаптеров |
| `STM32-base_files/cmake/irq_registry_config.h.in` | Шаблон генерируемой таблицы и заглушек ISR |

Стоимость: каждое прерывание добавляет обход строки таблицы и виртуальный вызов на каждый зарегистрированный обработчик. Точное число тактов зависит от МК, компилятора, оптимизации, памяти и кеша, поэтому фиксированных цифр здесь нет — при необходимости измерьте свою сборку.

---

## 11. Примеры использования

Минимальный собственный обработчик:

```cpp
#include "irq_registry.hpp"

class Handler : public IIRQHandler {
public:
    void HandleIRQ() override { /* проверить и сбросить флаги своей периферии */ }
};

static Handler handler;

// В main при замаскированном векторе; irq — выбранный аппаратный вектор:
if (IRQ_Registry::Register(irq, &handler) == SysInitStatus::InitOK) {
    // настроить источники прерываний периферии, затем:
    NVIC_EnableIRQ(irq);
}

// Позже, из main:
// NVIC_DisableIRQ(irq);
// IRQ_Registry::Unregister(irq, &handler);
// if (IRQ_Registry::HasHandlers(irq)) NVIC_EnableIRQ(irq);
```

Регистрация не настраивает за вас источники прерываний периферии и NVIC.

---

## 12. Угловые случаи

| Ситуация | Поведение |
| --- | --- |
| Тот же объект/функция дважды на одном IRQ | `InitError` |
| Больше `IRQ_MAX_SHARED` обработчиков на векторе | `InitError` |
| Пул функций исчерпан | `InitError` |
| `Register`/`Unregister` из callback | Запрещено — порядок обхода не определён |
| Изменение регистрации при активном векторе | Вызывающий код маскирует вектор / согласует с `Dispatch` |

---

## См. также

- [System.md](System.md)
- [EXTI.md](EXTI.md)
- [UART.md](UART.md)
- [TIM.md](TIM.md)
