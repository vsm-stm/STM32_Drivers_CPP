# SPI — Detailed Description

*[English](#english) · [Русский](#русский)*

---

## English

## Table of Contents

1. [Why This Is Needed](#1-why-this-is-needed)
2. [Constant Initialization and Constructor Diagnostics](#2-constant-initialization-and-constructor-diagnostics)
3. [Ownership and setup](#3-ownership-and-setup)
4. [Software CS, hardware NSS and shared buses](#4-software-cs-hardware-nss-and-shared-buses)
5. [Frames, directions and status](#5-frames-directions-and-status)
6. [Physical completion through Process()](#6-physical-completion-through-process)
7. [Slave: packet terminated by hardware NSS](#7-slave-packet-terminated-by-hardware-nss)
8. [Constructor-bound DMA, requests and fallback directions](#8-constructor-bound-dma-requests-and-fallback-directions)
9. [F7 cache and buffer handover](#9-f7-cache-and-buffer-handover)
10. [IRQ, errors, callbacks and cleanup](#10-irq-errors-callbacks-and-cleanup)
11. [Usage Examples](#11-usage-examples)
12. [Edge Cases](#12-edge-cases)
13. [See Also](#see-also)

---

## 1. Why This Is Needed

`SPI` drives one SPI peripheral as master or slave.

- The constructor fixes the **direction** (`TX`, `RX` or `TXRX`), the pins, an optional chip-select and optional DMA.
- Transfers can be blocking, interrupt-driven or DMA — all share one transfer engine.
- For asynchronous transfers, **`Process()`** in the main loop confirms that the last frame has really left the wire. Interrupt handlers never wait.

---

## 2. Constant Initialization and Constructor Diagnostics

Declare the object at file scope with `constinit` (or as `static constinit` inside a function). Arguments: `SPI::_N::instance`, direction, named SCK/MOSI/MISO pins, optional CS/NSS pin, optional TX/RX DMA (named `_N::DMA::...` routes on F4/F7, physical `DMAx_Channely_BASE` on G0 — the SPI supplies the request IDs).

What happens with a wrong argument (e.g. a MOSI pin of SPI2 on SPI1):

| Declaration | Result |
| --- | --- |
| `constinit` object | **Compilation error** with the reason text in the message. |
| Object without `constinit` | Compiles; at start-up the constructor stops in `System::DebugTrap(reason)`. |

Every check calls `STM32_CONFIG_ERROR("reason")`: at compile time a GCC error containing the reason, at run time `System::DebugTrap(text)`. `constinit` is required by the usage contract but not enforced.

`SetUp()` still checks hardware state and occupied resources. The object keeps the instance reference plus one register pointer; null means "not set up" (cleared after a failed first `SetUp()` and by `DeInit()`). Details: [Initialization.md](Initialization.md).

---

## 3. Ownership and setup

**One object controls one SPI peripheral.**

- Initialize System/RCC first.
- Call `SetUp()` once; call `DeInit()` before reconfiguring or destroying the object (the destructor does not do it).
- The SPI owns its DMA objects. Keep the SPI object and active buffers alive.

**Constructor:**

```cpp
constinit SPI bus(SPI::_1::instance, SPI::Direction::TXRX,
                  SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7, SPI::_1::MISO::PA6,
                  PIN(GPIOB_BASE, 0));   // optional software master CS
```

- SCK is required. A data pin may be omitted (`PIN{}`) if that direction is not used externally.
- Named AF pins are checked for peripheral ownership at compile time.
- After CS come the optional `DMAReq` for TX and RX.

**What the direction means:**

| Direction | Hardware mode | Allowed methods |
| --- | --- | --- |
| `TX` | bidirectional one-line output (BIDIMODE/BIDIOE); no RX work, no second DMA | `Send` |
| `RX` | full duplex, RXONLY cleared; master clocks with dummy frames from the TX interrupt, slave only receives | `Receive` |
| `TXRX` | full duplex | `Send` (RX discarded), `Receive` (dummy TX), `SendReceive` |

A method that does not fit the fixed direction returns `Error`.

**`SetUp()`** — clock, mode and frame settings (or the `Init_struct_Typedef` overload):

```cpp
SysInitStatus initialized = bus.SetUp(
    SPI::Master_sel::Master, SPI::NSS_ctrl::Software,
    SPI::Data_frame_format::Byte, SPI::Frame_Format::MSB,
    SPI::cPolPha::None, SPI::BaudRate::DIV8);
```

Check for `InitOK` before starting. `InitError` on: invalid enum values, missing SCK, hardware NSS without its pin, slave with software NSS, unsupported instance, repeated setup, or IRQ registration failure.

**Device coverage:**

- SPI1–SPI6 on F4/F7 and SPI1–SPI3 on G0, where CMSIS declares them.
- Named pins follow the device AF map — check that the pin is bonded in your package. SPI5 uses AF6 on F410/F411/F412/F413/F423 and AF5 on larger F4/F7.
- On G0 with SPI3, SPI2 and SPI3 share `SPI2_3_IRQn`.

**`SPI_USE_ATOMIC`** (in `spi.hpp`, default `0`): `1` switches control-register read-modify-write to ST `ATOMIC_*` macros. It protects single updates, not a whole transfer or buffer handover. DMA has its own `DMA_USE_ATOMIC`. Rebuild SPI and its users after changing it.

---

## 4. Software CS, hardware NSS and shared buses

| Configuration | Who drives chip select |
| --- | --- |
| Master, software NSS, CS pin passed | the driver: high after setup, low at start, high only after final completion/error/timeout |
| Master, software NSS, no CS pin | the application (all CS GPIOs) |
| Master, hardware NSS | hardware: SSM cleared, SSOE set; NSS follows SPI enable |
| Slave | hardware NSS required (SSM/SSOE cleared); the application configures EXTI on that pin to detect packet end |

**Shared bus:** use one SPI object and application-owned CS pins, so CS can stay low across several operations (command + response). Release CS on errors too.

```cpp
constinit SPI bus(SPI::_1::instance, SPI::Direction::TXRX,
                  SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7, SPI::_1::MISO::PA6);
PIN flash_cs(GPIOB, 0), adc_cs(GPIOB, 1);
flash_cs.SetUp(PIN::TYPE::OUTPUT_PushPull, PIN::OUTPUT_SPEED::Low, PIN::LVL::HIGH);
adc_cs.SetUp(PIN::TYPE::OUTPUT_PushPull, PIN::OUTPUT_SPEED::Low, PIN::LVL::HIGH);

// After a successful bus.SetUp(Master, Software):
uint8_t command[] = {0x9F};
uint8_t response[3];
flash_cs.SetLevel(0);
SysStatus result = bus.Send(command, 1);
if (result == SysStatus::OK) result = bus.Receive(response, 3);
flash_cs.SetLevel(1);
// Inspect result; the application chooses the device and protocol.
```

With IRQ/DMA transfers, release CS only after `Process()` reports physical completion — not right after a successful start.

**Devices with different settings** (CPOL/CPHA, divider, frame size) need explicit reconfiguration while idle: `DeInit()` + `SetUp()`. The constructor's direction and DMA stay the same. There is no device scheduler.

---

## 5. Frames, directions and status

**Frame size.** Every transfer has `uint8_t*` and `uint16_t*` overloads; use the one matching `SetUp(Byte / Half_Word)`. `len` is always **1…65535 frames**:

- `uint8_t` buffer → `len` bytes, 8-bit DR/DMA accesses.
- `uint16_t` buffer → `len` half-words (2·`len` bytes), 16-bit accesses, 2-byte alignment.
- On F7/G0 the RX FIFO threshold matches the frame size; there is no implicit packing.

**What each operation transmits:** `Send` sends your frames and discards RX; `Receive` sends dummy frames (`0xFF` / `0xFFFF`) and stores RX; `SendReceive` uses both buffers. `Receive_DMA` uses two-line reception with dummy clocking (RXONLY is not used).

| Mode | Methods | Returned `OK` means |
| --- | --- | --- |
| Blocking | `Send`, `Receive`, `Send_Receive` | transfer **finished** |
| IRQ | `Send_IRQ`, `Receive_IRQ`, `SendReceive_IRQ` | transfer **started** |
| DMA | `Send_DMA`, `Receive_DMA`, `SendReceive_DMA` | transfer **started** |

| Constructor direction | Operations | Servicing |
| --- | --- | --- |
| `TX` | `Send` | TX only: TX DMA or TX IRQ |
| `RX` | `Receive` | master: RX DMA/IRQ + dummy TX from IRQ; slave: RX only |
| `TXRX` | `Send`, `Receive`, `SendReceive` | both directions; a direction without DMA is served by the SPI IRQ |

DMA binding does not remove blocking or IRQ methods. Example — `RX` master on G0 with one DMA and no MOSI:

```cpp
constexpr SPI::DMAReq rx = DMA1_Channel3_BASE;
constinit SPI bus(SPI::_1::instance, SPI::Direction::RX, SPI::_1::SCK::PA5,
                  PIN{}, SPI::_1::MISO::PA6, PIN{}, {}, rx);
// Check SetUp(Master, Software), then Receive_DMA(buffer, capacity).
// Call Process() from main; use the buffer only after it returns OK.
```

**Status codes:**

| Result | When |
| --- | --- |
| `NotInit` | `SetUp()` has not succeeded |
| `Busy` | another operation is running |
| `Error` | null/zero arguments, more than 65535 frames, frame width or alignment mismatch, missing data pin or required DMA |
| `Timeout` | blocking call exceeded its budget |

- After `Error`/`Timeout` (and cleanup) a new transfer may start.
- A blocking call has **one** timeout for the whole call and needs a running System tick.
- Asynchronous calls have no internal deadline — the application enforces one and can cancel with `DeInit()`.

```cpp
// After a successful setup with Half_Word:
uint16_t tx[] = {0x1234, 0xABCD}, rx[2];
SysStatus result = bus.Send_Receive(tx, rx, 2);   // two 16-bit frames, not two bytes
```

---

## 6. Physical completion through Process()

> Neither TXE nor TX DMA "transfer complete" means the last frame has left the wire. The interrupt moves frames; DMA TC only means the buffer has been fed or filled.

After starting an IRQ/DMA transfer, call `Process()` regularly from `main`:

```cpp
if (bus.SendReceive_IRQ(tx, rx, 2) == SysStatus::OK) {
    // return to the main loop
}
// In main, while the transfer is active:
SysStatus state = bus.Process();
if (state == SysStatus::OK) {
    uint32_t received_frames = bus.GetDataReceivedCount();   // does not clear the count
    // RX data is now available to the CPU.
}
```

**What `Process()` checks (master):**

1. all planned TX/RX frames have been handled;
2. then the SPI itself — F4: TXE = 1 and BSY = 0; F7/G0: TX FIFO empty and BSY = 0.

A single BSY = 0 reading during an unfinished transfer is not enough — hence the order.

**What `Process()` does:** it never spins on BSY; while it is too early it returns `Busy`. When done, it stops sources/DMA, does the cache handover, releases the driver-managed CS, publishes `OK` and calls `OnComplete()`. Hardware errors call `OnError(mask)` after cleanup. Both notifications run in `main` (inside `Process()`), not in a DMA interrupt.

`GetStatus()` is a pure read and does none of this work. Blocking calls do not call `OnComplete()`; `OnError()` reports their hardware errors.

---

## 7. Slave: packet terminated by hardware NSS

In slave mode the **master** decides packet length; the end of a packet is NSS going high.

**Buffer capacity:**

- `len` is the buffer capacity, not a required packet length. Packets may be shorter or longer.
- The first `len` complete frames are stored; `GetDataReceivedCount()` reports how many (after `Process()` returns `OK`).
- Extra frames are dropped and `IsTruncated()` becomes `true`; the stored prefix is still a successful packet. Exactly `len` frames is **not** truncation.
- After the TX payload, the slave sends dummy `0xFF`/`0xFFFF` until NSS rises. This tail is fed by the TXE interrupt (also after TX DMA TC), so choose SCK with your interrupt latency in mind — continuous dummy output at maximum SCK is not guaranteed.

**Packet end:** prepare TX/RX **before** the master selects NSS. The application configures a rising-edge EXTI on the hardware NSS pin, acknowledges it and calls `NssReleased()`:

```cpp
constinit SPI slave(SPI::_1::instance, SPI::Direction::TXRX,
                    SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7, SPI::_1::MISO::PA6,
                    SPI::_1::SS::PA4);
static uint8_t packet[64];
// Check SetUp(Slave, Hard), then Receive_IRQ(packet, sizeof(packet)).

// In the application's EXTI handler, after checking/acknowledging the NSS flag:
slave.NssReleased();

// In main, after the transfer has started:
if (slave.Process() == SysStatus::OK) {
    uint32_t frames = slave.GetDataReceivedCount();
    bool truncated = slave.IsTruncated();
    // Use packet[0..frames), then re-arm and check the start result.
}
```

**What happens inside:**

- `NssReleased()` disables SPI sources and requests a DMA stop — no waiting, no cache work, no callbacks.
- F4/F7: each DMA interrupt collects its stop TC and final count; `Process()` stays `Busy` until both directions have stopped.
- G0: the last DMA request must already be serviced before the NSS stop; `Process()` reads the stopped count. An arbitrary mid-transfer abort does not give a valid length. On errors the whole packet is discarded.
- `Process()` invalidates the DMA-written prefix, drains the last RX FIFO tail and publishes the packet. It also resets the SPI to flush TX frames prefetched but never sent.
- Slave BSY is **not** used as a packet boundary — affected devices have a documented slave-BSY erratum.

**Timing rules:**

- The NSS EXTI, SPI and DMA handlers of this object must not preempt one another.
- The EXTI edge must be serviced before the next transaction, and the master must wait until `Process()` has run and the slave is re-armed.
- Polling NSS in the main loop cannot replace edge capture if the master can produce short high pulses.

**Integrity:** only complete frames are reported; an incomplete last frame and protocol validity are the application's job. OVR **before** the buffer is full means data loss → `Error`. OVR **after** it is full is truncation.

---

## 8. Constructor-bound DMA, requests and fallback directions

```cpp
// F4/F7: the chosen resource is visible in the name.
constexpr SPI::DMAReq tx = SPI::_1::DMA::TX_DMA2_Stream3;
constexpr SPI::DMAReq rx = SPI::_1::DMA::RX_DMA2_Stream0;
// G0 alternatives:
// constexpr SPI::DMAReq tx = DMA1_Channel2_BASE;
// constexpr SPI::DMAReq rx = DMA1_Channel3_BASE;

constinit SPI bus(SPI::_1::instance, SPI::Direction::TXRX, SPI::_1::SCK::PA5,
                  SPI::_1::MOSI::PA7, SPI::_1::MISO::PA6, PIN{}, tx, rx);
if (bus.SetUp(SPI::Master_sel::Master, SPI::NSS_ctrl::Software) == SysInitStatus::InitOK) {
    // Send_DMA / Receive_DMA / SendReceive_DMA can now be started.
}
```

**Checks:**

- At compile time (`constinit`): wrong request, TX and RX on the same resource, DMA not matching the direction.
- In `SetUp()`: busy resources, hardware setup or registry failure → `InitError`; only resources acquired by this call are released.
- `SetUp()` configures ownership, data width (= frame size), direction and the DR address.

**Interrupts:** SPI, DMA TX and DMA RX have separate handlers. Even if TX and RX share one NVIC vector, they take **two** registry slots; each handler checks only its own DMA.

**Which DMA each method needs:**

| Method | DMA needed |
| --- | --- |
| `Send_DMA` | TX |
| `Receive_DMA` | RX |
| `SendReceive_DMA` | TX and RX |

- `RX` master: only RX DMA; dummy frames come from the TX interrupt. `RX` slave: only RX DMA.
- `TX`: only TX DMA, no receive work.
- `TXRX`: a direction without DMA is served by the SPI interrupt (dummy TX or discarded RX). At high SCK, interrupt latency becomes the limit.
- `Send_DMA(..., minc = DISABLE)` repeats the first frame.
- DMA buffers must be in DMA-accessible memory.

---

## 9. F7 cache and buffer handover

| Moment | Cache operation (through System) |
| --- | --- |
| before TX | `CacheClean` (only the repeated frame with `minc = DISABLE`) |
| before RX | `CacheCleanInvalidate` |
| after DMA stop, success | `CacheInvalidate` of the written prefix |
| after abort/error | `CacheInvalidate` of the whole RX allocation |

Byte length = frame count × frame width.

- Cacheable RX buffers on F7 must own complete 32-byte lines, including padding. Do not read or write them while `Busy`.
- Ranges entirely inside a configured non-cacheable DMA region are skipped by System; on F4/G0 the helpers are empty. No separate SPI build option is needed.
- For `TXRX` DMA dummy feeding the all-ones source is a constant in Flash; the separate discard word is never touched by the CPU, so it needs no cache handover.
- For simultaneous TX/RX use **different** buffers — receiving into a TX buffer DMA has not yet read corrupts the transmission.

```cpp
alignas(32) static uint16_t rx[64];   // owns all affected cache lines
static uint16_t tx[64];
// After Half_Word setup with constructor-bound DMA:
bus.SendReceive_DMA(tx, rx, 64);      // 64 frames = 128 cache-maintained RX bytes
// Do not access rx before Process() completes.
```

---

## 10. IRQ, errors, callbacks and cleanup

**Registration:**

- `SetUp()` registers the SPI object and enables its NVIC line once, and separately registers the DMA TX/RX handlers.
- `IRQ_en()` changes only CR2 source bits. Transfer sources are enabled only while a transfer runs.
- `DeInit()` removes only this object's entries; other handlers and shared NVIC state stay.
- Registry changes happen in `main`, never in callbacks.

**Bounded work per interrupt:** one SPI/NSS interrupt entry reads or discards at most one RX FIFO — 1 frame on F4; 4 × 8-bit or 2 × 16-bit frames on F7/G0. Error handling and normal RX share that budget, so incoming clocks cannot extend the loop indefinitely. Remaining RX is handled by later interrupts; after NSS, `Process()` drains the stopped FIFO. The TX interrupt writes at most one frame.

**Snapshots:** error/RX/TX checks share one CR2/SR snapshot per entry. F4 reads the first RX frame without re-reading SR; F7/G0 refresh SR for further FIFO frames. The DR/SR sequence clearing OVR and the final wire-status check in `Process()` remain. Each DMA handler uses one `GetIRQFlags()` snapshot for TC and errors.

**Errors** are reported once through `OnError(uint32_t mask)` with bits `Overrun`, `ModeFault`, `DmaTx`, `DmaRx`.

- CRCERR/FRE and the `Crc`/`Frame` enum entries were removed — hardware CRC and TI mode are not used.
- DMA errors take precedence over TC.
- Sources stop in the interrupt; cache, status and the notification happen in `Process()`.
- A blocking timeout releases the driver-managed CS and resets only the aborted SPI to discard queued/latched state; other peripherals and shared clocks are untouched.

**Callbacks** (protected virtual):

| Callback | Type | Override rule |
| --- | --- | --- |
| `OnTxEmpty`, `OnRxByte`, `OnRxWord`, `OnDmaTxComplete`, `OnDmaRxComplete` | part of standard processing | call the base implementation to keep storage/count/truncation |
| `OnComplete`, `OnError` | notifications | empty by default |

Byte/word callbacks also run during blocking polling or the final FIFO drain, in the calling/`main` context.

```cpp
class Link : public SPI {
public:
    using SPI::SPI;
protected:
    void OnRxWord(uint16_t frame) override {
        SPI::OnRxWord(frame);   // keep storage/count/truncation
    }
    void OnComplete() override { /* main/Process: packet is available */ }
    void OnError(uint32_t errors) override { /* main: record the failure mask */ }
};
```

**`DeInit()`** cancels without completion callbacks, stops DMA, returns an active RX buffer to the CPU, unregisters this object and releases the DMA resources. Disable or serialize your NSS EXTI callback first. The received count and truncation flag are reset on the next start or by `DeInit()`. The destructor does not call `DeInit()`.

---

## 11. Usage Examples

A minimal master. Check device/package availability and every returned status.

```cpp
#include "spi.hpp"
using S = SPI::_1;

constinit SPI bus(S::instance, SPI::Direction::TXRX,
                  S::SCK::PA5, S::MOSI::PA7, S::MISO::PA6);

// In main, after System/RCC initialization:
if (bus.SetUp(SPI::Master_sel::Master, SPI::NSS_ctrl::Software)
        != SysInitStatus::InitOK)
    System::DebugTrap("SPI setup");

uint8_t tx[] = {0x9F, 0, 0, 0}, rx[4];
if (bus.SendReceive_IRQ(tx, rx, 4) == SysStatus::OK) {
    while (bus.Process() == SysStatus::Busy) { /* other main-loop work */ }
    // rx is valid if Process() returned OK; buffers must stay alive until then.
}
```

More examples: shared bus (section 4), RX-only DMA (section 5), slave (section 7), DMA binding (section 8).

---

## 12. Edge Cases

| Situation | Behaviour |
| --- | --- |
| DMA TC received | Buffer done, not wire done — call `Process()` in `main`. |
| Slave packet length | Determined only by NSS; extra frames → `IsTruncated()`. |
| G0 slave: NSS rises before the last DMA request is serviced | Not supported — the final request must be serviced before the stop. |
| Handler preemption, control serialization | Application responsibility. |
| F7 cache-line ownership | Application responsibility. |

---

## See Also

- [ST RM0444 — SPI 32-bit FIFOs](https://www.st.com/resource/en/reference_manual/rm0444-stm32g0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [ST RM0431 — F7 SPI FIFO capacity](https://www.st.com/resource/en/reference_manual/dm00305990.pdf)
- [ST AN5543 — data flow, DMA and physical completion, NSS](https://www.st.com/resource/en/application_note/an5543-guidelines-for-enhanced-spi-communication-on-stm32-mcus-and-mpus-stmicroelectronics.pdf)
- [ST F7 SPI driver — FIFO threshold and transaction-end checks](https://raw.githubusercontent.com/STMicroelectronics/stm32f7xx-hal-driver/master/Src/stm32f7xx_hal_spi.c)
- [ST F429 errata §2.12.5 — slave BSY](https://www.st.com/resource/en/errata_sheet/dm00068628-stm32f427437-and-stm32f429439-line-limitations-stmicroelectronics.pdf)
- [ST F411 datasheet — AF mapping](https://www.st.com/resource/en/datasheet/stm32f411ce.pdf)
- [ST F410 datasheet — SPI5 AF6 pins](https://www.st.com/resource/en/datasheet/stm32f410rb.pdf)
- [ST F412 datasheet — AF mapping](https://www.st.com/resource/en/datasheet/stm32f412ve.pdf)
- [ST F413 datasheet — AF mapping](https://www.st.com/resource/en/datasheet/stm32f413cg.pdf)
- [ST F429 datasheet — SPI4–SPI6 pins](https://www.st.com/resource/en/datasheet/stm32f429zi.pdf)
- [ST F767 datasheet — SPI4–SPI6 pins](https://www.st.com/resource/en/datasheet/stm32f767vi.pdf)
- [ST RM0385 — DMA request mapping](https://www.st.com/resource/en/reference_manual/dm00124865.pdf)
- [ST RM0383 — F411 DMA request mapping](https://www.st.com/resource/en/reference_manual/dm00119316-stm32f4-series-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [ST RM0401 — F410 DMA request mapping](https://www.st.com/resource/en/reference_manual/dm00180366-stm32f410-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [ST G0B1 datasheet — SPI3 pins](https://www.st.com/resource/en/datasheet/stm32g0b1kc.pdf)
- [ST G0 LL DMAMUX — SPI3 RX=66, TX=67](https://raw.githubusercontent.com/STMicroelectronics/stm32g0xx-hal-driver/master/Inc/stm32g0xx_ll_dmamux.h)
- [Initialization.md](Initialization.md)
- [DMA.md](DMA.md)
- [GPIO.md](GPIO.md)
- [IRQ_Registry.md](IRQ_Registry.md)
- [System.md](System.md)

---

## Русский

## Содержание

1. [Зачем это нужно](#1-зачем-это-нужно)
2. [Постоянная инициализация и диагностика конструктора](#2-постоянная-инициализация-и-диагностика-конструктора)
3. [Настройка и владение](#3-настройка-и-владение)
4. [Общая шина и SS](#4-общая-шина-и-ss)
5. [Размер кадра и методы](#5-размер-кадра-и-методы)
6. [Process и завершение](#6-process-и-завершение)
7. [Slave до снятия аппаратного NSS](#7-slave-до-снятия-аппаратного-nss)
8. [DMA и кеш](#8-dma-и-кеш)
9. [Ошибки, callbacks и освобождение](#9-ошибки-callbacks-и-освобождение)
10. [Примеры использования](#10-примеры-использования)
11. [Угловые случаи](#11-угловые-случаи)
12. [См. также](#см-также)

---

## 1. Зачем это нужно

`SPI` управляет одним блоком SPI в режиме ведущего (master) или ведомого (slave).

- Конструктор закрепляет **направление** (`TX`, `RX` или `TXRX`), выводы, необязательный chip-select и необязательный DMA.
- Обмен бывает блокирующим, через прерывания или через DMA — всё работает на одном механизме обмена.
- Для асинхронного обмена **`Process()`** в основном цикле подтверждает, что последний кадр действительно ушёл на линию. Обработчики прерываний ничего не ждут.

---

## 2. Постоянная инициализация и диагностика конструктора

Объявляйте объект глобально с `constinit` (или как `static constinit` внутри функции). Аргументы: `SPI::_N::instance`, направление, именованные выводы SCK/MOSI/MISO, необязательный вывод CS/NSS, необязательные TX/RX DMA (именованные маршруты `_N::DMA::...` на F4/F7, физические `DMAx_Channely_BASE` на G0 — номера запросов подставляет SPI).

Что происходит при неверном аргументе (например, вывод MOSI от SPI2 у SPI1):

| Объявление | Результат |
| --- | --- |
| Объект с `constinit` | **Ошибка компиляции**, текст причины — в сообщении. |
| Объект без `constinit` | Собирается; при старте конструктор останавливается в `System::DebugTrap(причина)`. |

Каждая проверка вызывает `STM32_CONFIG_ERROR("причина")`: при компиляции — ошибка GCC с текстом причины, во время работы — `System::DebugTrap(text)`. `constinit` обязателен по правилам использования, но не проверяется.

`SetUp()` по-прежнему проверяет состояние аппаратуры и занятость ресурсов. Объект хранит ссылку на instance и один указатель регистров; нулевой указатель — «не настроен» (обнуляется после неудачного первого `SetUp()` и в `DeInit()`). Подробнее: [Initialization.md](Initialization.md).

---

## 3. Настройка и владение

**Один объект управляет одним блоком SPI.**

- Сначала настройте System/RCC.
- `SetUp()` вызывается один раз; перед перенастройкой или уничтожением объекта — `DeInit()` (деструктор его не вызывает).
- DMA хранится внутри SPI. Объект SPI и активные буферы должны существовать до конца использования.

**Конструктор:**

- SCK обязателен. Вывод данных можно опустить (`PIN{}`), если это направление снаружи не используется.
- Принадлежность именованных выводов периферии проверяется при компиляции.
- После CS идут необязательные `DMAReq` для TX и RX.
- В master с программным NSS переданный CS автоматически управляется драйвером; без него chip-select управляет приложение. Аппаратный NSS требует соответствующий вывод. Slave всегда использует аппаратный NSS.

**Что означает направление:**

| Направление | Аппаратный режим | Разрешённые методы |
| --- | --- | --- |
| `TX` | однопроводной выход (BIDIMODE/BIDIOE); без RX и второго DMA | `Send` |
| `RX` | full duplex, RXONLY сброшен; master тактирует пустыми кадрами из TX-прерывания, slave только принимает | `Receive` |
| `TXRX` | full duplex | `Send` (RX отбрасывается), `Receive` (пустые TX), `SendReceive` |

Метод, не подходящий закреплённому направлению, возвращает `Error`.

**`SetUp()`** — тактирование, режим и формат кадра (или перегрузка с `Init_struct_Typedef`):

```cpp
constinit SPI bus(SPI::_1::instance, SPI::Direction::TXRX,
                  SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7, SPI::_1::MISO::PA6);
SysInitStatus initialized = bus.SetUp(
    SPI::Master_sel::Master, SPI::NSS_ctrl::Software,
    SPI::Data_frame_format::Half_Word, SPI::Frame_Format::MSB,
    SPI::cPolPha::None, SPI::BaudRate::DIV8);
uint16_t tx[] = {0x1234, 0xABCD}, rx[2];
// Только после initialized == InitOK:
SysStatus result = bus.Send_Receive(tx, rx, 2); // Два кадра, не два байта.
```

`InitError` при: неверных значениях enum, отсутствии SCK, аппаратном NSS без вывода, slave с программным NSS, неподдерживаемом экземпляре, повторной настройке или ошибке регистрации IRQ.

**Поддержка МК:**

- SPI1–SPI6 на F4/F7 и SPI1–SPI3 на G0, если они объявлены в CMSIS.
- Именованные выводы следуют карте AF; наличие вывода в корпусе проверяет пользователь. SPI5 на F410/F411/F412/F413/F423 использует AF6, на старших F4/F7 — AF5.
- На G0 с SPI3 блоки SPI2 и SPI3 делят `SPI2_3_IRQn`.

**`SPI_USE_ATOMIC`** (в `spi.hpp`, по умолчанию `0`): `1` переключает «прочитать-изменить-записать» управляющих регистров на макросы ST `ATOMIC_*`. Защищается отдельная операция, а не весь обмен или передача буфера. У DMA своя настройка `DMA_USE_ATOMIC`. После изменения пересоберите драйвер и приложение.

---

## 4. Общая шина и SS

| Конфигурация | Кто управляет chip-select |
| --- | --- |
| Master, программный NSS, вывод CS передан | драйвер: высокий уровень после настройки, низкий при старте, высокий только после окончательного завершения/ошибки/таймаута |
| Master, программный NSS, без вывода CS | приложение (все CS-выводы) |
| Master, аппаратный NSS | аппаратура: SSM сброшен, SSOE установлен; NSS следует за включением SPI |
| Slave | нужен аппаратный NSS (SSM/SSOE сброшены); приложение настраивает EXTI на этом выводе для определения конца пакета |

Программный NSS в режиме slave не поддерживается.

**Общая шина:** используйте один объект SPI и выводы CS приложения — тогда CS может оставаться активным между командой и ответом. При ошибке тоже снимайте CS.

```cpp
PIN cs(GPIOB, 0);
cs.SetUp(PIN::TYPE::OUTPUT_PushPull, PIN::OUTPUT_SPEED::Low, PIN::LVL::HIGH);
// bus настроен как Master/Software, CS в конструктор не передан.
cs.SetLevel(0);
SysStatus result = bus.Send(command, command_frames);
if (result == SysStatus::OK) result = bus.Receive(reply, reply_capacity);
cs.SetLevel(1);
```

При обмене через IRQ/DMA снимайте CS только после того, как `Process()` сообщит о физическом завершении, а не сразу после успешного запуска.

**Устройства с разными настройками** (CPOL/CPHA, делитель, размер кадра) требуют явной перенастройки в простое: `DeInit()` + `SetUp()`. Направление и DMA из конструктора не меняются. Планировщика устройств нет.

---

## 5. Размер кадра и методы

**Размер кадра.** У каждого метода есть перегрузки `uint8_t*` и `uint16_t*`; используйте ту, что соответствует `SetUp(Byte / Half_Word)`. `len` всегда — **1…65535 кадров**:

- буфер `uint8_t` → `len` байт, 8-битные обращения к DR/DMA;
- буфер `uint16_t` → `len` полуслов (2·`len` байт), 16-битные обращения, выравнивание на 2 байта;
- на F7/G0 порог RX FIFO соответствует размеру кадра, неявной упаковки нет.

**Что передаёт каждая операция:** `Send` передаёт ваши кадры и отбрасывает RX; `Receive` передаёт пустые кадры (`0xFF` / `0xFFFF`) и сохраняет RX; `SendReceive` использует оба буфера. `Receive_DMA` использует двухпроводной приём с пустыми кадрами (RXONLY не используется).

| Режим | Методы | Значение `OK` при возврате |
| --- | --- | --- |
| Блокирующий | `Send`, `Receive`, `Send_Receive` | обмен **завершён** |
| IRQ | `Send_IRQ`, `Receive_IRQ`, `SendReceive_IRQ` | обмен **запущен** |
| DMA | `Send_DMA`, `Receive_DMA`, `SendReceive_DMA` | обмен **запущен** |

| Направление конструктора | Операции | Обслуживание |
| --- | --- | --- |
| `TX` | `Send` | только TX: TX DMA или TX IRQ |
| `RX` | `Receive` | master: RX DMA/IRQ + пустые TX из прерывания; slave: только RX |
| `TXRX` | `Send`, `Receive`, `SendReceive` | оба направления; направление без DMA обслуживает прерывание SPI |

Привязанный DMA не отменяет блокирующие методы и методы IRQ. Пример — `RX` master на G0 с одним DMA и без MOSI:

```cpp
constexpr SPI::DMAReq rx = DMA1_Channel3_BASE;
constinit SPI bus(SPI::_1::instance, SPI::Direction::RX, SPI::_1::SCK::PA5,
                  PIN{}, SPI::_1::MISO::PA6, PIN{}, {}, rx);
// Проверить SetUp(Master, Software), затем Receive_DMA(buffer, capacity).
// Вызывать Process() из main; использовать буфер только после OK.
```

**Коды статуса:**

| Результат | Когда |
| --- | --- |
| `NotInit` | `SetUp()` не выполнен успешно |
| `Busy` | идёт другая операция |
| `Error` | нулевые аргументы, больше 65535 кадров, несоответствие ширины кадра или выравнивания, нет вывода данных или нужного DMA |
| `Timeout` | блокирующий вызов превысил отведённое время |

- После `Error`/`Timeout` (и очистки) можно начать новый обмен.
- У блокирующего вызова **один** таймаут на весь вызов; нужен работающий `GetTick()`.
- У асинхронных вызовов внутреннего срока нет — приложение контролирует время само и может отменить обмен через `DeInit()`.

---

## 6. Process и завершение

> Ни TXE, ни «передача завершена» TX DMA не означают, что последний кадр ушёл на линию. Прерывание перемещает кадры; DMA TC лишь означает, что буфер передан или заполнен.

После запуска обмена через IRQ/DMA регулярно вызывайте `Process()` из `main`:

```cpp
// После успешного SendReceive_IRQ или SendReceive_DMA:
SysStatus state = bus.Process();
if (state == SysStatus::OK) {
    uint32_t frames = bus.GetDataReceivedCount();
    // Теперь RX доступен процессору; чтение счётчика его не сбрасывает.
}
```

**Что проверяет `Process()` (master):**

1. все запланированные TX/RX-кадры обработаны;
2. затем сам SPI — F4: TXE = 1 и BSY = 0; F7/G0: TX FIFO пуст и BSY = 0.

Одного чтения BSY = 0 во время незавершённого обмена недостаточно — поэтому такой порядок.

**Что делает `Process()`:** никогда не крутится в ожидании BSY; пока рано — возвращает `Busy`. По завершении останавливает источники/DMA, обслуживает кеш, снимает CS, которым управляет драйвер, публикует `OK` и вызывает `OnComplete()`. Аппаратные ошибки после очистки вызывают `OnError(mask)`. Оба уведомления выполняются в `main` (внутри `Process()`), а не в прерывании DMA.

`GetStatus()` только читает статус и этой работы не выполняет. Блокирующие вызовы `OnComplete()` не вызывают; `OnError()` сообщает об их аппаратных ошибках.

---

## 7. Slave до снятия аппаратного NSS

В режиме slave длину пакета определяет **ведущий**; конец пакета — подъём NSS.

**Вместимость буфера:**

- `len` — вместимость буфера, а не обязательная длина пакета. Пакет может быть короче или длиннее.
- Сохраняются первые `len` полных кадров; их число сообщает `GetDataReceivedCount()` (после `OK` от `Process()`).
- Лишние кадры отбрасываются, и `IsTruncated()` становится `true`; сохранённый префикс всё равно считается успешным пакетом. Ровно `len` кадров — **не** усечение.
- После TX-данных slave выдаёт пустые `0xFF`/`0xFFFF` до подъёма NSS. Этот хвост подаёт прерывание TXE (в том числе после TX DMA TC), поэтому выбирайте SCK с учётом задержки прерываний — непрерывная выдача на максимальной SCK не гарантируется.

**Конец пакета:** подготовьте TX/RX **до** того, как ведущий опустит NSS. Приложение настраивает EXTI по нарастающему фронту на выводе аппаратного NSS, подтверждает флаг и вызывает `NssReleased()`:

```cpp
constinit SPI slave(SPI::_1::instance, SPI::Direction::TXRX,
                    SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7, SPI::_1::MISO::PA6,
                    SPI::_1::SS::PA4);
static uint8_t packet[64];
// Проверить SetUp(Slave, Hard), затем Receive_IRQ(packet, sizeof(packet)).

// В пользовательском EXTI после проверки/подтверждения флага NSS:
slave.NssReleased();

// В main после запуска:
if (slave.Process() == SysStatus::OK) {
    uint32_t frames = slave.GetDataReceivedCount();
    bool truncated = slave.IsTruncated();
    // Обработать packet[0..frames), затем снова запустить приём.
}
```

**Что происходит внутри:**

- `NssReleased()` отключает источники SPI и запрашивает остановку DMA — без ожиданий, работы с кешем и callback.
- F4/F7: каждое прерывание DMA фиксирует TC остановки и итоговый счётчик; `Process()` остаётся `Busy`, пока не остановятся оба направления.
- G0: последний запрос DMA должен быть обслужен до остановки по NSS; `Process()` читает остановленный счётчик. Произвольная остановка посреди переноса не даёт достоверной длины. При ошибках весь пакет отбрасывается.
- `Process()` инвалидирует записанный DMA префикс, дочитывает хвост RX FIFO и публикует пакет. Также сбрасывает SPI, чтобы удалить заранее загруженные, но не переданные TX-кадры.
- BSY в режиме slave **не** используется как граница пакета — у ряда МК есть документированная ошибка (errata) slave-BSY.

**Требования к времени:**

- Обработчики NSS EXTI, SPI и DMA этого объекта не должны вытеснять друг друга.
- Фронт EXTI должен быть обслужен до следующего пакета, а ведущий должен дождаться выполнения `Process()` и повторного запуска приёма.
- Опрос NSS в основном цикле не заменяет захват фронта, если ведущий может давать короткие импульсы.

**Целостность:** сообщаются только полные кадры; неполный последний кадр и корректность протокола проверяет приложение. OVR **до** заполнения буфера — потеря данных → `Error`. OVR **после** заполнения — усечение с сохранением префикса.

---

## 8. DMA и кеш

**Привязка DMA.** DMA передаются в конструктор после CS и настраиваются в `SetUp()`; отдельного `AttachDMA()` нет.

```cpp
// F4/F7:
constexpr SPI::DMAReq tx = SPI::_1::DMA::TX_DMA2_Stream3;
constexpr SPI::DMAReq rx = SPI::_1::DMA::RX_DMA2_Stream0;
// G0:
// constexpr SPI::DMAReq tx = DMA1_Channel2_BASE;
// constexpr SPI::DMAReq rx = DMA1_Channel3_BASE;
constinit SPI bus(SPI::_1::instance, SPI::Direction::TXRX, SPI::_1::SCK::PA5,
                  SPI::_1::MOSI::PA7, SPI::_1::MISO::PA6, PIN{}, tx, rx);
// Проверить bus.SetUp(Master, Software), затем запускать обмен.
```

**Проверки:**

- При компиляции (`constinit`): неверный запрос, один ресурс для TX и RX, DMA несовместимого направления.
- В `SetUp()`: занятый DMA, ошибка настройки или регистрации → `InitError`; освобождается только захваченное этим вызовом.
- Ширина данных DMA соответствует размеру кадра.

**Прерывания:** SPI, DMA TX и DMA RX регистрируются отдельно. Даже при общем векторе TX/RX нужны **два** места в реестре; каждый обработчик проверяет только свой DMA.

**Какой DMA нужен методу:**

| Метод | Нужный DMA |
| --- | --- |
| `Send_DMA` | TX |
| `Receive_DMA` | RX |
| `SendReceive_DMA` | TX и RX |

- `RX` master: только RX DMA, пустые кадры выдаёт TX-прерывание. `RX` slave: только RX DMA.
- `TX`: только TX DMA, без обслуживания RX.
- `TXRX`: направление без DMA обслуживает прерывание SPI. Частота SCK должна учитывать задержку прерываний.
- `minc = DISABLE` повторяет первый TX-кадр.
- Буферы должны быть доступны DMA.

**Кеш** (через System; на F4/G0 функции пустые):

| Момент | Операция |
| --- | --- |
| перед TX | `CacheClean` (при `minc = DISABLE` — только повторяемый кадр) |
| перед RX | `CacheCleanInvalidate` |
| после остановки DMA, успех | `CacheInvalidate` записанного префикса |
| после отмены/ошибки | `CacheInvalidate` всего RX-буфера |

Размер в байтах = число кадров × ширина кадра.

- Процессор не обращается к активным RX-линиям кеша и не меняет TX-буфер во время обмена.
- Некешируемые области проекта System пропускает.
- Для одновременного TX/RX используйте **разные** буферы: приём в ещё не прочитанные DMA исходные данные портит передачу.
- Источник пустых кадров для `TXRX` — неизменяемая константа во Flash; слово для отбрасываемых данных процессор не читает и не меняет.

```cpp
alignas(32) static uint16_t rx[64]; // Владеет всеми затронутыми кеш-линиями.
static uint16_t tx[64];
// После настройки Half_Word с DMA из конструктора:
bus.SendReceive_DMA(tx, rx, 64);   // 64 кадра = 128 байт RX с обслуживанием кеша
// Не обращаться к rx, пока Process() не вернёт результат.
```

---

## 9. Ошибки, callbacks и освобождение

**Регистрация:**

- `SetUp()` регистрирует объект SPI и один раз включает его линию NVIC, а также отдельно регистрирует обработчики DMA TX/RX.
- `IRQ_en()` меняет только биты источников в CR2, не реестр и не NVIC. Источники обмена включаются только на время активной операции.
- `DeInit()` удаляет только свои записи; общие обработчики и NVIC сохраняются.

**Ошибки** сообщаются одним вызовом `OnError(uint32_t mask)` с битами `Overrun`, `ModeFault`, `DmaTx`, `DmaRx`.

- CRCERR/FRE и элементы enum `Crc`/`Frame` удалены — аппаратный CRC и режим TI не используются.
- Ошибки DMA приоритетнее TC.
- Источники останавливаются в прерывании; кеш, статус и уведомление — в `Process()`.
- При таймауте/ошибке сбрасывается только прерванный SPI (удаляются очередь и защёлкнутое состояние); CS, которым управляет драйвер, снимается.

**Callback** (protected virtual):

| Callback | Тип | Правило переопределения |
| --- | --- | --- |
| `OnTxEmpty`, `OnRxByte`, `OnRxWord`, `OnDmaTxComplete`, `OnDmaRxComplete` | часть стандартной обработки | вызывайте базовую реализацию, чтобы сохранить запись/счётчик/усечение |
| `OnComplete`, `OnError` | уведомления | по умолчанию пустые |

Покадровые callback могут выполняться также из блокирующего вызова или при дочитывании FIFO в `main`. Менять реестр и привязки из callback запрещено.

```cpp
class Link : public SPI {
public:
    using SPI::SPI;
protected:
    void OnRxWord(uint16_t frame) override {
        SPI::OnRxWord(frame); // Сохранить запись/счётчик/усечение.
    }
    void OnComplete() override { /* main/Process: пакет доступен */ }
    void OnError(uint32_t errors) override { /* main: запомнить маску ошибок */ }
};
```

**Ограниченная работа за одно прерывание:** один вход в прерывание SPI/NSS читает или отбрасывает не больше одного RX FIFO — 1 кадр на F4; 4 × 8 бит или 2 × 16 бит на F7/G0. Обработка ошибки и обычный RX используют общий счётчик, поэтому новые такты не продлевают цикл бесконечно. Остаток RX обслуживают следующие прерывания; после NSS `Process()` дочитывает остановленный FIFO. TX-прерывание записывает не больше одного кадра.

**Снимки регистров:** проверки ошибок/RX/TX используют один снимок CR2/SR на вход. На F4 первый RX-кадр читается без повторного чтения SR; на F7/G0 SR обновляется для следующих кадров FIFO. Последовательность DR/SR для сброса OVR и итоговая проверка линии в `Process()` сохранены. Каждый обработчик DMA берёт TC и ошибки из одного `GetIRQFlags()`.

**`DeInit()`** отменяет обмен без callback завершения, останавливает DMA, возвращает активный RX-буфер процессору, удаляет свои записи IRQ и освобождает DMA. Перед этим отключите или согласуйте свой NSS EXTI. Счётчик и признак усечения сбрасываются при новом запуске или в `DeInit()`. Деструктор `DeInit()` не вызывает.

---

## 10. Примеры использования

Минимальный master. Проверьте наличие блока/выводов в корпусе и каждый возвращаемый статус.

```cpp
#include "spi.hpp"
using S = SPI::_1;

constinit SPI bus(S::instance, SPI::Direction::TXRX,
                  S::SCK::PA5, S::MOSI::PA7, S::MISO::PA6);

// В main, после инициализации System/RCC:
if (bus.SetUp(SPI::Master_sel::Master, SPI::NSS_ctrl::Software)
        != SysInitStatus::InitOK)
    System::DebugTrap("SPI setup");

uint8_t tx[] = {0x9F, 0, 0, 0}, rx[4];
if (bus.SendReceive_IRQ(tx, rx, 4) == SysStatus::OK) {
    while (bus.Process() == SysStatus::Busy) { /* другая работа основного цикла */ }
    // rx достоверен, если Process() вернул OK; буферы должны жить до этого момента.
}
```

Другие примеры: общая шина (раздел 4), приём только через RX DMA (раздел 5), slave (раздел 7), привязка DMA (раздел 8).

---

## 11. Угловые случаи

| Ситуация | Поведение |
| --- | --- |
| Получен DMA TC | Буфер завершён, но не обмен на линии — вызывайте `Process()` в `main`. |
| Длина slave-пакета | Определяется только NSS; лишние кадры → `IsTruncated()`. |
| G0 slave: NSS поднялся до обслуживания последнего запроса DMA | Не поддерживается — последний запрос должен быть обслужен до остановки. |
| Вытеснение обработчиков, согласование управления | Ответственность приложения. |
| Владение кеш-линиями на F7 | Ответственность приложения. |

---

## См. также

- [ST RM0444 — 32-битные FIFO SPI](https://www.st.com/resource/en/reference_manual/rm0444-stm32g0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [ST RM0431 — ёмкость FIFO SPI на F7](https://www.st.com/resource/en/reference_manual/dm00305990.pdf)
- [ST AN5543 — поток данных, DMA, физическое завершение, NSS](https://www.st.com/resource/en/application_note/an5543-guidelines-for-enhanced-spi-communication-on-stm32-mcus-and-mpus-stmicroelectronics.pdf)
- [Драйвер ST F7 SPI — порог FIFO и проверки конца транзакции](https://raw.githubusercontent.com/STMicroelectronics/stm32f7xx-hal-driver/master/Src/stm32f7xx_hal_spi.c)
- [ST F429 errata §2.12.5 — slave BSY](https://www.st.com/resource/en/errata_sheet/dm00068628-stm32f427437-and-stm32f429439-line-limitations-stmicroelectronics.pdf)
- [ST F411 datasheet — карта AF](https://www.st.com/resource/en/datasheet/stm32f411ce.pdf)
- [ST F410 datasheet — выводы SPI5 AF6](https://www.st.com/resource/en/datasheet/stm32f410rb.pdf)
- [ST F412 datasheet — карта AF](https://www.st.com/resource/en/datasheet/stm32f412ve.pdf)
- [ST F413 datasheet — карта AF](https://www.st.com/resource/en/datasheet/stm32f413cg.pdf)
- [ST F429 datasheet — выводы SPI4–SPI6](https://www.st.com/resource/en/datasheet/stm32f429zi.pdf)
- [ST F767 datasheet — выводы SPI4–SPI6](https://www.st.com/resource/en/datasheet/stm32f767vi.pdf)
- [ST RM0385 — карта запросов DMA](https://www.st.com/resource/en/reference_manual/dm00124865.pdf)
- [ST RM0383 — карта запросов DMA F411](https://www.st.com/resource/en/reference_manual/dm00119316-stm32f4-series-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [ST RM0401 — карта запросов DMA F410](https://www.st.com/resource/en/reference_manual/dm00180366-stm32f410-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [ST G0B1 datasheet — выводы SPI3](https://www.st.com/resource/en/datasheet/stm32g0b1kc.pdf)
- [ST G0 LL DMAMUX — SPI3 RX=66, TX=67](https://raw.githubusercontent.com/STMicroelectronics/stm32g0xx-hal-driver/master/Inc/stm32g0xx_ll_dmamux.h)
- [Initialization.md](Initialization.md)
- [DMA.md](DMA.md)
- [GPIO.md](GPIO.md)
- [IRQ_Registry.md](IRQ_Registry.md)
- [System.md](System.md)
