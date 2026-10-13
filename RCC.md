# RCC / ClockSystem

*[English](#english) · [Русский](#русский)*

## English

ClockSystem performs one clock setup per hardware reset on STM32F4/F7/G0.
Call `System::Init()` first, before configuring peripherals. Configurations,
PLL selection, bus frequencies, Flash latency and SysTick periods are evaluated
at compile time. Runtime code applies register values and waits for hardware.
C++20 is required; the base already selects it.

### Automatic PLL selection

```cpp
constexpr auto clocks = ClockSystem::CalcPLL(
    64 * MHz, ClockSystem::PLL_ClockSource::HSI);

if (System::Init() != SysInitStatus::InitOK)
    System::DebugTrap("System init");
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
    System::DebugTrap("Clock init");
```

`CalcPLL(frequency, source, hse_clock = 0, pll_q = 2)` targets exact nominal
**SYSCLK**, sets AHB /1 and selects the smallest legal divider for each APB.
For HSE, provide the actual crystal frequency in Hz. Q is supplied by the user;
USB/SDIO/RNG clock requirements are separate from the requested SYSCLK.

The search traverses M in ascending order, maximizing the legal PLL input
frequency. With the same M it chooses the smallest legal P (F4/F7) or R (G0).
Exactness is checked with 64-bit integers:

```text
Fsource * N == Frequested * M * output_divider
```

This preserves the priority of the PLL comparison frequency for jitter: F4/F7
prefer input near 2 MHz, G0 near 16 MHz, subject to the device limits and exact
output. ST documents these recommendations in its [F4](https://raw.githubusercontent.com/STMicroelectronics/stm32f4xx-hal-driver/master/Inc/stm32f4xx_hal_rcc.h),
[F7](https://raw.githubusercontent.com/STMicroelectronics/stm32f7xx-hal-driver/master/Inc/stm32f7xx_hal_rcc.h)
and [G0](https://raw.githubusercontent.com/STMicroelectronics/stm32g0xx-hal-driver/master/Inc/stm32g0xx_hal_rcc.h) RCC documentation.

If no exact legal solution exists, the returned configuration has M=0.
`Init<clocks>()` then fails its static assertion. Choose another target or
provide a manual configuration; there is no approximate automatic fallback.

### Manual configuration

Example: F407, HSE 8 MHz, SYSCLK/HCLK 168 MHz, APB1 42 MHz, APB2 84 MHz:

```cpp
constexpr ClockSystem::Config clocks{
    .ClkSrc = ClockSystem::SystemClockSource::PLL,
    .HSE_Clk = 8 * MHz,
    .BusDiv = {ClockSystem::AHB_Divider::DIV1,
               ClockSystem::APB_Divider::DIV4,
               ClockSystem::APB_Divider::DIV2},
    .PLLCfgr = {ClockSystem::PLL_ClockSource::HSE, 4, 168, 2, 7, 2},
};
// System::Init() has already run.
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
    System::DebugTrap("Clock init");
```

HSI without PLL uses `constexpr ClockSystem::Config clocks{};` and the same
`Init<clocks>()` call. HSE uses `.ClkSrc = HSE` and `.HSE_Clk`. G0 also supports
LSI (nominal 32000 Hz) and LSE (32768 Hz). LSE starts with temporary backup-domain
write access; the previous DBP setting is restored, and RTC is not reset.

`ClkSrc` determines the active clock path. PLL fields are ignored for direct
sources, and HSE_Clk is ignored unless HSE actually supplies SYSCLK or PLL.
On F446, `PLL_R` selects the R system output; ordinary F4/F7 `PLL` selects P.
On G0 `PLL` selects R; P/Q divisors are written where present, but P/Q outputs
remain disabled. R is ignored on devices without an R field; G0 has no APB2.

### Static validation and supported devices

`Init<config>()` diagnoses unsupported sources, HSE limits, coefficient ranges,
PLL input/VCO/output limits, invalid enum values, bus limits and SysTick capacity.
Frequency limits are checked as rational values before division. For a manual
configuration with fractional-Hz frequencies, the published uint32_t frequency
variables are truncated to whole Hz; automatic selection is exact to the requested Hz.

`rcc_defs.hpp` covers every F4/F7 selector currently in STM32-base_files:

| Devices | SYSCLK/HCLK max | APB1 max | APB2 max |
|---|---:|---:|---:|
| F401 | 84 MHz | 42 MHz | 84 MHz |
| F410/411/412/413/423 | 100 MHz | 50 MHz | 100 MHz |
| F405/407/415/417 | 168 MHz | 42 MHz | 84 MHz |
| F427/429/437/439/446/469/479 | 180 MHz | 45 MHz | 90 MHz |
| F722/723/730/732/733/745/746/750/756/765/767/769/777/779 | 216 MHz | 54 MHz | 108 MHz |
| G0 | 64 MHz | 64 MHz | none |

Unknown F4/F7 selectors and other families produce a clear `#error`.
VDD is fixed at 3000 mV, valid for the same timing band at 3.3 V. F401 uses
Scale 2; other F4/F7 use Scale 1; G0 uses Range 1. F4 overdrive, where present,
is required above HCLK 168 MHz; F7 above 180 MHz. Flash latency is derived from
HCLK: 30 MHz per step on F4/F7 and 24 MHz on G0 Range 1. These are the
2.7..3.6 V timing conditions, not a measured supply voltage.

F401 requires N >=192 and VCO >=192 MHz ([RM0368](https://www.st.com/resource/en/reference_manual/rm0368-stm32f401xbc-and-stm32f401xde-advanced-armbased-32bit-mcusstmicroelectronics.pdf)).
Other F4/F7 use N >=50. F469/479 retain the datasheet's 192 MHz VCO minimum
([DS11189](https://www.st.com/resource/en/datasheet/stm32f469ii.pdf),
[DS11118](https://www.st.com/resource/en/datasheet/stm32f479ni.pdf)); the other
mapped F4/F7 use 100 MHz. G0 uses the datasheet's VCO minimum of 96 MHz,
R=2..8 and P=2..32; Q=2..8 where available ([G030 datasheet](https://www.st.com/resource/en/datasheet/stm32g030f6.pdf),
[RM0444](https://www.st.com/resource/en/reference_manual/dm00371828-stm32g0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)).
PLL SYSCLK minimum is 24 MHz on F4/F7 except F446 (12.5 MHz); G0 uses 12 MHz.

### Runtime sequence

1. Reject a second attempt before touching registers. A hardware failure also
   consumes the single attempt; recover with a hardware reset.
2. Enable only the oscillator required by ClkSrc and, for PLL, its input.
3. If using PLL, stop the old unused PLL. Program voltage scaling with PLL off,
   then configure PLL, enable it and wait for lock. On F4/F7 wait for VOSRDY
   after PLL starts, then enable overdrive when required. G0 waits for VOSF to clear.
4. Raise Flash latency with one masked ACR write before increasing HCLK.
5. Set bus dividers while SYSCLK is still HSI; use the precomputed intermediate
   HCLK/SysTick period. Reset TIMPRE where present to enforce timer x1/x2.
6. Write SW with one masked CFGR write; compare SWS against that exact source.
7. Publish the final HCLK and immediately configure SysTick using its precomputed
   period. Check InitTicks(), publish APB/timer clocks and HSE frequency.
8. Reduce latency only after the confirmed clock decrease.

PLL waits: 2 ms; HSI/LSI: 2 ms; HSE: 100 ms; LSE: 5000 ms;
power/Flash: 100 ms; SYSCLK switch: 5000 ms. Waits require a serviceable SysTick:
call from main with IRQs enabled. Peripheral/DMA activity and auxiliary PLLs
must be stopped before this initial setup. Dynamic reconfiguration is unsupported.
Clock changes restart SysTick and discard its partial period; the software tick
counter is retained. No oscillator fallback, retry state machine or IRQ locks are added.

### SysTick precision

`cycles_per_tick = floor(HCLK / TICK_BASE)`; LOAD is one less. A nonzero period
must fit the 24-bit timer. Actual tick rate is `HCLK / cycles_per_tick`.
At LSE 32768 Hz with AHB /1 the period is 32 cycles, giving 1024 ticks/s;
one GetTick increment is 0.9765625 ms. Delay_ms and tick timeouts share this
quantization error. LSI also has its oscillator tolerance. No fractional-period
scheduler is used. Exact automatic PLL selection refers to nominal SYSCLK,
not measured crystal accuracy or necessarily an exact 1 kHz SysTick.

### Migration

Replace runtime `Init(...)` / `InitCalcPLL(...)` with a constexpr Config or
CalcPLL result and `Init<config>()`. GetHSEClock and System clock variables remain.
`System::InitTicks(ticks)` accepts a precomputed cycle count; the no-argument
version remains available for existing callers. Compile-time preparation does
not require LTO. LTO and the syscalls/sysmem exclusions remain a separate build topic.

## Русский

ClockSystem выполняет одну настройку тактирования после аппаратного сброса для
STM32F4/F7/G0. Сначала вызывается System::Init(), затем RCC, затем настройка
периферии. Нужен C++20, который уже выбран в базе.

### API и проверка при компиляции

```cpp
constexpr auto clocks = ClockSystem::CalcPLL(
    64 * MHz, ClockSystem::PLL_ClockSource::HSI);

if (System::Init() != SysInitStatus::InitOK)
    System::DebugTrap("System init");
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
    System::DebugTrap("Clock init");
```

Ручной вариант — constexpr ClockSystem::Config с полями ClkSrc, HSE_Clk,
BusDiv и PLLCfgr, затем тот же Init<config>(). Пример для F407 приведён выше.
Для HSI без PLL достаточно `constexpr ClockSystem::Config clocks{};`.
Старые Init(...) и InitCalcPLL(...) заменены новым API.

Init<config>() через static_assert проверяет источники, коэффициенты PLL,
частоты входа/VCO/выхода, значения делителей, пределы SYSCLK/HCLK/APB и период
SysTick. При компиляции также вычисляются поля регистров, Flash latency,
конечные частоты шин/таймеров и периоды SysTick. Во время исполнения остаются
применение настроек, ожидания аппаратных флагов и отказ при повторном вызове.
Для этого не требуется LTO.

ClkSrc определяет выбранный путь. PLLCfgr игнорируется для прямых источников;
HSE_Clk — когда HSE не используется. F446 допускает SYSCLK от PLL_R.
G0 поддерживает HSI/HSE/PLL/LSI/LSE; PLL использует выход R. Выходы P/Q на G0
не включаются. Для отсутствующих R/APB2 лишних проверок нет.
LSE включается с временным разрешением записи в backup domain; исходный DBP
восстанавливается, RTC не сбрасывается.

### Точная частота и приоритет для jitter

CalcPLL(frequency, source, hse_clock=0, pll_q=2) рассчитывает точный номинальный
SYSCLK, оставляет AHB /1 и выбирает минимальные допустимые делители APB.
Сначала перебирается M от меньшего к большему — приоритет максимальной
допустимой частоты сравнения PLL. Для одинакового M выбирается минимальный
P/R. Ориентиры ST: 2 МГц на входе PLL F4/F7, 16 МГц для G0; ссылки выше.

Равенство `Fsource * N == Frequested * M * P/R` проверяется в 64-битных целых.
Округления до ближайшей частоты нет. Если решения нет, CalcPLL возвращает
конфигурацию с M=0, и Init<config>() выдаёт ошибку сборки. Пользователь выбирает
другую частоту либо ручную конфигурацию; ручной вариант тоже проверяется.

Частотные ограничения проверяются до деления, без потери дробной части.
Для ручных дробных частот переменные uint32_t публикуют целые Гц с округлением
вниз; автоматический расчёт соответствует заданным Гц точно. Делитель Q задаёт
пользователь: требования USB/SDIO/RNG не выводятся из одной целевой SYSCLK.

### Модели, питание и порядок переключения

Поддерживаются все F4/F7 из текущих карт STM32-base_files и семейство G0.
Группы лимитов приведены в таблице выше; неизвестная модель — явный #error.
Константы находятся в rcc_defs.hpp. VDD фиксирован: 3000 мВ, тот же диапазон
Flash timing используется при 3,3 В. F401 работает в Scale 2, другие F4/F7 —
Scale 1, G0 — Range 1. Для F4 с overdrive он включается при HCLK выше 168 МГц,
для F7 — выше 180 МГц. Latency вычисляется от HCLK по шагам 30 МГц для F4/F7
и 24 МГц для G0 Range 1.

Отдельно учтены F401: N>=192, VCO>=192 МГц; F469/479: VCO>=192 МГц по
Datasheet; остальные F4/F7: N>=50, VCO>=100 МГц. G0: VCO>=96 МГц по Datasheet,
M=1..8, N=8..86, R=2..8, P=2..32, Q=2..8 при наличии. Минимальная частота
PLL SYSCLK: 24 МГц для F4/F7, кроме F446 (12,5 МГц); для G0 — 12 МГц.

Последовательность:

1. Отказать при повторном вызове до записи регистров. После аппаратной ошибки
   повторная попытка также запрещена, требуется аппаратный сброс.
2. Включить нужный источник. Для PLL остановить оставшуюся неиспользуемую PLL,
   задать VOS, записать коэффициенты, запустить PLL, дождаться готовности VOS
   и при необходимости overdrive. На F4/F7 VOSRDY ожидается после запуска PLL.
3. Повысить latency одной записью ACR перед любым повышением HCLK.
4. Выставить делители на текущем HSI, настроить SysTick для промежуточного HCLK.
   Сбросить TIMPRE при наличии, чтобы частоты таймеров соответствовали правилу x1/x2.
5. Записать SW одной операцией CFGR через маску, дождаться точного значения SWS.
6. Сразу пересчитать SysTick по готовому периоду, проверить результат, опубликовать
   частоты APB и таймеров. Снизить latency после подтверждённого снижения частоты.

Пределы ожидания: PLL/HSI/LSI — 2 мс; HSE — 100 мс; LSE — 5000 мс;
VOS/overdrive/Flash — 100 мс; переключение SYSCLK — 5000 мс.
SysTick должен обслуживаться: вызов из main с разрешёнными IRQ. Периферия/DMA
и вспомогательные PLL должны быть остановлены до этой начальной настройки.
Динамическая переконфигурация не поддерживается. При смене HCLK SysTick
перезапускается с потерей части текущего периода, счётчик GetTick сохраняется.

### Погрешность SysTick

Период — `floor(HCLK / TICK_BASE)` тактов, LOAD на единицу меньше. Если HCLK
не кратна TICK_BASE, частота тика отличается от 1 кГц. При LSE 32768 Гц и AHB /1:
период 32 такта, 1024 тика/с, один прирост GetTick равен 0,9765625 мс.
Delay_ms и таймауты имеют ту же погрешность; для LSI добавляется точность самого
RC-генератора. Период 0 или больше возможностей 24-битного таймера отклоняется
при компиляции. Чередование периодов и дополнительный таймер не вводятся.

Для заранее рассчитанного периода добавлена System::InitTicks(ticks).
Старая перегрузка без аргумента сохранена. Идея LTO и исключения syscalls.c/
sysmem.c из LTO остаётся отдельным вопросом настройки сборки.

## Additional primary references / Дополнительные первичные источники

- [RM0090 — F405/407/415/417/427/429/437/439](https://www.st.com/resource/en/reference_manual/dm00031020-stm32f405-407-415-417-437-455-469-application-note-stmicroelectronics.pdf)
- [RM0383 — F411](https://www.st.com/resource/en/reference_manual/rm0383-stm32f411xce-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [RM0401 — F410](https://www.st.com/resource/en/reference_manual/rm0401-stm32f410-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [RM0402 — F412](https://www.st.com/resource/en/reference_manual/rm0402-stm32f412-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [RM0430 — F413/423](https://www.st.com/resource/en/reference_manual/rm0430-stm32f413423-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [RM0431 — F72/73 Flash/VOS timing](https://www.st.com/resource/en/reference_manual/rm0431-stm32f72xxx-and-stm32f73xxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [F746 datasheet — PLL operating limits](https://www.st.com/resource/en/datasheet/stm32f746zg.pdf)
- [F767 datasheet — PLL operating limits](https://www.st.com/resource/en/datasheet/stm32f767zi.pdf)
