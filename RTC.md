# RTC / RTC_cl — Detailed Description

*[English](#english) · [Русский](#русский)*

---

## English

## Table of Contents

1. [Why This Is Needed and Ownership](#1-why-this-is-needed-and-ownership)
2. [SetUp and Retained Configuration](#2-setup-and-retained-configuration)
3. [Calendar Writes and Reads](#3-calendar-writes-and-reads)
4. [WakeUp Timer](#4-wakeup-timer)
5. [Alarms](#5-alarms)
6. [Timestamp](#6-timestamp)
7. [IRQ and DeInit](#7-irq-and-deinit)
8. [Calibration and Frequency Output](#8-calibration-and-frequency-output)
9. [Migration and Verification Limits](#9-migration-and-verification-limits)
10. [Usage Examples](#10-usage-examples)
11. [Edge Cases](#11-edge-cases)
12. [See Also](#see-also)

---

## 1. Why This Is Needed and Ownership

`RTC_cl` drives the real-time clock on F4/F7/G0:

- calendar (time and date);
- periodic wake-up timer;
- two alarms (A and B);
- timestamp capture on an external edge.

Events arrive through protected virtual callbacks (`OnWakeup`, `OnAlarmA`, `OnAlarmB`, `OnTimestamp`) that you override in a derived class — the same pattern as USART.

**Ownership rules:**

- One object owns the RTC after a successful `SetUp()`. A second `SetUp()` requires `DeInit()` first.
- Copy and move are disabled. The object must stay alive while its interrupts are registered.
- Call configuration and `DeInit()` from `main`, with SysTick running (timeouts use `GetTick()`; with SysTick masked they never expire).
- Interrupt handlers never wait and never change registrations.
- Coordinate configuration calls and calendar reads between `main` and interrupts yourself.

---

## 2. SetUp and Retained Configuration

```cpp
SysInitStatus status = clock.SetUp(RTC_cl::CLK_Source::LSE, 127, 255);
```

**Arguments:** clock source (`LSI`, `LSE` or `HSE`; `NO` is rejected), `PREDIV_A` (0…127) and `PREDIV_S` (0…32767).

```text
f_calendar = f_RTCCLK / ((PREDIV_A + 1) * (PREDIV_S + 1))
```

| Source | Dividers for 1 Hz | Notes |
| --- | --- | --- |
| LSE 32768 Hz | 127 / 255 | default dividers |
| LSI nominal 32000 Hz | 127 / 249 | default 127/255 would give ≈ 0.97656 Hz; real LSI frequency varies with chip, temperature and supply |
| HSE | application-chosen | HSE must already be running; on F4/F7 the application chooses RTCPRE, on G0 the input is HSE/32 |

The driver starts LSI/LSE itself and keeps the board's LSE drive-strength setting (it does not force maximum drive).

**`InitError`** when: the RTC is already owned, the source/dividers are invalid, a wait times out, or the retained configuration uses an incompatible 12-hour format/BYPSHAD.

**The RTC survives reset, so `SetUp()` distinguishes two cases:**

| Situation | What happens |
| --- | --- |
| RTC already running from the **same** source (RTCEN/INITS set) | Calendar and dividers are **kept**; shadow registers are resynchronized (RSF). New divider arguments are ignored for the running calendar. |
| A **different** source is retained | **The backup domain is reset** and the new source applied. |

> ⚠ **Changing the source resets the backup domain:** time, backup registers and other domain settings are lost, and a later `InitError` cannot bring them back. The reset also stops LSE — consider other users of LSE/backup domain.

In both cases old Alarm/WUT/Timestamp events and their interrupts are disabled; enable the ones you need again.

**Timeouts** (through `System::WaitBits`):

| Wait | Limit |
| --- | --- |
| LSE ready | 5000 ms |
| DBP, LSI, INITF, RSF, ALRAWF/ALRBWF, WUTWF | 1000 ms each |

Write protection (WPR) is restored on every exit, including timeouts. A busy calibration (RECALPF) returns `Busy` immediately, without waiting.

---

## 3. Calendar Writes and Reads

**Writing** — decimal values, 24-hour format, year 0…99 = 2000…2099:

```cpp
SysStatus t = clock.SetTime({14, 25, 0});
SysStatus d = clock.SetDate({22, 5, 26}, RTC_cl::WeekDay::Friday);   // 26 May 2022
```

- All ranges are checked: hours, minutes, seconds, month, day (with month length and leap years), weekday (Monday = 1 … Sunday = 7). Invalid input is not written.
- Results: `NotInit`, `Error`, `Timeout` or `OK`.
- After the write the driver leaves INIT mode and resynchronizes shadows; a sync failure does not undo the write.
- `SetTime` and `SetDate` are two separate operations — not an atomic pair.

**Packed BCD variants:** `SetTime_hex`/`SetDate_hex` take BCD (`{0x14, 0x25, 0x00}` = 14:25:00) and check both BCD digits and calendar ranges. `to_bcd(0…99)` returns packed BCD, `0xFF` otherwise.

**Reading:**

| Method | Reads |
| --- | --- |
| `GetTime()` | TR, then DR (to release the shadow lock) |
| `GetDate()` | DR alone |
| `GetDateTime(time, date)` | coherent pair: TR then DR — use this when you need both |

Read only after a successful `SetUp()`, and do not interrupt a pair read with another RTC read from a callback.

---

## 4. WakeUp Timer

```cpp
clock.EnableWakeUpTimer(0);   // 1 second with ck_spre = 1 Hz
clock.EnableWakeUpTimer(4);   // 5 seconds; replaces the previous setting
clock.DisableWakeUpTimer();
```

`count` (0…65535) goes into WUTR, so the period is `(count + 1) / f_WUT`.

| WUCKSEL / `clock_div` | Clock |
| --- | --- |
| 000 | RTCCLK / 16 |
| 001 | RTCCLK / 8 |
| 010 | RTCCLK / 4 |
| 011 | RTCCLK / 2 |
| 100 | ck_spre (calendar 1 Hz), 16-bit counter |
| 110 | ck_spre, counter extended by 65536 |

Codes 101/111 are rejected.

**`EnableWakeUpTimer()`:** disables WUTE/WUTIE, waits for WUTWF (≤ 1000 ms), writes the configuration, clears old flags, then registers and enables the event. On timeout or registration failure the timer stays off. Returns `SysStatus`.

`DisableWakeUpTimer()` does not wait. `OnWakeup()` is called after the flag is acknowledged.

---

## 5. Alarms

```cpp
RTC_cl::AlarmConfig alarm{
    .hour = {14, RTC_cl::Alarm_Masks::Care},
    .min  = {25, RTC_cl::Alarm_Masks::Care},
    .sec  = {0,  RTC_cl::Alarm_Masks::Care}
};
clock.EnableAlarm_A(ENABLE, alarm);   // every day at 14:25:00
clock.EnableAlarm_A(DISABLE);
clock.EnableAlarm_B(ENABLE, alarm);   // Alarm B has the same interface
```

**`AlarmConfig` fields:**

- `Ignore` — field not compared; `Care` — compared with the given decimal value.
- `day` — day of month (1…31), **not** weekday.
- Hours 0…23, minutes/seconds 0…59.
- `sub_sec` 0…32767 and `sub_sec_msk` (MASKSS) 0…15: 0 ignores sub-seconds, 1 compares the lowest bit, 15 compares all 15 bits. Pick the target according to your PRER dividers.

> The default (all fields `Ignore`) is **not** "alarm off" — it matches every second.

**`EnableAlarm_A/B()`:** disables the alarm (ALRxE/ALRxIE), waits for ALRxWF (≤ 1000 ms), clears the old flag and enables only after a successful registration. Notifications: `OnAlarmA()`/`OnAlarmB()`.

---

## 6. Timestamp

```cpp
clock.EnableTimestamp(ENABLE, RTC_cl::TimestampEdge::Rising);
// With interrupts: OnTimestamp(const Timestamp&) is called.

// Polling instead of the interrupt:
clock.IRQ_en(RTC_cl::IRQ_s::TS, DISABLE);
RTC_cl::Timestamp stamp;
if (clock.GetTimestamp(stamp) == SysStatus::OK) {
    uint8_t captured_hour = stamp.time.hour();
    bool lost_edge = stamp.overflow;
}
clock.EnableTimestamp(DISABLE);
```

- `EnableTimestamp()` sets TSE/TSIE and the edge, and clears stale TSF/TSOVF.
- The **RTC_TS pin** (usually PC13), its availability in your package and alternate routing are the application's job. This is not GPIO EXTI; internal TAMP events are not configured.
- Use **either** polling **or** the interrupt for a given capture, not both.

**How a capture is read:** TSTR/TSDR/TSSSR are read **before** TSF is cleared (clearing TSF releases the capture). The interrupt passes a snapshot to `OnTimestamp()`:

- the reference is valid only during the call — copy it if you need it later;
- no need to re-read the registers in the callback.

**Captured data:**

- no year — `date.year()` is 0;
- `sub_seconds` is the raw down-counter value, not milliseconds;
- `overflow` reflects TSOVF (an edge was lost).

`GetTimestamp()` returns `Busy` when there is no new capture; after a successful read it clears the capture and overflow flags.

---

## 7. IRQ and DeInit

**Vectors:**

| Family | Vector / EXTI line |
| --- | --- |
| G0 | `RTC_TAMP_IRQn`, internal EXTI19 |
| F4/F7 | `RTC_Alarm_IRQn`/EXTI17, `RTC_WKUP_IRQn`/EXTI22, `TAMP_STAMP_IRQn`/EXTI21 |

On F4/F7 give all three RTC vectors the **same preemption priority** — they serve one hardware block and must not interrupt each other.

**Registration:**

- The first enabled source registers the object (regardless of the current NVIC state).
- Registry full → `InitError` from `IRQ_en()` or `Error` from the event-configuration call.
- Enabling more sources does not create duplicate registrations.

**Handler:** takes one snapshot of CR and event flags, handles only enabled sources, acknowledges its flags **before** calling callbacks (F4/F7: rc_w0 writes that keep other flags, including TAMP; G0: SCR). It never waits.

**Rules:**

- `IRQ_en(source, DISABLE)` disables the source but keeps the registration until `DeInit()`.
- Callbacks must not call configuration methods, `IRQ_en()`, `SetUp()`/`DeInit()` or change the registry.
- To pause all RTC interrupts, mask NVIC yourself — and mind other users of shared vectors.

**`DeInit()`:**

- disables its own alarms, wake-up timer, timestamp and calibration output;
- removes only its own registrations and keeps NVIC enabled for remaining handlers (on F4/F7, EXTI21 stays on if TAMP_STAMP still has handlers; G0 TAMP uses its own internal EXTI21);
- **keeps** the calendar, clock source, calibration and backup registers;
- `SetUp()` may be called again afterwards.

---

## 8. Calibration and Frequency Output

```cpp
SysStatus cal = clock.SetCalibration(6, false);   // about -5.72 ppm at LSE 32768 Hz
// Busy = RECALPF set: retry later from main (no internal wait).
clock.EnableCOE(true, RTC_cl::CalOut::Out_512);   // RTCCLK/64 = 512 Hz at 32768 Hz
clock.EnableCOE(true, RTC_cl::CalOut::Out_1Hz);   // ck_spre
clock.EnableCOE(false);
```

**Smooth calibration:**

- CALM (0…511) removes pulses over a 32-second window; CALP adds 512 pulses.
- At nominal LSE one step ≈ 0.954 ppm; total range ≈ ±488 ppm.
- CALW8/CALW16 are not used.
- Measure your oscillator first, then calibrate.

**Calibration output (COE):** the RTC_CALIB pin and its routing are prepared by the application. The labels 512 Hz/1 Hz assume the matching input and calendar frequencies.

**`WriteProtection()`** remains for low-level access (keys `0xCA`, `0x53` unlock; `0xFF` locks). Ordinary methods handle write protection automatically.

---

## 9. Migration and Verification Limits

| Old API | New API |
| --- | --- |
| static functions | an `RTC_cl` object |
| function-pointer arguments for WUT/Alarm | overridden callbacks in a derived class |
| `void` configuration methods | methods return a status |

- The source/calendar are **not** reset on every start-up; a source change follows the explicit backup-domain reset policy (section 2).
- Behaviour that must be verified on your actual part: source change, LSE start-up, retained calendar after reset, alarms, wake-up, timestamp and shared TAMP operation.

---

## 10. Usage Examples

The blocks below continue with the same `Clock` object.

```cpp
#include "rtc.hpp"

class Clock : public RTC_cl {
protected:
    void OnWakeup() override { /* set a flag for main */ }
    void OnAlarmA() override { /* notify the application */ }
    void OnAlarmB() override { /* notify the application */ }
    void OnTimestamp(const Timestamp& stamp) override {
        // Copy stamp if you need it after the callback returns.
    }
};
static Clock clock;

// LSE 32768 Hz: 32768 / (128 * 256) = 1 Hz.
SysInitStatus status = clock.SetUp(RTC_cl::CLK_Source::LSE, 127, 255);
// Alternative for nominal LSI 32000 Hz (instead of, not after, the call above):
// clock.SetUp(RTC_cl::CLK_Source::LSI, 127, 249);
```

```cpp
if (status == SysInitStatus::InitOK) {
    SysStatus t = clock.SetTime({14, 25, 0});
    SysStatus d = clock.SetDate({22, 5, 26}, RTC_cl::WeekDay::Friday);
}
RTC_cl::RtcTime time;
RTC_cl::RtcDate date;
clock.GetDateTime(time, date);   // coherent pair: TR then DR
uint8_t hours   = time.hour();
uint8_t minutes = time.min();
uint8_t seconds = time.sec();
uint8_t day     = date.day();
```

```cpp
clock.EnableWakeUpTimer(0);   // 1 s
clock.EnableWakeUpTimer(4);   // 5 s, replaces the previous setting
clock.DisableWakeUpTimer();
```

```cpp
RTC_cl::AlarmConfig alarm{
    .hour = {14, RTC_cl::Alarm_Masks::Care},
    .min  = {25, RTC_cl::Alarm_Masks::Care},
    .sec  = {0,  RTC_cl::Alarm_Masks::Care}
};
clock.EnableAlarm_A(ENABLE, alarm);   // daily at 14:25:00
clock.EnableAlarm_A(DISABLE);
clock.EnableAlarm_B(ENABLE, alarm);
```

```cpp
clock.EnableTimestamp(ENABLE, RTC_cl::TimestampEdge::Rising);
clock.IRQ_en(RTC_cl::IRQ_s::TS, DISABLE);   // poll instead of IRQ
RTC_cl::Timestamp stamp;
if (clock.GetTimestamp(stamp) == SysStatus::OK) {
    uint8_t captured_hour = stamp.time.hour();
    bool lost_edge = stamp.overflow;
}
clock.EnableTimestamp(DISABLE);
```

Check every returned status.

---

## 11. Edge Cases

| Situation | Behaviour |
| --- | --- |
| Clock source changed | Backup domain reset: time and backup registers lost. |
| Same source retained after reset | Dividers are not changed, even if new ones are passed. |
| LSI as source | Nominal frequency only — measure and choose dividers. |
| Pair read interrupted by another RTC read | Not allowed — the shadow pair can become inconsistent. |
| Timestamp year | Not captured (0). |
| `OnTimestamp` reference used later | Invalid — copy the data inside the callback. |

---

## See Also

- [ST F4 HAL RTC: shadow, initialization and alarm API](https://github.com/STMicroelectronics/stm32f4xx-hal-driver/blob/master/Src/stm32f4xx_hal_rtc.c)
- [ST G0 HAL RTC ex: timestamp read before clearing TSF](https://github.com/STMicroelectronics/stm32g0xx-hal-driver/blob/master/Src/stm32g0xx_hal_rtc_ex.c)
- [RM0390: WUCKSEL and MASKSS](https://www.st.com/resource/en/reference_manual/dm00135183.pdf)
- [System.md](System.md)
- [IRQ_Registry.md](IRQ_Registry.md)
- [EXTI.md](EXTI.md)

---

## Русский

## Содержание

1. [Зачем это нужно и модель работы](#1-зачем-это-нужно-и-модель-работы)
2. [SetUp и сохранённые настройки](#2-setup-и-сохранённые-настройки)
3. [Календарь: запись и чтение](#3-календарь-запись-и-чтение)
4. [WakeUp Timer](#4-wakeup-timer-1)
5. [Будильники](#5-будильники)
6. [Timestamp](#6-timestamp-1)
7. [IRQ и освобождение](#7-irq-и-освобождение)
8. [Калибровка и вывод частоты](#8-калибровка-и-вывод-частоты)
9. [Миграция и проверка](#9-миграция-и-проверка)
10. [Примеры использования](#10-примеры-использования)
11. [Угловые случаи](#11-угловые-случаи)
12. [См. также](#см-также)

---

## 1. Зачем это нужно и модель работы

`RTC_cl` управляет часами реального времени на F4/F7/G0:

- календарь (время и дата);
- периодический таймер пробуждения (WakeUp);
- два будильника (A и B);
- захват метки времени (timestamp) по внешнему фронту.

События приходят через protected virtual callback (`OnWakeup`, `OnAlarmA`, `OnAlarmB`, `OnTimestamp`), которые вы переопределяете в наследнике — как в USART.

**Правила владения:**

- После успешного `SetUp()` RTC принадлежит одному объекту. Повторный `SetUp()` — только после `DeInit()`.
- Копирование и перемещение запрещены. Объект должен существовать, пока зарегистрированы его прерывания.
- Настройку и `DeInit()` вызывайте из `main` при работающем SysTick (таймауты считаются по `GetTick()`; при замаскированном SysTick они не истекают).
- Обработчики прерываний ничего не ждут и не меняют регистрации.
- Согласование вызовов настройки и чтения календаря между `main` и прерываниями — задача приложения.

---

## 2. SetUp и сохранённые настройки

```cpp
class Clock : public RTC_cl {
protected:
    void OnWakeup() override { /* выставить флаг для main */ }
    void OnAlarmA() override { /* уведомить приложение */ }
    void OnAlarmB() override { /* уведомить приложение */ }
    void OnTimestamp(const Timestamp& stamp) override {
        // Скопировать stamp, если данные нужны после выхода из callback.
        // Ссылка действует только во время вызова.
    }
};
static Clock clock;

// LSE, 32768 Гц: 32768 / (128 * 256) = 1 Гц.
SysInitStatus status = clock.SetUp(RTC_cl::CLK_Source::LSE, 127, 255);
// Альтернатива для номинального LSI 32000 Гц (вместо, а не после вызова выше):
// clock.SetUp(RTC_cl::CLK_Source::LSI, 127, 249);
```

**Аргументы:** источник (`LSI`, `LSE` или `HSE`; `NO` отклоняется), `PREDIV_A` (0…127) и `PREDIV_S` (0…32767).

```text
f_календаря = f_RTCCLK / ((PREDIV_A + 1) * (PREDIV_S + 1))
```

| Источник | Делители для 1 Гц | Примечание |
| --- | --- | --- |
| LSE 32768 Гц | 127 / 255 | делители по умолчанию |
| LSI, номинал 32000 Гц | 127 / 249 | с 127/255 получится ≈ 0,97656 Гц; реальная частота LSI зависит от экземпляра, температуры и питания |
| HSE | выбирает приложение | HSE должен быть уже запущен; на F4/F7 приложение задаёт RTCPRE, на G0 вход — HSE/32 |

LSI/LSE драйвер запускает сам и сохраняет заданную платой мощность драйвера LSE (максимальную принудительно не выбирает).

**`InitError`**, если: RTC уже занят, неверен источник/делители, истёк таймаут, или сохранённая конфигурация использует несовместимый 12-часовой формат/BYPSHAD.

**RTC переживает сброс, поэтому `SetUp()` различает два случая:**

| Ситуация | Что происходит |
| --- | --- |
| RTC уже работает от **того же** источника (RTCEN/INITS установлены) | Календарь и делители **сохраняются**; shadow-регистры синхронизируются заново (RSF). Новые делители для идущих часов игнорируются. |
| Сохранён **другой** источник | **Backup-домен сбрасывается**, применяется новый источник. |

> ⚠ **Смена источника сбрасывает backup-домен:** теряются время, backup-регистры и другие настройки домена, и последующий `InitError` их не вернёт. Сброс также останавливает LSE — учитывайте других потребителей LSE/backup-домена. Это согласованная политика смены источника после перепрошивки.

В обоих случаях старые события Alarm/WUT/Timestamp и их прерывания отключаются; нужные включите заново.

**Пределы ожидания** (через `System::WaitBits`):

| Ожидание | Предел |
| --- | --- |
| Готовность LSE | 5000 мс |
| DBP, LSI, INITF, RSF, ALRAWF/ALRBWF, WUTWF | по 1000 мс |

Защита записи (WPR) восстанавливается при любом выходе, включая таймаут. Занятая калибровка (RECALPF) сразу возвращает `Busy` без ожидания.

---

## 3. Календарь: запись и чтение

**Запись** — десятичные значения, 24-часовой формат, год 0…99 = 2000…2099:

```cpp
if (status == SysInitStatus::InitOK) {
    SysStatus t = clock.SetTime({14, 25, 0});
    SysStatus d = clock.SetDate({22, 5, 26}, RTC_cl::WeekDay::Friday);   // 26 мая 2022
}
```

- Проверяются все диапазоны: часы, минуты, секунды, месяц, день (с учётом длины месяца и високосного года), день недели (понедельник = 1 … воскресенье = 7). Неверный ввод не записывается.
- Результаты: `NotInit`, `Error`, `Timeout` или `OK`.
- После записи драйвер выходит из режима INIT и синхронизирует shadow-регистры; ошибка синхронизации запись не отменяет.
- `SetTime` и `SetDate` — две отдельные операции, а не атомарная установка пары.

**Варианты в упакованном BCD:** `SetTime_hex`/`SetDate_hex` принимают BCD (`{0x14, 0x25, 0x00}` = 14:25:00) и проверяют и BCD-цифры, и календарные диапазоны. `to_bcd(0…99)` возвращает упакованный BCD, иначе `0xFF`.

**Чтение:**

```cpp
RTC_cl::RtcTime time;
RTC_cl::RtcDate date;
clock.GetDateTime(time, date); // Согласованная пара: TR, затем DR.
uint8_t hours   = time.hour();
uint8_t minutes = time.min();
uint8_t seconds = time.sec();
uint8_t day     = date.day();
```

| Метод | Что читает |
| --- | --- |
| `GetTime()` | TR, затем DR (чтобы снять блокировку shadow) |
| `GetDate()` | только DR |
| `GetDateTime(time, date)` | согласованную пару TR → DR — используйте, когда нужны оба |

Читайте только после успешного `SetUp()` и не прерывайте чтение пары другим чтением RTC из callback.

---

## 4. WakeUp Timer

```cpp
clock.EnableWakeUpTimer(0); // 1 секунда при ck_spre = 1 Гц.
clock.EnableWakeUpTimer(4); // 5 секунд, прежняя настройка заменяется.
clock.DisableWakeUpTimer();
```

`count` (0…65535) записывается в WUTR, поэтому период равен `(count + 1) / f_WUT`.

| WUCKSEL / `clock_div` | Источник тактов |
| --- | --- |
| 000 | RTCCLK / 16 |
| 001 | RTCCLK / 8 |
| 010 | RTCCLK / 4 |
| 011 | RTCCLK / 2 |
| 100 | ck_spre (1 Гц календаря), 16-битный счётчик |
| 110 | ck_spre, к счётчику добавляется 65536 |

Коды 101/111 отклоняются.

**`EnableWakeUpTimer()`:** отключает WUTE/WUTIE, ждёт WUTWF (≤ 1000 мс), записывает параметры, сбрасывает старые флаги, затем регистрирует и включает событие. При таймауте или ошибке регистрации таймер остаётся выключенным. Возвращает `SysStatus`.

`DisableWakeUpTimer()` не ждёт. `OnWakeup()` вызывается после сброса флага.

---

## 5. Будильники

```cpp
RTC_cl::AlarmConfig alarm{
    .hour = {14, RTC_cl::Alarm_Masks::Care},
    .min  = {25, RTC_cl::Alarm_Masks::Care},
    .sec  = {0,  RTC_cl::Alarm_Masks::Care}
};
clock.EnableAlarm_A(ENABLE, alarm); // Ежедневно в 14:25:00.
clock.EnableAlarm_A(DISABLE);
// Alarm B имеет такой же интерфейс:
clock.EnableAlarm_B(ENABLE, alarm);
```

**Поля `AlarmConfig`:**

- `Ignore` — поле не сравнивается; `Care` — сравнивается с заданным десятичным значением.
- `day` — число месяца (1…31), **не** день недели.
- Часы 0…23, минуты/секунды 0…59.
- `sub_sec` 0…32767 и `sub_sec_msk` (MASKSS) 0…15: 0 — подсекунды игнорируются, 1 — сравнивается младший бит, 15 — все 15 бит. Цель выбирайте с учётом делителей PRER.

> Конфигурация по умолчанию (все поля `Ignore`) — это **не** выключенный будильник: она срабатывает каждую секунду.

**`EnableAlarm_A/B()`:** выключает будильник (ALRxE/ALRxIE), ждёт ALRxWF (≤ 1000 мс), сбрасывает старый флаг и включает только после успешной регистрации. Уведомления: `OnAlarmA()`/`OnAlarmB()`.

---

## 6. Timestamp

```cpp
clock.EnableTimestamp(ENABLE, RTC_cl::TimestampEdge::Rising);
// В прерывании вызывается OnTimestamp(const Timestamp&).
// Для опроса вместо прерывания:
clock.IRQ_en(RTC_cl::IRQ_s::TS, DISABLE);
RTC_cl::Timestamp stamp;
if (clock.GetTimestamp(stamp) == SysStatus::OK) {
    uint8_t captured_hour = stamp.time.hour();
    bool lost_edge = stamp.overflow;
}
clock.EnableTimestamp(DISABLE);
```

- `EnableTimestamp()` включает TSE/TSIE, выбирает фронт и сбрасывает старые TSF/TSOVF.
- **Вывод RTC_TS** (обычно PC13), его наличие в корпусе и альтернативную маршрутизацию готовит приложение. Это не GPIO EXTI; внутренние события TAMP не настраиваются.
- Для одного захвата используйте **либо** опрос, **либо** прерывание, не оба сразу.

**Как читается захват:** TSTR/TSDR/TSSSR читаются **до** сброса TSF (сброс TSF удаляет аппаратный захват). Прерывание передаёт снимок в `OnTimestamp()`:

- ссылка действительна только во время вызова — скопируйте данные, если они нужны позже;
- повторно читать регистры из callback не нужно.

**Что содержит захват:**

- года нет — `date.year()` равен 0;
- `sub_seconds` — значение нисходящего счётчика, а не миллисекунды;
- `overflow` отражает TSOVF (фронт был потерян).

`GetTimestamp()` возвращает `Busy`, если нового захвата нет; после успешного чтения сбрасывает флаги захвата и переполнения.

---

## 7. IRQ и освобождение

**Векторы:**

| Семейство | Вектор / линия EXTI |
| --- | --- |
| G0 | `RTC_TAMP_IRQn`, внутренняя EXTI19 |
| F4/F7 | `RTC_Alarm_IRQn`/EXTI17, `RTC_WKUP_IRQn`/EXTI22, `TAMP_STAMP_IRQn`/EXTI21 |

На F4/F7 дайте всем трём векторам RTC **одинаковый приоритет вытеснения** — они обслуживают один аппаратный блок и не должны прерывать друг друга.

**Регистрация:**

- Первое включение любого источника регистрирует объект (независимо от текущего состояния NVIC).
- Реестр заполнен → `InitError` из `IRQ_en()` или `Error` из функции настройки события.
- Включение следующих источников дубликатов не создаёт.

**Обработчик:** делает один снимок CR и флагов событий, обрабатывает только включённые источники и сбрасывает свои флаги **до** вызова callback (F4/F7: запись rc_w0 с сохранением остальных флагов, включая TAMP; G0: SCR). Ожиданий в нём нет.

**Правила:**

- `IRQ_en(источник, DISABLE)` выключает источник, но регистрация сохраняется до `DeInit()`.
- Callback не должны вызывать методы настройки, `IRQ_en()`, `SetUp()`/`DeInit()` и менять реестр.
- Чтобы приостановить все прерывания RTC, маскируйте NVIC сами — с учётом других пользователей общих векторов.

**`DeInit()`:**

- выключает свои будильники, таймер пробуждения, timestamp и вывод калибровки;
- удаляет только свои регистрации и сохраняет NVIC для оставшихся обработчиков (на F4/F7 EXTI21 остаётся включённой, если на TAMP_STAMP есть другие обработчики; TAMP на G0 использует отдельную внутреннюю EXTI21);
- **сохраняет** календарь, источник, калибровку и backup-регистры;
- после него можно снова вызвать `SetUp()`.

---

## 8. Калибровка и вывод частоты

```cpp
SysStatus cal = clock.SetCalibration(6, false); // Около -5,72 ppm при LSE 32768 Гц.
// Busy означает RECALPF = 1: повторить позже из main, ожидания внутри нет.
clock.EnableCOE(true, RTC_cl::CalOut::Out_512); // RTCCLK/64 = 512 Гц при 32768 Гц.
clock.EnableCOE(true, RTC_cl::CalOut::Out_1Hz); // ck_spre.
clock.EnableCOE(false);
```

**Плавная калибровка:**

- CALM (0…511) убирает импульсы в 32-секундном окне; CALP добавляет 512 импульсов.
- При номинальном LSE один шаг ≈ 0,954 ppm, весь диапазон ≈ ±488 ppm.
- CALW8/CALW16 не используются.
- Сначала измерьте генератор, затем калибруйте.

**Вывод калибровки (COE):** вывод RTC_CALIB и его маршрутизацию готовит приложение. Подписи 512 Гц/1 Гц верны при соответствующих частотах входа и календаря.

**`WriteProtection()`** оставлен для низкоуровневого доступа (ключи `0xCA`, `0x53` снимают защиту, `0xFF` возвращает). Обычные методы управляют защитой сами.

---

## 9. Миграция и проверка

| Старый API | Новый API |
| --- | --- |
| статические функции | объект `RTC_cl` |
| аргументы-функции для WUT/Alarm | переопределённые callback в наследнике |
| методы настройки без результата | методы возвращают статус |

- Источник и календарь **не** сбрасываются при каждом запуске; смена источника следует явной политике сброса backup-домена (раздел 2). Без `DeInit()` объект не перенастраивается.
- На используемом МК нужно проверить: смену источника, запуск LSE, сохранение календаря после сброса, будильники, пробуждение, timestamp и совместную работу с TAMP.

---

## 10. Примеры использования

Блоки ниже продолжают работу с одним и тем же объектом `Clock`.

```cpp
#include "rtc.hpp"

class Clock : public RTC_cl {
protected:
    void OnWakeup() override { /* выставить флаг для main */ }
    void OnAlarmA() override { /* уведомить приложение */ }
    void OnAlarmB() override { /* уведомить приложение */ }
    void OnTimestamp(const Timestamp& stamp) override {
        // Скопировать stamp, если данные нужны после выхода из callback.
    }
};
static Clock clock;

// LSE, 32768 Гц: 32768 / (128 * 256) = 1 Гц.
SysInitStatus status = clock.SetUp(RTC_cl::CLK_Source::LSE, 127, 255);
// Альтернатива для номинального LSI 32000 Гц (вместо, а не после вызова выше):
// clock.SetUp(RTC_cl::CLK_Source::LSI, 127, 249);
```

```cpp
if (status == SysInitStatus::InitOK) {
    SysStatus t = clock.SetTime({14, 25, 0});
    SysStatus d = clock.SetDate({22, 5, 26}, RTC_cl::WeekDay::Friday);
}
RTC_cl::RtcTime time;
RTC_cl::RtcDate date;
clock.GetDateTime(time, date); // Согласованная пара: TR, затем DR.
uint8_t hours   = time.hour();
uint8_t minutes = time.min();
uint8_t seconds = time.sec();
uint8_t day     = date.day();
```

```cpp
clock.EnableWakeUpTimer(0); // 1 с
clock.EnableWakeUpTimer(4); // 5 с, прежняя настройка заменяется
clock.DisableWakeUpTimer();
```

```cpp
RTC_cl::AlarmConfig alarm{
    .hour = {14, RTC_cl::Alarm_Masks::Care},
    .min  = {25, RTC_cl::Alarm_Masks::Care},
    .sec  = {0,  RTC_cl::Alarm_Masks::Care}
};
clock.EnableAlarm_A(ENABLE, alarm); // Ежедневно в 14:25:00.
clock.EnableAlarm_A(DISABLE);
clock.EnableAlarm_B(ENABLE, alarm);
```

```cpp
clock.EnableTimestamp(ENABLE, RTC_cl::TimestampEdge::Rising);
clock.IRQ_en(RTC_cl::IRQ_s::TS, DISABLE); // опрос вместо прерывания
RTC_cl::Timestamp stamp;
if (clock.GetTimestamp(stamp) == SysStatus::OK) {
    uint8_t captured_hour = stamp.time.hour();
    bool lost_edge = stamp.overflow;
}
clock.EnableTimestamp(DISABLE);
```

Проверяйте каждый возвращаемый статус.

---

## 11. Угловые случаи

| Ситуация | Поведение |
| --- | --- |
| Сменился источник тактирования | Сброс backup-домена: время и backup-регистры теряются. |
| После сброса сохранён тот же источник | Делители не меняются, даже если переданы новые. |
| Источник LSI | Частота только номинальная — измерьте и подберите делители. |
| Чтение пары прервано другим чтением RTC | Нельзя — пара shadow-регистров может рассогласоваться. |
| Год в timestamp | Не захватывается (0). |
| Ссылка из `OnTimestamp` используется позже | Недействительна — копируйте данные внутри callback. |

---

## См. также

- [ST F4 HAL RTC: shadow, инициализация и API будильников](https://github.com/STMicroelectronics/stm32f4xx-hal-driver/blob/master/Src/stm32f4xx_hal_rtc.c)
- [ST G0 HAL RTC ex: чтение timestamp до сброса TSF](https://github.com/STMicroelectronics/stm32g0xx-hal-driver/blob/master/Src/stm32g0xx_hal_rtc_ex.c)
- [RM0390: WUCKSEL и MASKSS](https://www.st.com/resource/en/reference_manual/dm00135183.pdf)
- [System.md](System.md)
- [IRQ_Registry.md](IRQ_Registry.md)
- [EXTI.md](EXTI.md)
