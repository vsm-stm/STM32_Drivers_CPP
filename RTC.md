# RTC / RTC_cl

*[English](#english) · [Русский](#русский)*

---

## English

## Contents

1. [Why this is needed](#1-why-this-is-needed)
2. [Static class — why there is no instantiation](#2-static-class--why-there-is-no-instantiation)
3. [SetUp() — clock source and "not losing time"](#3-setup--clock-source-and-not-losing-time)
4. [Write Protection](#4-write-protection)
5. [BCD time/date format](#5-bcd-timedate-format)
6. [WakeUp Timer](#6-wakeup-timer)
7. [Alarm A / Alarm B](#7-alarm-a--alarm-b)
8. [IRQ_en() and a single handler for all sources](#8-irq_en-and-a-single-handler-for-all-sources)
9. [Calibration (CALR)](#9-calibration-calr)
10. [Usage examples](#10-usage-examples)
11. [Edge cases](#11-edge-cases)

---

## 1. Why this is needed

`RTC_cl` is the STM32G0 real-time clock driver (the only family this project supports for RTC —
it isn't abstracted across F4/F7 the way `UART`/`TIM`/`SPI` are). It provides time/date with
BCD storage in hardware, three independent interrupt sources (WakeUp Timer, Alarm A, Alarm B),
and survives an MCU reset and Stop mode, because it lives in the backup domain (`RCC->BDCR`),
clocked separately from the main system (LSE/LSI, not HSE/HSI/PLL).

Like [`IRQ_Registry`](IRQ_Registry.md), [`USART`](UART.md#5-irq_en--регистрация-по-требованию) and
[`TIM`](TIM.md#4-irq_en-и-общийраздельный-вектор), it uses the "auto-register with IRQ_Registry on
the first source enabled, auto-unregister on the last one disabled" pattern (`IRQ_en()`, see §8) —
the user never needs to touch `IRQ_Registry`/`NVIC` directly.

---

## 2. Static class — why there is no instantiation

```cpp
class RTC_cl { /* all methods static */ };
```

Unlike `USART`/`SPI`/`TIM` (one object per physical peripheral instance), on STM32G0 there is
**one** RTC for the whole chip — there's no point creating an object that could be instantiated
multiple times. All methods are called as `RTC_cl::SetUp(...)`, `RTC_cl::EnableWakeUpTimer(...)`.
The internal state (callbacks, the handler) consists of static class fields, not instance fields
(see §8).

---

## 3. SetUp() — clock source and "not losing time"

```cpp
static SysInitStatus SetUp(CLK_Source clk_src = CLK_Source::LSI,
                            uint32_t prediv_a = 127, uint32_t prediv_s = 255);
```

The calendar clock frequency:

```text
f_ck_spre = f_rtcclk / ((prediv_a + 1) * (prediv_s + 1))
```

By default (`prediv_a=127`, `prediv_s=255`) and LSE = 32768 Hz: `32768 / (128 * 256) = 1 Hz` —
exactly one calendar tick per second (the standard configuration recommended in the Reference
Manual — the two-stage divider balances the asynchronous stage's power consumption against the
synchronous stage's accuracy).

**Don't reinitialize if the RTC is already ticking from the desired source**:

```cpp
const bool already_running =
    (RCC->BDCR & RCC_BDCR_RTCEN) &&
    ((RCC->BDCR & RCC_BDCR_RTCSEL_Msk) == static_cast<uint32_t>(clk_src));
if (!already_running) { /* ... set PRER, enter initialization mode ... */ }
```

A key subtlety, explicitly noted by a comment in the code: this check **must not** use the `RSF`
flag (Registers Synchronized Flag, `ICSR`) — it is cleared on **any** system reset (a debugger
attaching, `SYSRESETREQ`, ...), even if the RTC itself never stopped and is still counting time.
Checking via `RTC_BDCR_RTCEN`/`RTCSEL` is immune to this, because both bits live in the backup
domain, which a system reset doesn't touch — so `SetUp()` can safely be called again after a
software reset without disturbing a clock that's already running.

---

## 4. Write Protection

```cpp
inline static void WriteProtection(FunctionalState enable) {
    if (enable) { RTC->WPR = 0xFF; }              // any wrong key — locks
    else        { RTC->WPR = 0xCA; RTC->WPR = 0x53; } // the correct key pair — unlocks
}
```

Most RTC registers are protected against accidental writes. **Every** public method of the class
that touches protected registers (`SetTime`, `SetDate`, `EnableWakeUpTimer`, `EnableAlarm_A/B`,
`IRQ_en`, `SetCalibration`, `EnableCOE`) removes the protection itself at the start and restores
it at the end — calling code never needs to think about `WriteProtection()` directly.

---

## 5. BCD time/date format

The RTC stores time/date in BCD (Binary-Coded Decimal — each decimal digit in its own 4-bit
group) rather than in binary — this way they can be shown on a seven-segment display without any
recalculation. `SetTime()`/`SetDate()` accept plain decimal `uint8_t` values and convert them
internally:

```cpp
inline static uint8_t to_bcd(uint8_t v) {
    if (v > 99) return 0xFF;
    uint8_t t = ((uint16_t)v * 205u) >> 11;   // fast division by 10 without hardware DIV
    return (t << 4) | (v - t * 10u);
}
```

`(v * 205) >> 11` is an integer-division-by-10 approximation trick via multiply and shift
(`205/2048 ≈ 1/10`, exact across the whole `0..99` range) — faster than hardware division on
Cortex-M0+ (no single-cycle `DIV`). `SetTime_hex()`/`SetDate_hex()` are conversion-free versions,
for when the calling code already works directly with "raw" BCD values.

`GetTime()`/`GetDate()` return **not** decoded decimal numbers, but structures with individual
BCD digits (`RtcTime{su, st, mnu, mnt, hu, ht, ...}`, `RtcDate{du, dt, mu, mt, yu, yt, wdu}`) — the
`.sec()`/`.min()`/`.hour()`/`.day()`/`.month()`/`.year()` methods assemble the decimal value
(`st*10 + su`) only on demand, rather than always, saving the recomputation in cases where the raw
digits are what's needed (for example, for direct output to a display).

Writing the time/date requires initialization mode (similar to writing `PRER` in `SetUp()`):
`ICSR_INIT = 1`, wait for `INITF`, write the register, clear `INIT`.

---

## 6. WakeUp Timer

```cpp
static void EnableWakeUpTimer(uint32_t counter = 0, uint8_t clock_div = 0b100,
                               void (*cb)(void) = nullptr);
```

The WUT clock source (`WUCKSEL`, 3 bits) has 6 options, from `RTCCLK/2` to `ck_spre` (1 Hz with
the default pre-dividers) `+65536` added to the counter. The mode recommended in the doc comment
is `0b100` (`ck_spre`, i.e. usually 1 Hz): period = `(counter + 1)` seconds — `counter=0` → 1 s,
`counter=59` → 60 s.

**`WUTR`/`WUCKSEL` are write-protected while `WUTE = 1`** (RM0454 §27.5.2) — before changing
them you need to clear `WUTE` and wait for `WUTWF` (~2 RTCCLK cycles, about 61 µs at 32768 Hz):

```cpp
RTC->CR &= ~RTC_CR_WUTE;
while (!(RTC->ICSR & RTC_ICSR_WUTWF)) {}
RTC->WUTR = counter;
RTC->CR   = (RTC->CR & ~RTC_CR_WUCKSEL_Msk) | (clock_div << RTC_CR_WUCKSEL_Pos) | RTC_CR_WUTE;
```

The `cb` callback is stored in the static field `_wut_cb` **before** waiting for `WUTWF` — so
even if the user calls `EnableWakeUpTimer()` twice in a row with different callbacks, the last one
passed always stays the active one. `IRQ_en(IRQ_s::WUT, ENABLE)` at the end registers with
`IRQ_Registry` (see §8), which happens automatically — there's no need to call it separately.

---

## 7. Alarm A / Alarm B

```cpp
struct AlarmField { uint8_t val = 0; Alarm_Masks mask = Alarm_Masks::Ignore; };
struct AlarmConfig { AlarmField day, hour, min, sec; uint32_t sub_sec_msk = 0; uint32_t sub_sec = 0; };

static void EnableAlarm_A(FunctionalState en, const AlarmConfig& cfg = kNoAlarm, void (*cb)(void) = nullptr);
static void EnableAlarm_B(FunctionalState en, const AlarmConfig& cfg = kNoAlarm, void (*cb)(void) = nullptr);
```

Each field (`day`/`hour`/`min`/`sec`) is compared only if its `mask == Alarm_Masks::Care` —
fields left at `Ignore` (the default value) don't take part in the comparison, meaning "trigger on
any day, but exactly at 14:25:00" is achieved simply by not specifying `day`:

```cpp
RTC_cl::AlarmConfig cfg {
    .hour = {14, RTC_cl::Alarm_Masks::Care},
    .min  = {25, RTC_cl::Alarm_Masks::Care},
    .sec  = { 0, RTC_cl::Alarm_Masks::Care},
};   // day is left Ignore — triggers every day at 14:25:00
```

`build_alrmr()` (a free function in `rtc.cpp`, not a class method) assembles `ALRMAR`/`ALRMBR`
from an `AlarmConfig`, including the `MSKx` bits and the BCD conversion of each field via
`to_bcd()`. `ALRMASSR`/`ALRMBSSR` (sub-second comparison) are programmed separately, if
`sub_sec_msk != 0`.

Before changing `ALRMAR`/`ALRMBR`, same as WUT: clear `ALRAE`/`ALRBE`, wait for
`ALRAWF`/`ALRBWF`, then write. Disabling (`en == DISABLE`) just clears `ALRAE`/`ALRBE` and doesn't
touch the match register — on the next `en == ENABLE` without passing a new `cfg` (`= kNoAlarm` by
default), the "empty" configuration (all fields `Ignore`) will take effect, not the last one used
— you should always pass `cfg` explicitly when re-enabling.

---

## 8. IRQ_en() and a single handler for all sources

All three sources (WUT, Alarm A, Alarm B — TS/Timestamp is declared in `IRQ_s`, but has no public
`Enable*` method in this file) share **one** NVIC vector (`RTC_TAMP_IRQn`) and **one** static
handler:

```cpp
struct RtcHandler : IIRQHandler {
    void HandleIRQ() override final {
        if (RTC->SR & RTC_SR_WUTF)  { RTC->SCR = RTC_SCR_CWUTF;  if (_wut_cb)     _wut_cb();     }
        if (RTC->SR & RTC_SR_ALRAF) { RTC->SCR = RTC_SCR_CALRAF; if (_alarm_a_cb) _alarm_a_cb(); }
        if (RTC->SR & RTC_SR_ALRBF) { RTC->SCR = RTC_SCR_CALRBF; if (_alarm_b_cb) _alarm_b_cb(); }
    }
};
static RtcHandler _rtc_handler;   // the single instance for the whole class — a static field
```

This is the same "unconditionally check every possible flag" principle used in
[`TIM::HandleIRQ()`](TIM.md#5-timhandleirq--база-для-всех-подклассов) — each check is cheap, and
RTC interrupts are inherently rare (seconds/minutes), so the cost of this unconditional checking
is negligible.

`IRQ_en()` is the same auto-register/auto-unregister idiom as `USART`/`TIM` use, but it registers
not `this` (there's no object, the class is static) but the address of the single static
`_rtc_handler`:

```cpp
static void IRQ_en(IRQ_s irq, FunctionalState en) {
    WriteProtection(DISABLE);
    if (en) RTC->CR |= (uint32_t)irq; else RTC->CR &= ~(uint32_t)irq;
    WriteProtection(ENABLE);

    if (en && !NVIC_GetEnableIRQ(RTC_TAMP_IRQn)) {
        IRQ_Registry::Register(RTC_TAMP_IRQn, &_rtc_handler);
        EXTI->IMR1 |= EXTI_IMR1_IM19;    // RTC interrupts are routed through the EXTI19 line
        NVIC_EnableIRQ(RTC_TAMP_IRQn);
    } else if (!en && !(RTC->CR & all_irq_bits)) {
        IRQ_Registry::Unregister(RTC_TAMP_IRQn);
        NVIC_DisableIRQ(RTC_TAMP_IRQn);
    }
}
```

An extra step compared to `USART`/`TIM`: on STM32G0, RTC interrupts are physically routed through
the EXTI19 line, so the first enable also unmasks `EXTI->IMR1` — without this, the NVIC would
never see the interrupt, even with `RTC_CR_WUTIE`/`ALRAIE`/`ALRBIE` set.

---

## 9. Calibration (CALR)

```cpp
static void SetCalibration(uint16_t calm = 0, bool calp = false);
```

Operates within a 32-second window (`2^20 = 1 048 576` RTCCLK pulses):

- `calp = false` → suppress `calm` pulses per window → the clock slows down by `calm × 0.9537`
  ppm.
- `calp = true` → add 512, suppress `calm` → net effect `(512 - calm) × 0.9537` ppm (speeds up if
  `calm < 512`).

Resolution ≈ 0.954 ppm/step, range ±488 ppm. Example from the doc comment: a measured period of
999.9945 ms (the clock runs 5.5 ppm fast) → `calm = round(5.5 / 0.9537) = 6` →
`RTC_cl::SetCalibration(6)` (without `calp`) — a compensation of −5.72 ppm, the closest available
value to the required −5.5 ppm.

`SetCalibration()` waits for `RECALPF` (recalibration pending, ≤ 2 RTCCLK cycles) before writing
— the previous calibration must be absorbed by the hardware before a new one is applied.

---

## 10. Usage examples

### Basic setup + time/date

```cpp
RTC_cl::SetUp(RTC_cl::CLK_Source::LSE);
RTC_cl::SetTime({14, 25, 0});
RTC_cl::SetDate({22, 5, 26}, RTC_cl::WeekDay::Friday);   // May 22, 2026, Friday

auto t = RTC_cl::GetTime();
uint8_t hh = t.hour(), mm = t.min(), ss = t.sec();
```

### WakeUp Timer — periodic wake-up every 2 seconds

```cpp
RTC_cl::EnableWakeUpTimer(/*counter=*/1, /*clock_div=*/0b100, []{
    // called every (counter+1) = 2 seconds from RTC_TAMP_IRQn
});
```

### Alarm A — a daily alarm at a fixed time

```cpp
RTC_cl::AlarmConfig cfg {
    .hour = {7,  RTC_cl::Alarm_Masks::Care},
    .min  = {30, RTC_cl::Alarm_Masks::Care},
    .sec  = {0,  RTC_cl::Alarm_Masks::Care},
};
RTC_cl::EnableAlarm_A(ENABLE, cfg, []{ /* good morning */ });
// later:
RTC_cl::EnableAlarm_A(DISABLE);   // clears ALRAE without touching ALRMAR
```

### Calibration after measuring the real period

```cpp
RTC_cl::SetCalibration(/*calm=*/6, /*calp=*/false);  // compensating for a ~5.5 ppm drift
```

---

## 11. Edge cases

- **`RSF` is not a valid indicator of "the RTC is already configured"** — use the `RTCEN` +
  `RTCSEL` combination instead (see §3); this is already done inside `SetUp()`, but it matters
  when writing similar code for another backup-domain peripheral.
- **`WUTR`/`ALRMAR`/`ALRMBR` cannot be written while the corresponding Enable bit is set** —
  `EnableWakeUpTimer()`/`EnableAlarm_A/B()` clear and wait for the needed flag (`WUTWF`/`ALRAWF`/
  `ALRBWF`) themselves before writing; if working with the registers manually, you need to repeat
  this yourself.
- **`EnableAlarm_A(ENABLE)` without an explicit `cfg`** uses `kNoAlarm` (all fields `Ignore`) —
  not the "last used" configuration; to re-enable with the same configuration you need to pass it
  again.
- **RTC interrupts depend on `EXTI_IMR1_IM19`** — if something outside this code (not through
  `IRQ_en()`) accidentally clears this EXTI bit, `RTC_TAMP_IRQn` will stop firing even with
  `WUTIE`/`ALRAIE`/`ALRBIE` set.
- **`to_bcd()` returns `0xFF` when `v > 99`** — not a `DebugTrap`, just an explicitly invalid BCD
  value; `SetTime`/`SetDate`/`build_alrmr` don't check for this separately, an invalid argument
  will silently end up in the register as `0xFF`.
- **A single static `_rtc_handler` for the whole class** — unlike `USART`/`TIM`/`SPI`, where
  `HandleIRQ()` is a method of the peripheral object itself, here it's a dedicated singleton
  object (see §8), because `RTC_cl` itself has no instances that could serve that role.

---

## See also

- [IRQ_Registry.md](IRQ_Registry.md) — the general mechanism `IRQ_en()` (§8) is built on; here
  what gets registered is not `this` (the class is static) but the singleton handler's address.
- [System.md](System.md) — `SysInitStatus`, the common `DebugTrap`/`SysStatus` infrastructure.

---

## Русский

## Содержание

1. [Зачем это нужно](#1-зачем-это-нужно)
2. [Статический класс — почему нет инстанцирования](#2-статический-класс--почему-нет-инстанцирования)
3. [SetUp() — источник тактирования и "не потерять время"](#3-setup--источник-тактирования-и-не-потерять-время)
4. [Write Protection](#4-write-protection)
5. [BCD-формат времени/даты](#5-bcd-формат-временидаты)
6. [WakeUp Timer](#6-wakeup-timer)
7. [Alarm A / Alarm B](#7-alarm-a--alarm-b)
8. [IRQ_en() и единый обработчик на все источники](#8-irq_en-и-единый-обработчик-на-все-источники)
9. [Калибровка (CALR)](#9-калибровка-calr)
10. [Примеры использования](#10-примеры-использования)
11. [Угловые случаи](#11-угловые-случаи)

---

## 1. Зачем это нужно

`RTC_cl` — драйвер часов реального времени STM32G0 (единственное поддерживаемое в этом проекте
семейство для RTC — не абстрагирован под F4/F7, в отличие от `UART`/`TIM`/`SPI`). Даёт время/дату
с BCD-хранением в железе, три независимых источника прерываний (WakeUp Timer, Alarm A, Alarm B),
и переживает сброс МК и Stop-режим, поскольку живёт в backup-домене (`RCC->BDCR`), тактируемом
отдельно от основной системы (LSE/LSI, а не HSE/HSI/PLL).

Как и [`IRQ_Registry`](IRQ_Registry.md), [`USART`](UART.md#5-irq_en--регистрация-по-требованию) и
[`TIM`](TIM.md#4-irq_en-и-общийраздельный-вектор), использует паттерн "auto-register в
IRQ_Registry на первом включении источника, auto-unregister на последнем выключении" (`IRQ_en()`,
см. §8) — пользователю не нужно вручную трогать `IRQ_Registry`/`NVIC` вообще.

---

## 2. Статический класс — почему нет инстанцирования

```cpp
class RTC_cl { /* все методы static */ };
```

В отличие от `USART`/`SPI`/`TIM` (по объекту на каждый физический экземпляр периферии), RTC на
STM32G0 **один** на весь чип — нет смысла заводить объект, который можно было бы создать
несколько раз. Все методы вызываются как `RTC_cl::SetUp(...)`, `RTC_cl::EnableWakeUpTimer(...)`.
Внутреннее состояние (колбэки, обработчик) — статические поля класса, не поля экземпляра (см.
§8).

---

## 3. SetUp() — источник тактирования и "не потерять время"

```cpp
static SysInitStatus SetUp(CLK_Source clk_src = CLK_Source::LSI,
                            uint32_t prediv_a = 127, uint32_t prediv_s = 255);
```

Тактовая частота календаря:

```text
f_ck_spre = f_rtcclk / ((prediv_a + 1) * (prediv_s + 1))
```

По умолчанию (`prediv_a=127`, `prediv_s=255`) и LSE = 32768 Гц: `32768 / (128 * 256) = 1 Гц` —
ровно один тик календаря в секунду (стандартная конфигурация, рекомендованная в Reference
Manual — двухступенчатый делитель балансирует энергопотребление асинхронного каскада против
точности синхронного).

**Не инициализировать заново, если RTC уже тикает от нужного источника**:

```cpp
const bool already_running =
    (RCC->BDCR & RCC_BDCR_RTCEN) &&
    ((RCC->BDCR & RCC_BDCR_RTCSEL_Msk) == static_cast<uint32_t>(clk_src));
if (!already_running) { /* ... установить PRER, войти в режим инициализации ... */ }
```

Ключевой нюанс, прямо описанный комментарием в коде: для этой проверки **нельзя** использовать
флаг `RSF` (Registers Synchronized Flag, `ICSR`) — он сбрасывается при **любом** системном
сбросе (отладчик подключился, `SYSRESETREQ`, ...), даже если сам RTC не останавливался и
продолжает считать время. Проверка через `RTC_BDCR_RTCEN`/`RTCSEL` устойчива к этому, потому что
оба бита живут в backup-домене, который системный сброс не трогает — так `SetUp()` можно
безопасно вызывать повторно после программного сброса, не сбивая уже идущие часы.

---

## 4. Write Protection

```cpp
inline static void WriteProtection(FunctionalState enable) {
    if (enable) { RTC->WPR = 0xFF; }              // любой неверный ключ — заблокировать
    else        { RTC->WPR = 0xCA; RTC->WPR = 0x53; } // правильная пара ключей — разблокировать
}
```

Большинство регистров RTC защищены от случайной записи. **Каждый** публичный метод класса,
который трогает защищённые регистры (`SetTime`, `SetDate`, `EnableWakeUpTimer`, `EnableAlarm_A/B`,
`IRQ_en`, `SetCalibration`, `EnableCOE`), сам снимает защиту в начале и восстанавливает её в
конце — вызывающему коду никогда не нужно думать о `WriteProtection()` напрямую.

---

## 5. BCD-формат времени/даты

RTC хранит время/дату в BCD (Binary-Coded Decimal — каждая десятичная цифра в отдельной
4-битной группе), а не в двоичном виде — так их можно показать на семисегментном индикаторе без
пересчёта. `SetTime()`/`SetDate()` принимают обычные десятичные `uint8_t` и сами конвертируют:

```cpp
inline static uint8_t to_bcd(uint8_t v) {
    if (v > 99) return 0xFF;
    uint8_t t = ((uint16_t)v * 205u) >> 11;   // быстрое деление на 10 без аппаратного DIV
    return (t << 4) | (v - t * 10u);
}
```

`(v * 205) >> 11` — трюк аппроксимации целочисленного деления на 10 через умножение и сдвиг
(`205/2048 ≈ 1/10`, точен для всего диапазона `0..99`) — быстрее, чем аппаратное деление на
Cortex-M0+ (нет single-cycle `DIV`). `SetTime_hex()`/`SetDate_hex()` — версии без конвертации,
для случая, когда вызывающий код уже работает с "сырыми" BCD-значениями напрямую.

`GetTime()`/`GetDate()` возвращают **не** декодированные декатные числа, а структуры с отдельными
BCD-разрядами (`RtcTime{su, st, mnu, mnt, hu, ht, ...}`, `RtcDate{du, dt, mu, mt, yu, yt, wdu}`) —
методы `.sec()`/`.min()`/`.hour()`/`.day()`/`.month()`/`.year()` собирают десятичное значение
(`st*10 + su`) только по требованию, а не всегда, экономя пересчёт там, где нужны сырые разряды
(например, для прямого вывода на индикатор).

Запись времени/даты требует режима инициализации (аналогично записи `PRER` в `SetUp()`):
`ICSR_INIT = 1`, дождаться `INITF`, записать регистр, снять `INIT`.

---

## 6. WakeUp Timer

```cpp
static void EnableWakeUpTimer(uint32_t counter = 0, uint8_t clock_div = 0b100,
                               void (*cb)(void) = nullptr);
```

Источник тактирования WUT (`WUCKSEL`, 3 бита) — 6 вариантов, от `RTCCLK/2` до `ck_spre` (1 Гц с
дефолтными предделителями) `+65536` к счётчику. Рекомендуемый в doc-комментарии режим —
`0b100` (`ck_spre`, т.е. обычно 1 Гц): период = `(counter + 1)` секунд — `counter=0` → 1 с,
`counter=59` → 60 с.

**`WUTR`/`WUCKSEL` защищены от записи, пока `WUTE = 1`** (RM0454 §27.5.2) — перед изменением
нужно снять `WUTE` и дождаться `WUTWF` (~2 такта RTCCLK, около 61 мкс на 32768 Гц):

```cpp
RTC->CR &= ~RTC_CR_WUTE;
while (!(RTC->ICSR & RTC_ICSR_WUTWF)) {}
RTC->WUTR = counter;
RTC->CR   = (RTC->CR & ~RTC_CR_WUCKSEL_Msk) | (clock_div << RTC_CR_WUCKSEL_Pos) | RTC_CR_WUTE;
```

Колбэк `cb` сохраняется в статическое поле `_wut_cb` **до** ожидания `WUTWF` — так что даже если
пользователь дважды вызовет `EnableWakeUpTimer()` подряд с разными колбэками, актуальным всегда
останется последний переданный. `IRQ_en(IRQ_s::WUT, ENABLE)` в конце — регистрация в
`IRQ_Registry` (см. §8), выполняется автоматически, отдельно вызывать не нужно.

---

## 7. Alarm A / Alarm B

```cpp
struct AlarmField { uint8_t val = 0; Alarm_Masks mask = Alarm_Masks::Ignore; };
struct AlarmConfig { AlarmField day, hour, min, sec; uint32_t sub_sec_msk = 0; uint32_t sub_sec = 0; };

static void EnableAlarm_A(FunctionalState en, const AlarmConfig& cfg = kNoAlarm, void (*cb)(void) = nullptr);
static void EnableAlarm_B(FunctionalState en, const AlarmConfig& cfg = kNoAlarm, void (*cb)(void) = nullptr);
```

Каждое поле (`day`/`hour`/`min`/`sec`) сравнивается только если его `mask == Alarm_Masks::Care` —
поля, оставленные `Ignore` (значение по умолчанию), в сравнении не участвуют, то есть
"срабатывать в любой день, но точно в 14:25:00" получается, просто не указывая `day`:

```cpp
RTC_cl::AlarmConfig cfg {
    .hour = {14, RTC_cl::Alarm_Masks::Care},
    .min  = {25, RTC_cl::Alarm_Masks::Care},
    .sec  = { 0, RTC_cl::Alarm_Masks::Care},
};   // day оставлен Ignore — сработает каждый день в 14:25:00
```

`build_alrmr()` (свободная функция в `rtc.cpp`, не метод класса) собирает `ALRMAR`/`ALRMBR` из
`AlarmConfig`, включая биты `MSKx` и BCD-конвертацию каждого поля через `to_bcd()`.
`ALRMASSR`/`ALRMBSSR` (под-секундное сравнение) программируются отдельно, если `sub_sec_msk != 0`.

Перед изменением `ALRMAR`/`ALRMBR`, аналогично WUT: снять `ALRAE`/`ALRBE`, дождаться
`ALRAWF`/`ALRBWF`, затем писать. Отключение (`en == DISABLE`) просто снимает `ALRAE`/`ALRBE` и
не трогает регистр совпадения — при следующем `en == ENABLE` без передачи нового `cfg`
(`= kNoAlarm` по умолчанию) сработает "пустая" конфигурация (все поля `Ignore`), а не последняя
использованная — стоит всегда передавать `cfg` явно при повторном включении.

---

## 8. IRQ_en() и единый обработчик на все источники

Все три источника (WUT, Alarm A, Alarm B — TS/Timestamp объявлен в `IRQ_s`, но не имеет
публичного метода `Enable*` в этом файле) делят **один** вектор NVIC (`RTC_TAMP_IRQn`) и **один**
статический обработчик:

```cpp
struct RtcHandler : IIRQHandler {
    void HandleIRQ() override final {
        if (RTC->SR & RTC_SR_WUTF)  { RTC->SCR = RTC_SCR_CWUTF;  if (_wut_cb)     _wut_cb();     }
        if (RTC->SR & RTC_SR_ALRAF) { RTC->SCR = RTC_SCR_CALRAF; if (_alarm_a_cb) _alarm_a_cb(); }
        if (RTC->SR & RTC_SR_ALRBF) { RTC->SCR = RTC_SCR_CALRBF; if (_alarm_b_cb) _alarm_b_cb(); }
    }
};
static RtcHandler _rtc_handler;   // единственный экземпляр на весь класс — статическое поле
```

Это тот же принцип "проверить все возможные флаги безусловно", что и в
[`TIM::HandleIRQ()`](TIM.md#5-timhandleirq--база-для-всех-подклассов) — каждая проверка дешева,
а RTC-прерывания в принципе редки (секунды/минуты), так что цена такой безусловной проверки
несущественна.

`IRQ_en()` — тот же auto-register/auto-unregister идиом, что у `USART`/`TIM`, но регистрирует не
`this` (объекта-то нет, класс статический), а адрес единственного статического `_rtc_handler`:

```cpp
static void IRQ_en(IRQ_s irq, FunctionalState en) {
    WriteProtection(DISABLE);
    if (en) RTC->CR |= (uint32_t)irq; else RTC->CR &= ~(uint32_t)irq;
    WriteProtection(ENABLE);

    if (en && !NVIC_GetEnableIRQ(RTC_TAMP_IRQn)) {
        IRQ_Registry::Register(RTC_TAMP_IRQn, &_rtc_handler);
        EXTI->IMR1 |= EXTI_IMR1_IM19;    // RTC-прерывания заведены через линию EXTI19
        NVIC_EnableIRQ(RTC_TAMP_IRQn);
    } else if (!en && !(RTC->CR & all_irq_bits)) {
        IRQ_Registry::Unregister(RTC_TAMP_IRQn);
        NVIC_DisableIRQ(RTC_TAMP_IRQn);
    }
}
```

Дополнительный шаг по сравнению с `USART`/`TIM`: RTC-прерывания на STM32G0 физически заведены
через линию EXTI19, поэтому первое включение размаскирует ещё и `EXTI->IMR1` — без этого NVIC
никогда не увидит прерывание, даже если `RTC_CR_WUTIE`/`ALRAIE`/`ALRBIE` установлены.

---

## 9. Калибровка (CALR)

```cpp
static void SetCalibration(uint16_t calm = 0, bool calp = false);
```

Работает в окне 32 секунды (`2^20 = 1 048 576` импульсов RTCCLK):

- `calp = false` → подавить `calm` импульсов за окно → часы замедляются на `calm × 0.9537` ppm.
- `calp = true` → добавить 512, подавить `calm` → чистый эффект `(512 - calm) × 0.9537` ppm
  (ускорение, если `calm < 512`).

Разрешение ≈ 0.954 ppm/шаг, диапазон ±488 ppm. Пример из doc-комментария: измеренный период
999.9945 мс (часы спешат на 5.5 ppm) → `calm = round(5.5 / 0.9537) = 6` →
`RTC_cl::SetCalibration(6)` (без `calp`) — компенсация −5.72 ppm, ближайшее доступное значение к
требуемым −5.5 ppm.

`SetCalibration()` дожидается `RECALPF` (recalibration pending, ≤ 2 такта RTCCLK) перед записью —
предыдущая калибровка должна быть аппаратно поглощена перед подачей новой.

---

## 10. Примеры использования

### Базовая настройка + время/дата

```cpp
RTC_cl::SetUp(RTC_cl::CLK_Source::LSE);
RTC_cl::SetTime({14, 25, 0});
RTC_cl::SetDate({22, 5, 26}, RTC_cl::WeekDay::Friday);   // 22 мая 2026, пятница

auto t = RTC_cl::GetTime();
uint8_t hh = t.hour(), mm = t.min(), ss = t.sec();
```

### WakeUp Timer — периодическое пробуждение раз в 2 секунды

```cpp
RTC_cl::EnableWakeUpTimer(/*counter=*/1, /*clock_div=*/0b100, []{
    // вызывается каждые (counter+1) = 2 секунды из RTC_TAMP_IRQn
});
```

### Alarm A — ежедневный будильник в фиксированное время

```cpp
RTC_cl::AlarmConfig cfg {
    .hour = {7,  RTC_cl::Alarm_Masks::Care},
    .min  = {30, RTC_cl::Alarm_Masks::Care},
    .sec  = {0,  RTC_cl::Alarm_Masks::Care},
};
RTC_cl::EnableAlarm_A(ENABLE, cfg, []{ /* доброе утро */ });
// позже:
RTC_cl::EnableAlarm_A(DISABLE);   // снимает ALRAE, не трогая ALRMAR
```

### Калибровка после измерения реального периода

```cpp
RTC_cl::SetCalibration(/*calm=*/6, /*calp=*/false);  // компенсация ухода на ~5.5 ppm
```

---

## 11. Угловые случаи

- **`RSF` не годится как признак "RTC уже настроен"** — используйте комбинацию `RTCEN` +
  `RTCSEL` (см. §3); это уже сделано внутри `SetUp()`, но важно при написании похожего кода
  для другой периферии backup-домена.
- **`WUTR`/`ALRMAR`/`ALRMBR` нельзя писать, пока включён соответствующий Enable-бит** —
  `EnableWakeUpTimer()`/`EnableAlarm_A/B()` сами снимают и ждут нужный флаг (`WUTWF`/`ALRAWF`/
  `ALRBWF`) перед записью; при ручной работе с регистрами это нужно повторить самостоятельно.
- **`EnableAlarm_A(ENABLE)` без явного `cfg`** использует `kNoAlarm` (все поля `Ignore`) — не
  "последнюю использованную" конфигурацию; для повторного включения с той же конфигурацией нужно
  передать её снова.
- **RTC-прерывания завязаны на `EXTI_IMR1_IM19`** — если кто-то извне (не через `IRQ_en()`)
  случайно снимет этот бит EXTI, `RTC_TAMP_IRQn` перестанет срабатывать даже при установленных
  `WUTIE`/`ALRAIE`/`ALRBIE`.
- **`to_bcd()` возвращает `0xFF` при `v > 99`** — не `DebugTrap`, просто явно невалидное BCD-
  значение; `SetTime`/`SetDate`/`build_alrmr` не проверяют это отдельно, невалидный аргумент
  молча попадёт в регистр как `0xFF`.
- **Единственный статический `_rtc_handler` на весь класс** — в отличие от `USART`/`TIM`/`SPI`,
  где `HandleIRQ()` — метод самого объекта периферии, здесь это выделенный singleton-объект
  (см. §8), потому что у самого `RTC_cl` нет экземпляров, которые могли бы им быть.

---

## См. также

- [IRQ_Registry.md](IRQ_Registry.md) — общий механизм, на котором построен `IRQ_en()` (§8);
  здесь регистрируется не `this` (класс статический), а адрес singleton-обработчика.
- [System.md](System.md) — `SysInitStatus`, общая инфраструктура `DebugTrap`/`SysStatus`.
