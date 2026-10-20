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

ModbusRTU_Slave is an RS-485 transport using USART with embedded TX/RX DMA,
two fixed 256-byte buffers, CRC16 and an application parser. It is not a register
table/function implementation: the parser checks address, function, length,
broadcast policy and whether to reply.

**Packet boundaries use UART IDLE, not strict RTU t1.5/t3.5.** IDLE is roughly
one character of silence; an allowed RTU intra-frame pause can split reception.
There is no separate t3.5 wait before a reply. Use only where the peer/pause
behavior is ensured by the application; full RTU timing conformance is not
claimed. No hardware TIM for these intervals is added.

---

## 2. Constant Initialization and Constructor Diagnostics

Declare fixed peripheral objects with `constinit` at file scope or as `static constinit`. Use USART::_N::instance, its named TX/RX pins and both DMA routes, plus the application DE GPIO. G0 takes numeric DMAx_Channely_BASE selections; USART fixes request sources.

For peripheral binding checks, each failing condition uses `STM32_CONFIG_ERROR("reason")`. The reason is a template argument of the non-constexpr error function, so GCC includes it in the diagnostic itself. The macro uses `std::is_constant_evaluated()` to select that path; at runtime it calls `System::DebugTrap(text)` directly, without calling the template. A `constinit` initializer rejects that call during compilation; without constinit, a failing runtime construction stops. The keyword is mandatory by the usage contract, not enforced by the constructor. SetUp still checks hardware state and occupied resources. See [Initialization.md](Initialization.md) for constructor variants and migration.

---

## 3. Initialization and Line Format

Declare with constinit and USART::_N::instance. Both TX and RX DMA are required;
F4/F7 named routes encode stream/direction, while G0 takes physical DMA channel
base addresses and USART supplies request IDs. Wrong bindings are diagnosed by
the USART/Modbus constructors. Repeat SetUp requires DeInit. SetRxParse installs
the parser before starting reception.

| FrameFormat | Line | Payload |
|---|---|---|
| E8_1 | 8E1 | Eight bits, even parity, one stop bit |
| O8_1 | 8O1 | Eight bits, odd parity, one stop bit |
| N8_2 | 8N2 | Eight bits, no parity, two stop bits |
| N8_1 | 8N1 | Legacy default for compatibility |

The format is fixed by SetUp. For 8E1/8O1 the hardware word is nine bits with
parity in the final bit; DMA data remains byte-oriented.

---

## 4. Receive and Main-Loop Parsing

Call Rx_Process frequently from main. A ready packet is held until consumed;
there is no queue of unlimited requests. It checks length/CRC, calls the parser
with CRC excluded and rearms reception. Parser pointers are temporary and must
not be retained across the next receive. An invalid packet is discarded.

F4/F7 IDLE requests a stop; DMA RX IRQ publishes after confirmed stop. G0 requires
the last RX request serviced before the normal IDLE stop, as documented for
USART. Error packets are not used. See UART.md for count/cache handover.

---

## 5. Reply Buffer, CRC and Physical Completion

GetTxBufferPtr reserves the buffer; nullptr means busy or not initialized.
It has room for 254 payload bytes plus two CRC bytes. SendData adds CRC, low
byte first, through byte accesses (no unaligned uint16_t access). Do not modify
the buffer after start. SendData is nonblocking: OK means started, not on-wire
completion. NotInit/Busy/Error cover missing setup, active TX and invalid length
or unreserved buffer.

A failed start lowers DE and keeps the prepared response for retry/cancel.
Successful start clears old TC before enabling TCIE. Physical UART TC lowers DE,
frees the TX buffer and disables only TCIE. DMA TX errors also lower DE/free it;
an independent RX error does not disable an active response transmitter.
Handlers do not wait or change registry/NVIC.

CancelTxBuffer returns Busy during sending, OK for releasing a prepared/free
buffer and NotInit before SetUp. F7 internal buffers have their own aligned
32-byte lines; USART/System perform cache maintenance without sharing control
fields with a DMA cache line.

DeInit in main releases USART/DMA resources, lowers DE and frees buffers. Wait
for normal TC first if truncating a live response is not acceptable.

---

## 6. Usage Examples

```cpp
constinit ModbusRTU_Slave bus(USART::_3::instance, 19200, USART::_3::TX::PD8,
    USART::_3::RX::PD9, PIN(GPIOD_BASE, 2),
    USART::_3::DMA::TX_DMA1_Stream3,
    USART::_3::DMA::RX_DMA1_Stream1);

void parse_request(uint8_t* data, uint32_t len) {
}
bus.SetRxParse(parse_request);
SysInitStatus init = bus.SetUp(ModbusRTU_Slave::FrameFormat::E8_1);
// for (;;) { bus.Rx_Process(); }
```

```cpp
uint8_t* reply = bus.GetTxBufferPtr();
if (reply) {
    reply[0] = 1;
    reply[1] = 3;
    reply[2] = 0;
    SysStatus sent = bus.SendData(3);
}
SysStatus cancelled = bus.CancelTxBuffer();
```

The examples operate on one bus. Check init/sent/cancelled; asynchronous OK does not confirm TC.

---

## 7. Edge Cases

Strict RTU t1.5/t3.5 timing is not implemented. Response payload capacity is 254 bytes. The parser runs in main and owns protocol validation, not permanent RX storage. Active TX cannot be cancelled/modified. Wait for TC before teardown when a complete response is required.

---

## See Also

Исходники/изменения просмотрены вручную; сборка и тесты в этом проходе не запускались.
[Modbus over Serial Line, §2.5.1](https://www.modbus.org/file/secure/modbusoverserial.pdf)
описывает RTU framing, parity и интервалы. Ограничения IDLE выше сохранены явно
по согласованному решению.

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

ModbusRTU_Slave использует USART с собственными TX/RX DMA, два фиксированных
буфера по 256 байт, CRC16 и пользовательский parser. Это транспорт, а не готовая
реализация таблицы регистров/функций Modbus. Parser проверяет адрес, код функции,
длину запроса, broadcast и допустимость ответа.

**Граница приёма сохранена по UART IDLE. Строгие интервалы RTU t1.5/t3.5 не
реализованы.** IDLE наступает примерно через один символ тишины; допустимая RTU
пауза внутри кадра может разделить его на два пакета. Отдельного ожидания t3.5
перед ответом нет. Этот транспорт применим, когда поведение второй стороны и
паузы гарантированы приложением; полное соответствие RTU timing не заявляется.
Аппаратный TIM для этих интервалов в этой правке не добавлялся.

---

## 2. Постоянная инициализация и диагностика конструктора

Объявляйте объекты периферии глобально с `constinit` либо как `static constinit`. Используйте USART::_N::instance, его пины TX/RX и оба DMA-маршрута, а также GPIO DE приложения. На G0 передаются числовые DMAx_Channely_BASE, источник запросов закрепляет USART.

При проверке привязок периферии каждое неверное условие использует `STM32_CONFIG_ERROR("причина")`. Причина передаётся как параметр шаблона неконстантной функции, поэтому GCC включает её в само сообщение ошибки. Макрос выбирает путь через `std::is_constant_evaluated()`; во время выполнения напрямую вызывает `System::DebugTrap(text)`, без вызова шаблонной функции. При `constinit` такой вызов отклоняется при сборке; без него неверное runtime-создание останавливает программу. Ключевое слово обязательно по контракту, конструктор не проверяет его наличие. Проверки состояния аппаратуры и занятых ресурсов остаются в SetUp. Варианты конструкторов и миграция: [Initialization.md](Initialization.md).

---

## 3. Инициализация

~~~cpp
// F4: запрос однозначно содержит направление, контроллер и stream.
constinit ModbusRTU_Slave bus(USART::_3::instance, 19200, USART::_3::TX::PD8,
    USART::_3::RX::PD9, PIN(GPIOD_BASE, 2),
    USART::_3::DMA::TX_DMA1_Stream3,
    USART::_3::DMA::RX_DMA1_Stream1);

void parse_request(uint8_t* data, uint32_t len) {
    // Main context, len уже без CRC. Проверить адрес/функцию/broadcast.
    // Нельзя сохранять data для использования после повторного приёма.
}

// После System/RCC:
bus.SetRxParse(parse_request);
SysInitStatus init = bus.SetUp(ModbusRTU_Slave::FrameFormat::E8_1);
// Основной цикл при успешном init:
// for (;;) { bus.Rx_Process(); }
~~~

На G0 в тех же позициях конструктора передаются физические DMA-каналы;
направления/request выбирает USART. Оба DMA обязательны. Неверные привязки
диагностируются конструктором, как у USART. Повторный SetUp — ошибка до DeInit.

Формат задаётся только при SetUp:

| FrameFormat | Линия | Полезные данные |
|---|---|---|
| E8_1 | 8E1 | 8 бит + even parity + 1 stop |
| O8_1 | 8O1 | 8 бит + odd parity + 1 stop |
| N8_2 | 8N2 | 8 бит без parity + 2 stop |
| N8_1 | 8N1 | Старый формат, по умолчанию для совместимости |

Для 8E1/8O1 USART аппаратно настраивается на 9-битное слово, в котором последний
бит занимает parity. Это сохраняет 8 полезных бит и Byte DMA. Формат 8N1 оставлен
для существующих устройств; обычный RTU без parity использует два stop-бита.

---

## 4. Приём

Rx_Process вызывается из main часто и последовательно. Если приём не запущен,
метод вооружает RX DMA. При готовом пакете он проверяет минимальный размер и CRC,
вызывает parser только для правильного CRC, затем явно сбрасывает признак готовности.
Неверный CRC/короткий пакет/отсутствие parser отбрасываются. После ошибки RX
следующий Rx_Process может запустить приём заново.

Один RX-буфер удерживается до обработки; непрерывного приёма в фоне и очереди нет.
Следующий приём запускается очередным Rx_Process. Пока DMA остановлен или parser
занят, новые данные могут потеряться. Полный буфер не доказывает наличие RTU t3.5.
Приложение обеспечивает время обработки/паузы; добавления второго буфера не было.

На F4/F7 нормальный IDLE запрашивает остановку, пакет публикуется в DMA RX IRQ
после подтверждения остановки. На G0 действует согласованное условие USART:
последний DMA-запрос должен быть обслужен до остановки по IDLE. Ошибочные пакеты
не используются. Подробности — [UART.md](UART.md).

---

## 5. Ответ

~~~cpp
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
~~~

GetTxBufferPtr резервирует буфер; nullptr означает занятость или отсутствие SetUp.
Доступно 254 байта данных + 2 байта CRC. После запуска нельзя менять содержимое.
CRC читается и записывается отдельными байтами, low byte первым: невыровненных
uint16_t-доступов нет.

SendData не ждёт DMA/TC. Возвращает NotInit до SetUp, Busy при активной отправке,
Error для неверной длины/отсутствия подготовленного буфера. При ошибке старта
DE сразу опускается, подготовленный ответ сохраняется для повторной отправки или
CancelTxBuffer. При успешном старте TCIE включается после очистки старого TC;
физический TC снимает DE, освобождает буфер и выключает только источник TCIE.

Ошибка DMA TX также снимает DE и освобождает буфер; независимая ошибка RX
не отключает передатчик во время ответа. Обработчики не меняют реестр/NVIC и
не ждут завершения аппаратуры. CancelTxBuffer возвращает Busy до окончания
отправки, OK при освобождении подготовленного/свободного буфера, NotInit до SetUp.

На F7 оба внутренних буфера выровнены по 32 байтам и занимают собственные
кеш-линии. Операции обслуживания кеша выполняет USART через System. Отдельные
поля управления не делят кеш-линию с буфером.

DeInit вызывается из main: освобождает ресурсы USART/DMA, опускает DE и возвращает
буфер в свободное состояние. Дождитесь штатного TC, если нельзя обрывать текущий
ответ. GPIO/тактовые сигналы освобождаются по обычному контракту USART.

---

## 6. Примеры использования

```cpp
// F4: запрос однозначно содержит направление, контроллер и stream.
constinit ModbusRTU_Slave bus(USART::_3::instance, 19200, USART::_3::TX::PD8,
    USART::_3::RX::PD9, PIN(GPIOD_BASE, 2),
    USART::_3::DMA::TX_DMA1_Stream3,
    USART::_3::DMA::RX_DMA1_Stream1);

void parse_request(uint8_t* data, uint32_t len) {
    // Main context, len уже без CRC. Проверить адрес/функцию/broadcast.
    // Нельзя сохранять data для использования после повторного приёма.
}

// После System/RCC:
bus.SetRxParse(parse_request);
SysInitStatus init = bus.SetUp(ModbusRTU_Slave::FrameFormat::E8_1);
// Основной цикл при успешном init:
// for (;;) { bus.Rx_Process(); }
```

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

Это последовательность работы одного bus. Проверяйте init/sent/cancelled, не считайте OK асинхронной отправки подтверждением TC.

---

## 7. Угловые случаи

Строгие RTU-паузы t1.5/t3.5 не реализованы: граница пакета — IDLE. Доступно 254 байта ответа до CRC. Parser вызывается из main, проверяет протокол и не сохраняет RX-указатель. Нельзя отменять/менять активный TX-буфер; дождитесь TC, если нельзя обрывать ответ перед DeInit.

---

## См. также

Исходники/изменения просмотрены вручную; сборка и тесты в этом проходе не запускались.
[Modbus over Serial Line, §2.5.1](https://www.modbus.org/file/secure/modbusoverserial.pdf)
описывает RTU framing, parity и интервалы. Ограничения IDLE выше сохранены явно
по согласованному решению.

- [Initialization.md](Initialization.md)
- [UART.md](UART.md)
- [DMA.md](DMA.md)
- [GPIO.md](GPIO.md)
