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

RTC_cl provides the F4/F7/G0 calendar, wakeup, alarms and timestamp events through
protected virtual callbacks. One object owns RTC after successful SetUp. Repeat
setup requires DeInit; copy/move is disabled and the object must stay alive while
registered. Settings/DeInit run in main with working SysTick/GetTick. Masked
SysTick prevents deadlines from advancing. IRQ never waits or changes registration.
Serialize settings and shadow reads between main/IRQ.

---

## 2. SetUp and Retained Configuration

SetUp selects CLK_Source::LSI/LSE/HSE and PREDIV_A/PREDIV_S. Valid ranges are
0..127 and 0..32767; NO is invalid. InitError covers occupied RTC, invalid
source/dividers, timeout and incompatible retained 12-hour/BYPSHAD configuration.

If the retained source matches and RTCEN/INITS are set, calendar/dividers are kept
and shadows resynchronized through RSF. New divider arguments do not change an
already running retained calendar. Old Alarm/WUT/TS events/IRQs are disabled;
the application enables desired events again.

**Changing the retained source resets the backup domain and applies the new
source: time, backup registers and other domain settings are lost.** A later
InitError cannot restore them. Reset also stops LSE; account for other consumers.

The driver starts LSI/LSE, retaining board-configured LSE electrical settings.
HSE is application-started; F4/F7 RTCPRE and dividers are application-selected,
while G0 uses HSE/32. Calendar frequency is:

```text
f_calendar = f_RTCCLK / ((PREDIV_A + 1) * (PREDIV_S + 1))
```

Defaults LSI,127/255 assume 32768 Hz. Nominal 32000 Hz LSI gives about 0.97656 Hz:
choose dividers explicitly (127/249 for nominal 32000 Hz). Actual RC frequency
depends on device, temperature and supply. LSE waits up to 5000 ms; DBP/LSI/INITF/
RSF/alarm/write flags up to 1000 ms each through System::WaitBits. WPR protection
is restored on all exits. Busy calibration RECALPF returns Busy without waiting.

---

## 3. Calendar Writes and Reads

Time/Date use decimal values, 24-hour format and years 0..99 for 2000..2099.
Hours/minutes/seconds/month/day/leap-year/weekday ranges are checked; invalid
input is not written. Weekday is Monday=1..Sunday=7. SetTime/SetDate return
NotInit/Error/Timeout/OK. Exit from INIT and shadow sync follow the write;
sync failure does not undo it. Separate time/date calls are not an atomic pair.

SetTime_hex/SetDate_hex retain packed BCD (0x14,0x25,0x00 means 14:25:00), with
BCD-digit and calendar checks. to_bcd accepts 0..99, otherwise returns 0xFF.
GetTime also reads DR to release the shadow lock. GetDate reads a standalone
date; GetDateTime reads coherent TR then DR. Do not interrupt that pair with
another RTC read from a callback; read only after successful SetUp.

---

## 4. WakeUp Timer

EnableWakeUpTimer count is WUTR, so period is (count+1)/clock for the ordinary
16-bit selections. The driver disables WUTE/WUTIE, waits for WUTWF up to 1000 ms,
writes the configuration, clears old flags and registers/enables the event.
Timeout/registration failure leaves it off. The method returns SysStatus;
DisableWakeUpTimer does not wait. OnWakeup follows flag acknowledgement.

| WUCKSEL/clock_div | Source |
|---|---|
| 000 | RTCCLK/16 |
| 001 | RTCCLK/8 |
| 010 | RTCCLK/4 |
| 011 | RTCCLK/2 |
| 100 | ck_spre, 16-bit counter |
| 110 | ck_spre; counter value is extended by 65536 |

Use the selected device's range/clock. With 1 Hz ck_spre and the default clock,
count=0 gives one second and count=4 gives five.

---

## 5. Alarms

AlarmConfig fields with Ignore are not compared; Care uses decimal values.
day is day-of-month, not weekday. Hours are 0..23, minutes/seconds 0..59 and
sub_sec is 0..32767. MASKSS/sub_sec_msk is 0..15: 0 ignores subseconds, 1 compares
the low bit, 15 all fifteen bits. Select the target with the configured PRER.
Default all-Ignore configuration does not mean an alarm is disabled.

EnableAlarm_A/B disables the selected ALRxE/ALRxIE, waits for ALRxWF up to 1000 ms,
clears its old flag and enables after successful registration. Failure does not
enable it. Notifications are OnAlarmA/OnAlarmB.

---

## 6. Timestamp

EnableTimestamp configures TSE/TSIE and edge and clears previous TSF/TSOVF.
The application prepares the bonded RTC_TS pin/routing; this is not GPIO EXTI
and internal TAMP events are not configured. Choose polling or IRQ consumption,
not both for the same capture.

TSTR/TSDR/TSSSR are read before clearing TSF, because that clear releases the
capture. IRQ passes a snapshot to OnTimestamp without a permanent object buffer;
the callback reference is valid only during the call. No second register read
is needed. Timestamp has no year (date.year()=0); sub_seconds is a downcounter,
not milliseconds. overflow reflects TSOVF. GetTimestamp returns Busy with no
new TSF and clears capture/overflow after a successful read.

---

## 7. IRQ and DeInit

G0 uses RTC_TAMP_IRQn and internal EXTI19. F4/F7 use RTC_Alarm/EXTI17,
RTC_WKUP/EXTI22 and TAMP_STAMP/EXTI21. Keep all F4/F7 RTC vectors at the same
preemption priority. First source enable registers regardless of current NVIC;
exhaustion returns InitError from IRQ_en or Error from event configuration.
Repeated source enable creates no duplicate registration.

IRQ snapshots CR/events, handles enabled sources only and acknowledges before
callbacks. F4/F7 rc_w0 writes preserve other flags including TAMP; G0 uses SCR.
IRQ_en(DISABLE) changes the source but retains registration until DeInit.
RTC callbacks must not call configuration, IRQ_en, DeInit or alter the registry.
Mask vectors externally when needed, accounting for shared consumers.

DeInit disables own alarms/WUT/timestamp/calibration output and removes only its
registrations, preserving other handlers and their NVIC state. F4/F7 EXTI21 is
kept when TAMP_STAMP still has handlers. G0 TAMP uses separate internal EXTI21.
The calendar/source/calibration/backup contents remain; SetUp can run again.

---

## 8. Calibration and Frequency Output

CALM 0..511 suppresses pulses over a 32-second window; CALP adds 512 pulses.
At nominal LSE one step is about 0.954 ppm, approximately ±488 ppm overall.
CALW8/CALW16 are not set. Busy RECALPF returns immediately, so retry from main.
Measure the source first. RTC_CALIB pin/routing are application responsibilities;
512/1 Hz output labels assume the corresponding input/calendar clocks.
WriteProtection retains low-level keys 0xCA,0x53 for unlock and 0xFF for protect;
ordinary methods handle protection automatically.

```cpp
SysStatus cal = clock.SetCalibration(6, false);
clock.EnableCOE(true, RTC_cl::CalOut::Out_512);
clock.EnableCOE(true, RTC_cl::CalOut::Out_1Hz); // ck_spre.
clock.EnableCOE(false);
```

---

## 9. Migration and Verification Limits

Static calls become an RTC_cl object; WUT/Alarm function arguments become
inherited callbacks. Configuration methods return status. The source/calendar
are not reset on every startup; changing a retained source follows the explicit
backup-domain reset policy. Prior source inspection did not include builds or
hardware tests. Source changes, LSE startup, retained time, alarms, timestamp and
shared TAMP operation require checking on the actual part.

---

## 10. Usage Examples

```cpp
class Clock : public RTC_cl {
protected:
    void OnWakeup() override {  }
    void OnAlarmA() override {  }
    void OnAlarmB() override {  }
    void OnTimestamp(const Timestamp& stamp) override {
    }
};
static Clock clock;
SysInitStatus status = clock.SetUp(RTC_cl::CLK_Source::LSE, 127, 255);
// clock.SetUp(RTC_cl::CLK_Source::LSI, 127, 249);
```

```cpp
if (status == SysInitStatus::InitOK) {
    SysStatus t = clock.SetTime({14, 25, 0});
    SysStatus d = clock.SetDate({22, 5, 26}, RTC_cl::WeekDay::Friday);
}
RTC_cl::RtcTime time;
RTC_cl::RtcDate date;
clock.GetDateTime(time, date);
uint8_t hours = time.hour();
uint8_t minutes = time.min();
uint8_t seconds = time.sec();
uint8_t day = date.day();
```

```cpp
clock.EnableWakeUpTimer(0);
clock.EnableWakeUpTimer(4);
clock.DisableWakeUpTimer();
```

```cpp
RTC_cl::AlarmConfig alarm{
    .hour = {14, RTC_cl::Alarm_Masks::Care},
    .min  = {25, RTC_cl::Alarm_Masks::Care},
    .sec  = {0,  RTC_cl::Alarm_Masks::Care}
};
clock.EnableAlarm_A(ENABLE, alarm);
clock.EnableAlarm_A(DISABLE);
clock.EnableAlarm_B(ENABLE, alarm);
```

```cpp
clock.EnableTimestamp(ENABLE, RTC_cl::TimestampEdge::Rising);
clock.IRQ_en(RTC_cl::IRQ_s::TS, DISABLE);
RTC_cl::Timestamp stamp;
if (clock.GetTimestamp(stamp) == SysStatus::OK) {
    uint8_t captured_hour = stamp.time.hour();
    bool lost_edge = stamp.overflow;
}
clock.EnableTimestamp(DISABLE);
```

Examples continue the same Clock object. Check every returned status. Timestamp polling and IRQ use are alternatives.

---

## 11. Edge Cases

Source changes destroy backup-domain contents. Retained matching sources keep their dividers. LSI frequency is nominal, not measured. Shadow read pairs must not be interleaved. Timestamp has no year, and its callback reference is temporary. No hardware waits are permitted in IRQ.

---

## See Also

- [RTC: shadow, initialization and alarm API](https://github.com/STMicroelectronics/stm32f4xx-hal-driver/blob/master/Src/stm32f4xx_hal_rtc.c).
- [G0 Timestamp: чтение до сброса TSF](https://github.com/STMicroelectronics/stm32g0xx-hal-driver/blob/master/Src/stm32g0xx_hal_rtc_ex.c).
- [RM0390: WUCKSEL и MASKSS](https://www.st.com/resource/en/reference_manual/dm00135183.pdf).

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

Реализованы STM32F4/F7/G0. RTC_cl — объект, события переопределяются через
protected virtual методы, как в USART. Один объект владеет единственным RTC.
Повторный успешный SetUp запрещён до DeInit; копирование и перемещение запрещены.
Объект должен жить до удаления его регистраций.

Настройка и DeInit вызываются из main. Нужен работающий System::GetTick/SysTick;
при замаскированном SysTick таймауты не отсчитываются. В обработчике IRQ нет
ожиданий, изменения реестра и вызовов конфигурации. Все обращения к настройкам
и чтению shadow-регистров приложения должны быть согласованы между main/IRQ.

---

## 2. SetUp и сохранённые настройки

~~~cpp
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
// Альтернатива для номинального LSI 32000 Гц (не второй вызов SetUp):
// clock.SetUp(RTC_cl::CLK_Source::LSI, 127, 249);
~~~

SetUp возвращает InitError при занятом RTC, неверном источнике/делителях,
таймауте или несовместимом сохранённом формате 12 h/BYPSHAD. Допустимы
PREDIV_A=0..127, PREDIV_S=0..32767. CLK_Source::NO отвергается.

При том же сохранённом источнике RTCEN/INITS календарь и делители сохраняются,
shadow-регистры повторно синхронизируются через RSF. Аргументы делителей в этом
случае не переустанавливают ходящие часы. SetUp отключает старые Alarm/WUT/TS
события и их IRQ; приложение включает нужные события заново.

**Если уже выбран другой источник, SetUp сбрасывает backup domain и применяет
новый источник. Теряются время, backup-регистры и другие настройки этого домена.**
Это согласованная политика смены источника после перепрошивки. Возврат InitError
после такого сброса не восстанавливает прежние данные. Сброс также останавливает
LSE; приложение учитывает других потребителей LSE/backup domain.

LSI/LSE запускает драйвер. У LSE сохраняется заданная платой электрическая
настройка; максимальная мощность драйвера принудительно не выбирается.
HSE должен быть заранее запущен приложением. На F4/F7 приложение задаёт RTCPRE
и соответствующие частоте делители; на G0 вход RTC — HSE/32.

Формула: f_calendar = f_RTCCLK / ((PREDIV_A+1)*(PREDIV_S+1)). Старые значения
по умолчанию сохранены: LSI, 127/255. Они рассчитаны на 32768 Гц: при номинальном
LSI 32000 Гц дают около 0.97656 Гц, поэтому для LSI нужно явно выбрать делители.
Фактическая частота RC зависит от экземпляра, температуры и питания; равенство
номиналу не гарантируется.

Ожидания выполняются через System::WaitBits: LSE — до 5000 мс, DBP, LSI, INITF,
RSF, ALRAWF/ALRBWF и WUTWF — до 1000 мс каждое. При выходе, в том числе при
таймауте, WPR снова защищён. Занятая калибровка RECALPF возвращает Busy сразу.

---

## 3. Календарь: запись и чтение

~~~cpp
if (status == SysInitStatus::InitOK) {
    SysStatus t = clock.SetTime({14, 25, 0});
    SysStatus d = clock.SetDate({22, 5, 26}, RTC_cl::WeekDay::Friday);
}
RTC_cl::RtcTime time;
RTC_cl::RtcDate date;
clock.GetDateTime(time, date); // Согласованная пара: TR, затем DR.
uint8_t hours = time.hour();
uint8_t minutes = time.min();
uint8_t seconds = time.sec();
uint8_t day = date.day();
~~~

Time/Date содержат десятичные значения; год 0..99 означает 2000..2099.
Проверяются 24-часовой формат, минуты/секунды, месяц, день с учётом длины месяца
и високосного года, день недели Monday=1..Sunday=7. Неверный ввод не записывается.
SetTime/SetDate возвращают NotInit, Error, Timeout или OK. Выход из INIT и
синхронизация выполняются после записи; ошибка синхронизации не отменяет запись.
Отдельные SetTime и SetDate — две операции, не атомарная установка пары.

SetTime_hex/SetDate_hex сохранены для packed BCD: {0x14,0x25,0x00} означает
14:25:00. Проверяются BCD-цифры и затем календарные диапазоны. to_bcd(0..99)
возвращает packed BCD, за пределами — 0xFF.

GetTime дополнительно читает DR, освобождая shadow lock. GetDate — самостоятельное
чтение даты. Для одной согласованной пары используют GetDateTime. Чтения допустимы
после успешного SetUp; не прерывайте пару другим чтением RTC из callback.

---

## 4. WakeUp Timer

~~~cpp
clock.EnableWakeUpTimer(0); // 1 секунда при ck_spre=1 Гц.
clock.EnableWakeUpTimer(4); // 5 секунд, прежняя настройка заменяется.
clock.DisableWakeUpTimer();
~~~

| WUCKSEL / clock_div | Источник |
|---|---|
| 000 | RTCCLK / 16 |
| 001 | RTCCLK / 8 |
| 010 | RTCCLK / 4 |
| 011 | RTCCLK / 2 |
| 100 | ck_spre, 16-битный счётчик |
| 110 | ck_spre, к counter добавляется 65536 |

counter=0..65535. Обычно период равен (counter+1)/f_WUT; код 110 добавляет 65536
тиков. API принимает перечисленные коды; 101/111 отвергаются.

Rearm отключает WUTE/WUTIE, ждёт WUTWF, записывает параметры, очищает старый WUTF
и включает событие/IRQ. При таймауте или ошибке регистрации событие остаётся
выключенным. EnableWakeUpTimer возвращает SysStatus; DisableWakeUpTimer не ждёт.
OnWakeup вызывается после очистки флага.

---

## 5. Будильники

~~~cpp
RTC_cl::AlarmConfig alarm{
    .hour = {14, RTC_cl::Alarm_Masks::Care},
    .min  = {25, RTC_cl::Alarm_Masks::Care},
    .sec  = {0,  RTC_cl::Alarm_Masks::Care}
};
clock.EnableAlarm_A(ENABLE, alarm); // Ежедневно в 14:25:00.
clock.EnableAlarm_A(DISABLE);
// Alarm B имеет такой же интерфейс:
clock.EnableAlarm_B(ENABLE, alarm);
~~~

Поля Ignore не сравниваются, Care используют десятичные значения. day — число
месяца 1..31, не день недели. Часы 0..23, минуты/секунды 0..59. Подсекундная цель
sub_sec=0..32767, MASKSS/sub_sec_msk=0..15: 0 игнорирует все подсекунды,
1 сравнивает младший бит, 15 — все 15 бит. Фактическую цель выбирают с учётом PRER.
Конфигурация по умолчанию игнорирует все поля: это не выключенный будильник.

Перед настройкой отключаются соответствующие ALRxE/ALRxIE. Ожидание ALRxWF
ограничено 1000 мс; старый флаг очищается до повторного включения. Используются
OnAlarmA/OnAlarmB. Ошибка регистрации не включает будильник.

---

## 6. Timestamp

~~~cpp
clock.EnableTimestamp(ENABLE, RTC_cl::TimestampEdge::Rising);
// В IRQ вызывается OnTimestamp(const Timestamp&).
// Для опроса вместо IRQ:
clock.IRQ_en(RTC_cl::IRQ_s::TS, DISABLE);
RTC_cl::Timestamp stamp;
if (clock.GetTimestamp(stamp) == SysStatus::OK) {
    uint8_t captured_hour = stamp.time.hour();
    bool lost_edge = stamp.overflow;
}
clock.EnableTimestamp(DISABLE);
~~~

Драйвер включает TSE/TSIE, выбирает фронт, очищает старые TSF/TSOVF и обслуживает
событие. Пин RTC_TS (обычно PC13), его доступность в корпусе и альтернативную
маршрутизацию готовит приложение. Это не GPIO EXTI; внутренние TAMP-события не
настраиваются. Не смешивайте опрос и IRQ-потребление одного захвата.

TSTR/TSDR/TSSSR читаются **до** очистки TSF: очистка удаляет аппаратный захват.
IRQ очищает прочитанный флаг и передаёт снимок в callback без постоянного
дополнительного буфера объекта; повторно читать TSTR/TSDR из callback не нужно.
Timestamp не содержит года: date.year()=0. sub_seconds — значение нисходящего
счётчика, а не миллисекунды. overflow сообщает о TSOVF. GetTimestamp возвращает
Busy, если нового TSF нет; захват и overflow очищаются после чтения.

---

## 7. IRQ и освобождение

G0 использует RTC_TAMP_IRQn и прямую внутреннюю EXTI19. На F4/F7 используются
RTC_Alarm_IRQn / EXTI17, RTC_WKUP_IRQn / EXTI22, TAMP_STAMP_IRQn / EXTI21.
На F4/F7 все три вектора RTC должны иметь одинаковый приоритет вытеснения:
они обслуживают один аппаратный RTC и не должны прерывать друг друга.
Первое включение источника регистрирует объект независимо от текущего NVIC.
При заполненном реестре возвращается InitError (из IRQ_en) или Error из функции
настройки события. Повторное включение не добавляет дубликат.

IRQ читает один снимок CR и событий, обрабатывает только включённые источники,
очищает свои флаги и вызывает callbacks. F4/F7 rc_w0 записываются с сохранением
других флагов, включая TAMP; G0 использует SCR. Ожиданий в IRQ нет.

IRQ_en(source, DISABLE) меняет источник, сохраняя регистрацию до DeInit.
Конфигурация выполняется только из main; callback не должен вызывать IRQ_en,
SetUp/DeInit или изменять реестр. Для паузы всей группы IRQ приложение использует
NVIC самостоятельно; общие линии требуют учёта остальных потребителей.

DeInit выключает собственные Alarm/WUT/Timestamp/calibration output, удаляет
только свои регистрации и сохраняет NVIC для оставшихся обработчиков. EXTI21
на F4/F7 сохраняется, если на TAMP_STAMP остаются другие обработчики. G0 TAMP
использует отдельную внутреннюю EXTI21. Календарь, источник, калибровка и backup
данные сохраняются; повторный SetUp возможен после DeInit.

---

## 8. Калибровка и вывод частоты

~~~cpp
SysStatus cal = clock.SetCalibration(6, false); // Около -5.72 ppm при LSE 32768 Гц.
// Busy означает RECALPF=1: повторить позже из main, ожидания внутри нет.
clock.EnableCOE(true, RTC_cl::CalOut::Out_512); // RTCCLK/64, 512 Гц при 32768 Гц.
clock.EnableCOE(true, RTC_cl::CalOut::Out_1Hz); // ck_spre.
clock.EnableCOE(false);
~~~

CALM=0..511 подавляет импульсы в 32-секундном окне; CALP добавляет 512 импульсов.
При номинальном LSE один шаг примерно 0.954 ppm, диапазон примерно ±488 ppm.
CALR записывается без CALW8/CALW16. Для точности сначала измеряют генератор.
Пин RTC_CALIB и его маршрутизацию готовит приложение. Названия 512/1 Hz верны
при соответствующих частотах входа и календаря.

WriteProtection сохранён для низкоуровневого доступа: ключи 0xCA,0x53 снимают
WPR, 0xFF возвращает защиту. Обычные методы выполняют это самостоятельно.

---

## 9. Миграция и проверка

Статические вызовы заменяются на объект. Аргументы-функции WUT/Alarm удалены:
события переопределяются в наследнике. Функции настройки теперь возвращают статус.
SetUp сохраняет параметры источника/делителей; объект не перенастраивается без
DeInit. Автоматического сброса времени при каждом запуске нет.

Исходники сверены вручную. Сборки и аппаратные тесты в этом проходе не запускались.
Переход источника, LSE startup, retained calendar, Alarm/WUT/Timestamp и совместное
использование TAMP требуют проверки на используемом МК.

---

## 10. Примеры использования

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
// Альтернатива для номинального LSI 32000 Гц (не второй вызов SetUp):
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
uint8_t hours = time.hour();
uint8_t minutes = time.min();
uint8_t seconds = time.sec();
uint8_t day = date.day();
```

```cpp
clock.EnableWakeUpTimer(0); // 1 секунда при ck_spre=1 Гц.
clock.EnableWakeUpTimer(4); // 5 секунд, прежняя настройка заменяется.
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
// Alarm B имеет такой же интерфейс:
clock.EnableAlarm_B(ENABLE, alarm);
```

```cpp
clock.EnableTimestamp(ENABLE, RTC_cl::TimestampEdge::Rising);
// В IRQ вызывается OnTimestamp(const Timestamp&).
// Для опроса вместо IRQ:
clock.IRQ_en(RTC_cl::IRQ_s::TS, DISABLE);
RTC_cl::Timestamp stamp;
if (clock.GetTimestamp(stamp) == SysStatus::OK) {
    uint8_t captured_hour = stamp.time.hour();
    bool lost_edge = stamp.overflow;
}
clock.EnableTimestamp(DISABLE);
```

Примеры продолжают настройку одного Clock. Проверяйте каждый статус; опрос Timestamp и его потребление из IRQ — альтернативные варианты.

---

## 11. Угловые случаи

Смена источника сбрасывает backup domain с потерей времени и backup-регистров. Сохранённый тот же источник не переустанавливает делители. LSI требует учёта реальной частоты. Не прерывайте чтение shadow-пары другим чтением RTC. Timestamp не хранит год; ссылка callback действительна только во время вызова.

---

## См. также

- [RTC: shadow, initialization and alarm API](https://github.com/STMicroelectronics/stm32f4xx-hal-driver/blob/master/Src/stm32f4xx_hal_rtc.c).
- [G0 Timestamp: чтение до сброса TSF](https://github.com/STMicroelectronics/stm32g0xx-hal-driver/blob/master/Src/stm32g0xx_hal_rtc_ex.c).
- [RM0390: WUCKSEL и MASKSS](https://www.st.com/resource/en/reference_manual/dm00135183.pdf).

- [System.md](System.md)
- [IRQ_Registry.md](IRQ_Registry.md)
- [EXTI.md](EXTI.md)
