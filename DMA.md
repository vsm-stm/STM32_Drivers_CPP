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
pointer". `GetStreamIndex()` (§4) calculates the index from the controller's address
range and register stride, rejecting addresses between channel/stream bases.

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
block between the available DMA controllers and request sources. The device has
5 or 7 DMA1 channels and may also have five DMA2 channels. The caller selects
an available channel and a valid request source through `DMAMUX1_ChannelN->CCR`.

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

Each descriptor stores the controller, flag registers/offset, IRQ and a used flag.
F4/F7 have 16 streams. G0 has 5 or 7 DMA1 channels, plus five DMA2 channels when present.
GetStreamIndex() checks the controller range and exact stride from CMSIS base addresses;
a pointer inside a register block is rejected. DMA2 on G0 uses descriptor/DMAMUX
indices 7..11, but its own ISR/IFCR for DMA flags.

Successful `SetUp()` assigns the hardware to that object. Repeated setup, including
a different object selecting the same channel, returns `InitError` until the owner's
`Release()`. Transfer completion retains ownership. One configured object belongs
to one consumer. SetUp/Release run in the main loop; access is serialized externally.

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
`Req::Usart1::RX` = `DMA2_Stream2`, `Req::Usart1::RX_alt` = `DMA2_Stream5`).

---

## 6. SetUp() — Step by Step

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

**Shared vectors on G0:** Channel2/3 use `DMA1_Channel2_3_IRQn`. The other vector
serves Channel4/5, Channel4..7, or Channel4..7 plus DMA2 Channel1..5, depending
on the device, and includes DMAMUX overrun. [`IRQ_Registry`](IRQ_Registry.md)
supports multiple handlers; its default G0 capacity is three, which may require
an override in the configuration header for more consumers on the same line.
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
DMA_Sx dma_rx(DMA_Sx::Req::Usart1::RX);       // DMA2_Stream2, CHSEL=4
DMA_Sx dma_rx_alt(DMA_Sx::Req::Usart1::RX_alt); // DMA2_Stream5, CHSEL=4 — same request, different stream
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

- `SetUp()` permits zero addresses/count without starting DMA. `SetCount()` accepts
  1..65535 elements; count is in elements, not necessarily bytes.
- Address setters require nonzero addresses aligned to the configured port width.
  Setters return NotInit without ownership, Busy with EN=1, or Error for invalid
  arguments. Rejected values leave registers unchanged.
- Constructor request takes precedence over settings.channel. The effective value
  must fit CHSEL (0..7) or the selected device's DMAREQ_ID field. The peripheral
  driver checks reserved IDs and routes.
- `Stream_EN(DISABLE)` waits for EN=0 without SysTick/timeouts and can run in IRQ.
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
указателю". `GetStreamIndex()` (§4) вычисляет индекс по диапазону контроллера
и шагу регистров, отклоняя адреса между началами каналов/потоков.

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
блок между доступными контроллерами DMA и источниками запросов. В зависимости
от МК есть 5 или 7 каналов DMA1 и ещё пять каналов DMA2. Пользователь выбирает
существующий канал и допустимый источник через `DMAMUX1_ChannelN->CCR`.

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

Дескриптор содержит контроллер, регистры/смещение флагов, IRQ и used.
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
`Req::Usart1::RX` = `DMA2_Stream2`, `Req::Usart1::RX_alt` = `DMA2_Stream5`).

---

## 6. SetUp() — пошагово

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
DMA_Sx dma_rx(DMA_Sx::Req::Usart1::RX);       // DMA2_Stream2, CHSEL=4
DMA_Sx dma_rx_alt(DMA_Sx::Req::Usart1::RX_alt); // DMA2_Stream5, CHSEL=4 — тот же запрос, другой стрим
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

- `SetUp()` допускает нулевые адреса/счётчик без запуска. `SetCount()` принимает
  1..65535 элементов; счётчик задаётся в элементах, а не обязательно байтах.
- Методы адресов требуют ненулевой адрес с выравниванием по ширине порта.
  Методы возвращают NotInit без владения ресурсом, Busy при EN=1 либо Error
  для неверных аргументов. При отказе регистры не меняются.
- Запрос из конструктора имеет приоритет над settings.channel. Выбранное значение
  должно помещаться в CHSEL (0..7) либо поле DMAREQ_ID выбранного МК.
  Зарезервированные ID и маршруты проверяет периферийный драйвер.
- `Stream_EN(DISABLE)` ждёт EN=0 без SysTick/таймаута; вызов допустим из IRQ.
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
  обработчик DMA TC-прерывания; см. §9.2 там про разделяемые векторы `DMA1_Channel2_3_IRQn`.
- [UART.md](UART.md#8-dma-интеграция), [SPI.md](SPI.md#7-dma-интеграция),
  [TIM.md](TIM.md#62-tim_pwm) — три драйвера, использующих `DMA_Sx` через одинаковый паттерн
  `AttachDMA()`.
- [System.md](System.md) — `SysStatus`/`SysInitStatus`, используемые публичным API `DMA_Sx`.

## UART integration: request inspection and errors / Интеграция UART

CanUseRequest(DMAReq req) const checks the constructor's stream and preset:
F4/F7 require the same stream, and an existing preset must equal req.ch.
On G0 the request may use any channel, with hardware existence checked by SetUp().
Without a constructor preset, the consumer passes the matched req.ch to SetUp().
The method does not read registers, configure DMA or acquire ownership.

GetErrorFlags() const returns normalized hardware status bits:
F4/F7 TEIF0/DMEIF0/FEIF0, G0 TEIF1; zero when not configured.
Read before ClearFlags(). UART converts these into DmaTx/DmaRx callback indicators.
UART enables TC/TE and F4/F7 DME; FIFO mode is not used for its byte transfers.

CanUseRequest() проверяет маршрут DMAReq без настройки и изменения владения.
Наличие самого канала проверяет SetUp(). Если preset не задан, потребитель
передаёт выбранный req.ch в StreamSettings.
GetErrorFlags() возвращает аппаратные биты ошибок до их очистки, либо 0 до настройки.

В UART-части Req исправлено USART1::TX_alt -> RX_alt: DMA2_Stream5/CH4 обслуживает RX.
Добавлены Uart7/Uart8 и G0 Usart3..6 по наличию периферии.
