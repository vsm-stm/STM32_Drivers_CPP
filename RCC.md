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

A wrong configuration therefore fails the **build**, not the device.

---

## 2. Initialization Order

```text
System::Init()  →  ClockSystem::Init<config>()  →  peripherals
```

- Works on STM32F4, F7 and G0.
- One clock setup per hardware reset.
- Requires C++20 (already selected by the build).

---

## 3. Automatic PLL selection

The easiest way: ask for a SYSCLK frequency and let `CalcPLL` find exact PLL coefficients.

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
2. For the same M, take the smallest legal output divider: P on F4/F7, R on G0.
3. Accept only exact results, checked with 64-bit integers:

```text
Fsource * N == Frequested * M * output_divider
```

**No exact solution?** `CalcPLL` returns a configuration with `M = 0`, and `Init<clocks>()` fails a `static_assert` — the build stops. Choose another frequency or write a manual configuration. There is no "closest frequency" fallback.

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

## 5. Static validation and supported devices

`Init<config>()` rejects at compile time:

- unsupported sources and invalid enum values;
- HSE outside its limits;
- PLL coefficients out of range;
- PLL input, VCO or output frequency out of range;
- bus frequencies above the device maximum;
- a SysTick period that does not fit the 24-bit counter.

Limits are compared as exact fractions before any division. For manual configurations producing fractional-Hz frequencies, the published `uint32_t` frequency variables are rounded **down** to whole Hz. Automatic selection is exact to the requested Hz.

**Device limits** (`rcc_defs.hpp` covers every F4/F7 selector in STM32-base_files):

| Devices | SYSCLK/HCLK max | APB1 max | APB2 max |
| --- | ---: | ---: | ---: |
| F401 | 84 MHz | 42 MHz | 84 MHz |
| F410/411/412/413/423 | 100 MHz | 50 MHz | 100 MHz |
| F405/407/415/417 | 168 MHz | 42 MHz | 84 MHz |
| F427/429/437/439/446/469/479 | 180 MHz | 45 MHz | 90 MHz |
| F722/723/730/732/733/745/746/750/756/765/767/769/777/779 | 216 MHz | 54 MHz | 108 MHz |
| G0 | 64 MHz | 64 MHz | — |

Unknown F4/F7 selectors and other families stop with a clear `#error`.

**Power assumptions** (fixed, not measured):

- VDD = 3000 mV, i.e. the 2.7…3.6 V timing band (also valid at 3.3 V).
- Voltage scaling: F401 — Scale 2; other F4/F7 — Scale 1; G0 — Range 1.
- Over-drive is enabled when required: F4 above HCLK 168 MHz (where present), F7 above 180 MHz.
- Flash latency is derived from HCLK: one wait state per 30 MHz on F4/F7, per 24 MHz on G0 Range 1.

**PLL limits:**

| Device | N | VCO min | Other |
| --- | --- | --- | --- |
| F401 | ≥ 192 | 192 MHz ([RM0368](https://www.st.com/resource/en/reference_manual/rm0368-stm32f401xbc-and-stm32f401xde-advanced-armbased-32bit-mcusstmicroelectronics.pdf)) | |
| F469/479 | ≥ 50 | 192 MHz ([DS11189](https://www.st.com/resource/en/datasheet/stm32f469ii.pdf), [DS11118](https://www.st.com/resource/en/datasheet/stm32f479ni.pdf)) | |
| Other F4/F7 | ≥ 50 | 100 MHz | |
| G0 | 8…86 | 96 MHz | M = 1…8, R = 2…8, P = 2…32, Q = 2…8 where present ([G030 datasheet](https://www.st.com/resource/en/datasheet/stm32g030f6.pdf), [RM0444](https://www.st.com/resource/en/reference_manual/dm00371828-stm32g0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)) |

Minimum SYSCLK from PLL: 24 MHz on F4/F7 (F446: 12.5 MHz), 12 MHz on G0.

---

## 6. Runtime sequence

What `Init<config>()` does on the hardware:

1. **One attempt only.** A second call is rejected before touching registers. A hardware failure (oscillator does not start) also uses up the attempt — recover with a reset.
2. Enable only the oscillator needed by `ClkSrc` (and the PLL's input).
3. If using the PLL: stop the old unused PLL, set voltage scaling while the PLL is off, program and start the PLL, wait for lock. On F4/F7 wait for VOSRDY after the PLL starts, then enable over-drive if needed. On G0 wait for VOSF to clear.
4. Raise Flash latency (one masked ACR write) **before** HCLK goes up.
5. Set bus dividers while still running from HSI; reload SysTick for the intermediate HCLK. Clear TIMPRE where present so timer clocks follow the standard ×1/×2 rule.
6. Switch SYSCLK (one masked CFGR write) and wait until SWS reports exactly that source.
7. Publish the final HCLK and immediately reload SysTick with its precomputed period; check `InitTicks()`; publish APB/timer clocks and the HSE frequency.
8. Lower Flash latency only after the frequency decrease is confirmed.

**Timeouts:**

| Wait | Limit |
| --- | --- |
| PLL lock, HSI/LSI ready | 2 ms |
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

---

## 2. Порядок инициализации

```text
System::Init()  →  ClockSystem::Init<config>()  →  периферия
```

- Работает на STM32F4, F7 и G0.
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
2. При одинаковом M выбирается минимальный выходной делитель: P на F4/F7, R на G0.
3. Принимается только точный результат, проверка в 64-битных целых:

```text
Fsource * N == Frequested * M * P/R
```

**Точного решения нет?** `CalcPLL` вернёт конфигурацию с `M = 0`, и `Init<config>()` остановит сборку. Выберите другую частоту или задайте конфигурацию вручную (она тоже проверяется). Округления «до ближайшей частоты» нет.

Пределы частот сравниваются как точные дроби до деления. Если ручная конфигурация даёт дробные герцы, опубликованные переменные `uint32_t` округляются **вниз** до целых Гц; автоматический расчёт точен до заданного герца.

---

## 5. Модели, питание и порядок переключения

**Пределы моделей** (`rcc_defs.hpp` покрывает все F4/F7 из STM32-base_files):

| МК | SYSCLK/HCLK макс. | APB1 макс. | APB2 макс. |
| --- | ---: | ---: | ---: |
| F401 | 84 МГц | 42 МГц | 84 МГц |
| F410/411/412/413/423 | 100 МГц | 50 МГц | 100 МГц |
| F405/407/415/417 | 168 МГц | 42 МГц | 84 МГц |
| F427/429/437/439/446/469/479 | 180 МГц | 45 МГц | 90 МГц |
| F722/723/730/732/733/745/746/750/756/765/767/769/777/779 | 216 МГц | 54 МГц | 108 МГц |
| G0 | 64 МГц | 64 МГц | — |

Неизвестная модель F4/F7 или другое семейство — явный `#error`.

**Допущения по питанию** (фиксированы, не измеряются):

- VDD = 3000 мВ, то есть диапазон таймингов 2,7…3,6 В (подходит и для 3,3 В).
- Масштаб напряжения ядра: F401 — Scale 2; остальные F4/F7 — Scale 1; G0 — Range 1.
- Over-drive включается при необходимости: F4 — при HCLK выше 168 МГц (где он есть), F7 — выше 180 МГц.
- Задержка Flash считается от HCLK: один такт ожидания на каждые 30 МГц для F4/F7 и 24 МГц для G0 Range 1.

**Пределы PLL:**

| МК | N | VCO мин. | Прочее |
| --- | --- | --- | --- |
| F401 | ≥ 192 | 192 МГц | |
| F469/479 | ≥ 50 | 192 МГц (по datasheet) | |
| Остальные F4/F7 | ≥ 50 | 100 МГц | |
| G0 | 8…86 | 96 МГц (по datasheet) | M = 1…8, R = 2…8, P = 2…32, Q = 2…8 при наличии |

Минимальная SYSCLK от PLL: 24 МГц для F4/F7 (F446 — 12,5 МГц), 12 МГц для G0.

**Что `Init<config>()` делает с аппаратурой:**

1. **Только одна попытка.** Повторный вызов отклоняется до записи регистров. Аппаратный отказ (генератор не запустился) тоже расходует попытку — нужен сброс.
2. Включается нужный источник. Для PLL: остановить оставшуюся неиспользуемую PLL, задать масштаб напряжения, записать коэффициенты, запустить PLL, дождаться готовности напряжения и при необходимости over-drive. На F4/F7 VOSRDY ожидается после запуска PLL.
3. Задержка Flash повышается одной записью ACR **до** повышения HCLK.
4. Делители шин выставляются, пока система ещё на HSI; SysTick перенастраивается для промежуточной HCLK. TIMPRE сбрасывается (где есть), чтобы частоты таймеров подчинялись обычному правилу ×1/×2.
5. SYSCLK переключается одной записью CFGR по маске; ожидается точное значение SWS.
6. SysTick сразу перенастраивается по готовому периоду, результат проверяется, публикуются частоты APB и таймеров. Задержка Flash снижается только после подтверждённого снижения частоты.

**Пределы ожидания:**

| Ожидание | Предел |
| --- | --- |
| PLL, HSI/LSI | 2 мс |
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
