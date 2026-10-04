# DMA / DMA_Sx — Detailed Description

*[English](#english) · [Русский](#русский)*

---

## English

## Table of Contents

1. [Why This Is Needed](#1-why-this-is-needed)
2. [Stream (F4/F7) vs Channel+DMAMUX (G0)](#2-stream-f4f7-vs-channeldmamux-g0)
3. [Register Abstraction](#3-register-abstraction)
4. [Static Descriptor Table and Protection from Double Initialization](#4-static-descriptor-table-and-protection-from-double-initialization)
5. [Req:: — Named Request Tables](#5-req--named-request-tables)
6. [SetUp() — Step by Step](#6-setup--step-by-step)
7. [Flags: One Register for Multiple Channels](#7-flags-one-register-for-multiple-channels)
8. [Integration with Peripheral Drivers](#8-integration-with-peripheral-drivers)
9. [Usage Examples](#9-usage-examples)
10. [Edge Cases](#10-edge-cases)

---

## 1. Why This Is Needed

`DMA_Sx` is one class per one DMA stream (F4/F7) or DMA channel (G0), hiding the fact that this is
physically **different hardware** across different families (see §2) behind one and the same API. Like
`USART`/`TIM`/`SPI`, it uses the pattern "a static table of peripheral facts + lookup by
pointer" — here the table (`dma_streams[]`, dma.cpp) is built not from a constant value, but by
**pointer arithmetic** (`GetStreamIndex()`, §4), because there are too many streams/channels
(16 on F4/F7) to search linearly with acceptable predictability compared to directly computing the
index.

`DMA_Sx` is usually not used by itself, but as the "engine" taken by [`USART`](UART.md),
[`SPI`](SPI.md) or [`TIM_PWM`](TIM.md#62-tim_pwm) — these drivers call `SetUp()`/
`Register()`/`Enable_IRQ()` on the `DMA_Sx*` passed to them from within their own `AttachDMA()`.

---

## 2. Stream (F4/F7) vs Channel+DMAMUX (G0)

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
     — fixed in silicon, look it up in Req::                ┌─────────────────────┐
       (see §5) or in RM0090 Table 42                        │ DMAMUX1 — a software │
                                                              │ router: ANY channel  │
                                                              │ → ANY peripheral     │
                                                              └─────────────────────┘
```

On F4/F7 the "stream ↔ peripheral" pairing is part of the silicon (for example, `USART1_TX` is **always**
`DMA2_Stream7` or `DMA2_Stream5`, never anything else); `DMA_Sx::Req::Usart1::TX`/`::TX_alt` in the
`Req::` table (§5) simply name these fixed pairs. On G0, `DMAMUX1` is a separate hardware
block between DMA1 and the peripheral: **any** of the 5 `DMA1_Channel1..5` channels can be routed to
**any** peripheral by writing the request ID into `DMAMUX1_ChannelN->CCR`; the choice of channel is
entirely up to the calling code, not the silicon.

---

## 3. Register Abstraction

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
within the flag group of one stream/channel — see §7). The entire public API (`SetMemAddr`, `SetCount`,
`ClearFlags`, `GetTC_Flag`, `Enable_IRQ`, `Stream_EN`, ...) is written once on top of these accessors
and works identically on F4/F7/G0.

---

## 4. Static Descriptor Table and Protection from Double Initialization

```cpp
struct dma_sets_typedef {
    DMA_TypeDef*        ctrl;      // DMA1 or DMA2
    volatile uint32_t*  isr;       // flag register (LISR/HISR or ISR)
    volatile uint32_t*  ifcr;      // flag-clear register (LIFCR/HIFCR or IFCR)
    uint8_t             if_offset; // bit offset of this stream within the flag group
    IRQn_Type           irqn;      // NVIC vector
    bool                used;      // SetUp() has already been called — protects against re-initialization
};
```

16 entries on F4/F7 (`DMA1`/`DMA2` × 8 streams each), 5 on G0 (`DMA1_Channel1..5`). The row
index for a specific `_stream` is **not stored**, but computed on the fly via
`GetStreamIndex()` — pointer arithmetic relative to the base address of the first stream/channel:

```cpp
// F4/F7:
if (base >= dma1_base && base <= dma1_end) return (base - dma1_base) / 0x18;       // register stride of one stream
if (base >= dma2_base && base <= dma2_end) return 8 + (base - dma2_base) / 0x18;   // DMA2 continues indexing from 8

// G0:
if (base >= ch1_base && base < ch1_base + kStreamCount * 0x14) return (base - ch1_base) / 0x14;
```

`SetUp()` finds the needed entry via `GetStreamIndex()` and checks the `used` flag — **each
`DMA_Sx` object can be configured (`SetUp()`) only once**; calling it again on a stream/channel that is
already in use returns `InitError` instead of silently re-initializing it on top of someone else's
configuration. An important consequence for G0: if two different `DMA_Sx` objects accidentally point to
the same `DMA1_ChannelN` (for example, the code was copied and the channel was forgotten to be changed) —
the second `SetUp()` fails with `InitError`, rather than silently breaking the first one.

---

## 5. Req:: — Named Request Tables

```cpp
struct DMAReq {
#if F4/F7:  DMA_Stream_TypeDef* stream; uint32_t ch; // CHSEL — both fields are required
#elif G0:   uint32_t ch;                             // only the DMAMUX1 request ID
};

struct Req {
    struct Usart1 { static constexpr DMAReq TX = {...}; static constexpr DMAReq RX = {...}; };
    // ... Spi1, Tim3, Adc1, ...
};
```

`Req::` is not about selecting registers (that's what `_CR()`/`_NDTR()`/... do), but about
**which number to write into CHSEL (F4/F7) or into DMAMUX1 (G0)** so the stream/channel listens to
exactly the right peripheral. The struct names are deliberately `CamelCase` (`Usart1`, not `USART1`) so
as not to conflict with the CMSIS macros `USART1`/`SPI1`/... (those are `#define`d as pointers to the
peripheral's registers, not identifiers). On F4/F7, where one peripheral can have more than one
hardware-valid stream, the second option is called `::TX_alt`/`::RX_alt` (for example,
`Req::Usart1::TX` = `DMA2_Stream7`, `Req::Usart1::TX_alt` = `DMA2_Stream5`).

---

## 6. SetUp() — Step by Step

```cpp
SysInitStatus SetUp(const StreamSettings& settings);
```

1. `GetStreamIndex(_stream)` → the `dma_streams[]` table; an unknown pointer or `used == true`
   → immediate `InitError`.
2. Enable the DMA controller clock (`RCC->AHB1ENR` on F4/F7 — `DMA1EN`/`DMA2EN` depending on
   which controller the stream belongs to; `RCC->AHBENR |= DMA1EN` on G0 — the same bit also enables
   `DMAMUX1` at the same time).
3. Clear `EN` and **wait** until the hardware resets it itself (`while (_CR() & CR_EN) {}`) — DMA
   does not allow changing most `CR`/`CCR` fields while the stream is active; trying to configure it
   "on the fly" without this wait would result in a register write where part of it is ignored by the
   hardware.
4. `ClearFlags()` — clear the flags from the previous use of this stream (if it was already
   active earlier in the program).
5. Determine the **effective channel/request**: if the object was constructed with a `DMAReq`
   (`_has_preset == true`) — it is used, otherwise `settings.channel` is. So a constructor called with
   `Req::Usart1::TX` always "wins" over a value accidentally left in `StreamSettings::channel`.
6. Assemble `CR`/`CCR` — direction, element size (the same for memory and peripheral — there is no
   separate "resize" case in this driver), `MINC`/`PINC`/`CIRC`. On G0, `DMAMUX1_ChannelN->CCR = eff_ch`
   is written separately (access to DMAMUX uses the same index `idx` as the channel itself — they are
   physically aligned 1:1).
7. Write `_CR()`, `_PAR()`, `_MAR()`, `_NDTR()` from `settings` (the addresses/count can be left as
   zero here and set later via `SetMemAddr()`/`SetPerAddr()`/`SetCount()` — typical for peripheral
   drivers that configure the channel once in `AttachDMA()` and pass the address/length on every
   `Send_DMA()`).

---

## 7. Flags: One Register for Multiple Channels

On both F4/F7 (`LISR`/`HISR`) and G0 (`ISR`), the flags of **several** streams/channels are
packed into one register in groups of a few bits each — hence the `if_offset` field in the descriptor
table:

```cpp
inline void ClearFlags() { *_info->ifcr = FLAG_ALL << _info->if_offset; }
inline bool GetTC_Flag() { return (*_info->isr & (FLAG_TC << _info->if_offset)) != 0; }
```

On F4/F7 the group occupies 6 bits, but the offsets go `0, 6, 16, 22` (not `0, 6, 12, 18` — bits
15:12 inside `LISR`/`HISR` are reserved by hardware), so the offsets in the `dma_streams[]` table are
written explicitly for each stream rather than computed by a formula. On G0 the group is 4 bits,
`if_offset = (channel_number - 1) * 4`, more evenly.

**A shared vector for several streams/channels** — on G0, `DMA1_Channel2_3_IRQn` serves both
channels 2 and 3, and `DMA1_Ch4_5_DMAMUX1_OVR_IRQn` serves channels 4, 5 and the DMAMUX overrun all at
once (this is exactly why [`IRQ_Registry`](IRQ_Registry.md) supports several handlers on one IRQ line,
`IRQ_MAX_SHARED = 3` for G0 — see [IRQ_Registry.md §9.3](IRQ_Registry.md#93-dmamux1-overrun-g0-специфика)).
`GetTC_Flag()` of each `DMA_Sx` object checks **its own** `if_offset` within the shared `ISR`, so sharing
one vector between several channels is safe — `Dispatch()` will call `HandleIRQ()` on both registered
objects, but each will see only its own bit.

---

## 8. Integration with Peripheral Drivers

The common `AttachDMA()` pattern is identical in [`UART`](UART.md#8-dma-интеграция) and
[`SPI`](SPI.md#7-dma-интеграция) (and partially in [`TIM_PWM`](TIM.md#62-tim_pwm)):

1. Find `tx_req`/`rx_req` in the peripheral's own table (`uart_dma_req_table`,
   `spi_dma_req_table`) by the pointer to its registers (`USARTx`/`SPIx`).
2. Fill in `DMA_Sx::StreamSettings` (direction, data register address, `Byte` width,
   `minc = true`) and call `dma->SetUp(cfg)` — the driver code itself does **not** touch `_CR()`/`_NDTR()`
   directly, only through the public `DMA_Sx` API.
3. Register **the peripheral object itself** (`this` — `USART*`/`SPI*`), rather than a separate
   DMA-specific handler, via `IRQ_Registry::Register(dma->GetIRQn(), this)` — the peripheral itself knows
   how to distinguish, inside its own `HandleIRQ()`, DMA TC from its own status flags
   (`_dma_tx->GetTC_Flag()`/`_dma_rx->GetTC_Flag()` are checked first, before reading the peripheral's
   own `SR`/`ISR`).
4. If TX and RX share the same NVIC vector — registration for the second channel is skipped
   (the check `tx->GetIRQn() != rx->GetIRQn()`), so as not to needlessly occupy a second slot of the same
   `_table` row.

`DMA_Sx` by itself contains no logic about "whose TC this is" — that semantics is added by the
peripheral driver on top of `GetTC_Flag()`/`ClearFlags()`.

---

## 9. Usage Examples

### G0 — Explicit Channel Selection + the Req:: Table

```cpp
DMA_Sx dma_tx(DMA1_Channel4, DMA_Sx::Req::Usart1::TX);
DMA_Sx dma_rx(DMA1_Channel5, DMA_Sx::Req::Usart1::RX);
// ch2/ch3 or ch4/ch5 share a vector — different pairs were chosen so as not to overlap with another peripheral
```

### F4/F7 — via the Req:: Table (the Stream Is Already Baked into DMAReq)

```cpp
DMA_Sx dma_tx(DMA_Sx::Req::Usart1::TX);       // DMA2_Stream7, CHSEL=4
DMA_Sx dma_tx_alt(DMA_Sx::Req::Usart1::TX_alt); // DMA2_Stream5, CHSEL=4 — same request, different stream
```

### Standalone Memory-to-Memory Transfer, No Peripheral

```cpp
DMA_Sx::StreamSettings cfg;
cfg.direction   = DMA_Sx::DIR::Mem_To_Mem;
cfg.data_size   = DMA_Sx::SIZE::Word;
cfg.minc        = true;
cfg.pinc        = true;
DMA_Sx copier(DMA1_Channel3, /*raw request, ignored in mem2mem*/ 0u);
copier.SetUp(cfg);
copier.SetPerAddr(reinterpret_cast<uint32_t>(src));
copier.SetMemAddr(reinterpret_cast<uint32_t>(dst));
copier.SetCount(len);
copier.Stream_EN(ENABLE);
while (!copier.GetTC_Flag()) {}
```

### Through a Peripheral Driver (the Typical Case — See UART.md/SPI.md)

```cpp
USART a(USART3, 115200, USART::_3::TX::PD8, USART::_3::RX::PD9);
DMA_Sx dma_tx(DMA_Sx::Req::Usart3::TX);
a.SetUp();
a.AttachDMA(&dma_tx, nullptr);   // SetUp() and IRQ_Registry::Register() — inside AttachDMA()
a.Send_DMA(buf, len);
```

---

## 10. Edge Cases

- **`SetUp()` can be called exactly once per object** — calling it again on the same
  physical stream/channel (even from a different `DMA_Sx` object) returns `InitError` because of the
  `used` flag in the shared `dma_streams[]` table.
- **`AttachDMA()` must be called AFTER the peripheral's `SetUp()`** — DMA is configured based on
  the already-known address of the peripheral's data register (`TDR`/`RDR`/`DR`), which does not
  depend on the peripheral's `SetUp()`, but the binding itself goes through a request table that looks up
  `USARTx`/`SPIx` — without it, `AttachDMA()` doesn't find the entry and silently fails to configure the
  DMA (see the code in [UART.md §8](UART.md#8-dma-интеграция)).
- **`settings.channel` is ignored if the object was created with a `DMAReq`** — the effective
  request is always taken from the constructor (`_has_preset`), not from `StreamSettings`, even if both
  are set and do not match.
- **On F4/F7, `settings.channel > 7` causes `InitError`** immediately in `SetUp()` (CHSEL is a
  3-bit field); on G0 there is no such check (the DMAMUX1 request ID is wider, up to 63).
- **Shared vectors on G0** (`DMA1_Channel2_3_IRQn`, `DMA1_Ch4_5_DMAMUX1_OVR_IRQn`) require
  careful selection of the channel pair for two independent peripherals that must not interfere with
  each other's response time — see the warning at the very beginning of `dma.hpp` (the "Notes" section of
  the file's doc comment).

---

## See Also

- [IRQ_Registry.md](IRQ_Registry.md) — peripheral drivers register themselves (not `DMA_Sx`) as
  the handler for the DMA TC interrupt; see §9.2 there for shared vectors `DMA1_Channel2_3_IRQn`.
- [UART.md](UART.md#8-dma-интеграция), [SPI.md](SPI.md#7-dma-интеграция),
  [TIM.md](TIM.md#62-tim_pwm) — three drivers that use `DMA_Sx` through the same `AttachDMA()`
  pattern.
- [System.md](System.md) — `SysStatus`/`SysInitStatus`, used by the public `DMA_Sx` API.

---

## Русский

## Содержание

1. [Зачем это нужно](#1-зачем-это-нужно)
2. [Stream (F4/F7) vs Channel+DMAMUX (G0)](#2-stream-f4f7-vs-channeldmamux-g0)
3. [Регистровая абстракция](#3-регистровая-абстракция)
4. [Статическая таблица дескрипторов и защита от двойной инициализации](#4-статическая-таблица-дескрипторов-и-защита-от-двойной-инициализации)
5. [Req:: — именованные таблицы запросов](#5-req--именованные-таблицы-запросов)
6. [SetUp() — пошагово](#6-setup--пошагово)
7. [Флаги: один регистр на несколько каналов](#7-флаги-один-регистр-на-несколько-каналов)
8. [Интеграция с периферийными драйверами](#8-интеграция-с-периферийными-драйверами)
9. [Примеры использования](#9-примеры-использования)
10. [Угловые случаи](#10-угловые-случаи)

---

## 1. Зачем это нужно

`DMA_Sx` — один класс на один DMA-стрим (F4/F7) или DMA-канал (G0), скрывающий то, что это
физически **разное железо** на разных семействах (см. §2), за одним и тем же API. Как и
`USART`/`TIM`/`SPI`, использует паттерн "статическая таблица фактов о периферии + поиск по
указателю" — здесь таблица (`dma_streams[]`, dma.cpp) построена не по значению-константе, а
**арифметикой указателей** (`GetStreamIndex()`, §4), потому что стримов/каналов слишком много
(16 на F4/F7), чтобы искать линейно с приемлемой предсказуемостью по сравнению с прямым расчётом
индекса.

`DMA_Sx` обычно используется не сам по себе, а как "мотор", который берёт [`USART`](UART.md),
[`SPI`](SPI.md) или [`TIM_PWM`](TIM.md#62-tim_pwm) — эти драйверы вызывают `SetUp()`/
`Register()`/`Enable_IRQ()` на переданном `DMA_Sx*` изнутри своих `AttachDMA()`.

---

## 2. Stream (F4/F7) vs Channel+DMAMUX (G0)

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
     — фиксировано в кремнии, смотреть в Req::             ┌─────────────────────┐
       (см. §5) или в RM0090 Table 42                       │ DMAMUX1 — программный │
                                                             │ маршрутизатор: ЛЮБОЙ  │
                                                             │ канал → ЛЮБАЯ периферия│
                                                             └─────────────────────┘
```

На F4/F7 связка "стрим ↔ периферия" — часть кремния (например, `USART1_TX` **всегда**
`DMA2_Stream7` или `DMA2_Stream5`, никак иначе); `DMA_Sx::Req::Usart1::TX`/`::TX_alt` в таблице
`Req::` (§5) просто называют эти зафиксированные пары. На G0 `DMAMUX1` — отдельный аппаратный
блок между DMA1 и периферией: **любой** из 5 каналов `DMA1_Channel1..5` можно направить на
**любую** периферию, записав ID запроса в `DMAMUX1_ChannelN->CCR`; выбор канала — целиком на
усмотрение вызывающего кода, а не кремния.

---

## 3. Регистровая абстракция

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
флагов одного стрима/канала — см. §7). Весь публичный API (`SetMemAddr`, `SetCount`, `ClearFlags`,
`GetTC_Flag`, `Enable_IRQ`, `Stream_EN`, ...) написан один раз поверх этих акцессоров и работает
одинаково на F4/F7/G0.

---

## 4. Статическая таблица дескрипторов и защита от двойной инициализации

```cpp
struct dma_sets_typedef {
    DMA_TypeDef*        ctrl;      // DMA1 или DMA2
    volatile uint32_t*  isr;       // регистр флагов (LISR/HISR либо ISR)
    volatile uint32_t*  ifcr;      // регистр сброса флагов (LIFCR/HIFCR либо IFCR)
    uint8_t             if_offset; // смещение бит этого стрима внутри группы флагов
    IRQn_Type           irqn;      // вектор NVIC
    bool                used;      // SetUp() уже вызывался — защита от повторной инициализации
};
```

16 записей на F4/F7 (`DMA1`/`DMA2` × 8 стримов каждый), 5 на G0 (`DMA1_Channel1..5`). Индекс
строки для конкретного `_stream` **не хранится**, а вычисляется на лету через
`GetStreamIndex()` — арифметика указателей относительно базового адреса первого стрима/канала:

```cpp
// F4/F7:
if (base >= dma1_base && base <= dma1_end) return (base - dma1_base) / 0x18;       // шаг регистров одного стрима
if (base >= dma2_base && base <= dma2_end) return 8 + (base - dma2_base) / 0x18;   // DMA2 продолжает индексацию с 8

// G0:
if (base >= ch1_base && base < ch1_base + kStreamCount * 0x14) return (base - ch1_base) / 0x14;
```

`SetUp()` находит нужную запись через `GetStreamIndex()` и проверяет флаг `used` — **каждый
`DMA_Sx`-объект можно настроить (`SetUp()`) только один раз**; повторный вызов на уже
использованном стриме/канале возвращает `InitError`, не переинициализируя его тихо поверх чужой
настройки. Важное следствие для G0: если два разных `DMA_Sx`-объекта случайно указывают на один
и тот же `DMA1_ChannelN` (например, скопировали код и забыли поменять канал) — второй `SetUp()`
провалится с `InitError`, а не молча сломает первый.

---

## 5. Req:: — именованные таблицы запросов

```cpp
struct DMAReq {
#if F4/F7:  DMA_Stream_TypeDef* stream; uint32_t ch; // CHSEL — оба поля обязательны
#elif G0:   uint32_t ch;                             // только DMAMUX1 request ID
};

struct Req {
    struct Usart1 { static constexpr DMAReq TX = {...}; static constexpr DMAReq RX = {...}; };
    // ... Spi1, Tim3, Adc1, ...
};
```

`Req::` — не про выбор регистров (это делает `_CR()`/`_NDTR()`/...), а про то, **какое число
записать в CHSEL (F4/F7) или в DMAMUX1 (G0)**, чтобы стрим/канал слушал именно нужную периферию.
Имена структур — `CamelCase` (`Usart1`, не `USART1`) специально, чтобы не конфликтовать с
CMSIS-макросами `USART1`/`SPI1`/... (те `#define`'ятся как указатели на регистры периферии, а не
идентификаторы). На F4/F7, где для одной периферии может быть больше одного аппаратно валидного
стрима, второй вариант называется `::TX_alt`/`::RX_alt` (например,
`Req::Usart1::TX` = `DMA2_Stream7`, `Req::Usart1::TX_alt` = `DMA2_Stream5`).

---

## 6. SetUp() — пошагово

```cpp
SysInitStatus SetUp(const StreamSettings& settings);
```

1. `GetStreamIndex(_stream)` → таблица `dma_streams[]`; неизвестный указатель или `used == true`
   → `InitError` немедленно.
2. Включить тактирование контроллера DMA (`RCC->AHB1ENR` на F4/F7 — `DMA1EN`/`DMA2EN` в
   зависимости от того, какому контроллеру принадлежит стрим; `RCC->AHBENR |= DMA1EN` на G0 —
   этот же бит заодно включает и `DMAMUX1`).
3. Снять `EN` и **дождаться**, пока хардвер сам его сбросит (`while (_CR() & CR_EN) {}`) — DMA
   не позволяет менять большинство полей `CR`/`CCR`, пока стрим активен; попытка настроить его
   "на лету" без этого ожидания привела бы к записи в регистр, часть которой хардвер
   проигнорирует.
4. `ClearFlags()` — снять флаги предыдущего использования этого стрима (если он уже был
   активен раньше в программе).
5. Определить **эффективный канал/запрос**: если объект был сконструирован с `DMAReq`
   (`_has_preset == true`) — используется он, иначе — `settings.channel`. Так конструктор,
   вызванный с `Req::Usart1::TX`, всегда "побеждает" значение, случайно оставленное в
   `StreamSettings::channel`.
6. Собрать `CR`/`CCR` — направление, размер элемента (одинаковый для памяти и периферии — нет
   отдельного случая "переразмерить" в этом драйвере), `MINC`/`PINC`/`CIRC`. На G0 отдельно
   пишется `DMAMUX1_ChannelN->CCR = eff_ch` (доступ к DMAMUX идёт по тому же индексу `idx`, что
   и сам канал — они физически выровнены 1:1).
7. Записать `_CR()`, `_PAR()`, `_MAR()`, `_NDTR()` из `settings` (адреса/счётчик можно оставить
   нулями здесь и задать позже через `SetMemAddr()`/`SetPerAddr()`/`SetCount()` — типично для
   периферийных драйверов, которые конфигурируют канал один раз в `AttachDMA()`, а адрес/длину
   передают на каждый `Send_DMA()`).

---

## 7. Флаги: один регистр на несколько каналов

И на F4/F7 (`LISR`/`HISR`), и на G0 (`ISR`) флаги **нескольких** стримов/каналов упакованы в
один регистр группами по несколько бит — отсюда поле `if_offset` в таблице дескрипторов:

```cpp
inline void ClearFlags() { *_info->ifcr = FLAG_ALL << _info->if_offset; }
inline bool GetTC_Flag() { return (*_info->isr & (FLAG_TC << _info->if_offset)) != 0; }
```

На F4/F7 группа занимает 6 бит, но смещения идут `0, 6, 16, 22` (не `0, 6, 12, 18` — биты 15:12
внутри `LISR`/`HISR` зарезервированы аппаратно), поэтому смещения в таблице `dma_streams[]`
прописаны явно для каждого стрима, а не вычисляются формулой. На G0 группа — 4 бита,
`if_offset = (channel_number - 1) * 4`, ровнее.

**Общий вектор на несколько стримов/каналов** — на G0 `DMA1_Channel2_3_IRQn` обслуживает оба
канала 2 и 3, а `DMA1_Ch4_5_DMAMUX1_OVR_IRQn` — каналы 4, 5 и переполнение DMAMUX разом (это и
есть повод, почему [`IRQ_Registry`](IRQ_Registry.md) поддерживает несколько обработчиков на одной
IRQ-линии, `IRQ_MAX_SHARED = 3` для G0 — см. [IRQ_Registry.md §9.3](IRQ_Registry.md#93-dmamux1-overrun-g0-специфика)).
`GetTC_Flag()` каждого `DMA_Sx`-объекта проверяет **свой** `if_offset` внутри общего `ISR`, так
что делить один вектор между несколькими каналами безопасно — `Dispatch()` вызовет `HandleIRQ()`
у обоих зарегистрированных объектов, но каждый увидит только свой бит.

---

## 8. Интеграция с периферийными драйверами

Общий паттерн `AttachDMA()` — одинаков в [`UART`](UART.md#8-dma-интеграция) и
[`SPI`](SPI.md#7-dma-интеграция) (и частично в [`TIM_PWM`](TIM.md#62-tim_pwm)):

1. Найти `tx_req`/`rx_req` в собственной таблице периферии (`uart_dma_req_table`,
   `spi_dma_req_table`) по указателю на регистры (`USARTx`/`SPIx`).
2. Заполнить `DMA_Sx::StreamSettings` (направление, адрес регистра данных, ширина `Byte`,
   `minc = true`) и вызвать `dma->SetUp(cfg)` — **не** сам код драйвера трогает `_CR()`/`_NDTR()`
   напрямую, только через публичный API `DMA_Sx`.
3. Зарегистрировать **сам периферийный объект** (`this` — `USART*`/`SPI*`), а не отдельный
   DMA-специфичный обработчик, через `IRQ_Registry::Register(dma->GetIRQn(), this)` — периферия
   сама умеет отличить в своём `HandleIRQ()` DMA TC от собственных статусных флагов
   (`_dma_tx->GetTC_Flag()`/`_dma_rx->GetTC_Flag()` проверяются первыми, до чтения `SR`/`ISR`
   самой периферии).
4. Если TX и RX делят один и тот же вектор NVIC — регистрация для второго канала пропускается
   (проверка `tx->GetIRQn() != rx->GetIRQn()`), чтобы не занимать второй слот той же строки
   `_table` без необходимости.

`DMA_Sx` сам по себе не содержит логики "чей это TC" — эту семантику добавляет уже периферийный
драйвер поверх `GetTC_Flag()`/`ClearFlags()`.

---

## 9. Примеры использования

### G0 — явный выбор канала + таблица Req::

```cpp
DMA_Sx dma_tx(DMA1_Channel4, DMA_Sx::Req::Usart1::TX);
DMA_Sx dma_rx(DMA1_Channel5, DMA_Sx::Req::Usart1::RX);
// ch2/ch3 или ch4/ch5 делят вектор — выбраны разные пары, чтобы не пересекаться с другой периферией
```

### F4/F7 — через таблицу Req:: (стрим уже зашит в DMAReq)

```cpp
DMA_Sx dma_tx(DMA_Sx::Req::Usart1::TX);       // DMA2_Stream7, CHSEL=4
DMA_Sx dma_tx_alt(DMA_Sx::Req::Usart1::TX_alt); // DMA2_Stream5, CHSEL=4 — тот же запрос, другой стрим
```

### Автономный (standalone) перенос память→память, без периферии

```cpp
DMA_Sx::StreamSettings cfg;
cfg.direction   = DMA_Sx::DIR::Mem_To_Mem;
cfg.data_size   = DMA_Sx::SIZE::Word;
cfg.minc        = true;
cfg.pinc        = true;
DMA_Sx copier(DMA1_Channel3, /*raw request, игнорируется в mem2mem*/ 0u);
copier.SetUp(cfg);
copier.SetPerAddr(reinterpret_cast<uint32_t>(src));
copier.SetMemAddr(reinterpret_cast<uint32_t>(dst));
copier.SetCount(len);
copier.Stream_EN(ENABLE);
while (!copier.GetTC_Flag()) {}
```

### Через периферийный драйвер (типичный случай — см. UART.md/SPI.md)

```cpp
USART a(USART3, 115200, USART::_3::TX::PD8, USART::_3::RX::PD9);
DMA_Sx dma_tx(DMA_Sx::Req::Usart3::TX);
a.SetUp();
a.AttachDMA(&dma_tx, nullptr);   // SetUp() и IRQ_Registry::Register() — внутри AttachDMA()
a.Send_DMA(buf, len);
```

---

## 10. Угловые случаи

- **`SetUp()` можно вызвать ровно один раз на объект** — повторный вызов на том же
  физическом стриме/канале (даже с другого `DMA_Sx`-объекта) вернёт `InitError` из-за флага
  `used` в общей таблице `dma_streams[]`.
- **`AttachDMA()` должен вызываться ПОСЛЕ `SetUp()` периферии** — DMA настраивается на основе
  уже известного адреса регистра данных периферии (`TDR`/`RDR`/`DR`), которые не зависят от
  `SetUp()` периферии, но сама привязка идёт через таблицу запросов, которая ищет `USARTx`/
  `SPIx` — без него `AttachDMA()` не находит запись и тихо не настраивает DMA (см. код в
  [UART.md §8](UART.md#8-dma-интеграция)).
- **`settings.channel` игнорируется, если объект создан с `DMAReq`** — эффективный запрос
  всегда берётся из конструктора (`_has_preset`), а не из `StreamSettings`, даже если оба
  заданы и не совпадают.
- **На F4/F7 `settings.channel > 7` — `InitError`** сразу в `SetUp()` (CHSEL — 3-битное поле);
  на G0 такой проверки нет (DMAMUX1 request ID шире, до 63).
- **Расшаренные вектора на G0** (`DMA1_Channel2_3_IRQn`, `DMA1_Ch4_5_DMAMUX1_OVR_IRQn`) требуют
  осторожного выбора пары каналов для двух независимых периферий, которые не должны мешать друг
  другу по времени реакции — см. предупреждение в самом начале `dma.hpp` (раздел "Notes" в
  doc-комментарии файла).

---

## См. также

- [IRQ_Registry.md](IRQ_Registry.md) — периферийные драйверы регистрируют себя (не `DMA_Sx`) как
  обработчик DMA TC-прерывания; см. §9.2 там про разделяемые векторы `DMA1_Channel2_3_IRQn`.
- [UART.md](UART.md#8-dma-интеграция), [SPI.md](SPI.md#7-dma-интеграция),
  [TIM.md](TIM.md#62-tim_pwm) — три драйвера, использующих `DMA_Sx` через одинаковый паттерн
  `AttachDMA()`.
- [System.md](System.md) — `SysStatus`/`SysInitStatus`, используемые публичным API `DMA_Sx`.
