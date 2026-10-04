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
6. [Deinit() — Switching Modes on a Single Physical Timer](#6-deinit--switching-modes-on-a-single-physical-timer)
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

`TIM` is the common part shared by eight different drivers, but is not itself a working driver —
its constructor is `protected`, so `TIM` can't be created directly. The drivers fall into three
groups:

- **Neither one nor the other** — `TIM_PeriodicIRQ`: doesn't occupy any pins, pure Update/CC
  interrupts.
- **Generators** (produce an output signal) — `TIM_PWM`, `TIM_EncoderGenerator`, `TIM_StepGenerator`.
- **Measurers** (read/decode an input signal) — `TIM_InputCapture`, `TIM_PulseMeasure`,
  `TIM_EncoderReader`, `TIM_HWCounter`.

They appear in `tim.hpp` in exactly this order, with banner separators `// === Generators ===` /
`// === Measurers ===`.

The reason for protecting the constructor isn't purely organizational. CMSIS gives every
`TIM_TypeDef*` the same set of fields (`CCR1-4`, `CCMR1-2`, `CCER`, `BDTR`, `RCR`, `OR`), even if
the specific timer doesn't physically implement some of them: `TIM6`/`TIM7` (basic timers, F4)
have no compare channels at all, only the Update event; `TIM9-TIM14` have 1-2 channels; the full 4
channels exist only on `TIM1/2/3/4/5/8` (F4) or `TIM1/TIM3` (G0). A write to a register that
physically doesn't exist on a given instance is ignored by hardware — not a bus error, just a
silent no-op. In other words, `TIM t(TIM6); t.SetCCCallback(...)` would compile and simply never
fire. Each subclass requires a specific `Line`/`TIM_PIN` from the `tim_defs.hpp` tables, which
**declare only the channels that actually exist** for that timer (for example, there's no
`TIM::_14::CH2` for `TIM14` — the second channel simply doesn't exist in the table), so a
"channel that doesn't exist" error is caught at compile time instead of silently at runtime.

By the same logic, **`Init()` and `SetFrequency()` are also `protected`** — they're internal
building blocks, not part of the public contract. They have no legitimate external caller: each
subclass calls them itself from its own `SetUp()`. Calling `SetFrequency()` directly on an object
that needs extra bookkeeping around the frequency (for example, `TIM_StepGenerator`, which
recomputes `CCR` for the new `ARR`) would silently misconfigure the timer — that's exactly what
once happened: `SetFrequency()` was accidentally called directly on a `TIM_StepGenerator` instead
of `SetStepFrequency()`. Subclasses that genuinely need public access to the base semantics
without extra bookkeeping (`TIM_PeriodicIRQ`) get it back selectively via `using
TIM::SetFrequency;` — not a copy of the method, but literally the same name, just with a
different access level in that specific subclass. `IRQ_en()`, by contrast, stays public in the
base class deliberately: it's documented as a method that can be called directly (see
`TIM_PeriodicIRQ::SetCompareIRQ()`), and calling it incorrectly doesn't corrupt state — at worst
it flips a bit that has no effect on anything.

---

## 2. PeriphInfo — A Facts Table for Each Timer

```cpp
struct PeriphInfo {
    TIM_TypeDef*        periph;        // TIM3, ...
    volatile uint32_t*  clk_reg;       // &RCC->APB1ENR
    uint32_t            clk_bit;       // RCC_APB1ENR_TIM3EN
    volatile uint32_t*  rst_reg;       // &RCC->APB1RSTR
    uint32_t            rst_bit;       // RCC_APB1RSTR_TIM3RST — used by Deinit(), see §6
    uint32_t const*     bus_clk;       // &System::TIMxAPB1Clock (already accounting for the "x2" rule, see RCC.md)
    IRQn_Type           irq_up;        // Update event vector
    IRQn_Type           irq_cc;        // CC event vector; == irq_up if the vector is shared
    bool                has_bdtr;      // advanced/semi-advanced (BDTR/MOE/RCR available)
    uint8_t             channel_count; // 0, 1, 2, or 4
    uint32_t            arr_max;       // 0xFFFF (16-bit) or 0xFFFFFFFF (TIM2/TIM5, 32-bit)
    uint32_t            dma_up_req;    // DMAMUX request ID for the Update event (0 — none/not needed)
};
static const PeriphInfo tim_table[];   // one row per timer, per-family — tim_defs.hpp Section B
```

`Init()` finds the row via a linear search on `TIMx` and stores the pointer in `_info` — from
then on, **all** the rest of the code (`SetFrequency`, `IRQ_en`, `Deinit`, every subclass's
`SetUp()`) reads facts through `_info`, instead of `switch(TIMx)`/`#if` in every method. The
`rst_reg`/`rst_bit` pair is an exact mirror of `clk_reg`/`clk_bit`, just for the RCC reset register
instead of the enable register (see §6). An example of a real row (F4, `TIM3` — a 16-bit
general-purpose timer, 4 channels, a shared Update+CC vector):

```cpp
{ TIM3, &RCC->APB1ENR, RCC_APB1ENR_TIM3EN, &RCC->APB1RSTR, RCC_APB1RSTR_TIM3RST,
  &System::TIMxAPB1Clock, TIM3_IRQn, TIM3_IRQn,
  /*has_bdtr=*/false, /*channels=*/4, /*arr_max=*/0xFFFF, /*dma_up_req=*/0 }
```

Compare this with `TIM1` on the same F4 (an advanced timer — `has_bdtr = true`, separate Update
and CC vectors):

```cpp
{ TIM1, &RCC->APB2ENR, RCC_APB2ENR_TIM1EN, &RCC->APB2RSTR, RCC_APB2RSTR_TIM1RST,
  &System::TIMxAPB2Clock, TIM1_UP_TIM10_IRQn, TIM1_CC_IRQn, /*has_bdtr=*/true, 4, 0xFFFF, 0 }
```

and with `TIM6` (a basic timer — 0 channels, no subclass with CC channels can be instantiated on
it):

```cpp
{ TIM6, &RCC->APB1ENR, RCC_APB1ENR_TIM6EN, &RCC->APB1RSTR, RCC_APB1RSTR_TIM6RST,
  &System::TIMxAPB1Clock, TIM6_DAC_IRQn, TIM6_DAC_IRQn, false, /*channels=*/0, 0xFFFF, 0 }
```

> On STM32G0 the repository has no device CMSIS header — the `rst_reg`/`rst_bit` values
> (`RCC_APBRSTR1_*`/`RCC_APBRSTR2_*`) were filled in by analogy with the already-used
> `APBENR1`/`APBENR2` (the same bit naming/placement pattern), but haven't been checked against
> real hardware. A mistake in the macro name simply won't compile — a loud, safe failure, not a
> silent register corruption.

---

## 3. SetFrequency() — PSC/ARR Calculation, and Why It's protected

```cpp
SysInitStatus TIM::SetFrequency(uint32_t freq, uint32_t arr = 0);   // protected
```

A single function with two modes, switched by `arr`:

- **`arr == 0`** (the default) — the function picks both `PSC` and `ARR` itself, maximizing
  `ARR` (i.e., the resolution of `SetCCR()`/`SetDuty()`) for the given `freq`:

  ```cpp
  uint32_t ratio = *_info->bus_clk / freq;                 // (PSC+1)*(ARR+1)
  uint32_t psc   = ratio / (uint64_t(_info->arr_max) + 1) + 1;  // the minimum divider for which ARR fits
  uint32_t arr   = ratio / psc;                             // the maximum ARR for this psc
  ```

- **`arr != 0`** — the period is given explicitly by the calling code, the function only picks
  `PSC` to match it:

  ```cpp
  uint64_t psc = *_info->bus_clk / (uint64_t(arr) * freq);
  ```

  Casting to `uint64_t` in both branches is mandatory on 32-bit timers (`TIM2`/`TIM5`,
  `arr_max = 0xFFFFFFFF`) — without it, `arr_max + 1` or `arr * freq` would silently overflow
  `uint32_t`. `psc` in both branches is a "divider" in 1-based form (1 = divide by 1), so the
  final register write is always `TIMx->PSC = psc - 1u`, regardless of which branch computed it —
  the bounds check (`psc == 0` → unreachable frequency, `psc > 0x10000` → doesn't fit in the
  16-bit `PSC`) is also shared by both branches.

**The method is `protected`** — calling it directly on an object that needs extra bookkeeping
around the frequency would silently misconfigure something (see §1). Classes that need this
publicly have their own variants:

| Class | Public Method | How It Differs from Plain `SetFrequency()` |
|-------|------------------|---------------------------------------------|
| `TIM_PeriodicIRQ` | `using TIM::SetFrequency;` | No difference — the base method's semantics are already correct, no wrapper needed. |
| `TIM_PWM` | `SetFrequency(freq, arr=0)` | Here `arr=0` means **not** "pick again," but "keep the current period" — otherwise `SetDuty()`'s resolution would change unpredictably on every frequency change. |
| `TIM_StepGenerator` | `SetStepFrequency(freq)` | Additionally recomputes `CCR = ARR/2` (see §7.4) and clears `TIM_CR1_OPM` left over from `RunSteps()`. |

---

## 4. IRQ_en() and the Shared/Separate Vector

```cpp
void TIM::IRQ_en(IRQ irq, FunctionalState en)   // public — intentionally, see §1
```

The same auto-register/auto-unregister idiom as `USART`/`RTC` (see
[IRQ_Registry.md](IRQ_Registry.md)), but with one difference: on advanced timers the Update and
Compare events can sit on **two different** NVIC vectors (`irq_up` != `irq_cc`, for example
`TIM1_UP_TIM10_IRQn` and `TIM1_CC_IRQn` on F4). On the first enable of any source, `IRQ_en()`
registers **both** vectors (if they differ) on the same `this`:

```cpp
if (en && !NVIC_GetEnableIRQ(_info->irq_up)) {
    IRQ_Registry::Register(_info->irq_up, this);
    NVIC_EnableIRQ(_info->irq_up);
    if (_info->irq_cc != _info->irq_up) {
        IRQ_Registry::Register(_info->irq_cc, this);
        NVIC_EnableIRQ(_info->irq_cc);
    }
}
```

and symmetrically unregisters both when the last source is disabled (`!(TIMx->DIER & all)`, where
`all` is the mask of all 5 IE bits). On timers with a shared vector (`irq_up == irq_cc`, most
general-purpose timers), the second `Register()` simply isn't executed — it isn't needed.

---

## 5. TIM::HandleIRQ() — The Base for All Subclasses

```cpp
void TIM::HandleIRQ()
{
    uint32_t sr = TIMx->SR;
    TIMx->SR = ~sr;   // see below — not "= 0"
    if ((sr & TIM_SR_UIF)   && _update_cb) _update_cb();
    if ((sr & TIM_SR_CC1IF) && _cc_cb[0])  _cc_cb[0]();
    if ((sr & TIM_SR_CC2IF) && _cc_cb[1])  _cc_cb[1]();
    if ((sr & TIM_SR_CC3IF) && _cc_cb[2])  _cc_cb[2]();
    if ((sr & TIM_SR_CC4IF) && _cc_cb[3])  _cc_cb[3]();
}
```

Two non-obvious decisions, both commented directly in the code and worth knowing when
reading/editing it:

- **`TIMx->SR = ~sr`, not `TIMx->SR = 0`.** `SR` is "write-0-to-clear, write-1-is-no-op": writing
  the inverted mask clears exactly the flags that were read, and leaves the rest alone.
  `SR = 0` would clear **all** flags at once — including one that hardware managed to set in the
  gap between reading `sr` and writing (for example, `CC2IF` gets set while the `CC1` callback is
  still running) — such an event would be silently lost instead of staying set for the next entry
  into `HandleIRQ()` (NVIC tail-chaining would pick it up immediately). The genuinely dangerous
  window is only a few cycles between reading `sr` itself and writing `~sr`, not the entire time
  the callbacks run (they already happen after the write).
- **All 4 CC flags are checked unconditionally**, rather than looping up to
  `_info->channel_count`. On timers with fewer channels, the missing `SR` bits are hardwired to 0
  per the Reference Manual — checking the "extra" bits is both correct and cheap (one `AND` plus a
  branch per channel, negligible next to interrupt entry/exit), so it's not worth complicating the
  code for this case.

Subclasses that need different dispatch logic (DMA plus timer flags together, a single channel
instead of all four, etc.) override `HandleIRQ()` entirely — see the subclass table in §7.
`TIM_PulseMeasure` and `TIM_EncoderReader` don't override it at all — both are purely hardware
measurers with no interrupt whatsoever.

---

## 6. Deinit() — Switching Modes on a Single Physical Timer

```cpp
void TIM::Deinit();   // public
```

The inverse operation to `Init()`+`SetUp()`: it lets you hand the same physical timer to a
different mode (for example, `TIM_PWM` → `TIM_StepGenerator` on the same `TIM3`) **without
rebooting the controller**. Before this method existed, switching was unsafe — each `SetUp()`
only clears the registers that matter specifically to it (`TIM_PWM::SetUp()` doesn't touch
`SMCR`/`RCR`, for example), so state from the previous mode could leak into the next one.

```cpp
void TIM::Deinit()
{
    IRQ_en(IRQ::UE,  DISABLE);   // + CC1..CC4 — disable and unregister everything that was enabled
    Stop();
    *_info->rst_reg |= _info->rst_bit;    // pulse the RCC reset bit —
    *_info->rst_reg &= ~_info->rst_bit;   // a full reset of CR1/CR2/SMCR/DIER/SR/EGR/CCMRx/CCER/
                                           // CNT/PSC/ARR/RCR/CCRx/BDTR/DCR/DMAR/OR in one operation
    *_info->clk_reg &= ~_info->clk_bit;   // undo of Init()
    _update_cb = nullptr;
    _cc_cb[0] = _cc_cb[1] = _cc_cb[2] = _cc_cb[3] = nullptr;
}
```

The key idea is not to zero out the registers manually one by one (easy to forget one of ~15), but
to take advantage of the fact that in RCC every timer has not only a clock **enable** bit
(`RCC_APB1ENR_TIM3EN`, already used in `clk_bit`), but also a separate **reset** bit
(`RCC_APB1RSTR_TIM3RST`) — setting and immediately clearing it means "this entire timer has
returned to its state right after power-on-reset," in hardware, in a single operation.

```cpp
TIM_PWM pwm(TIM3, TIM::_3::CH2::PB5);
TIM_StepGenerator step(TIM3, TIM::_3::CH2::PB5);   // the same physical timer, not in use yet

pwm.SetUp(10*kHz);
pwm.Start();
...
pwm.Stop();
pwm.Deinit();          // a full hardware reset of TIM3 — can be called from any object wrapping this TIMx
step.SetUp(1*MHz);
step.Start();          // the same TIM3, now as a step generator
```

`Deinit()` can be called on **any** `TIM_*` object wrapping the desired `TIMx` — the reset is
tied to the physical peripheral through `_info->rst_reg`/`rst_bit`, not to which C++ object called
it.

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

> STM32F7 isn't covered at all in this same sense — Section A of `tim_defs.hpp` (pin tables) is
> already conditionally compiled under `STM32F4 || STM32F7`, but `tim_table`/`itr_table` (Section
> B) are filled in only for `STM32F4`. Building `TIM.*` for F7 currently won't link until someone
> adds a verified block with RCC bits/IRQ vectors/ITR routes per RM0410.

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
| Switch one physical timer between modes without a reboot | `Deinit()` (§6) |

---

## 10. Edge Cases

- **`TIM6`/`TIM7` (F4) — 0 channels** (`channel_count = 0`): suitable only for
  `TIM_PeriodicIRQ` (the Update event) — all other subclasses require at least one CC channel and
  either won't compile (no required pin table) or `SetUp()` will return `InitError`.
- **16-bit vs. 32-bit ARR** — only `TIM2`/`TIM5` on F4/F7 have `arr_max = 0xFFFFFFFF`; all
  others are limited to `0xFFFF`. `SetDuty()` warns in its doc comment: for ARR values near the
  32-bit limit, use `SetCCR()` directly, not percentages.
- **`has_bdtr`** — not just "an advanced timer": it's the single indicator of the presence of
  `BDTR`/`MOE` (needed for `TIM_PWM` on some timers — without `MOE` the outputs physically aren't
  activated) and `RCR` (needed for `TIM_StepGenerator::RunSteps()`).
- **`TIM_HWCounter` on STM32G0 is always `InitError`** — the `itr_table` isn't confirmed (see
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
- **A shared Update+CC vector on most general-purpose timers** — `IRQ_en()` doesn't register the
  same vector twice (the `irq_cc != irq_up` comparison), but this means `HandleIRQ()` for such
  timers is called on **any** of the Update/CC1-4 events — the code inside always figures out for
  itself which `SR` bits are actually set (see §5).

---

## See Also

- [IRQ_Registry.md](IRQ_Registry.md) — `IRQ_en()` (§4) registers `this` the same way
  `USART`/`SPI`/`RTC` do; see there also for the shared/separate vector on a single `_table` row.
- [DMA.md](DMA.md) — `TIM_PWM::AttachDMA()` (§7.2) configures `DMA_Sx` the same way `USART`/`SPI`
  do.
- [RCC.md](RCC.md) — `_info->bus_clk`/`clk_reg`/`rst_reg` (§2-3, §6) point to the registers and
  variables this module itself writes (including the "timers x2" rule for the clock frequency).
- [GPIO.md](GPIO.md) — `TIM_PIN`/`Line` (tim_defs.hpp) are built on top of the same `PIN` as the
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
6. [Deinit() — переключение режима на одном физическом таймере](#6-deinit--переключение-режима-на-одном-физическом-таймере)
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

`TIM` — общая часть для восьми разных драйверов, но сам по себе не является рабочим драйвером — его
конструктор `protected`, создать `TIM` напрямую нельзя. Драйверы делятся на три группы:

- **Ни то ни другое** — `TIM_PeriodicIRQ`: не занимает пинов, чистые Update/CC-прерывания.
- **Generators** (производят выходной сигнал) — `TIM_PWM`, `TIM_EncoderGenerator`, `TIM_StepGenerator`.
- **Measurers** (читают/декодируют входной сигнал) — `TIM_InputCapture`, `TIM_PulseMeasure`,
  `TIM_EncoderReader`, `TIM_HWCounter`.

Именно в этом порядке они и идут в `tim.hpp`, с баннерами-разделителями `// === Generators ===` /
`// === Measurers ===`.

Причина защиты конструктора — не только организационная. CMSIS даёт каждому `TIM_TypeDef*`
одинаковый набор полей (`CCR1-4`, `CCMR1-2`, `CCER`, `BDTR`, `RCR`, `OR`), даже если конкретный
таймер физически не реализует часть из них: `TIM6`/`TIM7` (basic timers, F4) вообще не имеют
каналов сравнения, только Update-событие; `TIM9-TIM14` — 1-2 канала; полные 4 канала есть только у
`TIM1/2/3/4/5/8` (F4) или `TIM1/TIM3` (G0). Запись в регистр, которого физически нет у данного
экземпляра, аппаратно игнорируется — не ошибка шины, а тихий no-op. То есть `TIM t(TIM6);
t.SetCCCallback(...)` скомпилировался бы и просто никогда не сработал. Каждый подкласс требует
конкретный `Line`/`TIM_PIN` из таблиц `tim_defs.hpp`, которые **объявляют только существующие
каналы** для этого таймера (например, для `TIM14` нет `TIM::_14::CH2` — второго канала там просто
не существует в таблице), так что ошибка "канал, которого не существует" ловится на этапе
компиляции, а не тишиной в рантайме.

По той же логике **`Init()` и `SetFrequency()` тоже `protected`** — это внутренние строительные
блоки, а не часть публичного контракта. У них нет ни одного легитимного внешнего вызывающего кода:
каждый подкласс сам вызывает их из своего `SetUp()`. Прямой вызов `SetFrequency()` на объекте,
которому нужна дополнительная бухгалтерия вокруг частоты (например, `TIM_StepGenerator`,
пересчитывающий `CCR` под новый `ARR`), тихо сконфигурировал бы таймер неправильно — именно так
когда-то и произошло: `SetFrequency()` был случайно вызван напрямую на `TIM_StepGenerator` вместо
`SetStepFrequency()`. Подклассам, которым реально нужен публичный доступ к базовой семантике без
дополнительной бухгалтерии (`TIM_PeriodicIRQ`), возвращают его точечно через `using
TIM::SetFrequency;` — не копия метода, а буквально то же самое имя, просто с другим уровнем
доступа в конкретном подклассе. `IRQ_en()`, для сравнения, остался публичным в базовом классе
осознанно: он документирован как метод, который можно звать напрямую (см. `TIM_PeriodicIRQ::
SetCompareIRQ()`), и неверный вызов не портит состояние — максимум щёлкает битом, который ни на что
не влияет.

---

## 2. PeriphInfo — таблица фактов о каждом таймере

```cpp
struct PeriphInfo {
    TIM_TypeDef*        periph;        // TIM3, ...
    volatile uint32_t*  clk_reg;       // &RCC->APB1ENR
    uint32_t            clk_bit;       // RCC_APB1ENR_TIM3EN
    volatile uint32_t*  rst_reg;       // &RCC->APB1RSTR
    uint32_t            rst_bit;       // RCC_APB1RSTR_TIM3RST — используется Deinit(), см. §6
    uint32_t const*     bus_clk;       // &System::TIMxAPB1Clock (уже с учётом правила "x2", см. RCC.md)
    IRQn_Type           irq_up;        // вектор Update-события
    IRQn_Type           irq_cc;        // вектор CC-события; == irq_up, если вектор общий
    bool                has_bdtr;      // advanced/semi-advanced (BDTR/MOE/RCR доступны)
    uint8_t             channel_count; // 0, 1, 2 или 4
    uint32_t            arr_max;       // 0xFFFF (16 бит) или 0xFFFFFFFF (TIM2/TIM5, 32 бита)
    uint32_t            dma_up_req;    // DMAMUX request ID для Update-события (0 — нет/не нужен)
};
static const PeriphInfo tim_table[];   // одна строка на таймер, per-family — tim_defs.hpp Section B
```

`Init()` ищет строку линейным поиском по `TIMx` и запоминает указатель в `_info` — дальше **весь**
остальной код (`SetFrequency`, `IRQ_en`, `Deinit`, все `SetUp()` подклассов) читает факты через
`_info`, вместо `switch(TIMx)`/`#if` в каждом методе. Пара `rst_reg`/`rst_bit` — точное зеркало
`clk_reg`/`clk_bit`, только для RCC reset-регистра вместо enable-регистра (см. §6). Пример реальной
строки (F4, `TIM3` — 16-битный general-purpose, 4 канала, общий вектор Update+CC):

```cpp
{ TIM3, &RCC->APB1ENR, RCC_APB1ENR_TIM3EN, &RCC->APB1RSTR, RCC_APB1RSTR_TIM3RST,
  &System::TIMxAPB1Clock, TIM3_IRQn, TIM3_IRQn,
  /*has_bdtr=*/false, /*channels=*/4, /*arr_max=*/0xFFFF, /*dma_up_req=*/0 }
```

Сравните с `TIM1` на том же F4 (advanced timer — `has_bdtr = true`, отдельные вектора Update и CC):

```cpp
{ TIM1, &RCC->APB2ENR, RCC_APB2ENR_TIM1EN, &RCC->APB2RSTR, RCC_APB2RSTR_TIM1RST,
  &System::TIMxAPB2Clock, TIM1_UP_TIM10_IRQn, TIM1_CC_IRQn, /*has_bdtr=*/true, 4, 0xFFFF, 0 }
```

и с `TIM6` (basic timer — 0 каналов, никакой подкласс с CC-каналами на нём не заведётся):

```cpp
{ TIM6, &RCC->APB1ENR, RCC_APB1ENR_TIM6EN, &RCC->APB1RSTR, RCC_APB1RSTR_TIM6RST,
  &System::TIMxAPB1Clock, TIM6_DAC_IRQn, TIM6_DAC_IRQn, false, /*channels=*/0, 0xFFFF, 0 }
```

> На STM32G0 в репозитории нет CMSIS-заголовка устройства — значения `rst_reg`/`rst_bit`
> (`RCC_APBRSTR1_*`/`RCC_APBRSTR2_*`) заполнены по аналогии с уже используемыми `APBENR1`/`APBENR2`
> (тот же паттерн именования/расположения битов), но не сверены вживую. Ошибка в имени макроса
> просто не даст скомпилироваться — громкий, безопасный отказ, а не тихая порча регистра.

---

## 3. SetFrequency() — расчёт PSC/ARR, и почему он protected

```cpp
SysInitStatus TIM::SetFrequency(uint32_t freq, uint32_t arr = 0);   // protected
```

Одна функция с двумя режимами, переключаемыми по `arr`:

- **`arr == 0`** (по умолчанию) — функция сама подбирает и `PSC`, и `ARR`, максимизируя `ARR`
  (то есть разрешение `SetCCR()`/`SetDuty()`) при заданной `freq`:

  ```cpp
  uint32_t ratio = *_info->bus_clk / freq;                 // (PSC+1)*(ARR+1)
  uint32_t psc   = ratio / (uint64_t(_info->arr_max) + 1) + 1;  // минимальный делитель, при котором ARR влезает
  uint32_t arr   = ratio / psc;                             // максимальный ARR при этом psc
  ```

- **`arr != 0`** — период задан явно вызывающим кодом, функция подбирает только `PSC` под него:

  ```cpp
  uint64_t psc = *_info->bus_clk / (uint64_t(arr) * freq);
  ```

  Приведение к `uint64_t` в обеих ветках обязательно на 32-битных таймерах (`TIM2`/`TIM5`,
  `arr_max = 0xFFFFFFFF`) — без него `arr_max + 1` или `arr * freq` тихо переполнили бы `uint32_t`.
  `psc` в обеих ветках — "делитель" в 1-based форме (1 = делить на 1), поэтому финальная запись в
  регистр всегда `TIMx->PSC = psc - 1u`, независимо от того, какая ветка его вычислила — проверка
  границ (`psc == 0` → недостижимая частота, `psc > 0x10000` → не влезает в 16-битный `PSC`) тоже
  общая для обеих веток.

**Метод `protected`** — прямой вызов на объекте, которому нужна дополнительная бухгалтерия вокруг
частоты, тихо сконфигурировал бы что-то неправильно (см. §1). У классов, которым это нужно публично,
есть свои варианты:

| Класс | Публичный метод | Чем отличается от голого `SetFrequency()` |
|-------|------------------|---------------------------------------------|
| `TIM_PeriodicIRQ` | `using TIM::SetFrequency;` | Ничем — семантика базового метода и так верна, обёртка не нужна. |
| `TIM_PWM` | `SetFrequency(freq, arr=0)` | `arr=0` здесь означает **не** "подобрать заново", а "оставить текущий период" — иначе разрешение `SetDuty()` менялось бы непредсказуемо при каждой смене частоты. |
| `TIM_StepGenerator` | `SetStepFrequency(freq)` | Дополнительно пересчитывает `CCR = ARR/2` (см. §7.4) и снимает `TIM_CR1_OPM`, оставшийся от `RunSteps()`. |

---

## 4. IRQ_en() и общий/раздельный вектор

```cpp
void TIM::IRQ_en(IRQ irq, FunctionalState en)   // public — намеренно, см. §1
```

Тот же auto-register/auto-unregister идиом, что и у `USART`/`RTC` (см. [IRQ_Registry.md](IRQ_Registry.md)),
но с одним отличием: у продвинутых таймеров Update и Compare-события могут висеть на **двух разных**
векторах NVIC (`irq_up` != `irq_cc`, например `TIM1_UP_TIM10_IRQn` и `TIM1_CC_IRQn` на F4). При
первом включении любого источника `IRQ_en()` регистрирует **оба** вектора (если они разные) на один
и тот же `this`:

```cpp
if (en && !NVIC_GetEnableIRQ(_info->irq_up)) {
    IRQ_Registry::Register(_info->irq_up, this);
    NVIC_EnableIRQ(_info->irq_up);
    if (_info->irq_cc != _info->irq_up) {
        IRQ_Registry::Register(_info->irq_cc, this);
        NVIC_EnableIRQ(_info->irq_cc);
    }
}
```

и симметрично отменяет регистрацию на обоих при выключении последнего источника (`!(TIMx->DIER & all)`,
где `all` — маска всех 5 IE-битов). На таймерах с общим вектором (`irq_up == irq_cc`, большинство
general-purpose таймеров) второй `Register()` просто не выполняется — не нужен.

---

## 5. TIM::HandleIRQ() — база для всех подклассов

```cpp
void TIM::HandleIRQ()
{
    uint32_t sr = TIMx->SR;
    TIMx->SR = ~sr;   // см. ниже — не "= 0"
    if ((sr & TIM_SR_UIF)   && _update_cb) _update_cb();
    if ((sr & TIM_SR_CC1IF) && _cc_cb[0])  _cc_cb[0]();
    if ((sr & TIM_SR_CC2IF) && _cc_cb[1])  _cc_cb[1]();
    if ((sr & TIM_SR_CC3IF) && _cc_cb[2])  _cc_cb[2]();
    if ((sr & TIM_SR_CC4IF) && _cc_cb[3])  _cc_cb[3]();
}
```

Два неочевидных решения, оба закомментированы прямо в коде и стоит знать при чтении/правке:

- **`TIMx->SR = ~sr`, а не `TIMx->SR = 0`.** `SR` — "write-0-to-clear, write-1-is-no-op": запись
  инвертированной маски снимает ровно те флаги, что были прочитаны, и не трогает остальные.
  Блок `SR = 0` снял бы **все** флаги разом — включая тот, что аппаратура могла успеть выставить
  в промежутке между чтением `sr` и записью (например, `CC2IF` взводится, пока ещё выполняется
  колбэк `CC1`) — такое событие было бы потеряно молча вместо того, чтобы остаться выставленным
  для следующего входа в `HandleIRQ()` (tail-chaining NVIC подхватит его сразу же). Реально опасное
  окно — только несколько тактов между самим чтением `sr` и записью `~sr`, не всё время выполнения
  колбэков (они уже идут после записи).
- **Все 4 CC-флага проверяются безусловно**, а не циклом до `_info->channel_count`. На таймерах с
  меньшим числом каналов недостающие биты `SR` аппаратно зашиты в 0 согласно Reference Manual —
  проверка "лишних" битов и корректна, и дёшева (одно `AND`+переход на канал, пренебрежимо на фоне
  входа/выхода из прерывания), так что не стоит усложнять код ради этого случая.

Подклассы, которым нужна другая логика диспетчеризации (DMA + таймерные флаги вместе, единственный
канал вместо всех четырёх, и т.д.), переопределяют `HandleIRQ()` целиком — см. таблицу подклассов
в §7. `TIM_PulseMeasure` и `TIM_EncoderReader` не переопределяют его вообще — оба чисто аппаратные
измерители без единого прерывания.

---

## 6. Deinit() — переключение режима на одном физическом таймере

```cpp
void TIM::Deinit();   // public
```

Обратная операция к `Init()`+`SetUp()`: позволяет отдать один и тот же физический таймер другому
режиму (например, `TIM_PWM` → `TIM_StepGenerator` на одном `TIM3`) **без перезагрузки контроллера**.
До появления этого метода переключение было небезопасным — каждый `SetUp()` очищает только те
регистры, которые важны конкретно ему (`TIM_PWM::SetUp()` не трогает `SMCR`/`RCR`, например), так что
состояние от предыдущего режима могло протечь в следующий.

```cpp
void TIM::Deinit()
{
    IRQ_en(IRQ::UE,  DISABLE);   // + CC1..CC4 — снять и разрегистрировать всё, что было включено
    Stop();
    *_info->rst_reg |= _info->rst_bit;    // импульс reset-бита RCC —
    *_info->rst_reg &= ~_info->rst_bit;   // полный сброс CR1/CR2/SMCR/DIER/SR/EGR/CCMRx/CCER/
                                           // CNT/PSC/ARR/RCR/CCRx/BDTR/DCR/DMAR/OR за одну операцию
    *_info->clk_reg &= ~_info->clk_bit;   // откат Init()
    _update_cb = nullptr;
    _cc_cb[0] = _cc_cb[1] = _cc_cb[2] = _cc_cb[3] = nullptr;
}
```

Ключевая идея — не обнулять регистры вручную по одному (легко забыть какой-то из ~15), а
воспользоваться тем, что у каждого таймера в RCC есть не только бит **разрешения** тактирования
(`RCC_APB1ENR_TIM3EN`, уже используется в `clk_bit`), но и отдельный бит **сброса**
(`RCC_APB1RSTR_TIM3RST`) — установить и тут же снять его означает "весь этот таймер вернулся в
состояние сразу после power-on-reset", аппаратно, за одну операцию.

```cpp
TIM_PWM pwm(TIM3, TIM::_3::CH2::PB5);
TIM_StepGenerator step(TIM3, TIM::_3::CH2::PB5);   // тот же физический таймер, пока не используется

pwm.SetUp(10*kHz);
pwm.Start();
...
pwm.Stop();
pwm.Deinit();          // полный аппаратный сброс TIM3 — можно вызывать с любого объекта на этот TIMx
step.SetUp(1*MHz);
step.Start();          // тот же TIM3, теперь как генератор шагов
```

`Deinit()` можно вызвать на **любом** `TIM_*` объекте, обёрнутом вокруг нужного `TIMx` — сброс
завязан на физическую периферию через `_info->rst_reg`/`rst_bit`, а не на то, какой C++-объект его
вызвал.

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

> STM32F7 в этом же смысле не покрыт вообще — Section A `tim_defs.hpp` (таблицы пинов) уже условно
> собирается под `STM32F4 || STM32F7`, но `tim_table`/`itr_table` (Section B) заполнены только для
> `STM32F4`. Сборка `TIM.*` под F7 сейчас не слинкуется, пока кто-то не добавит верифицированный
> блок с RCC-битами/IRQ-векторами/ITR-маршрутами по RM0410.

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
| Переключить один физический таймер между режимами без ребута | `Deinit()` (§6) |

---

## 10. Угловые случаи

- **`TIM6`/`TIM7` (F4) — 0 каналов** (`channel_count = 0`): годятся только для
  `TIM_PeriodicIRQ` (Update-событие) — все остальные подклассы требуют хотя бы один CC-канал и
  либо не скомпилируются (нет нужной таблицы пинов), либо `SetUp()` вернёт `InitError`.
- **16-бит vs 32-бит ARR** — только `TIM2`/`TIM5` на F4/F7 имеют `arr_max = 0xFFFFFFFF`; все
  остальные ограничены `0xFFFF`. `SetDuty()` предупреждает в своём doc-комментарии: для ARR
  вблизи 32-битного предела использовать `SetCCR()` напрямую, не проценты.
- **`has_bdtr`** — не просто "продвинутый таймер": это единственный признак наличия `BDTR`/`MOE`
  (нужен для `TIM_PWM` на некоторых таймерах — без `MOE` выходы физически не активируются) и `RCR`
  (нужен для `TIM_StepGenerator::RunSteps()`).
- **`TIM_HWCounter` на STM32G0 всегда `InitError`** — таблица `itr_table` не подтверждена (см.
  §8); это не баг конкретного вызова, а сознательно неполная таблица.
- **`TIM_HWCounter::on_complete` привязан к `SetUp()`, не к `StopAfter()`/`StopAT()`** — один
  колбэк на весь жизненный цикл объекта; сменить его можно только повторным `SetUp()` (что заново
  сцепит ITR-маршрут — не то же самое, что просто сменить цель).
- **`TIM_EncoderReader` требует именно CH1+CH2** — аппаратное ограничение Encoder Interface mode,
  не выбор этого драйвера; CH3/CH4 конструктор отклонит трапом.
- **`Init()`/`SetFrequency()` теперь `protected`** — прямой вызов `pwm.SetFrequency(...)` или
  `step.SetFrequency(...)` больше не скомпилируется на объектах, где это было бы неправильно; вместо
  них у `TIM_PWM` и `TIM_StepGenerator` свои публичные обёртки (см. §3), `TIM_PeriodicIRQ` открывает
  базовый метод через `using`.
- **`TIM_StepGenerator::SetStepFrequency()` сбрасывает режим `RunSteps()`** — явно снимает
  `TIM_CR1_OPM`, возвращая таймер в свободный (continuous) режим; вызов после `RunSteps()`
  отменяет ожидаемый авто-стоп.
- **Общий вектор Update+CC у большинства general-purpose таймеров** — `IRQ_en()` не регистрирует
  второй раз один и тот же вектор (сравнение `irq_cc != irq_up`), но это значит, что `HandleIRQ()`
  для таких таймеров вызывается на **любое** из событий Update/CC1-4 — код внутри всегда сам
  разбирает, какие именно биты `SR` реально стоят (см. §5).

---

## См. также

- [IRQ_Registry.md](IRQ_Registry.md) — `IRQ_en()` (§4) регистрирует `this` тем же способом, что
  `USART`/`SPI`/`RTC`; см. там же про общий/раздельный вектор на одну строку `_table`.
- [DMA.md](DMA.md) — `TIM_PWM::AttachDMA()` (§7.2) настраивает `DMA_Sx` так же, как `USART`/`SPI`.
- [RCC.md](RCC.md) — `_info->bus_clk`/`clk_reg`/`rst_reg` (§2-3, §6) указывают на регистры и
  переменные, которые пишет именно этот модуль (включая правило "таймеры x2" для тактовой частоты).
- [GPIO.md](GPIO.md) — `TIM_PIN`/`Line` (tim_defs.hpp) построены поверх того же `PIN`, что и
  остальные периферийные таблицы пинов.
- [System.md](System.md) — общая инфраструктура (`SysStatus`/`SysInitStatus`, `DebugTrap`),
  используемая всеми методами `SetUp()` этого файла.
