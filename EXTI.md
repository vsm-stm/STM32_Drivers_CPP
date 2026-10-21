# EXTI / EXTI_Line — Detailed Description

*[English](#english) · [Русский](#русский)*

---

## English

## Table of Contents

1. [Why This Is Needed](#1-why-this-is-needed)
2. [Constant Initialization and Constructor Diagnostics](#2-constant-initialization-and-constructor-diagnostics)
3. [Module Selection and Dependencies](#3-module-selection-and-dependencies)
4. [Binding and initialization](#4-binding-and-initialization)
5. [IRQ and capacity](#5-irq-and-capacity)
6. [Masking and teardown](#6-masking-and-teardown)
7. [Usage Examples](#7-usage-examples)
8. [Edge Cases](#8-edge-cases)
9. [See Also](#see-also)

---

## 1. Why This Is Needed

`EXTI_Line` turns a GPIO pin into an interrupt source: "call me when PC13 goes low".

- The **constructor** fixes *which* pin and *which* edge (rising, falling or both).
- **`SetUp()`** configures the pin as an input, routes the EXTI line to the right port, enables the edge detector and registers the object in the shared [IRQ_Registry](IRQ_Registry.md).
- Your class overrides **`OnInterrupt()`** and receives a call on every detected edge.

Only the 16 external GPIO lines (EXTI0…EXTI15) are covered. Internal EXTI sources (RTC, PVD, COMP, USB wake-up…) belong to their own drivers.

---

## 2. Constant Initialization and Constructor Diagnostics

Declare fixed peripheral objects at file scope with `constinit` (or as `static constinit` inside a function). `EXTI_Line` takes only a `PIN` and an `Edge` — there is no `Instance` argument and no DMA.

What happens with a wrong argument:

| Declaration | Result |
| --- | --- |
| `constinit` object | **Compilation error.** The message contains the reason, e.g. `"EXTI: a valid external GPIO pin is required"`. |
| Object without `constinit` | Compiles; at start-up the constructor stops in `System::DebugTrap(reason)`. |

How it works: every check calls `STM32_CONFIG_ERROR("reason")`. During compile-time evaluation this calls a deliberately non-`constexpr` function whose template argument is the reason text, so GCC prints that text. At run time the same macro calls `System::DebugTrap(text)` directly.

`constinit` is required by the usage contract but not enforced by the constructor. `SetUp()` still checks the things only known at run time: hardware state and occupied lines. Constructor variants and migration: [Initialization.md](Initialization.md).

---

## 3. Module Selection and Dependencies

Enable the module in `project.json`:

```json
"drivers": ["EXTI"]
```

Include `exti.hpp`.

- **Uses:** the core drivers System/RCC, GPIO and IRQ_Registry.
- **Does not need:** DMA or a timer.
- **Families:** external lines 0…15 on STM32F4, F7 and G0.

---

## 4. Binding and initialization

```cpp
#include "exti.hpp"

class Button final : public EXTI_Line {
public:
    constexpr Button() : EXTI_Line(PIN(GPIOC_BASE, 13), Edge::Falling) {}
    volatile bool pressed = false;
protected:
    void OnInterrupt() override { pressed = true; }
};

constinit Button button;

// In main, after System/RCC initialization:
// if (button.SetUp(PIN::TYPE::INPUT_PullUp) != SysInitStatus::InitOK) {
//     ... handle setup failure ...
// }
```

**Constructor** — no register access, only binding:

- Edge: `Edge::Rising`, `Edge::Falling` or `Edge::Both`.
- With `constinit`, an absent pin, a pin number outside 0…15 or an invalid edge is a compile error.
- Use `PIN(GPIOx_BASE, number)`. The overload taking a CMSIS pointer (`PIN(GPIOC, 13)`) runs at run time and cannot be checked at compile time.
- Whether the pin actually exists in your package is your responsibility.
- `PIN::GetNumber()` only reads the bound number; it cannot rebind the pin.

**`SetUp(type)`** — accepts `INPUT_NO_Pull` (default), `INPUT_PullUp` or `INPUT_PullDown`. It:

1. configures the GPIO input and pull resistor;
2. routes the EXTI line to the pin's port;
3. selects the edges and clears stale pending flags;
4. registers the object and enables the line and its NVIC vector.

`SetUp()` returns `InitError` — and leaves GPIO/EXTI untouched — when:

- the object is already set up;
- the line number is owned by another object;
- the input type is not one of the three above;
- registration in IRQ_Registry fails (the previous NVIC mask is restored).

**One owner per line number.** PA0 and PB0 cannot both use EXTI0, because the hardware has only one EXTI0 line. Objects cannot be copied or moved, and the pin/edge cannot change at run time. To reconfigure, call `DeInit()` from `main` first.

---

## 5. IRQ and capacity

Each object registers itself separately in IRQ_Registry. When the vector fires, its handler:

- reads only its own line's mask and pending flags;
- acknowledges its own line;
- calls `OnInterrupt()` (protected virtual, empty by default).

The handler never reads GPIO, waits, allocates memory or changes the registry.

> **Not an edge counter.** On G0 the rising and falling pending registers are read once each; if both flags are set, you get **one** notification. The callback does not tell which edge occurred and does not guarantee a call for every physical transition.

Several lines share one NVIC vector:

| Family | Hardware vectors for GPIO lines |
| --- | --- |
| F4/F7 | EXTI0…EXTI4 separately, EXTI9_5 for 5…9, EXTI15_10 for 10…15 |
| G0 | EXTI0_1 for 0…1, EXTI2_3 for 2…3, EXTI4_15 for 4…15 |

**Capacity limit: `IRQ_MAX_SHARED` = 3 registrations per vector (default).**

- On G0, at most three `EXTI_Line` objects fit on EXTI4_15; a fourth `SetUp()` returns `InitError`.
- The same limit applies to the shared F4/F7 vectors (EXTI9_5, EXTI15_10).
- Other handlers registered on the same vector count too.
- The driver does not raise the limit or create its own dispatch table. To change it, edit `irq_registry_settings.hpp` and rebuild the registry and all its users (there is no CMake option).

**Rules for the application:**

- Serialize configuration, registry and shared-register changes made from `main` with the affected EXTI interrupts.
- Never call `SetUp()`/`DeInit()` from a callback.
- Do not change the GPIO configuration or EXTI routing of a line that has an active owner.
- Keep the object alive while it is registered.

---

## 6. Masking and teardown

```cpp
button.IRQ_en(DISABLE); // Masks only this line; NVIC is untouched.
button.IRQ_en(ENABLE);  // Pending events are kept and may fire immediately.
button.DeInit();        // From main only; no callback is invoked.
```

**`IRQ_en()`** changes only the line's interrupt mask. GPIO, edge selection, registration, pending bits and the shared NVIC vector stay as they are. Before `SetUp()` it does nothing.

**`DeInit()`**:

- masks the line, disables its edge detection and clears its pending flags;
- removes only *this* object from the registry and frees the line number;
- if other handlers remain on the vector, restores their previous NVIC enable state; if none remain, keeps the vector disabled and clears its NVIC pending bit;
- keeps the GPIO input/pull, the port routing and shared clocks;
- keeps the constructor binding, so `SetUp()` can be called again.

The destructor does **not** tear anything down — call `DeInit()` before destroying the object.

**`EXTI_USE_ATOMIC`** (in `exti.hpp`, default `0`): when set to `1`, read-modify-write updates of control registers use ST's `ATOMIC_*` macros. Pending flags are always cleared by a direct write-1-to-clear access. The setting does not make whole multi-step operations atomic and does not replace the rules in section 5.

---

## 7. Usage Examples

A complete, independent example. Check that the pin exists in your package and check every returned status.

```cpp
#include "exti.hpp"

class Button : public EXTI_Line {
public:
    constexpr Button() : EXTI_Line(PIN(GPIOC_BASE, 13), Edge::Falling) {}
    volatile bool pressed = false;
protected:
    void OnInterrupt() override { pressed = true; }
};

constinit Button button;

int main()
{
    // ... successful System/RCC initialization ...
    if (button.SetUp(PIN::TYPE::INPUT_PullUp) != SysInitStatus::InitOK) {
        // handle the error
    }
    for (;;) {
        if (button.pressed) {
            button.pressed = false;
            // react to the button
        }
    }
}
```

---

## 8. Edge Cases

| Situation | Behaviour |
| --- | --- |
| PA0 and PB0 both want EXTI0 | Impossible — one owner per line number; the second `SetUp()` returns `InitError`. |
| More handlers than `IRQ_MAX_SHARED` on one vector | `SetUp()` returns `InitError`. |
| Several edges in quick succession | May produce fewer callbacks; do not use as a pulse counter. |
| Callback wants to reconfigure | Not allowed — callbacks must not change registration or setup. |
| `main` changes shared EXTI registers | Must be serialized with the affected interrupts. |

---

## See Also

- [ST G0 LL EXTI](https://github.com/STMicroelectronics/stm32g0xx-hal-driver/blob/master/Inc/stm32g0xx_ll_exti.h): EXTICR layout, port selection, RPR1/FPR1 acknowledgement.
- [ST F4 LL EXTI](https://github.com/STMicroelectronics/stm32f4xx-hal-driver/blob/master/Inc/stm32f4xx_ll_exti.h): masks, edge selection and PR acknowledgement.
- [Initialization.md](Initialization.md)
- [GPIO.md](GPIO.md)
- [IRQ_Registry.md](IRQ_Registry.md)
- [System.md](System.md)

---

## Русский

## Содержание

1. [Зачем это нужно](#1-зачем-это-нужно)
2. [Постоянная инициализация и диагностика конструктора](#2-постоянная-инициализация-и-диагностика-конструктора)
3. [Подключение и зависимости](#3-подключение-и-зависимости)
4. [Привязка и настройка](#4-привязка-и-настройка)
5. [Прерывания и ограничение реестра](#5-прерывания-и-ограничение-реестра)
6. [Маскирование и освобождение](#6-маскирование-и-освобождение)
7. [Примеры использования](#7-примеры-использования)
8. [Угловые случаи](#8-угловые-случаи)
9. [См. также](#см-также)

---

## 1. Зачем это нужно

`EXTI_Line` превращает вывод GPIO в источник прерывания: «позови меня, когда PC13 упадёт в ноль».

- **Конструктор** закрепляет, *какой* вывод и *какой* фронт отслеживать (нарастающий, спадающий или оба).
- **`SetUp()`** настраивает вывод на вход, направляет линию EXTI на нужный порт, включает детектор фронта и регистрирует объект в общем [IRQ_Registry](IRQ_Registry.md).
- Ваш класс переопределяет **`OnInterrupt()`** и получает вызов на каждый обнаруженный фронт.

Модуль работает только с 16 внешними линиями GPIO (EXTI0…EXTI15). Внутренние источники EXTI (RTC, PVD, COMP, пробуждение USB…) относятся к своим драйверам.

---

## 2. Постоянная инициализация и диагностика конструктора

Объявляйте постоянные объекты периферии глобально с `constinit` (или как `static constinit` внутри функции). `EXTI_Line` принимает только `PIN` и `Edge` — без `Instance` и без DMA.

Что происходит при неверном аргументе:

| Объявление | Результат |
| --- | --- |
| Объект с `constinit` | **Ошибка компиляции.** В сообщении есть причина, например `"EXTI: a valid external GPIO pin is required"`. |
| Объект без `constinit` | Собирается; при старте конструктор останавливается в `System::DebugTrap(причина)`. |

Как это устроено: каждая проверка вызывает `STM32_CONFIG_ERROR("причина")`. При вычислении на этапе компиляции макрос вызывает намеренно не-`constexpr` функцию, у которой текст причины — параметр шаблона, поэтому GCC печатает этот текст. Во время выполнения тот же макрос напрямую вызывает `System::DebugTrap(text)`.

`constinit` обязателен по правилам использования, но конструктор его наличие не проверяет. `SetUp()` по-прежнему проверяет то, что известно только во время работы: состояние аппаратуры и занятость линий. Варианты конструкторов и миграция: [Initialization.md](Initialization.md).

---

## 3. Подключение и зависимости

Включите модуль в `project.json`:

```json
"drivers": ["EXTI"]
```

Заголовок — `exti.hpp`.

- **Использует:** ядро System/RCC, GPIO и IRQ_Registry.
- **Не требует:** DMA и таймера.
- **Семейства:** внешние линии 0…15 на STM32F4, F7 и G0.

---

## 4. Привязка и настройка

```cpp
#include "exti.hpp"

class Button final : public EXTI_Line {
public:
    constexpr Button() : EXTI_Line(PIN(GPIOC_BASE, 13), Edge::Falling) {}
    volatile bool pressed = false;
protected:
    void OnInterrupt() override { pressed = true; }
};

constinit Button button;

// В main, после инициализации System/RCC:
// if (button.SetUp(PIN::TYPE::INPUT_PullUp) != SysInitStatus::InitOK) {
//     ... обработка ошибки ...
// }
```

**Конструктор** — только привязка, без обращения к регистрам:

- Фронт: `Edge::Rising`, `Edge::Falling` или `Edge::Both`.
- С `constinit` отсутствующий вывод, номер вне 0…15 или неверный фронт — ошибка компиляции.
- Используйте `PIN(GPIOx_BASE, номер)`. Перегрузка с CMSIS-указателем (`PIN(GPIOC, 13)`) выполняется во время работы и не проверяется при компиляции.
- Есть ли такой вывод в вашем корпусе, проверяет приложение.
- `PIN::GetNumber()` только читает номер, перепривязать вывод через него нельзя.

**`SetUp(type)`** — принимает `INPUT_NO_Pull` (по умолчанию), `INPUT_PullUp` или `INPUT_PullDown`. Метод:

1. настраивает вход GPIO и подтяжку;
2. направляет линию EXTI на порт вывода;
3. выбирает фронты и сбрасывает старые флаги ожидания;
4. регистрирует объект и разрешает линию и её вектор NVIC.

`SetUp()` возвращает `InitError` — и не трогает GPIO/EXTI — если:

- объект уже настроен;
- номер линии занят другим объектом;
- тип входа не из трёх перечисленных;
- регистрация в IRQ_Registry не удалась (прежняя маска NVIC восстанавливается).

**Один владелец на номер линии.** PA0 и PB0 не могут одновременно использовать EXTI0: в аппаратуре одна линия EXTI0. Объекты нельзя копировать и перемещать, вывод и фронт во время работы не меняются. Для перенастройки сначала вызовите `DeInit()` из `main`.

---

## 5. Прерывания и ограничение реестра

Каждый объект отдельно регистрируется в IRQ_Registry. Когда срабатывает вектор, его обработчик:

- читает только маску и флаги ожидания своей линии;
- подтверждает событие своей линии;
- вызывает `OnInterrupt()` (protected virtual, по умолчанию пустой).

Обработчик не читает GPIO, ничего не ждёт, не выделяет память и не меняет реестр.

> **Это не счётчик фронтов.** На G0 регистры нарастающих и спадающих фронтов читаются по одному разу; если стоят оба флага, вызов будет **один**. Callback не сообщает, какой фронт был, и не гарантирует вызов на каждый физический переход.

Несколько линий делят один вектор NVIC:

| Семейство | Аппаратные векторы для линий GPIO |
| --- | --- |
| F4/F7 | EXTI0…EXTI4 отдельно, EXTI9_5 для 5…9, EXTI15_10 для 10…15 |
| G0 | EXTI0_1 для 0…1, EXTI2_3 для 2…3, EXTI4_15 для 4…15 |

**Ограничение: `IRQ_MAX_SHARED` = 3 регистрации на вектор (по умолчанию).**

- На G0 на EXTI4_15 помещается не больше трёх объектов `EXTI_Line`; четвёртый `SetUp()` вернёт `InitError`.
- То же ограничение действует для общих векторов F4/F7 (EXTI9_5, EXTI15_10).
- Учитываются и другие обработчики, зарегистрированные на том же векторе.
- Драйвер сам лимит не увеличивает и своей таблицы диспетчеризации не создаёт. Чтобы изменить лимит, поправьте `irq_registry_settings.hpp` и пересоберите реестр вместе со всеми его пользователями (настройки через CMake нет).

**Правила для приложения:**

- Изменения конфигурации, реестра и общих регистров из `main` согласуйте с затронутыми прерываниями EXTI.
- Не вызывайте `SetUp()`/`DeInit()` из callback.
- Не меняйте напрямую настройку GPIO и маршрутизацию EXTI линии, у которой есть активный владелец.
- Объект должен существовать, пока он зарегистрирован.

---

## 6. Маскирование и освобождение

```cpp
button.IRQ_en(DISABLE); // Маскирует только эту линию; NVIC не трогается.
button.IRQ_en(ENABLE);  // Ожидающие события сохраняются и могут сработать сразу.
button.DeInit();        // Только из main; callback не вызывается.
```

**`IRQ_en()`** меняет только маску прерывания линии. GPIO, выбор фронтов, регистрация, флаги ожидания и общий вектор NVIC остаются как были. До `SetUp()` вызов ничего не делает.

**`DeInit()`**:

- маскирует линию, отключает детектор фронтов и сбрасывает её флаги ожидания;
- удаляет из реестра только *этот* объект и освобождает номер линии;
- если на векторе остались другие обработчики, восстанавливает их прежнее разрешение NVIC; если не осталось — оставляет вектор выключенным и сбрасывает его флаг ожидания в NVIC;
- сохраняет вход и подтяжку GPIO, маршрутизацию порта и общие такты;
- сохраняет привязку конструктора, так что `SetUp()` можно вызвать снова.

Деструктор **ничего не освобождает** — вызовите `DeInit()` перед уничтожением объекта.

**`EXTI_USE_ATOMIC`** (в `exti.hpp`, по умолчанию `0`): при значении `1` операции «прочитать-изменить-записать» над управляющими регистрами выполняются макросами ST `ATOMIC_*`. Флаги ожидания всегда сбрасываются прямой записью единицы (write-1-to-clear). Настройка не делает атомарными многошаговые операции и не отменяет правила из раздела 5.

---

## 7. Примеры использования

Полный самостоятельный пример. Проверьте, что вывод есть в вашем корпусе, и проверяйте каждый возвращаемый статус.

```cpp
#include "exti.hpp"

class Button : public EXTI_Line {
public:
    constexpr Button() : EXTI_Line(PIN(GPIOC_BASE, 13), Edge::Falling) {}
    volatile bool pressed = false;
protected:
    void OnInterrupt() override { pressed = true; }
};

constinit Button button;

int main()
{
    // ... успешная инициализация System/RCC ...
    if (button.SetUp(PIN::TYPE::INPUT_PullUp) != SysInitStatus::InitOK) {
        // обработка ошибки
    }
    for (;;) {
        if (button.pressed) {
            button.pressed = false;
            // реакция на кнопку
        }
    }
}
```

---

## 8. Угловые случаи

| Ситуация | Поведение |
| --- | --- |
| PA0 и PB0 оба хотят EXTI0 | Невозможно — один владелец на номер линии; второй `SetUp()` вернёт `InitError`. |
| Обработчиков на векторе больше `IRQ_MAX_SHARED` | `SetUp()` вернёт `InitError`. |
| Несколько фронтов подряд | Вызовов может быть меньше; не используйте как счётчик импульсов. |
| Callback хочет перенастроить линию | Нельзя — callback не меняет регистрацию и настройку. |
| `main` меняет общие регистры EXTI | Нужно согласовать с затронутыми прерываниями. |

---

## См. также

- [ST G0 LL EXTI](https://github.com/STMicroelectronics/stm32g0xx-hal-driver/blob/master/Inc/stm32g0xx_ll_exti.h): раскладка EXTICR, выбор порта, подтверждение через RPR1/FPR1.
- [ST F4 LL EXTI](https://github.com/STMicroelectronics/stm32f4xx-hal-driver/blob/master/Inc/stm32f4xx_ll_exti.h): маски, выбор фронтов и подтверждение через PR.
- [Initialization.md](Initialization.md)
- [GPIO.md](GPIO.md)
- [IRQ_Registry.md](IRQ_Registry.md)
- [System.md](System.md)
