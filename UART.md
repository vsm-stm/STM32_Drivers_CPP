# UART / USART — Detailed Description

*[English](#english) · [Русский](#русский)*

---

## English

## Table of Contents

1. [Why This Is Needed](#1-why-this-is-needed)
2. [Constant Initialization and Constructor Diagnostics](#2-constant-initialization-and-constructor-diagnostics)
3. [Initialization and ownership](#3-initialization-and-ownership)
4. [Kernel clock (F7/G0)](#4-kernel-clock-f7g0)
5. [Transfer contract](#5-transfer-contract)
6. [One packet at a time](#6-one-packet-at-a-time)
7. [DMA and cache](#7-dma-and-cache)
8. [Errors, FIFO and callbacks](#8-errors-fifo-and-callbacks)
9. [Usage Examples](#9-usage-examples)
10. [Edge Cases](#10-edge-cases)
11. [See Also](#see-also)

---

## 1. Why This Is Needed

USART provides independent byte-oriented TX/RX, blocking/IRQ/DMA operations and packet completion on IDLE. Constructor bindings fix pins, DMA and the supported kernel-clock source; separate handlers service UART and each DMA direction.

---

## 2. Constant Initialization and Constructor Diagnostics

Declare fixed peripheral objects with `constinit` at file scope or as `static constinit`. Use the provided _N::instance, named pins and optional _N::DMA routes. G0 takes numeric DMAx_Channely_BASE selections; the peripheral fixes the request source.

For peripheral binding checks, each failing condition uses `STM32_CONFIG_ERROR("reason")`. The reason is a template argument of the non-constexpr error function, so GCC includes it in the diagnostic itself. The macro uses `std::is_constant_evaluated()` to select that path; at runtime it calls `System::DebugTrap(text)` directly, without calling the template. A `constinit` initializer rejects that call during compilation; without constinit, a failing runtime construction stops. The keyword is mandatory by the usage contract, not enforced by the constructor. SetUp still checks hardware state and occupied resources. See [Initialization.md](Initialization.md) for constructor variants and migration.

---

## 3. Initialization and ownership

One USART object controls one UART. Call System/RCC initialization first.
The constructor fixes optional DMA TX/RX and the kernel clock; PCLK is the default. SetUp() applies
that source, oversampling x16 and a selected 8N1/8E1/8O1/8N2 frame (8N1 by default); G0 PRESC is /1.
Baud is rounded to the nearest representable divider:
BRR = (kernel_clock + baud/2) / baud, with 16 <= BRR <= 65535
and baud <= kernel_clock/16.
Zero baud and rates outside this range return InitError; no arbitrary 9600 minimum.

Named pins use USART::_N::TX/RX. PIN{} leaves a direction unconfigured.
Tables follow peripheral/GPIO availability in the CMSIS device header:
F4/F7 UART/USART 1..8 where present, G0 USART1..6 where present.
LPUART and F4 UART9/10 are outside these tables.
A GPIO port being present does not guarantee that its pin is bonded in the selected package.

SetUp() registers this object once on the UART IRQ and checks the result.
Repeated SetUp() is InitError until DeInit(). Initialization and ownership changes
run in main. Do not register this object manually.

IRQ_en() changes peripheral source bits only. Completion disables its sources
without unregistering anything. DeInit() removes this object's registrations and
preserves other handlers on shared vectors. The object must outlive registration;
its destructor does not call DeInit().

UART_USE_ATOMIC in uart.hpp selects control-bit and UART RCC clock-field updates:

- 0 (default): ordinary SET_BIT/CLEAR_BIT/MODIFY_REG.
- 1: ST ATOMIC_SET_BIT/ATOMIC_CLEAR_BIT/ATOMIC_MODIFY_REG. F4/F7 use LDREX/STREX
  with retry; G0 briefly masks interrupts per update and restores the previous PRIMASK.

Change the definition in uart.hpp, then rebuild both drivers and application.
The value must match in every translation unit using UART; defining it only in
main.cpp does not configure uart.cpp. Selection uses the preprocessor and adds no
run-time branch. Atomic mode protects individual register updates. Transfer calls,
buffer/state access and setup/deinitialization follow the caller's serialization contract.
Status reads, SR/ICR flag acknowledgement and whole-register initialization remain
direct accesses with their hardware semantics.

---

## 4. Kernel clock (F7/G0)

The optional last constructor argument is `USART::ClockSource`:

| Source | Frequency used for BRR |
|---|---|
| `PCLK` (default) | This instance's APB clock |
| `SYSCLK` | System clock before the AHB divider; derived from HCLK and HPRE |
| `HSI` | 16 MHz, before G0 HSIDIV |
| `LSE` | 32768 Hz |

```cpp
// F7/G0; the caller must enable HSI and wait for HSIRDY before SetUp().
constinit USART link(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7,
           {}, {}, USART::ClockSource::HSI);
SysInitStatus status = link.SetUp();
```

`SetUp()` checks that the selected HSI/LSE is enabled and ready. It neither starts
an oscillator nor waits for one. An unavailable/not-ready source returns InitError
before UART configuration. On G0, instances without an RCC USARTxSEL field accept
PCLK only; passing another source also returns InitError. F4 retains its original
PCLK clocking. DMA request descriptions follow the pins on every supported family;
ClockSource follows both DMA requests on F7/G0.

The source is immutable, including across DeInit()/SetUp(). Keep the oscillator
running and RCC frequencies/dividers unchanged while using the UART. SetBaud()
uses the same source, checks readiness and returns Error if it is no longer ready;
it does not change the source. With x16 oversampling, LSE limits baud to 2048.

---

## 5. Transfer contract

| Mode | Methods | Length and completion |
|---|---|---|
| Blocking | Send / Receive | Nonzero uint32_t length; timeout covers the entire call |
| IRQ | Send_IRQ / Receive_IRQ | 1..65535 bytes; RX ends at capacity or IDLE |
| DMA | Send_DMA / Receive_DMA | 1..65535 bytes; constructor-bound DMA configured by SetUp; RX ends at capacity or IDLE |

Before initialization: NotInit. Null pointer, zero length, unsupported asynchronous
length or an unconfigured direction: Error. An operation already active in the
same direction: Busy. TX and RX may run independently.
A new operation may be started after Error or Timeout.

Send() OK means all bytes were queued into UART; IRQ/DMA TX OK means the source
buffer can be reused. The last stop bit may still be on the wire.
Use the UART TC event for physical completion, for example before releasing RS-485 DE.
SetBaud() validates the new divider, writes BRR with UE=0 and stores BaudRate.
It restores only the previous UE bit using UART_USE_ATOMIC, preserving other current CR1 bits.
It returns NotInit, Error or Busy when the change cannot be performed.

Before SetBaud(), the caller must also ensure that the last stop bit has finished;
the Busy check covers the software operation only. Waiting for physical completion
is the caller's responsibility; the driver has no additional TX tracking field.
When the IRQ handler clears TC, use the OnTC() notification to observe completion.

Blocking timeouts require a running System tick; see [System.md](System.md).
Blocking Receive() reads exactly the requested length and does not publish the
asynchronous packet-ready flag.

---

## 6. One packet at a time

Receive_IRQ/DMA starts a fresh packet and clears the previous ready/count/overflow state.
IDLE ends the packet. On F4/F7 DMA RX follows this order without waiting in IRQ:

1. Disable UART RX interrupt sources and DMAR requests.
2. Request a DMA stop and return from the USART handler; RX remains Busy.
3. DMA RX TC confirms the stop; read the remaining count and invalidate written bytes.
4. Retain any pending UART/FIFO tail that fits the buffer.
5. Publish the byte count, OK and the ready flag.

On F4, the SR/DR sequence clearing IDLE occurs after DMA stops, and a pending
RXNE byte is retained when there is space. On G0, a FIFO tail below RXFT is drained.
DMA TC and IDLE cannot publish the same operation twice.
OnIdle() for F4/F7 DMA RX therefore runs from DMA RX IRQ, after the stop.
For IRQ reception it still runs from USART IRQ. USART and its DMA IRQ handlers
must not preempt one another for the same object.

On G0, IDLE disables DMAR, requests a DMA stop without waiting, and completes
the packet in USART IRQ. Capacity completion runs from DMA RX IRQ. This assumes
that the last DMA request has already been serviced by IDLE handling. Aborting
an active DMA transfer can leave CNDTR inaccurate; a failed packet is discarded
without reading its DMA length. The application must allow the final DMA request
to complete before the IDLE stop. This API does not provide arbitrary abort with
recovery of partial data. No Process call or additional completion timer is used.

GetDataReceivedCount() is a pure read: zero while Busy or without a completed
packet, otherwise the completed count. ClearDataReceivedFlag() clears ready/count.
Consume the stopped buffer before clearing the flag and starting another receive.
GetOverflowCount() retains its read-and-clear contract; call it after reception stops.

DMAR=0 stops UART DMA requests; it does not disable the receiver. Bytes arriving
before the next Receive can fill RDR/FIFO or cause ORE. A new Receive discards stale
input/errors before arming the buffer. This is packet reception with explicit
rearming, not continuous reception without gaps.

---

## 7. DMA and cache

Pass named DMA_Sx::DMAReq constants directly
after the pins: USART(instance, baud, tx_pin, rx_pin, dma_tx, dma_rx).
Both default to {}. On G0 pass DMAx_Channely_BASE; USART selects its TX/RX request.
F4/F7 requests already contain the stream. USART embeds both DMA_Sx objects.
With constinit, the constexpr constructor checks request validity, ownership,
direction and distinct TX/RX resources during compilation. Each failed check uses
STM32_CONFIG_ERROR with its cause as a template argument, included in GCC's
constinit diagnostic. At runtime the macro calls DebugTrap directly.
constinit remains mandatory by the API contract. The deprecated CMSIS-pointer
overload additionally performs runtime instance lookup. No Config object is needed.
SetUp configures Byte/MINC, direction and DR/TDR/RDR and acquires the resources;
there is no external DMA object, manual DMA SetUp or AttachDMA method.
SetUp returns SysInitStatus and rejects repeated setup, invalid/busy hardware
resources. Failure releases only resources acquired by that call. DeInit releases
DMA, preserving the constructor bindings for another explicit SetUp.
Bound DMA does not prevent blocking or IRQ transfers.
USART, DMA TX and DMA RX have separate handlers. A shared TX/RX DMA vector
receives two registrations and requires two registry slots for this UART.
The registry itself and the plain-function adapter pool are unchanged.

Shared UART vectors also use IRQ_Registry capacity. If all four G0B0/B1
USART3..6 are used, configure at least four slots in irq_registry_settings.hpp.
An exhausted IRQ row returns InitError; this is not configured through CMake.

USART1 DMA2_Stream5 / CH4 is named RX_DMA2_Stream5; it is an RX route.
UART7/8 routes and G0 USART3..6 request IDs are provided where those peripherals exist.
USART reads ownership/direction from the named request. The duplicated UART DMA
route table was removed. SPI and ADC now validate request metadata in their constructors too.
GetErrorFlags() reads hardware error flags; UART enables TC/TE and F4/F7 DME interrupts.

Cache maintenance belongs to UART, using [System helpers](System.md):

| Handover | Operation |
|---|---|
| CPU -> TX DMA | CacheClean(buffer, length) |
| CPU -> RX DMA | CacheCleanInvalidate(buffer, capacity) |
| RX DMA -> CPU | Stop first, then CacheInvalidate(buffer, actual DMA count) |

On F4/G0 these functions are empty. On F7 System skips maintenance when D-cache is
disabled or the whole range lies inside one configured non-cacheable dma_regions area.
There is no second UART implementation or buffer-specific #ifdef.

A cacheable RX allocation must own every touched 32-byte line, including padding.
Do not let unrelated objects share those lines, or let the CPU access them while DMA
owns the buffer. Logical packet lengths need not be multiples of 32.
Aligned 64-byte storage can receive a packet of up to 37 bytes:

~~~cpp
alignas(32) static uint8_t packet[64];
uart.Receive_DMA(packet, 37);
~~~

Operation cost scales with touched cache lines and dirty writebacks, plus barriers.
Non-cacheable F7 buffers still incur the System range check. No cycle/size benchmark
was performed for these changes. See [ST AN4839](https://www.st.com/resource/en/application_note/an4839-level-1-cache-on-stm32f7-series-and-stm32h7-series-stmicroelectronics.pdf).

---

## 8. Errors, FIFO and callbacks

UART PE/FE/NE/ORE are checked by a common enabled-error mask. DMA errors take
precedence over completion. The affected operation stops, becomes Error and invokes
OnError(uint32_t errors) once with the combined mask. An erroneous RX packet is not
published as ready. Error contains Parity/Framing/Noise/Overrun/DmaTx/DmaRx bits.
For blocking Receive, OnError runs in the calling context; asynchronous errors run in IRQ.

Callbacks are protected. Overrides of OnRxByte, OnTxEmpty, OnIdle,
OnDmaTxComplete and OnDmaRxComplete call the base implementation to retain standard
buffer/completion handling. OnTC and OnError are notifications with empty defaults.
No Register/Unregister, SetUp or DeInit from callbacks.

USART IRQ uses its initial status/CR1/CR3 snapshots for source and DMA-gate
checks. F4/F7 read the single RX byte without a second RXNE check. TCIE and TC
are read again after TX handling: callbacks may enable TCIE, and writing TDR/DR
clears TC.

G0 FIFO is accepted only for instances listed by IS_UART_FIFO_INSTANCE.
Threshold codes are 0..5; FIFO_TH::all is 5 (RX full, TX empty).
F4/F7 accept FIFO::NO only. Oversampling remains x16; no x8 selection was added.

Ready RX/TX processing is bounded by hardware FIFO depth per direction and IRQ
entry: eight words on G0 with FIFO enabled, one byte otherwise. IDLE handles its
own bounded RX tail; normal RX draining is skipped in that entry. Processing does
not wait for future bytes. RX/TX sources service remaining data in later IRQs;
arrivals after IDLE belong to the next packet and retain the documented gap contract.
Oversampling is performed by hardware and does not multiply CPU interrupts.
Select the frame once using SetUp(fifo, tx_threshold, rx_threshold, FrameFormat::E8_1/O8_1/N8_2/N8_1). All formats retain eight payload bits; parity uses the ninth hardware word bit. The legacy SetParity method still ignores its argument; use FrameFormat instead.

---

## 9. Usage Examples

~~~cpp
// G0: physical channels are selected explicitly, DMA objects belong to UART.
constinit USART uart(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7,
           DMA1_Channel4_BASE,
           DMA1_Channel5_BASE);
alignas(32) static uint8_t packet[128];

if (uart.SetUp() == SysInitStatus::InitOK)
    uart.Receive_DMA(packet, sizeof(packet));

// Main:
if (uart.GetDataReceivedFlag()) {
    uint32_t count = uart.GetDataReceivedCount();
    // Process packet[0..count) before rearming.
    uart.ClearDataReceivedFlag();
    uart.Receive_DMA(packet, sizeof(packet));
}
~~~

For F4/F7 pass USART::_1::DMA::TX_DMA2_Stream7 and
USART::_1::DMA::RX_DMA2_Stream5 directly in the last two DMA positions.

Existing consumers that relied on GetDataReceivedCount() clearing the flag must
now call ClearDataReceivedFlag() explicitly. Modbus constructor arguments have been
migrated to these request descriptions; its current transfer/framing contract is described in [Modbus.md](Modbus.md).

Independent example; check device/package availability and every returned status.

```cpp
#include "uart.hpp"
using U = USART::_3;
constinit USART port(U::instance, 115200, U::TX::PD8, U::RX::PD9);
// In main, after System/RCC initialization:
if (port.SetUp() != SysInitStatus::InitOK)
    System::DebugTrap("UART setup");
// Select blocking/IRQ methods, or constructor-bound DMA as described above.
```

---

## 10. Edge Cases

IDLE ends a packet and stops RX DMA. Do not treat it as strict Modbus RTU t1.5/t3.5 framing. On G0 the final request must be serviced before the normal IDLE stop. TX DMA completion is not physical TC; buffer/cache ownership and control/IRQ serialization remain application obligations.

---

## See Also

- [Initialization.md](Initialization.md)
- [DMA.md](DMA.md)
- [GPIO.md](GPIO.md)
- [IRQ_Registry.md](IRQ_Registry.md)
- [Modbus.md](Modbus.md)

---

## Русский

## Содержание

1. [Зачем это нужно](#1-зачем-это-нужно)
2. [Постоянная инициализация и диагностика конструктора](#2-постоянная-инициализация-и-диагностика-конструктора)
3. [Настройка и владение](#3-настройка-и-владение)
4. [Источник тактирования (F7/G0)](#4-источник-тактирования-f7g0)
5. [Обмен и готовность пакета](#5-обмен-и-готовность-пакета)
6. [DMA, кеш и ошибки](#6-dma-кеш-и-ошибки)
7. [Совместимость](#7-совместимость)
8. [Источники для таблиц](#8-источники-для-таблиц)
9. [Примеры использования](#9-примеры-использования)
10. [Угловые случаи](#10-угловые-случаи)
11. [См. также](#см-также)

---

## 1. Зачем это нужно

USART предоставляет независимые TX/RX, блокирующие/IRQ/DMA-операции и завершение пакета по IDLE. Конструктор закрепляет пины, DMA и доступный источник частоты; UART и каждое направление DMA обслуживаются отдельно.

---

## 2. Постоянная инициализация и диагностика конструктора

Объявляйте объекты периферии глобально с `constinit` либо как `static constinit`. Используйте готовый _N::instance, именованные пины и необязательные маршруты _N::DMA. На G0 передаются числовые DMAx_Channely_BASE, источник запроса закрепляет периферия.

При проверке привязок периферии каждое неверное условие использует `STM32_CONFIG_ERROR("причина")`. Причина передаётся как параметр шаблона неконстантной функции, поэтому GCC включает её в само сообщение ошибки. Макрос выбирает путь через `std::is_constant_evaluated()`; во время выполнения напрямую вызывает `System::DebugTrap(text)`, без вызова шаблонной функции. При `constinit` такой вызов отклоняется при сборке; без него неверное runtime-создание останавливает программу. Ключевое слово обязательно по контракту, конструктор не проверяет его наличие. Проверки состояния аппаратуры и занятых ресурсов остаются в SetUp. Варианты конструкторов и миграция: [Initialization.md](Initialization.md).

---

## 3. Настройка и владение

Один объект обслуживает один UART. Сначала настраиваются System и RCC.
Источник фиксируется в конструкторе, по умолчанию PCLK. SetUp() применяет
выбранный источник, oversampling x16 и выбранный 8N1/8E1/8O1/8N2 (по умолчанию 8N1); PRESC на G0 — /1.
BRR округляется до ближайшего целого: (kernel_clock + baud/2) / baud.
Допустимы 16..65535 и baud <= kernel_clock/16. Нулевой или непредставимый baud —
InitError. Условного нижнего предела 9600 больше нет.

Таблицы учитывают наличие периферии и GPIO в CMSIS: F4/F7 UART/USART 1..8,
G0 USART1..6 при их наличии. LPUART и F4 UART9/10 в эти таблицы не входят.
Наличие порта не означает доступность каждого пина в конкретном корпусе.
PIN{} оставляет направление ненастроенным.

SetUp() один раз регистрирует объект на UART IRQ и проверяет результат.
Повторная настройка — InitError до DeInit(). IRQ_en() меняет только биты источника.
Завершение обмена не меняет реестр. DeInit() останавливает DMA, освобождает свои
ресурсы и удаляет только свои обработчики, сохраняя общие IRQ других устройств.
Настройка/подключение/удаление выполняются в main, объект живёт до снятия регистрации.

UART_USE_ATOMIC в uart.hpp выбирает изменение управляющих битов CR1/CR3,
включая IRQ_en/DMA_en, бита тактирования UART и поля источника в RCC:

- 0 (по умолчанию): обычные SET_BIT/CLEAR_BIT/MODIFY_REG.
- 1: ATOMIC_SET_BIT/ATOMIC_CLEAR_BIT/ATOMIC_MODIFY_REG ST. F4/F7 используют
  LDREX/STREX с повтором; G0 кратко маскирует прерывания на одну операцию,
  восстанавливая прежний PRIMASK.

Измените значение в uart.hpp и пересоберите драйверы вместе с приложением.
Значение должно совпадать во всех единицах трансляции с UART: define только
в main.cpp не меняет настройку uart.cpp. Выбор выполняется препроцессором,
ветвления при выполнении нет. Атомарный режим защищает отдельное изменение
регистра. Сериализация вызовов, работа с буферами/состоянием и настройка/освобождение
сохраняют прежний контракт вызывающего кода. Чтение статуса, подтверждение
флагов SR/ICR и полная инициализация регистров выполняются прямыми обращениями
с учётом аппаратной семантики.

---

## 4. Источник тактирования (F7/G0)

Последний необязательный параметр конструктора — `USART::ClockSource`:

| Источник | Частота для расчёта BRR |
|---|---|
| `PCLK` (по умолчанию) | Частота APB соответствующего USART |
| `SYSCLK` | Частота до делителя AHB, восстановленная по HCLK и HPRE |
| `HSI` | 16 МГц, до HSIDIV на G0 |
| `LSE` | 32768 Гц |

```cpp
// F7/G0: вызывающий код заранее включает HSI и ждёт HSIRDY.
constinit USART link(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7,
           {}, {}, USART::ClockSource::HSI);
SysInitStatus status = link.SetUp();
```

`SetUp()` только проверяет, что выбранный HSI/LSE включён и готов. Запускать
генераторы и ожидать их готовности должен вызывающий код. Недоступный или
неготовый источник даёт InitError до настройки UART. На G0 экземпляры без поля
RCC USARTxSEL принимают только PCLK; другой источник также даёт InitError.
Конструктор и тактирование F4 остаются прежними — от PCLK.

Источник неизменяем, в том числе после DeInit()/SetUp(). Во время использования
UART генератор должен работать, частоты и делители RCC должны сохраняться.
SetBaud() рассчитывает BRR от того же источника и возвращает Error, если он
перестал быть готов; источник не переключается. При oversampling x16 частота
LSE ограничивает baud значением 2048.

---

## 5. Обмен и готовность пакета

Нулевой указатель, нулевая длина, ненастроенное направление — Error.
До SetUp — NotInit, при занятом направлении — Busy.
IRQ/DMA принимают 1..65535 байт; блокирующие методы — ненулевую длину uint32_t.
После Error/Timeout разрешён новый запуск.

Таймаут блокирующего метода относится ко всему вызову, не к каждому байту.
Ему нужен работающий System::GetTick(). Блокирующий Receive читает ровно len байт
и не устанавливает флаг готовности асинхронного пакета.

TX OK означает передачу данных в UART и возможность повторного использования
исходного буфера. Последний стоповый бит может ещё передаваться.
Физическое завершение определяется UART TC; это нужно, например, для RS-485 DE.
SetBaud() проверяет divider, записывает BRR при UE=0, сохраняет новый BaudRate
и возвращает SysStatus.
Прежний бит UE восстанавливается с учётом UART_USE_ATOMIC; остальные текущие биты CR1 сохраняются.

Перед SetBaud вызывающий код должен дождаться последнего стопового бита;
проверка Busy относится только к программной операции. Ожидание физического TC —
ответственность вызывающего кода; дополнительного поля в драйвере нет.
Если обработчик IRQ очищает TC, завершение можно учитывать по уведомлению OnTC().

Receive_IRQ/DMA заканчивается по IDLE или заполнению буфера.
На F4/F7 при IDLE отключаются источники RX и DMAR и запрашивается остановка DMA.
USART IRQ сразу возвращается; статус RX остаётся Busy. Остановка завершается
событием DMA TC: отдельный DMA RX IRQ читает счётчик и обслуживает кеш.
Ожиданий в этих обработчиках нет. Оставшийся в UART/FIFO хвост
добавляется, если есть место. Только после этого публикуются длина и готовность.
На F4 чтение SR/DR для очистки IDLE выполняется после остановки DMA.
Повторного завершения одной операции по DMA TC и IDLE нет.
OnIdle для DMA RX F4/F7 вызывается из DMA RX IRQ после остановки; для приёма
через IRQ — из USART IRQ. Обработчики USART и его DMA не должны вытеснять друг
друга.

На G0 IDLE отключает DMAR, затем DMA без ожидания и завершает пакет в USART IRQ.
Заполнение буфера обрабатывается в DMA RX IRQ. Условие: последний запрос DMA
уже обслужен к моменту остановки по IDLE. Abort посреди переноса может оставить
CNDTR недостоверным; повреждённый пакет отбрасывается без чтения длины из DMA.
Приложение должно обеспечить обслуживание последнего запроса до остановки.
Восстановление частичных данных после произвольного abort не поддерживается.
Process и дополнительный таймер завершения не используются.

GetDataReceivedCount() ничего не сбрасывает: при Busy/отсутствии готового пакета
возвращает 0, после завершения — длину. После обработки вызвать
ClearDataReceivedFlag(), затем запустить следующий приём.
GetOverflowCount() остаётся чтением со сбросом и вызывается после остановки приёма.

DMAR отключает запросы DMA, но не приёмник UART. До повторного запуска новые байты
могут заполнить регистр/FIFO и вызвать ORE. Следующий Receive удаляет старый ввод
и ошибки. Непрерывный приём без пауз эта модель не обеспечивает.

---

## 6. DMA, кеш и ошибки

Конструктор принимает описания dma_tx и dma_rx после пинов, по умолчанию {}.
На F4/F7 передаётся именованный DMAReq с потоком; на G0 —
только DMAx_Channely_BASE; запрос TX/RX выбирает USART. USART содержит два объекта DMA_Sx внутри;
отдельные DMA-объекты в приложении не создаются. Совместимость периферии,
направления и выбор разных TX/RX ресурсов проверяются при компиляции
объекта с constinit, как принадлежность PIN. Runtime-проверки с DebugTrap
сохранены только у deprecated-конструктора с CMSIS-указателем. Config не нужен. Дублирующая таблица UART DMA удалена.
На F7/G0 ClockSource следует после обоих DMA. Привязки неизменяемые;
блокирующие и IRQ-методы доступны и при наличии DMA. AttachDMA удалён.
SetUp возвращает SysInitStatus, приобретает выбранные ресурсы и проверяет
результаты настройки/регистрации. Занятость проверяется при приобретении.
Повторная настройка — ошибка до DeInit().
DeInit освобождает DMA, но сохраняет привязки для следующего SetUp.
Отказ освобождает только ресурсы, полученные текущим вызовом.
USART, DMA TX и DMA RX имеют отдельные обработчики. На общем DMA-векторе
TX/RX занимают два места реестра. Реестр и пул адаптеров обычных функций не меняются.

Для одновременного использования G0B0/B1 USART3..6 на общем UART-векторе
нужно не менее четырёх мест в irq_registry_settings.hpp. Настройка выполняется
через заголовок; при заполнении строки возвращается InitError.

UART вызывает System::CacheClean перед TX, CacheCleanInvalidate перед RX,
CacheInvalidate после остановки RX для записанных DMA байтов.
F4/G0: пустые функции. F7: System пропускает кешевые операции для диапазона,
полностью находящегося внутри одной некешируемой dma_regions области.
При этом остаётся проверка диапазона; отдельной реализации UART нет.

Кешируемый RX-буфер должен владеть всеми затронутыми 32-байтными строками.
Пока DMA работает, CPU не обращается к этим строкам. Длина пакета может быть
некратна 32, если выделенная память включает необходимый запас.
Например, alignas(32) uint8_t buffer[64] позволяет принять до 37 байт.
Стоимость зависит от числа строк, записи грязных данных и барьеров; замеров нет.

PE/FE/NE/ORE проверяются общей маской разрешённых ошибок.
Ошибки DMA имеют приоритет над успешным завершением.
Операция останавливается, получает Error и вызывает OnError(errors) с общей маской.
Повреждённый RX-пакет не получает ready. При блокирующем Receive callback вызывается
из вызывающего кода, при асинхронном обмене — из IRQ.

Callbacks доступны наследникам как protected. Для сохранения стандартной обработки
переопределения вызывают базовые OnRxByte/OnTxEmpty/OnIdle/OnDmaTxComplete/
OnDmaRxComplete. OnTC/OnError — уведомления с пустой реализацией по умолчанию.
Из callback нельзя менять реестр или вызывать SetUp/DeInit.

USART IRQ проверяет источники и разрешения DMA по начальным снимкам
status/CR1/CR3. На F4/F7 один RX-байт читается без повторной проверки RXNE.
После обработки TX повторно читаются TCIE и TC: callbacks могут включить TCIE,
а запись TDR/DR сбрасывает TC.

FIFO G0 проверяется по возможностям конкретного USART. Код all исправлен на 5:
RX — полный FIFO, TX — пустой. Oversampling остаётся x16, режим x8 не добавлен.

За вход в IRQ обслуживается не больше глубины FIFO в каждом направлении:
8 слов на G0 при включённом FIFO, иначе один байт. При IDLE обычное чтение RX
пропускается, хвост обрабатывает OnIdle с тем же пределом. Ожидания новых байтов
нет. В обычном обмене остаток обслуживается следующими IRQ; при IDLE действует
прежнее условие паузы между пакетами, новые поступления не гарантируются до
следующего Receive.
Формат выбирается один раз: SetUp(fifo, tx_threshold, rx_threshold, FrameFormat::E8_1/O8_1/N8_2/N8_1). Во всех вариантах восемь полезных бит; parity занимает девятый аппаратный бит слова. Старый SetParity по-прежнему игнорирует аргумент: используйте FrameFormat.

---

## 7. Совместимость

USART1 DMA2_Stream5 / CH4 назван RX_DMA2_Stream5: это маршрут RX.
Добавлены UART7/8 и G0 USART3..6 DMAReq; исправлены G0 AF USART1/4 и общие IRQ.
Добавлены DMA_Sx::CanUseRequest() и GetErrorFlags(); прочие потребители DMA не менялись.

Потребители, ожидавшие сброс флага в GetDataReceivedCount(), должны теперь вызывать
ClearDataReceivedFlag() явно. Конструктор Modbus переведён на описания Req;
актуальный контракт передачи/IDLE описан в [Modbus.md](Modbus.md).

---

## 8. Источники для таблиц

- [ST USART F7 — источники тактирования](https://www.st.com/resource/en/product_training/STM32F7_Peripheral_USART.pdf).
- [ST RM0444 — RCC/USART G0x1](https://www.st.com/resource/en/reference_manual/dm00371828.pdf).
- [ST G0 workshop — USART FIFO depth](https://www.st.com/content/dam/AME/2019/STM32G0-q1-webinar-workshops/STM32G0_Workshop_Presentation.pdf).
- [ST AN4031 — таблицы DMA F4/F7](https://www.st.com/resource/en/application_note/dm00046011-using-the-stm32f2-stm32f4-and-stm32f7-series-dma-controller-stmicroelectronics.pdf).
- [ST G0 LL DMAMUX — идентификаторы запросов](https://raw.githubusercontent.com/STMicroelectronics/stm32g0xx-hal-driver/master/Inc/stm32g0xx_ll_dmamux.h).
- [ST G071 datasheet — AF USART1/4](https://www.st.com/resource/en/datasheet/stm32g071kb.pdf).
- [ST G0B1 datasheet — AF USART3..6](https://www.st.com/resource/en/datasheet/stm32g0b1me.pdf).

---

## 9. Примеры использования

Отдельный пример; проверьте наличие блока/пинов в корпусе и каждый возвращаемый статус.

```cpp
#include "uart.hpp"
using U = USART::_3;
constinit USART port(U::instance, 115200, U::TX::PD8, U::RX::PD9);
// In main, after System/RCC initialization:
if (port.SetUp() != SysInitStatus::InitOK)
    System::DebugTrap("UART setup");
// Select blocking/IRQ methods, or constructor-bound DMA as described above.
```

---

## 10. Угловые случаи

IDLE завершает пакет и останавливает RX DMA. Это не строгий Modbus RTU framing t1.5/t3.5. На G0 последний запрос должен быть обслужен до штатной остановки по IDLE. Завершение TX DMA не равно физическому TC; владение буфером/кеш-линиями и согласование управления с IRQ остаются приложению.

---

## См. также

- [Initialization.md](Initialization.md)
- [DMA.md](DMA.md)
- [GPIO.md](GPIO.md)
- [IRQ_Registry.md](IRQ_Registry.md)
- [Modbus.md](Modbus.md)
