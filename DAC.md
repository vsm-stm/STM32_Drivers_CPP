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

`DAC_Module` drives one DAC output channel: you write a number 0…4095, the pin outputs the matching voltage.

- One object = one channel (CH1 on PA4 or CH2 on PA5).
- Values are 12-bit, right-aligned.
- The second channel can be driven by another object, independently.

---

## 2. Constant Initialization and Constructor Diagnostics

Declare fixed peripheral objects at file scope with `constinit` (or as `static constinit` inside a function). `DAC_Module` takes `DAC_Module::_1::instance` and a channel; there is no DMA binding.

What happens with a wrong argument (for example CH2 on F410, which has only CH1):

| Declaration | Result |
| --- | --- |
| `constinit` object | **Compilation error** with the reason text in the message. |
| Object without `constinit` | Compiles; at start-up the constructor stops in `System::DebugTrap(reason)`. |

How it works: every check calls `STM32_CONFIG_ERROR("reason")`. During compile-time evaluation it calls a deliberately non-`constexpr` function whose template argument is the reason text, so GCC prints that text. At run time it calls `System::DebugTrap(text)` directly.

`constinit` is required by the usage contract but not enforced by the constructor. `SetUp()` still checks hardware state and repeated initialization. Constructor variants and migration: [Initialization.md](Initialization.md).

---

## 3. Configuration and API

Enable the module in `project.json`:

```json
"drivers": ["DAC"]
```

Dependencies: only the core (System, GPIO). No DMA and no IRQ registration.

| Method | Result |
| --- | --- |
| `SetUp()` | `SysInitStatus::InitOK` or `InitError` |
| `SetValue(uint32_t)` | `SysStatus::OK`, `Error` or `NotInit` |
| `DeInit()` | Disables this channel; returns `void` |

**Constructor:**

- Binds DAC1 and `CHANNEL::CH1` (PA4) or `CHANNEL::CH2` (PA5). The CMSIS name `DAC` is an alias of the same block.
- Does not touch hardware.
- The instance and channel are fixed; copy and move are disabled.

```cpp
#include "dac.hpp"

constinit DAC_Module output(DAC_Module::_1::instance, DAC_Module::CHANNEL::CH1); // PA4

// After System/RCC initialization:
if (output.SetUp() == SysInitStatus::InitOK) {
    SysStatus result = output.SetValue(2048);   // about half of Vref
    // Check result == SysStatus::OK.
}
// When finished:
output.DeInit();
```

**`DAC_USE_ATOMIC`** (in `dac.hpp`, default `0`):

- `0` — ordinary read-modify-write of control registers.
- `1` — ST `ATOMIC_*` macros for CR/MCR and the clock-enable bit.

Data writes and write-1-to-clear flag clearing are always direct. Atomic single operations do not make the whole channel setup atomic. Rebuild DAC and its users after changing the setting.

---

## 4. Setup and writes

**`SetUp()`** first checks the instance, the channel and that the object is not already set up — before touching any register. A repeated `SetUp()` returns `InitError`; after `DeInit()` the object can be set up again.

Then it:

1. enables the DAC clock;
2. puts only the selected pin into analog mode;
3. clears this channel's old trigger, waveform, DMA and IRQ settings;
4. selects direct output with the output buffer on (on G0 — MCR "normal mode, external pin, buffered");
5. writes zero and enables the channel.

The other channel is not touched. Register fields: [ST G0 LL DAC](https://raw.githubusercontent.com/STMicroelectronics/stm32g0xx-hal-driver/master/Inc/stm32g0xx_ll_dac.h), [ST F4 LL DAC](https://raw.githubusercontent.com/STMicroelectronics/stm32f4xx-hal-driver/master/Inc/stm32f4xx_ll_dac.h).

**`SetValue(value)`** checks initialization first:

| Situation | Result |
| --- | --- |
| Before a successful `SetUp()` or after `DeInit()` | `NotInit`, nothing is written |
| `value > 4095` | `Error`, the previous code stays on the output |
| `0 ≤ value ≤ 4095` | one volatile register write, then `OK` |

An `Error` does not undo a successful setup.

**State is stored minimally.** The object keeps only the pointer to the data register; a non-null pointer means "set up". `SetUp()`/`DeInit()` compute the DAC block address from the instance when needed — there is no separate `DACx` field or init flag.

**No analog waiting.** `SetUp()`/`SetValue()` only confirm register writes. They do not wait for analog start-up or settling. Take start-up time, settling time, buffer and load limits from your device's datasheet. With the trigger disabled (TEN = 0), a write to DHR updates the output register immediately — see [ST RM0401, DAC channel enable and conversion](https://www.st.com/resource/en/reference_manual/rm0401-stm32f410-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).

---

## 5. Devices and channel lifetime

**Supported devices:** F4/F7/G0 whose CMSIS header defines DAC1. Selecting the module on a device without a DAC is a compile-time error.

| Device | Channels |
| --- | --- |
| F410 | CH1 only; CH2 is rejected at `constinit` construction ([ST RM0401](https://www.st.com/resource/en/reference_manual/rm0401-stm32f410-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)) |
| Other F4 | CH2 availability follows CMSIS `DAC_CHANNEL2_SUPPORT` |
| F7 / G0 with DAC | CH1 and CH2 |

Check that the pin is actually bonded in your package.

**Ownership rules:**

- One object per hardware channel. CH1 and CH2 may belong to different objects; there is no ownership registry.
- Serialize setup, teardown and writes that could overlap on the same channel.
- Do not change the DAC configuration of an owned channel from other code.

**`DeInit()`:**

- disables only the selected channel and clears the register pointer;
- is harmless to call repeatedly;
- keeps the DAC/GPIO clocks and the pin's analog mode;
- does **not** promise to hold the previous voltage.

The destructor does nothing to the hardware — call `DeInit()` before destroying an active object.

---

## 6. Usage Examples

A complete example. Check that the DAC and pin exist on your device/package and check every returned status.

```cpp
#include "dac.hpp"

constinit DAC_Module output(DAC_Module::_1::instance, DAC_Module::CHANNEL::CH1);

int main()
{
    // ... successful System/RCC initialization ...
    if (output.SetUp() == SysInitStatus::InitOK) {
        SysStatus written = output.SetValue(2048);
        // Check written before relying on the output.
    }
}
```

---

## 7. Edge Cases

| Situation | Behaviour |
| --- | --- |
| Value above 4095 | `Error`; the output keeps the previous code. |
| `SetUp()` called twice | `InitError`; call `DeInit()` first. |
| Setup from an interrupt | Not intended — configure from `main`. |
| Both channels in use | Shared GPIO/DAC clocks must stay enabled; `DeInit()` of one channel keeps them. |

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

`DAC_Module` управляет одним выходным каналом ЦАП: вы записываете число 0…4095, на выводе появляется соответствующее напряжение.

- Один объект = один канал (CH1 на PA4 или CH2 на PA5).
- Значения 12-битные, с правым выравниванием.
- Второй канал может независимо обслуживаться другим объектом.

---

## 2. Постоянная инициализация и диагностика конструктора

Объявляйте постоянные объекты периферии глобально с `constinit` (или как `static constinit` внутри функции). `DAC_Module` принимает `DAC_Module::_1::instance` и канал; привязки DMA нет.

Что происходит при неверном аргументе (например, CH2 на F410, где есть только CH1):

| Объявление | Результат |
| --- | --- |
| Объект с `constinit` | **Ошибка компиляции**, текст причины — в сообщении. |
| Объект без `constinit` | Собирается; при старте конструктор останавливается в `System::DebugTrap(причина)`. |

Как это устроено: каждая проверка вызывает `STM32_CONFIG_ERROR("причина")`. При вычислении на этапе компиляции макрос вызывает намеренно не-`constexpr` функцию, у которой текст причины — параметр шаблона, поэтому GCC печатает этот текст. Во время выполнения напрямую вызывается `System::DebugTrap(text)`.

`constinit` обязателен по правилам использования, но конструктор его наличие не проверяет. `SetUp()` по-прежнему проверяет состояние аппаратуры и повторную инициализацию. Варианты конструкторов и миграция: [Initialization.md](Initialization.md).

---

## 3. Атомарные операции

**`DAC_USE_ATOMIC`** (в `dac.hpp`, по умолчанию `0`):

- `0` — обычное «прочитать-изменить-записать» управляющих регистров.
- `1` — макросы ST `ATOMIC_*` для CR/MCR и бита включения тактирования.

Запись данных и сброс флагов записью единицы (write-1-to-clear) всегда прямые. Атомарность отдельных операций не делает атомарной всю настройку канала. После изменения пересоберите DAC и приложение.

---

## 4. Подключение и API

Включите модуль в `project.json`:

```json
"drivers": ["DAC"]
```

Зависимости: только ядро (System, GPIO). DMA и регистрация IRQ не нужны.

| Метод | Результат |
| --- | --- |
| `SetUp()` | `SysInitStatus::InitOK` или `InitError` |
| `SetValue(uint32_t)` | `SysStatus::OK`, `Error` или `NotInit` |
| `DeInit()` | Отключает этот канал; возвращает `void` |

**Конструктор:**

- Закрепляет DAC1 и `CHANNEL::CH1` (PA4) или `CHANNEL::CH2` (PA5). CMSIS-имя `DAC` — псевдоним того же блока.
- Не обращается к аппаратуре.
- Экземпляр и канал неизменяемы; копирование и перемещение запрещены.

```cpp
#include "dac.hpp"

constinit DAC_Module output(DAC_Module::_1::instance, DAC_Module::CHANNEL::CH1); // PA4

// После настройки System/RCC:
if (output.SetUp() == SysInitStatus::InitOK) {
    SysStatus result = output.SetValue(2048);   // примерно половина Vref
    // Проверить result == SysStatus::OK.
}
// После окончания использования:
output.DeInit();
```

---

## 5. Настройка и запись

**`SetUp()`** до обращения к регистрам проверяет экземпляр, канал и то, что объект ещё не настроен. Повторный `SetUp()` возвращает `InitError`; после `DeInit()` настройка снова разрешена.

Затем метод:

1. включает тактирование DAC;
2. переводит в аналоговый режим только выбранный вывод;
3. сбрасывает у этого канала прежние настройки триггера, генерации сигналов, DMA и IRQ;
4. включает прямой вывод с выходным буфером (на G0 — режим MCR «обычный, на внешний вывод, с буфером»);
5. записывает ноль и разрешает канал.

Второй канал не затрагивается. Поля регистров: [ST G0 LL DAC](https://raw.githubusercontent.com/STMicroelectronics/stm32g0xx-hal-driver/master/Inc/stm32g0xx_ll_dac.h), [ST F4 LL DAC](https://raw.githubusercontent.com/STMicroelectronics/stm32f4xx-hal-driver/master/Inc/stm32f4xx_ll_dac.h).

**`SetValue(value)`** сначала проверяет инициализацию:

| Ситуация | Результат |
| --- | --- |
| До успешного `SetUp()` или после `DeInit()` | `NotInit`, записи нет |
| `value > 4095` | `Error`, на выходе остаётся прежний код |
| `0 ≤ value ≤ 4095` | одна volatile-запись в регистр, затем `OK` |

`Error` не отменяет выполненную настройку.

**Состояние хранится минимально.** Объект хранит только указатель на регистр данных; ненулевой указатель означает «настроен». `SetUp()`/`DeInit()` вычисляют адрес блока DAC из экземпляра — отдельного поля `DACx` и флага инициализации нет.

**Аналоговую часть драйвер не ждёт.** `SetUp()`/`SetValue()` подтверждают только запись в регистры, но не запуск и установление аналогового выхода. Время запуска и установления, ограничения буфера и нагрузки берите из datasheet вашего МК. При выключенном триггере (TEN = 0) запись в DHR сразу обновляет выходной регистр — см. [ST RM0401, включение и преобразование DAC](https://www.st.com/resource/en/reference_manual/rm0401-stm32f410-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).

---

## 6. Поддержка МК и время жизни

**Поддерживаемые МК:** F4/F7/G0, в CMSIS-заголовке которых есть DAC1. Подключение модуля на МК без ЦАП — ошибка компиляции.

| МК | Каналы |
| --- | --- |
| F410 | Только CH1; CH2 отклоняется при `constinit`-конструировании ([ST RM0401](https://www.st.com/resource/en/reference_manual/rm0401-stm32f410-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)) |
| Остальные F4 | Наличие CH2 определяется по `DAC_CHANNEL2_SUPPORT` из CMSIS |
| F7 / G0 с DAC | CH1 и CH2 |

Наличие вывода в конкретном корпусе проверяет пользователь.

**Правила владения:**

- Один объект на аппаратный канал. CH1 и CH2 могут принадлежать разным объектам; реестра владельцев нет.
- Настройка, отключение и запись в один канал не должны пересекаться по времени.
- Внешний код не должен менять конфигурацию занятого канала.

**`DeInit()`:**

- отключает только выбранный канал и обнуляет указатель регистра;
- безопасен при повторном вызове;
- сохраняет тактирование DAC/GPIO и аналоговый режим вывода;
- **не гарантирует** удержание прежнего напряжения.

Деструктор аппаратуру не трогает — перед уничтожением активного объекта вызовите `DeInit()`.

---

## 7. Примеры использования

Полный пример. Проверьте наличие ЦАП и вывода в вашем МК/корпусе и каждый возвращаемый статус.

```cpp
#include "dac.hpp"

constinit DAC_Module output(DAC_Module::_1::instance, DAC_Module::CHANNEL::CH1);

int main()
{
    // ... успешная инициализация System/RCC ...
    if (output.SetUp() == SysInitStatus::InitOK) {
        SysStatus written = output.SetValue(2048);
        // Проверьте written, прежде чем полагаться на выход.
    }
}
```

---

## 8. Угловые случаи

| Ситуация | Поведение |
| --- | --- |
| Значение больше 4095 | `Error`; на выходе остаётся прежний код. |
| `SetUp()` дважды | `InitError`; сначала вызовите `DeInit()`. |
| Настройка из прерывания | Не предусмотрена — настраивайте из `main`. |
| Используются оба канала | Общие такты GPIO/DAC должны оставаться включёнными; `DeInit()` одного канала их сохраняет. |

---

## См. также

- [Initialization.md](Initialization.md)
- [GPIO.md](GPIO.md)
- [System.md](System.md)
