# DMA / DMA_Sx — Detailed Description

*[English](#english) · [Русский](#русский)*

---

## English

## Table of Contents

1. [Why This Is Needed](#1-why-this-is-needed)
2. [Constant Initialization and Constructor Diagnostics](#2-constant-initialization-and-constructor-diagnostics)
3. [Stream (F4/F7) vs Channel+DMAMUX (G0)](#3-stream-f4f7-vs-channeldmamux-g0)
4. [Register Abstraction](#4-register-abstraction)
5. [Static Descriptor Table and Protection from Double Initialization](#5-static-descriptor-table-and-protection-from-double-initialization)
6. [_N::DMA — Named Request Tables](#6-_ndma--named-request-tables)
7. [SetUp() — Step by Step](#7-setup--step-by-step)
8. [Flags: One Register for Multiple Channels](#8-flags-one-register-for-multiple-channels)
9. [Integration with Peripheral Drivers](#9-integration-with-peripheral-drivers)
10. [Integration Details](#10-integration-details)
11. [Usage Examples](#11-usage-examples)
12. [Edge Cases](#12-edge-cases)
13. [See Also](#see-also)

---

## 1. Why This Is Needed

DMA_USE_ATOMIC in dma.hpp defaults to 0 (ordinary CR/CCR and RCC RMW).
Set 1 to use ST ATOMIC_* operations and rebuild DMA and its users. Individual
register updates are protected; acquisition, buffer ownership and multi-step
configuration still require external serialization.

`DMA_Sx` is one class per one DMA stream (F4/F7) or DMA channel (G0), hiding the fact that this is
physically **different hardware** across different families (see §3) behind one and the same API.
DMA keeps its own hardware flag-register/IRQ descriptors. Peripheral drivers instead
receive their selected `_N::instance` directly. `GetStreamIndex()` (§5) calculates the index from the controller's address
range and register stride, rejecting addresses between channel/stream bases.

`DMA_Sx` is usually not used by itself, but as the "engine" taken by [`USART`](UART.md),
[`SPI`](SPI.md) or [`TIM`](TIM.md#8-independent-pwm-dma) — these drivers call `SetUp()`/
IRQ registration and `Enable_IRQ()` for internally owned DMA objects. USART,
SPI and ADC receive named DMA requests on F4/F7 and physical channels on G0;
TIM receives the selection with each channel pin and configures it in SetUpPWM.

After successful SetUp and handler registration, `Enable_IRQ(irq, enable_nvic = true)`
enables the DMA interrupt source and, by default, its NVIC line. With `false` it
enables only the source, preserving NVIC enable/pending state. `Disable_IRQ()` disables only the
source. These methods do not acknowledge flags or start a transfer.

---

## 2. Constant Initialization and Constructor Diagnostics

Declare fixed peripheral objects with `constinit` at file scope or as `static constinit`. Standalone DMA takes a numeric stream/channel base or a named peripheral route. DMA SetUp retains resource/transfer checks. Peripheral drivers own their embedded DMA objects.

For peripheral binding checks, each failing condition calls non-constexpr `System::DebugTrap` with its cause. A `constinit` initializer rejects that call during compilation; without constinit, a failing runtime construction stops. The keyword is mandatory by the usage contract, not enforced by the constructor. SetUp still checks hardware state and occupied resources. See [Initialization.md](Initialization.md) for constructor variants and migration.

---

## 3. Stream (F4/F7) vs Channel+DMAMUX (G0)

```
F4/F7: DMA_Stream_TypeDef                          G0: DMA_Channel_TypeDef + DMAMUX1
┌─────────────────────────────┐                    ┌─────────────────────────────┐
│ CR: CHSEL[2:0] hard-wired    │                    │ CCR: direction/size/inc      │
│      to the peripheral       │                    │      (no CHSEL — the channel │
│      (see RM0090 Table 42)   │                    │      is physically the same) │
│ NDTR/PAR/M0AR                │                    │ CNDTR/CPAR/CMAR              │
│ LISR/HISR (flags, 2 regs)    │                    │ ISR (flags, one register)    │
│ LIFCR/HIFCR (clear, 2 regs)  │                    │ IFCR (clear, one register)   │
└─────────────────────────────┘                    └─────────────┬───────────────┘
                                                                  │ DMAMUX1_ChannelN->CCR = req_id
     "Which stream serves USART1_TX?"                            ▼
     — fixed in silicon, look it up in _N::DMA                ┌─────────────────────┐
       (see §6) or in RM0090 Table 42                        │ DMAMUX1 — a software │
                                                              │ router: ANY channel  │
                                                              │ → ANY peripheral     │
                                                              └─────────────────────┘
```

On F4/F7 the "stream ↔ peripheral" pairing is part of the silicon. For example,
`USART::_3::DMA::TX_DMA1_Stream3` and `USART::_3::DMA::TX_DMA1_Stream4` name two fixed
USART3 TX routes and show the occupied resource directly. On G0, `DMAMUX1` is a separate hardware
block between the available DMA controllers and request sources. The device has
5 or 7 DMA1 channels and may also have five DMA2 channels. The caller selects
an available channel and a valid request source through `DMAMUX1_ChannelN->CCR`.

---

## 4. Register Abstraction

Just as in [`UART`](UART.md)/[`SPI`](SPI.md), the difference in register names is hidden behind five
private inline accessors plus a set of `constexpr` masks:

```cpp
volatile uint32_t& _CR();    // ->CR   (F4/F7, DMA_Stream_TypeDef) | ->CCR  (G0, DMA_Channel_TypeDef)
volatile uint32_t& _NDTR();  // ->NDTR                              | ->CNDTR
volatile uint32_t& _PAR();   // ->PAR                               | ->CPAR
volatile uint32_t& _MAR();   // ->M0AR                              | ->CMAR
```

plus `CR_EN`/`CR_MINC`/`CR_PINC`/`CR_CIRC` (the bit name is the same in meaning, but the CMSIS
macro differs: `DMA_SxCR_EN` vs `DMA_CCR_EN`) and `FLAG_TC`/`FLAG_ALL` (the position of the TC flag
within the flag group of one stream/channel — see §8). The entire public API (`SetMemAddr`, `SetCount`,
`ClearFlags`, `GetTC_Flag`, `Enable_IRQ`, `Stream_EN`, ...) is written once on top of these accessors
and works identically on F4/F7/G0.

---

## 5. Static Descriptor Table and Protection from Double Initialization

Each descriptor stores only the controller, flag registers/offset and IRQ.
`dma_streams` is a `static const` table in Flash; mutable ownership flags live
in the separate zero-initialized `dma_used` array in RAM, with the same indices.
`_info` points to a const descriptor. SetUp marks its index occupied; Release
clears that index. No additional per-object index is stored.
F4/F7 have 16 streams. G0 has 5 or 7 DMA1 channels, plus five DMA2 channels when present.
GetStreamIndex() checks the controller range and exact stride from CMSIS base addresses;
a pointer inside a register block is rejected. DMA2 on G0 uses descriptor/DMAMUX
indices 7..11, but its own ISR/IFCR for DMA flags.

Successful `SetUp()` assigns the hardware to that object. Repeated setup, including
a different object selecting the same channel, returns `InitError` until the owner's
`Release()`. Transfer completion retains ownership. One configured object belongs
to one consumer. SetUp/Release run in the main loop; access is serialized externally.

---

## 6. _N::DMA — Named Request Tables

```cpp
struct DMAReq {
#if F4/F7:  uintptr_t stream; uint32_t ch; // CHSEL — both fields are required
#elif G0:   uint32_t ch;                             // DMAMUX1 request ID
    uintptr_t periph_base; // USART owner, zero for other current consumers
    DMA_Sx::DIR direction; // meaningful when periph_base != 0
};

struct Req {
    struct Usart1 {
#if defined(STM32F4) || defined(STM32F7)
        static constexpr DMAReq TX_DMA2_Stream7 = {DMA2_Stream7_BASE, 4U, USART1_BASE, DMA_Sx::DIR::To_Per};
        static constexpr DMAReq RX_DMA2_Stream2 = {DMA2_Stream2_BASE, 4U, USART1_BASE, DMA_Sx::DIR::From_Per};
        static constexpr DMAReq RX_DMA2_Stream5 = {DMA2_Stream5_BASE, 4U, USART1_BASE, DMA_Sx::DIR::From_Per};
#elif defined(STM32G0)
        static constexpr DMAReq TX = {51U, USART1_BASE, DMA_Sx::DIR::To_Per};
        static constexpr DMAReq RX = {50U, USART1_BASE, DMA_Sx::DIR::From_Per};
#endif
    };
    // ... Spi1, Tim3, Adc1, ...
};
```

`_N::DMA` is not about selecting registers (that's what `_CR()`/`_NDTR()`/... do), but about
**which number to write into CHSEL (F4/F7) or into DMAMUX1 (G0)** so the stream/channel listens to
exactly the right peripheral. The struct names are deliberately `CamelCase` (`Usart1`, not `USART1`) so
as not to conflict with the CMSIS macros `USART1`/`SPI1`/... (those are `#define`d as pointers to the
peripheral's registers, not identifiers). F4/F7 route names identify each stream:
`USART::_1::DMA::RX_DMA2_Stream2` and `USART::_1::DMA::RX_DMA2_Stream5`, for example.
G0 keeps TX/RX because the channel is chosen separately in the constructor.

---

## 7. SetUp() — Step by Step

1. Resolve the exact hardware address and reject an already owned resource.
2. Select the constructor request, or settings.channel when no preset was supplied.
   Validate the effective CHSEL/request, direction, size, address alignment and
   count (0..65535). Mem_To_Mem is supported on G0 only and cannot be circular.
   F4/F7 reject this mode with InitError before changing hardware. The peripheral driver checks
   the route and device-specific request availability.
3. Enable the selected DMA controller clock. On G0 either DMA enable also clocks DMAMUX1.
4. Stop through `Stream_EN(DISABLE)`: clear EN and wait for EN=0 without a timeout.
   Clear this resource's DMA flags.
5. Write configuration, addresses and count. On G0 the descriptor index selects
   DMAMUX1; memory-to-memory uses no peripheral request.
6. Return InitOK with DMA disabled. Zero addresses/count are valid for deferred
   preparation; fill them before starting.

Rejected settings do not claim ownership or modify hardware. The element size is
the same for both ports. Address/count setters require successful setup and EN=0;
check their returned statuses.

---

## 8. Flags: One Register for Multiple Channels

On both F4/F7 (`LISR`/`HISR`) and G0 (`ISR`), the flags of **several** streams/channels are
packed into one register in groups of a few bits each — hence the `if_offset` field in the descriptor
table:

```cpp
inline void ClearFlags() { *_info->ifcr = FLAG_ALL << _info->if_offset; }
inline bool GetTC_Flag() { return (*_info->isr & (FLAG_TC << _info->if_offset)) != 0; }
inline bool GetHT_Flag() const { return (*_info->isr & (FLAG_HT << _info->if_offset)) != 0; }
inline void ClearHT_Flag() { *_info->ifcr = FLAG_HT << _info->if_offset; }
inline void ClearTC_Flag() { *_info->ifcr = FLAG_TC << _info->if_offset; }
```

`GetHT_Flag()` reads the half-transfer flag independently of whether `IRQ::HTC`
is enabled. `ClearHT_Flag()` clears only HT; `ClearTC_Flag()` clears only TC.
Both retain other completion/error flags and flags of other streams/channels.
Each clear performs one write to the clear register, without read/modify/write.
All three methods require successful `SetUp()`. The consumer enables interrupt
sources and handles errors; `ClearFlags()` still clears all flags of its resource.
ADC clears individual events through these methods and reads them together with
GetIRQFlags in its separate DMA handler: HT is explicitly enabled
through ADC_N::IRQ_en, and circular HT/TC invalidate the corresponding buffer half.
With HT disabled, circular TC invalidates the entire buffer. See [ADC.md](ADC.md).

On F4/F7 the group occupies 6 bits, but the offsets go `0, 6, 16, 22` (not `0, 6, 12, 18` — bits
15:12 inside `LISR`/`HISR` are reserved by hardware), so the offsets in the `dma_streams[]` table are
written explicitly for each stream rather than computed by a formula. On G0 the group is 4 bits,
`if_offset = (channel_number - 1) * 4`, more evenly.

**Shared vectors on G0:** Channel2/3 use `DMA1_Channel2_3_IRQn`. The other vector
serves Channel4/5, Channel4..7, or Channel4..7 plus DMA2 Channel1..5, depending
on the device, and includes DMAMUX overrun. [`IRQ_Registry`](IRQ_Registry.md)
supports multiple handlers; its default G0 capacity is three, which may require
an override in the configuration header for more consumers on the same line.
`GetTC_Flag()` of each `DMA_Sx` object checks **its own** `if_offset` within the shared `ISR`, so sharing
one vector between several channels is safe — `Dispatch()` will call `HandleIRQ()` on both registered
objects, but each will see only its own bit.

---

## 9. Integration with Peripheral Drivers

### Nonblocking stop used by USART

`RequestStop()` clears EN once and returns without waiting. `IsEnabled()` reads
EN once. Both require successful SetUp. Neither method clears status flags.
On F4/F7 a software stop produces TC; USART completes the stopped RX packet
from its DMA RX handler after that event. G0 abort has a different contract:
the caller must prevent a simultaneous DMA transfer before trusting its count.
USART uses the simple G0 IDLE stop: disable DMAR, disable DMA without waiting,
then report the packet in USART IRQ. The last request must already be serviced;
failed packets are discarded without using CNDTR. This is not a recovery API
for an arbitrary abort during a transfer.
`Stream_EN(DISABLE)` remains a blocking stop for main-loop code; it must not
be called from an IRQ. Other peripheral consumers are migrated separately.

`RequestStop()` сбрасывает EN без ожидания, `IsEnabled()` однократно читает EN.
На F4/F7 USART завершает RX после остановки по DMA TC. Для G0 произвольный
abort посреди переноса не гарантирует итоговый CNDTR. Блокирующий
`Stream_EN(DISABLE)` остаётся для основного кода; вызывать его из IRQ нельзя.
USART на G0 отключает DMAR/DMA при IDLE без ожидания и завершает пакет в USART IRQ.
Последний запрос должен быть уже обслужен; при ошибке пакет отбрасывается без
чтения CNDTR. Восстановление данных после произвольного abort не выполняется.

[`UART`](UART.md) configures constructor-bound DMA during SetUp.
[`SPI`](SPI.md) also configures constructor-bound DMA in SetUp.
[`TIM`](TIM.md#8-independent-pwm-dma) configures channel-bound DMA during SetUpPWM.
[`ADC`](ADC.md) and [`I2C`](I2C.md) also configure owned DMA at SetUp.
The common configuration steps are:

1. USART/SPI/ADC/I2C receive named F4/F7 requests and check owner/direction in their constructors;
   no duplicate USART/SPI/ADC/I2C DMA table is used. TIM retains its channel route map.
2. Fill in `DMA_Sx::StreamSettings` (direction, data register address, `Byte` width,
   `minc = true`) and call `dma->SetUp(cfg)` — the driver code itself does **not** touch `_CR()`/`_NDTR()`
   directly, only through the public `DMA_Sx` API.
3. Register the consumer's handler. USART/SPI/I2C register separate DMA TX and
   DMA RX handlers, distinct from the peripheral IRQ handler. TIM registers one
   handler per channel DMA; the timer IRQ does not scan DMA flags.
4. USART/SPI/I2C TX and RX sharing one NVIC vector still need two registry slots.
   Each handler checks its own DMA flags only.

`DMA_Sx` by itself contains no logic about "whose TC this is" — that semantics is added by the
peripheral driver on top of `GetTC_Flag()`/`ClearFlags()`.

---

## 10. Integration Details

### UART integration: request inspection and errors

CanUseRequest(DMAReq req) const checks the constructor's stream and preset:
F4/F7 require the same stream, and an existing preset must equal req.ch.
On G0 the request may use any channel, with hardware existence checked by SetUp().
Without a constructor preset, the consumer passes the matched req.ch to SetUp().
The method does not read registers, configure DMA or acquire ownership.

GetErrorFlags() const returns normalized hardware status bits:
F4/F7 TEIF0/DMEIF0 (direct mode), G0 TEIF1; zero when not configured.
FEIF is excluded from error reporting; ClearFlags() still clears all flags in one write.
Read before ClearFlags(). UART converts these into DmaTx/DmaRx callback indicators.
UART enables TC/TE and F4/F7 DME; FIFO mode is not used for its byte transfers.

GetIRQFlags() reads HT, TC and supported errors together in one ISR access.
Public DMA_Sx::FLAG_HT, FLAG_TC and FLAG_ERRORS decode that normalized snapshot.
FEIF is excluded. The method does not acknowledge flags or stop DMA and returns
zero before SetUp. USART/SPI/ADC/TIM/I2C use it in their separate DMA handlers;
ADC also decodes HT without a second ISR read.
TC produced by a stop after the snapshot is handled by the next DMA IRQ.

### Explicit route names

All F4/F7 routes now include the DMA controller and stream in their names:
`USART::_3::DMA::TX_DMA1_Stream3`, `SPI::_1::DMA::RX_DMA2_Stream0`, `ADC_N::_1::DMA::RX_DMA2_Stream4`.
These replace TX/RX and TX_alt/RX_alt; update application declarations accordingly.
Hardware stream addresses and CHSEL values are unchanged. DMAReq stores integer
stream bases (DMAx_Streamy_BASE), resolved to pointers by DMA_Sx. G0 keeps its request names:
the constructor already takes an explicit channel.

### SPI stop

SPI binds DMA in the constructor. Its SPI, DMA TX and DMA RX handlers are
registered separately. NSS only disables sources and requests stop; no IRQ waits.
F4/F7 collect stop TC in DMA IRQ. G0 slave RX requires the last request serviced
before NSS stop; Process reads the stopped count. Errors discard the packet.

### Independent TIM channels

TIM accepts up to four {pin, dma} constructor bindings. SetUpPWM acquires each
channel's resource and registers a distinct handler, including on shared vectors.
Each PWM DMA uses TIMx_CHx with CCDS=1 and its own CCxDE gate; no TIMx_UP fan-out.
16-bit timers use uint16_t/Half_Word, 32-bit TIM2/TIM5 use uint32_t/Word.
System cleans the TX buffer on F7. Each DMA handler reads only its own flags,
stops without waiting, publishes per-channel OK/Error and calls its bound callback.
Other channels continue; TC is buffer completion, not final PWM wire completion.
The caller masks affected TIM/DMA IRQs around main-side DIER modifications and
prevents mutual preemption of this object's handlers. DeInit releases acquired
DMA and registrations from main. See [TIM.md](TIM.md).
SetUpPWM enables NVIC during initialization; SendDMA rearms TE/DME through
Enable_IRQ(..., false), preserving the caller's NVIC mask.

---

## 11. Usage Examples

### G0 — Standalone DMA_Sx with explicit channel and request

```cpp
DMA_Sx dma_tx(DMA1_Channel4_BASE, USART::_1::DMA::TX);
DMA_Sx dma_rx(DMA1_Channel5_BASE, USART::_1::DMA::RX);
// ch2/ch3 or ch4/ch5 share a vector — different pairs were chosen so as not to overlap with another peripheral
```

### F4/F7 — via the peripheral DMA definitions (the Stream Is Already Baked into DMAReq)

```cpp
DMA_Sx dma_rx(USART::_1::DMA::RX_DMA2_Stream2);     // CHSEL=4
DMA_Sx dma_rx_alt(USART::_1::DMA::RX_DMA2_Stream5); // CHSEL=4
```

### G0 — Standalone Memory-to-Memory Transfer, No Peripheral

This example applies to G0. F4/F7 return InitError for Mem_To_Mem;
FIFO-based memory-to-memory support is deferred.

```cpp
DMA_Sx::StreamSettings cfg;
cfg.direction   = DMA_Sx::DIR::Mem_To_Mem;
cfg.data_size   = DMA_Sx::SIZE::Word;
cfg.minc        = true;
cfg.pinc        = true;
DMA_Sx copier(DMA1_Channel3_BASE, /*raw request, ignored in mem2mem*/ 0u);
copier.SetUp(cfg);
copier.SetPerAddr(reinterpret_cast<uint32_t>(src));
copier.SetMemAddr(reinterpret_cast<uint32_t>(dst));
copier.SetCount(len);
copier.Stream_EN(ENABLE);
while (!copier.GetTC_Flag()) {}
```

### Through a Peripheral Driver (the Typical Case — See UART.md/SPI.md)

```cpp
constinit USART a(USART::_3::instance, 115200, USART::_3::TX::PD8, USART::_3::RX::PD9,
        USART::_3::DMA::TX_DMA1_Stream3);
a.SetUp(); // Check InitOK; configures DMA and registers its handler.
a.Send_DMA(buf, len);
```

---

## 12. Edge Cases

- `SetUp()` permits zero addresses/count without starting DMA. `SetCount()` accepts
  1..65535 elements; count is in elements, not necessarily bytes.
- Address setters require nonzero addresses aligned to the configured port width.
  Setters return NotInit without ownership, Busy with EN=1, or Error for invalid
  arguments. Rejected values leave registers unchanged.
- Constructor request takes precedence over settings.channel. The effective value
  must fit CHSEL (0..7) or the selected device's DMAREQ_ID field. The peripheral
  driver checks reserved IDs and routes.
- `Stream_EN(DISABLE)` waits for EN=0 without SysTick/timeouts; main-loop use only.
  IRQ consumers use RequestStop and their hardware completion event.
  Before enable, the caller prepares valid transfer arguments. MINC/PINC/CIRC
  changes require a stopped channel and valid settings.
- Shared IRQs depend on the G0 device: DMA1 Channel2/3 share one line; the other
  line serves Channel4/5 or Channel4..7 and, when present, DMA2 Channel1..5.
  Configure IRQ_Registry capacity for the actual independent consumers.

### Release and handover

`Release()` returns OK after stopping and resetting the owned resource, clearing
its flags and releasing ownership, or NotInit when the object owns no resource.
F4/F7 also reset M1AR/FIFO; G0 clears its DMAMUX configuration and overrun flag.
Constructor stream/request selection is retained. Another object may then claim
the hardware with a different request.

Before release, the peripheral owner disables its DMA requests and unregisters
its own handler. Release leaves shared NVIC lines and controller clocks enabled.
Consumer-side detach/status handling is addressed in the respective driver reviews.

### Cache maintenance

The consumer calls `System::CacheClean` before TX,
`System::CacheCleanInvalidate` before RX, and `System::CacheInvalidate` after
confirmed RX completion/stop. Sizes are bytes. The peripheral driver determines
the actual received range before exposing it to application code. Direct DMA
users make these calls themselves; DMA retains no cache/buffer state.

F7 helpers skip ranges wholly inside configured non-cacheable project regions.
Cached RX storage must own all affected 32-byte lines, including padding.
CPU must not access a DMA-owned portion during transfer. F4/G0 helpers are
inline no-ops. See [System.md](System.md).

---

## See Also

- [IRQ_Registry.md](IRQ_Registry.md) — peripheral drivers register themselves (not `DMA_Sx`) as
  the handler for the DMA TC interrupt; shared vectors include `DMA1_Channel2_3_IRQn`.
- [UART.md](UART.md#6-dma-кеш-и-ошибки), [SPI.md](SPI.md#8-dma-и-кеш),
  [TIM.md](TIM.md#8-независимые-pwm-dma) — DMA consumers: USART uses constructor bindings
  configured by SetUp; SPI also uses constructor binding; TIM configures constructor-bound channel DMA in SetUpPWM.
- [System.md](System.md) — `SysStatus`/`SysInitStatus`, used by the public `DMA_Sx` API.

- [Initialization.md](Initialization.md)
- [UART.md](UART.md)
- [SPI.md](SPI.md)
- [ADC.md](ADC.md)
- [I2C.md](I2C.md)
- [TIM.md](TIM.md)
- [System.md](System.md)

---

## Русский

## Содержание

1. [Постоянная инициализация и диагностика конструктора](#1-постоянная-инициализация-и-диагностика-конструктора)
2. [Зачем это нужно](#2-зачем-это-нужно)
3. [Stream (F4/F7) vs Channel+DMAMUX (G0)](#3-stream-f4f7-vs-channeldmamux-g0-1)
4. [Регистровая абстракция](#4-регистровая-абстракция)
5. [Статическая таблица дескрипторов и защита от двойной инициализации](#5-статическая-таблица-дескрипторов-и-защита-от-двойной-инициализации)
6. [_N::DMA — именованные таблицы запросов](#6-_ndma--именованные-таблицы-запросов)
7. [SetUp() — пошагово](#7-setup--пошагово)
8. [Флаги: один регистр на несколько каналов](#8-флаги-один-регистр-на-несколько-каналов)
9. [Интеграция с периферийными драйверами](#9-интеграция-с-периферийными-драйверами)
10. [Подробности интеграции](#10-подробности-интеграции)
11. [Примеры использования](#11-примеры-использования)
12. [Угловые случаи](#12-угловые-случаи)
13. [См. также](#см-также)

---

## 1. Постоянная инициализация и диагностика конструктора

Объявляйте объекты периферии глобально с `constinit` либо как `static constinit`. Самостоятельный DMA принимает числовую базу потока/канала либо именованный маршрут периферии. Проверки ресурса/параметров остаются в DMA SetUp. Периферийные драйверы сами содержат свои DMA.

При проверке привязок периферии каждое неверное условие вызывает неконстантный `System::DebugTrap` с причиной. При `constinit` такой вызов отклоняется при сборке; без него неверное runtime-создание останавливает программу. Ключевое слово обязательно по контракту, конструктор не проверяет его наличие. Проверки состояния аппаратуры и занятых ресурсов остаются в SetUp. Варианты конструкторов и миграция: [Initialization.md](Initialization.md).

---

## 2. Зачем это нужно

`DMA_Sx` — один класс на один DMA-стрим (F4/F7) или DMA-канал (G0), скрывающий то, что это
физически **разное железо** на разных семействах (см. §3), за одним и тем же API. Как и
`USART`/`TIM`/`SPI`, использует паттерн "статическая таблица фактов о периферии + поиск по
указателю". `GetStreamIndex()` (§5) вычисляет индекс по диапазону контроллера
и шагу регистров, отклоняя адреса между началами каналов/потоков.

`DMA_Sx` обычно используется не сам по себе, а как "мотор", который берёт [`USART`](UART.md),
[`SPI`](SPI.md) или [`TIM`](TIM.md#8-independent-pwm-dma) — эти драйверы вызывают `SetUp()`/
регистрацию IRQ и Enable_IRQ для собственных DMA_Sx. USART/SPI/ADC/I2C
принимают выбор DMA в конструкторе и настраивают его при SetUp;
TIM настраивает DMA канала в SetUpPWM.

После успешного SetUp и регистрации обработчика `Enable_IRQ(irq, enable_nvic = true)`
включает источник прерывания DMA и по умолчанию его линию NVIC. При `false`
включается только источник, сохраняя разрешение и pending-состояние NVIC. `Disable_IRQ()` отключает только
источник. Эти методы не очищают флаги и не запускают передачу.

DMA_USE_ATOMIC в dma.hpp: 0 по умолчанию — обычные изменения управляющих
битов CR/CCR и RCC, 1 — ATOMIC_* ST. После изменения пересобираются DMA и
потребители. Настройка не заменяет согласование целой операции и владения буфером.

---

## 3. Stream (F4/F7) vs Channel+DMAMUX (G0)

```
F4/F7: DMA_Stream_TypeDef                          G0: DMA_Channel_TypeDef + DMAMUX1
┌─────────────────────────────┐                    ┌─────────────────────────────┐
│ CR: CHSEL[2:0] жёстко        │                    │ CCR: направление/размер/inc  │
│      привязан к периферии    │                    │      (без CHSEL — канал      │
│      (см. RM0090 Table 42)   │                    │      физически один и тот же) │
│ NDTR/PAR/M0AR                │                    │ CNDTR/CPAR/CMAR              │
│ LISR/HISR (флаги, 2 регистра)│                    │ ISR (флаги, один регистр)    │
│ LIFCR/HIFCR (сброс, 2 рег.)  │                    │ IFCR (сброс, один регистр)   │
└─────────────────────────────┘                    └─────────────┬───────────────┘
                                                                  │ DMAMUX1_ChannelN->CCR = req_id
     "Какой стрим обслуживает USART1_TX?"                        ▼
     — фиксировано в кремнии, смотреть в _N::DMA             ┌─────────────────────┐
       (см. §6) или в RM0090 Table 42                       │ DMAMUX1 — программный │
                                                             │ маршрутизатор: ЛЮБОЙ  │
                                                             │ канал → ЛЮБАЯ периферия│
                                                             └─────────────────────┘
```

На F4/F7 связка "стрим ↔ периферия" задана аппаратно. Например,
`USART::_3::DMA::TX_DMA1_Stream3` и `USART::_3::DMA::TX_DMA1_Stream4` обозначают два
маршрута USART3 TX и сразу показывают занятый ресурс. На G0 `DMAMUX1` — отдельный аппаратный
блок между доступными контроллерами DMA и источниками запросов. В зависимости
от МК есть 5 или 7 каналов DMA1 и ещё пять каналов DMA2. Пользователь выбирает
существующий канал и допустимый источник через `DMAMUX1_ChannelN->CCR`.

---

## 4. Регистровая абстракция

Как и в [`UART`](UART.md)/[`SPI`](SPI.md), разница в именах регистров спрятана в пяти приватных
инлайн-акцессорах плюс наборе `constexpr`-масок:

```cpp
volatile uint32_t& _CR();    // ->CR   (F4/F7, DMA_Stream_TypeDef) | ->CCR  (G0, DMA_Channel_TypeDef)
volatile uint32_t& _NDTR();  // ->NDTR                              | ->CNDTR
volatile uint32_t& _PAR();   // ->PAR                               | ->CPAR
volatile uint32_t& _MAR();   // ->M0AR                              | ->CMAR
```

плюс `CR_EN`/`CR_MINC`/`CR_PINC`/`CR_CIRC` (имя бита одно и то же по смыслу, но разный макрос
CMSIS: `DMA_SxCR_EN` vs `DMA_CCR_EN`) и `FLAG_TC`/`FLAG_ALL` (позиция флага TC внутри группы
флагов одного стрима/канала — см. §8). Весь публичный API (`SetMemAddr`, `SetCount`, `ClearFlags`,
`GetTC_Flag`, `Enable_IRQ`, `Stream_EN`, ...) написан один раз поверх этих акцессоров и работает
одинаково на F4/F7/G0.

---

## 5. Статическая таблица дескрипторов и защита от двойной инициализации

Дескриптор содержит только контроллер, регистры/смещение флагов и IRQ.
`dma_streams` — таблица `static const` во Flash. Изменяемые флаги занятости
вынесены в отдельный обнуляемый массив `dma_used` в RAM с теми же индексами.
`_info` указывает на константный дескриптор. SetUp отмечает индекс занятым,
Release освобождает его. Дополнительный индекс в объекте не хранится.
На F4/F7 есть 16 потоков. На G0 — 5 или 7 каналов DMA1 и ещё пять каналов DMA2,
если этот контроллер присутствует. GetStreamIndex() проверяет диапазон контроллера
и точный шаг из базовых адресов CMSIS; указатель внутрь блока регистров отклоняется.
DMA2 на G0 использует индексы дескрипторов/DMAMUX 7..11 и собственные ISR/IFCR.

Успешный `SetUp()` закрепляет ресурс за объектом. Повторная настройка, в том числе
другим объектом с тем же каналом, возвращает `InitError` до `Release()` владельца.
Завершение передачи сохраняет назначение канала. Один настроенный объект
используется одним потребителем. SetUp/Release выполняются в mainloop;
доступ согласовывается извне.

---

## 6. _N::DMA — именованные таблицы запросов

```cpp
struct DMAReq {
#if F4/F7:  uintptr_t stream; uint32_t ch; // CHSEL — оба поля обязательны
#elif G0:   uint32_t ch;                             // DMAMUX1 request ID
    uintptr_t periph_base; // USART; 0 для остальных текущих потребителей
    DMA_Sx::DIR direction; // используется при periph_base != 0
};

struct Req {
    struct Usart1 {
#if defined(STM32F4) || defined(STM32F7)
        static constexpr DMAReq TX_DMA2_Stream7 = {DMA2_Stream7_BASE, 4U, USART1_BASE, DMA_Sx::DIR::To_Per};
        static constexpr DMAReq RX_DMA2_Stream2 = {DMA2_Stream2_BASE, 4U, USART1_BASE, DMA_Sx::DIR::From_Per};
        static constexpr DMAReq RX_DMA2_Stream5 = {DMA2_Stream5_BASE, 4U, USART1_BASE, DMA_Sx::DIR::From_Per};
#elif defined(STM32G0)
        static constexpr DMAReq TX = {51U, USART1_BASE, DMA_Sx::DIR::To_Per};
        static constexpr DMAReq RX = {50U, USART1_BASE, DMA_Sx::DIR::From_Per};
#endif
    };
    // ... Spi1, Tim3, Adc1, ...
};
```

`_N::DMA` — не про выбор регистров (это делает `_CR()`/`_NDTR()`/...), а про то, **какое число
записать в CHSEL (F4/F7) или в DMAMUX1 (G0)**, чтобы стрим/канал слушал именно нужную периферию.
Имена структур — `CamelCase` (`Usart1`, не `USART1`) специально, чтобы не конфликтовать с
CMSIS-макросами `USART1`/`SPI1`/... (те `#define`'ятся как указатели на регистры периферии, а не
идентификаторы). Имена маршрутов F4/F7 показывают поток:
например, `USART::_1::DMA::RX_DMA2_Stream2` и `USART::_1::DMA::RX_DMA2_Stream5`.
На G0 сохраняются TX/RX: канал отдельно задаётся в конструкторе.

---

## 7. SetUp() — пошагово

1. Найти точный аппаратный адрес и отклонить уже занятый ресурс.
2. Выбрать запрос из конструктора либо settings.channel, если preset не задан.
   Проверить выбранное поле CHSEL/request, направление, размер, выравнивание
   адресов и счётчик (0..65535). Mem_To_Mem поддерживается только на G0,
   без circular. На F4/F7 этот режим возвращает InitError до изменения регистров. Маршрут
   и доступность запроса на выбранном МК проверяет периферийный драйвер.
3. Включить тактирование выбранного контроллера. На G0 включение любого DMA
   также включает DMAMUX1.
4. Остановить через `Stream_EN(DISABLE)`: снять EN и ждать EN=0 без таймаута.
   Снять DMA-флаги этого ресурса.
5. Записать конфигурацию, адреса и счётчик. На G0 индекс дескриптора выбирает
   канал DMAMUX1; режим память→память не использует периферийный запрос.
6. Вернуть InitOK с выключенным DMA. Нулевые адреса/счётчик допустимы для
   подготовки; перед запуском нужно задать аргументы передачи.

Неверные настройки не занимают ресурс и не меняют регистры. Размер элемента
одинаков для обоих портов. Методы адресов/счётчика требуют успешной настройки
и EN=0; проверяйте возвращаемые статусы.

---

## 8. Флаги: один регистр на несколько каналов

И на F4/F7 (`LISR`/`HISR`), и на G0 (`ISR`) флаги **нескольких** стримов/каналов упакованы в
один регистр группами по несколько бит — отсюда поле `if_offset` в таблице дескрипторов:

```cpp
inline void ClearFlags() { *_info->ifcr = FLAG_ALL << _info->if_offset; }
inline bool GetTC_Flag() { return (*_info->isr & (FLAG_TC << _info->if_offset)) != 0; }
inline bool GetHT_Flag() const { return (*_info->isr & (FLAG_HT << _info->if_offset)) != 0; }
inline void ClearHT_Flag() { *_info->ifcr = FLAG_HT << _info->if_offset; }
inline void ClearTC_Flag() { *_info->ifcr = FLAG_TC << _info->if_offset; }
```

`GetHT_Flag()` читает флаг половины передачи независимо от включения `IRQ::HTC`.
`ClearHT_Flag()` очищает только HT, `ClearTC_Flag()` — только TC. Остальные
флаги завершения/ошибок и флаги чужих каналов/потоков сохраняются.
Каждая очистка — одна запись в регистр очистки, без чтения/изменения/записи.
Предусловие всех трёх методов — успешный `SetUp()`. Включение источников IRQ
и обработку ошибок выполняет потребитель. `ClearFlags()` по-прежнему очищает
все флаги своего ресурса. ADC использует эти методы в отдельном DMA IRQ:
HT включается явно через IRQ_en, circular HT/TC обслуживают соответствующую
половину буфера. Без HT circular TC инвалидирует весь буфер. См. [ADC.md](ADC.md).

На F4/F7 группа занимает 6 бит, но смещения идут `0, 6, 16, 22` (не `0, 6, 12, 18` — биты 15:12
внутри `LISR`/`HISR` зарезервированы аппаратно), поэтому смещения в таблице `dma_streams[]`
прописаны явно для каждого стрима, а не вычисляются формулой. На G0 группа — 4 бита,
`if_offset = (channel_number - 1) * 4`, ровнее.

**Общие векторы G0:** Channel2/3 используют `DMA1_Channel2_3_IRQn`. Другая
линия обслуживает Channel4/5, Channel4..7 либо Channel4..7 вместе с DMA2
Channel1..5 в зависимости от МК, а также переполнение DMAMUX.
[`IRQ_Registry`](IRQ_Registry.md) допускает несколько обработчиков; на G0
предел по умолчанию равен трём. Для большего числа потребителей одной линии
его нужно переопределить через конфигурационный заголовок.
`GetTC_Flag()` каждого `DMA_Sx`-объекта проверяет **свой** `if_offset` внутри общего `ISR`, так
что делить один вектор между несколькими каналами безопасно — `Dispatch()` вызовет `HandleIRQ()`
у обоих зарегистрированных объектов, но каждый увидит только свой бит.

---

## 9. Интеграция с периферийными драйверами

[`UART`](UART.md) настраивает DMA из конструктора внутри SetUp.
[`SPI`](SPI.md) также настраивает DMA из конструктора внутри SetUp.
[`TIM`](TIM.md#8-independent-pwm-dma) настраивает DMA канала внутри SetUpPWM.
[`ADC`](ADC.md) и [`I2C`](I2C.md) также настраивают собственные DMA в SetUp.
Общие шаги настройки:

1. USART/SPI/ADC/I2C получают именованные Req F4/F7 и проверяют принадлежность
   и направление в конструкторе. Их дублирующие таблицы DMA удалены.
   TIM сохраняет существующую таблицу маршрутов каналов.
2. Заполнить `DMA_Sx::StreamSettings` (направление, адрес регистра данных, ширина `Byte`,
   `minc = true`) и вызвать `dma->SetUp(cfg)` — **не** сам код драйвера трогает `_CR()`/`_NDTR()`
   напрямую, только через публичный API `DMA_Sx`.
3. Зарегистрировать обработчик потребителя. USART/SPI/I2C регистрируют отдельные
   объекты DMA TX/RX, отличные от обработчика периферии. TIM регистрирует свой
   обработчик для каждого DMA канала; timer IRQ DMA-флаги не опрашивает.
4. Общий вектор DMA TX/RX USART, SPI или I2C требует двух мест в реестре. Каждый
   обработчик проверяет свой канал/поток.

`DMA_Sx` сам по себе не содержит логики "чей это TC" — эту семантику добавляет уже периферийный
драйвер поверх `GetTC_Flag()`/`ClearFlags()`.

---

## 10. Подробности интеграции

### Интеграция UART

CanUseRequest() проверяет маршрут DMAReq без настройки и изменения владения.
Наличие самого канала проверяет SetUp(). Если preset не задан, потребитель
передаёт выбранный req.ch в StreamSettings.
GetErrorFlags() возвращает аппаратные биты ошибок до их очистки, либо 0 до настройки.
На F4/F7 это TEIF0/DMEIF0 для direct mode, на G0 — TEIF1.
FEIF исключён из маски ошибок; ClearFlags() по-прежнему сбрасывает все флаги одной записью.

GetIRQFlags() за одно чтение ISR возвращает HT, TC и поддерживаемые ошибки.
Публичные DMA_Sx::FLAG_HT, FLAG_TC и FLAG_ERRORS разбирают этот снимок:
позиции приведены к первой группе. FEIF исключён. Флаги не очищаются, DMA не
останавливается; до SetUp возвращается 0. USART/SPI/ADC/TIM/I2C используют
единый снимок; ADC получает из него и HT без повторного чтения ISR.
TC от остановки, запрошенной после снимка, обрабатывается следующим DMA IRQ.

В UART-части Req маршрут USART1 DMA2_Stream5/CH4 назван RX_DMA2_Stream5: он обслуживает RX.
Добавлены Uart7/Uart8 и G0 Usart3..6 по наличию периферии.

### Явные имена маршрутов — 15.10.2026

Все маршруты F4/F7 содержат контроллер DMA и поток в имени. Старые TX/RX и
TX_alt/RX_alt заменены новыми именами; объявления DMA в приложении нужно обновить.
Аппаратные адреса потоков и CHSEL сохранены. DMAReq хранит целочисленный
DMAx_Streamy_BASE; указатель формируется в DMA_Sx. Имена G0 прежние: канал явно задаётся
в конструкторе. В таблицах UART/ADC/I2C/SPI заменены только ссылки на маршруты.

### Остановка SPI

SPI получает DMA в конструкторе; SPI, DMA TX и DMA RX регистрируются отдельно.
NSS отключает источники и запрашивает остановку без ожидания. F4/F7 фиксируют
остановку по TC в DMA IRQ. Для slave RX G0 последний запрос должен быть обслужен
до остановки по NSS; Process читает длину. При ошибке пакет отбрасывается.

### Независимые каналы TIM

TIM принимает до четырёх {pin, dma}. SetUpPWM настраивает DMA и отдельно
регистрирует обработчик каждого канала, в том числе при общем векторе.
Используются TIMx_CHx, CCDS=1 и отдельные CCxDE. Один UP на несколько DMA
не размножается. 16-битные таймеры используют uint16_t/Half_Word, 32-битные
TIM2/TIM5 — uint32_t/Word. Кеш TX обслуживает System. DMA IRQ проверяет только
свой ресурс, останавливает его без ожидания и публикует статус/вызывает callback.
TC относится к буферу, не к окончанию последнего PWM-периода. Изменения DIER
из main согласует вызывающий код, маскируя TIM/DMA IRQ; обработчики одного
объекта не прерывают друг друга. DeInit освобождает ресурсы из main.
SetUpPWM включает NVIC при инициализации; SendDMA восстанавливает TE/DME через
Enable_IRQ(..., false), сохраняя маску NVIC вызывающего кода.

---

## 11. Примеры использования

### G0 — отдельный DMA_Sx с явным каналом и запросом

```cpp
DMA_Sx dma_tx(DMA1_Channel4_BASE, USART::_1::DMA::TX);
DMA_Sx dma_rx(DMA1_Channel5_BASE, USART::_1::DMA::RX);
// ch2/ch3 или ch4/ch5 делят вектор — выбраны разные пары, чтобы не пересекаться с другой периферией
```

### F4/F7 — через определения _N::DMA (стрим уже зашит в DMAReq)

```cpp
DMA_Sx dma_rx(USART::_1::DMA::RX_DMA2_Stream2);     // CHSEL=4
DMA_Sx dma_rx_alt(USART::_1::DMA::RX_DMA2_Stream5); // CHSEL=4
```

### G0 — автономный перенос память→память, без периферии

Пример относится к G0. На F4/F7 режим Mem_To_Mem возвращает InitError;
поддержка переноса память→память через FIFO отложена.

```cpp
DMA_Sx::StreamSettings cfg;
cfg.direction   = DMA_Sx::DIR::Mem_To_Mem;
cfg.data_size   = DMA_Sx::SIZE::Word;
cfg.minc        = true;
cfg.pinc        = true;
DMA_Sx copier(DMA1_Channel3_BASE, /*raw request, игнорируется в mem2mem*/ 0u);
copier.SetUp(cfg);
copier.SetPerAddr(reinterpret_cast<uint32_t>(src));
copier.SetMemAddr(reinterpret_cast<uint32_t>(dst));
copier.SetCount(len);
copier.Stream_EN(ENABLE);
while (!copier.GetTC_Flag()) {}
```

### Через периферийный драйвер (типичный случай — см. UART.md/SPI.md)

```cpp
constinit USART a(USART::_3::instance, 115200, USART::_3::TX::PD8, USART::_3::RX::PD9,
        USART::_3::DMA::TX_DMA1_Stream3);
a.SetUp(); // Проверить InitOK; настраивает DMA и регистрирует его обработчик.
a.Send_DMA(buf, len);
```

---

## 12. Угловые случаи

- `SetUp()` допускает нулевые адреса/счётчик без запуска. `SetCount()` принимает
  1..65535 элементов; счётчик задаётся в элементах, а не обязательно байтах.
- Методы адресов требуют ненулевой адрес с выравниванием по ширине порта.
  Методы возвращают NotInit без владения ресурсом, Busy при EN=1 либо Error
  для неверных аргументов. При отказе регистры не меняются.
- Запрос из конструктора имеет приоритет над settings.channel. Выбранное значение
  должно помещаться в CHSEL (0..7) либо поле DMAREQ_ID выбранного МК.
  Зарезервированные ID и маршруты проверяет периферийный драйвер.
- `Stream_EN(DISABLE)` ждёт EN=0 без SysTick/таймаута и предназначен для основного
  кода. В IRQ используется RequestStop с завершением по аппаратному событию.
  Перед включением пользователь подготавливает корректные аргументы передачи.
  MINC/PINC/CIRC меняются только при остановленном канале и допустимых настройках.
- Общие IRQ зависят от модели G0: DMA1 Channel2/3 делят одну линию, другая
  обслуживает Channel4/5 либо Channel4..7, а при наличии DMA2 также его Channel1..5.
  Размер строки IRQ_Registry выбирается по числу независимых потребителей.

### Освобождение и передача ресурса

`Release()` возвращает OK после остановки и сброса своего ресурса, очистки флагов
и снятия занятости либо NotInit, если ресурс объекту не принадлежит.
На F4/F7 также сбрасываются M1AR/FIFO; на G0 — конфигурация своего канала DMAMUX
и его флаг переполнения. Выбор стрима/запроса из конструктора сохраняется.
После освобождения другой объект может занять ресурс с другим запросом.

До освобождения периферийный владелец отключает свои DMA-запросы и удаляет
свой обработчик. Release сохраняет общую NVIC-линию и тактирование контроллера.
Отмену привязки и обработку статусов в потребителях исправляем при их разборе.

### Обслуживание кеша

Потребитель вызывает `System::CacheClean` перед TX,
`System::CacheCleanInvalidate` перед RX и `System::CacheInvalidate` после
подтверждённого завершения/остановки RX. Размеры задаются в байтах.
Периферийный драйвер определяет фактически принятый диапазон до передачи данных
приложению. При прямом использовании DMA эти вызовы делает пользователь;
DMA не хранит состояние кеша/буфера.

На F7 диапазоны целиком внутри настроенных некешируемых областей пропускаются.
Кешируемый RX-буфер должен владеть всеми затронутыми 32-байтными строками,
включая padding. CPU не обращается к части, принадлежащей DMA, во время передачи.
На F4/G0 функции пустые и определены inline. Подробнее — [System.md](System.md).

---

## См. также

- [IRQ_Registry.md](IRQ_Registry.md) — периферийные драйверы регистрируют себя (не `DMA_Sx`) как
  обработчик DMA TC-прерывания; в том числе на общих векторах `DMA1_Channel2_3_IRQn`.
- [UART.md](UART.md#6-dma-кеш-и-ошибки), [SPI.md](SPI.md#8-dma-и-кеш),
  [TIM.md](TIM.md#8-независимые-pwm-dma) — потребители DMA: USART использует конструктор
  и SetUp, SPI тоже использует конструктор; TIM настраивает DMA канала из конструктора внутри SetUpPWM.
- [System.md](System.md) — `SysStatus`/`SysInitStatus`, используемые публичным API `DMA_Sx`.

- [Initialization.md](Initialization.md)
- [UART.md](UART.md)
- [SPI.md](SPI.md)
- [ADC.md](ADC.md)
- [I2C.md](I2C.md)
- [TIM.md](TIM.md)
- [System.md](System.md)
