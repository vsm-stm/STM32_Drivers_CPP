# ADC / ADC_N — Detailed Description

*[English](#english) · [Русский](#русский)*

---

## English

## Table of Contents

1. [Why This Is Needed](#1-why-this-is-needed)
2. [Constant Initialization and Constructor Diagnostics](#2-constant-initialization-and-constructor-diagnostics)
3. [API and Conversion Modes](#3-api-and-conversion-modes)
4. [Setup Order and Lifetime](#4-setup-order-and-lifetime)
5. [Channels and Sampling](#5-channels-and-sampling)
6. [IRQs and Inherited Callbacks](#6-irqs-and-inherited-callbacks)
7. [Timer and External Trigger Binding](#7-timer-and-external-trigger-binding)
8. [HT/TC and Cache Ownership](#8-httc-and-cache-ownership)
9. [Errors, Stop and DeInit](#9-errors-stop-and-deinit)
10. [Migration](#10-migration)
11. [Usage Examples](#11-usage-examples)
12. [Edge Cases](#12-edge-cases)
13. [See Also](#see-also)

---

## 1. Why This Is Needed

`ADC_N` drives one ADC block. **Everything about "how" is fixed in the constructor:**

- conversion mode — `Single` or `Continuous`;
- optional DMA;
- optional trigger — a timer or an external EXTI line.

After that the workflow is always the same: `SetUp()` → `AddChannel()` → `Start()`. A single `Start()` method runs whatever mode the constructor selected. The ADC and its DMA have separate interrupt handlers.

---

## 2. Constant Initialization and Constructor Diagnostics

Declare the object at file scope with `constinit` (or as `static constinit` inside a function). Arguments: `ADC_N::_N::instance`, mode, optional DMA (named `_N::DMA::...` route on F4/F7, physical `DMAx_Channely_BASE` on G0 — the ADC supplies the request ID), optional `TIM*` and trigger.

What happens with a wrong argument (e.g. `Continuous` together with a trigger):

| Declaration | Result |
| --- | --- |
| `constinit` object | **Compilation error** with the reason text in the message. |
| Object without `constinit` | Compiles; at start-up the constructor stops in `System::DebugTrap(reason)`. |

Every check calls `STM32_CONFIG_ERROR("reason")`: at compile time a GCC error containing the reason, at run time `System::DebugTrap(text)`. `constinit` is required by the usage contract but not enforced.

`SetUp()` still checks hardware state and occupied resources. The object keeps the instance reference plus one register pointer; null means "not set up" (cleared after a failed first `SetUp()` and by `DeInit()`). Details: [Initialization.md](Initialization.md).

---

## 3. API and Conversion Modes

```cpp
ADC_N(const Instance& instance, Conversion conversion,
      DMAReq dma = {}, TIM* timer = nullptr,
      Trigger source = Trigger::None,
      System::CachePolicy cache_policy = System::CachePolicy::Cacheable);
SysStatus Start(uint16_t* buf = nullptr, uint32_t len = 0);
```

Order: **constructor → `SetUp()` → `AddChannel()` → optional `IRQ_en()` → `Start()`**.

What `Start()` does depends on the constructor:

| Constructor | `Start()` behaviour |
| --- | --- |
| `Single`, no DMA, no trigger | one sequence; read the result via DR/`GetData()` |
| `Single` + DMA, no trigger | one sequence into a finite DMA buffer |
| `Continuous`, no DMA | repeating conversions; DR holds only the latest result |
| `Continuous` + DMA | repeating sequences, circular DMA |
| `Single` + trigger | one sequence per trigger event; circular DMA if DMA is bound |
| `Continuous` + any trigger | **rejected** at `constinit` construction |

**Rules:**

- With DMA bound, `Start(buf, len)` always uses DMA and needs a buffer and length.
- Without DMA, call `Start()`; passing a buffer/length → `Error`.
- After a finite capture, just call `Start()` again — no new `SetUp()`/`AddChannel()`.
- While a capture is running, another `Start()` or a channel change → `Busy`.
- `Stop()` keeps bindings and channels. A second `SetUp()` requires `DeInit()`.
- There is no `AttachDMA`/`AttachTrig`/`AttachExtTrig`, `Start_DMA`, `StartCont` or any other way to change bindings at run time. Copy/move are disabled.

**`ADC_USE_ATOMIC`** (in `adc.hpp`) and **`DMA_USE_ATOMIC`** independently choose plain (`0`, default) or ST `ATOMIC_*` control-register updates. Rebuild driver and application after a change. They do not serialize whole `main`/IRQ operations.

---

## 4. Setup Order and Lifetime

**Calling context:**

- Initialize System/RCC first.
- Call `SetUp()`, `AddChannel()`/`ClearChannels()`, `Start()`, `Stop()` and `DeInit()` one after another from `main`.
- SysTick must run. G0 hardware handshakes have a 2-tick budget — about 1–2 ms with a 1 ms tick, depending on when in the tick the wait starts.
- Interrupt handlers never wait; `Stop()`/`DeInit()` in `main` may wait for the hardware to stop.

**Serialization:**

- Serialize control calls with this object's ADC/DMA interrupts, and do not let these handlers preempt each other.
- There are no internal locks, thread checks or `Process()` method. `IsBusy()`/`GetStatus()` are pure queries.
- Callbacks only notify `main`. They must not reconfigure or restart the ADC, or change the IRQ registry. Disabling a source with `IRQ_en()` is allowed.

**Lifetime:** the object, the trigger timer and the active buffer must stay alive until the capture ends and registrations are removed.

**Embedded DMA:** `SetUp()` acquires and configures it (half-words, peripheral → memory, memory increment, ADC DR address, circular according to the fixed mode). Do not reconfigure, release or hand that DMA to another user before `DeInit()`.

---

## 5. Channels and Sampling

**Pins** are `ADC_PIN` descriptors, checked at compile time:

| Family | Example |
| --- | --- |
| F4/F7 | `ADC_N::_1::PA0_IN0`, `ADC_N::_3::PF6_IN4` |
| G0 | `ADC_N::IN::PA0_IN0` |

Whether the pin is bonded in your package and the exact hardware route are for the application to check.

**Sequence:** the order of `AddChannel()` calls is the order of results.

| Family | Sequence capability |
| --- | --- |
| F4/F7 | up to 16 ranks, any order, repeats allowed |
| G0 | up to 8 ordered ranks for inputs 0…14; a 9th rank or an input above 14 switches to bitmask mode — then channels must be strictly ascending without repeats |

A physical input used more than once must use the same sampling time each time.

**ADC clock and sampling:**

| Family | ADC clock | Sampling time |
| --- | --- | --- |
| F4/F7 | PCLK2 / 4 | per channel (`CH_SMPL`) |
| G0 | PCLK / 2 | `SetUp()` defines SMP1/SMP2; each `AddChannel()` picks one of them |

On F4/F7 the ADC prescaler (ADCPRE) is **shared** by ADC1/2/3 even in independent mode: configure all ADCs before starting any. No common/multi-ADC manager is provided.

**Internal channels** VREFINT/TEMP/VBAT are added as `INTERNAL` inputs on ADC1.

- Choose the sampling time from the datasheet and the ADC clock — the short default for external inputs may be too short.
- `Delay_ms(1)` on enabling an internal source covers its start-up, not the sampling time of each conversion. Sampling is not chosen automatically.
- Where TEMP and VBAT share one input, they cannot both be added.

---

## 6. IRQs and Inherited Callbacks

**Registration.** `SetUp()` registers the ADC and its embedded DMA separately — no manual registration needed. The ADC handler reads only ADC flags; the DMA handler only its own stream/channel. If setup fails, only what this call acquired is released; other handlers on shared vectors stay.

**`IRQ_en()`** changes sources only (NVIC and registrations are untouched):

| Source | Behaviour |
| --- | --- |
| `EOC`/`EOS` | F4/F7: two names for one source, calls only `OnEOS()`; G0: separate events |
| `OVR` | overrun reported through `OnError()` |
| `DMA_HT` | off by default; enable while idle, **before** `Start()`, to get `OnDmaHalf()` |
| `DMA_TC` | on by default; cannot be disabled for a finite capture |

- Changing `DMA_HT` while `Busy` is ignored (the half layout is fixed at `Start()`).
- In circular mode `DMA_TC` gates the completion notifications.
- DMA transfer error (TE) and F4/F7 direct-mode error (DME) are enabled at setup and re-armed after an error. DMA calls with `Enable_IRQ(..., false)` keep the caller's NVIC mask.

**Callbacks** are overridden in a derived class:

```cpp
class MyADC : public ADC_N {
public:
    using ADC_N::ADC_N;
    volatile bool ready = false;
    volatile uint32_t errors = 0;
protected:
    void OnDmaComplete() override { ready = true; }
    void OnDmaHalf() override { /* first half is ready before it is overwritten */ }
    void OnError(uint32_t causes) override { errors = causes; }
    void OnEOS() override { /* end of sequence, if that IRQ is enabled */ }
};
```

- G0 also has `OnEOC()`.
- You do not need to call the base callbacks for the operation to complete.
- Do not call `GetData()` while DMA owns DR — reading DR can steal a DMA sample.
- Without DMA, DR holds one result, not a scan buffer; scans and `Continuous` can overwrite an unread result.

---

## 7. Timer and External Trigger Binding

**The application prepares the trigger source:**

| Trigger | Preparation |
| --- | --- |
| Timer TRGO | `tim.SetUp(freq)` then `tim.SetUpTrigger()` |
| Timer CCx | `tim.SetCompareIRQ(ch, compare, nullptr)` |
| EXTI11 | `timer = nullptr`; the application configures the EXTI line/edge routing (rising edge) |

- The ADC does **not** choose the timer frequency, compare value, TRGO or EXTI routing.
- The timer must match the selected `ADC_N::Trigger`; the constructor checks only that a timer pointer is present (or absent) as required, not the hardware route.
- A timer that is not set up → `InitError` from the ADC `SetUp()`.

**Start/stop order:** `Start()` arms DMA and the ADC **before** starting the timer. `Stop()` and errors stop that timer — do not restart it yourself during a capture.

EXTSEL values are family-specific and listed in `enum Trigger`. TRGO2 is not supported by the current TIM API; G0 TIM1 is used through CC4.

---

## 8. HT/TC and Cache Ownership

**DMA buffer policy.** The last constructor argument is `System::CachePolicy`, fixed for the object. `Cacheable` is the default and accepts any valid DMA buffer, including non-cacheable memory; the F7 driver performs its normal cache operations without detecting the memory region. `NonCacheable` skips every cache call for every ADC DMA buffer and each completed half. The application must then place **every** buffer used by that object entirely in DTCM or non-cacheable MPU memory; a cacheable buffer in this mode is a caller error. The policy does not place buffers or configure MPU. F4/G0 perform no cache operations with either value. DMA completion and buffer ownership requirements remain unchanged.

F7 example (supply only non-cacheable buffers to this object):

```cpp
using A = ADC_N::_1;
constinit ADC_N adc(A::instance, ADC_N::Conversion::Continuous,
    A::DMA::RX_DMA2_Stream0, nullptr, ADC_N::Trigger::None,
    System::CachePolicy::NonCacheable);
```

**Cache operations around DMA** (F7; empty on F4/G0):

| Moment | Operation |
| --- | --- |
| before `Start()` | `System::CacheCleanInvalidate` on the buffer |
| finite capture, TC | the whole buffer is invalidated and returned |
| circular, HT | first half invalidated, then `OnDmaHalf()` |
| circular, TC | second half invalidated (or the whole buffer if HT is off), then `OnDmaComplete()` |

A finite capture may also use HT, but the final TC still returns all samples.

`GetIRQFlags()` reads HT, TC and error flags in one access; `FLAG_HT`/`FLAG_TC`/`FLAG_ERRORS` decode them. HT and TC are cleared separately.

> **HT/TC do not pause circular DMA.** They are deadlines, not locks. Choose sample rate, sampling time and buffer size so each half is processed before it is overwritten. Missed rotations are not detected or recovered. With a trigger, also leave enough time for each conversion.

**F7 buffer layout:**

- The application owns every 32-byte cache line the buffer touches, including padding.
- With HT, each half of a cacheable buffer must start on a line boundary and be a whole number of lines. Example: `alignas(32) uint16_t samples[32]` = two halves of 32 bytes.
- A finite capture without HT may have any length.
- The buffer must be reachable by DMA.
- `NonCacheable` skips cache calls for all samples and halves; no cache-line separation is needed for truly non-cacheable buffers. HT still requires an even sample count.

---

## 9. Errors, Stop and DeInit

`OnError()` receives a mask of `Error::Overrun` and/or `Error::Dma`. Overrun is reported only when `OVR` is enabled; `Continuous` without DMA may silently drop unread results.

**On an error** conversions and the timer stop and a DMA stop is requested:

- **F4/F7:** stays `Busy` until the DMA interrupt confirms the stop (TC, or hardware TE with EN = 0); then `Error` and exactly one `OnError()`.
- **G0:** the packet is discarded without reading CNDTR as a partial length; `Error`/`OnError()` are published right after the stop command. No waiting in interrupts.

Error data is neither cache-invalidated nor meant to be read. No success callback follows a detected error. The next `Start()` from `main` re-arms the sources and handshakes.

**`Stop()`** (from `main`): stops timer/ADC, confirms the DMA stop, keeps the configuration and calls no callbacks. An earlier error is still reported in its return status.

**`DeInit()`** (from `main`):

- removes only this ADC's registrations, releases the DMA and disables the ADC;
- stops the bound timer but does not destroy or reconfigure it;
- keeps analog GPIO configuration and shared DMA/GPIO clocks;
- keeps the constructor parameters for a later `SetUp()`.

The destructor does nothing to the hardware. No separate "DMA owned" flag is stored: handlers use the fixed DMA selection; cleanup uses `GetIRQn()` (−1 if never acquired) and a safe `Release()` — also after a DMA setup that failed before acquiring the resource.

---

## 10. Migration

| Previous API | Current API |
| --- | --- |
| `ADC_N(ADCx)`, mode chosen at start | `ADC_N(_N::instance, Conversion, dma, timer, Trigger)` |
| `AttachDMA`/`AttachTrig`/`AttachExtTrig` | constructor bindings; DMA configured in `SetUp()` |
| `StartCont`/`StartCont_DMA`/`StartTrig_DMA` | fixed policy and one `Start()` |
| `Start_DMA(buf)` | `Start(buf, GetChannelCount())` |
| `TIM_TriggerGenerator` | common `TIM*` plus `ADC_N::Trigger` |
| shared ADC/DMA `HandleIRQ` | two separately registered handlers |
| unused `OnDmaHalf` | explicit `IRQ_en(DMA_HT, ENABLE)` |

Trigger routes follow the official ST LL ADC headers: [F4](https://github.com/STMicroelectronics/stm32f4xx-hal-driver/blob/master/Inc/stm32f4xx_ll_adc.h), [F7](https://github.com/STMicroelectronics/stm32f7xx-hal-driver/blob/master/Inc/stm32f7xx_ll_adc.h), [G0](https://github.com/STMicroelectronics/stm32g0xx-hal-driver/blob/master/Inc/stm32g0xx_ll_adc.h).

---

## 11. Usage Examples

The three examples are independent — do not give the same DMA to several active objects.

### Reading without DMA

```cpp
constinit ADC_N input(ADC_N::_1::instance, ADC_N::Conversion::Single);

if (input.SetUp() == SysInitStatus::InitOK) {
#if defined(STM32G0)
    auto channel = input.AddChannel(ADC_N::IN::PA0_IN0);
#else
    auto channel = input.AddChannel(ADC_N::_1::PA0_IN0);
#endif
    if (channel == SysStatus::OK) {
        auto started = input.Start();   // check OK
        // Later in main: if (!input.IsBusy()) read input.GetData().
    }
}
```

### One-shot DMA, two channels

```cpp
#if defined(STM32G0)
constexpr ADC_N::DMAReq dma = DMA1_Channel1_BASE;
#else
constexpr ADC_N::DMAReq dma = ADC_N::_1::DMA::RX_DMA2_Stream0;
#endif
constinit MyADC adc(ADC_N::_1::instance, ADC_N::Conversion::Single, dma);
alignas(32) static uint16_t samples[16];   // a whole cache line; 2 samples used

if (adc.SetUp() == SysInitStatus::InitOK) {
#if defined(STM32G0)
    auto a = adc.AddChannel(ADC_N::IN::PA0_IN0);
    auto b = adc.AddChannel(ADC_N::IN::PB0_IN8);
#else
    auto a = adc.AddChannel(ADC_N::_1::PA0_IN0);
    auto b = adc.AddChannel(ADC_N::_1::PB0_IN8);
#endif
    if (a == SysStatus::OK && b == SysStatus::OK) {
        adc.ready = false;
        auto started = adc.Start(samples, 2);   // check OK before waiting
    }
}
// Main: when ready, use samples[0..1], clear ready, Start(samples, 2) again.
// Handle errors via `errors`/GetStatus() — do not wait only for `ready`.
```

### Timer-triggered circular DMA with half-buffer notifications

Uses TIM3 on F4/G0 and TIM2 on F7. Pick a timer your part actually has (F410 has no TIM3 — use e.g. `TIM1_CC1` with `SetCompareIRQ(CH1, compare, nullptr)`).

```cpp
#if defined(STM32G0)
constexpr ADC_N::DMAReq timed_dma = DMA1_Channel1_BASE;
#else
constexpr ADC_N::DMAReq timed_dma = ADC_N::_1::DMA::RX_DMA2_Stream0;
#endif
#if defined(STM32F7)
constinit TIM sampling(TIM::_2::instance);
constinit MyADC timed(ADC_N::_1::instance, ADC_N::Conversion::Single, timed_dma, &sampling,
                      ADC_N::Trigger::TIM2_TRGO);
#else
constinit TIM sampling(TIM::_3::instance);
constinit MyADC timed(ADC_N::_1::instance, ADC_N::Conversion::Single, timed_dma, &sampling,
                      ADC_N::Trigger::TIM3_TRGO);
#endif
alignas(32) static uint16_t circular[32];   // two 32-byte halves

if (sampling.SetUp(1000) == SysInitStatus::InitOK          // 1 kHz trigger
    && sampling.SetUpTrigger() == SysInitStatus::InitOK
    && timed.SetUp() == SysInitStatus::InitOK) {
#if defined(STM32G0)
    auto channel = timed.AddChannel(ADC_N::IN::PA0_IN0);
#else
    auto channel = timed.AddChannel(ADC_N::_1::PA0_IN0);
#endif
    if (channel == SysStatus::OK) {
        timed.IRQ_en(ADC_N::IRQ::DMA_HT, ENABLE);
        auto started = timed.Start(circular, 32);
    }
}
```

**Buffer length rules for `Start(buf, len)`:**

- `buf` must be non-null and `uint16_t`-aligned.
- Single without trigger: `len` must equal `GetChannelCount()`.
- Other DMA modes: `len` in 1…65535 and a multiple of the channel count.
- With HT enabled, `len` must also be even.

Check every `SetUp()`/`AddChannel()`/`Start()` result.

---

## 12. Edge Cases

| Situation | Behaviour |
| --- | --- |
| Reading several channels without DMA | DR keeps only the last result — use DMA for scans. |
| `Continuous` + external trigger | Rejected at construction. |
| Circular data not processed in time | Overwritten silently; HT/TC are deadlines, not locks. |
| Capture ended with an error | Treat all data as invalid. |
| G0 configuration timeout | Retry only after a successful `ClearChannels()` or `DeInit()` + `SetUp()`. |
| Pin bonding, exact sampling time | Application responsibility. |

---

## See Also

- [Initialization.md](Initialization.md)
- [DMA.md](DMA.md)
- [TIM.md](TIM.md)
- [GPIO.md](GPIO.md)
- [System.md](System.md)

---

## Русский

## Содержание

1. [Зачем это нужно](#1-зачем-это-нужно)
2. [Постоянная инициализация и диагностика конструктора](#2-постоянная-инициализация-и-диагностика-конструктора)
3. [Текущий API](#3-текущий-api)
4. [Порядок работы и время жизни](#4-порядок-работы-и-время-жизни)
5. [Каналы и sampling](#5-каналы-и-sampling)
6. [IRQ и callbacks](#6-irq-и-callbacks)
7. [Таймер и внешние события](#7-таймер-и-внешние-события)
8. [HT/TC и кеш](#8-httc-и-кеш)
9. [Ошибки и освобождение](#9-ошибки-и-освобождение)
10. [Миграция](#10-миграция)
11. [Примеры использования](#11-примеры-использования)
12. [Угловые случаи](#12-угловые-случаи)
13. [См. также](#см-также)

---

## 1. Зачем это нужно

`ADC_N` управляет одним блоком АЦП. **Всё, что касается «как», задаётся в конструкторе:**

- режим преобразования — `Single` или `Continuous`;
- необязательный DMA;
- необязательный триггер — таймер или внешняя линия EXTI.

Дальше порядок всегда одинаков: `SetUp()` → `AddChannel()` → `Start()`. Один метод `Start()` запускает тот режим, который выбран конструктором. У АЦП и его DMA отдельные обработчики прерываний.

---

## 2. Постоянная инициализация и диагностика конструктора

Объявляйте объект глобально с `constinit` (или как `static constinit` внутри функции). Аргументы: `ADC_N::_N::instance`, режим, необязательный DMA (именованный маршрут `_N::DMA::...` на F4/F7, физический `DMAx_Channely_BASE` на G0 — номер запроса подставляет АЦП), необязательные `TIM*` и триггер.

Что происходит при неверном аргументе (например, `Continuous` вместе с триггером):

| Объявление | Результат |
| --- | --- |
| Объект с `constinit` | **Ошибка компиляции**, текст причины — в сообщении. |
| Объект без `constinit` | Собирается; при старте конструктор останавливается в `System::DebugTrap(причина)`. |

Каждая проверка вызывает `STM32_CONFIG_ERROR("причина")`: при компиляции — ошибка GCC с текстом причины, во время работы — `System::DebugTrap(text)`. `constinit` обязателен по правилам использования, но не проверяется.

`SetUp()` по-прежнему проверяет состояние аппаратуры и занятость ресурсов. Объект хранит ссылку на instance и один указатель регистров; нулевой указатель — «не настроен» (обнуляется после неудачного первого `SetUp()` и в `DeInit()`). Подробнее: [Initialization.md](Initialization.md).

---

## 3. Текущий API

```cpp
ADC_N(const Instance& instance, Conversion conversion,
      DMAReq dma = {}, TIM* timer = nullptr,
      Trigger source = Trigger::None,
      System::CachePolicy cache_policy = System::CachePolicy::Cacheable);

SysStatus Start(uint16_t* buf = nullptr, uint32_t len = 0);
```

Порядок: **конструктор → `SetUp()` → `AddChannel()` → при необходимости `IRQ_en()` → `Start()`**.

АЦП содержит собственный `DMA_Sx`. На F4/F7 передаётся именованный `DMAReq`, на G0 — только физический канал DMA; источник запроса АЦП выбирается автоматически.

Что делает `Start()` в зависимости от конструктора:

| Конструктор | Поведение `Start()` |
| --- | --- |
| `Single` без DMA и триггера | одна последовательность; результат через DR/`GetData()` |
| `Single` + DMA без триггера | одна последовательность в конечный буфер DMA |
| `Continuous` без DMA | непрерывные преобразования; DR хранит только последний результат |
| `Continuous` + DMA | непрерывные последовательности, кольцевой DMA |
| `Single` + триггер | одна последовательность на каждое событие; с DMA — кольцевой |
| `Continuous` + любой триггер | **отклоняется** при `constinit`-конструировании |

**Правила:**

- Если DMA привязан, `Start(buf, len)` всегда использует его и требует буфер и длину.
- Без DMA вызывайте `Start()`; переданные буфер/длина → `Error`.
- После конечного захвата просто снова вызовите `Start()` — без повторных `SetUp()`/`AddChannel()`.
- Во время захвата новый `Start()` или изменение каналов → `Busy`.
- `Stop()` сохраняет привязки и каналы. Повторный `SetUp()` — только после `DeInit()`.
- `AttachDMA`/`AttachTrig`/`AttachExtTrig`, `Start_DMA`/`StartCont`/`StartCont_DMA`/`StartTrig_DMA` удалены; способов сменить привязки во время работы нет. Копирование и перемещение запрещены.

**`ADC_USE_ATOMIC`** (в `adc.hpp`) и **`DMA_USE_ATOMIC`** независимо выбирают обычное (`0`, по умолчанию) или `ATOMIC_*` от ST изменение управляющих регистров. После изменения пересоберите драйвер и приложение. Целые операции `main`/IRQ они не согласуют.

---

## 4. Порядок работы и время жизни

**Контекст вызова:**

- System/RCC настраиваются заранее.
- `SetUp()`, `AddChannel()`/`ClearChannels()`, `Start()`, `Stop()` и `DeInit()` вызываются последовательно из `main`.
- Нужен работающий SysTick. Аппаратные ожидания на G0 ограничены 2 тиками — при тике 1 мс это примерно 1–2 мс в зависимости от фазы начала ожидания.
- В прерываниях ожиданий нет; `Stop()`/`DeInit()` из `main` могут ждать остановки аппаратуры.

**Согласование:**

- Управляющие вызовы согласуйте с прерываниями ADC/DMA этого объекта; эти обработчики не должны вытеснять друг друга.
- Внутренних блокировок, проверок PRIMASK/IPSR, потоков и метода `Process()` нет. `IsBusy()`/`GetStatus()` — чистые запросы.
- Callback только уведомляют `main` и не перенастраивают/не перезапускают АЦП и не меняют реестр IRQ. Отключение источника через `IRQ_en()` допустимо.

**Время жизни:** объекты ADC/TIM и активный буфер должны существовать до завершения захвата и снятия регистраций.

**Встроенный DMA:** `SetUp()` захватывает и настраивает его (полуслова, периферия → память, инкремент адреса памяти, адрес DR, кольцевой режим по фиксированной политике). До `DeInit()` АЦП этот DMA нельзя перенастраивать, освобождать или передавать другому потребителю.

---

## 5. Каналы и sampling

**Выводы** — описания `ADC_PIN`, проверяемые при компиляции:

| Семейство | Пример |
| --- | --- |
| F4/F7 | `ADC_N::_1::PA0_IN0`, `ADC_N::_3::PF6_IN4` |
| G0 | `ADC_N::IN::PA0_IN0` |

Наличие вывода в корпусе и точный аппаратный маршрут проверяет приложение.

**Последовательность:** порядок вызовов `AddChannel()` — это порядок результатов.

| Семейство | Возможности последовательности |
| --- | --- |
| F4/F7 | до 16 позиций, любой порядок, повторы разрешены |
| G0 | до 8 упорядоченных позиций для входов 0…14; девятая позиция или вход выше 14 включает режим битовой маски — тогда каналы должны идти строго по возрастанию без повторов |

Физический вход, использованный несколько раз, должен везде иметь одинаковое время выборки.

**Тактирование АЦП и время выборки:**

| Семейство | Частота АЦП | Время выборки |
| --- | --- | --- |
| F4/F7 | PCLK2 / 4 | для каждого канала (`CH_SMPL`) |
| G0 | PCLK / 2 | `SetUp()` задаёт SMP1/SMP2, каждый `AddChannel()` выбирает одно из них |

На F4/F7 предделитель АЦП (ADCPRE) **общий** для ADC1/2/3 даже в независимом режиме: сначала настройте все АЦП, потом запускайте любой. Менеджера общего/многоканального режима нет.

**Внутренние каналы** VREFINT/TEMP/VBAT добавляются как входы `INTERNAL` на ADC1.

- Время выборки выбирайте по datasheet и частоте АЦП — короткое значение по умолчанию для внешних входов может не подойти.
- `Delay_ms(1)` при включении внутреннего источника покрывает его запуск, а не время выборки каждого отсчёта. Автоподбора времени выборки нет.
- Если TEMP и VBAT используют один вход, добавить оба нельзя.

---

## 6. IRQ и callbacks

**Регистрация.** `SetUp()` отдельно регистрирует АЦП и его встроенный DMA — ручная регистрация не нужна. Обработчик АЦП читает только флаги АЦП, обработчик DMA — только своего потока/канала. При ошибке настройки освобождается только захваченное этим вызовом; чужие обработчики общих векторов остаются.

**`IRQ_en()`** меняет только источники (NVIC и регистрации не трогаются):

| Источник | Поведение |
| --- | --- |
| `EOC`/`EOS` | F4/F7: два имени одного источника, вызывается только `OnEOS()`; G0: отдельные события |
| `OVR` | переполнение сообщается через `OnError()` |
| `DMA_HT` | по умолчанию выключен; включите в простое, **до** `Start()`, чтобы получать `OnDmaHalf()` |
| `DMA_TC` | по умолчанию включён; для конечного захвата отключить нельзя |

- Изменение `DMA_HT` при `Busy` игнорируется (разбиение на половины фиксируется в `Start()`).
- В кольцевом режиме `DMA_TC` управляет уведомлениями о завершении.
- Ошибки DMA (TE) и, на F4/F7, ошибка прямого режима (DME) включаются при настройке и снова разрешаются после ошибки. Вызовы `Enable_IRQ(..., false)` сохраняют маску NVIC вызывающего кода.

**Callback** переопределяются в наследнике:

```cpp
class MyADC : public ADC_N {
public:
    using ADC_N::ADC_N;
    volatile bool ready = false;
    volatile uint32_t errors = 0;
protected:
    void OnDmaComplete() override { ready = true; }
    void OnDmaHalf() override { /* первая половина готова до её перезаписи */ }
    void OnError(uint32_t causes) override { errors = causes; }
    void OnEOS() override { /* конец последовательности, если включён IRQ */ }
};
```

- На G0 есть также `OnEOC()`.
- Вызывать базовые callback для завершения операции не нужно.
- Не вызывайте `GetData()`, пока DR принадлежит DMA — чтение DR может забрать отсчёт DMA.
- Без DMA DR хранит один результат, а не буфер сканирования; сканирование и `Continuous` могут перезаписать непрочитанный результат.

---

## 7. Таймер и внешние события

**Источник триггера готовит приложение:**

| Триггер | Подготовка |
| --- | --- |
| TRGO таймера | `tim.SetUp(freq)`, затем `tim.SetUpTrigger()` |
| CCx таймера | `tim.SetCompareIRQ(ch, compare, nullptr)` |
| EXTI11 | `timer = nullptr`; маршрутизацию линии EXTI и фронт (нарастающий) настраивает приложение |

- АЦП **не** задаёт частоту таймера, значение сравнения, TRGO или маршрутизацию EXTI.
- Таймер должен соответствовать выбранному `ADC_N::Trigger`; конструктор проверяет только наличие (или отсутствие) указателя на таймер, а не аппаратный маршрут. Источник-таймер без TIM, либо TIM вместе с `None`/`EXTI11` отклоняются при `constinit`.
- Ненастроенный таймер → `InitError` в `SetUp()` АЦП.

**Порядок запуска и остановки:** `Start()` готовит DMA и АЦП **до** запуска таймера. `Stop()` и ошибки останавливают этот таймер — не перезапускайте его самостоятельно во время захвата.

Значения EXTSEL зависят от семейства и перечислены в `enum Trigger`. TRGO2 текущий API TIM не поддерживает; TIM1 на G0 используется через CC4.

---

## 8. HT/TC и кеш

**Политика DMA-буферов.** Последний аргумент конструктора — `System::CachePolicy`, неизменный для объекта. По умолчанию `Cacheable`: можно передавать любой допустимый DMA-буфер, включая некешируемую память; на F7 драйвер выполняет обычное обслуживание кеша без определения области памяти. `NonCacheable` пропускает все вызовы обслуживания кеша для каждого DMA-буфера ADC и каждой завершённой половины. В этом режиме приложение обязано размещать **все** используемые объектом буферы целиком в DTCM или некешируемой памяти MPU; передача кешируемого буфера — ошибка вызывающего кода. Политика не размещает буферы и не настраивает MPU. На F4/G0 при обоих значениях операций с кешем нет. Требования к завершению DMA и владению буфером сохраняются.

Пример F7 (этому объекту передаются только некешируемые буферы):

```cpp
using A = ADC_N::_1;
constinit ADC_N adc(A::instance, ADC_N::Conversion::Continuous,
    A::DMA::RX_DMA2_Stream0, nullptr, ADC_N::Trigger::None,
    System::CachePolicy::NonCacheable);
```

**Операции с кешем вокруг DMA** (F7; на F4/G0 пустые):

| Момент | Операция |
| --- | --- |
| перед `Start()` | `System::CacheCleanInvalidate` для буфера |
| конечный захват, TC | весь буфер инвалидируется и возвращается |
| кольцевой, HT | инвалидируется первая половина, затем `OnDmaHalf()` |
| кольцевой, TC | инвалидируется вторая половина (или весь буфер, если HT выключен), затем `OnDmaComplete()` |

Конечный захват тоже может использовать HT, но итоговый TC всё равно возвращает все отсчёты.

`GetIRQFlags()` читает HT, TC и флаги ошибок одним обращением; разбор — масками `DMA_Sx::FLAG_HT`/`FLAG_TC`/`FLAG_ERRORS`. HT и TC сбрасываются раздельно (`ClearHT_Flag`/`ClearTC_Flag`).

> **HT/TC не приостанавливают кольцевой DMA.** Это сроки, а не блокировки. Подберите частоту, время выборки и размер буфера так, чтобы каждая половина обрабатывалась до перезаписи. Пропущенные обороты не обнаруживаются и не восстанавливаются. С триггером также оставьте достаточно времени на каждое преобразование.

**Буфер на F7:**

- Приложение владеет всеми 32-байтными кеш-линиями, которые задевает буфер, включая выравнивающие байты.
- С HT каждая половина кешируемого буфера должна начинаться на границе линии и занимать целое число линий. Пример: `alignas(32) uint16_t samples[32]` = две половины по 32 байта. Драйвер буферы не выделяет и не копирует.
- Конечный захват без HT может иметь любую длину.
- Буфер должен быть доступен DMA.
- `NonCacheable` пропускает вызовы кеша для всех отсчётов и половин; для действительно некешируемых буферов разделение по кеш-линиям не требуется. Для HT по-прежнему нужна чётная длина.

---

## 9. Ошибки и освобождение

`OnError()` получает маску `Error::Overrun` и/или `Error::Dma`. Переполнение сообщается, только если включён `OVR`; `Continuous` без DMA может молча терять непрочитанные результаты.

**При ошибке** преобразования и таймер останавливаются, запрашивается остановка DMA:

- **F4/F7:** состояние `Busy` сохраняется, пока прерывание DMA не подтвердит остановку (TC или аппаратный TE при EN = 0); затем `Error` и ровно один `OnError()`.
- **G0:** пакет отбрасывается без чтения CNDTR как длины частичных данных; `Error`/`OnError()` публикуются сразу после команды остановки. Ожиданий в прерываниях нет.

Ошибочные данные не инвалидируются в кеше и не предназначены для чтения. После обнаруженной ошибки «успешные» callback не вызываются. Следующий `Start()` из `main` снова разрешает источники и выполняет нужные handshake.

**`Stop()`** (из `main`): останавливает таймер/АЦП, подтверждает остановку DMA, сохраняет настройки и callback не вызывает. Ранее зафиксированная ошибка по-прежнему возвращается в его статусе.

**`DeInit()`** (из `main`):

- удаляет только регистрации этого АЦП, освобождает DMA и выключает АЦП;
- останавливает привязанный таймер, но не уничтожает и не перенастраивает его;
- сохраняет аналоговую настройку GPIO и общее тактирование DMA/GPIO;
- сохраняет параметры конструктора для последующего `SetUp()`.

Деструктор аппаратуру не трогает. Отдельного флага «DMA захвачен» нет: обработчики используют закреплённый выбор DMA; освобождение — через `GetIRQn()` (−1, если не захвачен) и безопасный `Release()`, в том числе после неудачной настройки DMA без захваченного ресурса.

---

## 10. Миграция

| Раньше | Сейчас |
| --- | --- |
| `ADC_N(ADCx)`, режим при запуске | `ADC_N(_N::instance, Conversion, dma, timer, Trigger)` |
| `AttachDMA`/`AttachTrig`/`AttachExtTrig` | привязки в конструкторе; DMA настраивается в `SetUp()` |
| `StartCont`/`StartCont_DMA`/`StartTrig_DMA` | фиксированная политика и один `Start()` |
| `Start_DMA(buf)` | `Start(buf, GetChannelCount())` |
| `TIM_TriggerGenerator` | общий `TIM*` и `ADC_N::Trigger` |
| общий `HandleIRQ` ADC/DMA | два отдельно зарегистрированных обработчика |
| неиспользуемый `OnDmaHalf` | явное `IRQ_en(DMA_HT, ENABLE)` |

Маршруты триггеров сверены с официальными заголовками ST LL ADC: [F4](https://github.com/STMicroelectronics/stm32f4xx-hal-driver/blob/master/Inc/stm32f4xx_ll_adc.h), [F7](https://github.com/STMicroelectronics/stm32f7xx-hal-driver/blob/master/Inc/stm32f7xx_ll_adc.h), [G0](https://github.com/STMicroelectronics/stm32g0xx-hal-driver/blob/master/Inc/stm32g0xx_ll_adc.h).

---

## 11. Примеры использования

Три примера независимы — не отдавайте один DMA нескольким активным объектам.

### Чтение без DMA

```cpp
constinit ADC_N input(ADC_N::_1::instance, ADC_N::Conversion::Single);

if (input.SetUp() == SysInitStatus::InitOK) {
#if defined(STM32G0)
    auto channel = input.AddChannel(ADC_N::IN::PA0_IN0);
#else
    auto channel = input.AddChannel(ADC_N::_1::PA0_IN0);
#endif
    if (channel == SysStatus::OK) {
        auto started = input.Start(); // Проверить OK.
        // Позже из main: если !input.IsBusy(), прочитать input.GetData().
    }
}
```

### Однократный DMA, два канала

```cpp
#if defined(STM32G0)
constexpr ADC_N::DMAReq dma = DMA1_Channel1_BASE;
#else
constexpr ADC_N::DMAReq dma = ADC_N::_1::DMA::RX_DMA2_Stream0;
#endif
constinit MyADC adc(ADC_N::_1::instance, ADC_N::Conversion::Single, dma);
alignas(32) static uint16_t samples[16]; // Целая кеш-линия; используются 2 отсчёта.

if (adc.SetUp() == SysInitStatus::InitOK) {
#if defined(STM32G0)
    auto a = adc.AddChannel(ADC_N::IN::PA0_IN0);
    auto b = adc.AddChannel(ADC_N::IN::PB0_IN8);
#else
    auto a = adc.AddChannel(ADC_N::_1::PA0_IN0);
    auto b = adc.AddChannel(ADC_N::_1::PB0_IN8);
#endif
    if (a == SysStatus::OK && b == SysStatus::OK) {
        adc.ready = false;
        auto started = adc.Start(samples, 2); // Проверить OK перед ожиданием.
    }
}
// Main: после ready обработать samples[0..1], сбросить ready и снова Start(samples, 2).
// Ошибку обрабатывать через errors/GetStatus(), а не ждать только ready.
```

После конечного TC обслуживается кеш, публикуется `OK` и вызывается `OnDmaComplete()`.

### Кольцевой DMA по таймеру с уведомлениями о половинах

Используется TIM3 на F4/G0 и TIM2 на F7. Выберите таймер, который реально есть на вашем МК (у F410 нет TIM3 — используйте, например, `TIM1_CC1` с `SetCompareIRQ(CH1, compare, nullptr)`).

```cpp
#if defined(STM32G0)
constexpr ADC_N::DMAReq timed_dma = DMA1_Channel1_BASE;
#else
constexpr ADC_N::DMAReq timed_dma = ADC_N::_1::DMA::RX_DMA2_Stream0;
#endif
#if defined(STM32F7)
constinit TIM sampling(TIM::_2::instance);
constinit MyADC timed(ADC_N::_1::instance, ADC_N::Conversion::Single, timed_dma, &sampling,
                      ADC_N::Trigger::TIM2_TRGO);
#else
constinit TIM sampling(TIM::_3::instance);
constinit MyADC timed(ADC_N::_1::instance, ADC_N::Conversion::Single, timed_dma, &sampling,
                      ADC_N::Trigger::TIM3_TRGO);
#endif
alignas(32) static uint16_t circular[32]; // Две половины по 32 байта.

if (sampling.SetUp(1000) == SysInitStatus::InitOK          // триггер 1 кГц
    && sampling.SetUpTrigger() == SysInitStatus::InitOK
    && timed.SetUp() == SysInitStatus::InitOK) {
#if defined(STM32G0)
    auto channel = timed.AddChannel(ADC_N::IN::PA0_IN0);
#else
    auto channel = timed.AddChannel(ADC_N::_1::PA0_IN0);
#endif
    if (channel == SysStatus::OK) {
        timed.IRQ_en(ADC_N::IRQ::DMA_HT, ENABLE);
        auto started = timed.Start(circular, 32); // Проверить OK.
    }
}
```

**Правила длины для `Start(buf, len)`:**

- `buf` ненулевой и выровнен под `uint16_t`.
- Single без триггера: `len` равна `GetChannelCount()`.
- Остальные режимы DMA: `len` в диапазоне 1…65535 и кратна числу каналов.
- При включённом HT `len` ещё и чётная, иначе `Error`.

Проверяйте результат каждого `SetUp()`/`AddChannel()`/`Start()`.

---

## 12. Угловые случаи

| Ситуация | Поведение |
| --- | --- |
| Чтение нескольких каналов без DMA | DR хранит только последний результат — для сканирования используйте DMA. |
| `Continuous` + внешний триггер | Отклоняется при конструировании. |
| Кольцевые данные не обработаны вовремя | Молча перезаписываются; HT/TC — сроки, а не блокировки. |
| Захват завершился ошибкой | Все данные считайте недостоверными. |
| Таймаут настройки на G0 | Повтор только после успешного `ClearChannels()` или `DeInit()` + `SetUp()`. |
| Наличие вывода, точное время выборки | Ответственность приложения. |
| Ожидания и `DeInit()` | Только из `main`. |

---

## См. также

- [Initialization.md](Initialization.md)
- [DMA.md](DMA.md)
- [TIM.md](TIM.md)
- [GPIO.md](GPIO.md)
- [System.md](System.md)
