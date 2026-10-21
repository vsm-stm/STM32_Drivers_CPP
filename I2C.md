# I2C — Detailed Description

*[English](#english) · [Русский](#русский)*

---

## English

## Table of Contents

1. [Why This Is Needed](#1-why-this-is-needed)
2. [Constant Initialization and Constructor Diagnostics](#2-constant-initialization-and-constructor-diagnostics)
3. [Setup and Compile-Time Timing](#3-setup-and-compile-time-timing)
4. [Transfer API and Status](#4-transfer-api-and-status)
5. [IRQ and Completion](#5-irq-and-completion)
6. [Callbacks Through Inheritance](#6-callbacks-through-inheritance)
7. [Cache and Noncacheable DMA Memory](#7-cache-and-noncacheable-dma-memory)
8. [Atomic Register Updates](#8-atomic-register-updates)
9. [DeInit and Bus Recovery](#9-deinit-and-bus-recovery)
10. [Usage Examples](#10-usage-examples)
11. [Edge Cases](#11-edge-cases)
12. [See Also](#see-also)

---

## 1. Why This Is Needed

`I2C` is an **I2C master** with 7-bit addressing. Every operation exists in three flavours: blocking, interrupt-driven and DMA.

| Family | Hardware block |
| --- | --- |
| F4 | I2C v1 (SR1/SR2, CCR/TRISE) |
| F7, G0 | I2C v2 (TIMINGR, NBYTES/RELOAD) |

The API is the same on all families; the hardware differences are handled inside.

**Not supported:** slave mode, and the separate FMPI2C1 block of some F4 devices.

---

## 2. Constant Initialization and Constructor Diagnostics

Declare the object at file scope with `constinit` (or as `static constinit` inside a function). Arguments:

- `I2C::_N::instance`;
- named SCL and SDA pins of that instance;
- optional TX and RX DMA: named routes `_N::DMA::...` on F4/F7, physical channel addresses `DMAx_Channely_BASE` on G0 (the request ID comes from the peripheral).

What happens with a wrong argument:

| Declaration | Result |
| --- | --- |
| `constinit` object | **Compilation error** with the reason text in the message. |
| Object without `constinit` | Compiles; at start-up the constructor stops in `System::DebugTrap(reason)`. |

Every check calls `STM32_CONFIG_ERROR("reason")`: at compile time a GCC error containing the reason, at run time `System::DebugTrap(text)`. `constinit` is required by the usage contract but not enforced.

`SetUp()` still checks hardware state and occupied resources. The object stores the instance reference and one register pointer; a null pointer means "not set up" (it is cleared after a failed first `SetUp()` and by `DeInit()`). Details: [Initialization.md](Initialization.md).

---

## 3. Setup and Compile-Time Timing

**One object owns one I2C master.** Pins and DMA are fixed by the constructor. With DMA configured, blocking and IRQ operations are still available.

```cpp
template<ClockSystem::Config clocks, SPEED speed = SPEED::STANDARD>
SysInitStatus SetUp();
```

**Pass the same `clocks` you gave to `ClockSystem::Init<clocks>()`.** System/RCC must be initialized first.

`SetUp()`:

- configures SCL/SDA as alternate-function open-drain with pull-up;
- uses PCLK as the I2C kernel clock;
- writes the precomputed timing;
- sets up the embedded DMA and registers the interrupts.

> The internal pull-ups are weak. Real buses need external pull-up resistors sized for the bus capacitance and speed.

A repeated `SetUp()` requires `DeInit()` first. On failure only what this call acquired (DMA, registrations) is rolled back; other handlers on shared vectors stay.

**Timing is computed at compile time.**

- APB1 comes from `ClockSystem::GetAPB1Clock<clocks>()`, which shares RCC's preparation and validation — no duplicated PLL/divider formulas.
- F4: CCR/TRISE/FREQ are precomputed. F7/G0: TIMINGR is precomputed.
- An invalid clock configuration, speed or timing is a **build error**.
- Before touching GPIO/registers/DMA, `SetUp()` compares this APB1 with `System::APB1BusClock` (the value published by RCC) and returns `InitError` on mismatch. This is a check of the nominal value, not a measurement — always check the RCC init result yourself.
- Changing RCC behind ClockSystem's back, or recalculating timing at run time, is not supported.

**Speeds:**

| Speed | Rate | Families |
| --- | --- | --- |
| `STANDARD` (default) | 100 kHz | F4, F7, G0 |
| `FAST` | 400 kHz | F4, F7, G0 |
| `FAST_PLUS` | 1 MHz | F7, G0 (with suitable clock and electrical conditions) |

**F7/G0 timing search** (`src/tools/i2c_timing_utility.hpp`) runs only in the compiler. The old 128-entry RAM table (2.5 KB) and the run-time search are gone. The algorithm keeps ST's ordering:

1. for each PRESC, take the first valid SCLDEL/SDADEL;
2. then choose the smallest period error (first match wins a tie);
3. impossible periods are skipped early.

Assumptions: analog filter on, digital filter (DNF) = 0, rise/fall times 1000/300 ns (STANDARD), 300/300 ns (FAST), 100/100 ns (FAST_PLUS). The result is the closest **allowed** timing, not necessarily the exact frequency — check the real signal edges on your board.

The old `SetUp(SPEED)` and `SetUp<pclk_hz, SPEED>()` were removed.

---

## 4. Transfer API and Status

| Operation | Blocking | IRQ | DMA |
| --- | --- | --- | --- |
| Write | `Send` | `Send_IRQ` | `Send_DMA` |
| Read | `Receive` | `Receive_IRQ` | `Receive_DMA` |
| Register write | `WriteReg` | `WriteReg_IRQ` | `WriteReg_DMA` |
| Register read | `ReadReg` | `ReadReg_IRQ` | `ReadReg_DMA` |

**Arguments:**

- `addr` — 7-bit address **not shifted** (0…0x7F).
- `len` — number of bytes, up to 65535. F7/G0 split it into ≤255-byte NBYTES chunks internally (RELOAD); your buffer length is unaffected.
- `REG::_8BIT` / `REG::_16BIT` — one or two register-address bytes, high byte first.
- `ReadReg` uses a repeated START between the register address and the read.

**Special cases:**

- `Send`/`Receive` with `len = 0` → `Error`.
- `WriteReg`/`WriteReg_IRQ` with `len = 0` sends only the register address (allowed). DMA variants require `len > 0`.
- `IsReady(addr)` — blocking address probe without data.

**Blocking calls** return `OK`, `Busy`, `NotInit`, `Error` or `Timeout`; `GetError()` gives the reason. They need a running `GetTick()`. On F4 the ACK/POS/ADDR/STOP handling of the last 1–3 bytes is sensitive to interrupt latency — long foreign ISRs can break it. The driver does not mask interrupts internally.

**Asynchronous calls (IRQ/DMA):**

- `OK` means **started**, not finished.
- Until completion the buffer belongs to the driver — do not modify, free or read it.
- Check `IsBusy()`/`GetError()` or use callbacks.
- **There is no internal timeout.** `main` must enforce its own deadline and call `Abort()` if needed.

**`Abort()`** (from `main`, with this object's IRQs not running): disables requests, confirms the DMA stop, resets the I2C block and sets `GetError()` to `Aborted`. No callback is called. Bindings and registrations stay.

---

## 5. IRQ and Completion

**Registration.** `SetUp()` registers the I2C event/error handler (one shared vector on G0) and separate TX/RX DMA handlers. Each DMA handler reads only its own stream/channel through `GetIRQFlags()` with the public `FLAG_TC`/`FLAG_ERRORS` masks; half-transfer is not used. A full registry or a duplicate registration → `InitError`. On G0, when TX and RX DMA share one vector, that needs **two** registry slots.

**Rules for the application:**

- Handlers of one I2C object must not preempt each other (give them the same priority).
- Serialize start/`Abort()`/`DeInit()` with these interrupts.
- Callbacks must not wait, run blocking I/O, call `Abort()`/`DeInit()` or change the registry.

**When is a transfer complete?**

| Family | Rule |
| --- | --- |
| F4 TX | after BTF and issuing STOP |
| F4 RX DMA | STOP is issued after DMA TC (for a 1-byte read — right after ADDR) |
| F7/G0 | **both** STOPF (bus phase done) **and** DMA TC are required |

- F4 does not wait for STOP to clear. A new start returns `Busy` while STOP/BUSY are still set.
- F7/G0: if STOPF arrives first, the object stays `Busy` until the DMA interrupt; if TC arrives first, the TC source is disabled and the flag kept until the bus finishes. Success additionally requires the DMA remaining count to be zero. For CPU transfers STOPF also checks that the register address and data were fully sent.

**Errors:**

- F4/F7: the driver requests a DMA stop and finishes on TC or TE with EN = 0.
- G0: the failed packet is discarded after the stop command; CNDTR is not interpreted as partial data.
- No waits for EN/STOP and no unbounded FIFO draining inside interrupts.

An incomplete transfer is never reported as success. If expected events never arrive, the object may stay `Busy` — the external deadline + `Abort()` cover this too.

---

## 6. Callbacks Through Inheritance

```cpp
class SensorBus : public I2C {
public:
    using I2C::I2C;
    volatile bool ready = false;
    volatile ERR error = ERR::None;
protected:
    void OnComplete() override { ready = true; }
    void OnError(ERR reason) override { error = reason; }
};
```

- `OnComplete()`/`OnError()` run in the I2C or DMA interrupt, **after** the state is updated and the buffer is handed back.
- After `OnError()` do not use the received data.
- Blocking methods never call these callbacks.
- The old `SetDoneCallback`/`SetErrorCallback` API was removed.

---

## 7. Cache and Noncacheable DMA Memory

Relevant on F7 (data cache); on F4/G0 the cache helpers are empty.

| Direction | Before DMA | After success |
| --- | --- | --- |
| TX | `System::CacheClean` | — |
| RX | `System::CacheCleanInvalidate` | `System::CacheInvalidate`, before the CPU reads |

- The application owns **every 32-byte cache line** the buffer touches, including padding. `len` does not have to be a multiple of 32.
- After an error the RX buffer is not invalidated and must not be read as partial data.
- Ranges entirely inside a configured noncacheable DMA region are skipped by System automatically.
- Project/MPU settings do not switch I2C implementations.

---

## 8. Atomic Register Updates

**`I2C_USE_ATOMIC`** (in `i2c.hpp`, default `0`):

| Value | Read-modify-write of control registers |
| --- | --- |
| `0` | plain `SET_BIT`/`CLEAR_BIT`/`MODIFY_REG` |
| `1` | ST `ATOMIC_*`: LDREX/STREX on F4/F7; short PRIMASK save/mask/restore on G0 |

- `DMA_USE_ATOMIC` controls the DMA CR/CCR independently.
- Rebuild the library and application after changing it — a `#define` only in `main.cpp` is not enough.
- One atomic RMW does not serialize a whole transfer, the state machine or buffer handover.
- ICR/SR, data registers and NBYTES/START commands are always written directly, as the hardware requires.

---

## 9. DeInit and Bus Recovery

**`DeInit()`** (from `main`, before destroying the object):

- stops the current operation;
- releases its DMA and IRQ registrations (other handlers on shared vectors stay);
- resets the I2C block and returns the pins to input.

Repeated `DeInit()` is allowed; afterwards `SetUp()` can be called again with the same bindings.

**Bus recovery is application code.** If a slave holds SDA low:

1. `DeInit()` the driver;
2. drive SCL/SDA as GPIO (e.g. clock pulses on SCL);
3. confirm the lines are released, with delays/deadlines suitable for your devices;
4. `SetUp()` again.

The driver has no built-in universal recovery routine (`Recover()` was removed).

---

## 10. Usage Examples

Setup (same `clocks` for RCC and I2C):

```cpp
#include "i2c.hpp"

constexpr ClockSystem::Config clocks{};   // HSI, all dividers /1
#if defined(STM32G0)
constexpr I2C::DMAReq tx = DMA1_Channel1_BASE;
constexpr I2C::DMAReq rx = DMA1_Channel2_BASE;
#else
constexpr I2C::DMAReq tx = I2C::_1::DMA::TX_DMA1_Stream6;
constexpr I2C::DMAReq rx = I2C::_1::DMA::RX_DMA1_Stream0;
#endif

constinit I2C bus(I2C::_1::instance, I2C::_1::SCL::PB8, I2C::_1::SDA::PB9, tx, rx);

// In main:
if (System::Init() != SysInitStatus::InitOK) return;
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK) return;
auto init = bus.SetUp<clocks, I2C::SPEED::FAST>();
```

Blocking register read (device 0x68, register 0x75, 100 ms timeout):

```cpp
uint8_t id = 0;
auto status = bus.ReadReg(0x68, 0x75, &id, 1, I2C::REG::_8BIT, 100);
```

DMA read with a deadline (from `main` only):

```cpp
if (bus.Receive_DMA(0x68, buffer, sizeof(buffer)) == SysStatus::OK) {
    const uint32_t started = System::GetTick();
    while (bus.IsBusy() && System::GetTick() - started < 100u) {}
    if (bus.IsBusy()) bus.Abort();
    // If GetError() != ERR::None, do not use the data.
}
```

Check every returned status.

---

## 11. Edge Cases

| Situation | Behaviour |
| --- | --- |
| IRQ/DMA transfer never completes | No internal timeout; `main` sets a deadline and calls `Abort()`. |
| RX ended with an error | Buffer content is invalid. |
| F4: new transfer right after the previous one | May return `Busy` until STOP/BUSY clear. |
| Slave holds the bus | Recovery is up to the application (section 9). |
| Pull-ups, noise, bus capacitance | Application/hardware responsibility. |
| `SetUp()` twice | Error; call `DeInit()` first. |

---

## See Also

- [RM0444, DMAMUX input assignment](https://www.st.com/resource/en/reference_manual/rm0444-stm32g0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf): I2C1 10/11, I2C2 12/13, I2C3 62/63.
- [RM0090, DMA1 request mapping](https://www.st.com/resource/en/reference_manual/rm0090-stm32f407-advanced-armbased-32bit-mcus-stmicroelectronics.pdf): I2C3 RX Stream2 CH3.
- [RM0431, DMA1 request mapping](https://www.st.com/resource/en/reference_manual/rm0431-stm32f72xxx-and-stm32f73xxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf): F72/F73 I2C1–3.
- [RM0385](https://www.st.com/resource/en/reference_manual/dm00124865.pdf), [RM0410](https://www.st.com/resource/en/reference_manual/rm0410-stm32f76xxx-and-stm32f77xxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf): I2C4 RX Stream2 CH2, TX Stream5 CH2.
- [Initialization.md](Initialization.md)
- [RCC.md](RCC.md)
- [DMA.md](DMA.md)
- [GPIO.md](GPIO.md)
- [IRQ_Registry.md](IRQ_Registry.md)

---

## Русский

## Содержание

1. [Зачем это нужно](#1-зачем-это-нужно)
2. [Постоянная инициализация и диагностика конструктора](#2-постоянная-инициализация-и-диагностика-конструктора)
3. [Модель работы](#3-модель-работы)
4. [Передачи](#4-передачи)
5. [IRQ и завершение](#5-irq-и-завершение)
6. [Callbacks через наследование](#6-callbacks-через-наследование)
7. [Кеш и некешируемая DMA-память](#7-кеш-и-некешируемая-dma-память)
8. [Атомарные изменения регистров](#8-атомарные-изменения-регистров)
9. [DeInit и восстановление шины](#9-deinit-и-восстановление-шины)
10. [Примеры использования](#10-примеры-использования)
11. [Угловые случаи](#11-угловые-случаи)
12. [См. также](#см-также)

---

## 1. Зачем это нужно

`I2C` — **ведущий (master) I2C** с 7-битной адресацией. Каждая операция есть в трёх вариантах: блокирующем, через прерывания и через DMA.

| Семейство | Аппаратный блок |
| --- | --- |
| F4 | I2C v1 (SR1/SR2, CCR/TRISE) |
| F7, G0 | I2C v2 (TIMINGR, NBYTES/RELOAD) |

API одинаков на всех семействах, различия аппаратуры учтены внутри.

**Не поддерживаются:** режим ведомого (slave) и отдельный блок FMPI2C1 некоторых F4.

---

## 2. Постоянная инициализация и диагностика конструктора

Объявляйте объект глобально с `constinit` (или как `static constinit` внутри функции). Аргументы:

- `I2C::_N::instance`;
- именованные выводы SCL и SDA этого экземпляра;
- необязательные TX и RX DMA: именованные маршруты `_N::DMA::...` на F4/F7, адреса физических каналов `DMAx_Channely_BASE` на G0 (номер запроса берётся из периферии).

Что происходит при неверном аргументе:

| Объявление | Результат |
| --- | --- |
| Объект с `constinit` | **Ошибка компиляции**, текст причины — в сообщении. |
| Объект без `constinit` | Собирается; при старте конструктор останавливается в `System::DebugTrap(причина)`. |

Каждая проверка вызывает `STM32_CONFIG_ERROR("причина")`: при компиляции — ошибка GCC с текстом причины, во время работы — `System::DebugTrap(text)`. `constinit` обязателен по правилам использования, но не проверяется.

`SetUp()` по-прежнему проверяет состояние аппаратуры и занятость ресурсов. Объект хранит ссылку на instance и один указатель регистров; нулевой указатель означает «не настроен» (обнуляется после неудачного первого `SetUp()` и в `DeInit()`). Подробнее: [Initialization.md](Initialization.md).

---

## 3. Модель работы

**Один объект управляет одним ведущим I2C.** Выводы и DMA закрепляются конструктором. При настроенном DMA блокирующие и IRQ-операции тоже доступны.

```cpp
constexpr ClockSystem::Config clocks{}; // HSI, делители шин /1.
#if defined(STM32G0)
constexpr I2C::DMAReq tx = DMA1_Channel1_BASE;
constexpr I2C::DMAReq rx = DMA1_Channel2_BASE;
#else
constexpr I2C::DMAReq tx = I2C::_1::DMA::TX_DMA1_Stream6;
constexpr I2C::DMAReq rx = I2C::_1::DMA::RX_DMA1_Stream0;
#endif

constinit I2C bus(I2C::_1::instance, I2C::_1::SCL::PB8, I2C::_1::SDA::PB9, tx, rx);
// В main: одна и та же конфигурация для RCC и I2C.
if (System::Init() != SysInitStatus::InitOK) return;
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK) return;
auto init = bus.SetUp<clocks, I2C::SPEED::FAST>();
```

- Без DMA последние два аргумента опускаются.
- На F4/F7 именованный `DMAReq` уже содержит поток/канал (CHSEL), периферию и направление; несовместимая привязка при `constinit` — ошибка компиляции.
- На G0 приложение выбирает физический канал, источник DMAMUX драйвер берёт из I2Cx и направления.
- Один ресурс DMA нельзя одновременно закрепить за TX и RX или за другим потребителем.

**`SetUp()`:**

- настраивает SCL/SDA как альтернативную функцию open-drain с подтяжкой;
- использует PCLK как тактирование ядра I2C;
- записывает заранее рассчитанные тайминги;
- настраивает встроенные DMA и регистрирует прерывания.

> Внутренние подтяжки слабые. Реальной шине нужны внешние резисторы, подобранные под её ёмкость и скорость.

| Скорость | Частота | Семейства |
| --- | --- | --- |
| `STANDARD` (по умолчанию) | 100 кГц | F4, F7, G0 |
| `FAST` | 400 кГц | F4, F7, G0 |
| `FAST_PLUS` | 1 МГц | F7, G0 (при подходящем тактировании и электрических параметрах) |

`InitError` возвращается при: повторной настройке, несовпадении APB1 с конфигурацией, отсутствующей периферии, занятом DMA или отказе регистрации IRQ. При отказе удаляются только записи, добавленные этим вызовом, и освобождаются захваченные им DMA. Проверяйте возвращённый статус.

### Расчёт во время компиляции

**Передавайте в `SetUp<clocks, SPEED>()` тот же `clocks`, что и в `ClockSystem::Init<clocks>()`.**

- APB1 берётся из `ClockSystem::GetAPB1Clock<clocks>()` — тот же расчёт и те же проверки, что у RCC; копий формул PLL/делителей в I2C нет.
- F4: заранее рассчитываются CCR/TRISE/FREQ. F7/G0: TIMINGR.
- Неверная конфигурация тактирования, скорость или тайминги — **ошибка сборки**.
- Без второго параметра выбирается STANDARD: `bus.SetUp<clocks>()`.
- Старые `SetUp(SPEED)` и `SetUp<pclk_hz, SPEED>()` удалены.

**Проверка при запуске.** До изменения регистров, GPIO, DMA и регистрации IRQ `SetUp()` сравнивает рассчитанную APB1 с `System::APB1BusClock`. Несовпадение → `InitError` без захвата ресурсов. Это сравнение с частотой, опубликованной RCC, а не измерение генератора. Изменение RCC в обход System/ClockSystem не поддерживается; результат System/RCC Init проверяйте до настройки I2C. Частота фиксирована на всё время работы I2C.

**Поиск таймингов F7/G0** (`src/tools/i2c_timing_utility.hpp`) выполняет только компилятор. Старая C-утилита, статический массив на 128 записей (2560 байт RAM) и поиск во время работы удалены. Порядок выбора ST сохранён:

1. для каждого PRESC берётся первый подходящий SCLDEL/SDADEL;
2. затем выбирается минимальная ошибка периода (при равенстве — первый вариант);
3. невозможные периоды отсекаются досрочно.

Допущения: аналоговый фильтр включён, цифровой фильтр (DNF) = 0, фронты нарастания/спада 1000/300 нс (STANDARD), 300/300 нс (FAST), 100/100 нс (FAST_PLUS). Выбирается ближайший **допустимый** вариант, точное совпадение частоты не гарантируется — проверьте реальные фронты на своей плате.

---

## 4. Передачи

| Операция | Блокирующая | Через IRQ | Через DMA |
| --- | --- | --- | --- |
| Запись | `Send` | `Send_IRQ` | `Send_DMA` |
| Чтение | `Receive` | `Receive_IRQ` | `Receive_DMA` |
| Запись регистра | `WriteReg` | `WriteReg_IRQ` | `WriteReg_DMA` |
| Чтение регистра | `ReadReg` | `ReadReg_IRQ` | `ReadReg_DMA` |

**Аргументы:**

- `addr` — 7-битный адрес **без сдвига** (0…0x7F).
- `len` — число байтов, до 65535. F7/G0 внутри разбивают его на порции NBYTES ≤ 255 (RELOAD); длина вашего буфера от этого не меняется.
- `REG::_8BIT` / `REG::_16BIT` — один или два байта адреса регистра, старший первым.
- `ReadReg` использует повторный START между адресом регистра и чтением.

**Особые случаи:**

- `Send`/`Receive` с `len = 0` → `Error`.
- `WriteReg`/`WriteReg_IRQ` с `len = 0` передаёт только адрес регистра (допустимо). DMA-варианты требуют `len > 0`.
- `IsReady(addr)` — блокирующая проба адреса без данных.

```cpp
uint8_t id = 0;
auto status = bus.ReadReg(0x68, 0x75, &id, 1, I2C::REG::_8BIT, 100);
```

**Блокирующие вызовы** возвращают `OK`, `Busy`, `NotInit`, `Error` или `Timeout`; причину уточняет `GetError()`. Нужен работающий `GetTick()`. Нельзя обращаться к этому I2C из другого контекста во время блокирующей операции. На F4 обработка ACK/POS/ADDR/STOP последних 1–3 байт чувствительна к задержке прерываний — длинные посторонние ISR нужно проверять на плате. Внутреннего маскирования прерываний нет.

**Асинхронные вызовы (IRQ/DMA):**

- `OK` означает **запуск**, а не завершение.
- До окончания буфер принадлежит драйверу — не меняйте, не освобождайте и не читайте его как готовый результат.
- Результат узнавайте через `IsBusy()`/`GetError()` или callback.
- **Встроенного таймаута нет.** Срок ограничивает `main`:

```cpp
if (bus.Receive_DMA(0x68, buffer, sizeof(buffer)) == SysStatus::OK) {
    const uint32_t started = System::GetTick();
    while (bus.IsBusy() && System::GetTick() - started < 100u) {}
    if (bus.IsBusy()) bus.Abort();
    // При GetError() != ERR::None данные не использовать.
}
```

**`Abort()`** вызывается из `main`, когда прерывания этого объекта не выполняются. Он отключает запросы, подтверждает остановку DMA и сбрасывает I2C; callback не вызывается, `GetError()` возвращает `Aborted`. Привязки и регистрации сохраняются.

---

## 5. IRQ и завершение

**Регистрация.** `SetUp()` регистрирует обработчик событий/ошибок I2C (на G0 — один общий вектор) и отдельные обработчики DMA TX/RX. Каждый DMA-обработчик читает только свой поток/канал через `GetIRQFlags()` с публичными масками `FLAG_TC`/`FLAG_ERRORS`; прерывание половины передачи (HT) не используется. Заполненный реестр или повторная регистрация → `InitError`. На G0 общий вектор для двух направлений требует **двух** мест в IRQ_Registry.

**Правила для приложения:**

- Обработчики одного I2C не должны вытеснять друг друга (дайте им одинаковый приоритет).
- Старт, `Abort()` и `DeInit()` согласуйте с этими прерываниями.
- В callback запрещены ожидания, блокирующие передачи, `Abort()`/`DeInit()` и изменение реестра IRQ.

**Когда передача завершена?**

| Семейство | Правило |
| --- | --- |
| F4 TX | после BTF и выдачи STOP |
| F4 RX DMA | STOP формируется после DMA TC (при чтении одного байта — сразу после ADDR) |
| F7/G0 | нужны **оба** события: STOPF (фаза шины окончена) **и** DMA TC |

- F4 не ждёт снятия STOP: следующий запуск возвращает `Busy`, пока STOP/BUSY ещё установлены.
- F7/G0: если STOPF пришёл первым, объект остаётся `Busy` до прерывания DMA; если первым пришёл TC, источник TC выключается, а флаг сохраняется до завершения шины. Для успеха остаток счётчика DMA обязан быть нулём. При передаче через CPU STOPF дополнительно проверяет, что адрес регистра и данные переданы полностью.

**Ошибки:**

- F4/F7: драйвер запрашивает остановку DMA и завершает передачу по TC или TE при EN = 0.
- G0: ошибочный пакет отбрасывается после команды остановки; CNDTR не используется как длина частичных данных.
- Внутри прерываний нет ожиданий EN/STOP и циклов опустошения FIFO.

Неполная передача никогда не публикуется как успешная. Если ожидаемые события не пришли, объект может остаться `Busy` — для этого и нужны внешний срок и `Abort()`.

---

## 6. Callbacks через наследование

```cpp
class SensorBus : public I2C {
public:
    using I2C::I2C;
    volatile bool ready = false;
    volatile ERR error = ERR::None;
protected:
    void OnComplete() override { ready = true; }
    void OnError(ERR reason) override { error = reason; }
};
```

- `OnComplete()`/`OnError()` вызываются из прерывания I2C или DMA **после** обновления состояния и освобождения буфера.
- После `OnError()` принятые данные не используйте.
- Блокирующие методы callback не вызывают.
- Старые `SetDoneCallback`/`SetErrorCallback` удалены.

---

## 7. Кеш и некешируемая DMA-память

Актуально для F7 (кеш данных); на F4/G0 методы кеша пустые.

| Направление | Перед DMA | После успеха |
| --- | --- | --- |
| TX | `System::CacheClean` | — |
| RX | `System::CacheCleanInvalidate` | `System::CacheInvalidate`, до чтения процессором |

- Приложение владеет **всеми 32-байтными кеш-линиями**, которые задевает буфер, включая выравнивающие байты. `len` не обязана быть кратной 32.
- После ошибки RX-буфер не инвалидируется и не считается пригодным для чтения частичных данных.
- Диапазон, целиком лежащий в настроенной некешируемой DMA-области, System пропускает автоматически.
- Настройки проекта/MPU не переключают реализацию I2C.

---

## 8. Атомарные изменения регистров

**`I2C_USE_ATOMIC`** (в `i2c.hpp`, по умолчанию `0`):

| Значение | «Прочитать-изменить-записать» управляющих регистров |
| --- | --- |
| `0` | обычные `SET_BIT`/`CLEAR_BIT`/`MODIFY_REG` |
| `1` | `ATOMIC_*` от ST: LDREX/STREX на F4/F7; краткое сохранение/маскирование/восстановление PRIMASK на G0 |

- `DMA_USE_ATOMIC` независимо управляет регистрами CR/CCR самого DMA.
- После изменения пересоберите драйверы и приложение — `#define` только в `main.cpp` недостаточно.
- Атомарность защищает одну операцию, а не всю передачу, состояние автомата или передачу буфера.
- ICR/SR, регистры данных и команды NBYTES/START всегда пишутся напрямую, как требует аппаратура.

---

## 9. DeInit и восстановление шины

**`DeInit()`** (из `main`, до уничтожения объекта):

- останавливает текущий обмен;
- освобождает свои DMA и записи IRQ (другие обработчики общего вектора сохраняются);
- сбрасывает блок I2C и переводит выводы во вход.

Повторный `DeInit()` допустим; после него можно снова вызвать `SetUp()` с теми же привязками.

**Восстановление шины — задача приложения.** Если ведомый удерживает SDA:

1. `DeInit()` драйвера;
2. управляйте SCL/SDA как GPIO (например, тактовые импульсы на SCL);
3. убедитесь, что линии отпущены, с задержками/сроками, подходящими вашим устройствам;
4. снова `SetUp()`.

Встроенной универсальной процедуры восстановления нет (`Recover()` удалён).

---

## 10. Примеры использования

Настройка (один и тот же `clocks` для RCC и I2C):

```cpp
#include "i2c.hpp"

constexpr ClockSystem::Config clocks{};   // HSI, все делители /1
#if defined(STM32G0)
constexpr I2C::DMAReq tx = DMA1_Channel1_BASE;
constexpr I2C::DMAReq rx = DMA1_Channel2_BASE;
#else
constexpr I2C::DMAReq tx = I2C::_1::DMA::TX_DMA1_Stream6;
constexpr I2C::DMAReq rx = I2C::_1::DMA::RX_DMA1_Stream0;
#endif

constinit I2C bus(I2C::_1::instance, I2C::_1::SCL::PB8, I2C::_1::SDA::PB9, tx, rx);

// В main:
if (System::Init() != SysInitStatus::InitOK) return;
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK) return;
auto init = bus.SetUp<clocks, I2C::SPEED::FAST>();
```

Блокирующее чтение регистра (устройство 0x68, регистр 0x75, таймаут 100 мс):

```cpp
uint8_t id = 0;
auto status = bus.ReadReg(0x68, 0x75, &id, 1, I2C::REG::_8BIT, 100);
```

Чтение через DMA со сроком (только из `main`):

```cpp
if (bus.Receive_DMA(0x68, buffer, sizeof(buffer)) == SysStatus::OK) {
    const uint32_t started = System::GetTick();
    while (bus.IsBusy() && System::GetTick() - started < 100u) {}
    if (bus.IsBusy()) bus.Abort();
    // При GetError() != ERR::None данные не использовать.
}
```

Проверяйте каждый возвращаемый статус.

---

## 11. Угловые случаи

| Ситуация | Поведение |
| --- | --- |
| IRQ/DMA-передача не завершается | Встроенного таймаута нет; `main` задаёт срок и вызывает `Abort()`. |
| Приём завершился ошибкой | Содержимое буфера недостоверно. |
| F4: новая передача сразу после предыдущей | Может вернуть `Busy`, пока не снимутся STOP/BUSY. |
| Ведомый удерживает шину | Восстановление — задача приложения (раздел 9). |
| Подтяжки, помехи, ёмкость шины | Ответственность приложения и схемы. |
| `SetUp()` дважды | Ошибка; сначала `DeInit()`. |

---

## См. также

- [RM0444, DMAMUX input assignment](https://www.st.com/resource/en/reference_manual/rm0444-stm32g0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf): I2C1 10/11, I2C2 12/13, I2C3 62/63.
- [RM0090, DMA1 request mapping](https://www.st.com/resource/en/reference_manual/rm0090-stm32f407-advanced-armbased-32bit-mcus-stmicroelectronics.pdf): I2C3 RX Stream2 CH3.
- [RM0431, DMA1 request mapping](https://www.st.com/resource/en/reference_manual/rm0431-stm32f72xxx-and-stm32f73xxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf): F72/F73 I2C1–3.
- [RM0385](https://www.st.com/resource/en/reference_manual/dm00124865.pdf), [RM0410](https://www.st.com/resource/en/reference_manual/rm0410-stm32f76xxx-and-stm32f77xxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf): I2C4 RX Stream2 CH2, TX Stream5 CH2.
- [Initialization.md](Initialization.md)
- [RCC.md](RCC.md)
- [DMA.md](DMA.md)
- [GPIO.md](GPIO.md)
- [IRQ_Registry.md](IRQ_Registry.md)
