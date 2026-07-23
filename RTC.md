# RTC / RTC_cl — подробное описание

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
