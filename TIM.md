# TIM — Detailed Description

*[English](#english) · [Русский](#русский)*

---

## English

## Contents

1. [Why the Base Class Is Protected](#1-why-the-base-class-is-protected)
2. [PeriphInfo — A Facts Table for Each Timer](#2-periphinfo--a-facts-table-for-each-timer)
3. [SetFrequency() — PSC/ARR Calculation, and Why It's protected](#3-setfrequency--pscarr-calculation-and-why-its-protected)
4. [IRQ_en() and the Shared/Separate Vector](#4-irq_en-and-the-sharedseparate-vector)
5. [TIM::HandleIRQ() — The Base for All Subclasses](#5-timhandleirq--the-base-for-all-subclasses)
6. [DeInit() — Switching Modes on a Single Physical Timer](#6-deinit--switching-modes-on-a-single-physical-timer)
7. [Subclasses](#7-subclasses)
   - [7.1 TIM_PeriodicIRQ](#71-tim_periodicirq)
   - [Generators](#generators--produce-an-output-signal)
     - [7.2 TIM_PWM](#72-tim_pwm)
     - [7.3 TIM_EncoderGenerator](#73-tim_encodergenerator)
     - [7.4 TIM_StepGenerator](#74-tim_stepgenerator)
   - [Measurers](#measurers--readdecode-an-input-signal)
     - [7.5 TIM_InputCapture](#75-tim_inputcapture)
     - [7.6 TIM_PulseMeasure](#76-tim_pulsemeasure)
     - [7.7 TIM_EncoderReader](#77-tim_encoderreader)
     - [7.8 TIM_HWCounter](#78-tim_hwcounter)
8. [ITR / TRGO — Cascading Timers in Hardware](#8-itr--trgo--cascading-timers-in-hardware)
9. [Usage Examples](#9-usage-examples)
10. [Edge Cases](#10-edge-cases)

---

## 1. Why the Base Class Is Protected

`TIM` supplies clock/reset lookup, PSC/ARR calculation, start/stop and timer IRQ
registration. Its constructor stays protected; select a concrete `TIM_*` mode.
One initialized object owns a physical TIMx. Configure from main, serialize
control calls and keep the object alive while it is registered. Callbacks run
in IRQ and must not configure timers or modify the IRQ registry.

The common API is revised as of 18.10.2026. PWM, encoders, capture, pulse
measurement, HWCounter and TriggerGenerator remain separate review steps;
see [Review/TIM.md](Review/TIM.md). Compiling the source does not validate those
mode algorithms or their DMA paths.

`TIM_PIN` is defined before `TIM` and exposed as `TIM::TIM_PIN`. Its consteval
constructor checks GPIO port/pin/AF, channel range 0..3 and timer presence.
Named constants retain `TIM::_N::CHx::Pyz` spelling. Port/timer guards omit
unavailable declarations. Existing AF routes are retained; package bonding and
exact routing remain the caller's responsibility. Descriptor copies retain
the old assignable layout for mode constructors; no new runtime checker is added.

## 2. PeriphInfo — A Facts Table for Each Timer

`tim_table[]` is a static const table with one row per available peripheral:

| Field | Purpose |
|---|---|
| periph | Constructor's fixed TIMx |
| clk_reg / clk_bit | Enable the peripheral clock |
| rst_reg / rst_bit | Reset this timer |
| bus_clk | Pointer to System::TIMxAPB1Clock / TIMxAPB2Clock |
| irq_up / irq_cc | Update and CC vectors; may be the same |
| has_bdtr | Presence of the output gate used by existing modes |
| channel_count | Number of described CC channels, 0/1/2/4 |
| arr_max | Largest hardware ARR register value |
| dma_up_req | Existing G0 Update request; PWM route revision is separate |

F4/F7 use APB1/APB2. G0 uses its APB timer clock. RCC/IRQ names are selected
from the actual CMSIS device: F410 has TIM1_UP; F412 has TIM6 without DAC;
G0B/C have a shared TIM3/TIM4 vector and some share TIM16/17 with FDCAN.
Basic TIM6/TIM7 have no CC channels. TIM2/TIM5 on F4/F7, and TIM2 where
present on G0, have 32-bit counters; the remaining described counters are 16-bit.

`Init()` rejects an already initialized object, enables/reset the selected
peripheral, registers the update vector and a distinct CC vector once each,
then enables NVIC with DIER still zero. Failed registration removes only its
own insertion and restores prior NVIC enable states. `_info` is published
only on success; a later mode setup failure must roll back through DeInit.
A repeated SetUp returns InitError without replacing callbacks or hardware
configuration. A failed periodic frequency setup is rolled back and can retry.

## 3. SetFrequency() — PSC/ARR Calculation, and Why It's protected

```cpp
SysInitStatus TIM::SetFrequency(uint32_t freq, uint32_t arr = 0); // protected
```

`freq` is requested Hz. `arr` is **period ticks**, so register ARR is arr-1.
Zero requests automatic period selection. Calculation uses positive integer
values and truncating 32-bit divisions:

```cpp
ratio = timer_clock / freq;
// Explicit period:
psc = ratio / arr;
// Automatic period on 16-bit timers:
psc = (ratio - 1) / 65536 + 1;
arr = ratio / psc;
// Automatic period on 32-bit timers: psc = 1, arr = ratio.
```

PSC register is psc-1. The sequential division in the explicit case equals
floor(clock/(freq*arr)) without forming an overflowing product or doing a
64-bit divide. Reject zero/above-clock frequency, zero resulting period,
PSC outside 1..65536 or ARR register overflow. A 16-bit timer accepts period
**65536**, not just 65535. The uint32_t API represents 32-bit periods up to
UINT32_MAX, not 2^32. Integer truncation may change actual frequency; no exact
divisor search or new rounding policy is introduced. Rejected calls write nothing.

When stopped, UG loads the preload before Start. URS is temporarily set so
this service update does not request IRQ/DMA; only UIF is acknowledged and
CR1 is restored. UG can still be visible on TRGO: configure frequency before
arming ADC or slave timers. This follows the preload/URS behavior described
in [ST AN4013](https://www.st.com/resource/en/application_note/an4013-introduction-to-timers-for-stm32-mcus-stmicroelectronics.pdf).

A running timer is not stopped/reset by SetFrequency: PSC applies on a later
update and ARR follows the existing ARPE setting. No atomic or glitch-free
live change is promised. Public mode wrappers keep their existing contracts:
PeriodicIRQ exposes the base function, PWM may retain its period, and Step
also adjusts its compare value. Those mode-specific details are reviewed later.

## 4. IRQ_en() and the Shared/Separate Vector

`IRQ_en()` changes only the selected DIER bit; unavailable CC channels are
ignored. It neither registers nor unregisters handlers and does not change
NVIC. Registration lasts from common Init to DeInit. Disabling one source
preserves other timers on shared vectors and is permitted from a callback.
Application code must not register an initialized timer again.

## 5. TIM::HandleIRQ() — The Base for All Subclasses

The common handler takes one snapshot of SR & DIER for supported Update/CC1..4
events and clears exactly that snapshot with `SR = ~flags` (rc_w0). Disabled
polling flags and unrelated event flags are retained. It then invokes only
corresponding non-null function pointers. No locks, dispatch-time registration
or new callback adapters are added.

Callbacks are supplied during initialization: the Update function in SetUp,
the CC function in SetCompareIRQ. Public SetUpdateCallback/SetCCCallback have
been removed. Initialize compare events before Start; an assigned non-null
CC callback cannot be replaced until DeInit. A null callback binds no function,
while the IRQ source may still be enabled. Capture/HWCounter custom handlers
remain a separate review item; this paragraph describes the common handler.

```cpp
void Tick();
void MidPeriod();
TIM_PeriodicIRQ timer(TIM1);
if (timer.SetUp(1000, &Tick) == SysInitStatus::InitOK) {
    if (timer.SetCompareIRQ(TIM::Channel::CH1, timer.GetPeriod() / 2,
                           &MidPeriod) == SysInitStatus::InitOK)
        timer.Start();
}
```

## 6. DeInit() — Switching Modes on a Single Physical Timer

The public name is now DeInit, matching USART. From main it disables its
sources/stops the counter, unregisters only this object on timer vectors,
resets/disables the peripheral and clears `_info`/callbacks. Other shared
handlers retain their previous NVIC enable state; an empty vector remains
disabled and its pending bit is cleared. An uninitialized object does nothing.
The same object can then SetUp again. One physical timer must not be configured
through several live objects at once.

Mode-specific PWM DMA stop/unregister/release remains TIM-6, a separate step.
Before calling the current base DeInit, attached DMA must be stopped/released
by its owner. Base teardown is not a claim of complete PWM DMA cleanup.

```cpp
timer.Stop();
timer.DeInit();
if (timer.SetUp(2000, &Tick) == SysInitStatus::InitOK)
    timer.Start();
```

---

## 7. Subclasses

| Class | Group | Purpose | Own `HandleIRQ()`? |
|-------|--------|------------|----------------------|
| `TIM_PeriodicIRQ` | — | Periodic interrupt + extra events on CC channels without a pin | No — uses the base one |
| `TIM_PWM` | Generator | PWM on 1-4 channels, optionally with DMA on CCR | Yes — DMA TC + base |
| `TIM_EncoderGenerator` | Generator | Quadrature pulse generator (encoder emulation) | Yes — remaining-pulse counter |
| `TIM_StepGenerator` | Generator | 50%-duty STEP pulsing (STEP/DIR drive) | Yes — step counter increment |
| `TIM_InputCapture` | Measurer | Edge capture of CCRx on a single channel | Yes — one channel instead of all four |
| `TIM_PulseMeasure` | Measurer | Period + pulse width (PWM input, reset mode) | No — purely hardware, no callback |
| `TIM_EncoderReader` | Measurer | Hardware reading of a real quadrature encoder | No — purely hardware, no interrupts |
| `TIM_HWCounter` | Measurer | Hardware counter of another timer's pulses via ITR, no CPU | Yes — auto-stop on reaching the target |

### 7.1 TIM_PeriodicIRQ

```cpp
SysInitStatus SetUp(uint32_t freq, void (*cb)(void) = nullptr, uint32_t arr = 0);
using TIM::SetFrequency;   // public access to the base method, see §1/§3
SysInitStatus SetCompareIRQ(TIM::Channel ch, uint32_t compare, void (*cb)(void));
```

`SetUp()` is `SetFrequency(freq, arr)` + a callback on Update (`arr=0` — as usual, auto-picked
resolution). `SetCompareIRQ()` adds an **extra** event on a CC channel without occupying a GPIO:
the channel stays in Frozen mode (`OCxM` = 0, the default reset value) — `CCxIF` is set purely by
the `CNT == CCRx` match, independent of `OCxM`/`CCxE`, so no pin is needed at all. Several channels
mean several independent moments within one period on one timer, without needing a separate timer
for every schedule.

```cpp
TIM_PeriodicIRQ tick(TIM3);
tick.SetUp(1000, &Tick);                                  // 1 kHz Update — SetUp() picks PSC/ARR itself
tick.SetCompareIRQ(TIM::Channel::CH1, tick.GetPeriod() / 2, &HalfTick); // + an event in the middle of the period
tick.Start();                                              // SetUp() does NOT start the timer
```

---

## Generators — Produce an Output Signal

### 7.2 TIM_PWM

```cpp
SysInitStatus SetUp(uint32_t freq, uint32_t arr = 1000);
SysInitStatus SetFrequency(uint32_t freq, uint32_t arr = 0);  // arr=0 = keep the current period, see §3
void SetDuty(TIM::Channel ch, uint32_t percent);   // 0-100%, scaled to the actual ARR
void SetCCR(TIM::Channel ch, uint32_t val);        // direct write to CCRx
void AttachDMA(DMA_Sx* dma, TIM::Channel ch);
SysStatus SendDMA(TIM::Channel ch, const uint16_t* data, uint32_t len);
Output GetOutput(TIM_PIN pin);   // a handle to one channel, see below
```

Each channel is configured in PWM mode 1 (`OCxM = 6`) **with mandatory preload** (`OCxPE = 1`):

> Preload is mandatory for DMA-driven output: without it, the DMA write arrives roughly 3 cycles
> after the UPDATE event (AHB latency) and lands in CCR already mid-period — this clips the
> leading edge of the first bit on every CCR value change. With preload enabled, DMA writes to
> the shadow register, and the value is applied atomically on the next UPDATE.

`AttachDMA()`/`SendDMA()` let you change `CCRx` every period via DMA without CPU involvement — a
typical case: generating the WS2812B protocol, where each bit is encoded by its own high-phase
duration. Multiple channels are attached independently, each getting its own DMA stream; the
shared `TIM_DIER_UDE` is enabled on the first active channel and left untouched while the others
are still running.

**`Output`** is a lightweight handle to one specific channel (`_pwm` + `Channel`), obtained via
`GetOutput(pin)`. It solves a specific problem: without it, calling code would have to store a
bare `TIM::Channel` somewhere itself and not mix up which `TIM_PWM` it belongs to — `Output`
stores both facts together, so `output.SetDuty(50)` physically cannot be applied to the wrong
object. `GetOutput()` takes the same `TIM::_N::CHx::Pyz` already passed to the constructor — the
channel isn't typed in manually a second time; it traps if the channel wasn't configured.

```cpp
TIM_PWM ws_tim(TIM1, TIM::_1::CH1::PA8, TIM::_1::CH2::PA9, TIM::_1::CH3::PA10);
ws_tim.SetUp(800'000, 79);           // 800 kHz, ARR=79 (tuned to the specific MCU's clock)
ws_tim.AttachDMA(&dma_ch1, TIM::Channel::CH1);
ws_tim.SendDMA(TIM::Channel::CH1, bit_pattern, n_bits);

TIM_PWM::Output red = ws_tim.GetOutput(TIM::_1::CH1::PA8);
red.SetDuty(50);   // from here on the channel can never be mixed up
```

### 7.3 TIM_EncoderGenerator

```cpp
TIM_EncoderGenerator step(TIM3, TIM::_3::CH1::PA6, TIM::_3::CH2::PA7);   // A, B (90°)
step.SetUp(/*freq=*/1000, /*period=*/999, /*ch1_width=*/500, /*ch2_width=*/250);
step.SetCompleteCallback(&OnDone);
step.GenPulse(200);    // 200 pulses forward; a negative value — backward
```

Not to be confused with `TIM_EncoderReader` (§7.7) — this class **emulates** a quadrature signal
(for example, for testing code that reads an encoder), rather than reading a real one. Both
channels are in toggle mode (`OCxM = 0b011`) — the output inverts on every compare match,
independent of `CCRx`, which produces the phase shift between A and B, set by the difference
between `ch1_width`/`ch2_width`. Direction isn't a separate register but an inversion of `CC1P`
polarity before start (`GenPulse(pulses > 0)` clears `CC1P`, `< 0` sets it and flips the sign).
`HandleIRQ()` is overridden here to simply decrement the software counter `_pulses` in the Update
handler — when it reaches 0, the timer stops and `_on_complete` is called. This is a software
(not RCR-based) way to limit the number of pulses — unlike `TIM_StepGenerator::RunSteps()` (§7.4),
there's no hardware auto-stop here, an ISR is required on every period.

### 7.4 TIM_StepGenerator

```cpp
TIM_StepGenerator step(TIM3, TIM::_3::CH1::PA6);
step.SetUp(1000, /*count_in_isr=*/true);   // 1 kHz, count steps in the ISR
step.Start();
uint32_t done = step.GetStepCount();
```

Uses **PWM mode 1** (`OCxM = 6`), not toggle mode: `CCR` is fixed at `ARR/2` (recomputed in
`SetStepFrequency()` on every frequency change), so one full `ARR` period is exactly one STEP
pulse (HIGH for the first half, LOW for the second). This means the timer's Update event matches
the step frequency 1-to-1 — `SetStepFrequency(freq)` calls `SetFrequency(freq)` directly, with no
doubling; `RunSteps()` sets `RCR = steps - 1`; `GetStepCount()` returns the counter as-is, with no
`>> 1`. A toggle-based scheme would require 2 Update events per pulse (rising+falling as separate
transitions) — doubling/halving would be needed everywhere for no gain: PWM mode gives the same
50%-duty accuracy via `ARR/2` as toggle does. `OC1PE` (CCR preload) is enabled together with
`TIM_CR1_ARPE` (ARR preload) so that on a live frequency change the new `ARR` and `CCR` apply
synchronously on the next Update, rather than `CCR` immediately and `ARR` only after a full period
(PSC is already always buffered in hardware).

There are two ways to count elapsed steps, chosen in `SetUp()`:

- **`count_in_isr = true`** — the Update IRQ is enabled, `HandleIRQ()` increments `_step_count`
  directly (1 Update = 1 step). A simple solution, but one interrupt per step — noticeable at tens
  of kHz.
- **`count_in_isr = false`** — the interrupt isn't enabled at all; instead,
  [`TIM_HWCounter`](#78-tim_hwcounter) is used, counting this timer's Update events via ITR/TRGO
  in hardware, with no ISR at all — for high step frequencies. `TIM_HWCounter::Count()` is then
  also 1:1 with steps — the `*2`/`/2` conversion needed under the old toggle mode is no longer
  needed anywhere.

**`RunSteps(steps)`** — a one-shot hardware run for an exact number of pulses, with no ISR
whatsoever needed to track completion:

```cpp
uint32_t events = steps - 1u;         // PWM: 1 event per step
TIMx->RCR  = events;                  // Repetition Counter
TIMx->CR1 |= TIM_CR1_OPM;             // One Pulse Mode — CEN clears itself when RCR is exhausted
TIMx->EGR  = TIM_EGR_UG;              // force reloading RCR from preload before the first period
```

Requires `has_bdtr == true` (the Repetition Counter physically exists only on
advanced/semi-advanced timers — `TIM1`/`TIM8` on F4, `TIM1`/`TIM16`/`TIM17` on G0) — otherwise
`InitError`. An important property: `RCR` counts **elapsed periods**, not time, so changing speed
via `SetStepFrequency()` between `RunSteps()` calls doesn't corrupt the final number of generated
pulses even when accelerating/decelerating mid-motion.

> The RCR arithmetic (`steps - 1`) hasn't been verified on real hardware — before relying on it
> for precise positioning, it's worth checking the actual number of pulses with an oscilloscope on
> the first run on a new board.

If RCR isn't available on this timer (for example, STEP is generated on `TIM3`) — use
`TIM_HWCounter::StopAfter()` on a second timer chained via ITR (§7.8).

---

## Measurers — Read/Decode an Input Signal

### 7.5 TIM_InputCapture

```cpp
TIM_InputCapture(TIM_TypeDef* timx, TIM::Line input);   // pin + channel are already linked, not two separate arguments
SysInitStatus SetUp(uint32_t max_freq, void (*cb)(void) = nullptr);
uint32_t GetCapture() const;
```

```cpp
TIM_InputCapture cap(TIM3, TIM::_3::CH1::PA6);
cap.SetUp(1'000'000, &OnEdgeCaptured);  // or without a callback — polling via GetCapture()
```

`CCxS` maps to the channel's **own** TI input (`0b01`) — a direct capture, with no cross-routing
between channels (that's what `TIM_PulseMeasure` does, see §7.6). Its own `HandleIRQ()` checks
only one `SR` bit (the channel capture is actually configured on), not all four — unlike the base
class, here it's known in advance that the other three are meaningless for this object.

### 7.6 TIM_PulseMeasure

```cpp
TIM_PulseMeasure pm(TIM3, TIM::_3::CH1::PA6);   // the channel must be CH1 or CH2
pm.SetUp(1'000'000);
uint32_t width  = pm.GetWidth();   // CCR1
uint32_t period = pm.GetPeriod();  // CCR2
```

Uses the hardware **Reset mode** (`SMS = 0b100`) with CCMR cross-routing: direct capture on the
chosen channel (`CC1S = 01`), inverted on the other (`CC2S = 10`, the same physical pin, but an
inverted input). Every edge at "its own" polarity (`CC1P`) resets the counter via `TS`/`SMS`, so
`CCR1` captures the pulse width and `CCR2` the full period, both updated in hardware every signal
cycle with no interrupt at all. There's no user callback — a purely hardware measurer, readable
via `GetWidth()`/`GetPeriod()` at any moment.

### 7.7 TIM_EncoderReader

```cpp
TIM_EncoderReader enc(TIM3, TIM::_3::CH1::PA6, TIM::_3::CH2::PA7);   // strictly CH1+CH2
enc.SetUp();                          // Mode::X4 by default — count all edges
enc.Start();

uint32_t pos = enc.Count();
bool     rev = enc.IsCountingDown();  // determined by hardware, read-only
```

Not to be confused with `TIM_EncoderGenerator` (§7.3) — this class **reads** a real quadrature
encoder (Encoder Interface mode, `SMS = 001/010/011`), rather than emulating one. `CNT` is
incremented/decremented in hardware on CH1/CH2 edges, direction (`CR1.DIR`) is set in hardware
from the real signal — not a single interrupt, not a single CPU cycle.

Requires **specifically CH1 and CH2** — Encoder Interface mode on every STM32 timer that has it
is hard-wired to TI1/TI2 (i.e., physically CH1/CH2); CH3/CH4 can't be used in principle, regardless
of which pins are passed. The constructor traps if the channels are swapped or taken from anywhere
other than CH1/CH2 — silently remapping them would be incorrect.

`IsCountingDown()` is **read-only** — unlike `TIM_HWCounter::SetDirection()` (there it's a command
from software), here the direction is decided entirely by hardware from the real signal; there's
deliberately no setter.

`Mode` — how many edges count as one "click":

| Mode | SMS | What Counts |
|------|-----|----------------|
| `X2_CH1` | `0b01` | Only CH1 edges, direction from the CH2 level |
| `X2_CH2` | `0b10` | Only CH2 edges, direction from the CH1 level |
| `X4` (default) | `0b11` | Edges of both channels — 4x resolution |

`ARR` is set to `_info->arr_max` (the maximum range before wraparound) — the same thing
`TIM_HWCounter::SetUp()` does for its own free-running count.

### 7.8 TIM_HWCounter

```cpp
SysInitStatus SetUp(TIM& master, void (*on_complete)(void) = nullptr);
void SetDirection(bool down);
bool IsCountingDown() const;
SysInitStatus StopAfter(uint32_t ticks);   // relative: "current CNT + ticks"
SysInitStatus StopAT(uint32_t ticks);      // absolute: a specific CNT value
void CancelStopAfter();
uint32_t Count() const;
void Reset();
```

`SetUp(master)` enables `master.EnableTriggerOutput()` (`MMS = 010`, TRGO on Update) and puts
**this** timer into External Clock Mode 1 (`SMS = 0b111`) with the `TS` source set to the found
ITR index — after this, `CNT` is incremented in hardware on every one of the master's Update
events, with no CPU cycles at all, at any frequency the master is able to generate. `SetUp()` also:

- remembers `master` (needed for auto-stop) and **binds `on_complete` for the object's entire
  lifetime** — the callback isn't passed again on every `StopAfter()`/`StopAT()`, there's one per
  object;
- **unconditionally** enables the CH1 channel interrupt (even if `on_complete == nullptr`) — the
  master's auto-stop is useful even without a callback, the same convention as
  `TIM_PeriodicIRQ::SetCompareIRQ()`.

**`SetDirection(down)`** — an explicit, independent command: count up or down. It's not connected
to the stop target at all — it simply determines how the next `StopAfter()` is interpreted.

**`StopAfter(ticks)`** and **`StopAT(ticks)`** don't reinitialize the coupling with the master
(only `SetUp()` does that) — they simply reposition the comparator:

```cpp
// StopAfter: relative to the current position, accounting for direction
uint64_t period = uint64_t(_info->arr_max) + 1;
uint32_t target = IsCountingDown()
    ? (TIMx->CNT + period - ticks) % period
    : (TIMx->CNT + ticks) % period;
*ccr(0) = target;          // CH1, purely as a comparator — no pin is occupied
IRQ_en(IRQ::CC1, ENABLE);  // re-enable — in case CancelStopAfter() turned it off
```

`StopAT(ticks)` does the same thing, but with `target = ticks` directly — independent of both the
current `CNT` and direction; handy when position is tracked in absolute coordinates ("go to
5000") rather than as a series of relative moves. Both methods can be called at any time — while
running or before `Start()` — to retarget an already-moving counter, for example when a new motion
command arrives.

On `CC1IF` firing, `HandleIRQ()` stops **`master`**, but not itself, and doesn't disable `CC1IE`
— the counter keeps living as a position (naturally "freezing" once the master stops, since ticks
no longer arrive), ready for the next `StopAfter()`/`StopAT()` without a repeated `SetUp()`.
`CancelStopAfter()` is the only way to explicitly disable `CC1IE`; the next `StopAfter()`/
`StopAT()` turns it back on by itself.

```cpp
TIM_StepGenerator step(TIM3, TIM::_3::CH1::PA6);
step.SetUp(5000, false);            // no ISR on the STEP timer itself

TIM_HWCounter counter(TIM1);        // any timer with a free ITR route to TIM3
counter.SetUp(step, &OnMoveDone);   // coupling + callback — once
counter.SetDirection(false);        // forward — a separate explicit command
counter.StopAfter(800);             // 800 steps (master's PWM mode: 1:1 with ticks)
step.Start();

// a new motion command arrived mid-move — just retarget:
counter.StopAfter(300);             // another 300 steps from the current CNT
counter.StopAT(5000);               // ...or straight to absolute position 5000
```

---

## 8. ITR / TRGO — Cascading Timers in Hardware

```cpp
struct ITR_Route { TIM_TypeDef* master; TIM_TypeDef* slave; uint8_t itr; };
static const ITR_Route itr_table[];
static uint8_t FindITR(TIM_TypeDef* master, TIM_TypeDef* slave);  // 0xFF = route not found/not confirmed
```

The internal Internal Trigger bus (`ITR0`-`ITR3`) connects one timer's TRGO to another's Slave
Mode Controller (`SMCR`/`TS`) — this is what `TIM_HWCounter` (§7.8) is built on. The `itr_table`
lists **pairs confirmed against the Reference Manual** `(master, slave, itr)`; `FindITR()` is a
linear search, `0xFF` means "there's no verified entry for this pair, treat it as unsupported" —
so `TIM_HWCounter::SetUp()` refuses to work instead of silently connecting to the wrong ITR index.

On F4 the table is filled in for `TIM1/2/3/4/5/8` (the only timers with a full-featured SMCR per
RM0090). On STM32G0 both entries (`TIM3→TIM1`, `TIM1→TIM3`) deliberately carry `itr = 0xFF` —
they haven't been checked against RM0444 yet, so `TIM_HWCounter` on G0 currently always returns
`InitError` until someone confirms and sets the real index.

```text
TIM_StepGenerator (master, STEP on its own CH)  ──TRGO (Update)──▶  ITRx  ──▶  TIM_HWCounter (slave)
        EnableTriggerOutput(): MMS=010                 SMCR: TS=itr, SMS=111 (External Clock Mode 1)
```

F7 now has the common peripheral table and compiles. Its HWCounter routes
remain unsupported: FindITR returns 0xFF until that mode is reviewed. G0
routes also remain unverified; existing F4 routes are filtered by availability.

---

## 9. Usage Examples

Expanded examples for each subclass are given directly in the source — see the doc comments
above each class in [tim.hpp](src/tim.hpp). A "what to choose" summary:

| Task | Class |
|--------|-------|
| Periodic tick + extra events without a pin | `TIM_PeriodicIRQ` |
| PWM (motors, LEDs, WS2812B) | `TIM_PWM` (+ `AttachDMA` for protocols like WS2812B) |
| Emulate a quadrature signal (tests) | `TIM_EncoderGenerator` |
| STEP/DIR pulses, low frequency, software counting | `TIM_StepGenerator(count_in_isr=true)` |
| STEP/DIR pulses, high frequency, no ISR on STEP | `TIM_StepGenerator(false)` + `TIM_HWCounter` |
| Measure the frequency/period of an input signal by edges | `TIM_InputCapture` |
| Measure both period AND duty cycle of a single PWM signal | `TIM_PulseMeasure` |
| Read a real quadrature encoder | `TIM_EncoderReader` |
| An exact number of steps without an ISR per step | `RunSteps()` (needs RCR) or `TIM_HWCounter::StopAfter()`/`StopAT()` (doesn't need it) |
| Switch one physical timer between modes without a reboot | `DeInit()` (§6) |

---

## 10. Edge Cases

- **`TIM6`/`TIM7` (F4) — 0 channels** (`channel_count = 0`): suitable only for
  `TIM_PeriodicIRQ` (the Update event) — all other subclasses require at least one CC channel and
  either won't compile (no required pin table) or `SetUp()` will return `InitError`.
- **16-bit vs. 32-bit ARR** — `TIM2`/`TIM5` on F4/F7 and present `TIM2` on G0 have `arr_max = 0xFFFFFFFF`; all
  others are limited to `0xFFFF`. `SetDuty()` warns in its doc comment: for ARR values near the
  32-bit limit, use `SetCCR()` directly, not percentages.
- **`has_bdtr`** — not just "an advanced timer": it's the single indicator of the presence of
  `BDTR`/`MOE` (needed for `TIM_PWM` on some timers — without `MOE` the outputs physically aren't
  activated) and `RCR` (needed for `TIM_StepGenerator::RunSteps()`).
- **`TIM_HWCounter` on STM32G0/F7 is always `InitError`** — the `itr_table` isn't confirmed (see
  §8); this isn't a bug in a specific call, it's a deliberately incomplete table.
- **`TIM_HWCounter::on_complete` is bound to `SetUp()`, not to `StopAfter()`/`StopAT()`** — one
  callback for the object's entire lifetime; it can only be changed by calling `SetUp()` again
  (which re-couples the ITR route — not the same thing as just changing the target).
- **`TIM_EncoderReader` requires specifically CH1+CH2** — a hardware limitation of Encoder
  Interface mode, not a choice made by this driver; the constructor rejects CH3/CH4 with a trap.
- **`Init()`/`SetFrequency()` are now `protected`** — calling `pwm.SetFrequency(...)` or
  `step.SetFrequency(...)` directly no longer compiles on objects where it would be wrong; instead
  `TIM_PWM` and `TIM_StepGenerator` have their own public wrappers (see §3), and `TIM_PeriodicIRQ`
  exposes the base method via `using`.
- **`TIM_StepGenerator::SetStepFrequency()` resets `RunSteps()` mode** — it explicitly clears
  `TIM_CR1_OPM`, returning the timer to free-running (continuous) mode; calling it after
  `RunSteps()` cancels the expected auto-stop.
- **A shared Update+CC vector on most general-purpose timers** — `Init()` does not register the
  same vector twice (the `irq_cc != irq_up` comparison), but this means `HandleIRQ()` for such
  timers is called on **any** of the Update/CC1-4 events — the code inside always figures out for
  itself which `SR` bits are actually set (see §5).

---

## See Also

- [IRQ_Registry.md](IRQ_Registry.md) — `Init()` (§4) registers `this` the same way
  `USART`/`SPI`/`RTC` do; see there also for the shared/separate vector on a single `_table` row.
- [DMA.md](DMA.md) — `TIM_PWM::AttachDMA()` (§7.2) configures `DMA_Sx` the same way `USART`/`SPI`
  do.
- [RCC.md](RCC.md) — `_info->bus_clk`/`clk_reg`/`rst_reg` (§2-3, §6) point to the registers and
  variables this module itself writes (including the "timers x2" rule for the clock frequency).
- [GPIO.md](GPIO.md) — `TIM_PIN`/`Line` (tim.hpp + tim_defs.hpp) are built on top of the same `PIN` as the
  other peripheral pin tables.
- [System.md](System.md) — the common infrastructure (`SysStatus`/`SysInitStatus`, `DebugTrap`)
  used by all `SetUp()` methods in this file.

---

## Русский

## Содержание

1. [Зачем базовый класс защищён](#1-зачем-базовый-класс-защищён)
2. [PeriphInfo — таблица фактов о каждом таймере](#2-periphinfo--таблица-фактов-о-каждом-таймере)
3. [SetFrequency() — расчёт PSC/ARR, и почему он protected](#3-setfrequency--расчёт-pscarr-и-почему-он-protected)
4. [IRQ_en() и общий/раздельный вектор](#4-irq_en-и-общийраздельный-вектор)
5. [TIM::HandleIRQ() — база для всех подклассов](#5-timhandleirq--база-для-всех-подклассов)
6. [DeInit() — переключение режима на одном физическом таймере](#6-deinit--переключение-режима-на-одном-физическом-таймере)
7. [Подклассы](#7-подклассы)
   - [7.1 TIM_PeriodicIRQ](#71-tim_periodicirq)
   - [Generators](#generators--производят-выходной-сигнал)
     - [7.2 TIM_PWM](#72-tim_pwm)
     - [7.3 TIM_EncoderGenerator](#73-tim_encodergenerator)
     - [7.4 TIM_StepGenerator](#74-tim_stepgenerator)
   - [Measurers](#measurers--читаютдекодируют-входной-сигнал)
     - [7.5 TIM_InputCapture](#75-tim_inputcapture)
     - [7.6 TIM_PulseMeasure](#76-tim_pulsemeasure)
     - [7.7 TIM_EncoderReader](#77-tim_encoderreader)
     - [7.8 TIM_HWCounter](#78-tim_hwcounter)
8. [ITR / TRGO — каскадирование таймеров в железе](#8-itr--trgo--каскадирование-таймеров-в-железе)
9. [Примеры использования](#9-примеры-использования)
10. [Угловые случаи](#10-угловые-случаи)

---

## 1. Зачем базовый класс защищён

`TIM` даёт поиск тактирования/сброса, расчёт PSC/ARR, запуск/остановку и
регистрацию IRQ. Конструктор защищён: создаётся нужный конкретный TIM_*.
Один настроенный объект владеет физическим TIMx. Настройка и управление
выполняются последовательно из main; объект живёт всё время регистрации.
Callbacks исполняются в IRQ и не перенастраивают таймер/реестр обработчиков.

Общая часть исправлена 18.10.2026. PWM, энкодеры, capture, измеритель импульсов,
HWCounter и TriggerGenerator разбираются отдельными шагами — [Review/TIM.md](Review/TIM.md).
Компиляция файла не подтверждает алгоритмы этих режимов или их DMA-пути.

`TIM_PIN` определён перед TIM и доступен как TIM::TIM_PIN. Конструктор consteval
проверяет GPIO/номер/AF, диапазон канала 0..3 и наличие таймера. Имена
TIM::_N::CHx::Pyz сохранены. Guards исключают отсутствующие порты/таймеры.
Прежние AF-маршруты сохранены; доступность в корпусе и точный маршрут проверяет
вызывающий. Копии дескрипторов пока сохраняют присваиваемую компоновку для
существующих конструкторов режимов; новых runtime-проверок конструктора нет.

## 2. PeriphInfo — таблица фактов о каждом таймере

`tim_table[]` — static const с одной строкой на существующий экземпляр:

| Поле | Назначение |
|---|---|
| periph | TIMx, закреплённый конструктором |
| clk_reg / clk_bit | Включение тактирования |
| rst_reg / rst_bit | Сброс своего таймера |
| bus_clk | Указатель на System::TIMxAPB1Clock / TIMxAPB2Clock |
| irq_up / irq_cc | Векторы Update/CC, могут совпадать |
| has_bdtr | Наличие выходного gate, используемого текущими режимами |
| channel_count | Число описанных CC-каналов: 0/1/2/4 |
| arr_max | Максимальное аппаратное значение регистра ARR |
| dma_up_req | Прежний G0 Update request; маршруты PWM DMA разбираются отдельно |

F4/F7 используют APB1/APB2, G0 — свою APB-частоту таймеров. Имена RCC/IRQ
выбираются по CMSIS: у F410 отдельный TIM1_UP, у F412 TIM6 без DAC, у G0B/C
общий TIM3/TIM4 и местами совместные TIM16/17 с FDCAN. У TIM6/TIM7 нет CC.
TIM2/TIM5 на F4/F7 и TIM2 при его наличии на G0 — 32-битные; остальные
описанные счётчики — 16-битные.

`Init()` отклоняет повтор, включает/сбрасывает выбранную периферию, регистрирует
Update и отличающийся от него CC один раз, включает NVIC при нулевом DIER.
При отказе регистрации удаляется только своя вставка и восстанавливается
предыдущее состояние NVIC. `_info` устанавливается только после успеха.
Последующий отказ настройки режима должен откатываться через DeInit.
Повторный SetUp возвращает InitError, не меняя callback/аппаратуру.
Ошибка частоты при первичной настройке PeriodicIRQ откатывается, повтор возможен.

## 3. SetFrequency() — расчёт PSC/ARR, и почему он protected

```cpp
SysInitStatus TIM::SetFrequency(uint32_t freq, uint32_t arr = 0); // protected
```

`freq` — частота в Гц; `arr` — **число тактов периода**, регистр ARR = arr-1.
Ноль означает автоматический выбор периода. Расчёт целочисленный, с усечением
и только 32-битным делением:

```cpp
ratio = timer_clock / freq;
// Явный период:
psc = ratio / arr;
// Автовыбор для 16-битного таймера:
psc = (ratio - 1) / 65536 + 1;
arr = ratio / psc;
// Для 32-битного при автовыборе: psc = 1, arr = ratio.
```

В PSC записывается psc-1. Последовательное деление эквивалентно
floor(clock/(freq*arr)), без переполняющегося произведения и 64-битного деления.
Отклоняются нулевая/слишком высокая частота, нулевой результат периода,
PSC вне 1..65536 и переполнение регистра ARR. Для 16-битного таймера разрешён
период **65536**, а не только 65535. uint32_t API задаёт 32-битный период до
UINT32_MAX, но не 2^32. Из-за усечения фактическая частота может отличаться;
нового поиска точных делителей/правила округления нет. При отказе регистры не меняются.

На остановленном таймере UG загружает preload до Start. Временно включается
URS, поэтому служебное событие не запрашивает IRQ/DMA; очищается только UIF,
CR1 восстанавливается. UG всё ещё может быть виден через TRGO: частоту задают
до arm ADC/ведомых таймеров. Поведение preload/URS описано в
[ST AN4013](https://www.st.com/resource/en/application_note/an4013-introduction-to-timers-for-stm32-mcus-stmicroelectronics.pdf).

Работающий таймер SetFrequency не останавливает и не сбрасывает: PSC применится
на последующем update, ARR — согласно текущему ARPE. Атомарное изменение без
переходного периода не обещается. Публичные обёртки режимов пока сохраняют
смысл: PeriodicIRQ открывает базовый метод, PWM может сохранять период, Step
дополнительно меняет compare. Детали этих режимов разбираются позже.

## 4. IRQ_en() и общий/раздельный вектор

`IRQ_en()` меняет только выбранный бит DIER; отсутствующие CC игнорируются.
Метод не регистрирует/не удаляет обработчики и не меняет NVIC. Регистрация
живёт от Init до DeInit. Отключение источника сохраняет чужие обработчики
общего вектора и допускается из callback. Повторная ручная регистрация
настроенного объекта запрещена.

## 5. TIM::HandleIRQ() — база для всех подклассов

Общий handler читает один снимок SR & DIER для Update/CC1..4 и подтверждает
только его записью SR = ~flags (rc_w0). Выключенные polling-флаги и посторонние
события сохраняются. Затем вызываются соответствующие ненулевые указатели
на функции. Блокировок, регистрации из ISR и новых callback-адаптеров нет.

Функции задаются при инициализации: Update в SetUp, CC в SetCompareIRQ.
Публичные SetUpdateCallback/SetCCCallback удалены. Compare настраивается до
Start; назначенный ненулевой callback нельзя заменить до DeInit. nullptr
означает, что функция не привязана, но источник IRQ может быть включён.
Собственные handlers Capture/HWCounter остаются отдельным этапом; здесь
описан общий handler.

```cpp
void Tick();
void MidPeriod();
TIM_PeriodicIRQ timer(TIM1);
if (timer.SetUp(1000, &Tick) == SysInitStatus::InitOK) {
    if (timer.SetCompareIRQ(TIM::Channel::CH1, timer.GetPeriod() / 2,
                           &MidPeriod) == SysInitStatus::InitOK)
        timer.Start();
}
```

## 6. DeInit() — переключение режима на одном физическом таймере

Имя метода теперь DeInit, как в USART. Из main он выключает источники и
останавливает счётчик, адресно удаляет только свои timer IRQ, сбрасывает и
отключает периферию, очищает `_info`/callbacks. Чужие обработчики сохраняют
предыдущее состояние NVIC; пустой вектор остаётся выключенным, pending очищается.
Для ненастроенного объекта действий нет. Затем допускается новый SetUp.
Одновременно настраивать несколько объектов на один TIMx нельзя.

Остановка/удаление регистрации/освобождение PWM DMA остаётся отдельным TIM-6.
До вызова нынешнего базового DeInit владелец должен остановить/освободить
подключённый DMA. Базовая очистка не означает готовую очистку PWM DMA.

```cpp
timer.Stop();
timer.DeInit();
if (timer.SetUp(2000, &Tick) == SysInitStatus::InitOK)
    timer.Start();
```

---

## 7. Подклассы

| Класс | Группа | Назначение | Свой `HandleIRQ()`? |
|-------|--------|------------|----------------------|
| `TIM_PeriodicIRQ` | — | Периодическое прерывание + доп. события на CC-каналах без пина | Нет — использует базовый |
| `TIM_PWM` | Generator | ШИМ на 1-4 каналах, опционально с DMA на CCR | Да — DMA TC + база |
| `TIM_EncoderGenerator` | Generator | Генератор квадратурных импульсов (эмуляция энкодера) | Да — счётчик оставшихся импульсов |
| `TIM_StepGenerator` | Generator | STEP-пульсация 50% duty (STEP/DIR привод) | Да — инкремент счётчика шагов |
| `TIM_InputCapture` | Measurer | Захват CCRx по фронту на одном канале | Да — один канал вместо всех четырёх |
| `TIM_PulseMeasure` | Measurer | Период+ширина импульса (PWM input, reset mode) | Нет — чисто аппаратный, без колбэка |
| `TIM_EncoderReader` | Measurer | Аппаратное чтение реального квадратурного энкодера | Нет — чисто аппаратный, без прерываний |
| `TIM_HWCounter` | Measurer | Аппаратный счётчик чужих импульсов через ITR, без CPU | Да — авто-стоп по достижении цели |

### 7.1 TIM_PeriodicIRQ

```cpp
SysInitStatus SetUp(uint32_t freq, void (*cb)(void) = nullptr, uint32_t arr = 0);
using TIM::SetFrequency;   // публичный доступ к базовому методу, см. §1/§3
SysInitStatus SetCompareIRQ(TIM::Channel ch, uint32_t compare, void (*cb)(void));
```

`SetUp()` — `SetFrequency(freq, arr)` + колбэк на Update (`arr=0` — как обычно, авто-подбор
разрешения). `SetCompareIRQ()` добавляет **дополнительное** событие на CC-канале, не занимая GPIO:
канал остаётся в режиме Frozen (`OCxM` = 0, сброс по умолчанию) — `CCxIF` взводится чисто от
совпадения `CNT == CCRx`, независимо от `OCxM`/`CCxE`, так что пин не требуется вообще. Несколько
каналов = несколько независимых моментов внутри одного периода на одном таймере, без необходимости
заводить отдельный таймер под каждое расписание.

```cpp
TIM_PeriodicIRQ tick(TIM3);
tick.SetUp(1000, &Tick);                                  // 1 кГц Update — SetUp() сам подбирает PSC/ARR
tick.SetCompareIRQ(TIM::Channel::CH1, tick.GetPeriod() / 2, &HalfTick); // + событие в середине периода
tick.Start();                                              // SetUp() таймер НЕ запускает
```

---

## Generators — производят выходной сигнал

### 7.2 TIM_PWM

```cpp
SysInitStatus SetUp(uint32_t freq, uint32_t arr = 1000);
SysInitStatus SetFrequency(uint32_t freq, uint32_t arr = 0);  // arr=0 = оставить текущий период, см. §3
void SetDuty(TIM::Channel ch, uint32_t percent);   // 0-100%, масштабируется по фактическому ARR
void SetCCR(TIM::Channel ch, uint32_t val);        // прямая запись CCRx
void AttachDMA(DMA_Sx* dma, TIM::Channel ch);
SysStatus SendDMA(TIM::Channel ch, const uint16_t* data, uint32_t len);
Output GetOutput(TIM_PIN pin);   // хендл на один канал, см. ниже
```

Каждый канал настраивается в PWM mode 1 (`OCxM = 6`) **с обязательным preload** (`OCxPE = 1`):

> Preload обязателен для DMA-управляемого вывода: без него DMA-запись приходит примерно через
> 3 такта после события UPDATE (задержка AHB) и попадает в CCR уже посреди периода — это обрезает
> передний фронт первого бита при каждой смене значения CCR. С preload включённым DMA пишет в
> теневой регистр, и значение атомарно применяется на следующем UPDATE.

`AttachDMA()`/`SendDMA()` позволяют менять `CCRx` каждый период через DMA без участия CPU — типичный
случай: генерация протокола WS2812B, где каждый бит кодируется своей длительностью high-фазы.
Несколько каналов подключаются независимо, каждый получает свой DMA-стрим; общий `TIM_DIER_UDE`
включается на первом активном канале и не трогается, пока другие ещё работают.

**`Output`** — лёгкий хендл на один конкретный канал (`_pwm` + `Channel`), полученный через
`GetOutput(pin)`. Решает конкретную проблему: без него вызывающий код должен сам где-то хранить
голый `TIM::Channel` и не перепутать, к какому `TIM_PWM` он относится — `Output` хранит оба факта
сразу, так что `output.SetDuty(50)` физически не может быть применён не к тому объекту. `GetOutput()`
принимает тот же `TIM::_N::CHx::Pyz`, что уже передан в конструктор — канал не набирается вручную
второй раз; трапает, если канал не был сконфигурирован.

```cpp
TIM_PWM ws_tim(TIM1, TIM::_1::CH1::PA8, TIM::_1::CH2::PA9, TIM::_1::CH3::PA10);
ws_tim.SetUp(800'000, 79);           // 800 кГц, ARR=79 (под тактовую конкретного МК)
ws_tim.AttachDMA(&dma_ch1, TIM::Channel::CH1);
ws_tim.SendDMA(TIM::Channel::CH1, bit_pattern, n_bits);

TIM_PWM::Output red = ws_tim.GetOutput(TIM::_1::CH1::PA8);
red.SetDuty(50);   // дальше канал никогда не путается
```

### 7.3 TIM_EncoderGenerator

```cpp
TIM_EncoderGenerator step(TIM3, TIM::_3::CH1::PA6, TIM::_3::CH2::PA7);   // A, B (90°)
step.SetUp(/*freq=*/1000, /*period=*/999, /*ch1_width=*/500, /*ch2_width=*/250);
step.SetCompleteCallback(&OnDone);
step.GenPulse(200);    // 200 импульсов вперёд; отрицательное значение — назад
```

Не путать с `TIM_EncoderReader` (§7.7) — этот класс **эмулирует** квадратурный сигнал (например,
для тестирования кода, который читает энкодер), а не читает реальный. Оба канала в toggle-режиме
(`OCxM = 0b011`) — выход инвертируется на каждое совпадение, независимо от `CCRx`, что даёт фазовый
сдвиг между A и B, задаваемый разностью `ch1_width`/`ch2_width`. Направление — не отдельный регистр,
а инверсия полярности `CC1P` перед стартом (`GenPulse(pulses > 0)` снимает `CC1P`, `< 0` — ставит и
инвертирует знак). `HandleIRQ()` здесь переопределён на простой декремент программного счётчика
`_pulses` в Update-обработчике — когда он достигает 0, таймер останавливается и вызывается
`_on_complete`. Это программный (не RCR-based) способ ограничить число импульсов — в отличие от
`TIM_StepGenerator::RunSteps()` (§7.4), тут нет аппаратного авто-стопа, ISR обязателен на каждый
период.

### 7.4 TIM_StepGenerator

```cpp
TIM_StepGenerator step(TIM3, TIM::_3::CH1::PA6);
step.SetUp(1000, /*count_in_isr=*/true);   // 1 кГц, считать шаги в ISR
step.Start();
uint32_t done = step.GetStepCount();
```

Использует **PWM mode 1** (`OCxM = 6`), не toggle-режим: `CCR` фиксирован на `ARR/2` (пересчитывается
в `SetStepFrequency()` при каждой смене частоты), так что один полный период `ARR` — это ровно один
STEP-импульс (HIGH первую половину, LOW вторую). Это значит, что Update-событие таймера 1-в-1
совпадает с частотой шагов — `SetStepFrequency(freq)` вызывает `SetFrequency(freq)` напрямую, без
удвоения, `RunSteps()` считает `RCR = steps - 1`, `GetStepCount()` возвращает счётчик как есть, без
`>> 1`. Схема с toggle потребовала бы 2 Update-события на импульс (rising+falling отдельными
переключениями) — везде пришлось бы удваивать/делить пополам, ничего не выигрывая: PWM-режим даёт ту
же точность 50%-duty через `ARR/2`, что и toggle. `OC1PE` (preload CCR) включён в паре с `TIM_CR1_ARPE`
(preload ARR), чтобы при живой смене частоты новые `ARR` и `CCR` применялись синхронно на следующем
Update, а не `CCR` сразу, а `ARR` только через период (PSC и так всегда буферизован аппаратно).

Два способа считать прошедшие шаги, выбираются в `SetUp()`:

- **`count_in_isr = true`** — Update IRQ включён, `HandleIRQ()` инкрементирует `_step_count`
  напрямую (1 Update = 1 шаг). Простое решение, но одно прерывание на каждый шаг — ощутимо на
  десятках кГц.
- **`count_in_isr = false`** — прерывание вообще не включается; вместо него используется
  [`TIM_HWCounter`](#78-tim_hwcounter), считающий Update-события этого таймера через ITR/TRGO в
  железе, без единого ISR — для высоких частот шагов. `TIM_HWCounter::Count()` тогда тоже 1:1 с
  шагами — конвертация `*2`/`/2`, нужная при старом toggle-режиме, больше не нужна нигде.

**`RunSteps(steps)`** — аппаратный одноразовый запуск на точное число импульсов без единого ISR
для отслеживания завершения:

```cpp
uint32_t events = steps - 1u;         // PWM: 1 событие на шаг
TIMx->RCR  = events;                  // Repetition Counter
TIMx->CR1 |= TIM_CR1_OPM;             // One Pulse Mode — CEN сбрасывается сам, когда RCR исчерпан
TIMx->EGR  = TIM_EGR_UG;              // форсировать перезагрузку RCR из preload перед первым периодом
```

Требует `has_bdtr == true` (Repetition Counter физически есть только у advanced/semi-advanced
таймеров — `TIM1`/`TIM8` на F4, `TIM1`/`TIM16`/`TIM17` на G0) — иначе `InitError`. Важное свойство:
`RCR` считает **прошедшие периоды**, а не время, поэтому смена скорости через `SetStepFrequency()`
между вызовами `RunSteps()` не портит итоговое число сгенерированных импульсов даже при
разгоне/торможении посреди движения.

> Арифметика RCR (`steps - 1`) не проверена на реальном железе — перед тем как полагаться на неё для
> точного позиционирования, стоит сверить реальное число импульсов осциллографом при первом запуске
> на новой плате.

Если RCR на этом таймере недоступен (например, STEP генерируется на `TIM3`) — используйте
`TIM_HWCounter::StopAfter()` на втором, сцепленном через ITR таймере (§7.8).

---

## Measurers — читают/декодируют входной сигнал

### 7.5 TIM_InputCapture

```cpp
TIM_InputCapture(TIM_TypeDef* timx, TIM::Line input);   // пин + канал уже связаны, а не два аргумента
SysInitStatus SetUp(uint32_t max_freq, void (*cb)(void) = nullptr);
uint32_t GetCapture() const;
```

```cpp
TIM_InputCapture cap(TIM3, TIM::_3::CH1::PA6);
cap.SetUp(1'000'000, &OnEdgeCaptured);  // или без колбэка — опрос через GetCapture()
```

`CCxS` маппится на **собственный** TI-вход канала (`0b01`) — прямой захват, без перекрёстной
маршрутизации между каналами (это делает `TIM_PulseMeasure`, см. §7.6). Свой `HandleIRQ()`
проверяет только один бит `SR` (канал, на котором реально сконфигурирован захват), а не все
четыре — в отличие от базового класса, здесь заранее известно, что остальные три не имеют смысла
для этого объекта.

### 7.6 TIM_PulseMeasure

```cpp
TIM_PulseMeasure pm(TIM3, TIM::_3::CH1::PA6);   // канал должен быть CH1 или CH2
pm.SetUp(1'000'000);
uint32_t width  = pm.GetWidth();   // CCR1
uint32_t period = pm.GetPeriod();  // CCR2
```

Использует аппаратный **Reset mode** (`SMS = 0b100`) с перекрёстной маршрутизацией CCMR: на
выбранном канале — прямой захват (`CC1S = 01`), на втором — обратный (`CC2S = 10`, тот же физический
пин, но инвертированный вход). Каждый фронт на "своём" полярности (`CC1P`) сбрасывает счётчик через
`TS`/`SMS`, поэтому `CCR1` фиксирует ширину импульса, `CCR2` — полный период, оба обновляются
аппаратно каждый цикл сигнала без единого прерывания. Никакого пользовательского колбэка нет —
чисто аппаратный измеритель, читаемый через `GetWidth()`/`GetPeriod()` в любой момент.

### 7.7 TIM_EncoderReader

```cpp
TIM_EncoderReader enc(TIM3, TIM::_3::CH1::PA6, TIM::_3::CH2::PA7);   // строго CH1+CH2
enc.SetUp();                          // Mode::X4 по умолчанию — считать все фронты
enc.Start();

uint32_t pos = enc.Count();
bool     rev = enc.IsCountingDown();  // определено железом, только для чтения
```

Не путать с `TIM_EncoderGenerator` (§7.3) — этот класс **читает** реальный квадратурный энкодер
(Encoder Interface mode, `SMS = 001/010/011`), а не эмулирует его. `CNT` инкрементируется/
декрементируется в железе по фронтам CH1/CH2, направление (`CR1.DIR`) выставляется аппаратно по
реальному сигналу — ни одного прерывания, ни одного такта CPU.

Требует **именно CH1 и CH2** — Encoder Interface mode в каждом STM32-таймере, где он есть, жёстко
привязан к TI1/TI2 (то есть физически CH1/CH2), CH3/CH4 использовать нельзя в принципе, вне
зависимости от того, какие пины переданы. Конструктор трапает, если каналы перепутаны местами или
взяты не с CH1/CH2 — тихо смаппить их было бы неправильно.

`IsCountingDown()` **read-only** — в отличие от `TIM_HWCounter::SetDirection()` (там это команда
от софта), здесь направление целиком решает железо по реальному сигналу; сеттера намеренно нет.

`Mode` — сколько фронтов считать за один "щелчок":

| Mode | SMS | Что считается |
|------|-----|----------------|
| `X2_CH1` | `0b01` | Только фронты CH1, направление — по уровню CH2 |
| `X2_CH2` | `0b10` | Только фронты CH2, направление — по уровню CH1 |
| `X4` (по умолчанию) | `0b11` | Фронты обоих каналов — 4x разрешение |

`ARR` выставляется в `_info->arr_max` (максимальный диапазон до заворота) — то же самое, что делает
`TIM_HWCounter::SetUp()` для своего свободного счёта.

### 7.8 TIM_HWCounter

```cpp
SysInitStatus SetUp(TIM& master, void (*on_complete)(void) = nullptr);
void SetDirection(bool down);
bool IsCountingDown() const;
SysInitStatus StopAfter(uint32_t ticks);   // относительно: "текущий CNT + ticks"
SysInitStatus StopAT(uint32_t ticks);      // абсолютно: конкретное значение CNT
void CancelStopAfter();
uint32_t Count() const;
void Reset();
```

`SetUp(master)` включает `master.EnableTriggerOutput()` (`MMS = 010`, TRGO на Update) и переводит
**этот** таймер в External Clock Mode 1 (`SMS = 0b111`) с источником `TS` = найденный ITR-индекс —
после этого `CNT` инкрементируется аппаратно на каждое Update-событие master'а, без единого цикла
CPU, на любой частоте, которую способен генерировать master. Заодно `SetUp()`:

- запоминает `master` (нужно для авто-стопа) и **связывает `on_complete` на весь жизненный цикл
  объекта** — колбэк не передаётся заново в каждый `StopAfter()`/`StopAT()`, он один на объект;
- **безусловно** включает прерывание по каналу CH1 (даже если `on_complete == nullptr`) — авто-стоп
  master'а полезен и без колбэка, та же конвенция, что у `TIM_PeriodicIRQ::SetCompareIRQ()`.

**`SetDirection(down)`** — явная, независимая команда: считать вверх или вниз. Не связана с целью
остановки никак — просто определяет, как трактуется следующий `StopAfter()`.

**`StopAfter(ticks)`** и **`StopAT(ticks)`** — не переинициализируют сцепление с мастером (это
делает только `SetUp()`), а просто переставляют компаратор:

```cpp
// StopAfter: относительно текущей позиции, с учётом направления
uint64_t period = uint64_t(_info->arr_max) + 1;
uint32_t target = IsCountingDown()
    ? (TIMx->CNT + period - ticks) % period
    : (TIMx->CNT + ticks) % period;
*ccr(0) = target;          // CH1, чисто как компаратор — ни один пин не занят
IRQ_en(IRQ::CC1, ENABLE);  // повторное включение — на случай, если CancelStopAfter() его выключил
```

`StopAT(ticks)` делает то же самое, но `target = ticks` напрямую — не зависит ни от текущего `CNT`,
ни от направления; удобно, когда позиция отслеживается в абсолютных координатах ("иди в 5000"), а
не как серия относительных перемещений. Оба метода можно звать в любой момент — на бегу или до
`Start()` — чтобы перенацелить уже двигающийся счётчик, например когда прилетела новая команда
движения.

`HandleIRQ()` по срабатыванию `CC1IF` останавливает **`master`**, но не сам себя, и не выключает
`CC1IE` — счётчик продолжает жить как позиция (естественно "замирает", когда останавливается master,
раз тики больше не приходят), готовый к следующему `StopAfter()`/`StopAT()` без повторного `SetUp()`.
`CancelStopAfter()` — единственный способ явно выключить `CC1IE`; следующий `StopAfter()`/`StopAT()`
сам включит его обратно.

```cpp
TIM_StepGenerator step(TIM3, TIM::_3::CH1::PA6);
step.SetUp(5000, false);            // без ISR на самом STEP-таймере

TIM_HWCounter counter(TIM1);        // любой таймер со свободным ITR-маршрутом к TIM3
counter.SetUp(step, &OnMoveDone);   // сцепление + колбэк — один раз
counter.SetDirection(false);        // вперёд — отдельная явная команда
counter.StopAfter(800);             // 800 шагов (PWM-режим master'а: 1:1 с тиками)
step.Start();

// новая команда движения пришла посреди хода — просто перенацелить:
counter.StopAfter(300);             // ещё 300 шагов от текущего CNT
counter.StopAT(5000);               // ...или сразу в абсолютную позицию 5000
```

---

## 8. ITR / TRGO — каскадирование таймеров в железе

```cpp
struct ITR_Route { TIM_TypeDef* master; TIM_TypeDef* slave; uint8_t itr; };
static const ITR_Route itr_table[];
static uint8_t FindITR(TIM_TypeDef* master, TIM_TypeDef* slave);  // 0xFF = маршрут не найден/не подтверждён
```

Внутренняя шина Internal Trigger (`ITR0`-`ITR3`) соединяет TRGO одного таймера со Slave Mode
Controller (`SMCR`/`TS`) другого — это то, на чём строится `TIM_HWCounter` (§7.8). Таблица
`itr_table` перечисляет **подтверждённые по Reference Manual** пары `(master, slave, itr)`;
`FindITR()` — линейный поиск, `0xFF` означает "для этой пары нет верифицированной записи, считать
неподдерживаемой" — так `TIM_HWCounter::SetUp()` отказывается работать, а не молча подключается к
неправильному ITR-индексу.

На F4 таблица заполнена для `TIM1/2/3/4/5/8` (единственные таймеры с полноценным SMCR по RM0090).
На STM32G0 обе записи (`TIM3→TIM1`, `TIM1→TIM3`) стоят с `itr = 0xFF` намеренно — они ещё не
сверены с RM0444, поэтому `TIM_HWCounter` на G0 сейчас всегда возвращает `InitError`, пока кто-то
не подтвердит и не проставит реальный индекс.

```text
TIM_StepGenerator (master, STEP на своём CH)  ──TRGO (Update)──▶  ITRx  ──▶  TIM_HWCounter (slave)
        EnableTriggerOutput(): MMS=010                 SMCR: TS=itr, SMS=111 (External Clock Mode 1)
```

Общая таблица периферии F7 теперь определена, исходник компилируется.
Маршруты HWCounter F7 пока не реализованы: FindITR возвращает 0xFF до разбора
этого режима. G0 также ожидает проверки маршрутов; прежние F4 отфильтрованы
по наличию периферии.

---

## 9. Примеры использования

Развёрнутые примеры для каждого подкласса приведены прямо в исходнике — см. doc-комментарии над
каждым классом в [tim.hpp](src/tim.hpp). Сводка "что выбрать":

| Задача | Класс |
|--------|-------|
| Периодический тик + доп. события без пина | `TIM_PeriodicIRQ` |
| ШИМ (моторы, LED, WS2812B) | `TIM_PWM` (+ `AttachDMA` для протоколов вроде WS2812B) |
| Эмулировать квадратурный сигнал (тесты) | `TIM_EncoderGenerator` |
| STEP/DIR импульсы, малая частота, программный счёт | `TIM_StepGenerator(count_in_isr=true)` |
| STEP/DIR импульсы, высокая частота, без ISR на STEP | `TIM_StepGenerator(false)` + `TIM_HWCounter` |
| Измерить частоту/период входного сигнала по фронтам | `TIM_InputCapture` |
| Измерить период И скважность одного PWM-сигнала | `TIM_PulseMeasure` |
| Прочитать реальный квадратурный энкодер | `TIM_EncoderReader` |
| Точное число шагов без ISR на каждый шаг | `RunSteps()` (нужен RCR) или `TIM_HWCounter::StopAfter()`/`StopAT()` (не нужен) |
| Переключить один физический таймер между режимами без ребута | `DeInit()` (§6) |

---

## 10. Угловые случаи

- **`TIM6`/`TIM7` (F4) — 0 каналов** (`channel_count = 0`): годятся только для
  `TIM_PeriodicIRQ` (Update-событие) — все остальные подклассы требуют хотя бы один CC-канал и
  либо не скомпилируются (нет нужной таблицы пинов), либо `SetUp()` вернёт `InitError`.
- **16-бит vs 32-бит ARR** — `TIM2`/`TIM5` на F4/F7 и существующий `TIM2` на G0 имеют `arr_max = 0xFFFFFFFF`; все
  остальные ограничены `0xFFFF`. `SetDuty()` предупреждает в своём doc-комментарии: для ARR
  вблизи 32-битного предела использовать `SetCCR()` напрямую, не проценты.
- **`has_bdtr`** — не просто "продвинутый таймер": это единственный признак наличия `BDTR`/`MOE`
  (нужен для `TIM_PWM` на некоторых таймерах — без `MOE` выходы физически не активируются) и `RCR`
  (нужен для `TIM_StepGenerator::RunSteps()`).
- **`TIM_HWCounter` на STM32G0/F7 всегда `InitError`** — таблица `itr_table` не подтверждена (см.
  §8); это не баг конкретного вызова, а сознательно неполная таблица.
- **`TIM_HWCounter::on_complete` привязан к `SetUp()`, не к `StopAfter()`/`StopAT()`** — один
  колбэк на весь жизненный цикл объекта; сменить его можно только через `DeInit()` и новый `SetUp()` (что заново
  сцепит ITR-маршрут — не то же самое, что просто сменить цель).
- **`TIM_EncoderReader` требует именно CH1+CH2** — аппаратное ограничение Encoder Interface mode,
  не выбор этого драйвера; CH3/CH4 конструктор отклонит трапом.
- **`Init()`/`SetFrequency()` теперь `protected`** — прямой вызов защищённого базового метода больше не скомпилируется на объектах, где это было бы неправильно; вместо
  них у `TIM_PWM` и `TIM_StepGenerator` свои публичные обёртки (см. §3), `TIM_PeriodicIRQ` открывает
  базовый метод через `using`.
- **`TIM_StepGenerator::SetStepFrequency()` сбрасывает режим `RunSteps()`** — явно снимает
  `TIM_CR1_OPM`, возвращая таймер в свободный (continuous) режим; вызов после `RunSteps()`
  отменяет ожидаемый авто-стоп.
- **Общий вектор Update+CC у большинства general-purpose таймеров** — `Init()` не регистрирует
  второй раз один и тот же вектор (сравнение `irq_cc != irq_up`), но это значит, что `HandleIRQ()`
  для таких таймеров вызывается на **любое** из событий Update/CC1-4 — код внутри всегда сам
  разбирает, какие именно биты `SR` реально стоят (см. §5).

---

## См. также

- [IRQ_Registry.md](IRQ_Registry.md) — `Init()` (§4) регистрирует `this` тем же способом, что
  `USART`/`SPI`/`RTC`; см. там же про общий/раздельный вектор на одну строку `_table`.
- [DMA.md](DMA.md) — `TIM_PWM::AttachDMA()` (§7.2) настраивает `DMA_Sx` так же, как `USART`/`SPI`.
- [RCC.md](RCC.md) — `_info->bus_clk`/`clk_reg`/`rst_reg` (§2-3, §6) указывают на регистры и
  переменные, которые пишет именно этот модуль (включая правило "таймеры x2" для тактовой частоты).
- [GPIO.md](GPIO.md) — `TIM_PIN`/`Line` (tim.hpp + tim_defs.hpp) построены поверх того же `PIN`, что и
  остальные периферийные таблицы пинов.
- [System.md](System.md) — общая инфраструктура (`SysStatus`/`SysInitStatus`, `DebugTrap`),
  используемая всеми методами `SetUp()` этого файла.
