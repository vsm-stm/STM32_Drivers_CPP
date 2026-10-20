# DAC / DAC_Module — Detailed Description

*[English](#english) · [Русский](#русский)*

---

## English

## Table of Contents

1. [Why This Is Needed](#1-why-this-is-needed)
2. [Constant Initialization and Constructor Diagnostics](#2-constant-initialization-and-constructor-diagnostics)
3. [Configuration and API](#3-configuration-and-api)
4. [Setup and writes](#4-setup-and-writes)
5. [Devices and channel lifetime](#5-devices-and-channel-lifetime)
6. [Usage Examples](#6-usage-examples)
7. [Edge Cases](#7-edge-cases)
8. [See Also](#see-also)

---

## 1. Why This Is Needed

One DAC_Module binds one available DAC channel. It configures its analog output and accepts 12-bit right-aligned values; the second channel can be used independently.

---

## 2. Constant Initialization and Constructor Diagnostics

Declare fixed peripheral objects with `constinit` at file scope or as `static constinit`. DAC takes DAC_Module::_1::instance and a channel; it has no DMA binding.

For peripheral binding checks, each failing condition calls non-constexpr `System::DebugTrap` with its cause. A `constinit` initializer rejects that call during compilation; without constinit, a failing runtime construction stops. The keyword is mandatory by the usage contract, not enforced by the constructor. SetUp still checks hardware state and occupied resources. See [Initialization.md](Initialization.md) for constructor variants and migration.

---

## 3. Configuration and API

DAC_USE_ATOMIC in dac.hpp defaults to 0 (ordinary control-register RMW).
Set 1 for ST ATOMIC_* updates to CR/MCR and clock-enable fields; rebuild DAC
and its users. Data-register writes and W1C flag clearing remain direct.
Individual atomic operations do not serialize the whole channel configuration.

Enable the module in project.json:

~~~json
"drivers": ["DAC"]
~~~

DAC_Module writes 12-bit right-aligned codes directly to one output channel.
System/GPIO are core dependencies; this API does not require DMA or IRQ registration.

| Method | Result |
|---|---|
| SetUp() | SysInitStatus::InitOK, or InitError |
| SetValue(uint32_t) | SysStatus::OK, Error, or NotInit |
| DeInit() | Disables this channel; returns void |

The constructor binds DAC1 and CHANNEL::CH1 (PA4) or CH2 (PA5). The CMSIS DAC
alias refers to the same instance. DACx/channel are fixed in protected members;
the volatile data-register pointer is private. No hardware is accessed by the
constructor. Copy and move are disabled.

~~~cpp
#include "dac.hpp"

constinit DAC_Module output(DAC_Module::_1::instance, DAC_Module::CHANNEL::CH1); // PA4

// After System/RCC initialization:
if (output.SetUp() == SysInitStatus::InitOK) {
    SysStatus result = output.SetValue(2048);
    // Check result == SysStatus::OK.
}
// When finished:
output.DeInit();
~~~

---

## 4. Setup and writes

SetUp rejects an invalid instance/channel and repeated initialization before
touching hardware. After DeInit the same object can be initialized again.

Setup enables the DAC clock, configures only the selected output pin as analog,
disables the selected channel's old trigger/waveform/DMA/IRQ settings, selects
buffered direct output, writes zero and enables that channel. On G0 the selected
MCR mode is set to normal external buffered output. The other channel is preserved.
The family-specific control fields are described in the
[ST G0 LL DAC header](https://raw.githubusercontent.com/STMicroelectronics/stm32g0xx-hal-driver/master/Inc/stm32g0xx_ll_dac.h)
and [ST F4 LL DAC header](https://raw.githubusercontent.com/STMicroelectronics/stm32f4xx-hal-driver/master/Inc/stm32f4xx_ll_dac.h).

SetValue checks initialization first:

- Before successful SetUp or after DeInit: NotInit, no register write.
- Value above 4095: Error, previous code is unchanged.
- Value 0..4095: one volatile register write, then OK.

No extra initialization flag is stored: the non-null register pointer marks a
successful setup. Error does not invalidate an already initialized channel.

SetUp/SetValue confirm configuration/register writes. They do not wait for
analog startup or settling. Observe the selected device's datasheet limits for
startup, settling, output buffer and load. Direct DHR writes update DOR without
a trigger when TEN is clear; see
[ST RM0401, DAC channel enable and conversion](https://www.st.com/resource/en/reference_manual/rm0401-stm32f410-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).

---

## 5. Devices and channel lifetime

Supported families are F4/F7/G0 where the selected CMSIS header exposes DAC1.
Selecting this module on a device without DAC produces a compile-time diagnostic.

F410 supports only CH1; CH2 is rejected during constinit construction. Other supported F4 DAC devices use
the CMSIS DAC_CHANNEL2_SUPPORT capability. F7/G0 devices with DAC expose both
channel slots. Check that the chosen output is bonded in the selected package.
F410's single channel is documented in
[ST RM0401](https://www.st.com/resource/en/reference_manual/rm0401-stm32f410-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).

Keep one object per hardware channel. Two different objects may control CH1 and
CH2 independently; the driver does not allocate a separate ownership registry.
Serialize setup/deinitialization and writes that could overlap the same channel.
Do not modify DAC configuration externally while this object owns the channel.

DeInit disables only the selected channel and clears its register pointer.
Repeated DeInit is harmless. DAC/GPIO clocks and the analog pin configuration
are retained. A disabled output is not a promise to hold the previous voltage.
The destructor is passive: explicitly call DeInit before destroying an active object.

---

## 6. Usage Examples

Independent example; check device/package availability and every returned status.

```cpp
#include "dac.hpp"
constinit DAC_Module output(DAC_Module::_1::instance, DAC_Module::CHANNEL::CH1);
// In main, after successful System/RCC initialization:
if (output.SetUp() == SysInitStatus::InitOK) {
    SysStatus written = output.SetValue(2048);
    // Check written before relying on the output.
}
```

---

## 7. Edge Cases

Values above 4095 return Error. Repeated SetUp requires DeInit; call configuration from main. GPIO/clock resources shared with another channel must be preserved.

---

## See Also

- [Initialization.md](Initialization.md)
- [GPIO.md](GPIO.md)
- [System.md](System.md)

---

## Русский

## Содержание

1. [Зачем это нужно](#1-зачем-это-нужно)
2. [Постоянная инициализация и диагностика конструктора](#2-постоянная-инициализация-и-диагностика-конструктора)
3. [Атомарные операции](#3-атомарные-операции)
4. [Подключение и API](#4-подключение-и-api)
5. [Настройка и запись](#5-настройка-и-запись)
6. [Поддержка МК и время жизни](#6-поддержка-мк-и-время-жизни)
7. [Примеры использования](#7-примеры-использования)
8. [Угловые случаи](#8-угловые-случаи)
9. [См. также](#см-также)

---

## 1. Зачем это нужно

Один DAC_Module закрепляет один доступный канал DAC, настраивает аналоговый выход и принимает 12-битные значения с правым выравниванием. Второй канал может использоваться независимо.

---

## 2. Постоянная инициализация и диагностика конструктора

Объявляйте объекты периферии глобально с `constinit` либо как `static constinit`. DAC принимает DAC_Module::_1::instance и канал; привязки DMA нет.

При проверке привязок периферии каждое неверное условие вызывает неконстантный `System::DebugTrap` с причиной. При `constinit` такой вызов отклоняется при сборке; без него неверное runtime-создание останавливает программу. Ключевое слово обязательно по контракту, конструктор не проверяет его наличие. Проверки состояния аппаратуры и занятых ресурсов остаются в SetUp. Варианты конструкторов и миграция: [Initialization.md](Initialization.md).

---

## 3. Атомарные операции

DAC_USE_ATOMIC в dac.hpp: 0 по умолчанию — обычные изменения управляющих
полей, 1 — ATOMIC_* ST для CR/MCR и включения тактирования. Пересоберите DAC
и приложение после изменения. Запись данных и очистка W1C остаются прямыми.
Настройка не заменяет согласование целой операции с каналом.

---

## 4. Подключение и API

Модуль включается в project.json:

~~~json
"drivers": ["DAC"]
~~~

DAC_Module записывает 12-битный код с правым выравниванием в один выходной канал.
System/GPIO входят в ядро библиотеки; этому API не требуется подключение DMA/IRQ.

| Метод | Результат |
|---|---|
| SetUp() | SysInitStatus::InitOK либо InitError |
| SetValue(uint32_t) | SysStatus::OK, Error либо NotInit |
| DeInit() | Отключает выбранный канал, возвращает void |

Конструктор закрепляет DAC1 и канал: CH1 — PA4, CH2 — PA5. CMSIS-имя DAC —
алиас того же экземпляра. DACx и канал неизменяемы и находятся в protected,
служебный volatile-указатель регистра — в private. Конструктор не обращается
к аппаратуре. Копирование и перемещение запрещены.

~~~cpp
#include "dac.hpp"

constinit DAC_Module output(DAC_Module::_1::instance, DAC_Module::CHANNEL::CH1); // PA4

// После настройки System/RCC:
if (output.SetUp() == SysInitStatus::InitOK) {
    SysStatus result = output.SetValue(2048);
    // Проверить result == SysStatus::OK.
}
// После окончания использования:
output.DeInit();
~~~

---

## 5. Настройка и запись

SetUp до обращения к регистрам проверяет экземпляр, канал и отсутствие повторной
инициализации. Повторный SetUp — InitError; после DeInit настройка снова разрешена.

Включается тактирование DAC, только выбранный пин переводится в analog.
Для выбранного канала сбрасываются прежние настройки триггера, генерации
сигналов, DMA и IRQ, включается прямой вывод с выходным буфером, записывается
ноль и разрешается канал. На G0 в MCR задаётся обычный режим с выводом на пин
и включённым буфером. Второй канал сохраняется. Поля описаны в
[ST G0 LL DAC](https://raw.githubusercontent.com/STMicroelectronics/stm32g0xx-hal-driver/master/Inc/stm32g0xx_ll_dac.h)
и [ST F4 LL DAC](https://raw.githubusercontent.com/STMicroelectronics/stm32f4xx-hal-driver/master/Inc/stm32f4xx_ll_dac.h).

SetValue сначала проверяет инициализацию:

- До успешного SetUp и после DeInit: NotInit, записи нет.
- Значение больше 4095: Error, предыдущий код сохраняется.
- Значение 0..4095: одна volatile-запись, затем OK.

Отдельного флага инициализации нет: признаком служит ненулевой указатель регистра.
Ошибка значения не отменяет выполненную инициализацию.

Успех настройки/записи не означает, что аналоговый выход уже установился.
Время запуска и установления, допустимую нагрузку и ограничения буфера
нужно учитывать по datasheet выбранного МК. При TEN=0 запись DHR обновляет DOR
без триггера; см.
[ST RM0401, включение и преобразование DAC](https://www.st.com/resource/en/reference_manual/rm0401-stm32f410-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).

---

## 6. Поддержка МК и время жизни

Поддерживаются F4/F7/G0, если в выбранном CMSIS есть DAC1.
При подключении модуля на МК без DAC выдаётся понятная ошибка компиляции.

У F410 есть только CH1: CH2 отклоняется при constinit-конструировании.
Для остальных F4 доступность CH2 проверяется по DAC_CHANNEL2_SUPPORT из CMSIS.
У поддерживаемых F7/G0 с DAC доступны оба канала. Наличие вывода в конкретном
корпусе проверяет пользователь. Единственный канал F410 описан в
[ST RM0401](https://www.st.com/resource/en/reference_manual/rm0401-stm32f410-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).

На аппаратный канал создаётся один объект. CH1 и CH2 могут обслуживаться разными
объектами; дополнительного реестра владельцев нет. Настройка, отключение и запись
не должны конфликтовать при одновременных вызовах. Внешний код не должен менять
конфигурацию занятого этим объектом канала.

DeInit отключает только выбранный канал и обнуляет служебный указатель.
Повторный вызов безопасен. Общие тактовые сигналы DAC/GPIO и analog-настройка
пина сохраняются. Отключение не гарантирует удержания прежнего напряжения.
Деструктор не меняет аппаратуру: перед уничтожением активного объекта нужен DeInit.

---

## 7. Примеры использования

Отдельный пример; проверьте наличие блока/пинов в корпусе и каждый возвращаемый статус.

```cpp
#include "dac.hpp"
constinit DAC_Module output(DAC_Module::_1::instance, DAC_Module::CHANNEL::CH1);
// In main, after successful System/RCC initialization:
if (output.SetUp() == SysInitStatus::InitOK) {
    SysStatus written = output.SetValue(2048);
    // Check written before relying on the output.
}
```

---

## 8. Угловые случаи

Значения выше 4095 возвращают Error. Повторный SetUp требует DeInit; настройка вызывается из main. Общие GPIO/такты другого канала должны сохраняться.

---

## См. также

- [Initialization.md](Initialization.md)
- [GPIO.md](GPIO.md)
- [System.md](System.md)
