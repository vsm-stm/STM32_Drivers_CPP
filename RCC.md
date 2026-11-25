# RCC / ClockSystem — Detailed Description

*[English](#english) · [Русский](#русский)*

---

## English

## Table of Contents

1. [Why This Is Needed](#1-why-this-is-needed)
2. [Initialization Order](#2-initialization-order)
3. [Automatic PLL selection](#3-automatic-pll-selection)
4. [Manual configuration](#4-manual-configuration)
5. [Static validation and supported devices](#5-static-validation-and-supported-devices)
6. [Runtime sequence](#6-runtime-sequence)
7. [SysTick precision](#7-systick-precision)
8. [Migration](#8-migration)
9. [APB1 from the RCC Configuration for I2C](#9-apb1-from-the-rcc-configuration-for-i2c)
10. [Usage Examples](#10-usage-examples)
11. [Edge Cases](#11-edge-cases)
12. [See Also](#see-also)

---

## 1. Why This Is Needed

`ClockSystem` sets up the chip's clock tree (oscillator → PLL → SYSCLK → AHB/APB buses) **once** after reset.

The work is split in two:

| At compile time | At run time |
| --- | --- |
| PLL coefficients, bus dividers, Flash wait states, SysTick periods, all limit checks | Write the precomputed register values, wait for oscillators/PLL to become ready, switch SYSCLK |

`Prepare()` keeps the common sequence and calls functional stages: voltage limits, source selection, PLL validation/frequency/encoding, buses, latency, SysTick and transition preparation. `rcc.hpp` selects one family header from `src/hardware/rcc/`. The choices are `rcc_defs_f4_f7.hpp`, `rcc_defs_g0.hpp`, `rcc_defs_g4.hpp` or `rcc_defs_l0.hpp`. Each provides its limits, register encodings and family functions under common names. `rcc_common.hpp` holds the shared types and helpers. `ClockSystem` keeps its public type names and uses the same `Prepare()` sequence and bus calculations across families. All preparation calls are evaluated for `static constexpr Prepared` in `Init<config>()`; they add no run-time call chain. `CalcPLL()` uses a separate selected search implementation and retains its previous search order. Hardware application stays in `rcc.cpp` with its existing local helpers and family/capability blocks. No additional hardware-stage call chain or inline register implementations are introduced in the header.

A wrong configuration therefore fails the **build**, not the device.

---

## 2. Initialization Order

```text
System::Init()  →  ClockSystem::Init<config>()  →  peripherals
```

- Works on STM32F4, F7, G0, L0 and G4.
- One clock setup per hardware reset.
- Requires C++20 (already selected by the build).

---

## 3. Automatic PLL selection

Ask for a SYSCLK frequency and let `CalcPLL` find exact PLL coefficients. The following 64 MHz example is for F4/F7/G0/G4; L0 has a 32 MHz maximum and the voltage-range argument shown below.

```cpp
constexpr auto clocks = ClockSystem::CalcPLL(
    64 * MHz, ClockSystem::PLL_ClockSource::HSI);

if (System::Init() != SysInitStatus::InitOK)
    System::DebugTrap("System init");
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
    System::DebugTrap("Clock init");
```

**`CalcPLL(frequency, source, hse_clock = 0, pll_q = 2)`:**

- targets the exact nominal **SYSCLK**;
- sets AHB to /1 and picks the smallest legal divider for each APB bus;
- for HSE, pass the real crystal frequency in Hz as `hse_clock`;
- `pll_q` is up to you — USB/SDIO/RNG clock requirements are **not** derived from SYSCLK.

**How the search works:**

1. Try PLL input dividers M from small to large — this gives the highest legal PLL input (comparison) frequency, which means lower jitter. ST recommends about 2 MHz on F4/F7 and about 16 MHz on G0 ([F4](https://raw.githubusercontent.com/STMicroelectronics/stm32f4xx-hal-driver/master/Inc/stm32f4xx_hal_rcc.h), [F7](https://raw.githubusercontent.com/STMicroelectronics/stm32f7xx-hal-driver/master/Inc/stm32f7xx_hal_rcc.h), [G0](https://raw.githubusercontent.com/STMicroelectronics/stm32g0xx-hal-driver/master/Inc/stm32g0xx_hal_rcc.h) RCC docs).
2. For the same M, take the smallest legal output divider: P on F4/F7, R on G0/G4.
3. Accept only exact results, checked with 64-bit integers:

```text
Fsource * N == Frequested * M * output_divider
```

L0 has no PLL input divider: it searches the allowed multiplier/output-divider pairs, preferring the smallest output divider. Its fourth argument is `VoltageRange` instead of `pll_q`; the default is Range 2.

**No exact solution?** `CalcPLL` returns a configuration with `M = 0` (L0: `PLL_MUL = 0`), and `Init<clocks>()` fails a `static_assert` — the build stops. Choose another frequency or write a manual configuration. There is no "closest frequency" fallback.

---

## 4. Manual configuration

Example: F407, 8 MHz crystal, SYSCLK/HCLK 168 MHz, APB1 42 MHz, APB2 84 MHz:

```cpp
constexpr ClockSystem::Config clocks{
    .ClkSrc  = ClockSystem::SystemClockSource::PLL,
    .HSE_Clk = 8 * MHz,
    .BusDiv  = {ClockSystem::AHB_Divider::DIV1,    // AHB  = 168 MHz
                ClockSystem::APB_Divider::DIV4,    // APB1 =  42 MHz
                ClockSystem::APB_Divider::DIV2},   // APB2 =  84 MHz
    .PLLCfgr = {ClockSystem::PLL_ClockSource::HSE, 4, 168, 2, 7, 2}, // M, N, P, Q, R
};
// System::Init() has already run.
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
    System::DebugTrap("Clock init");
```

**Other sources:**

| Source | How to configure |
| --- | --- |
| HSI, no PLL | `constexpr ClockSystem::Config clocks{};` |
| HSE, no PLL | `.ClkSrc = HSE` and `.HSE_Clk = <crystal Hz>` |
| LSI (G0 only) | nominal 32000 Hz |
| LSE (G0 only) | 32768 Hz; backup-domain write access is opened temporarily, the previous DBP bit is restored, RTC is not reset |

**Which fields matter:** `ClkSrc` decides the active path.

- For direct sources (HSI/HSE/LSI/LSE) the PLL fields are ignored.
- `HSE_Clk` is ignored unless HSE actually feeds SYSCLK or the PLL.
- F4/F7: `PLL` selects the P output; on F446 `PLL_R` selects the R output.
- G0: `PLL` selects R. P/Q divisors are written where present, but the P/Q outputs stay disabled. G0 has no APB2.
- R is ignored on devices without an R field.

---

### L0: voltage ranges, manual PLL and MSI

The default `Config{}` selects HSI16 and Range 2, with 1 WS. `System::Init()` itself starts on MSI range 5 and retains reset Range 2. To request 32 MHz, explicitly select Range 1:

```cpp
using C = ClockSystem;
constexpr auto clocks = C::CalcPLL(32 * MHz, C::PLL_ClockSource::HSI,
                                  0U, C::VoltageRange::Range1);
// Manual equivalent: HSI16 * 4 / 2 = 32 MHz.
constexpr C::Config manual{
    .ClkSrc = C::SystemClockSource::PLL,
    .PLLCfgr = {.PLL_ClkSrc = C::PLL_ClockSource::HSI,
                .PLL_MUL = 4, .PLL_DIV = 2},
    .Voltage = C::VoltageRange::Range1
};
// Apply exactly one configuration, after System::Init().
if (C::Init<clocks>() != SysInitStatus::InitOK) System::DebugTrap("Clock init");
```

For low-frequency MSI without starting HSI:

```cpp
constexpr ClockSystem::Config clocks{
    .ClkSrc = ClockSystem::SystemClockSource::MSI,
    .MSIRange = ClockSystem::MSI_Range::Range0,
    .Voltage = ClockSystem::VoltageRange::Range3
}; // 65,536 Hz, 0 WS; call Init<clocks>() after System::Init().
```

| MSI range | Nominal Hz |
| --- | ---: |
| Range0 | 65,536 |
| Range1 | 131,072 |
| Range2 | 262,144 |
| Range3 | 524,288 |
| Range4 | 1,048,576 |
| Range5 | 2,097,152 |
| Range6 | 4,194,304 |

Voltage-range CPU limits apply to HCLK after AHB division; for example, direct HSI16 with AHB /4 permits HCLK 4 MHz in Range 3. The PLL VCO limit is checked separately. Flash latency: Range 1 is 0 WS through 16 MHz, then 1 WS; Range 2 is 0 WS through 8 MHz, then 1 WS; Range 3 is 0 WS through 4.2 MHz. `Prepare()` also checks the voltage-dependent VCO ceiling; a small output frequency alone does not make an excessive VCO valid. Flash/EEPROM programming and erase are not available in Range 3. This RCC adaptation does not port the Flash driver to L0.

MSI uses the same integer SysTick calculation as other sources. AHB choices yielding HCLK below 1 kHz are rejected instead of producing a zero reload. Oscillator tolerances remain additional to integer-reload error.

### G4: automatic normal/boost and PLL R

```cpp
constexpr auto clocks = ClockSystem::CalcPLL(
    170 * MHz, ClockSystem::PLL_ClockSource::HSI);
// M=4, N=85, R=2; SYSCLK/APB1/APB2=170 MHz, Range 1 boost, 4 WS.
// After System::Init(): ClockSystem::Init<clocks>().
```

At SYSCLK 150 MHz or lower, normal mode is used. Flash latency is calculated from HCLK with 30 MHz bands in normal mode, 34 MHz bands in boost. Mode selection uses SYSCLK even when AHB divides it. All coefficients, latency, intermediate AHB/tick values and bus clocks are calculated at compilation; hardware application retains the one-attempt-per-reset contract.

References: [L0 RM0451](https://www.st.com/resource/en/reference_manual/dm00443854-ultra-low-power-stm32l0x0-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf), [L0 RM0376](https://www.st.com/resource/en/reference_manual/dm00108281.pdf), [G4 RCC sequence](https://raw.githubusercontent.com/STMicroelectronics/stm32g4xx-hal-driver/master/Src/stm32g4xx_hal_rcc.c), [G4 voltage scaling](https://raw.githubusercontent.com/STMicroelectronics/stm32g4xx-hal-driver/master/Src/stm32g4xx_hal_pwr_ex.c).

---

## 5. Static validation and supported devices

`Init<config>()` rejects at compile time:

- unsupported sources and invalid enum values;
- HSE outside its limits;
- PLL coefficients out of range;
- PLL input, VCO or output frequency out of range;
- bus frequencies above the device maximum;
- a SysTick period that does not fit the 24-bit counter.

Limits are compared as exact fractions before any division. For manual configurations producing fractional-Hz frequencies, the published `uint32_t` frequency variables are rounded **down** to whole Hz. Automatic selection is exact to the requested Hz.

Numeric data shared by the search and bus calculations are grouped in the nine fields of `RCCDefs::ClockLimits`: SYSCLK/APB limits, M/N ranges and PLL-input limits. The selected `inline constexpr ClockLimits limits{...}` uses named fields. HSE/VCO/minimum-PLL-output checks and voltage/Flash rules live in their functions instead of universal table columns. Unused fields remain zero; register masks, oscillator references and wait timeouts remain separate constants. Public names such as `ClockSystem::Config` are aliases for `RCCDefs` types. Rebuild the driver library and consuming firmware together after updating. The search receives the common `Prepare()` by function reference, so candidate validation is reused rather than duplicated.

**Device limits** (`rcc_defs_f4_f7.hpp` covers every F4/F7 selector in STM32-base_files):

| Devices | SYSCLK/HCLK max | APB1 max | APB2 max |
| --- | ---: | ---: | ---: |
| F401 | 84 MHz | 42 MHz | 84 MHz |
| F410/411/412/413/423 | 100 MHz | 50 MHz | 100 MHz |
| F405/407/415/417 | 168 MHz | 42 MHz | 84 MHz |
| F427/429/437/439/446/469/479 | 180 MHz | 45 MHz | 90 MHz |
| F722/723/730/732/733/745/746/750/756/765/767/769/777/779 | 216 MHz | 54 MHz | 108 MHz |
| G0 | 64 MHz | 64 MHz | — |
| L0 | SYSCLK ≤32 MHz; HCLK ≤32/16/4.2 MHz in Range 1/2/3 | ≤HCLK | ≤HCLK |
| G4 | 170 MHz (boost); 150 MHz (normal) | same as SYSCLK limit | same as SYSCLK limit |

Unknown F4/F7 selectors and families outside F4/F7/G0/L0/G4 stop with a clear `#error`.

**Power assumptions** (fixed, not measured):

- VDD = 3000 mV, i.e. the 2.7…3.6 V timing band (also valid at 3.3 V).
- Voltage scaling: F401 — Scale 2; other F4/F7 — Scale 1; G0 — Range 1. L0 selects `Config::Voltage` (default Range 2). G4 selects Range 1 normal up to SYSCLK 150 MHz, boost above it.
- Over-drive is enabled when required: F4 above HCLK 168 MHz (where present), F7 above 180 MHz.
- Flash latency is derived from HCLK: one wait state per 30 MHz on F4/F7, per 24 MHz on G0 Range 1.

**PLL limits:**

| Device | N | VCO min | Other |
| --- | --- | --- | --- |
| F401 | ≥ 192 | 192 MHz ([RM0368](https://www.st.com/resource/en/reference_manual/rm0368-stm32f401xbc-and-stm32f401xde-advanced-armbased-32bit-mcusstmicroelectronics.pdf)) | |
| F469/479 | ≥ 50 | 192 MHz ([DS11189](https://www.st.com/resource/en/datasheet/stm32f469ii.pdf), [DS11118](https://www.st.com/resource/en/datasheet/stm32f479ni.pdf)) | |
| Other F4/F7 | ≥ 50 | 100 MHz | |
| G0 | 8…86 | 96 MHz | M = 1…8, R = 2…8, P = 2…32, Q = 2…8 where present ([G030 datasheet](https://www.st.com/resource/en/datasheet/stm32g030f6.pdf), [RM0444](https://www.st.com/resource/en/reference_manual/dm00371828-stm32g0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)) |

Minimum SYSCLK from PLL: 24 MHz on F4/F7 (F446: 12.5 MHz), 12 MHz on G0, 2 MHz on L0, 8 MHz on G4.

L0 accepts an HSE crystal frequency of 1..25 MHz; a PLL input must be 2..24 MHz. Its VCO maximum is 96/48/24 MHz for Range 1/2/3. G4 uses M=1..16, N=8..127, R/Q in {2,4,6,8}, P=2..31 and VCO 64..344 MHz; P/Q outputs remain disabled. These limits do not add HSE bypass, USB or other peripheral-clock configuration.

---

## 6. Runtime sequence

What `Init<config>()` does on the hardware:

1. **One attempt only.** A second call is rejected before touching registers. A hardware failure (oscillator does not start) also uses up the attempt — recover with a reset.
2. Enable the required oscillator. F4/F7/G0/G4 reuse the HSI16 already running after `System::Init()`. On L0, RCC starts HSI if it supplies SYSCLK or the PLL.
3. If using the PLL: stop the old unused PLL, set voltage scaling while the PLL is off, program and start the PLL, wait for lock. On F4/F7 wait for VOSRDY after the PLL starts, then enable over-drive if needed. On G0/L0/G4 wait for VOSF to clear. L0 stops an inherited unused PLL even for a direct source before applying its voltage range. G4 applies normal/boost before increasing SYSCLK.
4. Raise Flash latency (one masked ACR write) **before** HCLK goes up.
5. Set bus dividers while still running from the reset source (L0: MSI range 5; others: HSI16). Reload SysTick if the new divider changes HCLK. Clear TIMPRE where present so timer clocks follow the standard ×1/×2 rule.
6. Switch SYSCLK (one masked CFGR write) and wait until SWS reports exactly that source.
7. Publish the final HCLK and immediately reload SysTick with its precomputed period; check `InitTicks()`; publish APB/timer clocks and the HSE frequency.
8. Lower Flash latency only after the frequency decrease is confirmed.

G4 switches to PLL above 80 MHz with final AHB /1 through an intermediate AHB /2, then restores /1. SysTick is reloaded at each HCLK stage. L0 changes the MSI range after preparing power and latency; because MSI is already SYSCLK, its tick is immediately updated.

**Timeouts:**

| Wait | Limit |
| --- | --- |
| PLL lock, HSI/MSI/LSI ready | 2 ms |
| HSE ready | 100 ms |
| LSE ready | 5000 ms |
| Voltage scaling / over-drive / Flash | 100 ms |
| SYSCLK switch | 5000 ms |

**Requirements:**

- Timeouts use SysTick: call from `main` with interrupts enabled.
- Stop peripherals, DMA and auxiliary PLLs before this initial setup.
- Changing clocks later at run time is **not** supported.
- Each clock change restarts SysTick (the current partial tick is lost; the tick counter keeps its value).
- There is no automatic fallback to another oscillator, no retry logic and no IRQ locking.

---

## 7. SysTick precision

```text
cycles_per_tick = floor(HCLK / TICK_BASE)      LOAD = cycles_per_tick - 1
actual tick rate = HCLK / cycles_per_tick
```

If HCLK is not a multiple of `TICK_BASE`, the tick is not exactly 1 kHz.

**Example — LSE 32768 Hz, AHB /1:** period = 32 cycles → 1024 ticks/s → one `GetTick()` step = 0.9765625 ms. `Delay_ms()` and all tick-based timeouts share this error. With LSI, the RC oscillator's own tolerance adds to it.

No fractional-period compensation is done. "Exact" PLL selection means the nominal SYSCLK, not crystal accuracy and not necessarily an exact 1 kHz SysTick.

---

## 8. Migration

| Old API | New API |
| --- | --- |
| `ClockSystem::Init(...)` at run time | `constexpr ClockSystem::Config` + `ClockSystem::Init<config>()` |
| `ClockSystem::InitCalcPLL(...)` | `constexpr auto c = ClockSystem::CalcPLL(...)` + `Init<c>()` |

- `GetHSEClock()` and the System clock variables remain.
- `System::InitTicks(ticks)` takes a precomputed cycle count; the old no-argument version is kept for existing callers.
- Compile-time preparation does **not** require LTO. LTO and the syscalls/sysmem exclusions are a separate build topic.

---

## 9. APB1 from the RCC Configuration for I2C

`ClockSystem::GetAPB1Clock<clocks>()` returns the nominal APB1 frequency of a configuration at compile time (`consteval`):

- uses the same preparation/validation as `Init<config>()` — no duplicated formulas;
- never touches hardware;
- an invalid `Config` fails a `static_assert`.

I2C uses it to compute its timing register at compile time:

```cpp
constexpr ClockSystem::Config clocks{};          // HSI, all dividers /1
// In main, after a successful System::Init():
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK) return;
// bus is an I2C object with bound pins/DMA:
auto status = bus.SetUp<clocks, I2C::SPEED::FAST>();
```

Before touching hardware, I2C compares that nominal APB1 with `System::APB1BusClock`. Equal numbers do **not** prove that RCC init succeeded or that the configurations are identical — always check the result of `ClockSystem::Init` yourself. These are nominal calculations, not measurements.

---

## 10. Usage Examples

- [Direct HSI, automatic PLL and passing clocks to consumers](#direct-hsi-automatic-pll-and-passing-clocks-to-consumers)

A complete minimal example (HSI, all dividers /1):

```cpp
#include "rcc.hpp"

constexpr ClockSystem::Config clocks{};   // HSI, bus dividers /1

int main()
{
    if (System::Init() != SysInitStatus::InitOK)
        System::DebugTrap("System init");
    if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
        System::DebugTrap("RCC init");
    // An I2C bus configured later uses the same config: bus.SetUp<clocks>().
}
```

---

### Direct HSI, automatic PLL and passing clocks to consumers

Choose exactly one startup function per reset, after System::Init and before peripheral setup. Both Config objects are constexpr; the PLL search and validation are not run-time calculations. Direct HSI ignores PLL coefficients. For HSE, pass the board's actual crystal frequency to CalcPLL, as in section 3. A manual Config is shown in that section for targets where an exact automatic solution is unavailable. GetAPB1Clock derives a nominal value without applying hardware; it is not a measurement and does not replace checking Init's result.

```cpp
#include "rcc.hpp"
constexpr ClockSystem::Config hsi{};
constexpr auto pll = ClockSystem::CalcPLL(32 * MHz, ClockSystem::PLL_ClockSource::HSI);
SysInitStatus StartHSI() { return ClockSystem::Init<hsi>(); }
SysInitStatus StartPLL() { return ClockSystem::Init<pll>(); }
constexpr uint32_t apb1 = ClockSystem::GetAPB1Clock<pll>();
static_assert(apb1 > 0);
uint32_t ConfiguredHSE() { return ClockSystem::GetHSEClock(); }
```

---

## 11. Edge Cases

| Situation | Behaviour |
| --- | --- |
| Supply voltage other than 2.7…3.6 V | Not modelled — limits assume VDD = 3000 mV. |
| Second `Init<config>()` call | Rejected; dynamic reconfiguration is not supported. |
| No exact PLL solution | Build error; provide a manual configuration. |
| HCLK not a multiple of `TICK_BASE` (e.g. LSE) | SysTick is slightly off 1 kHz (see section 7). |

---

## See Also

- [RM0090 — F405/407/415/417/427/429/437/439](https://www.st.com/resource/en/reference_manual/dm00031020-stm32f405-407-415-417-437-455-469-application-note-stmicroelectronics.pdf)
- [RM0383 — F411](https://www.st.com/resource/en/reference_manual/rm0383-stm32f411xce-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [RM0401 — F410](https://www.st.com/resource/en/reference_manual/rm0401-stm32f410-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [RM0402 — F412](https://www.st.com/resource/en/reference_manual/rm0402-stm32f412-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [RM0430 — F413/423](https://www.st.com/resource/en/reference_manual/rm0430-stm32f413423-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [RM0431 — F72/73 Flash/VOS timing](https://www.st.com/resource/en/reference_manual/rm0431-stm32f72xxx-and-stm32f73xxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [F746 datasheet — PLL operating limits](https://www.st.com/resource/en/datasheet/stm32f746zg.pdf)
- [F767 datasheet — PLL operating limits](https://www.st.com/resource/en/datasheet/stm32f767zi.pdf)
- [System.md](System.md)
- [I2C.md](I2C.md)
- [UART.md](UART.md)

---

## Русский

## Содержание

1. [Зачем это нужно](#1-зачем-это-нужно)
2. [Порядок инициализации](#2-порядок-инициализации)
3. [API и проверка при компиляции](#3-api-и-проверка-при-компиляции)
4. [Точная частота и приоритет для jitter](#4-точная-частота-и-приоритет-для-jitter)
5. [Модели, питание и порядок переключения](#5-модели-питание-и-порядок-переключения)
6. [Погрешность SysTick](#6-погрешность-systick)
7. [APB1 из конфигурации RCC для I2C](#7-apb1-из-конфигурации-rcc-для-i2c)
8. [Примеры использования](#8-примеры-использования)
9. [Угловые случаи](#9-угловые-случаи)
10. [См. также](#см-также)

---

## 1. Зачем это нужно

`ClockSystem` **один раз** после сброса настраивает дерево тактирования (генератор → PLL → SYSCLK → шины AHB/APB).

Работа разделена на две части:

| При компиляции | Во время работы |
| --- | --- |
| Коэффициенты PLL, делители шин, задержки Flash, периоды SysTick, все проверки пределов | Запись заранее вычисленных значений в регистры, ожидание готовности генераторов/PLL, переключение SYSCLK |

Поэтому неверная конфигурация ломает **сборку**, а не устройство.

`Prepare()` сохраняет общую последовательность и вызывает функциональные этапы: пределы питания, выбор источника, проверку/расчёт/кодирование PLL, шины, latency, SysTick и подготовку перехода. `rcc.hpp` подключает один заголовок из `src/hardware/rcc/` для выбранного семейства. Доступны `rcc_defs_f4_f7.hpp`, `rcc_defs_g0.hpp`, `rcc_defs_g4.hpp` или `rcc_defs_l0.hpp`. В нём заданы пределы, кодирование регистров и функции семейства под общими именами. Общие типы и вспомогательные функции находятся в `rcc_common.hpp`. `ClockSystem` сохраняет публичные имена типов и использует общий порядок `Prepare()` и расчёт шин. Все вызовы подготовки вычисляются для `static constexpr Prepared` внутри `Init<config>()` и не добавляют цепочку вызовов во время работы. `CalcPLL()` использует отдельно выбранный алгоритм поиска с прежним порядком перебора. Применение настроек остаётся в `rcc.cpp` с прежними локальными функциями и блоками семейств/возможностей. Дополнительная цепочка аппаратных этапов и inline-реализации регистровых операций в заголовке не добавляются.

---

## 2. Порядок инициализации

```text
System::Init()  →  ClockSystem::Init<config>()  →  периферия
```

- Работает на STM32F4, F7, G0, L0 и G4.
- Одна настройка тактирования на один аппаратный сброс.
- Нужен C++20 (уже выбран сборкой).

---

## 3. API и проверка при компиляции

**Автоматический расчёт PLL** — укажите частоту SYSCLK, `CalcPLL` подберёт точные коэффициенты:

```cpp
constexpr auto clocks = ClockSystem::CalcPLL(
    64 * MHz, ClockSystem::PLL_ClockSource::HSI);

if (System::Init() != SysInitStatus::InitOK)
    System::DebugTrap("System init");
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
    System::DebugTrap("Clock init");
```

**Ручная конфигурация** — пример для F407, кварц 8 МГц, SYSCLK/HCLK 168 МГц, APB1 42 МГц, APB2 84 МГц:

```cpp
constexpr ClockSystem::Config clocks{
    .ClkSrc  = ClockSystem::SystemClockSource::PLL,
    .HSE_Clk = 8 * MHz,
    .BusDiv  = {ClockSystem::AHB_Divider::DIV1,    // AHB  = 168 МГц
                ClockSystem::APB_Divider::DIV4,    // APB1 =  42 МГц
                ClockSystem::APB_Divider::DIV2},   // APB2 =  84 МГц
    .PLLCfgr = {ClockSystem::PLL_ClockSource::HSE, 4, 168, 2, 7, 2}, // M, N, P, Q, R
};
// System::Init() уже выполнен.
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
    System::DebugTrap("Clock init");
```

Для HSI без PLL достаточно `constexpr ClockSystem::Config clocks{};`. Старые `Init(...)` и `InitCalcPLL(...)` заменены этим API.

**Что проверяет `Init<config>()` через `static_assert`:** источники, коэффициенты PLL, частоты входа/VCO/выхода PLL, значения делителей, пределы SYSCLK/HCLK/APB, период SysTick.

**Что вычисляется при компиляции:** значения полей регистров, задержка Flash, итоговые частоты шин и таймеров, периоды SysTick.

**Что остаётся на время работы:** запись настроек, ожидание аппаратных флагов и отказ при повторном вызове. LTO для этого не нужен.

**Какие поля важны:** `ClkSrc` определяет активный путь.

- Для прямых источников (HSI/HSE/LSI/LSE) поля PLL игнорируются.
- `HSE_Clk` игнорируется, если HSE не питает SYSCLK или PLL.
- F4/F7: `PLL` выбирает выход P; на F446 `PLL_R` выбирает выход R.
- G0 поддерживает HSI/HSE/PLL/LSI/LSE; `PLL` использует выход R. Делители P/Q записываются, но выходы P/Q не включаются. APB2 на G0 нет.
- LSE включается с временным разрешением записи в backup-домен; прежнее значение DBP восстанавливается, RTC не сбрасывается.

---

## 4. Точная частота и приоритет для jitter

**`CalcPLL(frequency, source, hse_clock = 0, pll_q = 2)`:**

- рассчитывает точную номинальную **SYSCLK**;
- ставит AHB /1 и выбирает минимальные допустимые делители APB;
- для HSE передайте реальную частоту кварца в Гц в `hse_clock`;
- `pll_q` задаёте вы — требования USB/SDIO/RNG из целевой SYSCLK **не** выводятся.

**Как идёт поиск:**

1. Делитель входа M перебирается от меньшего к большему — так получается максимальная допустимая частота сравнения PLL, а значит меньший джиттер. Ориентиры ST: около 2 МГц на входе PLL для F4/F7 и около 16 МГц для G0 (документация RCC: [F4](https://raw.githubusercontent.com/STMicroelectronics/stm32f4xx-hal-driver/master/Inc/stm32f4xx_hal_rcc.h), [F7](https://raw.githubusercontent.com/STMicroelectronics/stm32f7xx-hal-driver/master/Inc/stm32f7xx_hal_rcc.h), [G0](https://raw.githubusercontent.com/STMicroelectronics/stm32g0xx-hal-driver/master/Inc/stm32g0xx_hal_rcc.h)).
2. При одинаковом M выбирается минимальный выходной делитель: P на F4/F7, R на G0/G4.
3. L0 перебирает дискретные множители и делители, предпочитая минимальный выходной делитель. Четвёртый аргумент `CalcPLL()` на L0 — `VoltageRange`, по умолчанию Range 2; при отсутствии решения возвращается `PLL_MUL=0`.
4. Принимается только точный результат, проверка в 64-битных целых:

```text
Fsource * N == Frequested * M * P/R
```

**Точного решения нет?** `CalcPLL` вернёт конфигурацию с `M = 0` (на L0 — `PLL_MUL = 0`), и `Init<config>()` остановит сборку. Выберите другую частоту или задайте конфигурацию вручную (она тоже проверяется). Округления «до ближайшей частоты» нет.

Пределы частот сравниваются как точные дроби до деления. Если ручная конфигурация даёт дробные герцы, опубликованные переменные `uint32_t` округляются **вниз** до целых Гц; автоматический расчёт точен до заданного герца.

---

### L0: диапазон питания, ручной PLL и MSI

По умолчанию `Config{}` выбирает HSI16 и Range 2 с 1 WS. Сам `System::Init()` оставляет штатный MSI range 5 и Range 2. Для 32 МГц нужно явно выбрать Range 1:

```cpp
using C = ClockSystem;
constexpr auto clocks = C::CalcPLL(32 * MHz, C::PLL_ClockSource::HSI,
                                  0U, C::VoltageRange::Range1);
// Ручной эквивалент: HSI16 * 4 / 2 = 32 МГц.
constexpr C::Config manual{
    .ClkSrc = C::SystemClockSource::PLL,
    .PLLCfgr = {.PLL_ClkSrc = C::PLL_ClockSource::HSI,
                .PLL_MUL = 4, .PLL_DIV = 2},
    .Voltage = C::VoltageRange::Range1
};
// После System::Init() применить ровно одну конфигурацию.
if (C::Init<clocks>() != SysInitStatus::InitOK) System::DebugTrap("Clock init");
```

Низкочастотный MSI без запуска HSI:

```cpp
constexpr ClockSystem::Config clocks{
    .ClkSrc = ClockSystem::SystemClockSource::MSI,
    .MSIRange = ClockSystem::MSI_Range::Range0,
    .Voltage = ClockSystem::VoltageRange::Range3
}; // 65 536 Гц, 0 WS; после System::Init() вызвать Init<clocks>().
```

| Диапазон MSI | Номинальные Гц |
| --- | ---: |
| Range0 | 65 536 |
| Range1 | 131 072 |
| Range2 | 262 144 |
| Range3 | 524 288 |
| Range4 | 1 048 576 |
| Range5 | 2 097 152 |
| Range6 | 4 194 304 |

Предел частоты ядра для диапазона питания применяется к HCLK после делителя AHB: например, HSI16 с AHB /4 даёт допустимые 4 МГц в Range 3. Предел VCO проверяется отдельно. Flash latency: Range 1 — 0 WS до 16 МГц, затем 1 WS; Range 2 — 0 WS до 8 МГц, затем 1 WS; Range 3 — 0 WS до 4,2 МГц. `Prepare()` проверяет и зависящий от диапазона предел VCO: низкая выходная частота сама по себе не делает завышенную VCO допустимой. В Range 3 недоступны запись и стирание Flash/EEPROM. Эта адаптация RCC не переносит драйвер Flash на L0.

Для MSI используется тот же целочисленный расчёт SysTick. Делители AHB, дающие HCLK ниже 1 кГц, отклоняются вместо нулевого reload. К погрешности целочисленного тика добавляется погрешность самого генератора.

### G4: автоматический normal/boost и выход PLL R

```cpp
constexpr auto clocks = ClockSystem::CalcPLL(
    170 * MHz, ClockSystem::PLL_ClockSource::HSI);
// M=4, N=85, R=2; SYSCLK/APB1/APB2=170 МГц, Range 1 boost, 4 WS.
// После System::Init(): ClockSystem::Init<clocks>().
```

До SYSCLK 150 МГц включительно используется normal. Flash latency считается от HCLK с шагом 30 МГц в normal и 34 МГц в boost. Режим выбирается по SYSCLK, в том числе при делителе AHB. Коэффициенты, latency, промежуточные AHB/тик и частоты шин считаются при компиляции; аппаратная настройка сохраняет правило одной попытки на сброс.

Источники: [L0 RM0451](https://www.st.com/resource/en/reference_manual/dm00443854-ultra-low-power-stm32l0x0-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf), [L0 RM0376](https://www.st.com/resource/en/reference_manual/dm00108281.pdf), [последовательность RCC G4](https://raw.githubusercontent.com/STMicroelectronics/stm32g4xx-hal-driver/master/Src/stm32g4xx_hal_rcc.c), [питание G4](https://raw.githubusercontent.com/STMicroelectronics/stm32g4xx-hal-driver/master/Src/stm32g4xx_hal_pwr_ex.c).

---

## 5. Модели, питание и порядок переключения

Данные, общие для поиска и расчёта шин, объединены в девять полей `RCCDefs::ClockLimits`: пределы SYSCLK/APB, диапазоны M/N и входа PLL. Выбранный `inline constexpr ClockLimits limits{...}` задаётся именованными полями. Проверки HSE/VCO/минимального выхода PLL и правила питания/Flash находятся в своих функциях вместо универсальных столбцов таблицы. Неиспользуемые поля остаются нулевыми; маски регистров, опорные частоты и таймауты — отдельными константами. Публичные имена, например `ClockSystem::Config`, сохранены как алиасы типов `RCCDefs`. После обновления пересоберите библиотеку и использующие её прошивки вместе. Поиск получает общую `Prepare()` по ссылке на функцию, поэтому проверка кандидата используется повторно, а не дублируется.

**Пределы моделей** (`rcc_defs_f4_f7.hpp` покрывает все F4/F7 из STM32-base_files):

| МК | SYSCLK/HCLK макс. | APB1 макс. | APB2 макс. |
| --- | ---: | ---: | ---: |
| F401 | 84 МГц | 42 МГц | 84 МГц |
| F410/411/412/413/423 | 100 МГц | 50 МГц | 100 МГц |
| F405/407/415/417 | 168 МГц | 42 МГц | 84 МГц |
| F427/429/437/439/446/469/479 | 180 МГц | 45 МГц | 90 МГц |
| F722/723/730/732/733/745/746/750/756/765/767/769/777/779 | 216 МГц | 54 МГц | 108 МГц |
| G0 | 64 МГц | 64 МГц | — |
| L0 | SYSCLK ≤32 МГц; HCLK ≤32/16/4,2 МГц в Range 1/2/3 | ≤HCLK | ≤HCLK |
| G4 | 170 МГц (boost); 150 МГц (normal) | как предел SYSCLK | как предел SYSCLK |

Неизвестная модель F4/F7 или семейство вне F4/F7/G0/L0/G4 — явный `#error`.

**Допущения по питанию** (фиксированы, не измеряются):

- VDD = 3000 мВ, то есть диапазон таймингов 2,7…3,6 В (подходит и для 3,3 В).
- Масштаб напряжения ядра: F401 — Scale 2; остальные F4/F7 — Scale 1; G0 — Range 1. На L0 выбирается `Config::Voltage` (по умолчанию Range 2). G4 выбирает Range 1 normal до SYSCLK 150 МГц, boost выше этой частоты.
- Over-drive включается при необходимости: F4 — при HCLK выше 168 МГц (где он есть), F7 — выше 180 МГц.
- Задержка Flash считается от HCLK: один такт ожидания на каждые 30 МГц для F4/F7 и 24 МГц для G0 Range 1.

**Пределы PLL:**

| МК | N | VCO мин. | Прочее |
| --- | --- | --- | --- |
| F401 | ≥ 192 | 192 МГц | |
| F469/479 | ≥ 50 | 192 МГц (по datasheet) | |
| Остальные F4/F7 | ≥ 50 | 100 МГц | |
| G0 | 8…86 | 96 МГц (по datasheet) | M = 1…8, R = 2…8, P = 2…32, Q = 2…8 при наличии |

Минимальная SYSCLK от PLL: 24 МГц для F4/F7 (F446 — 12,5 МГц), 12 МГц для G0, 2 МГц для L0, 8 МГц для G4.

На L0 поддерживается частота кварца HSE 1..25 МГц; вход PLL — 2..24 МГц. Предел VCO — 96/48/24 МГц для Range 1/2/3. На G4: M=1..16, N=8..127, R/Q из {2,4,6,8}, P=2..31, VCO 64..344 МГц; выходы P/Q не включаются. Поддержка HSE bypass, USB и настройки прочих периферийных тактов этим не добавляется.

**Что `Init<config>()` делает с аппаратурой:**

1. **Только одна попытка.** Повторный вызов отклоняется до записи регистров. Аппаратный отказ (генератор не запустился) тоже расходует попытку — нужен сброс.
2. Включается нужный источник. На F4/F7/G0/G4 HSI16 уже работает после `System::Init()`. На L0 RCC включает HSI, если он питает SYSCLK или PLL. Для PLL: остановить оставшуюся неиспользуемую PLL, задать масштаб напряжения, записать коэффициенты, запустить PLL, дождаться готовности напряжения и при необходимости over-drive. На F4/F7 VOSRDY ожидается после запуска PLL. На L0 неиспользуемая PLL останавливается и при прямом источнике — до выбора диапазона питания. G0/L0/G4 ожидают сброса VOSF; G4 выбирает normal/boost до повышения SYSCLK.
3. Задержка Flash повышается одной записью ACR **до** повышения HCLK.
4. Делители шин выставляются на штатном источнике (L0 — MSI range 5, остальные — HSI16). SysTick перенастраивается, если новый делитель изменил HCLK. TIMPRE сбрасывается (где есть), чтобы частоты таймеров подчинялись обычному правилу ×1/×2.
5. SYSCLK переключается одной записью CFGR по маске; ожидается точное значение SWS.
6. SysTick сразу перенастраивается по готовому периоду, результат проверяется, публикуются частоты APB и таймеров. Задержка Flash снижается только после подтверждённого снижения частоты.

При переключении G4 на PLL выше 80 МГц с итоговым AHB /1 сначала используется промежуточный AHB /2, затем возвращается /1. SysTick перенастраивается на каждом этапе HCLK. На L0 диапазон MSI меняется после подготовки питания и latency; поскольку MSI уже является SYSCLK, тик обновляется сразу.

**Пределы ожидания:**

| Ожидание | Предел |
| --- | --- |
| PLL, HSI/MSI/LSI | 2 мс |
| HSE | 100 мс |
| LSE | 5000 мс |
| Напряжение / over-drive / Flash | 100 мс |
| Переключение SYSCLK | 5000 мс |

**Требования:**

- Таймауты считаются по SysTick: вызывайте из `main` с разрешёнными прерываниями.
- Периферия, DMA и вспомогательные PLL должны быть остановлены до этой начальной настройки.
- Изменение тактирования во время работы **не поддерживается**.
- При смене HCLK SysTick перезапускается (часть текущего периода теряется, счётчик `GetTick()` сохраняется).

---

## 6. Погрешность SysTick

```text
период = floor(HCLK / TICK_BASE) тактов      LOAD = период - 1
реальная частота тика = HCLK / период
```

Если HCLK не кратна `TICK_BASE`, тик отличается от 1 кГц.

**Пример — LSE 32768 Гц, AHB /1:** период 32 такта → 1024 тика/с → один шаг `GetTick()` = 0,9765625 мс. `Delay_ms()` и таймауты имеют ту же погрешность; для LSI добавляется неточность самого RC-генератора.

Период 0 или не помещающийся в 24-битный счётчик отклоняется при компиляции. Компенсации дробного периода (чередования периодов, дополнительного таймера) нет.

Для готового периода есть `System::InitTicks(ticks)`; старая перегрузка без аргумента сохранена. LTO и исключение `syscalls.c`/`sysmem.c` из LTO — отдельный вопрос настройки сборки.

---

## 7. APB1 из конфигурации RCC для I2C

`ClockSystem::GetAPB1Clock<clocks>()` возвращает номинальную частоту APB1 конфигурации на этапе компиляции (`consteval`):

- использует ту же подготовку и проверки, что `Init<config>()` — формулы не дублируются;
- не запускает генераторы и не трогает регистры;
- неверный `Config` даёт `static_assert`.

I2C берёт APB1 через этот метод, чтобы рассчитать регистр таймингов при компиляции:

```cpp
constexpr ClockSystem::Config clocks{};          // HSI, все делители /1
// В main, после успешного System::Init():
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK) return;
// bus — объект I2C с закреплёнными выводами/DMA:
auto status = bus.SetUp<clocks, I2C::SPEED::FAST>();
```

Перед настройкой аппаратуры I2C сравнивает эту номинальную APB1 с `System::APB1BusClock`. Совпадение чисел **не доказывает**, что RCC настроен успешно или что конфигурации одинаковы — результат `ClockSystem::Init` проверяйте сами. Это номинальный расчёт, а не измерение частоты.

---

## 8. Примеры использования

- [Прямой HSI, автоматический PLL и передача конфигурации потребителям](#прямой-hsi-автоматический-pll-и-передача-конфигурации-потребителям)

Полный минимальный пример (HSI, все делители /1):

```cpp
#include "rcc.hpp"

constexpr ClockSystem::Config clocks{};   // HSI, делители шин /1

int main()
{
    if (System::Init() != SysInitStatus::InitOK)
        System::DebugTrap("System init");
    if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
        System::DebugTrap("RCC init");
    // I2C, настраиваемый позже, использует ту же конфигурацию: bus.SetUp<clocks>().
}
```

---

### Прямой HSI, автоматический PLL и передача конфигурации потребителям

За один reset выбирается одна функция настройки, после System::Init и до периферии. Оба Config — constexpr; поиск PLL и проверки не выполняются во время работы. Прямой HSI игнорирует коэффициенты PLL. Для HSE передайте фактическую частоту кварца платы в CalcPLL, как в разделе 3. Там же показан ручной Config для целей, где точного автоматического решения нет. GetAPB1Clock вычисляет номинальную частоту без настройки аппаратуры: это не измерение и не замена проверке результата Init.

```cpp
#include "rcc.hpp"
constexpr ClockSystem::Config hsi{};
constexpr auto pll = ClockSystem::CalcPLL(32 * MHz, ClockSystem::PLL_ClockSource::HSI);
SysInitStatus StartHSI() { return ClockSystem::Init<hsi>(); }
SysInitStatus StartPLL() { return ClockSystem::Init<pll>(); }
constexpr uint32_t apb1 = ClockSystem::GetAPB1Clock<pll>();
static_assert(apb1 > 0);
uint32_t ConfiguredHSE() { return ClockSystem::GetHSEClock(); }
```

---

## 9. Угловые случаи

| Ситуация | Поведение |
| --- | --- |
| Питание вне 2,7…3,6 В | Не моделируется — пределы рассчитаны на VDD = 3000 мВ. |
| Повторный `Init<config>()` | Отклоняется; перенастройка во время работы не поддерживается. |
| Нет точного решения PLL | Ошибка сборки; задайте конфигурацию вручную. |
| HCLK не кратна `TICK_BASE` (например, LSE) | SysTick немного отличается от 1 кГц (см. раздел 6). |

---

## См. также

- [RM0090 — F405/407/415/417/427/429/437/439](https://www.st.com/resource/en/reference_manual/dm00031020-stm32f405-407-415-417-437-455-469-application-note-stmicroelectronics.pdf)
- [RM0383 — F411](https://www.st.com/resource/en/reference_manual/rm0383-stm32f411xce-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [RM0401 — F410](https://www.st.com/resource/en/reference_manual/rm0401-stm32f410-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [RM0402 — F412](https://www.st.com/resource/en/reference_manual/rm0402-stm32f412-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [RM0430 — F413/423](https://www.st.com/resource/en/reference_manual/rm0430-stm32f413423-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [RM0431 — F72/73 Flash/VOS timing](https://www.st.com/resource/en/reference_manual/rm0431-stm32f72xxx-and-stm32f73xxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [F746 datasheet — PLL operating limits](https://www.st.com/resource/en/datasheet/stm32f746zg.pdf)
- [F767 datasheet — PLL operating limits](https://www.st.com/resource/en/datasheet/stm32f767zi.pdf)
- [RM0368 — F401](https://www.st.com/resource/en/reference_manual/rm0368-stm32f401xbc-and-stm32f401xde-advanced-armbased-32bit-mcusstmicroelectronics.pdf), [DS11189 — F469](https://www.st.com/resource/en/datasheet/stm32f469ii.pdf), [DS11118 — F479](https://www.st.com/resource/en/datasheet/stm32f479ni.pdf), [G030 datasheet](https://www.st.com/resource/en/datasheet/stm32g030f6.pdf), [RM0444 — G0x1](https://www.st.com/resource/en/reference_manual/dm00371828-stm32g0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [System.md](System.md)
- [I2C.md](I2C.md)
- [UART.md](UART.md)
