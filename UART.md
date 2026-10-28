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

`USART` is a byte-oriented serial port driver:

- **TX and RX are independent** — each can run its own operation at the same time.
- Three ways to transfer: **blocking**, **interrupt** and **DMA**.
- Asynchronous reception ends a **packet** on line idle (IDLE) or when the buffer is full.
- The constructor fixes the pins, the DMA channels and (F7/G0) the clock source.
- The UART and each DMA direction have their own interrupt handlers.

---

## 2. Constant Initialization and Constructor Diagnostics

Declare the object at file scope with `constinit` (or as `static constinit` inside a function). Arguments: `USART::_N::instance`, baud rate, named pins, optional DMA (named `_N::DMA::...` routes on F4/F7, physical `DMAx_Channely_BASE` on G0 — the USART supplies the request ID).

What happens with a wrong argument (e.g. a pin of another USART):

| Declaration | Result |
| --- | --- |
| `constinit` object | **Compilation error** with the reason text in the message, e.g. `USART: TX pin belongs to another peripheral`. |
| Object without `constinit` | Compiles; at start-up the constructor stops in `System::DebugTrap(reason)`. |

Every check calls `STM32_CONFIG_ERROR("reason")`: at compile time a GCC error containing the reason, at run time `System::DebugTrap(text)`. `constinit` is required by the usage contract but not enforced.

`SetUp()` still checks hardware state and occupied resources. The object keeps the instance reference plus one register pointer; null means "not set up" (cleared after a failed first `SetUp()` and by `DeInit()`). Details: [Initialization.md](Initialization.md).

---

## 3. Initialization and ownership

**One object controls one UART.** Initialize System/RCC first.

**`SetUp()`** applies:

- the clock source chosen in the constructor (default PCLK);
- oversampling ×16;
- frame format 8N1 (default), 8E1, 8O1 or 8N2;
- on G0, prescaler PRESC = /1.

**Baud rate** is rounded to the nearest divider:

```text
BRR = (kernel_clock + baud/2) / baud,   16 ≤ BRR ≤ 65535,   baud ≤ kernel_clock / 16
```

Zero or unrepresentable baud → `InitError`. There is no artificial 9600 minimum.

**Pins:** use `USART::_N::TX/RX`. `PIN{}` leaves that direction unused. The tables follow what the CMSIS device header declares:

| Family | Instances in the tables |
| --- | --- |
| F4/F7 | UART/USART 1…8, where present |
| G0 | USART1…6, where present |

LPUART and F4 UART9/10 are not covered. A GPIO port existing in CMSIS does not mean the pin is bonded in your package.

**Interrupt ownership:**

- `SetUp()` registers the object on the UART vector once and checks the result. Do not register it manually.
- A repeated `SetUp()` → `InitError` until `DeInit()`.
- `IRQ_en()` changes only peripheral source bits. Finishing a transfer disables its sources but never unregisters.
- `DeInit()` removes only this object's registrations; other handlers on shared vectors stay.
- Call setup/teardown from `main`. The object must outlive its registration; the destructor does **not** call `DeInit()`.

**`UART_USE_ATOMIC`** (in `uart.hpp`, default `0`) — how control bits (CR1/CR3, `IRQ_en`/DMA enable) and the UART's RCC clock fields are updated:

| Value | Update method |
| --- | --- |
| `0` | plain `SET_BIT`/`CLEAR_BIT`/`MODIFY_REG` |
| `1` | ST `ATOMIC_*`: LDREX/STREX with retry on F4/F7; short PRIMASK mask/restore per update on G0 |

- Change it in `uart.hpp` and rebuild drivers **and** application. Defining it only in `main.cpp` does not affect `uart.cpp`.
- Chosen by the preprocessor — no run-time branch.
- It protects individual register updates only. Serializing transfer calls, buffer/state access and setup/teardown is still the caller's job.
- Status reads, SR/ICR flag clearing and whole-register initialization are always direct accesses.

---

## 4. Kernel clock (F7/G0)

The optional last constructor argument is `USART::ClockSource`:

| Source | Frequency used for BRR |
| --- | --- |
| `PCLK` (default) | this instance's APB clock |
| `SYSCLK` | system clock before the AHB divider (derived from HCLK and HPRE) |
| `HSI` | 16 MHz (before HSIDIV on G0) |
| `LSE` | 32768 Hz |

```cpp
// F7/G0; the caller enables HSI and waits for HSIRDY before SetUp().
constinit USART link(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7,
                     {}, {}, USART::ClockSource::HSI);
SysInitStatus status = link.SetUp();
```

- `SetUp()` only **checks** that HSI/LSE is on and ready; it never starts or waits for an oscillator. Not ready → `InitError` before any UART configuration.
- G0 instances without an RCC `USARTxSEL` field accept only `PCLK`; anything else → `InitError`.
- F4 has no such argument — always PCLK.
- The source is fixed for the object's life, including across `DeInit()`/`SetUp()`. Keep the oscillator running and RCC dividers unchanged while the UART is in use.
- `SetBaud()` uses the same source and returns `Error` if it is no longer ready.
- With ×16 oversampling, LSE limits the baud rate to 2048.

---

## 5. Transfer contract

| Mode | Methods | Length | Completion |
| --- | --- | --- | --- |
| Blocking | `Send` / `Receive` | any non-zero `uint32_t` | timeout covers the whole call |
| IRQ | `Send_IRQ` / `Receive_IRQ` | 1…65535 | RX ends on full buffer or IDLE |
| DMA | `Send_DMA` / `Receive_DMA` | 1…65535 | DMA set up by `SetUp()`; RX ends on full buffer or IDLE |

**Status codes:**

| Result | When |
| --- | --- |
| `NotInit` | before `SetUp()` |
| `Error` | null pointer, zero/too large length or missing required DMA |
| `Busy` | an operation is already running in that direction |

TX and RX run independently. After `Error` or `Timeout` a new operation may be started.

Start validates direction status, buffer/length once, and the required DMA
selection for DMA methods. `NotInit`/`Busy` are returned directly from the status;
there is no second register-pointer or TE/RE check. The caller selects a direction
configured by the constructor pins and preserves that hardware configuration.
DMA starts use the existing low-level `PrepareTransfer()` instead of separate
checked address/count setters. TE/DME sources are restored in the same control
update for retry after errors; start never changes or checks NVIC. UART sources
are enabled together with one update per control register.

**What TX "OK" means:**

- `Send()` → all bytes were handed to the UART.
- `Send_IRQ`/`Send_DMA` → the source buffer may be reused.
- In both cases **the last stop bit may still be on the wire**. For physical completion (e.g. before releasing RS-485 DE) use the UART **TC** event / `OnTC()`.

**`SetBaud(baud)`:** validates the divider, writes BRR with UE = 0, stores the new rate and restores only the previous UE bit (other CR1 bits are kept). Returns `NotInit`, `Error` or `Busy` if the change cannot be made. The `Busy` check covers only the software operation — **you** must make sure the last stop bit has left the pin (wait for TC) before changing the baud rate.

**Blocking calls** need a running System tick ([System.md](System.md)). Blocking `Receive()` reads exactly the requested number of bytes and does not set the asynchronous "packet ready" flag.

---

## 6. One packet at a time

`Receive_IRQ`/`Receive_DMA` starts a **fresh packet** and clears the previous ready flag, count and overflow counter.

**End of packet on IDLE — F4/F7 with DMA** (no waiting inside interrupts):

1. Disable the UART RX interrupt sources and DMA requests (DMAR).
2. Request a DMA stop and return from the USART handler; RX stays `Busy`.
3. The DMA RX "transfer complete" interrupt confirms the stop, reads the remaining count and invalidates the written bytes in the cache.
4. Any byte still pending in the UART/FIFO is appended if it fits.
5. Publish the byte count, `OK` and the ready flag.

Notes:

- On F4 the SR/DR read that clears IDLE happens after the DMA stops; a pending RXNE byte is kept when there is room.
- A packet is never published twice (DMA TC and IDLE cannot both complete it).
- With DMA RX on F4/F7, `OnIdle()` runs in the **DMA RX** interrupt (after the stop); with IRQ reception it runs in the USART interrupt.
- The USART handler and its DMA handlers must not preempt each other for the same object.

**End of packet on IDLE — G0:** IDLE disables DMAR, requests the DMA stop without waiting and completes the packet in the USART interrupt; a full buffer completes in the DMA RX interrupt. G0's FIFO tail below the RX threshold is drained.

> **G0 condition:** the last DMA request must already be serviced when IDLE stops the transfer. Aborting an active DMA transfer can leave CNDTR inaccurate; such a packet is discarded without reading its DMA length. Arbitrary abort with recovery of partial data is not supported. No `Process()` call or completion timer is used.

**Reading the result:**

```cpp
if (uart.GetDataReceivedFlag()) {
    uint32_t count = uart.GetDataReceivedCount();   // pure read
    // ... use buffer[0..count) ...
    uart.ClearDataReceivedFlag();                   // clears ready + count
    uart.Receive_DMA(buffer, sizeof(buffer));       // re-arm
}
```

- `GetDataReceivedCount()` changes nothing: 0 while `Busy` or with no completed packet, otherwise the count.
- Consume the buffer **before** clearing the flag and starting the next receive.
- `GetOverflow()` tests `data_overflow_count != 0` (no separate flag). The `uint32_t` counter wraps without saturation — wrapping to 0 also clears the reported overflow.
- `GetOverflowCount()` reads **and clears** the counter; call it after reception has stopped. A new `Receive` also resets it.

> **Not continuous reception.** DMAR = 0 stops DMA requests but not the receiver. Bytes arriving before the next `Receive` can fill RDR/FIFO or cause an overrun (ORE). A new `Receive` discards stale input and errors before arming the buffer. This is packet reception with explicit re-arming, not gap-free streaming.

---

## 7. DMA and cache

**Binding.** Pass DMA requests right after the pins:

```text
USART(instance, baud, tx_pin, rx_pin, dma_tx = {}, dma_rx = {} [, clock_source])
```

- F4/F7: named requests (`_N::DMA::TX_DMA2_Stream7`…) already contain stream, channel and direction.
- G0: `DMAx_Channely_BASE`; the USART selects the TX/RX request.
- The USART **embeds** both `DMA_Sx` objects — there is no external DMA object, manual DMA `SetUp()` or `AttachDMA()`.
- With `constinit` the constructor checks request validity, owner, direction and that TX/RX use different resources, at compile time.
- The deprecated CMSIS-pointer constructor additionally looks up the instance at run time.
- DMA binding does not prevent blocking or IRQ transfers.

**`SetUp()` and DMA:** configures byte size, memory increment, direction and the data register; acquires the resources. It rejects repeated setup and invalid/busy hardware resources; on failure it releases only what this call acquired. `DeInit()` releases DMA but keeps the constructor bindings for a later `SetUp()`.

**Interrupt slots:**

- USART, DMA TX and DMA RX have separate handlers. When TX and RX DMA share one vector, that vector needs **two** registry slots for this UART.
- Shared UART vectors also consume registry capacity. Using all four G0B0/B1 USART3…6 needs at least **four** slots in `irq_registry_settings.hpp` (not configurable through CMake). A full row → `InitError`.

**Routes and request tables:**

- USART1 DMA2_Stream5 / CH4 is named `RX_DMA2_Stream5` — it is an RX route.
- UART7/8 routes and G0 USART3…6 request IDs are provided where those peripherals exist.
- The USART reads owner/direction from the named request; the old duplicated UART DMA route table was removed. SPI and ADC validate their requests in the constructor too.
- `GetErrorFlags()` reads hardware error flags. The UART enables TC/TE and, on F4/F7, DME interrupts.

**Cache maintenance** is done by the UART through [System helpers](System.md):

| Handover | Operation |
| --- | --- |
| CPU → TX DMA | `CacheClean(buffer, length)` |
| CPU → RX DMA | `CacheCleanInvalidate(buffer, capacity)` |
| RX DMA → CPU | stop first, then `CacheInvalidate(buffer, actual DMA count)` |

- On F4/G0 cache operations and their argument evaluation are compiled out.
- On F7 System skips them when D-cache is off or the whole range lies in DTCM or one configured non-cacheable `dma_regions` area (the range check itself still runs).
- There is one UART implementation — no buffer-specific `#ifdef`.

**Cacheable RX buffers on F7** must own every 32-byte line they touch, including padding. No unrelated object may share those lines, and the CPU must not touch them while DMA owns the buffer. Packet lengths need not be multiples of 32 — e.g. 64 bytes of aligned storage can receive up to 37 bytes:

```cpp
alignas(32) static uint8_t packet[64];
uart.Receive_DMA(packet, 37);
```

Cost grows with the number of touched lines, dirty write-backs and barriers; no benchmark was made. Background: [ST AN4839](https://www.st.com/resource/en/application_note/an4839-level-1-cache-on-stm32f7-series-and-stm32h7-series-stmicroelectronics.pdf).

---

## 8. Errors, FIFO and callbacks

**Errors:**

- Line errors PE/FE/NE/ORE are checked through a common mask of enabled errors.
- DMA errors take precedence over completion.
- The affected operation stops, becomes `Error` and calls `OnError(uint32_t errors)` **once** with the combined mask (bits `Parity`, `Framing`, `Noise`, `Overrun`, `DmaTx`, `DmaRx`).
- A faulty RX packet is never published as ready.
- For blocking `Receive`, `OnError` runs in the caller's context; for asynchronous transfers — in the interrupt.

**Callbacks** (protected virtual):

`OnIdle()` only notifies a successful IDLE-ended packet. RX is already stopped,
its tail/count/cache handling is complete and status/ready are published.
`OnDmaRxComplete()` similarly notifies capacity completion without calling
`OnIdle()`. `OnDmaTxComplete()` runs after DMA cleanup and TX OK. Existing calls
to these empty base methods remain valid but do no processing. Error packets
call `OnError()` instead; their contents must be discarded. No new state flags,
`Process()` method or waiting loops are involved.

IRQ reception and stopped-DMA reception have separate completion paths, with
one common result publication. Byte callbacks only move bytes; packet readiness
is published once after they return. An IDLE tail filling the buffer therefore
does not stop/publish it a second time. On F4/F7 DMA IDLE, UART RX sources/DMAR
are disabled once (one CR1 and one CR3 update), then the DMA IRQ performs only
buffer handover. Normal DMA capacity uses the known full length without reading
NDTR/CNDTR; an IDLE stop reads the final remaining count.

| Callback | Type | Override rule |
| --- | --- | --- |
| `OnRxByte`, `OnTxEmpty` | byte-buffer handling | call the base to keep storage/cursor/count handling |
| `OnIdle`, `OnDmaTxComplete`, `OnDmaRxComplete`, `OnTC`, `OnError` | notifications after standard processing | empty by default; no base call required |

No `Register`/`Unregister`, `SetUp()` or `DeInit()` from callbacks.

**Inside the USART interrupt:** source and DMA-gate checks use one initial snapshot of status/CR1/CR3. F4/F7 read the single RX byte without re-checking RXNE. TCIE and TC are re-read after TX handling, because callbacks may enable TCIE and writing DR/TDR clears TC.

**Work per interrupt entry is bounded:** at most the hardware FIFO depth per direction — 8 words on G0 with FIFO enabled, otherwise one byte. On IDLE the normal RX draining is skipped; the IRQ/DMA completion path handles the tail with the same bound before notifying `OnIdle`. The handler never waits for future bytes; remaining data is serviced by later interrupts. Hardware oversampling does not multiply CPU interrupts.

**FIFO (G0 only):** allowed only for instances listed in `IS_UART_FIFO_INSTANCE`. Threshold codes 0…5; `FIFO_TH::all` = 5 (RX full / TX empty). F4/F7 accept only `FIFO::NO`. Oversampling stays ×16 (no ×8 option).

**Frame format** is chosen once in `SetUp(fifo, tx_threshold, rx_threshold, FrameFormat::E8_1 / O8_1 / N8_2 / N8_1)`. All formats carry 8 data bits; parity uses the ninth hardware bit. The legacy `SetParity()` still ignores its argument — use `FrameFormat`.

---

## 9. Usage Examples

**Minimal blocking/IRQ port:**

```cpp
#include "uart.hpp"
using U = USART::_3;

constinit USART port(U::instance, 115200, U::TX::PD8, U::RX::PD9);

// In main, after System/RCC initialization:
if (port.SetUp() != SysInitStatus::InitOK)
    System::DebugTrap("UART setup");
```

**DMA packet reception (G0):**

```cpp
// G0: physical channels are chosen explicitly; the DMA objects belong to the UART.
constinit USART uart(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7,
                     DMA1_Channel4_BASE, DMA1_Channel5_BASE);
alignas(32) static uint8_t packet[128];

if (uart.SetUp() == SysInitStatus::InitOK)
    uart.Receive_DMA(packet, sizeof(packet));

// Main loop:
if (uart.GetDataReceivedFlag()) {
    uint32_t count = uart.GetDataReceivedCount();
    // Process packet[0..count) before re-arming.
    uart.ClearDataReceivedFlag();
    uart.Receive_DMA(packet, sizeof(packet));
}
```

On F4/F7 pass `USART::_1::DMA::TX_DMA2_Stream7` and `USART::_1::DMA::RX_DMA2_Stream5` in the two DMA positions instead.

**Migration note:** code that relied on `GetDataReceivedCount()` clearing the flag must now call `ClearDataReceivedFlag()` explicitly. Modbus uses the same request descriptions; its framing rules are in [Modbus.md](Modbus.md).

Check every returned status.

---

## 10. Edge Cases

| Situation | Behaviour |
| --- | --- |
| IDLE on the line | Ends the packet and stops RX DMA. This is **not** strict Modbus RTU t1.5/t3.5 framing. |
| G0: IDLE before the last DMA request was serviced | Must not happen — the final request has to be serviced before the IDLE stop. |
| TX DMA complete | Not physical completion — wait for TC before releasing DE or changing baud. |
| Bytes arrive between packets | May be lost or cause ORE until the next `Receive`. |
| Buffer/cache ownership, IRQ serialization | Application responsibility. |

---

## See Also

- [Initialization.md](Initialization.md)
- [DMA.md](DMA.md)
- [GPIO.md](GPIO.md)
- [IRQ_Registry.md](IRQ_Registry.md)
- [Modbus.md](Modbus.md)
- [ST USART F7 — clock sources](https://www.st.com/resource/en/product_training/STM32F7_Peripheral_USART.pdf)
- [ST RM0444 — RCC/USART G0x1](https://www.st.com/resource/en/reference_manual/dm00371828.pdf)
- [ST AN4031 — F4/F7 DMA tables](https://www.st.com/resource/en/application_note/dm00046011-using-the-stm32f2-stm32f4-and-stm32f7-series-dma-controller-stmicroelectronics.pdf)

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

`USART` — драйвер последовательного порта, работающий с байтами:

- **TX и RX независимы** — каждое направление может одновременно выполнять свою операцию.
- Три способа обмена: **блокирующий**, **через прерывания** и **через DMA**.
- Асинхронный приём завершает **пакет** по паузе на линии (IDLE) или при заполнении буфера.
- Конструктор закрепляет выводы, каналы DMA и (на F7/G0) источник тактирования.
- У UART и у каждого направления DMA свои обработчики прерываний.

---

## 2. Постоянная инициализация и диагностика конструктора

Объявляйте объект глобально с `constinit` (или как `static constinit` внутри функции). Аргументы: `USART::_N::instance`, скорость, именованные выводы, необязательные DMA (именованные маршруты `_N::DMA::...` на F4/F7, физические `DMAx_Channely_BASE` на G0 — номер запроса подставляет USART).

Что происходит при неверном аргументе (например, выводе другого USART):

| Объявление | Результат |
| --- | --- |
| Объект с `constinit` | **Ошибка компиляции**, текст причины — в сообщении, например `USART: TX pin belongs to another peripheral`. |
| Объект без `constinit` | Собирается; при старте конструктор останавливается в `System::DebugTrap(причина)`. |

Каждая проверка вызывает `STM32_CONFIG_ERROR("причина")`: при компиляции — ошибка GCC с текстом причины, во время работы — `System::DebugTrap(text)`. `constinit` обязателен по правилам использования, но не проверяется.

`SetUp()` по-прежнему проверяет состояние аппаратуры и занятость ресурсов. Объект хранит ссылку на instance и один указатель регистров; нулевой указатель — «не настроен» (обнуляется после неудачного первого `SetUp()` и в `DeInit()`). Подробнее: [Initialization.md](Initialization.md).

---

## 3. Настройка и владение

**Один объект управляет одним UART.** Сначала настройте System и RCC.

**`SetUp()`** применяет:

- источник тактирования, выбранный в конструкторе (по умолчанию PCLK);
- oversampling ×16;
- формат кадра 8N1 (по умолчанию), 8E1, 8O1 или 8N2;
- на G0 — предделитель PRESC = /1.

**Скорость** округляется до ближайшего делителя:

```text
BRR = (kernel_clock + baud/2) / baud,   16 ≤ BRR ≤ 65535,   baud ≤ kernel_clock / 16
```

Нулевая или непредставимая скорость → `InitError`. Искусственного нижнего предела 9600 нет.

**Выводы:** используйте `USART::_N::TX/RX`. `PIN{}` оставляет направление неиспользуемым. Таблицы следуют тому, что объявлено в CMSIS-заголовке:

| Семейство | Экземпляры в таблицах |
| --- | --- |
| F4/F7 | UART/USART 1…8, при наличии |
| G0 | USART1…6, при наличии |

LPUART и UART9/10 на F4 не поддерживаются. Наличие порта GPIO в CMSIS не означает, что вывод есть в вашем корпусе.

**Владение прерываниями:**

- `SetUp()` один раз регистрирует объект на векторе UART и проверяет результат. Не регистрируйте его вручную.
- Повторный `SetUp()` → `InitError` до `DeInit()`.
- `IRQ_en()` меняет только биты источников в периферии. Завершение обмена выключает свои источники, но регистрацию не снимает.
- `DeInit()` останавливает DMA, освобождает свои ресурсы и удаляет только свои регистрации; другие обработчики общих векторов остаются.
- Настройку и освобождение вызывайте из `main`. Объект должен жить до снятия регистрации; деструктор `DeInit()` **не** вызывает.

**`UART_USE_ATOMIC`** (в `uart.hpp`, по умолчанию `0`) — способ изменения управляющих битов (CR1/CR3, включая `IRQ_en`/включение DMA) и полей тактирования UART в RCC:

| Значение | Способ |
| --- | --- |
| `0` | обычные `SET_BIT`/`CLEAR_BIT`/`MODIFY_REG` |
| `1` | `ATOMIC_*` от ST: LDREX/STREX с повтором на F4/F7; на G0 — краткое маскирование прерываний на одну операцию с восстановлением прежнего PRIMASK |

- Меняйте значение в `uart.hpp` и пересобирайте драйверы **и** приложение. Определение только в `main.cpp` не влияет на `uart.cpp`.
- Выбор делает препроцессор — ветвления во время работы нет.
- Защищается только отдельное изменение регистра. Согласование вызовов обмена, доступа к буферам/состоянию и настройки/освобождения остаётся задачей вызывающего кода.
- Чтение статуса, сброс флагов SR/ICR и полная инициализация регистров — всегда прямые обращения.

---

## 4. Источник тактирования (F7/G0)

Последний необязательный аргумент конструктора — `USART::ClockSource`:

| Источник | Частота для расчёта BRR |
| --- | --- |
| `PCLK` (по умолчанию) | частота APB данного USART |
| `SYSCLK` | системная частота до делителя AHB (вычисляется по HCLK и HPRE) |
| `HSI` | 16 МГц (до HSIDIV на G0) |
| `LSE` | 32768 Гц |

```cpp
// F7/G0: вызывающий код заранее включает HSI и ждёт HSIRDY.
constinit USART link(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7,
                     {}, {}, USART::ClockSource::HSI);
SysInitStatus status = link.SetUp();
```

- `SetUp()` только **проверяет**, что HSI/LSE включён и готов; запускать генератор и ждать его готовности должен вызывающий код. Не готов → `InitError` до настройки UART.
- Экземпляры G0 без поля RCC `USARTxSEL` принимают только `PCLK`; иначе → `InitError`.
- На F4 этого аргумента нет — всегда PCLK.
- Источник неизменен всё время жизни объекта, в том числе после `DeInit()`/`SetUp()`. Пока UART используется, генератор должен работать, а делители RCC — не меняться.
- `SetBaud()` использует тот же источник и возвращает `Error`, если он перестал быть готов.
- При oversampling ×16 частота LSE ограничивает скорость значением 2048.

---

## 5. Обмен и готовность пакета

| Режим | Методы | Длина | Завершение |
| --- | --- | --- | --- |
| Блокирующий | `Send` / `Receive` | любая ненулевая `uint32_t` | таймаут на весь вызов, а не на байт |
| IRQ | `Send_IRQ` / `Receive_IRQ` | 1…65535 | приём заканчивается по заполнению или IDLE |
| DMA | `Send_DMA` / `Receive_DMA` | 1…65535 | приём заканчивается по заполнению или IDLE |

**Коды статуса:**

| Результат | Когда |
| --- | --- |
| `NotInit` | до `SetUp()` |
| `Error` | нулевой указатель, нулевая/слишком большая длина или нет нужного DMA |
| `Busy` | в этом направлении уже идёт операция |

После `Error`/`Timeout` можно запускать новую операцию. Блокирующим методам нужен работающий `System::GetTick()`. Блокирующий `Receive` читает ровно `len` байт и не выставляет флаг готовности асинхронного пакета.

При запуске один раз проверяются статус направления, буфер/длина и наличие
нужного DMA для DMA-методов. `NotInit`/`Busy` возвращаются прямо из статуса;
повторной проверки указателя регистров и TE/RE нет. Вызывающий код использует
направление, настроенное выводами конструктора, и сохраняет эту конфигурацию.
DMA-запуск использует существующий `PrepareTransfer()` вместо отдельных методов
адреса/счётчика с проверками. Источники TE/DME восстанавливаются той же операцией
для повторного запуска после ошибки; NVIC при запуске не проверяется и не меняется.
Источники UART включаются вместе, одним изменением каждого управляющего регистра.

**Что означает «OK» при передаче:**

- `Send()` → все байты переданы в UART.
- `Send_IRQ`/`Send_DMA` → исходный буфер можно использовать повторно.
- В обоих случаях **последний стоп-бит может ещё передаваться**. Физическое завершение (например, перед снятием DE в RS-485) определяется событием UART **TC** / `OnTC()`.

**`SetBaud(baud)`:** проверяет делитель, записывает BRR при UE = 0, сохраняет новую скорость и восстанавливает только прежний бит UE (остальные биты CR1 сохраняются, с учётом `UART_USE_ATOMIC`). Возвращает `SysStatus`. Проверка `Busy` касается только программной операции — дождаться ухода последнего стоп-бита (TC) перед сменой скорости должен **вызывающий код**.

**Конец пакета по IDLE — F4/F7 с DMA** (без ожиданий в прерываниях):

1. Отключаются источники прерываний RX и запросы DMA (DMAR).
2. Запрашивается остановка DMA, обработчик USART сразу выходит; RX остаётся `Busy`.
3. Прерывание DMA RX «передача завершена» подтверждает остановку, читает остаток счётчика и обслуживает кеш.
4. Оставшийся в UART/FIFO байт добавляется, если есть место.
5. Публикуются длина, `OK` и флаг готовности.

Замечания:

- На F4 чтение SR/DR для сброса IDLE выполняется после остановки DMA.
- Пакет не публикуется дважды (DMA TC и IDLE не могут оба завершить одну операцию).
- При DMA RX на F4/F7 `OnIdle()` вызывается из прерывания **DMA RX** (после остановки); при приёме через IRQ — из прерывания USART.
- Обработчики USART и его DMA не должны вытеснять друг друга.

**Конец пакета по IDLE — G0:** IDLE отключает DMAR, запрашивает остановку DMA без ожидания и завершает пакет в прерывании USART; заполнение буфера обрабатывается в прерывании DMA RX.

> **Условие G0:** к моменту остановки по IDLE последний запрос DMA уже должен быть обслужен. Прерывание DMA посреди переноса может оставить CNDTR недостоверным; такой пакет отбрасывается без чтения длины из DMA. Восстановление частичных данных после произвольной остановки не поддерживается. `Process()` и дополнительный таймер завершения не используются.

**Чтение результата:**

- `GetDataReceivedCount()` ничего не сбрасывает: 0 при `Busy` или без готового пакета, иначе — длина.
- После обработки вызовите `ClearDataReceivedFlag()`, затем запустите следующий приём.
- `GetOverflowCount()` читает **и сбрасывает** счётчик; вызывайте после остановки приёма.

> **Это не непрерывный приём.** DMAR отключает запросы DMA, но не приёмник UART. До следующего запуска новые байты могут заполнить регистр/FIFO и вызвать переполнение (ORE). Следующий `Receive` удаляет старый ввод и ошибки. Приём без пауз эта модель не обеспечивает.

---

## 6. DMA, кеш и ошибки

**Переполнение.** `GetOverflow()` проверяет `data_overflow_count != 0` (отдельного флага нет). Счётчик `uint32_t` переполняется без насыщения — после перехода через ноль `GetOverflow()` вернёт `false`. `GetOverflowCount()` читает и обнуляет счётчик после остановки приёма; новый `Receive` тоже его сбрасывает.

**Привязка DMA.** Описания `dma_tx` и `dma_rx` передаются после выводов, по умолчанию `{}`:

```text
USART(instance, baud, tx_pin, rx_pin, dma_tx = {}, dma_rx = {} [, clock_source])
```

- F4/F7: именованный `DMAReq` уже содержит поток, канал и направление.
- G0: только `DMAx_Channely_BASE`; запрос TX/RX выбирает USART.
- USART **содержит** оба объекта `DMA_Sx` внутри — отдельные DMA-объекты в приложении не создаются, `AttachDMA()` удалён.
- С `constinit` при компиляции проверяются принадлежность запроса, направление и разные ресурсы TX/RX — так же, как принадлежность выводов. Проверки во время работы с `DebugTrap` остались только у устаревшего конструктора с CMSIS-указателем. `Config` не нужен; дублирующая таблица маршрутов UART DMA удалена.
- На F7/G0 `ClockSource` идёт после обоих DMA.
- Привязки неизменны; блокирующие и IRQ-методы доступны и при наличии DMA.

**`SetUp()` и DMA:** захватывает выбранные ресурсы (занятость проверяется при захвате) и проверяет результаты настройки/регистрации. Повторная настройка — ошибка до `DeInit()`. При отказе освобождается только захваченное этим вызовом. `DeInit()` освобождает DMA, но сохраняет привязки для следующего `SetUp()`.

**Места в реестре прерываний:**

- У USART, DMA TX и DMA RX отдельные обработчики. Если TX и RX DMA делят один вектор, этому UART нужно **два** места в реестре. Сам реестр и пул адаптеров функций не меняются.
- Общие векторы UART тоже расходуют места. Для одновременной работы всех четырёх USART3…6 на G0B0/B1 нужно не менее **четырёх** мест в `irq_registry_settings.hpp` (через CMake не настраивается). Заполненная строка → `InitError`.

**Обслуживание кеша** выполняет UART через функции System:

| Передача владения | Операция |
| --- | --- |
| CPU → TX DMA | `CacheClean(buffer, length)` |
| CPU → RX DMA | `CacheCleanInvalidate(buffer, capacity)` |
| RX DMA → CPU | сначала остановка, затем `CacheInvalidate(buffer, фактическое число байт)` |

- На F4/G0 cache-операции и вычисление их аргументов исключены при компиляции.
- На F7 System пропускает их, если D-cache выключен или весь диапазон лежит в DTCM либо одной некешируемой области `dma_regions` (проверка диапазона всё равно выполняется).
- Отдельной реализации UART для разных буферов нет.

**Кешируемый RX-буфер на F7** должен владеть всеми 32-байтными линиями, которые он задевает, включая выравнивающие байты. Пока работает DMA, процессор к этим линиям не обращается. Длина пакета может быть некратной 32, если выделенная память включает запас:

```cpp
alignas(32) static uint8_t packet[64];
uart.Receive_DMA(packet, 37);   // до 37 байт в 64-байтном выровненном буфере
```

Стоимость зависит от числа линий, записи «грязных» данных и барьеров; замеров нет.

**Ошибки:**

- Ошибки линии PE/FE/NE/ORE проверяются общей маской разрешённых ошибок.
- Ошибки DMA имеют приоритет над успешным завершением.
- Операция останавливается, получает `Error` и **один раз** вызывает `OnError(errors)` с общей маской (биты `Parity`, `Framing`, `Noise`, `Overrun`, `DmaTx`, `DmaRx`).
- Повреждённый RX-пакет никогда не помечается готовым.
- При блокирующем `Receive` callback вызывается в контексте вызывающего кода, при асинхронном обмене — из прерывания.

**Callback** (protected virtual):

`OnIdle()` только уведомляет об успешном завершении пакета по IDLE. Приём уже
остановлен, хвост/счётчик/кеш обработаны, статус и ready опубликованы.
`OnDmaRxComplete()` аналогично уведомляет о заполнении буфера, не вызывая
`OnIdle()`. `OnDmaTxComplete()` выполняется после очистки DMA и установки TX OK.
Старые вызовы этих пустых базовых методов допустимы, но обработки не выполняют.
Ошибочный пакет вызывает `OnError()`; содержимое такого буфера использовать
нельзя. Новых флагов состояния, `Process()` или циклов ожидания нет.

Приём через IRQ и остановленный DMA завершаются разными функциями с одной общей
публикацией результата. Байтовые callbacks только перемещают байты; готовность
пакета публикуется один раз после их возврата. Заполнение буфера хвостом IDLE
не вызывает повторного отключения и публикации. При DMA IDLE на F4/F7 источники
UART RX/DMAR отключаются один раз (по одному изменению CR1 и CR3), затем DMA IRQ
выполняет только передачу буфера процессору. При обычном DMA TC длина уже равна
вместимости, NDTR/CNDTR не читается; после IDLE читается итоговый остаток.

| Callback | Тип | Правило переопределения |
| --- | --- | --- |
| `OnRxByte`, `OnTxEmpty` | обработка байтового буфера | вызывайте базовую реализацию для записи/указателей/счётчиков |
| `OnIdle`, `OnDmaTxComplete`, `OnDmaRxComplete`, `OnTC`, `OnError` | уведомления после штатной обработки | по умолчанию пустые; вызов базовой реализации не нужен |

Из callback нельзя менять реестр и вызывать `SetUp()`/`DeInit()`.

**Внутри прерывания USART:** источники и разрешения DMA проверяются по одному начальному снимку status/CR1/CR3. На F4/F7 один RX-байт читается без повторной проверки RXNE. TCIE и TC перечитываются после обработки TX: callback может включить TCIE, а запись DR/TDR сбрасывает TC.

**FIFO (только G0):** проверяется по возможностям конкретного USART. Код `FIFO_TH::all` исправлен на 5 (RX — полный FIFO, TX — пустой). Oversampling остаётся ×16, режим ×8 не добавлен.

**Работа за один вход в прерывание ограничена** глубиной FIFO в каждом направлении: 8 слов на G0 с включённым FIFO, иначе один байт. При IDLE обычное чтение RX пропускается; хвост с тем же пределом обрабатывает путь завершения IRQ/DMA до уведомления `OnIdle`. Новые байты не ожидаются: остаток обслуживается следующими прерываниями; после IDLE поступления относятся к следующему пакету.

**Формат кадра** выбирается один раз: `SetUp(fifo, tx_threshold, rx_threshold, FrameFormat::E8_1 / O8_1 / N8_2 / N8_1)`. Во всех вариантах 8 бит данных; чётность занимает девятый аппаратный бит слова. Старый `SetParity()` по-прежнему игнорирует аргумент — используйте `FrameFormat`.

---

## 7. Совместимость

- USART1 DMA2_Stream5 / CH4 называется `RX_DMA2_Stream5` — это маршрут RX.
- Добавлены `DMAReq` для UART7/8 и G0 USART3…6; исправлены AF USART1/4 на G0 и общие IRQ.
- Добавлены `DMA_Sx::CanUseRequest()` и `GetErrorFlags()`; остальные потребители DMA не менялись.
- Код, который рассчитывал на сброс флага в `GetDataReceivedCount()`, теперь должен явно вызывать `ClearDataReceivedFlag()`.
- Конструктор Modbus переведён на те же описания запросов; актуальные правила передачи/IDLE — в [Modbus.md](Modbus.md).

---

## 8. Источники для таблиц

- [ST USART F7 — источники тактирования](https://www.st.com/resource/en/product_training/STM32F7_Peripheral_USART.pdf)
- [ST RM0444 — RCC/USART G0x1](https://www.st.com/resource/en/reference_manual/dm00371828.pdf)
- [ST G0 workshop — глубина FIFO USART](https://www.st.com/content/dam/AME/2019/STM32G0-q1-webinar-workshops/STM32G0_Workshop_Presentation.pdf)
- [ST AN4031 — таблицы DMA F4/F7](https://www.st.com/resource/en/application_note/dm00046011-using-the-stm32f2-stm32f4-and-stm32f7-series-dma-controller-stmicroelectronics.pdf)
- [ST G0 LL DMAMUX — идентификаторы запросов](https://raw.githubusercontent.com/STMicroelectronics/stm32g0xx-hal-driver/master/Inc/stm32g0xx_ll_dmamux.h)
- [ST G071 datasheet — AF USART1/4](https://www.st.com/resource/en/datasheet/stm32g071kb.pdf)
- [ST G0B1 datasheet — AF USART3…6](https://www.st.com/resource/en/datasheet/stm32g0b1me.pdf)
- [ST AN4839 — кеш L1 на STM32F7/H7](https://www.st.com/resource/en/application_note/an4839-level-1-cache-on-stm32f7-series-and-stm32h7-series-stmicroelectronics.pdf)

---

## 9. Примеры использования

**Минимальный порт (блокирующий/IRQ):**

```cpp
#include "uart.hpp"
using U = USART::_3;

constinit USART port(U::instance, 115200, U::TX::PD8, U::RX::PD9);

// В main, после инициализации System/RCC:
if (port.SetUp() != SysInitStatus::InitOK)
    System::DebugTrap("UART setup");
```

**Приём пакетов через DMA (G0):**

```cpp
// G0: физические каналы выбираются явно, объекты DMA принадлежат UART.
constinit USART uart(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7,
                     DMA1_Channel4_BASE, DMA1_Channel5_BASE);
alignas(32) static uint8_t packet[128];

if (uart.SetUp() == SysInitStatus::InitOK)
    uart.Receive_DMA(packet, sizeof(packet));

// Основной цикл:
if (uart.GetDataReceivedFlag()) {
    uint32_t count = uart.GetDataReceivedCount();
    // Обработать packet[0..count) до повторного запуска.
    uart.ClearDataReceivedFlag();
    uart.Receive_DMA(packet, sizeof(packet));
}
```

На F4/F7 в позициях DMA передайте `USART::_1::DMA::TX_DMA2_Stream7` и `USART::_1::DMA::RX_DMA2_Stream5`.

Проверяйте каждый возвращаемый статус.

---

## 10. Угловые случаи

| Ситуация | Поведение |
| --- | --- |
| IDLE на линии | Завершает пакет и останавливает RX DMA. Это **не** строгие интервалы Modbus RTU t1.5/t3.5. |
| G0: IDLE до обслуживания последнего запроса DMA | Недопустимо — последний запрос должен быть обслужен до остановки по IDLE. |
| Завершение TX DMA | Это не физическое завершение — ждите TC перед снятием DE или сменой скорости. |
| Байты между пакетами | Могут потеряться или вызвать ORE до следующего `Receive`. |
| Владение буфером/кеш-линиями, согласование с IRQ | Ответственность приложения. |

---

## См. также

- [Initialization.md](Initialization.md)
- [DMA.md](DMA.md)
- [GPIO.md](GPIO.md)
- [IRQ_Registry.md](IRQ_Registry.md)
- [Modbus.md](Modbus.md)
