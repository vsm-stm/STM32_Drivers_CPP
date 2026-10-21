# Modbus / ModbusRTU_Slave — Detailed Description

*[English](#english) · [Русский](#русский)*

---

## English

## Table of Contents

1. [Why This Is Needed and Scope](#1-why-this-is-needed-and-scope)
2. [Constant Initialization and Constructor Diagnostics](#2-constant-initialization-and-constructor-diagnostics)
3. [Initialization and Line Format](#3-initialization-and-line-format)
4. [Receive and Main-Loop Parsing](#4-receive-and-main-loop-parsing)
5. [Reply Buffer, CRC and Physical Completion](#5-reply-buffer-crc-and-physical-completion)
6. [Usage Examples](#6-usage-examples)
7. [Edge Cases](#7-edge-cases)
8. [See Also](#see-also)

---

## 1. Why This Is Needed and Scope

`ModbusRTU_Slave` is the **transport layer** of a Modbus RTU slave on RS-485. It provides:

- a USART with its own TX and RX DMA;
- two fixed 256-byte buffers (receive and reply);
- CRC16 checking on receive and CRC16 appending on send;
- control of the RS-485 driver-enable (DE) pin;
- a call to **your** parser function for every packet with a correct CRC.

It is **not** a ready Modbus register map. Your parser decides everything protocol-related: slave address, function code, request length, broadcast handling and whether to reply.

> **Packet boundaries are detected by UART IDLE, not by strict RTU t1.5/t3.5 timing.**
>
> - IDLE fires after about one character time of silence. A pause inside a frame that RTU allows (up to 1.5 characters) can split one request into two packets.
> - There is no separate t3.5 delay before the reply.
> - No hardware timer is used for these intervals.
>
> Use this transport where the master's timing is known to be compatible. Full RTU timing conformance is not claimed.

---

## 2. Constant Initialization and Constructor Diagnostics

Declare the object at file scope with `constinit` (or as `static constinit` inside a function). The constructor takes:

- `USART::_N::instance`;
- baud rate;
- named TX and RX pins of that USART;
- the DE pin (any GPIO);
- **both** DMA routes — TX and RX.

| Family | DMA argument |
| --- | --- |
| F4/F7 | Named route such as `USART::_3::DMA::TX_DMA1_Stream3` — it already encodes controller, stream, channel and direction. |
| G0 | Physical channel address such as `DMA1_Channel2_BASE`; the USART supplies the DMAMUX request IDs. |

What happens with a wrong argument:

| Declaration | Result |
| --- | --- |
| `constinit` object | **Compilation error** with the reason text in the message. |
| Object without `constinit` | Compiles; at start-up the constructor stops in `System::DebugTrap(reason)`. |

Every check calls `STM32_CONFIG_ERROR("reason")`: at compile time it produces a GCC error containing the reason, at run time it calls `System::DebugTrap(text)`. `constinit` is required by the usage contract but not enforced. `SetUp()` still checks hardware state and occupied resources. Details: [Initialization.md](Initialization.md).

---

## 3. Initialization and Line Format

Order of calls:

1. `SetRxParse(function)` — install your parser **before** reception starts.
2. `SetUp(format)` — configures USART + DMA, then the DE pin (driven low before it becomes an output).
3. Call `Rx_Process()` in the main loop.

A repeated `SetUp()` fails until `DeInit()` is called.

| FrameFormat | Line | Meaning |
| --- | --- | --- |
| `E8_1` | 8E1 | 8 data bits, even parity, 1 stop bit — standard RTU |
| `O8_1` | 8O1 | 8 data bits, odd parity, 1 stop bit |
| `N8_2` | 8N2 | 8 data bits, no parity, 2 stop bits — standard RTU without parity |
| `N8_1` | 8N1 | Legacy default, kept for existing devices |

The format is fixed at `SetUp()`. For 8E1/8O1 the USART is configured for a 9-bit word whose last bit is parity, so the payload stays 8 bits and DMA keeps working with bytes.

---

## 4. Receive and Main-Loop Parsing

Call `Rx_Process()` often from `main`. Each call:

1. If reception is not running (and no packet is waiting), arms RX DMA into the receive buffer.
2. If a packet is ready:
   - packets of 3 bytes or less, or a missing parser → discarded;
   - CRC is checked; on match, your parser is called with the data **without** the 2 CRC bytes;
   - on mismatch the packet is silently dropped;
   - the "packet ready" flag is cleared, so the next call re-arms reception.

After an RX error the next `Rx_Process()` starts reception again.

**One receive buffer, no queue.** A ready packet stays in the buffer until `Rx_Process()` handles it; reception restarts only on the next call. While DMA is stopped or your parser is running, incoming bytes can be lost. The application is responsible for processing quickly enough and for the master's pauses.

**Parser rules:**

- runs in `main` context, not in an interrupt;
- `data` points into the receive buffer — do not keep the pointer after returning, it will be overwritten by the next packet.

**Packet end detection:**

- F4/F7: IDLE requests a DMA stop; the packet is published from the DMA RX interrupt once the stop is confirmed.
- G0: the last DMA request must be serviced before the IDLE stop, as described for USART.
- Packets with line errors are not passed to the parser.

Count and cache handover details: [UART.md](UART.md).

---

## 5. Reply Buffer, CRC and Physical Completion

**Sending a reply:**

1. `GetTxBufferPtr()` reserves the reply buffer. `nullptr` means busy or not set up.
2. Fill up to **254** bytes (2 more are reserved for CRC).
3. `SendData(len)` appends the CRC (low byte first, byte-wise — no unaligned 16-bit access), raises DE and starts TX DMA.

Do not modify the buffer after `SendData()` returned `OK`.

| `SendData()` result | Meaning |
| --- | --- |
| `OK` | Transmission **started** (not finished on the wire). |
| `NotInit` | `SetUp()` was not done. |
| `Busy` | A reply is still being sent. |
| `Error` | `len` is 0 or more than 254, or the buffer was not reserved. |

If the DMA start fails, DE is lowered immediately and the prepared reply is kept, so you can retry `SendData()` or call `CancelTxBuffer()`.

**When does DE go low?** Not when DMA finishes, but when the USART reports **TC** (transmission complete: the last stop bit has left the pin):

- before starting, the old TC flag is cleared, then the TC interrupt is enabled;
- the TC handler lowers DE, frees the buffer and disables only the TC interrupt source;
- a DMA TX error also lowers DE and frees the buffer;
- an RX error does **not** affect a reply being sent.

Handlers never wait and never change the IRQ registry or NVIC.

**`CancelTxBuffer()`** — give up a reserved but unsent reply:

| Result | When |
| --- | --- |
| `OK` | Buffer was reserved or free — now free. |
| `Busy` | A reply is on the wire; cannot cancel. |
| `NotInit` | Before `SetUp()`. |

**F7 cache:** both internal buffers are 32-byte aligned and occupy their own cache lines; no control field shares a line with them. Cache maintenance is done by USART through System.

**`DeInit()`** (from `main`): releases USART/DMA, lowers DE and frees the reply buffer. A reply on the wire is cut off — wait for TC first (`GetTxBufferPtr()` returns non-null again) if the reply must be complete. GPIO and clocks follow the USART rules.

---

## 6. Usage Examples

Setup and main loop (F4, USART3):

```cpp
#include "Modbus-slave.hpp"

constinit ModbusRTU_Slave bus(USART::_3::instance, 19200,
    USART::_3::TX::PD8, USART::_3::RX::PD9,
    PIN(GPIOD_BASE, 2),                    // DE
    USART::_3::DMA::TX_DMA1_Stream3,
    USART::_3::DMA::RX_DMA1_Stream1);

void parse_request(uint8_t* data, uint32_t len)
{
    // main context; len already excludes CRC.
    // Check address / function / broadcast here.
    // Do not keep `data` after returning.
}

int main()
{
    // ... successful System/RCC initialization ...
    bus.SetRxParse(parse_request);
    if (bus.SetUp(ModbusRTU_Slave::FrameFormat::E8_1) != SysInitStatus::InitOK) {
        // handle the error
    }
    for (;;) {
        bus.Rx_Process();
    }
}
```

Sending a reply (usually from inside the parser):

```cpp
uint8_t* reply = bus.GetTxBufferPtr();
if (reply) {
    reply[0] = 1;   // slave address; the content is up to the application
    reply[1] = 3;
    reply[2] = 0;
    SysStatus sent = bus.SendData(3);   // appends CRC and starts DMA
    // OK = started, not finished on the wire
}

// Changed your mind before SendData():
SysStatus cancelled = bus.CancelTxBuffer();
```

---

## 7. Edge Cases

| Situation | Behaviour |
| --- | --- |
| Master pauses inside a frame | Strict t1.5/t3.5 is not implemented; the frame may be split by IDLE and fail CRC. |
| Reply longer than 254 bytes | `SendData()` returns `Error`. |
| Parser keeps the `data` pointer | Not allowed — the buffer is reused by the next reception. |
| Cancel or edit a reply on the wire | Not possible — `CancelTxBuffer()` returns `Busy`. |
| `DeInit()` during a reply | The reply is cut off; wait for TC first if it must be complete. |

---

## See Also

- [Modbus over Serial Line, §2.5.1](https://www.modbus.org/file/secure/modbusoverserial.pdf) — RTU framing, parity and timing. The IDLE-based boundary described above is a deliberate design decision.
- [Initialization.md](Initialization.md)
- [UART.md](UART.md)
- [DMA.md](DMA.md)
- [GPIO.md](GPIO.md)

---

## Русский

## Содержание

1. [Зачем это нужно и область ответственности](#1-зачем-это-нужно-и-область-ответственности)
2. [Постоянная инициализация и диагностика конструктора](#2-постоянная-инициализация-и-диагностика-конструктора)
3. [Инициализация](#3-инициализация)
4. [Приём](#4-приём)
5. [Ответ](#5-ответ)
6. [Примеры использования](#6-примеры-использования)
7. [Угловые случаи](#7-угловые-случаи)
8. [См. также](#см-также)

---

## 1. Зачем это нужно и область ответственности

`ModbusRTU_Slave` — **транспортный уровень** ведомого Modbus RTU на RS-485. Он даёт:

- USART с собственными DMA на передачу и приём;
- два фиксированных буфера по 256 байт (приём и ответ);
- проверку CRC16 при приёме и добавление CRC16 при отправке;
- управление выводом разрешения передатчика RS-485 (DE);
- вызов **вашей** функции-парсера для каждого пакета с правильным CRC.

Это **не** готовая таблица регистров Modbus. Всё, что касается протокола, решает ваш парсер: адрес устройства, код функции, длина запроса, широковещательные запросы и нужен ли ответ.

> **Граница пакета определяется по UART IDLE, а не по строгим интервалам RTU t1.5/t3.5.**
>
> - IDLE срабатывает примерно через один символ тишины. Пауза внутри кадра, допустимая по RTU (до 1,5 символа), может разрезать запрос на два пакета.
> - Отдельной паузы t3.5 перед ответом нет.
> - Аппаратный таймер для этих интервалов не используется.
>
> Применяйте транспорт там, где тайминги ведущего заведомо совместимы. Полное соответствие RTU по таймингам не заявляется.

---

## 2. Постоянная инициализация и диагностика конструктора

Объявляйте объект глобально с `constinit` (или как `static constinit` внутри функции). Конструктор принимает:

- `USART::_N::instance`;
- скорость;
- именованные выводы TX и RX этого USART;
- вывод DE (любой GPIO);
- **оба** DMA-маршрута — TX и RX.

| Семейство | Аргумент DMA |
| --- | --- |
| F4/F7 | Именованный маршрут, например `USART::_3::DMA::TX_DMA1_Stream3` — в нём уже закодированы контроллер, поток, канал и направление. |
| G0 | Адрес физического канала, например `DMA1_Channel2_BASE`; номера запросов DMAMUX подставляет USART. |

Что происходит при неверном аргументе:

| Объявление | Результат |
| --- | --- |
| Объект с `constinit` | **Ошибка компиляции**, текст причины — в сообщении. |
| Объект без `constinit` | Собирается; при старте конструктор останавливается в `System::DebugTrap(причина)`. |

Каждая проверка вызывает `STM32_CONFIG_ERROR("причина")`: при компиляции это ошибка GCC с текстом причины, во время работы — вызов `System::DebugTrap(text)`. `constinit` обязателен по правилам использования, но не проверяется. `SetUp()` по-прежнему проверяет состояние аппаратуры и занятость ресурсов. Подробнее: [Initialization.md](Initialization.md).

---

## 3. Инициализация

```cpp
// F4: маршрут DMA однозначно задаёт направление, контроллер и поток.
constinit ModbusRTU_Slave bus(USART::_3::instance, 19200,
    USART::_3::TX::PD8, USART::_3::RX::PD9,
    PIN(GPIOD_BASE, 2),                    // DE
    USART::_3::DMA::TX_DMA1_Stream3,
    USART::_3::DMA::RX_DMA1_Stream1);

void parse_request(uint8_t* data, uint32_t len)
{
    // Контекст main, len уже без CRC. Проверить адрес/функцию/broadcast.
    // Нельзя сохранять data для использования после следующего приёма.
}

// После System/RCC:
bus.SetRxParse(parse_request);
SysInitStatus init = bus.SetUp(ModbusRTU_Slave::FrameFormat::E8_1);
// Основной цикл при успешном init:
// for (;;) { bus.Rx_Process(); }
```

Порядок вызовов:

1. `SetRxParse(функция)` — установить парсер **до** начала приёма.
2. `SetUp(формат)` — настраивает USART и DMA, затем вывод DE (сначала в нём выставляется 0, потом он становится выходом).
3. Вызывать `Rx_Process()` в основном цикле.

На G0 в тех же позициях конструктора передаются физические каналы DMA; направление и номер запроса выбирает USART. Оба DMA обязательны. Неверные привязки диагностирует конструктор, как у USART. Повторный `SetUp()` — ошибка до вызова `DeInit()`.

| FrameFormat | Линия | Значение |
| --- | --- | --- |
| `E8_1` | 8E1 | 8 бит данных, чётность even, 1 стоп-бит — стандарт RTU |
| `O8_1` | 8O1 | 8 бит данных, чётность odd, 1 стоп-бит |
| `N8_2` | 8N2 | 8 бит данных без чётности, 2 стоп-бита — стандарт RTU без чётности |
| `N8_1` | 8N1 | Старый формат по умолчанию, оставлен для существующих устройств |

Формат задаётся только в `SetUp()`. Для 8E1/8O1 USART настраивается на 9-битное слово, последний бит которого — чётность; полезных бит остаётся 8, и DMA продолжает работать с байтами.

---

## 4. Приём

Вызывайте `Rx_Process()` из `main` часто. Каждый вызов:

1. Если приём не запущен (и нет ожидающего пакета) — запускает RX DMA в буфер приёма.
2. Если пакет готов:
   - пакет из 3 байт и меньше или отсутствие парсера → пакет отбрасывается;
   - проверяется CRC; при совпадении вызывается ваш парсер с данными **без** 2 байт CRC;
   - при несовпадении пакет молча отбрасывается;
   - признак «пакет готов» сбрасывается, и следующий вызов снова запускает приём.

После ошибки приёма следующий `Rx_Process()` запускает приём заново.

**Один буфер приёма, без очереди.** Готовый пакет лежит в буфере, пока его не обработает `Rx_Process()`; приём возобновляется только на следующем вызове. Пока DMA остановлен или работает парсер, входящие байты могут потеряться. Успевать обрабатывать пакеты и обеспечивать паузы ведущего — задача приложения.

**Правила для парсера:**

- вызывается в контексте `main`, не из прерывания;
- `data` указывает в буфер приёма — не сохраняйте указатель после возврата, следующий пакет его перезапишет.

**Определение конца пакета:**

- F4/F7: IDLE запрашивает остановку DMA; пакет публикуется из прерывания DMA RX после подтверждения остановки.
- G0: последний запрос DMA должен быть обслужен до остановки по IDLE, как описано для USART.
- Пакеты с ошибками линии в парсер не передаются.

Подробности о счётчике и кеше: [UART.md](UART.md).

---

## 5. Ответ

```cpp
uint8_t* reply = bus.GetTxBufferPtr();
if (reply) {
    reply[0] = 1; // Адрес; содержимое ответа определяет приложение.
    reply[1] = 3;
    reply[2] = 0;
    SysStatus sent = bus.SendData(3); // Добавит CRC и запустит DMA.
    // OK = запуск, а не завершение на проводе.
}
// Если передумали отправлять подготовленный ответ:
SysStatus cancelled = bus.CancelTxBuffer();
```

**Отправка ответа:**

1. `GetTxBufferPtr()` резервирует буфер ответа. `nullptr` — занято или не выполнен `SetUp()`.
2. Заполните до **254** байт (ещё 2 зарезервированы под CRC).
3. `SendData(len)` дописывает CRC (младший байт первым, побайтно — без невыровненного 16-битного доступа), поднимает DE и запускает TX DMA.

После `OK` от `SendData()` буфер менять нельзя.

| Результат `SendData()` | Значение |
| --- | --- |
| `OK` | Передача **запущена** (ещё не завершена на линии). |
| `NotInit` | Не выполнен `SetUp()`. |
| `Busy` | Предыдущий ответ ещё передаётся. |
| `Error` | `len` равен 0 или больше 254, либо буфер не был зарезервирован. |

Если запуск DMA не удался, DE сразу опускается, а подготовленный ответ сохраняется — можно повторить `SendData()` или вызвать `CancelTxBuffer()`.

**Когда опускается DE?** Не по окончании DMA, а по флагу USART **TC** (передача завершена: последний стоп-бит покинул вывод):

- перед запуском старый флаг TC сбрасывается, затем разрешается прерывание TC;
- обработчик TC опускает DE, освобождает буфер и выключает только источник прерывания TC;
- ошибка DMA TX тоже опускает DE и освобождает буфер;
- ошибка приёма передачу ответа **не** прерывает.

Обработчики ничего не ждут и не меняют реестр прерываний или NVIC.

**`CancelTxBuffer()`** — отказаться от зарезервированного, но не отправленного ответа:

| Результат | Когда |
| --- | --- |
| `OK` | Буфер был зарезервирован или свободен — теперь свободен. |
| `Busy` | Ответ на линии; отменить нельзя. |
| `NotInit` | До `SetUp()`. |

**Кеш F7:** оба внутренних буфера выровнены по 32 байтам и занимают собственные кеш-линии; управляющие поля с ними линию не делят. Обслуживание кеша выполняет USART через System.

**`DeInit()`** (из `main`): освобождает USART/DMA, опускает DE и освобождает буфер ответа. Передаваемый ответ обрывается — если он должен дойти целиком, сначала дождитесь TC (`GetTxBufferPtr()` снова возвращает не-`nullptr`). GPIO и тактирование освобождаются по правилам USART.

---

## 6. Примеры использования

Настройка и основной цикл (F4, USART3):

```cpp
#include "Modbus-slave.hpp"

constinit ModbusRTU_Slave bus(USART::_3::instance, 19200,
    USART::_3::TX::PD8, USART::_3::RX::PD9,
    PIN(GPIOD_BASE, 2),                    // DE
    USART::_3::DMA::TX_DMA1_Stream3,
    USART::_3::DMA::RX_DMA1_Stream1);

void parse_request(uint8_t* data, uint32_t len)
{
    // Контекст main; len уже без CRC.
    // Здесь проверить адрес / функцию / broadcast.
    // Не сохранять data после возврата.
}

int main()
{
    // ... успешная инициализация System/RCC ...
    bus.SetRxParse(parse_request);
    if (bus.SetUp(ModbusRTU_Slave::FrameFormat::E8_1) != SysInitStatus::InitOK) {
        // обработка ошибки
    }
    for (;;) {
        bus.Rx_Process();
    }
}
```

Отправка ответа (обычно изнутри парсера):

```cpp
uint8_t* reply = bus.GetTxBufferPtr();
if (reply) {
    reply[0] = 1;   // адрес; содержимое определяет приложение
    reply[1] = 3;
    reply[2] = 0;
    SysStatus sent = bus.SendData(3);   // добавит CRC и запустит DMA
    // OK = запуск, а не завершение на линии
}

// Передумали до SendData():
SysStatus cancelled = bus.CancelTxBuffer();
```

---

## 7. Угловые случаи

| Ситуация | Поведение |
| --- | --- |
| Ведущий делает паузу внутри кадра | Строгие t1.5/t3.5 не реализованы; кадр может разрезаться по IDLE и не пройти CRC. |
| Ответ длиннее 254 байт | `SendData()` вернёт `Error`. |
| Парсер сохраняет указатель `data` | Нельзя — буфер переиспользуется следующим приёмом. |
| Отменить или изменить ответ на линии | Невозможно — `CancelTxBuffer()` вернёт `Busy`. |
| `DeInit()` во время ответа | Ответ обрывается; если он нужен целиком, сначала дождитесь TC. |

---

## См. также

- [Modbus over Serial Line, §2.5.1](https://www.modbus.org/file/secure/modbusoverserial.pdf) — кадры RTU, чётность и интервалы. Граница по IDLE, описанная выше, — осознанное проектное решение.
- [Initialization.md](Initialization.md)
- [UART.md](UART.md)
- [DMA.md](DMA.md)
- [GPIO.md](GPIO.md)
