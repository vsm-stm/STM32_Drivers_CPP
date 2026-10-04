# SPI

*[English](#english) · [Русский](#русский)*

---

## English

## Contents

1. [Why this is needed](#1-why-this-is-needed)
2. [Pins and the Master/Slave role in their configuration](#2-pins-and-the-masterslave-role-in-their-configuration)
3. [Software vs Hardware NSS](#3-software-vs-hardware-nss)
4. [Three transfer modes](#4-three-transfer-modes)
5. [HandleIRQ() — why the enable bits are checked, not just the flags](#5-handleirq--why-the-enable-bits-are-checked-not-just-the-flags)
6. [Receiving without TX data — RXONLY and "0xFF dummy"](#6-receiving-without-tx-data--rxonly-and-0xff-dummy)
7. [DMA Integration](#7-dma-integration)
8. [Virtual callbacks](#8-virtual-callbacks)
9. [Usage examples](#9-usage-examples)
10. [Edge cases](#10-edge-cases)

---

## 1. Why this is needed

`SPI` is one class per hardware SPI instance, working on STM32G0/F4/F7 with no changes to
application code — architecturally almost a mirror of [`USART`](UART.md): it inherits from
`IIRQHandler`, uses the `spi_table[]` table (peripheral → clk_reg/clk_bit/bus_clk/irq, the same
pattern as `usart_table`/`tim_table`), auto-registers/unregisters with `IRQ_Registry` via
`IRQ_en()`, and has three transfer modes (blocking/IRQ/DMA).

```cpp
SPI spi(SPI1, SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7);
spi.SetUp(SPI::Master_sel::Master, SPI::NSS_ctrl::Software,
          SPI::TYPE::TX, SPI::Data_frame_format::Byte,
          SPI::Frame_Format::MSB, SPI::cPolPha::None, SPI::BaudRate::DIV4);
spi.Send(buf, sizeof(buf), 1000);
```

The difference from `USART` in the register abstraction is minimal: F4/F7/G0 all use the same
`SPI_DR`/`SPI_SR` (no need to split into `TDR`/`RDR`/`ISR`/`ICR` as in UART); the only
family-specific split is the frame-width field (`CR1::DFF` on F4 vs. `CR2::DS` on F7/G0, see §2).

---

## 2. Pins and the Master/Slave role in their configuration

```cpp
explicit SPI(SPI_TypeDef* spix, PIN sck, PIN mosi, PIN miso = PIN{}, PIN ss = PIN{});
```

Like `USART`/`TIM`, the constructor checks the `periph_base` of every pin passed in against
`spix` and calls `System::DebugTrap()` on a mismatch (see
[GPIO.md §6](GPIO.md#6-constexpr-таблицы-пинов-и-защита-от-опечаток)).

Pin configuration in `SetHard()` **depends on the role** (Master/Slave), rather than being the
same for everyone:

| Pin | Master | Slave |
|-----|--------|-------|
| SCK | `AF_PushPull` | `AF_PushPull` |
| MOSI | `AF_PushPull` (the master drives the line) | `AF_OD_PulUp` (the slave only listens on its own MOSI line, physically pulled up) |
| MISO | `AF_OD_PulUp` (receives from the slave) | `AF_PushPull` (the slave drives the response itself) |
| SS (Hard NSS) | `AF_PushPull` | `AF_PushPull` |
| SS (Software, Master) | `OUTPUT_PushPull` (manages slave selection itself) | — |
| SS (Software, Slave) | — | `INPUT_NO_Pull` (listens for who selected it) |

Key detail: MOSI/MISO are configured as open-drain with a pull-up (`AF_OD_PulUp`) for the role in
which the line is **not** an output of the current device — this protects against a driver
conflict on the bus if, by mistake, two devices simultaneously decide they're the master.

---

## 3. Software vs Hardware NSS

```cpp
enum class NSS_ctrl { Hard, Software };
```

- **`Hard`** — hardware NSS: the `SS` line is connected to a pin, `SPI_CR2_SSOE` enables
  automatic hardware control of it (Master mode only).
- **`Software`** — `SlaveSelect()` manages the pin (`_ss`) manually:

```cpp
inline void SlaveSelect(FunctionalState en) {
    if (nss_ctrl != NSS_ctrl::Software) return;
    if (Master_slave == Master_sel::Master) _ss.SetLevel(!en);   // GPIO: LOW = selected
    else /* slave */ { if (en) SPIx->CR1 &= ~SPI_CR1_SSI; else SPIx->CR1 |= SPI_CR1_SSI; }
}
```

On Master + Software, this function toggles an ordinary GPIO. On Slave + Software, it instead
touches the internal `SSI` bit (Slave Select Internal) — a slave device can't physically "select
itself", so software NSS emulation for a slave means directly overriding the state that would
otherwise be read from the real `SS` pin, rather than driving a pin.

Every blocking/IRQ/DMA transfer (`Send`, `Send_IRQ`, `SendDMA`, ...) calls `SlaveSelect(ENABLE)`
itself before starting and `SlaveSelect(DISABLE)` after finishing — regardless of the NSS mode,
the call is harmless (with `Hard` it simply does nothing, see the `nss_ctrl != Software` check at
the top).

---

## 4. Three transfer modes

Just as in [UART.md §4](UART.md#4-три-режима-передачи):

| Mode | Methods | Direction |
|-------|--------|--------------|
| **Blocking** | `Send()`, `Receive()`, `Send_Receive()` | TX, RX-only (via a temporary `RXONLY`), full duplex |
| **IRQ** | `Send_IRQ()`, `Receive_IRQ()`, `SendReceive_IRQ()` | the same three variants, non-blocking |
| **DMA** | `SendDMA()`, `Receive_DMA()`, `ReceiveDMA()`, `SendReceive_DMA()` | TX-only, RX-only (no TX DMA), full duplex with TX=dummy, full duplex with real data |

A feature of SPI (unlike UART) is that it is a **synchronous** bus: receiving without
simultaneously transmitting still requires someone to generate the clock signal. Hence:

- `Receive()`/`Receive_IRQ()`/`Receive_DMA()` — temporarily enable `RXONLY` (`CR1_RXONLY`), under
  which the SPI itself generates the clock without any real transmission on MOSI, and clear it
  once finished.
- `ReceiveDMA()` (note the difference in name from `Receive_DMA()`!) — does **not** use `RXONLY`;
  instead it drives dummy `0xFF` bytes onto MOSI through a separate DMA TX channel, while
  simultaneously receiving real data on MISO through a DMA RX channel (see §6). Both methods
  solve the same problem — "receive without caring about the TX content" — but by different
  means: `Receive_DMA()` doesn't need a TX DMA channel at all, while `ReceiveDMA()` requires both.

---

## 5. HandleIRQ() — why the enable bits are checked, not just the flags

```cpp
void SPI::HandleIRQ() {
    // ... DMA TC flags first (see §7) ...
    uint32_t cr2 = SPIx->CR2;
    uint32_t sr  = SPIx->SR;
    if ((cr2 & SPI_CR2_RXNEIE) && (sr & SR_RXNE)) while (SPIx->SR & SR_RXNE) OnRxByte(RXD());
    if ((cr2 & SPI_CR2_TXEIE)  && (sr & SR_TXE))  OnTxEmpty();
    if ((cr2 & SPI_CR2_ERRIE)  && (sr & SR_ERR))  OnError();
}
```

Unlike [`TIM::HandleIRQ()`](TIM.md#5-timhandleirq--база-для-всех-подклассов) and
`USART::HandleIRQ()`, which check only the status flag itself, here the corresponding **enable**
bit in `CR2` (`RXNEIE`/`TXEIE`/`ERRIE`) is **additionally** checked. The reason is that `SR_TXE`
is physically set almost all the time (the transmit data register is empty most of the time —
that's the normal state of an idle bus), so checking the flag alone would make `HandleIRQ()` call
`OnTxEmpty()` on **any** interrupt on this SPI, including one that is really meant only for RX
(for example, `Receive_IRQ()` enables only `RXNEIE`, leaving `TXEIE` untouched) — without checking
`cr2 & SPI_CR2_TXEIE`, the TX-only callback would also fire on purely RX transactions.

---

## 6. Receiving without TX data — RXONLY and "0xFF dummy"

Two different techniques for the same practical task ("receive data on MISO without caring what
goes out on MOSI"); the choice depends on whether a TX DMA channel is available:

```
RXONLY (Receive/Receive_IRQ/Receive_DMA):           TX+RX DMA (ReceiveDMA):
  CR1 |= RXONLY                                       DMA TX -> MOSI: 0xFF, 0xFF, 0xFF, ... (MINC=DISABLE!)
  CR1 |= SPE  (the clock starts immediately)          DMA RX <- MISO: real data (MINC=ENABLE)
  # SPI itself generates the clock with no real TX activity   # the clock comes from REAL TX activity,
                                                                # just with dummy data
```

`_dma_tx->MINC(DISABLE)` in `ReceiveDMA()` is the key detail: the DMA TX source address is **not**
incremented — the same byte, `spi_dummy_byte` (a static constant), is sent `len` times in a row —
there's no need for a buffer of `len` copies of `0xFF`; a single byte at a fixed address is
enough. After `ReceiveDMA()`, `SendDMA()` restores `_dma_tx->MINC(ENABLE)` inside
`OnDmaRxComplete()` — otherwise the next ordinary `SendDMA()` call (with real data, where
incrementing is needed) would inherit `MINC = DISABLE` from the previous `ReceiveDMA()`.

---

## 7. DMA Integration

```cpp
void AttachDMA(DMA_Sx* tx = nullptr, DMA_Sx* rx = nullptr);
```

Structurally identical to [`USART::AttachDMA()`](UART.md#8-dma-интеграция) — the
`spi_dma_req_table` table gives `tx_req`/`rx_req` by `SPIx` pointer, `DMA_Sx::StreamSettings` is
configured for `&SPIx->DR` with `Byte` width, and `this` is registered as the DMA TC handler (and
here too with a check for a shared vector: `if (!tx || tx->GetIRQn() != rx->GetIRQn())` before
registering again for RX).

`HandleIRQ()` checks both DMA channels **before** the SPI flags (see §5) — the same way as in
[`USART::HandleIRQ()`](UART.md#6-handleirq--один-обработчик-на-uart-и-на-dma):

```cpp
if (_dma_tx && _dma_tx->GetTC_Flag()) { ...; OnDmaTxComplete(); }
if (_dma_rx && _dma_rx->GetTC_Flag()) { ...; OnDmaRxComplete(); }
```

**The order of completion matters in full duplex**: `OnDmaTxComplete()`, when RX is active
(`_rx_active == true`), **does nothing** (`return`s right after `DMA_TX(DISABLE)`) — all final
cleanup (clearing `SPE`, `SlaveSelect(DISABLE)`, resetting status) is deferred to
`OnDmaRxComplete()`, because TX can physically finish a few cycles before the last byte received
on MISO reaches memory via RX DMA — the bus should only be closed once **both** directions are
done.

---

## 8. Virtual callbacks

```cpp
virtual void OnTxEmpty();          // TX IRQ mode: pushes the next byte or finishes the transfer
virtual void OnRxByte(uint8_t);    // RX IRQ mode: receives a byte, tracks the end of the buffer
virtual void OnError();            // OVR/MODF/CRCERR — an empty stub by default
virtual void OnDmaTxComplete();    // DMA TX finished (see §7 about ordering in duplex)
virtual void OnDmaRxComplete();    // DMA RX finished — final bus cleanup
```

The same principle as in [UART.md §10](UART.md#10-виртуальные-колбэки--расширение-без-переписывания-handleirq):
`HandleIRQ()` is `final` and isn't overridden; instead, a subclass overrides the specific callback
it needs, without touching the DMA/CR2/SR dispatch logic.

```cpp
struct MySPI : SPI {
    using SPI::SPI;
    void OnTxEmpty() override { /* custom logic for feeding the next byte */ }
};
```

**`OnError()` does nothing by default** — the only one of the five callbacks with no meaningful
default implementation; `SR_ERR = OVR | MODF | CRCERR` combines three different hardware errors
into a single bit for the `HandleIRQ()` check, but doesn't distinguish between them on the user's
behalf — an override of `OnError()` must read `SPIx->SR` itself if it needs to know exactly which
error occurred.

---

## 9. Usage examples

### Blocking transfer (Master, Software NSS)

```cpp
SPI spi(SPI1, SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7, PIN{}, SPI::_1::SS::PA4);
spi.SetUp(SPI::Master_sel::Master, SPI::NSS_ctrl::Software, SPI::TYPE::TX,
          SPI::Data_frame_format::Byte, SPI::Frame_Format::MSB,
          SPI::cPolPha::None, SPI::BaudRate::DIV4);
spi.Send(buf, sizeof(buf), /*timeout_ms=*/100);
```

### Blocking full duplex (exchanging with a sensor via one request-response)

```cpp
uint8_t tx[4] = {0x9F, 0, 0, 0};   // for example, a JEDEC ID request for SPI Flash
uint8_t rx[4];
spi.Send_Receive(tx, rx, sizeof(tx), 1000);
```

### IRQ-driven TX with an overridden callback

```cpp
struct MySPI : SPI {
    using SPI::SPI;
    void OnTxEmpty() override { SPI::OnTxEmpty(); /* + custom telemetry */ }
};
MySPI spi(SPI1, SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7);
spi.SetUp(...);
spi.Send_IRQ(buf, sizeof(buf));
```

### DMA TX (for example, driving a matrix display through an SPI bridge)

```cpp
DMA_Sx dma_tx(DMA1_Channel3);   // G0: any free channel
spi.SetUp(...);
spi.AttachDMA(&dma_tx, nullptr);
spi.SendDMA(framebuffer, sizeof(framebuffer));
```

### Full duplex DMA — receiving arbitrary data, reading a slave's register

```cpp
DMA_Sx dma_tx(DMA1_Channel3), dma_rx(DMA1_Channel4);
spi.AttachDMA(&dma_tx, &dma_rx);
spi.ReceiveDMA(rx_buf, len);   // TX = 0xFF dummy automatically, RX = rx_buf
```

---

## 10. Edge cases

- **`ReceiveDMA()` requires BOTH DMA channels** (TX to generate the clock with dummy bytes, RX
  for receiving) — `SysStatus::Error` if even one isn't connected via `AttachDMA()`.
  `Receive_DMA()` (with an underscore, don't confuse them!) requires only the RX channel — it
  works via `RXONLY`, without a dummy TX.
- **`_dma_tx->MINC(DISABLE)` set in `ReceiveDMA()` is only restored in `OnDmaRxComplete()`** — if
  you override this callback without calling the base implementation, the next `SendDMA()` will
  inherit `MINC = DISABLE` and will send the same byte over and over instead of the buffer.
- **`OnDmaTxComplete()` doesn't finish the transfer when `_rx_active == true`** — all bus
  cleanup is deferred to `OnDmaRxComplete()` (see §7); when overriding `OnDmaTxComplete()`, don't
  add `SlaveSelect(DISABLE)` there for the duplex case — that would prematurely deselect the slave
  before reception is complete.
- **Software NSS on a Slave doesn't touch a real pin** — it uses `SPI_CR1_SSI`, a software "as
  if selected" flag, rather than the physical level on the pin (see §3) — a slave device can't
  select itself in hardware.
- **`HandleIRQ()` checks the enable bit (`CR2`) together with the flag (`SR`)** — unlike
  `TIM`/`USART`, where only the status flag itself is checked (see §5); when porting the pattern
  to a new driver, remember that not all SPI status flags are rare while idle.
- **`OnError()` doesn't distinguish OVR/MODF/CRCERR** — `SR_ERR` combines three bits into one
  mask for a single check in `HandleIRQ()`; the specific cause needs to be determined inside an
  overridden `OnError()` on your own.

---

## See also

- [IRQ_Registry.md](IRQ_Registry.md) — the mechanism `IRQ_en()` registration and `HandleIRQ()`
  dispatch are built on — the same pattern as `USART`/`TIM`/`RTC`.
- [DMA.md](DMA.md) — the `DMA_Sx` device, the `AttachDMA()` pattern shared with UART.md (§7 here).
- [GPIO.md](GPIO.md) — `PIN`, the compile-time `SPI::_N::SCK/MOSI/MISO/SS` tables (see §2 here
  about configuring them by Master/Slave role).
- [System.md](System.md) — `SysStatus`, `System::GetTick()` (blocking-transfer timeouts).
- [UART.md](UART.md) — an architecturally almost mirror-image driver, useful for comparing the
  register abstraction and the three transfer modes.

---

## Русский

## Содержание

1. [Зачем это нужно](#1-зачем-это-нужно)
2. [Пины и роль Master/Slave в их настройке](#2-пины-и-роль-masterslave-в-их-настройке)
3. [Software vs Hardware NSS](#3-software-vs-hardware-nss)
4. [Три режима передачи](#4-три-режима-передачи)
5. [HandleIRQ() — почему проверяются биты разрешения, а не только флаги](#5-handleirq--почему-проверяются-биты-разрешения-а-не-только-флаги)
6. [Приём без TX-данных — RXONLY и "0xFF dummy"](#6-приём-без-tx-данных--rxonly-и-0xff-dummy)
7. [DMA-интеграция](#7-dma-интеграция)
8. [Виртуальные колбэки](#8-виртуальные-колбэки)
9. [Примеры использования](#9-примеры-использования)
10. [Угловые случаи](#10-угловые-случаи)

---

## 1. Зачем это нужно

`SPI` — один класс на один аппаратный SPI, работающий на STM32G0/F4/F7 без изменений в коде
приложения — архитектурно почти зеркало [`USART`](UART.md): наследует `IIRQHandler`, использует
таблицу `spi_table[]` (peripheral → clk_reg/clk_bit/bus_clk/irq, тот же паттерн, что
`usart_table`/`tim_table`), auto-register/unregister в `IRQ_Registry` через `IRQ_en()`, и три
режима передачи (блокирующий/IRQ/DMA).

```cpp
SPI spi(SPI1, SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7);
spi.SetUp(SPI::Master_sel::Master, SPI::NSS_ctrl::Software,
          SPI::TYPE::TX, SPI::Data_frame_format::Byte,
          SPI::Frame_Format::MSB, SPI::cPolPha::None, SPI::BaudRate::DIV4);
spi.Send(buf, sizeof(buf), 1000);
```

Отличие от `USART` в регистровой абстракции — минимальное: F4/F7/G0 используют один и тот же
`SPI_DR`/`SPI_SR` (не нужно разделять на `TDR`/`RDR`/`ISR`/`ICR`, как в UART), единственный
семейный разрыв — поле ширины кадра (`CR1::DFF` на F4 против `CR2::DS` на F7/G0, см. §2).

---

## 2. Пины и роль Master/Slave в их настройке

```cpp
explicit SPI(SPI_TypeDef* spix, PIN sck, PIN mosi, PIN miso = PIN{}, PIN ss = PIN{});
```

Как и `USART`/`TIM`, конструктор проверяет `periph_base` каждого переданного пина против `spix`
и вызывает `System::DebugTrap()` при несовпадении (см.
[GPIO.md §6](GPIO.md#6-constexpr-таблицы-пинов-и-защита-от-опечаток)).

Настройка пинов в `SetHard()` **зависит от роли** (Master/Slave), а не одинакова для всех:

| Пин | Master | Slave |
|-----|--------|-------|
| SCK | `AF_PushPull` | `AF_PushPull` |
| MOSI | `AF_PushPull` (ведущий гонит линию) | `AF_OD_PulUp` (ведомый только слушает свою же MOSI-линию, физически подтянутую) |
| MISO | `AF_OD_PulUp` (принимает от ведомого) | `AF_PushPull` (ведомый сам гонит ответ) |
| SS (Hard NSS) | `AF_PushPull` | `AF_PushPull` |
| SS (Software, Master) | `OUTPUT_PushPull` (сам управляет выбором ведомого) | — |
| SS (Software, Slave) | — | `INPUT_NO_Pull` (слушает, кто его выбрал) |

Ключевая деталь: MOSI/MISO настраиваются в открытый сток с подтяжкой (`AF_OD_PulUp`) для той
роли, в которой линия **не** является выходом текущего устройства — это защищает от конфликта
драйверов на шине, если по ошибке два устройства одновременно решат, что они ведущие.

---

## 3. Software vs Hardware NSS

```cpp
enum class NSS_ctrl { Hard, Software };
```

- **`Hard`** — аппаратный NSS: линия `SS` подключена к выводу, `SPI_CR2_SSOE` включает
  автоматическое управление им железом (только в режиме Master).
- **`Software`** — `SlaveSelect()` управляет пином (`_ss`) вручную:

```cpp
inline void SlaveSelect(FunctionalState en) {
    if (nss_ctrl != NSS_ctrl::Software) return;
    if (Master_slave == Master_sel::Master) _ss.SetLevel(!en);   // GPIO: LOW = выбран
    else /* slave */ { if (en) SPIx->CR1 &= ~SPI_CR1_SSI; else SPIx->CR1 |= SPI_CR1_SSI; }
}
```

На Master + Software эта функция дёргает обычный GPIO. На Slave + Software она вместо этого
трогает внутренний бит `SSI` (Slave Select Internal) — ведомое устройство не может физически
"выбрать само себя", поэтому программная эмуляция NSS для ведомого означает не выставление
вывода, а прямую подмену состояния, которое иначе читалось бы с реального пина `SS`.

Каждая блокирующая/IRQ/DMA-передача (`Send`, `Send_IRQ`, `SendDMA`, ...) сама вызывает
`SlaveSelect(ENABLE)` перед стартом и `SlaveSelect(DISABLE)` после завершения — независимо от
режима NSS, вызов безвреден (при `Hard` он просто ничего не делает, см. проверку
`nss_ctrl != Software` в начале).

---

## 4. Три режима передачи

Как и в [UART.md §4](UART.md#4-три-режима-передачи):

| Режим | Методы | Направление |
|-------|--------|--------------|
| **Блокирующий** | `Send()`, `Receive()`, `Send_Receive()` | TX, RX-only (через временный `RXONLY`), полный дуплекс |
| **IRQ** | `Send_IRQ()`, `Receive_IRQ()`, `SendReceive_IRQ()` | те же три варианта, неблокирующие |
| **DMA** | `SendDMA()`, `Receive_DMA()`, `ReceiveDMA()`, `SendReceive_DMA()` | TX-only, RX-only (без TX DMA), полный дуплекс с TX=dummy, полный дуплекс с реальными данными |

Особенность SPI (в отличие от UART) — это **синхронная** шина: приём без одновременной передачи
всё равно требует, чтобы кто-то генерировал тактовый сигнал. Отсюда:

- `Receive()`/`Receive_IRQ()`/`Receive_DMA()` — временно включают `RXONLY` (`CR1_RXONLY`), под
  которым сам SPI генерирует клок без реальной передачи по MOSI, и снимают его после завершения.
- `ReceiveDMA()` (обратите внимание на разницу в названии с `Receive_DMA()`!) — **не**
  использует `RXONLY`, а вместо этого гонит по MOSI фиктивные байты `0xFF` через отдельный
  DMA TX-канал, одновременно принимая настоящие данные по MISO через DMA RX-канал (см. §6). Both
  методов решают одну и ту же задачу "принять, не заботясь о содержимом TX", но разными
  средствами — `Receive_DMA()` не требует TX DMA-канала вообще, `ReceiveDMA()` требует оба.

---

## 5. HandleIRQ() — почему проверяются биты разрешения, а не только флаги

```cpp
void SPI::HandleIRQ() {
    // ... сначала DMA TC-флаги (см. §7) ...
    uint32_t cr2 = SPIx->CR2;
    uint32_t sr  = SPIx->SR;
    if ((cr2 & SPI_CR2_RXNEIE) && (sr & SR_RXNE)) while (SPIx->SR & SR_RXNE) OnRxByte(RXD());
    if ((cr2 & SPI_CR2_TXEIE)  && (sr & SR_TXE))  OnTxEmpty();
    if ((cr2 & SPI_CR2_ERRIE)  && (sr & SR_ERR))  OnError();
}
```

В отличие от [`TIM::HandleIRQ()`](TIM.md#5-timhandleirq--база-для-всех-подклассов) и
`USART::HandleIRQ()`, которые проверяют только сам флаг статуса, здесь **дополнительно**
проверяется соответствующий бит **разрешения** в `CR2` (`RXNEIE`/`TXEIE`/`ERRIE`). Причина —
`SR_TXE` физически почти всегда взведён (регистр данных на передачу пуст большую часть времени,
это нормальное состояние простаивающей шины), поэтому чисто по флагу `HandleIRQ()` вызывал бы
`OnTxEmpty()` при **любом** прерывании на этом SPI, включая то, что реально предназначено только
для RX (например, `Receive_IRQ()` включает только `RXNEIE`, не трогая `TXEIE`) — без проверки
`cr2 & SPI_CR2_TXEIE` TX-only колбэк срабатывал бы и на чисто RX-транзакциях.

---

## 6. Приём без TX-данных — RXONLY и "0xFF dummy"

Две разные техники для одной и той же практической задачи ("принять данные по MISO, не заботясь,
что уходит на MOSI"), выбор зависит от того, есть ли доступный TX DMA-канал:

```
RXONLY (Receive/Receive_IRQ/Receive_DMA):           TX+RX DMA (ReceiveDMA):
  CR1 |= RXONLY                                       DMA TX -> MOSI: 0xFF, 0xFF, 0xFF, ... (MINC=DISABLE!)
  CR1 |= SPE  (клок стартует немедленно)              DMA RX <- MISO: настоящие данные (MINC=ENABLE)
  # SPI сам генерирует клок без реальной TX-активности   # клок идёт от РЕАЛЬНОЙ TX-активности,
                                                          # просто данные фиктивные
```

`_dma_tx->MINC(DISABLE)` в `ReceiveDMA()` — ключевая деталь: адрес источника DMA TX **не**
увеличивается, один и тот же байт `spi_dummy_byte` (статическая константа) отправляется `len`
раз подряд — не нужен буфер из `len` копий `0xFF`, один байт с фиксированным адресом справляется.
После `ReceiveDMA()` `SendDMA()` восстанавливает `_dma_tx->MINC(ENABLE)` в `OnDmaRxComplete()` —
иначе следующий вызов обычного `SendDMA()` (с реальными данными, где инкремент нужен) унаследовал
бы `MINC = DISABLE` от предыдущего `ReceiveDMA()`.

---

## 7. DMA-интеграция

```cpp
void AttachDMA(DMA_Sx* tx = nullptr, DMA_Sx* rx = nullptr);
```

Структурно идентично [`USART::AttachDMA()`](UART.md#8-dma-интеграция) — таблица
`spi_dma_req_table` даёт `tx_req`/`rx_req` по указателю `SPIx`, `DMA_Sx::StreamSettings`
настраивается на `&SPIx->DR` с шириной `Byte`, регистрируется `this` как обработчик DMA TC
(и здесь тоже с проверкой общего вектора: `if (!tx || tx->GetIRQn() != rx->GetIRQn())` перед
повторной регистрацией для RX).

`HandleIRQ()` проверяет оба DMA-канала **до** SPI-флагов (см. §5) — так же, как в
[`USART::HandleIRQ()`](UART.md#6-handleirq--один-обработчик-на-uart-и-на-dma):

```cpp
if (_dma_tx && _dma_tx->GetTC_Flag()) { ...; OnDmaTxComplete(); }
if (_dma_rx && _dma_rx->GetTC_Flag()) { ...; OnDmaRxComplete(); }
```

**Порядок завершения в полном дуплексе важен**: `OnDmaTxComplete()` при активном RX (`_rx_active
== true`) **ничего не делает** (`return` сразу после `DMA_TX(DISABLE)`) — вся финальная очистка
(снятие `SPE`, `SlaveSelect(DISABLE)`, сброс статуса) отложена до `OnDmaRxComplete()`, потому что
TX может физически завершиться на несколько тактов раньше, чем последний принятый по MISO байт
дойдёт до памяти через RX DMA — закрывать шину нужно только когда закончены **оба** направления.

---

## 8. Виртуальные колбэки

```cpp
virtual void OnTxEmpty();          // IRQ-режим TX: закачивает следующий байт или завершает передачу
virtual void OnRxByte(uint8_t);    // IRQ-режим RX: принимает байт, следит за концом буфера
virtual void OnError();            // OVR/MODF/CRCERR — по умолчанию пустая заглушка
virtual void OnDmaTxComplete();    // DMA TX завершён (см. §7 про порядок в дуплексе)
virtual void OnDmaRxComplete();    // DMA RX завершён — финальная очистка шины
```

Тот же принцип, что в [UART.md §10](UART.md#10-виртуальные-колбэки--расширение-без-переписывания-handleirq):
`HandleIRQ()` — `final`, не переопределяется; вместо этого наследник переопределяет нужный
колбэк, не трогая логику диспетчеризации DMA/CR2/SR.

```cpp
struct MySPI : SPI {
    using SPI::SPI;
    void OnTxEmpty() override { /* своя логика подачи следующего байта */ }
};
```

**`OnError()` по умолчанию ничего не делает** — единственный из пяти колбэков без
содержательной реализации по умолчанию; `SR_ERR = OVR | MODF | CRCERR` объединяет три разные
аппаратные ошибки в один бит для проверки в `HandleIRQ()`, но не различает их за пользователя —
переопределение `OnError()` должно само прочитать `SPIx->SR`, если нужно узнать, какая именно
ошибка произошла.

---

## 9. Примеры использования

### Блокирующая передача (Master, Software NSS)

```cpp
SPI spi(SPI1, SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7, PIN{}, SPI::_1::SS::PA4);
spi.SetUp(SPI::Master_sel::Master, SPI::NSS_ctrl::Software, SPI::TYPE::TX,
          SPI::Data_frame_format::Byte, SPI::Frame_Format::MSB,
          SPI::cPolPha::None, SPI::BaudRate::DIV4);
spi.Send(buf, sizeof(buf), /*timeout_ms=*/100);
```

### Полный дуплекс блокирующий (обмен с датчиком по одному запросу-ответу)

```cpp
uint8_t tx[4] = {0x9F, 0, 0, 0};   // например, JEDEC ID запрос для SPI Flash
uint8_t rx[4];
spi.Send_Receive(tx, rx, sizeof(tx), 1000);
```

### IRQ-driven TX с переопределённым колбэком

```cpp
struct MySPI : SPI {
    using SPI::SPI;
    void OnTxEmpty() override { SPI::OnTxEmpty(); /* + своя телеметрия */ }
};
MySPI spi(SPI1, SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7);
spi.SetUp(...);
spi.Send_IRQ(buf, sizeof(buf));
```

### DMA TX (например, вывод на матрицу через SPI-мост)

```cpp
DMA_Sx dma_tx(DMA1_Channel3);   // G0: любой свободный канал
spi.SetUp(...);
spi.AttachDMA(&dma_tx, nullptr);
spi.SendDMA(framebuffer, sizeof(framebuffer));
```

### Полный дуплекс DMA — приём произвольных данных, чтение регистра ведомого

```cpp
DMA_Sx dma_tx(DMA1_Channel3), dma_rx(DMA1_Channel4);
spi.AttachDMA(&dma_tx, &dma_rx);
spi.ReceiveDMA(rx_buf, len);   // TX = 0xFF dummy автоматически, RX = rx_buf
```

---

## 10. Угловые случаи

- **`ReceiveDMA()` требует ОБА DMA-канала** (TX для генерации клока фиктивными байтами, RX для
  приёма) — `SysStatus::Error`, если хотя бы один не подключён через `AttachDMA()`. `Receive_DMA()`
  (с подчёркиванием, не путать!) требует только RX-канал — работает через `RXONLY`, без dummy TX.
- **`_dma_tx->MINC(DISABLE)` в `ReceiveDMA()` восстанавливается только в `OnDmaRxComplete()`** —
  если переопределить этот колбэк без вызова базовой реализации, следующий `SendDMA()` унаследует
  `MINC = DISABLE` и будет слать один и тот же байт вместо буфера.
- **`OnDmaTxComplete()` при `_rx_active == true` не завершает передачу** — вся очистка шины
  отложена до `OnDmaRxComplete()` (см. §7); переопределяя `OnDmaTxComplete()`, не стоит
  добавлять туда `SlaveSelect(DISABLE)` для дуплексного случая — это преждевременно снимет
  выбор ведомого до завершения приёма.
- **Software NSS на Slave не трогает реальный вывод** — использует `SPI_CR1_SSI`, программный
  флаг "как будто выбран", а не физический уровень на пине (см. §3) — ведомое устройство не
  может выбрать себя аппаратно.
- **`HandleIRQ()` проверяет бит разрешения (`CR2`) вместе с флагом (`SR`)** — в отличие от
  `TIM`/`USART`, где проверяется только сам флаг статуса (см. §5); при переносе паттерна на
  новый драйвер стоит помнить, что не все флаги статуса SPI редки в простое.
- **`OnError()` не различает OVR/MODF/CRCERR** — `SR_ERR` объединяет три бита в одну маску для
  единственной проверки в `HandleIRQ()`; конкретную причину нужно определять внутри
  переопределённого `OnError()` самостоятельно.

---

## См. также

- [IRQ_Registry.md](IRQ_Registry.md) — механизм, на котором построена регистрация `IRQ_en()`
  и диспетчеризация `HandleIRQ()` — тот же паттерн, что у `USART`/`TIM`/`RTC`.
- [DMA.md](DMA.md) — устройство `DMA_Sx`, общий с UART.md паттерн `AttachDMA()` (§7 здесь).
- [GPIO.md](GPIO.md) — `PIN`, compile-time таблицы `SPI::_N::SCK/MOSI/MISO/SS` (см. §2 здесь про
  их настройку по ролям Master/Slave).
- [System.md](System.md) — `SysStatus`, `System::GetTick()` (таймауты блокирующих передач).
- [UART.md](UART.md) — архитектурно почти зеркальный драйвер, полезно для сравнения регистровой
  абстракции и трёх режимов передачи.
