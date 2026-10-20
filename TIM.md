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

One TIM object owns a counter base and configures channel roles through functions. Channels share PSC/ARR; whole-timer modes reserve the required resources. DMA and an optional second timer are fixed at construction.

---

## 2. Constant Initialization and Constructor Diagnostics

Declare fixed peripheral objects with `constinit` at file scope or as `static constinit`. Use the provided _N::instance, named pins and optional _N::DMA routes. G0 takes numeric DMAx_Channely_BASE selections; the peripheral fixes the request source.

For peripheral binding checks, each failing condition uses `STM32_CONFIG_ERROR("reason")`. The reason is a template argument of the non-constexpr error function, so GCC includes it in the diagnostic itself. The macro uses `std::is_constant_evaluated()` to select that path; at runtime it calls `System::DebugTrap(text)` directly, without calling the template. A `constinit` initializer rejects that call during compilation; without constinit, a failing runtime construction stops. The keyword is mandatory by the usage contract, not enforced by the constructor. SetUp still checks hardware state and occupied resources. See [Initialization.md](Initialization.md) for constructor variants and migration.

---

## 3. One timer and its channel resources

TIM_USE_ATOMIC in tim.hpp defaults to 0; 1 selects ST ATOMIC_* control/RCC
updates. Rebuild TIM and its users after changing it. DMA_USE_ATOMIC is separate.
Neither setting serializes a full operation or changes the caller's IRQ-masking
contract for channel control.

One `TIM` object owns one hardware timer. Initialize System/RCC first. The
constructor fixes up to four `{pin, dma}` descriptions, in any order. Named
`TIM::_N::CHx::Pyz` pins carry the channel number, GPIO/AF and timer identity.
Their numeric fields are checked by the `TIM_PIN` consteval constructor;
peripheral ownership and duplicate bindings are checked during constinit initialization.
Package bonding and exact device AF routing remain the application's checks.

```cpp
constinit TIM timer(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7});
```

The last optional constructor argument is a second TIM for finite quadrature:

```cpp
constinit TIM counter(TIM::_2::instance);
constinit TIM generator(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7}, {}, {}, &counter);
```

This pointer is fixed at construction. Construct counter first and keep it
alive while generator uses it. Init checks the normal TRGO/ITR route before
acquiring hardware or registering IRQs, returning InitError on a bad binding. SetUpQuadrature initializes
the pair; the counter uses PSC=1 and CNT counts full A/B cycles. Generator
DeInit releases both timers. Do not configure/use the counter independently
while the generator owns it; see the quadrature example below.

There are no channel objects, `GetOutput()` handles or `AttachDMA()`. Channel
initialization functions select the role once; a shared ownership mask rejects
a second setup, even with a null callback. `DeInit()` clears roles/acquisitions,
but preserves constructor bindings. TIM owns all four DMA objects; keep TIM alive. Destruction
performs no automatic teardown. Configuration and registry changes belong to
main, never to callbacks.

Old TIM_PeriodicIRQ/TIM_PWM/InputCapture and composite-mode implementations are
retained as comments at the ends of tim.hpp/tim.cpp. They are not compiled or
available as compatibility classes. Current encoder, pulse-burst and hardware
counter and finite quadrature functions are described below. Their old commented
implementations are not active.

---

## 4. Common setup and units

```cpp
SysInitStatus SetUp(uint32_t freq, void (*callback)() = nullptr, uint32_t period = 0);
```

The previous argument order and frequency meaning are retained:

- `freq`: requested **Update/PWM frequency**, in Hz.
- `period`: CNT ticks per period; the ARR register receives `period - 1`.
- `period == 0`: choose the largest period fitting the counter with the minimum
  prescaler divisor. Capture/counter operation requires an explicit period.
- `callback`: optional Update notification. Null leaves Update IRQ disabled.

The calculation uses 32-bit integer division:

```text
ratio = timer_clock / freq
PSC_divisor = ratio / period        (for an explicit period)
CNT_clock = timer_clock / PSC_divisor
actual_Update = CNT_clock / period
```

Rounding is retained. Read `GetCounterClock()` and `GetPeriod()` for actual
configuration; do not interpret `freq` as the CNT tick rate. A 16-bit counter
accepts periods 1..65536, a 32-bit counter 1..UINT32_MAX. PWM has the stricter
limit below. Invalid frequency/dividers/period or registration failure returns
InitError. Repeated SetUp returns InitError until DeInit.

PSC/ARR are loaded with a service UG while stopped; URS suppresses its IRQ/DMA
request. Configure TRGO and arm ADC only afterwards: hardware consumers must
not interpret setup events as samples. Channel setup does not generate a global
UG. `Start()` is explicit; setup never starts the timer automatically.

```cpp
void Tick();
constinit TIM tick(TIM::_3::instance);
if (tick.SetUp(1000, &Tick) == SysInitStatus::InitOK) tick.Start();
```

---

## 5. PWM and exact 0/100%

```cpp
constinit TIM pwm(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7});
if (pwm.SetUp(1000, nullptr, 1000) == SysInitStatus::InitOK) {
    // Check both InitOK results before Start.
    pwm.SetUpPWM(TIM::Channel::CH1, 250);
    pwm.SetUpPWM(TIM::Channel::CH2, 500);
    pwm.Start();
}
pwm.SetDuty(TIM::Channel::CH1, 75); // Check SysStatus.
```

`SetUpPWM(channel, initial_ccr=0, dma_callback=nullptr)` configures PWM1, direct
polarity, CCR preload and the bound pin, while stopped. It changes only this
channel; advanced timer MOE is enabled where present. Initial CCR is loaded
without a global update. Other channel modes remain intact.

**Period must fit CCR:** at most 65535 ticks for 16-bit PWM, so ARR <= 65534.
This leaves room for `CCR = period`, giving exact 100%; zero gives exact 0%.
There is no forced-active mode switch. For 32-bit PWM, the maximum period is
UINT32_MAX. A base automatically sized to 65536 ticks must be configured again
with an explicit valid PWM period, through DeInit/SetUp.

`SetDuty` clamps percent to 100. Its arithmetic avoids the overflowing
`period * percent` product and wide division. `SetCCR` takes raw ticks 0..period.
Both return NotInit, Error, Busy (DMA owns this CCR) or OK. PWM writes apply via
preload; they do not promise an immediate phase change.

---

## 6. Capture and pin-free compare

All channels share PSC/ARR. Different channel roles can coexist, but do not
have independent counter frequencies.

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

Capture is direct TIx, rising edge, no input filter or prescaler. An explicit
base period is required. GetCapture always reads hardware CCR; that read also
acknowledges CCIF. The IRQ only acknowledges enabled events and notifies the
callback; it does not read/copy CCR. The next capture may replace the value
before the application reads it. The application handles wraps, overcapture and time units.
Capture DMA is not added in this step; do not bind DMA to a capture channel.

```cpp
constinit TIM tick(TIM::_3::instance);
if (tick.SetUp(1000, nullptr, 1000) == SysInitStatus::InitOK) {
    tick.SetCompareIRQ(TIM::Channel::CH1, 500, &HalfTick); // Check InitOK.
    tick.Start();
}
```

Compare needs no GPIO, uses frozen OC mode and accepts 0..ARR. Null callback
configures only hardware compare without an interrupt; the channel is still
owned and cannot be initialized again. This is also how a direct ADC CCx event
is prepared.

---

## 7. Hardware triggers

`SetUpTrigger(TIM::Trigger::Update)` selects TRGO without any timer IRQ. It is
one-time, stopped configuration and requires a master-capable timer. Available
MMS selections are Update, ComparePulse (CH1 event), OC1Ref..OC4Ref. ComparePulse
requires CH1 configured; OCxRef requires that channel configured as PWM.

```cpp
constinit TIM sampling(TIM::_3::instance);
if (sampling.SetUp(1000) == SysInitStatus::InitOK) {
    sampling.SetUpTrigger(TIM::Trigger::Update); // Check InitOK.
    // Arm the ADC first, then start the timer.
}
```

A direct CCx ADC event does not need TRGO remapping: initialize compare and
select the appropriate source in ADC. TIM contains no ADC EXTSEL table. The
ADC constructor accepts `TIM*` and an ADC source. Configure TIM before ADC SetUp;
ADC Start arms the capture before starting this TIM. Stop/error stops the timer.
No separate TIM_TriggerGenerator is used. See [ADC.md](ADC.md).

---

## 8. Independent PWM DMA

DMAReq is passed in the constructor; TIM owns/configures DMA in SetUpPWM.
F4/F7 take a named Req; G0 takes only a physical channel. The route is checked
against both timer and channel in the constructor using the existing channel map. Resource
or registry failure releases only resources acquired by that channel setup.

Each PWM DMA uses its own TIMx_CHx request with `CR2.CCDS=1`, and its own
`DIER.CCxDE` gate. Requests occur at Update. UDE is not used. Multiple active
streams must not serve the same peripheral request. CCDS is common to the timer,
so per-channel DMA selection between Update and Compare is not independent.

F4/F7 names expose the selected controller/stream:

```cpp
constexpr TIM::DMAReq dma1 = TIM::_1::DMA::CH1_DMA2_Stream1;
constexpr TIM::DMAReq dma2 = TIM::_1::DMA::CH2_DMA2_Stream2;
constinit TIM waveform(TIM::_1::instance, {TIM::_1::CH1::PA8, dma1}, {TIM::_1::CH2::PA9, dma2});
```

G0 selects channels explicitly:

```cpp
constexpr TIM::DMAReq dma1 = DMA1_Channel2_BASE;
constexpr TIM::DMAReq dma2 = DMA1_Channel3_BASE;
constinit TIM waveform(TIM::_1::instance, {TIM::_1::CH1::PA8, dma1}, {TIM::_1::CH2::PA9, dma2});
```

Call SetUp once, SetUpPWM for each channel, then SendDMA for each buffer while
stopped, then Start. Each channel has its own GetDMAStatus and optional callback
bound by SetUpPWM. TC releases that channel's source buffer; it **does not mean
the last PWM period has ended**. CCR preload is applied on a later Update.
A failed channel does not stop other channels or the timer.

- Buffer length is 1..65535 elements and must match DMA width/alignment.
- Keep source data unchanged until status leaves Busy, including while handling
  an error stop. Use DMA-accessible RAM.
- F7 cleans the TX source through System::CacheClean. Project non-cacheable DMA
  areas are skipped; F4/G0 helpers are empty. There is no RX invalidate for PWM TX.
- F4/F7 TIM4 has no CH4 DMA request. TIM2 CH2/CH4 have overlapping hardware request
  routing and cannot be independent together with CCDS=1; this combination is
  rejected. No timer burst transfer or alternative combined-route scheme is added.

16-bit timers use uint16_t buffers with Half_Word DMA. 32-bit TIM2/TIM5 use the
uint32_t overload with Word DMA. A mismatched overload returns Error. No buffer
conversion or per-element validation is performed.

The application serializes main-side DIER writes by masking this TIM's affected
timer/DMA IRQs whenever they can run. This includes SendDMA and IRQ_en; configuring
several DMA channels or stopping the timer does not itself mask pending IRQs.
The driver adds no PRIMASK manipulation or IrqLock. Keep the same object's IRQs
at priorities that prevent mutual preemption.
SetUpPWM enables DMA sources and NVIC during initialization. SendDMA restores
TE/DME through Enable_IRQ(..., false), preserving the caller's NVIC mask.

---

## 9. WS2812B through the common TIM

```cpp
constexpr uint32_t period = 80; // Example: 64 MHz CNT clock / 800 kHz.
constexpr TIM::DMAReq dma = DMA1_Channel3_BASE; // G0
constinit TIM ws(TIM::_1::instance, {TIM::_1::CH1::PA8, dma});
WS2812B_Strip<8> strip(52, 26);
strip.SetAll(RGB::Red());
if (ws.SetUp(800000, nullptr, period) == SysInitStatus::InitOK
    && ws.SetUpPWM(TIM::Channel::CH1) == SysInitStatus::InitOK
    && ws.SendDMA(TIM::Channel::CH1, strip.Data(), strip.Len()) == SysStatus::OK) {
    ws.Start();
}
// In main, do not modify strip while ws.IsDMABusy(TIM::Channel::CH1).
```

Use period ticks, not the ARR register value: period=80 means ARR=79. Choose
CCR values and reset-trailer length for the actual clock and LED requirements.
The existing encoder buffer remains unchanged; no GetOutput/AttachDMA is needed.
DMA TC concerns the buffer, while physical/reset timing remains in the waveform.

---

## 10. IRQ and teardown

SetUp registers TIM on its Update/CC vectors (once for a shared vector).
SetUpPWM registers a distinct DMA handler for each bound DMA, even if their
NVIC vector is shared. Each DMA handler inspects its own resource only. Timer
IRQ handles enabled Update/CC events, preserving disabled polling flags.
GetCapture acknowledges CCIF by reading CCR. The IRQ has no CCR snapshots;
callbacks read hardware explicitly when needed.

Callbacks do not change registry/configuration. Timer/DMA handlers for the same
object must not preempt one another. The application masks affected timer/DMA IRQs around main-side DIER updates;
there are no internal locks or PRIMASK changes. Hardware-stop waiting is never performed in IRQ.
Errors stop only the affected channel's DMA and publish Error after stop; the
channel callback can inspect GetDMAStatus. There is no automatic retry.

DeInit runs from main: disable timer requests, stop/release acquired DMA, remove
only owned registrations, reset/disable this TIM and clear callbacks/roles.
Shared handlers/NVIC and shared DMA clocks remain. Constructor bindings survive
for a later explicit setup. DeInit may wait for DMA stop and invokes no callback.

---

## 11. Additional Modes and Integration

### Additional TIM modes

All modes below use the same TIM class and constructor channel bindings.
Whole-timer initialization replaces ordinary SetUp; configuration is fixed until
DeInit. Callbacks are optional and are passed only at initialization. No IRQ
waits for hardware. ETR is deferred.

### Encoder

```cpp
constinit TIM encoder(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7});
if (encoder.SetUpEncoder(65536, TIM::EncoderMode::X4) == SysInitStatus::InitOK)
    encoder.Start();
const uint32_t position = encoder.Count();
```

Only CH1/CH2 support hardware encoder input. SetUpEncoder requires both pins,
no DMA; it occupies the whole timer. period is explicit, ARR=period-1, PSC=0.
X2_CH1/X2_CH2 count both edges of that input; X4 counts both inputs. Count reads
raw CNT, with hardware wrap and no signed conversion/software extension. No
IRQ or callback is required. [RM0431, TIMx_SMCR](https://www.st.com/resource/en/reference_manual/dm00305990.pdf).

### Quadrature generation

```cpp
void Done(); // Optional; called from the second TIM's IRQ after both timers stop.
constinit TIM counter(TIM::_2::instance);
constinit TIM generator(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7}, {}, {}, &counter);
if (generator.SetUpQuadrature(TIM::Channel::CH1, TIM::Channel::CH2,
        1000, 1000, &Done) == SysInitStatus::InitOK) {
    generator.GenPulse(100); // Check SysStatus; starts both timers.
}
// In main: counter.Count() reads full A/B cycles directly from CNT.
// After stopping: generator.GenPulse(-100) reverses phase order. Check status.
```

SetUpQuadrature replaces ordinary SetUp and reserves the whole generator plus
its constructor-bound counter. a/b select two distinct bound output pins, with
no DMA. freq is full cycles/s; period is generator ticks per half cycle (even,
at least 4). Two toggle channels have compare values floor(period/4) and
floor(period/4)+period/2. Thus their separation is one quarter of the full A/B
cycle. Base calculation uses the existing integer divider policy; requested Hz
can differ from actual Hz.

The counter is initialized automatically over ITR/Update, PSC=1: two generator
Updates advance its CNT by one full cycle. There is no IRQ/software accumulation
per cycle. GenPulse(N) resets both CNT/prescaler phases, arms one compare and
starts the counter before the generator. Service UG is hidden from TRGO while
the source is stopped, so it is not included in the count. See [RM0090, MMS
Enable/Update](https://www.st.com/resource/en/reference_manual/rm0090-stm32f407-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
and [RM0444, EGR.UG](https://www.st.com/resource/en/reference_manual/rm0444-stm32g0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).

- Positive N: A leads B; negative N: B leads A. Direction changes the polarity
  of the selected A channel, not an unconditional CH1. Magnitude is full cycles.
- Zero leaves a stopped pair unchanged. Busy if either timer is running. Error
  if |N| exceeds the counter range: 65535 on 16-bit hardware; all int32_t
  magnitudes, including INT32_MIN, fit a 32-bit counter. No signed negation overflow.
- Optional Done is fixed in SetUpQuadrature. The counter's compare IRQ stops
  both timers, disarms CC1IE and then calls Done once. The generator has no
  enabled interrupt source. IRQ latency may allow excess edges or a partial cycle;
  no exact hardware termination is promised.
- Stop stops both timers without forcing A/B LOW. GenPulse resets phase for a
  new series, including after manual Stop. Use GenPulse to start, not plain Start.
- The caller serializes main operations with both timers' IRQs. GenPulse and
  DeInit belong to main. DeInit releases both acquired TIMs; constructor bindings
  survive. The counter must be free before setup and must not be independently
  reconfigured while owned. Neither destructor performs teardown.

### PWM input

```cpp
constinit TIM measure(TIM::_3::instance, {TIM::_3::CH1::PA6});
if (measure.SetUp(100, nullptr, 10000) == SysInitStatus::InitOK
    && measure.SetUpPulseMeasure(TIM::Channel::CH1) == SysInitStatus::InitOK)
    measure.Start();
const uint32_t period_ticks = measure.GetCapture(TIM::Channel::CH1);
const uint32_t high_ticks = measure.GetCapture(TIM::Channel::CH2);
```

Ordinary SetUp needs an explicit period. SetUpPulseMeasure accepts input CH1
or CH2, reserves both channels and SMCR. The selected channel captures rising
edges/period and resets CNT; the paired channel captures the falling edge/HIGH
width. For input CH2, swap CH1/CH2 in the two reads. No DMA on either channel.
Optional callback reports the period edge only; no RAM snapshot. Two CCR reads
are not an atomic pair. First-cycle validity, overflow, missing edges and
overcapture are application responsibilities. Other channels share the resetting
counter and cannot provide an independent time base.

### ITR and common controls

After ordinary SetUp, SetUpSlave(&source, SlaveMode::Reset/Gated/Trigger/
ExternalClock) configures SMCR once, while both timers are stopped. The source
must already have SetUpTrigger configured. TIM neither reconfigures the source
nor enables it automatically. GetCounterClock reports the internal clock/PSC;
it does not measure external event frequency. ExternalClock retains PSC and
counts one CNT step per PSC+1 source edges. SetUpOnePulse fixes OPM once before
Start; hardware clears CEN at Update. These functions affect the whole timer.

ITR0..ITR3 use normal TRGO mapping, filtered by the selected CMSIS device.
OC1-only slots and remapped inputs are not treated as TRGO. Unsupported routes
return InitError. F410 has TIM5 → TIM1; its TIM5 LPTIM input is outside this API.
The source must stay initialized while the receiver is in use.

### Finite pulse series

```cpp
constinit TIM burst(TIM::_1::instance, {TIM::_1::CH1::PA8});
if (burst.SetUpPulseBurst(TIM::Channel::CH1, 1000, 1000) == SysInitStatus::InitOK)
    burst.RunPulses(100); // Check SysStatus; starts the burst.
```

The whole-timer mode uses PWM2, high time floor(period/2), with LOW at CNT=0
and after terminal overflow. RCR=count-1 and OPM stop in hardware; optional
completion callback needs only the final Update. Service UG loads registers
with URS and its flag is cleared before Start. Zero count does not start.
Maximum count: F4 256; F7 65536; G0 TIM1 65536, TIM15/16/17 256. Unsupported
hardware/overflow returns an error. No software per-pulse count or automatic
fallback to a second timer. [RM0410, TIMx_RCR](https://www.st.com/resource/en/reference_manual/rm0410-stm32f76xxx-and-stm32f77xxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf),
[RM0444, TIM15_RCR](https://www.st.com/resource/en/reference_manual/rm0444-stm32g0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).

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

SetUpCounter replaces ordinary SetUp, fixes source/period and optional completion
callback, uses PSC=0 and reserves CH1 for the stop compare. Source TRGO must be
Update; its configuration is not changed. With no stop armed, Count works without
IRQ. StopAfter/StopAt arm one compare while both timers are stopped; targets must
be ahead of CNT within the current period. Before rearming a new series, Reset
the counter as needed. Pending CC1IF is cleared on arm. Completion disables CC1IE,
stops both timers and invokes the optional callback once.

**IRQ stop has latency:** the source can emit additional pulses before the CPU
clears CEN, and the last pulse can be partial. Stopping CNT does not force an
output LOW. Hardware gating feedback is not implemented; use the separate RCR
solution when exact hardware termination is required and supported.

---

## 12. Usage Examples

Independent example; check device/package availability and every returned status.

```cpp
#include "tim.hpp"
using T = TIM::_3;
constinit TIM pwm(T::instance, {T::CH1::PA6});
// In main, after System/RCC initialization:
if (pwm.SetUp(1000, nullptr, 1000) == SysInitStatus::InitOK
    && pwm.SetUpPWM(TIM::Channel::CH1, 500) == SysInitStatus::InitOK)
    pwm.Start();
// Other examples are in their mode sections: capture, compare, DMA,
// encoder, quadrature, pulse burst and a second hardware counter.
```

---

## 13. Edge Cases

Channels share the same counter frequency/period. A 16-bit PWM period is limited to 65535 ticks to represent 100%. IRQ-driven stop can emit extra/partial pulses. The caller masks affected IRQs for shared DIER changes. TC does not mean the last PWM period reached the output.

---

## See Also

- [ST F4 LL TIM — PWM modes, preload, CCDS](https://github.com/STMicroelectronics/stm32f4xx-hal-driver/blob/master/Inc/stm32f4xx_ll_tim.h).
- [ST G0 LL TIM — CC DMA selection](https://github.com/STMicroelectronics/stm32g0xx-hal-driver/blob/master/Inc/stm32g0xx_ll_tim.h).
- [ST AN4031 — DMA requests and stopping F4/F7 streams](https://www.st.com/resource/en/application_note/dm00046011.pdf).
- [ST AN5224 — DMAMUX request routing](https://www.st.com/resource/en/application_note/an5224-stm32-dmamux-the-dma-request-router-stmicroelectronics.pdf).
- [RM0383 — F411 DMA maps](https://www.st.com/resource/en/reference_manual/DM00119316.pdf).
- [RM0385 — F74/F75 DMA maps](https://www.st.com/resource/en/reference_manual/dm00124865.pdf).
- [RM0410 — F76/F77 DMA maps](https://www.st.com/resource/en/reference_manual/rm0410-stm32f76xxx-and-stm32f77xxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).
- [RM0444 — G0 DMAMUX map](https://www.st.com/resource/en/reference_manual/rm0444-stm32g0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).

**Validation:** source/diff inspection only. No build, tests or hardware execution
were performed for this API migration; the old compilation report is historical.

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

Один TIM владеет базой счётчика и настраивает роли каналов функциями. Каналы используют общие PSC/ARR; режимы всего таймера занимают соответствующие ресурсы. DMA и необязательный второй таймер закрепляются конструктором.

---

## 2. Постоянная инициализация и диагностика конструктора

Объявляйте объекты периферии глобально с `constinit` либо как `static constinit`. Используйте готовый _N::instance, именованные пины и необязательные маршруты _N::DMA. На G0 передаются числовые DMAx_Channely_BASE, источник запроса закрепляет периферия.

При проверке привязок периферии каждое неверное условие использует `STM32_CONFIG_ERROR("причина")`. Причина передаётся как параметр шаблона неконстантной функции, поэтому GCC включает её в само сообщение ошибки. Макрос выбирает путь через `std::is_constant_evaluated()`; во время выполнения напрямую вызывает `System::DebugTrap(text)`, без вызова шаблонной функции. При `constinit` такой вызов отклоняется при сборке; без него неверное runtime-создание останавливает программу. Ключевое слово обязательно по контракту, конструктор не проверяет его наличие. Проверки состояния аппаратуры и занятых ресурсов остаются в SetUp. Варианты конструкторов и миграция: [Initialization.md](Initialization.md).

---

## 3. Общий объект и привязки

Один TIM владеет одним аппаратным таймером и внутренними DMA его каналов.
Сначала настраиваются System/RCC. В конструктор передаются DMAReq F4/F7 либо
физические DMA-каналы G0; источник запроса G0 выбирается по TIM/каналу.
TIM_USE_ATOMIC в tim.hpp: 0 — обычные изменения управляющих регистров,
1 — ATOMIC_* ST. После изменения пересобираются драйвер и приложение.
DMA_USE_ATOMIC настраивается отдельно. Внешнее согласование main/IRQ сохраняется.
Конструктор принимает до четырёх `{pin, dma}` в любом порядке. Пин
`TIM::_N::CHx::Pyz` уже содержит канал, порт/AF и принадлежность таймеру.
Числа проверяет consteval TIM_PIN; конструктор TIM проверяет принадлежность
и повторы. Корпус МК и точный AF-маршрут проверяет пользователь.

```cpp
constinit TIM timer(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7});
```

Последний необязательный аргумент конструктора — второй TIM для конечной квадратуры:

```cpp
constinit TIM counter(TIM::_2::instance);
constinit TIM generator(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7}, {}, {}, &counter);
```

Указатель закреплён конструктором. Счётчик создают первым и сохраняют всё время
работы генератора. Init проверяет обычный маршрут TRGO/ITR до изменения
аппаратуры и регистрации IRQ; при несовместимости возвращает InitError. SetUpQuadrature настраивает пару; счётчик
использует PSC=1 и его CNT считает полные циклы A/B. DeInit генератора освобождает
оба таймера. Пока счётчик занят генератором, независимо его не настраивают
и не запускают; пример квадратуры приведён ниже.

Объектов каналов, GetOutput и AttachDMA нет. Привязки не меняются; функции
инициализации задают роль канала один раз. Общая маска запрещает повторную
настройку даже при nullptr callback. DeInit очищает роли/занятые ресурсы, сохраняя
привязки конструктора. Автоматического DeInit в деструкторе нет.

Старые TIM_PeriodicIRQ/TIM_PWM/InputCapture и составные классы сохранены
комментариями в конце tim.hpp/tim.cpp. Они не компилируются. Текущие функции
энкодера, серий импульсов, аппаратного счётчика и конечной квадратуры описаны ниже.
Их прежние закомментированные реализации не активны.

---

## 4. Один SetUp, прежние параметры

```cpp
SysInitStatus SetUp(uint32_t freq, void (*callback)() = nullptr, uint32_t period = 0);
```

- freq — требуемая частота Update/PWM в Гц, как прежде, не частота CNT.
- period — число тиков CNT в периоде; в ARR записывается period-1.
- Ноль выбирает период автоматически. Для capture/counter период обязателен.
- nullptr callback оставляет Update IRQ выключенным.

При явном периоде делитель PSC = floor(floor(timer_clock/freq)/period).
Фактическая частота CNT = timer_clock/(PSC+1), Update = CNT_clock/period.
Используется целочисленное деление, точность не обещается при неделимых частотах.
Читайте GetCounterClock и GetPeriod. Частота/делители и период проверяются;
ошибка регистрации откатывается. Повторный SetUp — InitError до DeInit.

16-битная база принимает 1..65536 тиков, 32-битная — 1..UINT32_MAX. Для PWM
действует более строгий предел ниже. PSC/ARR загружаются служебным UG при
остановленном таймере с URS, подавляющим запрос IRQ/DMA. TRGO/ADC подключаются
после настройки, чтобы UG не превратился в лишний запуск измерения. Настройка
каналов глобальный UG не создаёт. Запуск только явным Start.

```cpp
void Tick();
constinit TIM tick(TIM::_3::instance);
if (tick.SetUp(1000, &Tick) == SysInitStatus::InitOK) tick.Start();
```

---

## 5. PWM без forced active

```cpp
constinit TIM pwm(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7});
if (pwm.SetUp(1000, nullptr, 1000) == SysInitStatus::InitOK) {
    pwm.SetUpPWM(TIM::Channel::CH1, 250); // Проверить InitOK.
    pwm.SetUpPWM(TIM::Channel::CH2, 500); // Проверить InitOK.
    pwm.Start();
}
pwm.SetDuty(TIM::Channel::CH1, 75); // Проверить SysStatus.
```

SetUpPWM(channel, initial_ccr=0, dma_callback=nullptr) настраивает PWM1,
прямую полярность и preload CCR. Настраивается только выбранный канал,
другие каналы не сбрасываются. MOE включается на таймерах с BDTR.

Для 16-битного PWM период не больше **65535**, ARR <= 65534. Так CCR=period
даёт точные 100%, CCR=0 — точные 0%. Режим forced active не используется.
Для 32-битного PWM предел — UINT32_MAX. Если автоматическая база выбрала 65536,
для PWM нужен DeInit и повторный SetUp с явным допустимым периодом.

SetDuty ограничивает проценты значением 100; расчёт не переполняет 32-битное
произведение и не требует 64-битного деления. SetCCR принимает сырые тики
0..period. Возвращаются NotInit/Error/Busy/OK. При активном DMA менять CCR
через эти методы нельзя. Новое значение применяется через preload.

---

## 6. Capture и compare

PSC/ARR общие для всех каналов. Роли каналов могут различаться, частота CNT — одна.

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

Capture: прямой TIx, нарастающий фронт, без фильтра и входного делителя.
В базовом SetUp нужен явный период. GetCapture всегда читает аппаратный CCR
и подтверждает CCIF. IRQ только подтверждает разрешённые события и уведомляет
callback, без чтения/копирования CCR. Следующий захват может заменить значение
до его чтения приложением.
Переполнение периода, overcapture и расчёт времени обрабатывает приложение.
Capture DMA в этом шаге не добавляется; этому каналу DMA не передают.

```cpp
constinit TIM tick(TIM::_3::instance);
if (tick.SetUp(1000, nullptr, 1000) == SysInitStatus::InitOK) {
    tick.SetCompareIRQ(TIM::Channel::CH1, 500, &HalfTick); // Проверить InitOK.
    tick.Start();
}
```

Compare не требует GPIO: используется frozen OC, значение 0..ARR. С nullptr
настраивается только аппаратное событие без IRQ; канал всё равно занят до
DeInit. Такой compare может служить прямым CCx-триггером ADC.

---

## 7. TRGO и ADC

SetUpTrigger(Trigger::Update) выбирает аппаратный TRGO без IRQ. Настройка
однократная, до Start; нужен таймер с master mode. Доступны Update,
ComparePulse (событие CH1) и OC1Ref..OC4Ref. ComparePulse требует настроенный
CH1; OCxRef — канал PWM. Прямые ADC CCx-события не требуют перенастройки TRGO.

```cpp
constinit TIM sampling(TIM::_3::instance);
if (sampling.SetUp(1000) == SysInitStatus::InitOK) {
    sampling.SetUpTrigger(TIM::Trigger::Update); // Проверить InitOK.
    // Сначала взвести ADC, затем запустить TIM.
}
```

Таблицы EXTSEL принадлежат ADC. Его конструктор принимает TIM* и выбранный
источник. TIM настраивается до ADC SetUp; ADC Start вооружает захват и затем
запускает таймер. Stop/ошибка его останавливают. Отдельного TIM_TriggerGenerator
нет. Примеры совместной работы — в [ADC.md](ADC.md).

---

## 8. Независимые PWM DMA

DMA закреплён конструктором и настраивается внутри SetUpPWM. Самостоятельно
SetUp у него не вызывают. Проверяется полный маршрут TIM/CHx/DMA. При отказе
освобождаются только ресурсы неудачной настройки этого канала.

Используются TIMx_CHx, общий CCDS=1 и отдельные CCxDE. Каждый запрос возникает
на Update. UDE не включается. Один запрос не назначается нескольким активным
DMA. CCDS общий: независимо выбрать Update/Compare DMA каждому каналу нельзя.

```cpp
// F4/F7:
constexpr TIM::DMAReq dma1 = TIM::_1::DMA::CH1_DMA2_Stream1;
constexpr TIM::DMAReq dma2 = TIM::_1::DMA::CH2_DMA2_Stream2;
// G0 вместо них:
// constexpr TIM::DMAReq dma1 = DMA1_Channel2_BASE;
// constexpr TIM::DMAReq dma2 = DMA1_Channel3_BASE;
constinit TIM waveform(TIM::_1::instance, {TIM::_1::CH1::PA8, dma1}, {TIM::_1::CH2::PA9, dma2});
```

После SetUp настраиваются PWM-каналы. Для синхронного запуска сначала
SendDMA для каждого канала при остановленном TIM, затем Start. Статусы и
callbacks раздельные. Callback задаётся в SetUpPWM и вызывается при завершении
или ошибке своего DMA; результат показывает GetDMAStatus(channel).

TC означает, что DMA прочитал буфер, **не завершение последнего PWM-периода**.
Последний preload CCR применяется на последующем Update. Ошибка канала
останавливает только его DMA, таймер и остальные каналы продолжают работу.

Буфер: 1..65535 элементов, правильная ширина/выравнивание, доступная DMA память.
Менять данные до выхода из Busy запрещено, включая остановку после ошибки.
На F7 System::CacheClean обслуживает TX; некешируемые области проекта
пропускаются. На F4/G0 helpers пустые. Invalidate для PWM TX не нужен.

У F4/F7 TIM4 нет DMA-запроса CH4. Маршруты TIM2 CH2/CH4 пересекаются: их
одновременная независимая работа с CCDS=1 отклоняется. Burst-передача и
комбинированные обходные маршруты сейчас не добавляются.

Для 16-битных таймеров используются uint16_t/Half_Word, для 32-битных TIM2/TIM5 —
перегрузка uint32_t/Word. Несоответствие ширины — Error. Конвертации буфера и
покадровой проверки значений нет.

При записи DIER из main вызывающий код маскирует соответствующие timer/DMA IRQ,
если они могут выполняться. Это относится в том числе к SendDMA и IRQ_en.
Остановка CNT сама по себе не запрещает ожидающее прерывание. Внутри драйвера
нет PRIMASK/IrqLock. Прерывания одного объекта не должны прерывать друг друга.
SetUpPWM при инициализации включает источники DMA и NVIC. SendDMA восстанавливает
TE/DME через Enable_IRQ(..., false), сохраняя маску NVIC вызывающего кода.

---

## 9. WS2812B

```cpp
constexpr uint32_t period = 80; // Пример: CNT 64 МГц / 800 кГц.
constexpr TIM::DMAReq dma = DMA1_Channel3_BASE; // G0
constinit TIM ws(TIM::_1::instance, {TIM::_1::CH1::PA8, dma});
WS2812B_Strip<8> strip(52, 26);
strip.SetAll(RGB::Red());
if (ws.SetUp(800000, nullptr, period) == SysInitStatus::InitOK
    && ws.SetUpPWM(TIM::Channel::CH1) == SysInitStatus::InitOK
    && ws.SendDMA(TIM::Channel::CH1, strip.Data(), strip.Len()) == SysStatus::OK) {
    ws.Start();
}
// Не менять strip, пока ws.IsDMABusy(TIM::Channel::CH1).
```

period=80 означает ARR=79. CCR и число нулей reset trailer выбираются под
фактические частоты и требования светодиодов. Алгоритм кодирования цветов
не менялся. GetOutput/AttachDMA больше не нужны. DMA TC освобождает буфер;
физическое завершение и reset interval обеспечиваются самой последовательностью.

---

## 10. IRQ и освобождение

SetUp регистрирует TIM на Update/CC, один раз при общем векторе. Каждый DMA
имеет отдельный объект обработчика; общий вектор требует места для каждого.
Timer IRQ не опрашивает DMA. Выключенные polling-флаги сохраняются. Чтение
capture CCR подтверждает CCIF. В IRQ нет снимков CCR: callback при необходимости
сам читает аппаратное значение.

Callbacks не меняют реестр и конфигурацию. TIM/DMA IRQ этого объекта не должны
прерывать друг друга. Записи DIER из main согласует вызывающий код маскированием соответствующих
TIM/DMA IRQ. Встроенных блокировок или изменений PRIMASK нет. В IRQ нет ожиданий EN/BSY или иного аппаратного завершения.
Статус Error публикуется после остановки своего DMA, автоматического retry нет.

DeInit из main отключает запросы, останавливает/освобождает занятые DMA,
адресно удаляет регистрации, сбрасывает/отключает свой TIM и очищает callbacks
и маски ролей. Привязки конструктора сохраняются. Общие обработчики/NVIC и
такты контроллера DMA сохраняются. DeInit может ждать остановки DMA, callbacks
при этом не вызывает. Автоматической очистки в деструкторе нет.

---

## 11. Дополнительные режимы и интеграция

### Дополнительные режимы TIM

Все режимы ниже — функции общего TIM с прежними привязками каналов конструктора.
Настройка целого таймера заменяет обычный SetUp и сохраняется до DeInit.
Callbacks необязательны и задаются только при инициализации. Ожиданий аппаратуры
в IRQ нет. ETR отложен.

### Энкодер

```cpp
constinit TIM encoder(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7});
if (encoder.SetUpEncoder(65536, TIM::EncoderMode::X4) == SysInitStatus::InitOK)
    encoder.Start();
const uint32_t position = encoder.Count();
```

Аппаратный энкодер использует только CH1/CH2. Нужны оба пина, без DMA;
SetUpEncoder занимает таймер целиком. Период явный, ARR=period-1, PSC=0.
X2_CH1/X2_CH2 считают оба фронта соответствующего входа, X4 — обоих входов.
Count возвращает CNT напрямую, без программного накопления, преобразования
знака или расширения разрядности. IRQ и callback не требуются.

### Генерация квадратуры

```cpp
void Done(); // Optional; called from the second TIM's IRQ after both timers stop.
constinit TIM counter(TIM::_2::instance);
constinit TIM generator(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7}, {}, {}, &counter);
if (generator.SetUpQuadrature(TIM::Channel::CH1, TIM::Channel::CH2,
        1000, 1000, &Done) == SysInitStatus::InitOK) {
    generator.GenPulse(100); // Check SysStatus; starts both timers.
}
// In main: counter.Count() reads full A/B cycles directly from CNT.
// After stopping: generator.GenPulse(-100) reverses phase order. Check status.
```

SetUpQuadrature заменяет обычный SetUp, занимает генератор целиком и второй TIM
из конструктора. a/b — два разных выходных канала с закреплёнными пинами, без DMA.
freq — полные циклы A/B в секунду; period — тики генератора на полуцикл, чётное
число не меньше 4. Toggle-каналы сравнивают CNT с floor(period/4) и
floor(period/4)+period/2; разница составляет четверть полного цикла A/B.
Делители рассчитываются с прежним целочисленным усечением: фактическая частота
может отличаться от заданной.

Счётчик настраивается автоматически через ITR/Update с PSC=1: два Update
генератора увеличивают CNT на один полный цикл. IRQ/программного накопления
на каждый цикл нет. GenPulse(N) сбрасывает CNT и фазы делителей, взводит один
compare и запускает сначала счётчик, затем генератор. Служебный UG при
остановленном генераторе не передаётся в TRGO и в счёт не входит.

- N>0: A опережает B; N<0: B опережает A. Меняется полярность выбранного канала A,
  а не всегда CH1. Модуль N задаёт число полных циклов.
- Ноль не меняет остановленную пару. При работающем любом TIM — Busy. Если |N|
  больше диапазона счётчика — Error: предел 65535 для 16 бит; на 32-битном TIM
  помещаются все модули int32_t, включая INT32_MIN, без переполнения знакового минуса.
- Необязательный Done закрепляется в SetUpQuadrature. Compare IRQ счётчика
  останавливает оба TIM, снимает CC1IE и затем однократно вызывает Done. Источники
  IRQ генератора выключены. Из-за задержки IRQ возможны лишние фронты/неполный
  цикл; точная аппаратная остановка не обещается.
- Stop останавливает оба TIM, не переводя A/B принудительно в LOW. GenPulse
  восстанавливает фазу новой серии, в том числе после ручного Stop. Для запуска
  используется GenPulse, а не обычный Start.
- Вызовы main согласуются с IRQ обоих таймеров. GenPulse и DeInit вызывают из main.
  DeInit освобождает оба занятых TIM, привязки конструктора сохраняются. До настройки
  счётчик должен быть свободен; пока он занят, его независимо не перенастраивают.
  Деструкторы не выполняют освобождение аппаратуры.

### Измерение импульса

```cpp
constinit TIM measure(TIM::_3::instance, {TIM::_3::CH1::PA6});
if (measure.SetUp(100, nullptr, 10000) == SysInitStatus::InitOK
    && measure.SetUpPulseMeasure(TIM::Channel::CH1) == SysInitStatus::InitOK)
    measure.Start();
const uint32_t period_ticks = measure.GetCapture(TIM::Channel::CH1);
const uint32_t high_ticks = measure.GetCapture(TIM::Channel::CH2);
```

После обычного SetUp с явным периодом SetUpPulseMeasure выбирает вход CH1
или CH2, занимает оба канала и SMCR. Выбранный канал по нарастающему фронту
фиксирует период и сбрасывает CNT; парный по спадающему фиксирует длительность
HIGH. Для входа CH2 в примере меняются местами каналы чтения. DMA обоим каналам
не назначают. Необязательный callback уведомляет о фронте периода, данные в RAM
не копируются. Два чтения CCR не являются атомарной парой; первый цикл,
переполнение, пропавшие фронты и overcapture обрабатывает приложение. Остальные
каналы используют тот же сбрасываемый CNT, независимой временной базы у них нет.

### ITR и функции общей базы

После обычного SetUp функция SetUpSlave(&source, SlaveMode::Reset/Gated/Trigger/
ExternalClock) однократно настраивает SMCR при остановленных таймерах. У источника
заранее настраивают SetUpTrigger; его настройка/запуск остаются у приложения.
ExternalClock сохраняет PSC: один шаг CNT приходится на PSC+1 фронтов источника.
GetCounterClock показывает внутреннюю базу/PSC, а не частоту внешних событий.
SetUpOnePulse до Start однократно задаёт OPM: аппаратный Update снимает CEN.
Эти функции относятся ко всему таймеру.

ITR0..ITR3 используют обычное отображение TRGO с учётом наличия таймеров в CMSIS.
Слоты с прямым OC1 и переназначенные входы не выдаются за TRGO. Неподдерживаемый
маршрут возвращает InitError. У F410 поддерживается TIM5 → TIM1; вход LPTIM
таймера TIM5 к этому API не относится. Источник сохраняют настроенным всё время
работы приёмника. [RM0401, tables 59/63](https://www.st.com/resource/en/reference_manual/rm0401-stm32f410-advanced-armbased-32bit-mcus-stmicroelectronics.pdf),
[RM0444, tables 119/123/130](https://www.st.com/resource/en/reference_manual/rm0444-stm32g0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).

### Конечная серия импульсов

**1. RCR + OPM on one timer / RCR + OPM одного таймера**

```cpp
constinit TIM burst(TIM::_1::instance, {TIM::_1::CH1::PA8});
if (burst.SetUpPulseBurst(TIM::Channel::CH1, 1000, 1000) == SysInitStatus::InitOK)
    burst.RunPulses(100); // Check SysStatus; starts the burst.
```

Режим занимает таймер целиком. PWM2 даёт HIGH длительностью floor(period/2),
LOW при CNT=0 и после конечного переполнения. RCR=count-1 и OPM останавливают
серию аппаратно, без подсчёта каждого импульса в IRQ; необязательный callback
вызывается по последнему Update. Служебный UG загружает регистры с URS,
его флаг очищается до Start. Нулевая длина не запускает серию. Максимум:
F4 — 256, F7 — 65536, G0 TIM1 — 65536, TIM15/16/17 — 256. Для отсутствующего
RCR/неверного диапазона возвращается ошибка. SetDuty/SetCCR не меняют этот режим.

**2. Second hardware counter / Второй аппаратный счётчик**

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

SetUpCounter заменяет обычный SetUp, закрепляет источник/период и необязательный
callback, задаёт PSC=0 и занимает CH1 под compare остановки. Источник уже должен
выдавать Update в TRGO. Без взведённой остановки Count работает без IRQ.
StopAfter/StopAt взводят один compare при остановленных обоих таймерах; цель
должна лежать впереди CNT в текущем периоде. Для новой серии при необходимости
сначала Reset счётчика. При взведении очищается старый CC1IF; завершение снимает
CC1IE, останавливает оба таймера и один раз вызывает необязательный callback.

**Остановка из IRQ имеет задержку:** до снятия CEN источник может выдать лишние
импульсы, последний может оказаться неполным. Остановка CNT не переводит выход
принудительно в LOW. Аппаратная обратная связь gated не добавлена; когда нужна
точная аппаратная остановка и есть подходящий RCR, используется первое решение.

---

## 12. Примеры использования

Отдельный пример; проверьте наличие блока/пинов в корпусе и каждый возвращаемый статус.

```cpp
#include "tim.hpp"
using T = TIM::_3;
constinit TIM pwm(T::instance, {T::CH1::PA6});
// In main, after System/RCC initialization:
if (pwm.SetUp(1000, nullptr, 1000) == SysInitStatus::InitOK
    && pwm.SetUpPWM(TIM::Channel::CH1, 500) == SysInitStatus::InitOK)
    pwm.Start();
// Other examples are in their mode sections: capture, compare, DMA,
// encoder, quadrature, pulse burst and a second hardware counter.
```

---

## 13. Угловые случаи

Каналы используют общую частоту/период счётчика. Период 16-битного PWM ограничен 65535 тиками для представления 100%. Остановка из IRQ может допустить лишние/неполные импульсы. Приложение маскирует нужные IRQ при изменении общего DIER. TC не означает, что последний PWM-период уже выдан.

---

## См. также

- [ST F4 LL TIM — PWM modes, preload, CCDS](https://github.com/STMicroelectronics/stm32f4xx-hal-driver/blob/master/Inc/stm32f4xx_ll_tim.h).
- [ST G0 LL TIM — CC DMA selection](https://github.com/STMicroelectronics/stm32g0xx-hal-driver/blob/master/Inc/stm32g0xx_ll_tim.h).
- [ST AN4031 — DMA requests and stopping F4/F7 streams](https://www.st.com/resource/en/application_note/dm00046011.pdf).
- [ST AN5224 — DMAMUX request routing](https://www.st.com/resource/en/application_note/an5224-stm32-dmamux-the-dma-request-router-stmicroelectronics.pdf).
- [RM0383 — F411 DMA maps](https://www.st.com/resource/en/reference_manual/DM00119316.pdf).
- [RM0385 — F74/F75 DMA maps](https://www.st.com/resource/en/reference_manual/dm00124865.pdf).
- [RM0410 — F76/F77 DMA maps](https://www.st.com/resource/en/reference_manual/rm0410-stm32f76xxx-and-stm32f77xxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).
- [RM0444 — G0 DMAMUX map](https://www.st.com/resource/en/reference_manual/rm0444-stm32g0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).

- [Initialization.md](Initialization.md)
- [GPIO.md](GPIO.md)
- [DMA.md](DMA.md)
- [ADC.md](ADC.md)
- [IRQ_Registry.md](IRQ_Registry.md)
