# RCC / ClockSystem

*[English](#english) · [Русский](#русский)*

---

## English

## Contents

1. [Why this is needed](#1-why-this-is-needed)
2. [Clock signal path](#2-clock-signal-path)
3. [PLL — block diagram and parameters](#3-pll--block-diagram-and-parameters)
4. [Limits by MCU family](#4-limits-by-mcu-family)
5. [Files and roles](#5-files-and-roles)
6. [Init() — step by step](#6-init--step-by-step)
7. [Flash latency](#7-flash-latency)
8. [Validation before applying](#8-validation-before-applying)
9. [InitCalcPLL() — automatic coefficient selection](#9-initcalcpll--automatic-coefficient-selection)
10. [Timeouts](#10-timeouts)
11. [Usage examples](#11-usage-examples)
12. [Edge cases](#12-edge-cases)

---

## 1. Why this is needed

`ClockSystem` is a static class (like `IRQ_Registry`, no instantiation) that configures the MCU's
clock tree: selecting the SYSCLK source (HSI/HSE/PLL), PLL parameters, the AHB/APB1/APB2 bus
dividers, Flash latency (wait states), and the derived variables (`System::SystemCoreClock`,
`System::APB1BusClock`, etc.) that every other driver (UART, TIM, ...) later reads to compute its
own dividers.

There are two ways to get the frequency you want:

- **`Init()`** — you specify PLL_M/N/P/Q/R and the bus dividers yourself. Full control, but you
  must manually respect all the datasheet's constraints.
- **`InitCalcPLL()`** — you specify only the desired SYSCLK and the PLL source; M/N/P and the bus
  dividers are computed automatically (see [section 9](#9-initcalcpll--automatic-coefficient-selection)).

---

## 2. Clock signal path

```
┌────────┐     ┌────────┐                                        ┌──────────────┐
│  HSI   │     │  HSE    │                                       │   SYSCLK     │
│(internal│    │(crystal,│                                       │ (RCC->CFGR   │
│RC osc.)│     │Init()'s │                                       │  SW bits)    │
└───┬────┘     │HSE_Clk) │                                       └──────┬───────┘
    │          └───┬─────┘                                              │
    │              │                                                    │
    │   ┌──────────┴───────────┐                                        │
    │   │  PLL (see section 3) │────────────────────────────────────────┤
    │   │  Fsrc /M → VCO_in    │                                        │
    │   │  VCO_in *N → VCO_out │                                        │
    │   │  VCO_out /P (or /R)  │                                        │
    │   └──────────────────────┘                                        │
    └────────────────── HSI/HSE directly (PLL_ClkSrc == NO) ─────────────┘
                                                                          │
                                                                          ▼
                                                              ┌───────────────────────┐
                                                              │  AHB prescaler (HPRE)  │
                                                              │  sys_clk / AHB_Pre     │
                                                              └───────────┬───────────┘
                                                                          │  System::SystemCoreClock
                                                     ┌────────────────────┼────────────────────┐
                                                     ▼                                          ▼
                                       ┌─────────────────────────┐                ┌─────────────────────────┐
                                       │ APB1 prescaler (PPRE1)  │                │ APB2 prescaler (PPRE2)  │
                                       │ AHB_clk / APB1_Pre      │                │ AHB_clk / APB2_Pre      │
                                       │ = System::APB1BusClock  │                │ = System::APB2BusClock  │
                                       └────────────┬────────────┘                └────────────┬────────────┘
                                                    │ if APBx_Pre > 1:                          │
                                                    │ timers on this bus                        │
                                                    │ get APBxClock * 2                         │
                                                    ▼                                            ▼
                                       System::TIMxAPB1Clock                     System::TIMxAPB2Clock
```

**The "timers x2" rule**: in hardware, if the APBx divider is `> 1`, the clock frequency seen by
timers on that bus is `APBxClock * 2`, not `APBxClock` (compensation for the Cortex-M bus
architecture). If the divider is `== 1`, timers get the same frequency as the rest of the
peripherals on that bus. That's exactly why the code has two separate variables
(`System::APB1BusClock` for peripherals, `System::TIMxAPB1Clock` for `TIM::SetFrequency()`) rather
than one:

```cpp
System::TIMxAPB1Clock = (APB1_Pre == 1) ? (System::APB1BusClock) : (System::APB1BusClock * 2);
```

STM32G0 has no second APB2 bus (`PPRE_BUS_2_Pos == 0xFFFFFFFFU` is used as a "doesn't exist"
marker) — in that case `System::APB2BusClock`/`TIMxAPB2Clock` are simply zeroed out, and all the
code that configures PPRE2 compiles into a no-op via `if constexpr`.

---

## 3. PLL — block diagram and parameters

```
Fsrc (HSI or HSE) ──/M──▶ VCO_in ──*N──▶ VCO_out ──/P──▶ sys_clk   (F4/F7, main output)
                                              └──────/R──▶ sys_clk   (F446xx: PLL_R as an alternative
                                                                       output, selected via
                                                                       SystemClockSource::PLL_R; on G0
                                                                       /R is the only output, /P is not
                                                                       used there for sys_clk)
```

`ValidatePLLCfgr()` checks three ranges **before** a single bit of PLLCFGR is written:

1. `VCO_in = Fsrc / M` must fall within `[PLL_CLK_IN_MIN, PLL_CLK_IN_MAX]` — the PLL has an input
   frequency window outside of which it physically cannot lock.
2. `VCO_out = VCO_in * N` must fall within `[PLL_N_CLK_MIN, PLL_N_CLK_MAX]`.
3. The final `PLL_Out` (after `/P` or `/R`) must not exceed `SYS_CLK_LIMIT` for the specific MCU.

If even one check fails, `Init()`/`InitCalcPLL()` return `SysInitStatus::InitError` **before**
disabling the current PLL and **before** writing PLLCFGR, so the working clock configuration isn't
destroyed on the way to a failed attempt (see [section 8](#8-validation-before-applying)).

### PLL_CFGR fields

| Field | Range (F4/F7, `STM32F446xx`/`STM32F767xx`) | Range (G0) | Purpose |
|------|------------------------------------------------|----------------|------------|
| `PLL_M` | 2 – 63 | 1 – 8 | Input divider: `VCO_in = Fsrc / M` |
| `PLL_N` | 50 – 432 | 8 – 86 | Multiplier: `VCO_out = VCO_in * N` |
| `PLL_P` | 2, 4, 6, 8 (only these 4 values) | 2 – 16 | Main output divider (F4/F7 sys_clk) |
| `PLL_Q` | 2 – 15 | — | Separate output for USB/SDIO/RNG (usually 48 MHz) |
| `PLL_R` | 2 – 7 (F446xx/F767xx only) | 2 – 16 | Alternative/main output into sys_clk (see below) |

`f446xx_pllr_out` is a special flag: on STM32F446xx, the only member of the family where you can
take `SystemClockSource::PLL_R` instead of `PLL`, in which case `PLL_Out` is computed via `/R`
rather than `/P`. On G0, `/R` is the only path into sys_clk (F/P are used there for other
peripheral outputs, not for SYSCLK), so `ConfigurePLL()` on G0 always sets `RCC_PLLCFGR_PLLREN`.

---

## 4. Limits by MCU family

All constants are `static constexpr` inside `ClockSystem`, read by the compiler at build time
(no runtime branching — `#if defined(...)` has already picked the right block):

| Family / chip | SYS_CLK_LIMIT | APB1_CLK_LIMIT | APB2_CLK_LIMIT | LATENCY_DIV |
|-----------------|---------------|-----------------|-----------------|--------------|
| STM32F446xx / F429xx | 180 MHz | 45 MHz | 90 MHz | 30 MHz/wait-state |
| STM32F405xx / F407xx | 168 MHz | 42 MHz | 84 MHz | 30 MHz/wait-state |
| STM32F722xx / F746xx / F767xx | 216 MHz | 54 MHz | 108 MHz | 30 MHz/wait-state |
| STM32F411xE | 100 MHz | 50 MHz | 100 MHz | 30 MHz/wait-state |
| STM32G0 | 64 MHz | 64 MHz | — (no APB2) | 24 MHz/wait-state |

If the target chip doesn't match any `#elif` — `#error "rcc.hpp: unsupported STM32 target..."`
stops the build with a clear message instead of silently using the wrong constant.

---

## 5. Files and roles

```
rcc.hpp
  ├─ enum SystemClockSource, AHB_Divider, APB_Divider, PLL_ClockSource  — map 1:1 onto CFGR/PLLCFGR bits
  ├─ struct BusDividers, PLL_CFGR                                       — parameter aggregates
  └─ class ClockSystem { static Init(...), static InitCalcPLL(...) }    — the entire API, no instantiation

rcc.cpp
  ├─ Init()               — the full manual path, see section 6
  ├─ EnableHSE()           — starts the crystal + waits for HSERDY
  ├─ ValidatePLLCfgr()     — the three ranges from section 3, before writing any registers
  ├─ ConfigurePLL()        — writes RCC->PLLCFGR (differs by family)
  ├─ ValidateBusDividers() — computes the AHB/APB1/APB2 dividers and checks them against the limits from section 4
  └─ InitCalcPLL()         — automatic M/N/P + bus divider selection, see section 9
```

---

## 6. Init() — step by step

```cpp
static SysInitStatus Init(SystemClockSource ClkSrc, uint32_t HSE_Clk,
                           BusDividers BusDiv, PLL_CFGR PLLCfgr);
```

1. **HSE, if needed** — if `ClkSrc == HSE` or the PLL takes its input from HSE: `EnableHSE(HSE_Clk)`,
   waits for `HSERDY` up to `HSE_TIMEOUT_MS` (100 ms). An error here aborts `Init()` immediately —
   the PLL/dividers haven't been touched yet.
2. **If the PLL isn't used** (`PLL_ClkSrc == NO`) — `sys_clk` is taken directly as `HSI_Clock` or
   `HSE_Clk`, and the PLL is skipped entirely.
3. **If the PLL is used**:
   - `ValidatePLLCfgr()` — all three ranges from [section 3](#3-pll--block-diagram-and-parameters).
     On failure — exit from `Init()`, nothing in RCC has been changed yet.
   - Disable the PLL (`RCC_CR_PLLON` = 0), wait for `PLLRDY` = 0 (timeout `PLL_TIMEOUT_MS` = 2 ms) —
     PLLCFGR cannot be reconfigured while the PLL is running.
   - `ConfigurePLL()` — writes M/N/P/Q(/R) into `RCC->PLLCFGR`.
   - **VOS/overdrive** (only if the target frequency requires a higher core voltage):
     - F7: if `sys_clk > 180 MHz` — enable `PWR_CR1_ODEN` (overdrive), wait for `ODRDY`,
       then `PWR_CR1_ODSWEN`, wait for `ODSWRDY` (both — timeout `OVERDRIVE_TIMEOUT_MS` = 100 ms).
     - G0: set `PWR_CR1_VOS = Scale 1` unconditionally, wait for `PWR_SR2_VOSF` to clear.
   - Enable the PLL (`RCC_CR_PLLON` = 1), wait for `PLLRDY` = 1 (timeout `PLL_TIMEOUT_MS`).
4. **`ValidateBusDividers(sys_clk, BusDiv)`** — before switching SYSCLK, checks that
   `sys_clk / AHB_Pre`, `.../APB1_Pre`, `.../APB2_Pre` stay within the limits from section 4.
5. **Flash latency** — see [section 7](#7-flash-latency); an error if the result is `> 9` wait states.
6. **Writing the dividers** — `RCC->CFGR` gets `HPRE`/`PPRE1`(/`PPRE2` if it exists).
7. **Switching SYSCLK** — `RCC->CFGR |= (uint32_t)ClkSrc`, waits for `RCC_CFGR_SWS`
   (timeout `CLOCKSWITCH_TIMEOUT_MS` = 5000 ms — the longest of all timeouts, since the source
   switch itself can be slow for some transitions).
8. **Updating the global variables** — `System::SystemCoreClock`, `APB1BusClock`,
   `TIMxAPB1Clock` (and the APB2 counterparts, if the bus exists) — see the "timers x2" rule in
   section 2.
9. **`System::InitTicks()`** — reconfigures SysTick for the new `SystemCoreClock` (otherwise
   `System::GetTick()` would count the wrong number of milliseconds after the frequency change).

Any step that returns an error stops `Init()` immediately (`return SysInitStatus::InitError`) — a
partially applied configuration is only possible if the hardware transition itself hangs
(a timeout), not because of the function's own logic.

---

## 7. Flash latency

Flash can't keep up supplying data at high frequencies without wait states. The formula in the
code:

```cpp
uint32_t latency = (sys_clk - 1) / LATENCY_DIV;   // LATENCY_DIV = 30 MHz (F4/F7) or 24 MHz (G0)
if (latency > 9) return SysInitStatus::InitError; // FLASH_ACR_LATENCY is a 4-bit field, max 9 on these MCUs
FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY_Msk) | (latency << FLASH_ACR_LATENCY_Pos);
```

Example: F4 at 180 MHz → `(180'000'000 - 1) / 30'000'000` = 5 wait states. G0 at 64 MHz →
`(64'000'000-1) / 24'000'000` = 2 wait states.

This write happens **before** switching `SYSCLK` to the new source — otherwise the core could
already be running at the new (higher) frequency with a latency computed for the old one.

---

## 8. Validation before applying

The order in `Init()` is deliberate: **validate all parameters first, then touch the registers**.
If the PLL or bus divider check fails, the function exits early, and:

- HSE (if it was enabled) stays enabled, but SYSCLK was never switched to it.
- The old PLL is already disabled (the "disable PLL" step runs before `ConfigurePLL()`), so in the
  worst case the MCU is left **on HSI after a failed reconfiguration attempt** — a known,
  predictable state, not hung half-configured peripherals.

This ordering is a deliberate trade-off: the current implementation has no fully safe rollback to
"the configuration that existed before calling `Init()`" (the PLL is already disabled by the time
the bus dividers are checked), but impossible configurations (a frequency outside the PLL's range,
a bus above its limit) are always rejected before `RCC_CR_PLLON`, so the PLL is never started with
coefficients that are already known to be wrong.

---

## 9. InitCalcPLL() — automatic coefficient selection

```cpp
static SysInitStatus InitCalcPLL(uint32_t req_freq, PLL_ClockSource pll_src,
                                  uint32_t hse_clk = 0, uint8_t pll_q = 2);
```

A simplified algorithm — not an optimal search, but a fixed heuristic aimed at a "typical" request
(frequencies that are multiples of 1 MHz, an HSE input in the tens-of-MHz range):

1. **`pll_m`** is chosen so that `VCO_in = input_clk / pll_m` is as close as possible to 2 MHz
   (rounded up on inexact division): `pll_m = ceil(input_clk / 2 000 000)`.
2. **`pll_n`** is chosen so that `VCO_out = req_freq * pll_n_scale` — rounded up to a whole number
   of MHz: `pll_n = ceil(req_freq / 1 000 000)`.
3. **`pll_p`** is fixed at 2 (the minimum divider — meaning `VCO_out` must equal `req_freq * 2`,
   which is exactly what step 2 implies when `VCO_in` ≈ 2 MHz).
4. **The bus dividers** are chosen using rough thresholds relative to the specific MCU's
   `APB1_CLK_LIMIT`/`APB2_CLK_LIMIT` (not relative to the actual `req_freq / prescaler`) — three
   cases: both buses `/1`, APB1 `/2` + APB2 `/1`, or APB1 `/4` + APB2 `/2`. This is a quick
   heuristic, not a search for the minimal dividers that give the maximum peripheral frequency —
   for non-standard `req_freq` values it's worth checking the resulting
   `System::APB1BusClock`/`APB2BusClock` after the call.
5. The assembled `PLL_CFGR{pll_src, pll_m, pll_n, pll_p, pll_q, /*pll_r=*/2}` and `BusDividers`
   are passed to the regular `Init()` — all the validation from sections 3/4/8 applies as usual,
   so an unreachable `req_freq` will still return `InitError` rather than silently configuring
   something else.

**Limitation**: the heuristic doesn't try alternative `M`/`N`/`P` combinations if the first one
fails validation — it returns `InitError` right away. For frequencies that don't land exactly on
whole MHz with `VCO_in` ≈ 2 MHz, you may need `Init()` with manually chosen parameters (for
example, from the STM32CubeMX calculator).

---

## 10. Timeouts

| Constant | Value | What it waits for |
|-----------|----------|----------|
| `HSI_TIMEOUT_MS` | 2 ms | HSI startup (declared but not used in the current `Init()` — HSI starts almost instantly and isn't explicitly disabled) |
| `LSI_TIMEOUT_MS` | 2 ms | LSI startup (used by other drivers, not directly by `rcc.cpp`) |
| `HSE_TIMEOUT_MS` | 100 ms | `RCC_CR_HSERDY` after `RCC_CR_HSEON` |
| `PLL_TIMEOUT_MS` | 2 ms | Both disabling (`PLLRDY` → 0) and enabling (`PLLRDY` → 1) the PLL |
| `OVERDRIVE_TIMEOUT_MS` | 100 ms | F7 `ODRDY`/`ODSWRDY`; G0 `VOSF` |
| `CLOCKSWITCH_TIMEOUT_MS` | 5000 ms | `RCC_CFGR_SWS` — confirmation that SYSCLK actually switched |

Any of these timeouts expiring before the flag is ready immediately aborts `Init()` with
`SysInitStatus::InitError` — not an infinite `while` loop (unlike `IRQ_Registry`'s `BKPT` traps;
here the expected response to a hardware failure is to return an error to the caller, not halt the
program).

---

## 11. Usage examples

### Automatic selection (InitCalcPLL) — the typical case, as in main.cpp

```cpp
System::Init();
ClockSystem::InitCalcPLL(180'000'000, ClockSystem::PLL_ClockSource::HSE, 8'000'000);
System::Enable_CYCCNT();
```

180 MHz from an 8 MHz external crystal (STM32F429), bus dividers are selected automatically based
on this chip's `APB1_CLK_LIMIT`/`APB2_CLK_LIMIT`.

### Manual configuration (Init) — full control over the PLL and dividers

```cpp
ClockSystem::BusDividers div;
div.AHB_div  = ClockSystem::AHB_Divider::DIV1;
div.APB1_div = ClockSystem::APB_Divider::DIV4;   // APB1 <= 45 MHz on F446xx
div.APB2_div = ClockSystem::APB_Divider::DIV2;   // APB2 <= 90 MHz

ClockSystem::PLL_CFGR pll;
pll.PLL_ClkSrc = ClockSystem::PLL_ClockSource::HSE;
pll.PLL_M = 4;    // 8 MHz / 4 = 2 MHz (VCO_in)
pll.PLL_N = 180;  // 2 MHz * 180 = 360 MHz (VCO_out)
pll.PLL_P = 2;    // 360 / 2 = 180 MHz (sys_clk)
pll.PLL_Q = 7;    // 360 / 7 ≈ 51.4 MHz (USB — out of the 48 MHz spec, for field illustration only)

ClockSystem::Init(ClockSystem::SystemClockSource::PLL, 8'000'000, div, pll);
```

### Without the PLL — directly from HSI (the default)

```cpp
ClockSystem::Init();   // HSI, no dividers, no PLL — the simplest start
```

### Reading the HSE frequency after initialization

```cpp
uint32_t hse = ClockSystem::GetHSEClock();  // 0 if HSE was never enabled
```

---

## 12. Edge cases

- **`STM32F446xx` + `SystemClockSource::PLL_R`** — the only combination where the final `sys_clk`
  is computed via `/R` rather than `/P`; passed through the internal `f446xx_pllr_out` flag in
  `ValidatePLLCfgr()`.
- **STM32G0 has no APB2** — `PPRE_BUS_2_Pos` = `0xFFFFFFFFU` serves as a sentinel value; all code
  touching APB2 is wrapped in `if constexpr (PPRE_BUS_2_Pos != 0xFFFFFFFFU)`, so on G0 these
  branches aren't compiled at all (not merely skipped at runtime).
- **`HSE_Clk > 26 MHz`** is unconditionally rejected in `EnableHSE()` — a limitation of the current
  implementation, it doesn't account for bypass mode (an external oscillator instead of a crystal),
  marked `// todo` in the code.
- **Bus divider validation runs fresh on every `Init()`** — `AHB_Pre`/`APB1_Pre`/`APB2_Pre` are
  reset at the start of `ValidateBusDividers()`; old values from a previous call are never reused,
  even partially.
- **`InitCalcPLL()` requires `hse_clk != 0` if `pll_src == HSE`** — otherwise `InitError`
  immediately, before any computation.

---

## See also

- [System.md](System.md) — exactly where `SystemCoreClock`/`APB1BusClock`/... get written and who
  reads them (`System::Init()`, `System::InitTicks()`, the order of calls in `main()`).
- [UART.md](UART.md), [TIM.md](TIM.md), [SPI.md](SPI.md) — consumers of `bus_clk`: each computes
  its own `BRR`/`PSC`/`ARR` from the variables this module configures.

---

## Русский

## Содержание

1. [Зачем это нужно](#1-зачем-это-нужно)
2. [Путь тактового сигнала](#2-путь-тактового-сигнала)
3. [PLL — блок-схема и параметры](#3-pll--блок-схема-и-параметры)
4. [Лимиты по семействам МК](#4-лимиты-по-семействам-мк)
5. [Файлы и роли](#5-файлы-и-роли)
6. [Init() — пошагово](#6-init--пошагово)
7. [Flash latency](#7-flash-latency)
8. [Валидация до применения](#8-валидация-до-применения)
9. [InitCalcPLL() — автоподбор коэффициентов](#9-initcalcpll--автоподбор-коэффициентов)
10. [Таймауты](#10-таймауты)
11. [Примеры использования](#11-примеры-использования)
12. [Угловые случаи](#12-угловые-случаи)

---

## 1. Зачем это нужно

`ClockSystem` — статический класс (как `IRQ_Registry`, без инстанцирования), который настраивает
дерево тактирования МК: выбор источника SYSCLK (HSI/HSE/PLL), параметры PLL, делители шин
AHB/APB1/APB2, задержку Flash (wait states) и производные переменные (`System::SystemCoreClock`,
`System::APB1BusClock` и т.д.), которые потом читают все остальные драйверы (UART, TIM, ...) для
расчёта своих делителей.

Два способа получить нужную частоту:

- **`Init()`** — вы сами задаёте PLL_M/N/P/Q/R и делители шин. Полный контроль, но нужно вручную
  соблюдать все ограничения даташита.
- **`InitCalcPLL()`** — вы задаёте только желаемую SYSCLK и источник PLL; M/N/P и делители шин
  вычисляются автоматически (см. [раздел 9](#9-initcalcpll--автоподбор-коэффициентов)).

---

## 2. Путь тактового сигнала

```
┌────────┐     ┌────────┐                                        ┌──────────────┐
│  HSI   │     │  HSE    │                                       │   SYSCLK     │
│ (внутр.│     │(кварц,  │                                       │ (RCC->CFGR   │
│ RC-ген)│     │Init()'s │                                       │  SW-биты)    │
└───┬────┘     │HSE_Clk) │                                       └──────┬───────┘
    │          └───┬─────┘                                              │
    │              │                                                    │
    │   ┌──────────┴───────────┐                                        │
    │   │  PLL (см. раздел 3)  │────────────────────────────────────────┤
    │   │  Fsrc /M → VCO_in    │                                        │
    │   │  VCO_in *N → VCO_out │                                        │
    │   │  VCO_out /P (или /R) │                                        │
    │   └──────────────────────┘                                        │
    └────────────────── HSI/HSE напрямую (PLL_ClkSrc == NO) ─────────────┘
                                                                          │
                                                                          ▼
                                                              ┌───────────────────────┐
                                                              │  AHB prescaler (HPRE)  │
                                                              │  sys_clk / AHB_Pre     │
                                                              └───────────┬───────────┘
                                                                          │  System::SystemCoreClock
                                                     ┌────────────────────┼────────────────────┐
                                                     ▼                                          ▼
                                       ┌─────────────────────────┐                ┌─────────────────────────┐
                                       │ APB1 prescaler (PPRE1)  │                │ APB2 prescaler (PPRE2)  │
                                       │ AHB_clk / APB1_Pre      │                │ AHB_clk / APB2_Pre      │
                                       │ = System::APB1BusClock  │                │ = System::APB2BusClock  │
                                       └────────────┬────────────┘                └────────────┬────────────┘
                                                    │ если APBx_Pre > 1:                        │
                                                    │ таймеры на этой шине                       │
                                                    │ получают APBxClock * 2                     │
                                                    ▼                                            ▼
                                       System::TIMxAPB1Clock                     System::TIMxAPB2Clock
```

**Правило "таймеры x2"**: аппаратно, если APBx-делитель `> 1`, тактовая частота, которую видят
таймеры на этой шине, — это `APBxClock * 2`, а не `APBxClock` (компенсация архитектуры шины
Cortex-M). Если делитель `== 1`, таймеры получают ту же частоту, что и остальная периферия шины.
Именно поэтому в коде две отдельные переменные (`System::APB1BusClock` для периферии,
`System::TIMxAPB1Clock` для `TIM::SetFrequency()`), а не одна:

```cpp
System::TIMxAPB1Clock = (APB1_Pre == 1) ? (System::APB1BusClock) : (System::APB1BusClock * 2);
```

На STM32G0 нет второй шины APB2 (`PPRE_BUS_2_Pos == 0xFFFFFFFFU` как маркер "не существует") —
`System::APB2BusClock`/`TIMxAPB2Clock` в этом случае просто зануляются, а весь код, который
конфигурирует PPRE2, компилируется в no-op через `if constexpr`.

---

## 3. PLL — блок-схема и параметры

```
Fsrc (HSI или HSE) ──/M──▶ VCO_in ──*N──▶ VCO_out ──/P──▶ sys_clk   (F4/F7, основной выход)
                                              └──────/R──▶ sys_clk   (F446xx: PLL_R как альтернативный выход,
                                                                       выбирается SystemClockSource::PLL_R;
                                                                       на G0 /R — единственный выход, /P туда
                                                                       не используется для sys_clk)
```

`ValidatePLLCfgr()` проверяет три диапазона **до** того, как хоть один бит PLLCFGR будет записан:

1. `VCO_in = Fsrc / M` должен попасть в `[PLL_CLK_IN_MIN, PLL_CLK_IN_MAX]` — у PLL есть окно входной
   частоты, вне которого он физически не залочится.
2. `VCO_out = VCO_in * N` должен попасть в `[PLL_N_CLK_MIN, PLL_N_CLK_MAX]`.
3. Итоговый `PLL_Out` (после `/P` или `/R`) не должен превышать `SYS_CLK_LIMIT` для конкретного МК.

Если хоть одна проверка не проходит — `Init()`/`InitCalcPLL()` возвращают `SysInitStatus::InitError`
**до** выключения текущего PLL и **до** записи PLLCFGR, так что рабочая конфигурация тактирования
не разрушается по пути к неудачной попытке (см. [раздел 8](#8-валидация-до-применения)).

### Поля PLL_CFGR

| Поле | Диапазон (F4/F7, `STM32F446xx`/`STM32F767xx`) | Диапазон (G0) | Назначение |
|------|------------------------------------------------|----------------|------------|
| `PLL_M` | 2 – 63 | 1 – 8 | Входной делитель: `VCO_in = Fsrc / M` |
| `PLL_N` | 50 – 432 | 8 – 86 | Умножитель: `VCO_out = VCO_in * N` |
| `PLL_P` | 2, 4, 6, 8 (только эти 4 значения) | 2 – 16 | Основной выходной делитель (F4/F7 sys_clk) |
| `PLL_Q` | 2 – 15 | — | Отдельный выход для USB/SDIO/RNG (48 МГц обычно) |
| `PLL_R` | 2 – 7 (только F446xx/F767xx) | 2 – 16 | Альтернативный/основной выход в sys_clk (см. ниже) |

`f446xx_pllr_out` — специальный флаг: на STM32F446xx единственном из семейства можно взять
`SystemClockSource::PLL_R` вместо `PLL`, тогда `PLL_Out` считается через `/R`, а не `/P`. На G0
`/R` — единственный путь в sys_clk (F/P там используются для других периферийных выходов, не для
SYSCLK), поэтому `ConfigurePLL()` на G0 всегда включает `RCC_PLLCFGR_PLLREN`.

---

## 4. Лимиты по семействам МК

Все константы — `static constexpr` внутри `ClockSystem`, читаются компилятором на этапе сборки
(нет рантайм-ветвления — `#if defined(...)` уже выбрал нужный блок):

| Семейство / чип | SYS_CLK_LIMIT | APB1_CLK_LIMIT | APB2_CLK_LIMIT | LATENCY_DIV |
|-----------------|---------------|-----------------|-----------------|--------------|
| STM32F446xx / F429xx | 180 МГц | 45 МГц | 90 МГц | 30 МГц/wait-state |
| STM32F405xx / F407xx | 168 МГц | 42 МГц | 84 МГц | 30 МГц/wait-state |
| STM32F722xx / F746xx / F767xx | 216 МГц | 54 МГц | 108 МГц | 30 МГц/wait-state |
| STM32F411xE | 100 МГц | 50 МГц | 100 МГц | 30 МГц/wait-state |
| STM32G0 | 64 МГц | 64 МГц | — (нет APB2) | 24 МГц/wait-state |

Если целевой чип не попадает ни под один `#elif` — `#error "rcc.hpp: unsupported STM32 target..."`
останавливает сборку с понятным сообщением вместо тихой неправильной константы.

---

## 5. Файлы и роли

```
rcc.hpp
  ├─ enum SystemClockSource, AHB_Divider, APB_Divider, PLL_ClockSource  — маппятся 1:1 на биты CFGR/PLLCFGR
  ├─ struct BusDividers, PLL_CFGR                                       — агрегаты параметров
  └─ class ClockSystem { static Init(...), static InitCalcPLL(...) }    — весь API, без инстанцирования

rcc.cpp
  ├─ Init()               — полный ручной путь, см. раздел 6
  ├─ EnableHSE()           — запуск кварца + ожидание HSERDY
  ├─ ValidatePLLCfgr()     — три диапазона из раздела 3, до записи регистров
  ├─ ConfigurePLL()        — запись RCC->PLLCFGR (различается по семействам)
  ├─ ValidateBusDividers() — считает AHB/APB1/APB2 делители и сверяет лимиты из раздела 4
  └─ InitCalcPLL()         — автоподбор M/N/P + делителей шин, см. раздел 9
```

---

## 6. Init() — пошагово

```cpp
static SysInitStatus Init(SystemClockSource ClkSrc, uint32_t HSE_Clk,
                           BusDividers BusDiv, PLL_CFGR PLLCfgr);
```

1. **HSE, если нужен** — если `ClkSrc == HSE` или PLL берёт вход с HSE: `EnableHSE(HSE_Clk)`,
   ждёт `HSERDY` до `HSE_TIMEOUT_MS` (100 мс). Ошибка здесь прерывает `Init()` немедленно —
   PLL/делители ещё не тронуты.
2. **Если PLL не используется** (`PLL_ClkSrc == NO`) — `sys_clk` берётся напрямую как `HSI_Clock`
   или `HSE_Clk`, PLL полностью пропускается.
3. **Если PLL используется**:
   - `ValidatePLLCfgr()` — все три диапазона из [раздела 3](#3-pll--блок-схема-и-параметры).
     Ошибка — выход из `Init()`, ничего в RCC ещё не менялось.
   - Выключить PLL (`RCC_CR_PLLON` = 0), дождаться `PLLRDY` = 0 (таймаут `PLL_TIMEOUT_MS` = 2 мс) —
     нельзя перенастраивать PLLCFGR, пока PLL работает.
   - `ConfigurePLL()` — записать M/N/P/Q(/R) в `RCC->PLLCFGR`.
   - **VOS/overdrive** (только если целевая частота требует повышенного напряжения ядра):
     - F7: если `sys_clk > 180 МГц` — включить `PWR_CR1_ODEN` (overdrive), дождаться `ODRDY`,
       затем `PWR_CR1_ODSWEN`, дождаться `ODSWRDY` (оба — таймаут `OVERDRIVE_TIMEOUT_MS` = 100 мс).
     - G0: выставить `PWR_CR1_VOS = Scale 1` безусловно, дождаться сброса `PWR_SR2_VOSF`.
   - Включить PLL (`RCC_CR_PLLON` = 1), дождаться `PLLRDY` = 1 (таймаут `PLL_TIMEOUT_MS`).
4. **`ValidateBusDividers(sys_clk, BusDiv)`** — до переключения SYSCLK проверяет, что
   `sys_clk / AHB_Pre`, `.../APB1_Pre`, `.../APB2_Pre` укладываются в лимиты из раздела 4.
5. **Flash latency** — см. [раздел 7](#7-flash-latency); ошибка, если получилось `> 9` wait states.
6. **Запись делителей** — `RCC->CFGR` получает `HPRE`/`PPRE1`(/`PPRE2` если есть).
7. **Переключение SYSCLK** — `RCC->CFGR |= (uint32_t)ClkSrc`, ждёт `RCC_CFGR_SWS`
   (таймаут `CLOCKSWITCH_TIMEOUT_MS` = 5000 мс — самый долгий из всех таймаутов, т.к. само
   переключение источника может быть медленным при некоторых переходах).
8. **Обновление глобальных переменных** — `System::SystemCoreClock`, `APB1BusClock`,
   `TIMxAPB1Clock` (и APB2-аналоги, если шина существует) — см. правило "таймеры x2" в разделе 2.
9. **`System::InitTicks()`** — перенастраивает SysTick под новую `SystemCoreClock` (иначе
   `System::GetTick()` после смены частоты отсчитывал бы неверные миллисекунды).

Любой шаг, вернувший ошибку, останавливает `Init()` немедленно (`return SysInitStatus::InitError`) —
частично применённая конфигурация возможна только если сам аппаратный переход завис (таймаут), а не
из-за логики функции.

---

## 7. Flash latency

Flash не успевает отдавать данные на высоких частотах без wait states. Формула в коде:

```cpp
uint32_t latency = (sys_clk - 1) / LATENCY_DIV;   // LATENCY_DIV = 30 МГц (F4/F7) или 24 МГц (G0)
if (latency > 9) return SysInitStatus::InitError; // FLASH_ACR_LATENCY — 4-битное поле, максимум 9 на этих МК
FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY_Msk) | (latency << FLASH_ACR_LATENCY_Pos);
```

Пример: F4 на 180 МГц → `(180'000'000 - 1) / 30'000'000` = 5 wait states. G0 на 64 МГц →
`(64'000'000-1) / 24'000'000` = 2 wait states.

Запись происходит **до** переключения `SYSCLK` на новый источник — иначе ядро могло бы уже
работать на новой (высокой) частоте с латентностью, рассчитанной под старую.

---

## 8. Валидация до применения

Порядок в `Init()` намеренно такой: **сначала проверить все параметры, потом трогать регистры**.
Если проверка PLL или делителей шин проваливается, функция выходит рано и:

- HSE (если был включен) остаётся включённым, но SYSCLK на него не переключался.
- Старый PLL уже выключен (шаг "Disable PLL" выполняется перед `ConfigurePLL()`), поэтому в
  худшем случае МК остаётся на **HSI после неудачной попытки перенастройки** — известное,
  предсказуемое состояние, а не зависшая полу-настроенная периферия.

Такой порядок — осознанный компромисс: полностью безопасного отката до "конфигурации, которая
была до вызова `Init()`" в текущей реализации нет (PLL уже выключен к моменту проверки делителей
шин), но невозможные конфигурации (частота вне диапазона PLL, шина выше лимита) всегда
отбраковываются до `RCC_CR_PLLON`, так что PLL никогда не запускается с заведомо неверными
коэффициентами.

---

## 9. InitCalcPLL() — автоподбор коэффициентов

```cpp
static SysInitStatus InitCalcPLL(uint32_t req_freq, PLL_ClockSource pll_src,
                                  uint32_t hse_clk = 0, uint8_t pll_q = 2);
```

Упрощённый алгоритм — не оптимальный перебор, а фиксированная эвристика, рассчитанная на
"типичный" запрос (частоты, кратные 1 МГц, вход от HSE с частотой в единицы-десятки МГц):

1. **`pll_m`** выбирается так, чтобы `VCO_in = input_clk / pll_m` было как можно ближе к 2 МГц
   (округление вверх при неточном делении): `pll_m = ceil(input_clk / 2 000 000)`.
2. **`pll_n`** выбирается так, чтобы `VCO_out = req_freq * pll_n_scale` — округление вверх до
   целого числа МГц: `pll_n = ceil(req_freq / 1 000 000)`.
3. **`pll_p`** зафиксирован = 2 (минимальный делитель — значит `VCO_out` должен быть равен
   `req_freq * 2`, что и подразумевает шаг 2 при `VCO_in` ≈ 2 МГц).
4. **Делители шин** выбираются по грубым порогам относительно `APB1_CLK_LIMIT`/`APB2_CLK_LIMIT`
   самого МК (не относительно фактической `req_freq / prescaler`) — три случая: обе шины `/1`,
   APB1 `/2`+APB2 `/1`, или APB1 `/4`+APB2 `/2`. Это быстрая эвристика, а не поиск минимальных
   делителей, дающих максимальную частоту периферии, — при нестандартных `req_freq` стоит
   проверить получившиеся `System::APB1BusClock`/`APB2BusClock` после вызова.
5. Собранные `PLL_CFGR{pll_src, pll_m, pll_n, pll_p, pll_q, /*pll_r=*/2}` и `BusDividers`
   передаются в обычный `Init()` — вся валидация из разделов 3/4/8 применяется как обычно, так что
   недостижимая `req_freq` всё равно вернёт `InitError`, а не тихо настроит что-то другое.

**Ограничение**: эвристика не перебирает альтернативные `M`/`N`/`P`, если первая комбинация не
проходит валидацию — она возвращает `InitError` сразу. Для частот, которые не ложатся ровно на
целочисленные МГц с `VCO_in` ≈ 2 МГц, может потребоваться `Init()` с параметрами, подобранными
вручную (например, в калькуляторе STM32CubeMX).

---

## 10. Таймауты

| Константа | Значение | Что ждёт |
|-----------|----------|----------|
| `HSI_TIMEOUT_MS` | 2 мс | Запуск HSI (объявлена, но в текущем `Init()` не используется — HSI стартует практически мгновенно и не выключается явно) |
| `LSI_TIMEOUT_MS` | 2 мс | Запуск LSI (используется другими драйверами, не `rcc.cpp` напрямую) |
| `HSE_TIMEOUT_MS` | 100 мс | `RCC_CR_HSERDY` после `RCC_CR_HSEON` |
| `PLL_TIMEOUT_MS` | 2 мс | И выключение (`PLLRDY` → 0), и включение (`PLLRDY` → 1) PLL |
| `OVERDRIVE_TIMEOUT_MS` | 100 мс | F7 `ODRDY`/`ODSWRDY`; G0 `VOSF` |
| `CLOCKSWITCH_TIMEOUT_MS` | 5000 мс | `RCC_CFGR_SWS` — подтверждение факта переключения SYSCLK |

Любой из этих таймаутов, истёкший до готовности флага, немедленно прерывает `Init()` с
`SysInitStatus::InitError` — не бесконечный `while` (в отличие от `IRQ_Registry`'s `BKPT`-ловушек,
здесь ожидаемая реакция на сбой оборудования — вернуть ошибку вызывающему коду, а не остановить
программу).

---

## 11. Примеры использования

### Автоподбор (InitCalcPLL) — типичный случай, как в main.cpp

```cpp
System::Init();
ClockSystem::InitCalcPLL(180'000'000, ClockSystem::PLL_ClockSource::HSE, 8'000'000);
System::Enable_CYCCNT();
```

180 МГц из внешнего кварца 8 МГц (STM32F429), делители шин подбираются автоматически исходя
из `APB1_CLK_LIMIT`/`APB2_CLK_LIMIT` этого чипа.

### Ручная настройка (Init) — полный контроль над PLL и делителями

```cpp
ClockSystem::BusDividers div;
div.AHB_div  = ClockSystem::AHB_Divider::DIV1;
div.APB1_div = ClockSystem::APB_Divider::DIV4;   // APB1 <= 45 МГц на F446xx
div.APB2_div = ClockSystem::APB_Divider::DIV2;   // APB2 <= 90 МГц

ClockSystem::PLL_CFGR pll;
pll.PLL_ClkSrc = ClockSystem::PLL_ClockSource::HSE;
pll.PLL_M = 4;    // 8 МГц / 4 = 2 МГц (VCO_in)
pll.PLL_N = 180;  // 2 МГц * 180 = 360 МГц (VCO_out)
pll.PLL_P = 2;    // 360 / 2 = 180 МГц (sys_clk)
pll.PLL_Q = 7;    // 360 / 7 ≈ 51.4 МГц (USB — вне спецификации 48 МГц, только для примера полей)

ClockSystem::Init(ClockSystem::SystemClockSource::PLL, 8'000'000, div, pll);
```

### Без PLL — напрямую от HSI (значение по умолчанию)

```cpp
ClockSystem::Init();   // HSI, без делителей, без PLL — самый простой старт
```

### Чтение частоты HSE после инициализации

```cpp
uint32_t hse = ClockSystem::GetHSEClock();  // 0, если HSE не был включён
```

---

## 12. Угловые случаи

- **`STM32F446xx` + `SystemClockSource::PLL_R`** — единственная комбинация, где итоговая
  `sys_clk` считается через `/R`, а не `/P`; передаётся через внутренний флаг
  `f446xx_pllr_out` в `ValidatePLLCfgr()`.
- **STM32G0 не имеет APB2** — `PPRE_BUS_2_Pos` = `0xFFFFFFFFU` служит sentinel-значением;
  весь код, трогающий APB2, обёрнут в `if constexpr (PPRE_BUS_2_Pos != 0xFFFFFFFFU)`, поэтому
  на G0 эти ветки не компилируются вообще (а не просто пропускаются в рантайме).
- **`HSE_Clk > 26 МГц`** отклоняется в `EnableHSE()` безусловно — граница текущей реализации,
  не учитывает bypass-режим (внешний генератор вместо кварца), помечено `// todo` в коде.
- **Валидация делителей шин запускается заново при каждом `Init()`** — `AHB_Pre`/`APB1_Pre`/
  `APB2_Pre` сбрасываются в начале `ValidateBusDividers()`, старые значения от предыдущего
  вызова не переиспользуются даже частично.
- **`InitCalcPLL()` требует `hse_clk != 0`, если `pll_src == HSE`** — иначе `InitError` сразу,
  до каких-либо вычислений.

---

## См. также

- [System.md](System.md) — куда именно пишутся `SystemCoreClock`/`APB1BusClock`/... и кто их
  читает (`System::Init()`, `System::InitTicks()`, порядок вызовов в `main()`).
- [UART.md](UART.md), [TIM.md](TIM.md), [SPI.md](SPI.md) — потребители `bus_clk`: каждый считает
  свой `BRR`/`PSC`/`ARR` от переменных, которые настраивает этот модуль.
