# TIM — Detailed Description

*[English](#english) · [Русский](#русский)*

---

## English

## Table of Contents

1. [Why This Is Needed](#1-why-this-is-needed)
2. [Constant Initialization and Constructor Diagnostics](#2-constant-initialization-and-constructor-diagnostics)
3. [One timer and its channel resources](#3-one-timer-and-its-channel-resources)
4. [Common setup and units](#4-common-setup-and-units)
5. [PWM and exact 0/100%](#5-pwm-and-exact-0100)
6. [Capture and pin-free compare](#6-capture-and-pin-free-compare)
7. [Hardware triggers](#7-hardware-triggers)
8. [Independent PWM DMA](#8-independent-pwm-dma)
9. [WS2812B through the common TIM](#9-ws2812b-through-the-common-tim)
10. [IRQ and teardown](#10-irq-and-teardown)
11. [Additional Modes and Integration](#11-additional-modes-and-integration)
12. [Usage Examples](#12-usage-examples)
13. [Edge Cases](#13-edge-cases)
14. [See Also](#see-also)

---

## 1. Why This Is Needed

`TIM` is **one class for everything a timer can do**: periodic interrupt, PWM, input capture, compare events, ADC trigger, PWM with DMA, encoder, pulse series and more.

- One object owns one hardware timer — its counter base (PSC/ARR) and its four channels.
- `SetUp()` configures the common base; functions like `SetUpPWM()`/`SetUpCapture()` give each channel a role.
- All channels share one counter, so they share frequency and period.
- "Whole-timer" modes (encoder, pulse burst, …) take the entire timer.
- Channel pins, DMA and an optional second timer are fixed in the constructor.

---

## 2. Constant Initialization and Constructor Diagnostics

Declare the object at file scope with `constinit` (or as `static constinit` inside a function). Arguments: `TIM::_N::instance`, up to four `{pin, dma}` channel bindings, and optionally a second `TIM*`. DMA is a named `_N::DMA::...` route on F4/F7 or a physical `DMAx_Channely_BASE` on G0 (the timer supplies the request ID).

What happens with a wrong argument (e.g. a pin of TIM2 on TIM3, or the same channel twice):

| Declaration | Result |
| --- | --- |
| `constinit` object | **Compilation error** with the reason text in the message. |
| Object without `constinit` | Compiles; at start-up the constructor stops in `System::DebugTrap(reason)`. |

Every check calls `STM32_CONFIG_ERROR("reason")`: at compile time a GCC error containing the reason, at run time `System::DebugTrap(text)`. `constinit` is required by the usage contract but not enforced.

`SetUp()` still checks hardware state and occupied resources. The object keeps the instance reference plus one register pointer; null means "not set up" (cleared after a failed first `SetUp()` and by `DeInit()`). Details: [Initialization.md](Initialization.md).

---

## 3. One timer and its channel resources

**Constructor:**

```cpp
constinit TIM timer(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7});
```

- Up to four `{pin, dma}` bindings, **in any order**. A named pin `TIM::_N::CHx::Pyz` already knows its channel number, GPIO/AF and timer.
- `TIM_PIN` (consteval) checks the numbers; the TIM constructor checks timer ownership and duplicate channels.
- Package bonding and the exact device AF route remain the application's checks.
- Internally each channel stores only port/pin/AF and DMA; the channel number is the array index and the timer address comes from the instance. `DmaOwned(i)` is simply `_dma_status[i] != NotInit`.

**Second timer** (after the four channel bindings, before cache policy) — used by finite quadrature generation:

```cpp
constinit TIM counter(TIM::_2::instance);
constinit TIM generator(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7}, {}, {}, &counter);
```

- The pointer is fixed at construction. Declare `counter` first and keep it alive while `generator` uses it.
- `Init()` checks the TRGO/ITR route before touching hardware or registering IRQs and returns `InitError` on a bad pairing.
- `SetUpQuadrature()` initializes both timers; `generator.DeInit()` releases both. Do not use `counter` on its own while the generator owns it (see section 11).

**Roles and lifetime:**

- There are no per-channel objects, no `GetOutput()` handles and no `AttachDMA()`.
- Each channel's role is set once by its setup function; a second setup of the same channel is rejected (even with a null callback).
- `DeInit()` clears roles and acquired resources but keeps constructor bindings.
- TIM owns all four DMA objects; keep the TIM alive. The destructor does no teardown.
- Configuration and registry changes belong to `main`, never to callbacks.

**`TIM_USE_ATOMIC`** (in `tim.hpp`, default `0`): `1` switches control/RCC read-modify-write to ST `ATOMIC_*` macros. `DMA_USE_ATOMIC` is separate. Neither serializes a full operation or replaces the IRQ-masking rules below. Rebuild TIM and its users after changing it.

**Legacy code:** the old `TIM_PeriodicIRQ`/`TIM_PWM`/`InputCapture` and composite classes remain as comments at the end of `tim.hpp`/`tim.cpp`. They are not compiled and are not available as compatibility classes.

---

## 4. Common setup and units

```cpp
SysInitStatus SetUp(uint32_t freq, void (*callback)() = nullptr, uint32_t period = 0);
```

| Argument | Meaning |
| --- | --- |
| `freq` | requested **Update/PWM frequency** in Hz — not the counter tick rate |
| `period` | counter ticks per period; ARR receives `period − 1`. `0` = choose the largest period that fits, with the smallest prescaler. Capture/counter modes need an explicit period |
| `callback` | optional Update notification; `nullptr` leaves the Update interrupt off |

**Calculation** (32-bit integer division):

```text
ratio        = timer_clock / freq
PSC_divisor  = ratio / period          (explicit period)
CNT_clock    = timer_clock / PSC_divisor
actual_Update = CNT_clock / period
```

Results are rounded down, so the actual frequency can differ — read `GetCounterClock()` and `GetPeriod()`.

**Limits:** 16-bit counter — period 1…65536; 32-bit counter — 1…UINT32_MAX (PWM has a stricter limit, section 5). Invalid frequency/dividers/period or registration failure → `InitError`. A second `SetUp()` → `InitError` until `DeInit()`.

**Loading:** PSC/ARR are loaded with a service update (UG) while the timer is stopped; URS suppresses the IRQ/DMA request from that UG. Configure TRGO and arm the ADC **after** `SetUp()`, so hardware consumers do not see setup events as samples. Channel setup functions do not generate a global UG. **`Start()` is always explicit** — setup never starts the timer.

```cpp
void Tick();
constinit TIM tick(TIM::_3::instance);
if (tick.SetUp(1000, &Tick) == SysInitStatus::InitOK) tick.Start();   // 1 kHz Update IRQ
```

---

## 5. PWM and exact 0/100%

```cpp
constinit TIM pwm(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7});
if (pwm.SetUp(1000, nullptr, 1000) == SysInitStatus::InitOK) {   // 1 kHz, 1000 ticks
    // Check both InitOK results before Start.
    pwm.SetUpPWM(TIM::Channel::CH1, 250);   // 25 %
    pwm.SetUpPWM(TIM::Channel::CH2, 500);   // 50 %
    pwm.Start();
}
pwm.SetDuty(TIM::Channel::CH1, 75);         // check SysStatus
```

**`SetUpPWM(channel, initial_ccr = 0, dma_callback = nullptr)`** (timer stopped):

- PWM mode 1, normal polarity, CCR preload, configures the bound pin;
- touches only this channel; other channel modes stay intact;
- enables MOE on advanced timers;
- loads the initial CCR without a global update.

**Why the 16-bit PWM period is at most 65535 ticks (ARR ≤ 65534):** so that `CCR = period` fits, giving exact **100 %**; `CCR = 0` gives exact **0 %**. No forced-active mode is needed. For 32-bit PWM the limit is UINT32_MAX. If an automatic base chose 65536 ticks, reconfigure with an explicit valid period via `DeInit()` + `SetUp()`.

**Changing the duty:**

| Method | Input | Notes |
| --- | --- | --- |
| `SetDuty(ch, percent)` | 0…100 % (clamped to 100) | arithmetic avoids `period × percent` overflow and wide division |
| `SetCCR(ch, ticks)` | 0…period raw ticks | |

Both return `NotInit`, `Error`, `Busy` (DMA owns this CCR) or `OK`. Values apply through preload at the next Update — no immediate phase change.

---

## 6. Capture and pin-free compare

All channels share PSC/ARR — they can have different roles but not different counter frequencies.

**Input capture:**

```cpp
void Edge();
constinit TIM input(TIM::_3::instance, {TIM::_3::CH1::PA6});
if (input.SetUp(100, nullptr, 10000) == SysInitStatus::InitOK) {
    // Target CNT clock is 1 MHz; read GetCounterClock() for the actual value.
    if (input.SetUpCapture(TIM::Channel::CH1, &Edge) == SysInitStatus::InitOK)
        input.Start();
}
// In Edge(): input.GetCapture(TIM::Channel::CH1).
```

- Direct TIx input, rising edge, no filter, no input prescaler. Needs an explicit base period.
- `GetCapture()` always reads the hardware CCR; that read also clears CCIF.
- The interrupt only acknowledges enabled events and calls the callback — it does not copy CCR. A new capture may overwrite the value before you read it.
- Counter wraps, overcapture and conversion to time units are up to the application.
- Capture with DMA is not supported — do not bind DMA to a capture channel.

**Compare without a pin:**

```cpp
constinit TIM tick(TIM::_3::instance);
if (tick.SetUp(1000, nullptr, 1000) == SysInitStatus::InitOK) {
    tick.SetCompareIRQ(TIM::Channel::CH1, 500, &HalfTick);   // check InitOK
    tick.Start();
}
```

- No GPIO; frozen output-compare mode; compare value 0…ARR.
- A `nullptr` callback configures only the hardware compare event, without an interrupt — the channel is still owned. This is how a direct ADC CCx trigger is prepared.

---

## 7. Hardware triggers

**`SetUpTrigger(TIM::Trigger::...)`** selects the TRGO output for other timers/ADC — no timer interrupt involved.

- One-time configuration while stopped; the timer must support master mode.
- Available: `Update`, `ComparePulse` (CH1 event; CH1 must be configured), `OC1Ref`…`OC4Ref` (that channel must be configured as PWM).

```cpp
constinit TIM sampling(TIM::_3::instance);
if (sampling.SetUp(1000) == SysInitStatus::InitOK) {
    sampling.SetUpTrigger(TIM::Trigger::Update);   // check InitOK
    // Arm the ADC first, then start the timer.
}
```

**ADC integration:**

- A direct CCx event needs no TRGO: set up a compare and select that source in the ADC.
- The ADC (not TIM) holds the EXTSEL tables. The ADC constructor takes `TIM*` and the source.
- Configure TIM before the ADC `SetUp()`. ADC `Start()` arms capture first, then starts this timer; ADC stop/error stops it.
- There is no separate `TIM_TriggerGenerator`. See [ADC.md](ADC.md).

---

## 8. Independent PWM DMA

**DMA buffer policy.** The last constructor argument is `System::CachePolicy`, fixed for the object. `Cacheable` is the default and accepts any valid DMA buffer, including non-cacheable memory; the F7 driver performs its normal cache operations without detecting the memory region. `NonCacheable` skips every cache call for all TIM DMA channels. The application must then place **every** buffer used by that object entirely in DTCM or non-cacheable MPU memory; a cacheable buffer in this mode is a caller error. The policy does not place buffers or configure MPU. F4/G0 perform no cache operations with either value. DMA completion and buffer ownership requirements remain unchanged.

F7 example (supply only non-cacheable buffers to this object):

```cpp
using T = TIM::_1;
constinit TIM pwm(T::instance,
    {T::CH1::PA8, T::DMA::CH1_DMA2_Stream1}, {}, {}, {}, nullptr,
    System::CachePolicy::NonCacheable);
```

PWM DMA streams a buffer of CCR values into one channel — one value per PWM period (waveforms, LED protocols…).

**Binding:** the DMA is passed in the constructor; TIM configures it in `SetUpPWM()`. The route is checked against both timer and channel at construction. If channel setup fails, only that channel's resources are released.

**Hardware mechanism:**

- Each PWM DMA uses its own **TIMx_CHx** request with `CR2.CCDS = 1` and its own `DIER.CCxDE` gate. UDE is not used.
- With CCDS = 1 the requests occur at Update.
- CCDS is common to the whole timer, so channels cannot independently choose Update vs Compare DMA timing.
- Several active streams must not serve the same request.

F4/F7 — names show controller and stream:

```cpp
constexpr TIM::DMAReq dma1 = TIM::_1::DMA::CH1_DMA2_Stream1;
constexpr TIM::DMAReq dma2 = TIM::_1::DMA::CH2_DMA2_Stream2;
constinit TIM waveform(TIM::_1::instance, {TIM::_1::CH1::PA8, dma1}, {TIM::_1::CH2::PA9, dma2});
```

G0 — physical channels:

```cpp
constexpr TIM::DMAReq dma1 = DMA1_Channel2_BASE;
constexpr TIM::DMAReq dma2 = DMA1_Channel3_BASE;
constinit TIM waveform(TIM::_1::instance, {TIM::_1::CH1::PA8, dma1}, {TIM::_1::CH2::PA9, dma2});
```

**Sequence:** `SetUp()` once → `SetUpPWM()` per channel → `SendDMA()` per buffer while stopped → `Start()`.

- Each channel has its own `GetDMAStatus()` and optional callback (set in `SetUpPWM()`).
- **TC means the DMA has read the buffer — not that the last PWM period has ended.** The last CCR value is applied at a later Update.
- A failed channel does not stop other channels or the timer.

**Buffers:**

| Timer | Buffer type | DMA width |
| --- | --- | --- |
| 16-bit | `uint16_t` | Half_Word |
| 32-bit TIM2/TIM5 | `uint32_t` overload | Word |

- Length 1…65535 elements; a mismatched overload → `Error` (no conversion or per-element checks).
- Keep the data unchanged until the status leaves `Busy` (also while an error stop completes). Use DMA-accessible RAM.
- F7: with `Cacheable`, System cleans the source (`CacheClean`); `NonCacheable` skips it for all channels, without range detection. TX needs no invalidate. F4/G0 compile out cache calls.

**Hardware limitations:** F4/F7 TIM4 has no CH4 DMA request. TIM2 CH2/CH4 share hardware request routing and cannot work independently with CCDS = 1 — that combination is rejected. Timer DMA burst mode is not used.

**Interrupt rules:**

- Mask this timer's affected timer/DMA interrupts around `main`-side DIER writes — including `SendDMA()` and `IRQ_en()`. Stopping the timer does not mask pending interrupts. The driver has no PRIMASK manipulation or locks.
- Give this object's interrupts priorities that prevent them from preempting each other.
- `SetUpPWM()` enables DMA sources and NVIC during initialization; `SendDMA()` re-arms TE/DME via `Enable_IRQ(..., false)`, keeping your NVIC mask.

---

## 9. WS2812B through the common TIM

```cpp
constexpr uint32_t period = 80;                    // example: 64 MHz CNT clock / 800 kHz
constexpr TIM::DMAReq dma = DMA1_Channel3_BASE;    // G0
constinit TIM ws(TIM::_1::instance, {TIM::_1::CH1::PA8, dma});
WS2812B_Strip<8> strip(52, 26);                    // CCR for "1" and "0" bits
strip.SetAll(RGB::Red());

if (ws.SetUp(800000, nullptr, period) == SysInitStatus::InitOK
    && ws.SetUpPWM(TIM::Channel::CH1) == SysInitStatus::InitOK
    && ws.SendDMA(TIM::Channel::CH1, strip.Data(), strip.Len()) == SysStatus::OK) {
    ws.Start();
}
// In main, do not modify strip while ws.IsDMABusy(TIM::Channel::CH1).
```

- `period` is ticks, not the ARR register value: `period = 80` → ARR = 79.
- Choose the CCR values and reset-trailer length for your real clock and LED datasheet.
- The colour encoder is unchanged; `GetOutput()`/`AttachDMA()` are no longer needed.
- DMA TC releases the buffer; the physical end and the reset interval are produced by the waveform itself.

---

## 10. IRQ and teardown

**Registration:**

- `SetUp()` registers the TIM on its Update/CC vectors (once if they share a vector).
- `SetUpPWM()` registers a separate DMA handler for each bound DMA, even on a shared vector. Each DMA handler looks only at its own resource; the timer interrupt does not scan DMA flags.

**Timer interrupt:** handles only enabled Update/CC events and leaves disabled (polled) flags alone. It takes no CCR snapshots — callbacks read hardware themselves (and reading CCR clears CCIF).

**Rules:**

- Callbacks must not change the registry or configuration.
- Timer/DMA handlers of the same object must not preempt each other.
- Mask the affected interrupts around `main`-side DIER updates; there are no internal locks.
- Interrupts never wait for hardware.
- A DMA error stops only that channel's DMA and publishes `Error` after the stop; the channel callback can inspect `GetDMAStatus()`. No automatic retry.

**`DeInit()`** (from `main`): disables timer requests, stops and releases acquired DMA, removes only its own registrations, resets/disables this timer and clears callbacks/roles. Shared handlers, NVIC and DMA controller clocks stay. Constructor bindings survive for a later setup. It may wait for a DMA stop and never calls callbacks.

---

## 11. Additional Modes and Integration

### Additional TIM modes

All modes below are functions of the same `TIM` class and use the constructor's channel bindings.

- "Whole-timer" initialization **replaces** the ordinary `SetUp()`; the configuration is fixed until `DeInit()`.
- Callbacks are optional and are passed only at initialization.
- No interrupt waits for hardware.
- External trigger input (ETR) is not implemented yet.

### Encoder

```cpp
constinit TIM encoder(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7});
if (encoder.SetUpEncoder(65536, TIM::EncoderMode::X4) == SysInitStatus::InitOK)
    encoder.Start();
const uint32_t position = encoder.Count();
```

- Hardware encoder input exists only on CH1/CH2; both pins required, no DMA; occupies the whole timer.
- `period` is explicit: ARR = period − 1, PSC = 0.
- `X2_CH1`/`X2_CH2` count both edges of that input; `X4` counts both edges of both inputs.
- `Count()` returns raw CNT with hardware wrap — no sign conversion or software extension. No interrupt or callback needed. See [RM0431, TIMx_SMCR](https://www.st.com/resource/en/reference_manual/dm00305990.pdf).

### Quadrature generation

Generates an exact number of A/B quadrature cycles (e.g. to simulate an encoder), counted by a second timer in hardware.

```cpp
void Done();   // optional; called from the second TIM's IRQ after both timers stop
constinit TIM counter(TIM::_2::instance);
constinit TIM generator(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7}, {}, {}, &counter);

if (generator.SetUpQuadrature(TIM::Channel::CH1, TIM::Channel::CH2,
        1000, 1000, &Done) == SysInitStatus::InitOK) {
    generator.GenPulse(100);    // check SysStatus; starts both timers
}
// In main: counter.Count() reads full A/B cycles directly from CNT.
// After stopping: generator.GenPulse(-100) reverses the phase order.
```

**Setup (`SetUpQuadrature(a, b, freq, period, done)`):**

- Replaces ordinary `SetUp()`; reserves the whole generator plus its constructor-bound counter.
- `a`/`b` — two different bound output channels, no DMA.
- `freq` — full A/B cycles per second; `period` — generator ticks per **half** cycle (even, ≥ 4).
- The two toggle channels compare at `floor(period/4)` and `floor(period/4) + period/2` — a quarter cycle apart.
- Integer divider rounding applies; the actual frequency can differ from the requested one.
- The counter is configured automatically: ITR/Update with PSC = 1, so two generator Updates = one full cycle in CNT. No per-cycle interrupt or software counting.

**`GenPulse(N)`:** resets both counters and prescaler phases, arms one compare, starts the counter, then the generator. The service UG is hidden from TRGO and not counted ([RM0090, MMS Enable/Update](https://www.st.com/resource/en/reference_manual/rm0090-stm32f407-advanced-armbased-32bit-mcus-stmicroelectronics.pdf), [RM0444, EGR.UG](https://www.st.com/resource/en/reference_manual/rm0444-stm32g0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)).

| N | Behaviour |
| --- | --- |
| > 0 | A leads B, N full cycles (A = the channel passed as `a`, not necessarily CH1) |
| < 0 | B leads A, \|N\| full cycles |
| 0 | a stopped pair stays unchanged |
| either timer running | `Busy` |
| \|N\| beyond the counter range | `Error` — limit 65535 on 16-bit; any `int32_t` (incl. INT32_MIN) fits a 32-bit counter |

**Completion and stopping:**

- The counter's compare interrupt stops both timers, disables CC1IE and calls `Done` once. The generator has no interrupt sources enabled.
- Interrupt latency may let through extra edges or a partial cycle — exact hardware termination is not promised.
- `Stop()` stops both timers without forcing A/B low. `GenPulse()` restores the phase for a new series (also after a manual `Stop()`). Start with `GenPulse()`, not `Start()`.
- Serialize `main` calls with both timers' interrupts; call `GenPulse()`/`DeInit()` from `main`. `DeInit()` releases both timers. The counter must be free before setup. Destructors do no teardown.

### PWM input

Measures period and HIGH time of an external signal.

```cpp
constinit TIM measure(TIM::_3::instance, {TIM::_3::CH1::PA6});
if (measure.SetUp(100, nullptr, 10000) == SysInitStatus::InitOK
    && measure.SetUpPulseMeasure(TIM::Channel::CH1) == SysInitStatus::InitOK)
    measure.Start();
const uint32_t period_ticks = measure.GetCapture(TIM::Channel::CH1);
const uint32_t high_ticks   = measure.GetCapture(TIM::Channel::CH2);
```

- Needs ordinary `SetUp()` with an explicit period first. Input on CH1 or CH2; reserves both channels and SMCR.
- The selected channel captures rising edges (= period) and resets CNT; the paired channel captures the falling edge (= HIGH width). For input on CH2, swap CH1/CH2 in the reads.
- No DMA. The optional callback reports the period edge only; nothing is copied to RAM.
- The two CCR reads are not an atomic pair. First-cycle validity, overflow, missing edges and overcapture are the application's job.
- Other channels share the resetting counter — they have no independent time base.

### ITR and common controls

**`SetUpSlave(&source, SlaveMode::Reset / Gated / Trigger / ExternalClock)`** — after ordinary `SetUp()`, configures SMCR once, with both timers stopped.

- The source must already have `SetUpTrigger()`; TIM neither reconfigures nor starts the source.
- `ExternalClock` keeps PSC: one CNT step per PSC + 1 source edges.
- `GetCounterClock()` reports the internal clock/PSC, not the external event rate.

**`SetUpOnePulse()`** — sets one-pulse mode (OPM) once before `Start()`; hardware clears CEN at Update. Affects the whole timer.

**Routes:** ITR0…ITR3 use the normal TRGO mapping, filtered by what CMSIS declares for the device. OC1-only slots and remapped inputs are not treated as TRGO. Unsupported routes → `InitError`. F410: TIM5 → TIM1 is supported; the TIM5 LPTIM input is outside this API. Keep the source initialized while the receiver runs.

### Finite pulse series

Two ways to output exactly N pulses:

**1. RCR + OPM on one timer (exact, hardware-terminated):**

```cpp
constinit TIM burst(TIM::_1::instance, {TIM::_1::CH1::PA8});
if (burst.SetUpPulseBurst(TIM::Channel::CH1, 1000, 1000) == SysInitStatus::InitOK)
    burst.RunPulses(100);   // check SysStatus; starts the burst
```

- Whole-timer mode. PWM2, HIGH for `floor(period/2)`; output LOW at CNT = 0 and after the final overflow.
- The repetition counter (RCR = count − 1) and OPM stop the series in hardware — no per-pulse interrupt. The optional callback fires on the final Update.
- Service UG loads registers with URS; its flag is cleared before `Start()`. `count = 0` does not start.
- `SetDuty()`/`SetCCR()` do not apply to this mode.

| Timer | Max pulses per series |
| --- | --- |
| F4 (TIM1/TIM8) | 256 |
| F7 (TIM1/TIM8) | 65536 |
| G0 TIM1 | 65536 |
| G0 TIM15/16/17 | 256 |

Timers without RCR or out-of-range counts → error. There is no software counting or automatic fallback. See [RM0410, TIMx_RCR](https://www.st.com/resource/en/reference_manual/rm0410-stm32f76xxx-and-stm32f77xxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf), [RM0444, TIM15_RCR](https://www.st.com/resource/en/reference_manual/rm0444-stm32g0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).

**2. A second timer counts the pulses (any timer, IRQ-terminated):**

```cpp
constinit TIM pulses(TIM::_3::instance, {TIM::_3::CH1::PA6});
constinit TIM counter(TIM::_2::instance);
if (pulses.SetUp(1000, nullptr, 1000) == SysInitStatus::InitOK
    && pulses.SetUpPWM(TIM::Channel::CH1, 500) == SysInitStatus::InitOK
    && pulses.SetUpTrigger() == SysInitStatus::InitOK
    && counter.SetUpCounter(&pulses, 65536) == SysInitStatus::InitOK
    && counter.StopAfter(100) == SysStatus::OK) {
    counter.Start();
    pulses.Start();
}
// counter.Count() reads CNT; no IRQ copy or software accumulation.
```

- `SetUpCounter()` replaces ordinary `SetUp()`, fixes source, period and optional callback, uses PSC = 0 and reserves CH1 for the stop compare. The source's TRGO must be `Update`; its configuration is not changed.
- Without an armed stop, `Count()` works without any interrupt.
- `StopAfter()`/`StopAt()` arm one compare while both timers are stopped; the target must be ahead of CNT within the current period. Reset the counter before a new series if needed. A pending CC1IF is cleared when arming.
- On completion: CC1IE off, both timers stopped, callback called once.

> **IRQ stop has latency:** the source can emit extra pulses before the CPU clears CEN, and the last one may be partial. Stopping does not force the output LOW. Hardware gating feedback is not implemented — use method 1 when exact termination is needed and the timer has RCR.

---

## 12. Usage Examples

A minimal PWM output. Check device/package availability and every returned status.

```cpp
#include "tim.hpp"
using T = TIM::_3;

constinit TIM pwm(T::instance, {T::CH1::PA6});

// In main, after System/RCC initialization:
if (pwm.SetUp(1000, nullptr, 1000) == SysInitStatus::InitOK
    && pwm.SetUpPWM(TIM::Channel::CH1, 500) == SysInitStatus::InitOK)
    pwm.Start();   // 1 kHz, 50 %
```

Other examples are in their sections: periodic IRQ (4), PWM (5), capture/compare (6), trigger (7), DMA (8), WS2812B (9), encoder, quadrature, PWM input, pulse series (11).

---

## 13. Edge Cases

| Situation | Behaviour |
| --- | --- |
| Different frequencies on channels of one timer | Not possible — all channels share PSC/ARR. |
| 16-bit PWM period > 65535 ticks | Rejected — needed to represent exact 100 %. |
| IRQ-terminated pulse series | Can emit extra or partial pulses; use RCR for exact counts. |
| `main` changes DIER while timer/DMA IRQs can run | Caller must mask the affected IRQs. |
| PWM DMA TC | Buffer done, not "last PWM period on the pin". |

---

## See Also

- [ST F4 LL TIM — PWM modes, preload, CCDS](https://github.com/STMicroelectronics/stm32f4xx-hal-driver/blob/master/Inc/stm32f4xx_ll_tim.h)
- [ST G0 LL TIM — CC DMA selection](https://github.com/STMicroelectronics/stm32g0xx-hal-driver/blob/master/Inc/stm32g0xx_ll_tim.h)
- [ST AN4031 — DMA requests and stopping F4/F7 streams](https://www.st.com/resource/en/application_note/dm00046011.pdf)
- [ST AN5224 — DMAMUX request routing](https://www.st.com/resource/en/application_note/an5224-stm32-dmamux-the-dma-request-router-stmicroelectronics.pdf)
- [RM0383 — F411 DMA maps](https://www.st.com/resource/en/reference_manual/DM00119316.pdf)
- [RM0385 — F74/F75 DMA maps](https://www.st.com/resource/en/reference_manual/dm00124865.pdf)
- [RM0410 — F76/F77 DMA maps](https://www.st.com/resource/en/reference_manual/rm0410-stm32f76xxx-and-stm32f77xxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [RM0444 — G0 DMAMUX map](https://www.st.com/resource/en/reference_manual/rm0444-stm32g0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [Initialization.md](Initialization.md)
- [GPIO.md](GPIO.md)
- [DMA.md](DMA.md)
- [ADC.md](ADC.md)
- [IRQ_Registry.md](IRQ_Registry.md)

---

## Русский

## Содержание

1. [Зачем это нужно](#1-зачем-это-нужно)
2. [Постоянная инициализация и диагностика конструктора](#2-постоянная-инициализация-и-диагностика-конструктора)
3. [Общий объект и привязки](#3-общий-объект-и-привязки)
4. [Один SetUp, прежние параметры](#4-один-setup-прежние-параметры)
5. [PWM без forced active](#5-pwm-без-forced-active)
6. [Capture и compare](#6-capture-и-compare)
7. [TRGO и ADC](#7-trgo-и-adc)
8. [Независимые PWM DMA](#8-независимые-pwm-dma)
9. [WS2812B](#9-ws2812b)
10. [IRQ и освобождение](#10-irq-и-освобождение)
11. [Дополнительные режимы и интеграция](#11-дополнительные-режимы-и-интеграция)
12. [Примеры использования](#12-примеры-использования)
13. [Угловые случаи](#13-угловые-случаи)
14. [См. также](#см-также)

---

## 1. Зачем это нужно

`TIM` — **один класс для всего, что умеет таймер**: периодическое прерывание, PWM, захват входа, события сравнения, триггер АЦП, PWM с DMA, энкодер, серии импульсов и другое.

- Один объект владеет одним аппаратным таймером — его базой счёта (PSC/ARR) и четырьмя каналами.
- `SetUp()` настраивает общую базу; функции вроде `SetUpPWM()`/`SetUpCapture()` назначают роль каждому каналу.
- Все каналы используют один счётчик, поэтому у них общие частота и период.
- Режимы «всего таймера» (энкодер, серия импульсов, …) занимают таймер целиком.
- Выводы каналов, DMA и необязательный второй таймер закрепляются конструктором.

---

## 2. Постоянная инициализация и диагностика конструктора

Объявляйте объект глобально с `constinit` (или как `static constinit` внутри функции). Аргументы: `TIM::_N::instance`, до четырёх привязок каналов `{вывод, dma}` и необязательный второй `TIM*`. DMA — именованный маршрут `_N::DMA::...` на F4/F7 или физический `DMAx_Channely_BASE` на G0 (номер запроса подставляет таймер).

Что происходит при неверном аргументе (например, вывод TIM2 у TIM3 или один канал дважды):

| Объявление | Результат |
| --- | --- |
| Объект с `constinit` | **Ошибка компиляции**, текст причины — в сообщении. |
| Объект без `constinit` | Собирается; при старте конструктор останавливается в `System::DebugTrap(причина)`. |

Каждая проверка вызывает `STM32_CONFIG_ERROR("причина")`: при компиляции — ошибка GCC с текстом причины, во время работы — `System::DebugTrap(text)`. `constinit` обязателен по правилам использования, но не проверяется.

`SetUp()` по-прежнему проверяет состояние аппаратуры и занятость ресурсов. Объект хранит ссылку на instance и один указатель регистров; нулевой указатель — «не настроен» (обнуляется после неудачного первого `SetUp()` и в `DeInit()`). Подробнее: [Initialization.md](Initialization.md).

---

## 3. Общий объект и привязки

**Конструктор:**

```cpp
constinit TIM timer(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7});
```

- До четырёх привязок `{вывод, dma}` **в любом порядке**. Именованный вывод `TIM::_N::CHx::Pyz` уже знает номер канала, GPIO/AF и таймер.
- `TIM_PIN` (consteval) проверяет числа; конструктор TIM проверяет принадлежность таймеру и повторы каналов.
- Наличие вывода в корпусе и точный AF-маршрут проверяет пользователь.
- Внутри для каждого канала хранятся только порт/вывод/AF и DMA; номер канала — индекс массива, адрес таймера берётся из instance. `DmaOwned(i)` — это просто `_dma_status[i] != NotInit`.

**Второй таймер** (после четырёх привязок каналов, перед политикой кеша) — нужен для генерации конечной квадратуры:

```cpp
constinit TIM counter(TIM::_2::instance);
constinit TIM generator(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7}, {}, {}, &counter);
```

- Указатель закрепляется конструктором. Объявите `counter` первым и сохраняйте его, пока им пользуется `generator`.
- `Init()` проверяет маршрут TRGO/ITR до изменения аппаратуры и регистрации IRQ и возвращает `InitError` при несовместимой паре.
- `SetUpQuadrature()` настраивает оба таймера; `generator.DeInit()` освобождает оба. Пока генератор владеет `counter`, не используйте его самостоятельно (см. раздел 11).

**Роли и время жизни:**

- Объектов каналов, `GetOutput()` и `AttachDMA()` нет.
- Роль канала задаётся один раз его функцией настройки; повторная настройка того же канала отклоняется (даже с `nullptr` callback).
- `DeInit()` очищает роли и занятые ресурсы, сохраняя привязки конструктора.
- TIM владеет всеми четырьмя объектами DMA; объект должен жить всё время работы. Деструктор ничего не освобождает.
- Настройка и изменения реестра выполняются в `main`, никогда в callback.

**`TIM_USE_ATOMIC`** (в `tim.hpp`, по умолчанию `0`): `1` переключает «прочитать-изменить-записать» управляющих регистров и RCC на макросы ST `ATOMIC_*`. `DMA_USE_ATOMIC` настраивается отдельно. Ни одна из настроек не согласует целую операцию и не заменяет правил маскирования прерываний ниже. После изменения пересоберите TIM и его пользователей.

**Старый код:** прежние `TIM_PeriodicIRQ`/`TIM_PWM`/`InputCapture` и составные классы сохранены комментариями в конце `tim.hpp`/`tim.cpp`. Они не компилируются и недоступны как классы совместимости.

---

## 4. Один SetUp, прежние параметры

```cpp
SysInitStatus SetUp(uint32_t freq, void (*callback)() = nullptr, uint32_t period = 0);
```

| Аргумент | Значение |
| --- | --- |
| `freq` | требуемая **частота Update/PWM** в Гц — не частота тиков счётчика |
| `period` | тиков счётчика на период; в ARR пишется `period − 1`. `0` — выбрать максимальный помещающийся период с минимальным предделителем. Для capture/счётчика период обязателен |
| `callback` | необязательное уведомление Update; `nullptr` оставляет прерывание Update выключенным |

**Расчёт** (32-битное целочисленное деление):

```text
ratio         = timer_clock / freq
PSC_divisor   = ratio / period          (при явном периоде)
CNT_clock     = timer_clock / PSC_divisor
actual_Update = CNT_clock / period
```

Результаты округляются вниз, поэтому фактическая частота может отличаться — читайте `GetCounterClock()` и `GetPeriod()`.

**Пределы:** 16-битный счётчик — период 1…65536; 32-битный — 1…UINT32_MAX (для PWM предел строже, раздел 5). Неверная частота/делители/период или ошибка регистрации → `InitError`. Повторный `SetUp()` → `InitError` до `DeInit()`.

**Загрузка:** PSC/ARR загружаются служебным обновлением (UG) при остановленном таймере; URS подавляет запрос IRQ/DMA от этого UG. TRGO и запуск АЦП настраивайте **после** `SetUp()`, чтобы аппаратные потребители не приняли события настройки за отсчёты. Функции настройки каналов глобальный UG не создают. **`Start()` всегда явный** — настройка таймер не запускает.

```cpp
void Tick();
constinit TIM tick(TIM::_3::instance);
if (tick.SetUp(1000, &Tick) == SysInitStatus::InitOK) tick.Start();   // прерывание Update 1 кГц
```

---

## 5. PWM без forced active

```cpp
constinit TIM pwm(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7});
if (pwm.SetUp(1000, nullptr, 1000) == SysInitStatus::InitOK) {   // 1 кГц, 1000 тиков
    pwm.SetUpPWM(TIM::Channel::CH1, 250); // 25 %; проверить InitOK.
    pwm.SetUpPWM(TIM::Channel::CH2, 500); // 50 %; проверить InitOK.
    pwm.Start();
}
pwm.SetDuty(TIM::Channel::CH1, 75); // Проверить SysStatus.
```

**`SetUpPWM(channel, initial_ccr = 0, dma_callback = nullptr)`** (таймер остановлен):

- режим PWM1, прямая полярность, предзагрузка CCR, настройка закреплённого вывода;
- затрагивает только этот канал, режимы других каналов не сбрасываются;
- на таймерах с BDTR включает MOE;
- загружает начальный CCR без глобального обновления.

**Почему период 16-битного PWM не больше 65535 тиков (ARR ≤ 65534):** чтобы помещалось `CCR = period` — это точные **100 %**; `CCR = 0` — точные **0 %**. Режим forced active не нужен. Для 32-битного PWM предел — UINT32_MAX. Если автоматическая база выбрала 65536 тиков, перенастройте с явным допустимым периодом через `DeInit()` + `SetUp()`.

**Изменение заполнения:**

| Метод | Вход | Примечание |
| --- | --- | --- |
| `SetDuty(ch, percent)` | 0…100 % (больше — ограничивается 100) | расчёт без переполнения `period × percent` и без 64-битного деления |
| `SetCCR(ch, ticks)` | 0…period тиков | |

Оба возвращают `NotInit`, `Error`, `Busy` (CCR занят DMA) или `OK`. Значение применяется через предзагрузку на следующем Update — мгновенной смены фазы нет.

---

## 6. Capture и compare

PSC/ARR общие для всех каналов — роли у каналов могут быть разными, частота счётчика — одна.

**Захват входа:**

```cpp
void Edge();
constinit TIM input(TIM::_3::instance, {TIM::_3::CH1::PA6});
if (input.SetUp(100, nullptr, 10000) == SysInitStatus::InitOK) {
    // Целевая частота CNT 1 МГц; фактическую показывает GetCounterClock().
    if (input.SetUpCapture(TIM::Channel::CH1, &Edge) == SysInitStatus::InitOK)
        input.Start();
}
// В Edge(): input.GetCapture(TIM::Channel::CH1).
```

- Прямой вход TIx, нарастающий фронт, без фильтра и входного делителя. В базовом `SetUp()` нужен явный период.
- `GetCapture()` всегда читает аппаратный CCR; это чтение также сбрасывает CCIF.
- Прерывание только подтверждает разрешённые события и вызывает callback — CCR не копируется. Новый захват может перезаписать значение до того, как вы его прочтёте.
- Переполнение периода, overcapture и перевод в единицы времени — задача приложения.
- Захват с DMA не поддерживается — не назначайте DMA каналу захвата.

**Сравнение без вывода:**

```cpp
constinit TIM tick(TIM::_3::instance);
if (tick.SetUp(1000, nullptr, 1000) == SysInitStatus::InitOK) {
    tick.SetCompareIRQ(TIM::Channel::CH1, 500, &HalfTick); // Проверить InitOK.
    tick.Start();
}
```

- Без GPIO; режим frozen output compare; значение сравнения 0…ARR.
- С `nullptr` callback настраивается только аппаратное событие сравнения без прерывания — канал всё равно занят до `DeInit()`. Так готовится прямой CCx-триггер АЦП.

---

## 7. TRGO и ADC

**`SetUpTrigger(TIM::Trigger::...)`** выбирает выход TRGO для других таймеров/АЦП — без прерываний таймера.

- Однократная настройка до `Start()`; таймер должен поддерживать режим master.
- Доступны: `Update`, `ComparePulse` (событие CH1; CH1 должен быть настроен), `OC1Ref`…`OC4Ref` (этот канал должен быть настроен как PWM).

```cpp
constinit TIM sampling(TIM::_3::instance);
if (sampling.SetUp(1000) == SysInitStatus::InitOK) {
    sampling.SetUpTrigger(TIM::Trigger::Update); // Проверить InitOK.
    // Сначала подготовить ADC, затем запустить TIM.
}
```

**Связь с АЦП:**

- Прямому событию CCx TRGO не нужен: настройте сравнение и выберите этот источник в АЦП.
- Таблицы EXTSEL принадлежат АЦП, а не TIM. Конструктор АЦП принимает `TIM*` и источник.
- Настраивайте TIM до `SetUp()` АЦП. `Start()` АЦП сначала готовит захват, затем запускает этот таймер; остановка/ошибка АЦП его останавливает.
- Отдельного `TIM_TriggerGenerator` нет. Примеры совместной работы — в [ADC.md](ADC.md).

---

## 8. Независимые PWM DMA

**Политика DMA-буферов.** Последний аргумент конструктора — `System::CachePolicy`, неизменный для объекта. По умолчанию `Cacheable`: можно передавать любой допустимый DMA-буфер, включая некешируемую память; на F7 драйвер выполняет обычное обслуживание кеша без определения области памяти. `NonCacheable` пропускает все вызовы обслуживания кеша для всех DMA-каналов TIM. В этом режиме приложение обязано размещать **все** используемые объектом буферы целиком в DTCM или некешируемой памяти MPU; передача кешируемого буфера — ошибка вызывающего кода. Политика не размещает буферы и не настраивает MPU. На F4/G0 при обоих значениях операций с кешем нет. Требования к завершению DMA и владению буфером сохраняются.

Пример F7 (этому объекту передаются только некешируемые буферы):

```cpp
using T = TIM::_1;
constinit TIM pwm(T::instance,
    {T::CH1::PA8, T::DMA::CH1_DMA2_Stream1}, {}, {}, {}, nullptr,
    System::CachePolicy::NonCacheable);
```

PWM через DMA подаёт в канал буфер значений CCR — по одному на период PWM (сигналы произвольной формы, протоколы светодиодов…).

**Привязка:** DMA передаётся в конструктор, TIM настраивает его в `SetUpPWM()` — самостоятельно `SetUp()` у него не вызывают. Полный маршрут TIM/CHx/DMA проверяется при конструировании. Если настройка канала не удалась, освобождаются только ресурсы этого канала.

**Аппаратный механизм:**

- DMA каждого PWM-канала использует свой запрос **TIMx_CHx** с `CR2.CCDS = 1` и свой бит `DIER.CCxDE`. UDE не используется.
- При CCDS = 1 запросы возникают на Update.
- CCDS общий для таймера, поэтому независимо выбрать момент DMA (Update или Compare) для каждого канала нельзя.
- Один запрос не назначается нескольким активным DMA.

```cpp
// F4/F7:
constexpr TIM::DMAReq dma1 = TIM::_1::DMA::CH1_DMA2_Stream1;
constexpr TIM::DMAReq dma2 = TIM::_1::DMA::CH2_DMA2_Stream2;
// G0 вместо них:
// constexpr TIM::DMAReq dma1 = DMA1_Channel2_BASE;
// constexpr TIM::DMAReq dma2 = DMA1_Channel3_BASE;
constinit TIM waveform(TIM::_1::instance, {TIM::_1::CH1::PA8, dma1}, {TIM::_1::CH2::PA9, dma2});
```

**Последовательность:** `SetUp()` один раз → `SetUpPWM()` для каждого канала → `SendDMA()` для каждого буфера при остановленном таймере → `Start()`.

- У каждого канала свой `GetDMAStatus(channel)` и свой необязательный callback (задаётся в `SetUpPWM()`), вызываемый при завершении или ошибке его DMA.
- **TC означает, что DMA прочитал буфер, — а не что закончился последний период PWM.** Последнее значение CCR применяется на одном из следующих Update.
- Ошибка канала останавливает только его DMA; таймер и остальные каналы продолжают работу.

**Буферы:**

| Таймер | Тип буфера | Ширина DMA |
| --- | --- | --- |
| 16-битный | `uint16_t` | Half_Word |
| 32-битный TIM2/TIM5 | перегрузка `uint32_t` | Word |

- Длина 1…65535 элементов; несоответствие перегрузки → `Error` (конвертации и поэлементной проверки нет).
- Не меняйте данные, пока статус `Busy` (в том числе пока завершается остановка после ошибки). Используйте память, доступную DMA.
- F7: при `Cacheable` System очищает источник (`CacheClean`); `NonCacheable` пропускает вызовы для всех каналов без определения области. Инвалидация для TX не нужна. На F4/G0 вызовы кеша исключены при компиляции.

**Аппаратные ограничения:** у TIM4 на F4/F7 нет DMA-запроса CH4. Маршруты запросов TIM2 CH2/CH4 пересекаются и не могут работать независимо при CCDS = 1 — такое сочетание отклоняется. Burst-режим DMA таймера не используется.

**Правила прерываний:**

- Записи DIER из `main` — в том числе `SendDMA()` и `IRQ_en()` — окружайте маскированием затронутых прерываний таймера/DMA. Остановка таймера ожидающие прерывания не маскирует. Внутри драйвера нет PRIMASK и блокировок.
- Дайте прерываниям этого объекта приоритеты, исключающие взаимное вытеснение.
- `SetUpPWM()` включает источники DMA и NVIC при инициализации; `SendDMA()` снова разрешает TE/DME через `Enable_IRQ(..., false)`, сохраняя вашу маску NVIC.

---

## 9. WS2812B

```cpp
constexpr uint32_t period = 80;                    // пример: CNT 64 МГц / 800 кГц
constexpr TIM::DMAReq dma = DMA1_Channel3_BASE;    // G0
constinit TIM ws(TIM::_1::instance, {TIM::_1::CH1::PA8, dma});
WS2812B_Strip<8> strip(52, 26);                    // CCR для битов «1» и «0»
strip.SetAll(RGB::Red());

if (ws.SetUp(800000, nullptr, period) == SysInitStatus::InitOK
    && ws.SetUpPWM(TIM::Channel::CH1) == SysInitStatus::InitOK
    && ws.SendDMA(TIM::Channel::CH1, strip.Data(), strip.Len()) == SysStatus::OK) {
    ws.Start();
}
// Не менять strip, пока ws.IsDMABusy(TIM::Channel::CH1).
```

- `period` — тики, а не значение регистра ARR: `period = 80` → ARR = 79.
- Значения CCR и длину reset-паузы подберите под реальную частоту и datasheet светодиодов.
- Кодирование цветов не изменилось; `GetOutput()`/`AttachDMA()` больше не нужны.
- DMA TC освобождает буфер; физическое окончание и reset-интервал формирует сама последовательность.

---

## 10. IRQ и освобождение

**Регистрация:**

- `SetUp()` регистрирует TIM на векторах Update/CC (один раз, если вектор общий).
- `SetUpPWM()` регистрирует отдельный обработчик для каждого привязанного DMA, даже на общем векторе. Каждый DMA-обработчик смотрит только свой ресурс; прерывание таймера флаги DMA не опрашивает.

**Прерывание таймера:** обрабатывает только разрешённые события Update/CC, не трогая выключенные (опрашиваемые) флаги. Снимков CCR нет — callback сам читает аппаратное значение (чтение CCR сбрасывает CCIF).

**Правила:**

- Callback не меняют реестр и конфигурацию.
- Обработчики таймера/DMA одного объекта не должны вытеснять друг друга.
- Изменения DIER из `main` окружайте маскированием затронутых прерываний; внутренних блокировок нет.
- В прерываниях нет ожиданий аппаратуры.
- Ошибка DMA останавливает только DMA своего канала и публикует `Error` после остановки; callback канала может проверить `GetDMAStatus()`. Автоматического повтора нет.

**`DeInit()`** (из `main`): отключает запросы таймера, останавливает и освобождает занятые DMA, удаляет только свои регистрации, сбрасывает/выключает этот таймер и очищает callback и роли. Общие обработчики, NVIC и тактирование контроллера DMA сохраняются. Привязки конструктора остаются для последующей настройки. Может ждать остановки DMA, callback не вызывает. Деструктор ничего не освобождает.

---

## 11. Дополнительные режимы и интеграция

### Дополнительные режимы TIM

Все режимы ниже — функции того же класса `TIM`, использующие привязки каналов из конструктора.

- Настройка «всего таймера» **заменяет** обычный `SetUp()` и действует до `DeInit()`.
- Callback необязательны и задаются только при инициализации.
- В прерываниях нет ожиданий аппаратуры.
- Внешний вход ETR пока не реализован.

### Энкодер

```cpp
constinit TIM encoder(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7});
if (encoder.SetUpEncoder(65536, TIM::EncoderMode::X4) == SysInitStatus::InitOK)
    encoder.Start();
const uint32_t position = encoder.Count();
```

- Аппаратный вход энкодера есть только на CH1/CH2; нужны оба вывода, без DMA; таймер занимается целиком.
- `period` явный: ARR = period − 1, PSC = 0.
- `X2_CH1`/`X2_CH2` считают оба фронта своего входа; `X4` — оба фронта обоих входов.
- `Count()` возвращает CNT напрямую с аппаратным переполнением — без преобразования знака и программного расширения. Прерывания и callback не нужны.

### Генерация квадратуры

Генерирует точное число циклов квадратурного сигнала A/B (например, для имитации энкодера); циклы считает второй таймер аппаратно.

```cpp
void Done(); // Необязательно; вызывается из IRQ второго TIM после остановки обоих.
constinit TIM counter(TIM::_2::instance);
constinit TIM generator(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7}, {}, {}, &counter);

if (generator.SetUpQuadrature(TIM::Channel::CH1, TIM::Channel::CH2,
        1000, 1000, &Done) == SysInitStatus::InitOK) {
    generator.GenPulse(100); // Проверить SysStatus; запускает оба таймера.
}
// В main: counter.Count() читает полные циклы A/B прямо из CNT.
// После остановки: generator.GenPulse(-100) меняет порядок фаз.
```

**Настройка (`SetUpQuadrature(a, b, freq, period, done)`):**

- Заменяет обычный `SetUp()`; занимает генератор целиком и второй TIM из конструктора.
- `a`/`b` — два разных закреплённых выходных канала, без DMA.
- `freq` — полных циклов A/B в секунду; `period` — тиков генератора на **полуцикл** (чётное, ≥ 4).
- Два toggle-канала сравнивают CNT с `floor(period/4)` и `floor(period/4) + period/2` — сдвиг на четверть цикла.
- Делители считаются с целочисленным усечением; фактическая частота может отличаться от заданной.
- Счётчик настраивается автоматически: ITR/Update с PSC = 1, то есть два Update генератора = один полный цикл в CNT. Прерываний и программного счёта на каждый цикл нет.

**`GenPulse(N)`:** сбрасывает оба счётчика и фазы делителей, взводит одно сравнение, запускает сначала счётчик, затем генератор. Служебный UG в TRGO не передаётся и в счёт не входит.

| N | Поведение |
| --- | --- |
| > 0 | A опережает B, N полных циклов (A — канал, переданный как `a`, не обязательно CH1) |
| < 0 | B опережает A, \|N\| полных циклов |
| 0 | остановленная пара не меняется |
| любой таймер работает | `Busy` |
| \|N\| больше диапазона счётчика | `Error` — предел 65535 для 16 бит; любой `int32_t` (включая INT32_MIN) помещается в 32-битный счётчик |

**Завершение и остановка:**

- Прерывание сравнения счётчика останавливает оба таймера, выключает CC1IE и один раз вызывает `Done`. Источники прерываний генератора выключены.
- Из-за задержки прерывания возможны лишние фронты или неполный цикл — точная аппаратная остановка не обещается.
- `Stop()` останавливает оба таймера, не переводя A/B принудительно в LOW. `GenPulse()` восстанавливает фазу новой серии (и после ручного `Stop()`). Запускайте через `GenPulse()`, а не `Start()`.
- Вызовы из `main` согласуйте с прерываниями обоих таймеров; `GenPulse()`/`DeInit()` вызывайте из `main`. `DeInit()` освобождает оба таймера. До настройки счётчик должен быть свободен. Деструкторы ничего не освобождают.

### Измерение импульса

Измеряет период и длительность HIGH внешнего сигнала.

```cpp
constinit TIM measure(TIM::_3::instance, {TIM::_3::CH1::PA6});
if (measure.SetUp(100, nullptr, 10000) == SysInitStatus::InitOK
    && measure.SetUpPulseMeasure(TIM::Channel::CH1) == SysInitStatus::InitOK)
    measure.Start();
const uint32_t period_ticks = measure.GetCapture(TIM::Channel::CH1);
const uint32_t high_ticks   = measure.GetCapture(TIM::Channel::CH2);
```

- Сначала нужен обычный `SetUp()` с явным периодом. Вход — CH1 или CH2; занимаются оба канала и SMCR.
- Выбранный канал захватывает нарастающие фронты (= период) и сбрасывает CNT; парный — спадающий фронт (= длительность HIGH). Для входа CH2 поменяйте CH1/CH2 местами при чтении.
- Без DMA. Необязательный callback сообщает только о фронте периода; данные в RAM не копируются.
- Два чтения CCR не являются атомарной парой. Первый цикл, переполнение, пропавшие фронты и overcapture — задача приложения.
- Остальные каналы используют тот же сбрасываемый счётчик — независимой временной базы у них нет.

### ITR и функции общей базы

**`SetUpSlave(&source, SlaveMode::Reset / Gated / Trigger / ExternalClock)`** — после обычного `SetUp()` однократно настраивает SMCR при остановленных обоих таймерах.

- У источника заранее должен быть `SetUpTrigger()`; TIM источник не перенастраивает и не запускает.
- `ExternalClock` сохраняет PSC: один шаг CNT на PSC + 1 фронтов источника.
- `GetCounterClock()` показывает внутреннюю частоту/PSC, а не частоту внешних событий.

**`SetUpOnePulse()`** — однократно, до `Start()`, задаёт режим одного импульса (OPM): аппаратный Update снимает CEN. Влияет на весь таймер.

**Маршруты:** ITR0…ITR3 используют обычное отображение TRGO с учётом таймеров, объявленных в CMSIS. Слоты с прямым OC1 и переназначенные входы за TRGO не выдаются. Неподдерживаемый маршрут → `InitError`. На F410 поддерживается TIM5 → TIM1; вход LPTIM таймера TIM5 к этому API не относится. Источник должен оставаться настроенным всё время работы приёмника. См. [RM0401, таблицы 59/63](https://www.st.com/resource/en/reference_manual/rm0401-stm32f410-advanced-armbased-32bit-mcus-stmicroelectronics.pdf), [RM0444, таблицы 119/123/130](https://www.st.com/resource/en/reference_manual/rm0444-stm32g0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).

### Конечная серия импульсов

Два способа выдать ровно N импульсов:

**1. RCR + OPM одного таймера (точно, аппаратная остановка):**

```cpp
constinit TIM burst(TIM::_1::instance, {TIM::_1::CH1::PA8});
if (burst.SetUpPulseBurst(TIM::Channel::CH1, 1000, 1000) == SysInitStatus::InitOK)
    burst.RunPulses(100); // Проверить SysStatus; запускает серию.
```

- Режим всего таймера. PWM2, HIGH длительностью `floor(period/2)`; LOW при CNT = 0 и после последнего переполнения.
- Счётчик повторений (RCR = count − 1) и OPM останавливают серию аппаратно — без прерывания на каждый импульс. Необязательный callback вызывается по последнему Update.
- Служебный UG загружает регистры с URS; его флаг сбрасывается до `Start()`. `count = 0` серию не запускает.
- `SetDuty()`/`SetCCR()` к этому режиму не применяются.

| Таймер | Максимум импульсов в серии |
| --- | --- |
| F4 (TIM1/TIM8) | 256 |
| F7 (TIM1/TIM8) | 65536 |
| G0 TIM1 | 65536 |
| G0 TIM15/16/17 | 256 |

Таймер без RCR или превышение диапазона → ошибка. Программного счёта и автоматического перехода на второй таймер нет.

**2. Импульсы считает второй таймер (любой таймер, остановка из прерывания):**

```cpp
constinit TIM pulses(TIM::_3::instance, {TIM::_3::CH1::PA6});
constinit TIM counter(TIM::_2::instance);
if (pulses.SetUp(1000, nullptr, 1000) == SysInitStatus::InitOK
    && pulses.SetUpPWM(TIM::Channel::CH1, 500) == SysInitStatus::InitOK
    && pulses.SetUpTrigger() == SysInitStatus::InitOK
    && counter.SetUpCounter(&pulses, 65536) == SysInitStatus::InitOK
    && counter.StopAfter(100) == SysStatus::OK) {
    counter.Start();
    pulses.Start();
}
// counter.Count() читает CNT; без копирования в IRQ и программного накопления.
```

- `SetUpCounter()` заменяет обычный `SetUp()`, закрепляет источник, период и необязательный callback, ставит PSC = 0 и занимает CH1 под сравнение остановки. TRGO источника должен быть `Update`; его настройка не меняется.
- Без взведённой остановки `Count()` работает без прерываний.
- `StopAfter()`/`StopAt()` взводят одно сравнение при остановленных обоих таймерах; цель должна быть впереди CNT в текущем периоде. Для новой серии при необходимости сначала сбросьте счётчик. Старый CC1IF при взведении сбрасывается.
- При завершении: CC1IE выключается, оба таймера останавливаются, callback вызывается один раз.

> **Остановка из прерывания имеет задержку:** до снятия CEN источник может выдать лишние импульсы, последний может оказаться неполным. Остановка не переводит выход в LOW принудительно. Аппаратная обратная связь (gated) не реализована — если нужна точная остановка и у таймера есть RCR, используйте способ 1.

---

## 12. Примеры использования

Минимальный выход PWM. Проверьте наличие блока/выводов в корпусе и каждый возвращаемый статус.

```cpp
#include "tim.hpp"
using T = TIM::_3;

constinit TIM pwm(T::instance, {T::CH1::PA6});

// В main, после инициализации System/RCC:
if (pwm.SetUp(1000, nullptr, 1000) == SysInitStatus::InitOK
    && pwm.SetUpPWM(TIM::Channel::CH1, 500) == SysInitStatus::InitOK)
    pwm.Start();   // 1 кГц, 50 %
```

Остальные примеры — в разделах режимов: периодическое прерывание (4), PWM (5), захват/сравнение (6), триггер (7), DMA (8), WS2812B (9), энкодер, квадратура, измерение импульса, серии импульсов (11).

---

## 13. Угловые случаи

| Ситуация | Поведение |
| --- | --- |
| Разные частоты на каналах одного таймера | Невозможно — PSC/ARR общие. |
| Период 16-битного PWM больше 65535 тиков | Отклоняется — нужно для точных 100 %. |
| Серия импульсов с остановкой из прерывания | Возможны лишние или неполные импульсы; для точного счёта используйте RCR. |
| `main` меняет DIER, пока могут выполняться IRQ таймера/DMA | Вызывающий код маскирует затронутые IRQ. |
| TC у PWM DMA | Буфер передан, но последний период PWM ещё не обязательно выдан. |

---

## См. также

- [ST F4 LL TIM — режимы PWM, preload, CCDS](https://github.com/STMicroelectronics/stm32f4xx-hal-driver/blob/master/Inc/stm32f4xx_ll_tim.h)
- [ST G0 LL TIM — выбор CC DMA](https://github.com/STMicroelectronics/stm32g0xx-hal-driver/blob/master/Inc/stm32g0xx_ll_tim.h)
- [ST AN4031 — запросы DMA и остановка потоков F4/F7](https://www.st.com/resource/en/application_note/dm00046011.pdf)
- [ST AN5224 — маршрутизация запросов DMAMUX](https://www.st.com/resource/en/application_note/an5224-stm32-dmamux-the-dma-request-router-stmicroelectronics.pdf)
- [RM0383 — карты DMA F411](https://www.st.com/resource/en/reference_manual/DM00119316.pdf)
- [RM0385 — карты DMA F74/F75](https://www.st.com/resource/en/reference_manual/dm00124865.pdf)
- [RM0410 — карты DMA F76/F77](https://www.st.com/resource/en/reference_manual/rm0410-stm32f76xxx-and-stm32f77xxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [RM0444 — карта DMAMUX G0](https://www.st.com/resource/en/reference_manual/rm0444-stm32g0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [Initialization.md](Initialization.md)
- [GPIO.md](GPIO.md)
- [DMA.md](DMA.md)
- [ADC.md](ADC.md)
- [IRQ_Registry.md](IRQ_Registry.md)
