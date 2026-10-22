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

`DMA_Sx` is one object per DMA **stream** (F4/F7) or DMA **channel** (G0). The hardware differs a lot between families (section 3); `DMA_Sx` hides that behind one API.

**Usually you do not create `DMA_Sx` yourself.** It is the "engine" inside [USART](UART.md), [SPI](SPI.md), [I2C](I2C.md), [ADC](ADC.md) and [TIM](TIM.md#8-independent-pwm-dma). Those drivers own their DMA objects, call `SetUp()`, register the interrupt and enable it:

| Driver | Where the DMA is chosen | Where it is configured |
| --- | --- | --- |
| USART, SPI, I2C, ADC | constructor (named route on F4/F7, physical channel on G0) | `SetUp()` |
| TIM | constructor, together with each channel pin | `SetUpPWM()` |

Standalone use (memory-to-memory, custom peripherals) is possible too — see section 11.

**Interrupt enable** (after a successful `SetUp()` and handler registration):

- `Enable_IRQ(irq, enable_nvic = true)` enables the DMA interrupt source and, by default, the NVIC line. With `false`, only the source is enabled; NVIC enable/pending state is left as is.
- `Disable_IRQ()` disables only the source.
- Neither clears flags nor starts a transfer.

**`DMA_USE_ATOMIC`** (in `dma.hpp`, default `0`): `1` switches CR/CCR and RCC read-modify-write to ST `ATOMIC_*` macros. Rebuild DMA and its users after changing it. Individual register updates become atomic; acquisition, buffer ownership and multi-step configuration still need external serialization.

---

## 2. Constant Initialization and Constructor Diagnostics

Declare fixed objects at file scope with `constinit` (or as `static constinit` inside a function). A standalone `DMA_Sx` takes either a numeric stream/channel base address or a named peripheral route. Peripheral drivers own their embedded DMA objects — do not create a second one for the same resource.

What happens with a wrong binding:

| Declaration | Result |
| --- | --- |
| `constinit` object | **Compilation error** with the reason text in the message. |
| Object without `constinit` | Compiles; at start-up the constructor stops in `System::DebugTrap(reason)`. |

Every check calls `STM32_CONFIG_ERROR("reason")`: at compile time a GCC error containing the reason, at run time `System::DebugTrap(text)`. `constinit` is required by the usage contract but not enforced. `SetUp()` still checks resource ownership and transfer parameters. Details: [Initialization.md](Initialization.md).

---

## 3. Stream (F4/F7) vs Channel+DMAMUX (G0)

```text
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

| | F4/F7 | G0 |
| --- | --- | --- |
| Unit | stream (8 per controller, 16 total) | channel: 5 or 7 on DMA1, plus 5 on DMA2 where present |
| Who connects it to a peripheral | silicon: each stream has a fixed list of requests, selected by CHSEL | DMAMUX1: any channel can take any request ID |
| What you choose | one of the predefined routes, e.g. `USART::_3::DMA::TX_DMA1_Stream3` or `TX_DMA1_Stream4` — the name shows the occupied resource | any free channel, e.g. `DMA1_Channel4_BASE`; the request ID comes from the peripheral |

---

## 4. Register Abstraction

As in [UART](UART.md)/[SPI](SPI.md), the register name differences are hidden behind small private accessors and `constexpr` masks:

```cpp
volatile uint32_t& _CR();    // ->CR   (F4/F7, DMA_Stream_TypeDef) | ->CCR  (G0, DMA_Channel_TypeDef)
volatile uint32_t& _NDTR();  // ->NDTR                              | ->CNDTR
volatile uint32_t& _PAR();   // ->PAR                               | ->CPAR
volatile uint32_t& _MAR();   // ->M0AR                              | ->CMAR
```

Plus masks with family-neutral names:

- `CR_EN`/`CR_MINC`/`CR_PINC`/`CR_CIRC` — same meaning, different CMSIS macros (`DMA_SxCR_EN` vs `DMA_CCR_EN`);
- `FLAG_TC`/`FLAG_ALL` — positions inside one stream's/channel's flag group (section 8).

The whole public API (`SetMemAddr`, `SetCount`, `ClearFlags`, `GetTC_Flag`, `Enable_IRQ`, `Stream_EN`, …) is written once on top of these and behaves identically on F4/F7/G0.

---

## 5. Static Descriptor Table and Protection from Double Initialization

**Descriptors.** Each hardware stream/channel has a descriptor: controller, flag registers + bit offset, IRQ number.

- `dma_streams` — a `static const` table in **Flash**.
- `dma_used` — a separate zero-initialized "occupied" array in **RAM** with the same indices.
- The object's `_info` points to its const descriptor. No extra per-object index is stored.

**Index lookup.** `GetStreamIndex()` computes the index from the controller's address range and the exact register stride taken from CMSIS base addresses. An address pointing *inside* a register block is rejected. G0 DMA2 uses descriptor/DMAMUX indices 7…11 but its own ISR/IFCR.

**Ownership:**

- A successful `SetUp()` marks the resource occupied by this object.
- A second `SetUp()` — by the same or another object on the same resource — returns `InitError` until the owner calls `Release()`.
- Finishing a transfer keeps ownership.
- `SetUp()`/`Release()` run in the main loop; access is serialized by the caller.

---

## 6. _N::DMA — Named Request Tables

The value type is shared by all drivers:

```cpp
struct DMAReq {
#if defined(STM32F4) || defined(STM32F7)
    uintptr_t stream;   // DMAx_Streamy_BASE
    uint32_t  ch;       // CHSEL (0..7)
#elif defined(STM32G0)
    uint32_t  ch;       // DMAMUX1 request ID
#endif
    uintptr_t   periph_base;  // owning peripheral (0 = consumer validates its own route)
    DMA_Sx::DIR direction;    // To_Per / From_Per
};
```

The **named routes live with each peripheral**, in its `*_defs.hpp`, next to its pins:

```cpp
struct USART::_1 {
    struct DMA {
#if defined(STM32F4) || defined(STM32F7)
        static constexpr DMA_Sx::DMAReq TX_DMA2_Stream7 = {DMA2_Stream7_BASE, 4U, USART1_BASE, DMA_Sx::DIR::To_Per};
        static constexpr DMA_Sx::DMAReq RX_DMA2_Stream2 = {DMA2_Stream2_BASE, 4U, USART1_BASE, DMA_Sx::DIR::From_Per};
        static constexpr DMA_Sx::DMAReq RX_DMA2_Stream5 = {DMA2_Stream5_BASE, 4U, USART1_BASE, DMA_Sx::DIR::From_Per};
#elif defined(STM32G0)
        static constexpr DMA_Sx::DMAReq TX = {51U, USART1_BASE, DMA_Sx::DIR::To_Per};
        static constexpr DMA_Sx::DMAReq RX = {50U, USART1_BASE, DMA_Sx::DIR::From_Per};
#endif
    };
    // instance, TX/RX pins ...
};
```

- These tables answer **which number goes into CHSEL (F4/F7) or DMAMUX1 (G0)** so the stream/channel listens to the right peripheral. Register selection is the accessors' job (section 4).
- F4/F7 names include controller and stream (`RX_DMA2_Stream2`, `RX_DMA2_Stream5`), so the occupied resource is visible.
- G0 keeps plain `TX`/`RX`: the physical channel is chosen separately in the constructor.
- The old `DMA_Sx::Req::Usart1`-style tables were moved into the peripherals; include the peripheral's header to use its routes.

---

## 7. SetUp() — Step by Step

1. **Resolve and claim.** Find the exact hardware resource; reject it if already owned.
2. **Validate.** Take the constructor's request, or `settings.channel` when the constructor gave none. Check the effective CHSEL/request ID, direction, data size, address alignment and count (0…65535).
   - `Mem_To_Mem` is supported **only on G0** and cannot be circular; F4/F7 return `InitError` before touching hardware.
   - Whether the request is valid for this peripheral on this device is checked by the peripheral driver.
3. **Clock.** Enable the DMA controller clock (on G0 this also clocks DMAMUX1).
4. **Stop.** `Stream_EN(DISABLE)`: clear EN and wait for EN = 0 (no timeout). Clear this resource's flags.
5. **Program.** Write configuration, addresses and count. On G0 the descriptor index selects the DMAMUX1 channel; memory-to-memory uses no request.
6. **Return `InitOK`** with DMA disabled. Zero addresses/count are allowed for deferred preparation — set them before starting.

Rejected settings claim nothing and change no registers. Element size is the same for both ports. Address/count setters require a successful setup and EN = 0; check their return values.

---

## 8. Flags: One Register for Multiple Channels

Both families pack the flags of **several** streams/channels into one register (F4/F7: `LISR`/`HISR`; G0: `ISR`). Each stream/channel has a small group of bits at offset `if_offset`:

```cpp
inline void ClearFlags() { *_info->ifcr = FLAG_ALL << _info->if_offset; }
inline bool GetTC_Flag() { return (*_info->isr & (FLAG_TC << _info->if_offset)) != 0; }
inline bool GetHT_Flag() const { return (*_info->isr & (FLAG_HT << _info->if_offset)) != 0; }
inline void ClearHT_Flag() { *_info->ifcr = FLAG_HT << _info->if_offset; }
inline void ClearTC_Flag() { *_info->ifcr = FLAG_TC << _info->if_offset; }
```

- `GetHT_Flag()` reads the half-transfer flag whether or not the HT interrupt is enabled.
- `ClearHT_Flag()`/`ClearTC_Flag()` clear only that flag; `ClearFlags()` clears all flags of this resource. Other streams' flags are never touched.
- Each clear is one write to the clear register (no read-modify-write).
- All require a successful `SetUp()`. Enabling interrupt sources and handling errors is the consumer's job.
- ADC uses these together with `GetIRQFlags()` in its DMA handler: HT is enabled via `ADC_N::IRQ_en`, and circular HT/TC invalidate the matching buffer half (the whole buffer if HT is off). See [ADC.md](ADC.md).

**Offsets:**

| Family | Bits per group | Offsets |
| --- | --- | --- |
| F4/F7 | 6 | `0, 6, 16, 22` per register — bits 15:12 are reserved, so offsets are listed explicitly in `dma_streams[]` |
| G0 | 4 | `(channel_number - 1) * 4` |

**Shared vectors on G0:**

- Channel2/3 share `DMA1_Channel2_3_IRQn`.
- The other vector serves Channel4/5, or Channel4…7, or Channel4…7 plus DMA2 Channel1…5 (device-dependent), plus DMAMUX overrun.
- [IRQ_Registry](IRQ_Registry.md) allows several handlers per vector; the default capacity is 3 — raise it in the settings header if more consumers share a line.
- Sharing is safe: `Dispatch()` calls every registered handler, and each `DMA_Sx` checks only **its own** `if_offset`.

---

## 9. Integration with Peripheral Drivers

### Nonblocking stop used by USART

| Method | Behaviour | Where to call |
| --- | --- | --- |
| `RequestStop()` | clears EN once and returns immediately | interrupt or main |
| `IsEnabled()` | reads EN once | anywhere |
| `Stream_EN(DISABLE)` | clears EN and **waits** for EN = 0 | main loop only, never in an interrupt |

Both `RequestStop()` and `IsEnabled()` require a successful `SetUp()` and do not clear status flags.

- **F4/F7:** a software stop produces TC; USART finishes the stopped RX packet in its DMA RX handler after that event.
- **G0:** an abort in the middle of a transfer does not guarantee an accurate CNDTR. USART therefore uses a simple IDLE stop: disable DMAR, disable DMA without waiting, report the packet in the USART interrupt. The last request must already be serviced; failed packets are discarded without using CNDTR. This is not a recovery API for arbitrary aborts.

### How peripheral drivers use DMA_Sx

[UART](UART.md), [SPI](SPI.md), [ADC](ADC.md) and [I2C](I2C.md) configure their constructor-bound DMA in `SetUp()`; [TIM](TIM.md#8-independent-pwm-dma) configures channel-bound DMA in `SetUpPWM()`. Common steps:

1. The constructor receives a named F4/F7 route (or G0 channel) and checks owner/direction at compile time. No duplicate DMA tables remain in USART/SPI/ADC/I2C; TIM keeps its channel route map.
2. The driver fills `DMA_Sx::StreamSettings` (direction, data-register address, width, `minc = true`) and calls `dma.SetUp(cfg)`. Drivers never touch `_CR()`/`_NDTR()` directly — only the public `DMA_Sx` API.
3. The driver registers its own handlers: USART/SPI/I2C register separate DMA TX and DMA RX handlers (distinct from the peripheral handler); TIM registers one handler per channel DMA.
4. When TX and RX DMA share an NVIC vector, that still takes two registry slots; each handler checks only its own flags.

`DMA_Sx` has no idea "whose TC this is" — that meaning is added by the peripheral driver on top of `GetTC_Flag()`/`GetIRQFlags()`/`ClearFlags()`.

---

## 10. Integration Details

### UART integration: request inspection and errors

**`CanUseRequest(DMAReq req) const`** — checks a route against the constructor's stream and preset, without touching registers or ownership:

- F4/F7: same stream required; an existing preset must equal `req.ch`.
- G0: any channel; its hardware existence is checked by `SetUp()`.
- Without a constructor preset, the consumer passes the matched `req.ch` to `SetUp()`.

**`GetErrorFlags() const`** — normalized error bits (0 before setup): F4/F7 TEIF and DMEIF (direct mode), G0 TEIF. FIFO error (FEIF) is excluded; `ClearFlags()` still clears everything. Read it **before** `ClearFlags()`. UART converts it into its `DmaTx`/`DmaRx` error bits; it enables TC/TE and, on F4/F7, DME (FIFO mode is not used).

**`GetIRQFlags()`** — reads HT, TC and supported errors in **one** ISR access; decode with the public `DMA_Sx::FLAG_HT`, `FLAG_TC`, `FLAG_ERRORS`. FEIF excluded. It neither clears flags nor stops DMA, and returns 0 before `SetUp()`. USART/SPI/ADC/TIM/I2C use it in their DMA handlers. A TC caused by a stop requested after the snapshot is handled in the next DMA interrupt.

### Explicit route names

All F4/F7 routes include controller and stream: `USART::_3::DMA::TX_DMA1_Stream3`, `SPI::_1::DMA::RX_DMA2_Stream0`, `ADC_N::_1::DMA::RX_DMA2_Stream4`.

- They replace the old `TX`/`RX` and `TX_alt`/`RX_alt` names — update application declarations.
- Stream addresses and CHSEL values are unchanged. `DMAReq` stores the integer `DMAx_Streamy_BASE`; `DMA_Sx` turns it into a pointer.
- G0 keeps `TX`/`RX`: the constructor already takes an explicit channel.
- USART1 DMA2_Stream5 / CH4 is named `RX_DMA2_Stream5` (it serves RX). Uart7/Uart8 and G0 Usart3…6 routes exist where those peripherals do.

### SPI transfer preparation

`PrepareTransfer(uint32_t addr, uint32_t count, FunctionalState minc, uint32_t irq_mask)`
is an inline low-level operation for an **owned, stopped** DMA with peripheral
requests disabled. The caller guarantees a valid aligned memory address,
1..65535 elements, valid MINC and IRQ source bits (OR of `DMA_Sx::IRQ` values).
There are no argument, ownership or EN checks. It writes the memory address and
count, updates MINC and the supplied IRQ bits in one control-register operation,
and clears flags for this resource. Other control bits are preserved. It neither
accesses NVIC nor starts/waits for DMA or maintains cache. SPI uses it after its
status/buffer/DMA checks, passing `irq_mask=0`: SPI keeps the IRQ sources
configured once in `SetUpDMA()`. The checked individual setters remain available.

### SPI stop

SPI binds DMA in the constructor; SPI, DMA TX and DMA RX handlers are registered separately. On NSS release it only disables sources and requests a stop — no waiting in interrupts. F4/F7 collect the stop TC in the DMA interrupt. G0 slave RX requires the last request to be serviced before the NSS stop; `Process()` reads the count. Errors discard the packet.

### Independent TIM channels

- TIM accepts up to four `{pin, dma}` bindings. `SetUpPWM()` acquires each channel's DMA and registers a separate handler, even on shared vectors.
- Each PWM DMA uses the TIMx_CHx request with CCDS = 1 and its own CCxDE gate — no TIMx_UP fan-out.
- 16-bit timers use `uint16_t`/Half_Word; 32-bit TIM2/TIM5 use `uint32_t`/Word.
- System cleans the TX buffer from the cache on F7.
- Each DMA handler reads only its own flags, stops without waiting, publishes per-channel OK/Error and calls its callback; other channels keep running. TC means "buffer sent", not "last PWM period finished on the pin".
- The caller masks the affected TIM/DMA interrupts around main-side DIER changes and prevents these handlers from preempting each other. `DeInit()` releases DMA and registrations from `main`.
- `SetUpPWM()` enables NVIC during initialization; `SendDMA()` re-arms TE/DME via `Enable_IRQ(..., false)`, keeping the caller's NVIC mask. See [TIM.md](TIM.md).

---

## 11. Usage Examples

### G0 — Standalone DMA_Sx with explicit channel and request

```cpp
constinit DMA_Sx dma_tx(DMA1_Channel4_BASE, USART::_1::DMA::TX);
constinit DMA_Sx dma_rx(DMA1_Channel5_BASE, USART::_1::DMA::RX);
// Ch2/Ch3 and Ch4/Ch5 share vectors — pick pairs that do not collide with other peripherals.
```

### F4/F7 — via the peripheral DMA definitions (the Stream Is Already Baked into DMAReq)

```cpp
constinit DMA_Sx dma_rx(USART::_1::DMA::RX_DMA2_Stream2);       // CHSEL = 4
// Alternative route for the same request (not at the same time):
// constinit DMA_Sx dma_rx(USART::_1::DMA::RX_DMA2_Stream5);    // CHSEL = 4
```

### G0 — Standalone Memory-to-Memory Transfer, No Peripheral

G0 only — F4/F7 return `InitError` for `Mem_To_Mem` (FIFO-based memory-to-memory is not implemented).

```cpp
static constinit DMA_Sx copier(DMA1_Channel3_BASE, /*request, ignored for mem2mem*/ 0u);

DMA_Sx::StreamSettings cfg;
cfg.direction = DMA_Sx::DIR::Mem_To_Mem;
cfg.data_size = DMA_Sx::SIZE::Word;
cfg.minc      = true;
cfg.pinc      = true;

if (copier.SetUp(cfg) == SysInitStatus::InitOK
    && copier.SetPerAddr(reinterpret_cast<uint32_t>(src)) == SysStatus::OK
    && copier.SetMemAddr(reinterpret_cast<uint32_t>(dst)) == SysStatus::OK
    && copier.SetCount(len) == SysStatus::OK) {
    copier.Stream_EN(ENABLE);
    while (!copier.GetTC_Flag()) {}
}
```

### Through a Peripheral Driver (the Typical Case — See UART.md/SPI.md)

```cpp
constinit USART a(USART::_3::instance, 115200, USART::_3::TX::PD8, USART::_3::RX::PD9,
                  USART::_3::DMA::TX_DMA1_Stream3);

if (a.SetUp() == SysInitStatus::InitOK)   // configures DMA and registers its handler
    a.Send_DMA(buf, len);
```

---

## 12. Edge Cases

- **Count.** `SetUp()` allows zero addresses/count without starting. `SetCount()` accepts 1…65535 **elements** (not necessarily bytes).
- **Address setters** require non-zero addresses aligned to the port width. They return `NotInit` if the object owns no resource, `Busy` while EN = 1, `Error` for invalid arguments; rejected values leave registers unchanged.
- **Request priority.** The constructor's request wins over `settings.channel`. The value must fit CHSEL (0…7) or the device's DMAREQ_ID field. Reserved IDs and routes are checked by the peripheral driver.
- **Stopping.** `Stream_EN(DISABLE)` waits for EN = 0 without SysTick or timeout — main loop only. Interrupt code uses `RequestStop()` and waits for the hardware completion event.
- **Before enabling**, the caller prepares valid transfer arguments. MINC/PINC/CIRC may change only on a stopped channel.
- **G0 shared vectors** depend on the device (section 8). Size `IRQ_MAX_SHARED` for the real number of independent consumers.

### Release and handover

`Release()`:

- returns `OK` after stopping and resetting the owned resource, clearing its flags and releasing ownership; `NotInit` if the object owns nothing;
- F4/F7: also resets M1AR/FIFO; G0: clears its DMAMUX channel configuration and overrun flag;
- keeps the constructor's stream/request selection;
- leaves shared NVIC lines and controller clocks enabled.

Afterwards another object may claim the hardware with a different request. **Before** releasing, the peripheral owner disables its DMA requests and unregisters its handler.

### Cache maintenance

| Moment | Call (sizes in bytes) |
| --- | --- |
| before TX | `System::CacheClean` |
| before RX | `System::CacheCleanInvalidate` |
| after confirmed RX completion/stop | `System::CacheInvalidate` |

- Peripheral drivers do this themselves and determine the actually received range before handing data to the application. Direct `DMA_Sx` users must make these calls; `DMA_Sx` keeps no cache/buffer state.
- On F7 ranges wholly inside configured non-cacheable regions are skipped. Cached RX buffers must own every 32-byte line they touch, including padding. The CPU must not access the DMA-owned part during a transfer.
- On F4/G0 the helpers are empty inline functions. See [System.md](System.md).

---

## See Also

- [IRQ_Registry.md](IRQ_Registry.md) — peripheral drivers (not `DMA_Sx`) register themselves as DMA interrupt handlers, including on shared vectors such as `DMA1_Channel2_3_IRQn`.
- [UART.md](UART.md#6-dma-кеш-и-ошибки), [SPI.md](SPI.md#8-dma-и-кеш), [TIM.md](TIM.md#8-независимые-pwm-dma) — DMA consumers: USART and SPI bind DMA in the constructor and configure it in `SetUp()`; TIM configures channel DMA in `SetUpPWM()`.
- [System.md](System.md) — `SysStatus`/`SysInitStatus`, cache helpers.
- [Initialization.md](Initialization.md)
- [UART.md](UART.md)
- [SPI.md](SPI.md)
- [ADC.md](ADC.md)
- [I2C.md](I2C.md)
- [TIM.md](TIM.md)

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

Объявляйте постоянные объекты глобально с `constinit` (или как `static constinit` внутри функции). Самостоятельный `DMA_Sx` принимает числовой базовый адрес потока/канала либо именованный маршрут периферии. Драйверы периферии сами содержат свои DMA — не создавайте второй объект для того же ресурса.

Что происходит при неверной привязке:

| Объявление | Результат |
| --- | --- |
| Объект с `constinit` | **Ошибка компиляции**, текст причины — в сообщении. |
| Объект без `constinit` | Собирается; при старте конструктор останавливается в `System::DebugTrap(причина)`. |

Каждая проверка вызывает `STM32_CONFIG_ERROR("причина")`: при компиляции — ошибка GCC с текстом причины, во время работы — `System::DebugTrap(text)`. `constinit` обязателен по правилам использования, но не проверяется. `SetUp()` по-прежнему проверяет занятость ресурса и параметры передачи. Подробнее: [Initialization.md](Initialization.md).

---

## 2. Зачем это нужно

`DMA_Sx` — один объект на **поток** DMA (F4/F7) или **канал** DMA (G0). Аппаратура семейств сильно различается (раздел 3); `DMA_Sx` прячет это за единым API.

**Обычно `DMA_Sx` не создают вручную.** Это «двигатель» внутри [USART](UART.md), [SPI](SPI.md), [I2C](I2C.md), [ADC](ADC.md) и [TIM](TIM.md#8-independent-pwm-dma). Эти драйверы владеют своими DMA, вызывают `SetUp()`, регистрируют прерывание и включают его:

| Драйвер | Где выбирается DMA | Где настраивается |
| --- | --- | --- |
| USART, SPI, I2C, ADC | конструктор (именованный маршрут на F4/F7, физический канал на G0) | `SetUp()` |
| TIM | конструктор, вместе с выводом каждого канала | `SetUpPWM()` |

Самостоятельное использование (память→память, своя периферия) тоже возможно — см. раздел 11.

Индекс ресурса `GetStreamIndex()` (раздел 5) вычисляет по диапазону адресов контроллера и шагу регистров, отклоняя адреса между началами потоков/каналов.

**Включение прерывания** (после успешного `SetUp()` и регистрации обработчика):

- `Enable_IRQ(irq, enable_nvic = true)` включает источник прерывания DMA и по умолчанию линию NVIC. С `false` включается только источник; разрешение и флаг ожидания NVIC не меняются.
- `Disable_IRQ()` выключает только источник.
- Ни один из методов не сбрасывает флаги и не запускает передачу.

**`DMA_USE_ATOMIC`** (в `dma.hpp`, по умолчанию `0`): `1` переключает «прочитать-изменить-записать» CR/CCR и RCC на макросы ST `ATOMIC_*`. После изменения пересоберите DMA и его потребителей. Атомарными становятся отдельные изменения регистров; захват ресурса, владение буфером и многошаговая настройка по-прежнему требуют внешнего согласования.

---

## 3. Stream (F4/F7) vs Channel+DMAMUX (G0)

```text
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
     "Какой поток обслуживает USART1_TX?"                        ▼
     — фиксировано в кремнии, смотреть в _N::DMA             ┌─────────────────────┐
       (см. §6) или в RM0090 Table 42                       │ DMAMUX1 — программный │
                                                             │ маршрутизатор: ЛЮБОЙ  │
                                                             │ канал → ЛЮБАЯ периферия│
                                                             └─────────────────────┘
```

| | F4/F7 | G0 |
| --- | --- | --- |
| Единица | поток (8 на контроллер, всего 16) | канал: 5 или 7 у DMA1, плюс 5 у DMA2, если он есть |
| Кто связывает с периферией | кремний: у каждого потока фиксированный список запросов, выбор через CHSEL | DMAMUX1: любой канал может взять любой номер запроса |
| Что выбираете вы | один из готовых маршрутов, например `USART::_3::DMA::TX_DMA1_Stream3` или `TX_DMA1_Stream4` — имя показывает занятый ресурс | любой свободный канал, например `DMA1_Channel4_BASE`; номер запроса подставляет периферия |

---

## 4. Регистровая абстракция

Как и в [UART](UART.md)/[SPI](SPI.md), различия имён регистров спрятаны за небольшими приватными функциями доступа и `constexpr`-масками:

```cpp
volatile uint32_t& _CR();    // ->CR   (F4/F7, DMA_Stream_TypeDef) | ->CCR  (G0, DMA_Channel_TypeDef)
volatile uint32_t& _NDTR();  // ->NDTR                              | ->CNDTR
volatile uint32_t& _PAR();   // ->PAR                               | ->CPAR
volatile uint32_t& _MAR();   // ->M0AR                              | ->CMAR
```

Плюс маски с общими для семейств именами:

- `CR_EN`/`CR_MINC`/`CR_PINC`/`CR_CIRC` — одинаковый смысл, разные макросы CMSIS (`DMA_SxCR_EN` и `DMA_CCR_EN`);
- `FLAG_TC`/`FLAG_ALL` — позиции внутри группы флагов одного потока/канала (раздел 8).

Весь публичный API (`SetMemAddr`, `SetCount`, `ClearFlags`, `GetTC_Flag`, `Enable_IRQ`, `Stream_EN`, …) написан один раз поверх них и работает одинаково на F4/F7/G0.

---

## 5. Статическая таблица дескрипторов и защита от двойной инициализации

**Дескрипторы.** У каждого аппаратного потока/канала есть дескриптор: контроллер, регистры флагов и смещение битов, номер IRQ.

- `dma_streams` — таблица `static const` во **Flash**.
- `dma_used` — отдельный обнуляемый массив занятости в **RAM** с теми же индексами.
- `_info` объекта указывает на константный дескриптор. Дополнительный индекс в объекте не хранится.

**Поиск индекса.** `GetStreamIndex()` вычисляет индекс по диапазону адресов контроллера и точному шагу регистров из базовых адресов CMSIS. Адрес, указывающий *внутрь* блока регистров, отклоняется. DMA2 на G0 использует индексы дескрипторов/DMAMUX 7…11, но свои ISR/IFCR.

**Владение:**

- Успешный `SetUp()` помечает ресурс занятым этим объектом.
- Повторный `SetUp()` — этим или другим объектом на том же ресурсе — возвращает `InitError` до `Release()` владельца.
- Завершение передачи владение не снимает.
- `SetUp()`/`Release()` выполняются в основном цикле; согласование доступа — за вызывающим кодом.

---

## 6. _N::DMA — именованные таблицы запросов

Тип значения общий для всех драйверов:

```cpp
struct DMAReq {
#if defined(STM32F4) || defined(STM32F7)
    uintptr_t stream;   // DMAx_Streamy_BASE
    uint32_t  ch;       // CHSEL (0..7)
#elif defined(STM32G0)
    uint32_t  ch;       // номер запроса DMAMUX1
#endif
    uintptr_t   periph_base;  // периферия-владелец (0 = маршрут проверяет сам потребитель)
    DMA_Sx::DIR direction;    // To_Per / From_Per
};
```

**Именованные маршруты находятся у каждой периферии**, в её `*_defs.hpp`, рядом с выводами:

```cpp
struct USART::_1 {
    struct DMA {
#if defined(STM32F4) || defined(STM32F7)
        static constexpr DMA_Sx::DMAReq TX_DMA2_Stream7 = {DMA2_Stream7_BASE, 4U, USART1_BASE, DMA_Sx::DIR::To_Per};
        static constexpr DMA_Sx::DMAReq RX_DMA2_Stream2 = {DMA2_Stream2_BASE, 4U, USART1_BASE, DMA_Sx::DIR::From_Per};
        static constexpr DMA_Sx::DMAReq RX_DMA2_Stream5 = {DMA2_Stream5_BASE, 4U, USART1_BASE, DMA_Sx::DIR::From_Per};
#elif defined(STM32G0)
        static constexpr DMA_Sx::DMAReq TX = {51U, USART1_BASE, DMA_Sx::DIR::To_Per};
        static constexpr DMA_Sx::DMAReq RX = {50U, USART1_BASE, DMA_Sx::DIR::From_Per};
#endif
    };
    // instance, выводы TX/RX ...
};
```

- Эти таблицы отвечают на вопрос, **какое число записать в CHSEL (F4/F7) или в DMAMUX1 (G0)**, чтобы поток/канал слушал нужную периферию. Выбор регистров — задача функций доступа (раздел 4).
- Имена F4/F7 содержат контроллер и поток (`RX_DMA2_Stream2`, `RX_DMA2_Stream5`), поэтому занятый ресурс виден сразу.
- На G0 остаются `TX`/`RX`: физический канал задаётся отдельно в конструкторе.
- Старые таблицы вида `DMA_Sx::Req::Usart1` перенесены в периферию; чтобы использовать маршруты, подключите заголовок соответствующей периферии.

---

## 7. SetUp() — пошагово

1. **Найти и занять.** Определить точный аппаратный ресурс; если он уже занят — отказ.
2. **Проверить.** Взять запрос из конструктора, либо `settings.channel`, если в конструкторе его нет. Проверить итоговый CHSEL/номер запроса, направление, размер данных, выравнивание адресов и счётчик (0…65535).
   - `Mem_To_Mem` поддерживается **только на G0** и без кольцевого режима; F4/F7 возвращают `InitError` до изменения регистров.
   - Допустимость запроса для данной периферии на данном МК проверяет драйвер периферии.
3. **Тактирование.** Включить тактирование контроллера DMA (на G0 это включает и DMAMUX1).
4. **Остановка.** `Stream_EN(DISABLE)`: снять EN и ждать EN = 0 (без таймаута). Сбросить флаги этого ресурса.
5. **Запись.** Записать конфигурацию, адреса и счётчик. На G0 индекс дескриптора выбирает канал DMAMUX1; режим память→память запрос не использует.
6. **Вернуть `InitOK`** с выключенным DMA. Нулевые адреса/счётчик допустимы для отложенной подготовки — задайте их перед запуском.

Отклонённые настройки ничего не занимают и не меняют регистры. Размер элемента одинаков для обоих портов. Методы адресов/счётчика требуют успешной настройки и EN = 0; проверяйте возвращаемые статусы.

---

## 8. Флаги: один регистр на несколько каналов

Оба семейства упаковывают флаги **нескольких** потоков/каналов в один регистр (F4/F7: `LISR`/`HISR`; G0: `ISR`). У каждого потока/канала своя небольшая группа бит со смещением `if_offset`:

```cpp
inline void ClearFlags() { *_info->ifcr = FLAG_ALL << _info->if_offset; }
inline bool GetTC_Flag() { return (*_info->isr & (FLAG_TC << _info->if_offset)) != 0; }
inline bool GetHT_Flag() const { return (*_info->isr & (FLAG_HT << _info->if_offset)) != 0; }
inline void ClearHT_Flag() { *_info->ifcr = FLAG_HT << _info->if_offset; }
inline void ClearTC_Flag() { *_info->ifcr = FLAG_TC << _info->if_offset; }
```

- `GetHT_Flag()` читает флаг половины передачи независимо от того, включено ли прерывание HT.
- `ClearHT_Flag()`/`ClearTC_Flag()` сбрасывают только свой флаг; `ClearFlags()` — все флаги этого ресурса. Флаги других потоков не затрагиваются.
- Каждый сброс — одна запись в регистр сброса (без «прочитать-изменить-записать»).
- Все методы требуют успешного `SetUp()`. Включение источников прерываний и обработка ошибок — задача потребителя.
- ADC использует их вместе с `GetIRQFlags()` в своём обработчике DMA: HT включается через `ADC_N::IRQ_en`, кольцевые HT/TC инвалидируют соответствующую половину буфера (весь буфер, если HT выключен). См. [ADC.md](ADC.md).

**Смещения:**

| Семейство | Бит в группе | Смещения |
| --- | --- | --- |
| F4/F7 | 6 | `0, 6, 16, 22` в каждом регистре — биты 15:12 зарезервированы, поэтому смещения явно прописаны в `dma_streams[]` |
| G0 | 4 | `(номер_канала - 1) * 4` |

**Общие векторы на G0:**

- Channel2/3 делят `DMA1_Channel2_3_IRQn`.
- Другой вектор обслуживает Channel4/5, или Channel4…7, или Channel4…7 вместе с DMA2 Channel1…5 (зависит от МК), а также переполнение DMAMUX.
- [IRQ_Registry](IRQ_Registry.md) допускает несколько обработчиков на вектор; вместимость по умолчанию — 3, при большем числе потребителей одной линии увеличьте её в заголовке настроек.
- Это безопасно: `Dispatch()` вызывает все зарегистрированные обработчики, а каждый `DMA_Sx` проверяет только **свой** `if_offset`.

---

## 9. Интеграция с периферийными драйверами

### Неблокирующая остановка (используется USART)

| Метод | Поведение | Где вызывать |
| --- | --- | --- |
| `RequestStop()` | один раз сбрасывает EN и сразу возвращается | прерывание или main |
| `IsEnabled()` | однократно читает EN | где угодно |
| `Stream_EN(DISABLE)` | сбрасывает EN и **ждёт** EN = 0 | только основной цикл, никогда в прерывании |

`RequestStop()` и `IsEnabled()` требуют успешного `SetUp()` и флаги состояния не сбрасывают.

- **F4/F7:** программная остановка порождает TC; USART завершает остановленный RX-пакет в обработчике DMA RX после этого события.
- **G0:** остановка посреди переноса не гарантирует точный CNDTR. Поэтому USART использует простую остановку по IDLE: отключить DMAR, выключить DMA без ожидания, сообщить о пакете в прерывании USART. Последний запрос должен быть уже обслужен; ошибочные пакеты отбрасываются без использования CNDTR. Это не API восстановления после произвольной остановки.

### Как драйверы периферии используют DMA_Sx

[UART](UART.md), [SPI](SPI.md), [ADC](ADC.md) и [I2C](I2C.md) настраивают DMA из конструктора в `SetUp()`; [TIM](TIM.md#8-independent-pwm-dma) настраивает DMA каналов в `SetUpPWM()`. Общие шаги:

1. Конструктор получает именованный маршрут F4/F7 (или канал G0) и проверяет владельца/направление при компиляции. Дублирующих таблиц DMA в USART/SPI/ADC/I2C больше нет; TIM сохраняет свою карту маршрутов каналов.
2. Драйвер заполняет `DMA_Sx::StreamSettings` (направление, адрес регистра данных, ширина, `minc = true`) и вызывает `dma.SetUp(cfg)`. Драйвер никогда не трогает `_CR()`/`_NDTR()` напрямую — только публичный API `DMA_Sx`.
3. Драйвер регистрирует свои обработчики: USART/SPI/I2C — отдельные обработчики DMA TX и DMA RX (отличные от обработчика периферии); TIM — по одному на DMA каждого канала, прерывание таймера флаги DMA не опрашивает.
4. Если TX и RX DMA делят вектор NVIC, это всё равно два места в реестре; каждый обработчик проверяет только свои флаги.

`DMA_Sx` не знает, «чей это TC» — этот смысл добавляет драйвер периферии поверх `GetTC_Flag()`/`GetIRQFlags()`/`ClearFlags()`.

---

## 10. Подробности интеграции

### Интеграция UART

**`CanUseRequest(DMAReq req) const`** — проверяет маршрут относительно потока и preset из конструктора, не трогая регистры и владение:

- F4/F7: нужен тот же поток; существующий preset должен совпадать с `req.ch`.
- G0: подходит любой канал; его существование проверяет `SetUp()`.
- Если preset не задан, потребитель передаёт подходящий `req.ch` в `SetUp()`.

**`GetErrorFlags() const`** — нормализованные биты ошибок (0 до настройки): F4/F7 — TEIF и DMEIF (direct mode), G0 — TEIF. Ошибка FIFO (FEIF) исключена; `ClearFlags()` по-прежнему сбрасывает всё. Читайте **до** `ClearFlags()`. UART превращает это в свои биты ошибок `DmaTx`/`DmaRx`; он включает TC/TE и на F4/F7 — DME (режим FIFO не используется).

**`GetIRQFlags()`** — за **одно** чтение ISR возвращает HT, TC и поддерживаемые ошибки; разбор — публичными `DMA_Sx::FLAG_HT`, `FLAG_TC`, `FLAG_ERRORS` (позиции приведены к первой группе). FEIF исключён. Флаги не сбрасываются, DMA не останавливается; до `SetUp()` возвращается 0. USART/SPI/ADC/TIM/I2C используют этот единый снимок; ADC берёт из него и HT без повторного чтения ISR. TC от остановки, запрошенной после снимка, обрабатывается следующим прерыванием DMA.

### Явные имена маршрутов — 15.10.2026

Все маршруты F4/F7 содержат контроллер и поток: `USART::_3::DMA::TX_DMA1_Stream3`, `SPI::_1::DMA::RX_DMA2_Stream0`, `ADC_N::_1::DMA::RX_DMA2_Stream4`.

- Они заменили старые имена `TX`/`RX` и `TX_alt`/`RX_alt` — обновите объявления в приложении.
- Адреса потоков и значения CHSEL не изменились. `DMAReq` хранит целочисленный `DMAx_Streamy_BASE`; указатель формирует `DMA_Sx`.
- На G0 имена `TX`/`RX` сохранены: канал явно задаётся в конструкторе.
- Маршрут USART1 DMA2_Stream5 / CH4 называется `RX_DMA2_Stream5` (он обслуживает RX). Маршруты Uart7/Uart8 и G0 Usart3…6 есть там, где есть эта периферия.

### Подготовка передачи SPI

`PrepareTransfer(uint32_t addr, uint32_t count, FunctionalState minc, uint32_t irq_mask)` —
inline-операция для **захваченного, остановленного** DMA при отключённых запросах
периферии. Вызывающий код гарантирует корректный выровненный адрес памяти,
1..65535 элементов, корректные MINC и маску источников IRQ (OR значений
`DMA_Sx::IRQ`). Проверок аргументов, владения и EN нет. Метод записывает адрес
памяти и счётчик, обновляет MINC и переданные биты IRQ одним изменением
управляющего регистра и очищает флаги своего ресурса. Остальные биты управления
сохраняются. Метод не обращается к NVIC, не запускает DMA, не ждёт и не
обслуживает кеш. SPI использует его после проверки статуса/буферов/наличия DMA;
при этом `irq_mask=0`, поскольку SPI настраивает источники IRQ один раз в
`SetUpDMA()`. Отдельные методы записи с проверками остаются доступны.

### Остановка SPI

SPI получает DMA в конструкторе; обработчики SPI, DMA TX и DMA RX регистрируются отдельно. При снятии NSS драйвер только отключает источники и запрашивает остановку — без ожиданий в прерываниях. F4/F7 фиксируют TC остановки в прерывании DMA. Для slave RX на G0 последний запрос должен быть обслужен до остановки по NSS; `Process()` читает счётчик. При ошибке пакет отбрасывается.

### Независимые каналы TIM

- TIM принимает до четырёх привязок `{вывод, dma}`. `SetUpPWM()` захватывает DMA каждого канала и регистрирует отдельный обработчик, даже на общем векторе.
- DMA каждого PWM-канала использует запрос TIMx_CHx с CCDS = 1 и свой бит CCxDE — без размножения TIMx_UP.
- 16-битные таймеры — `uint16_t`/Half_Word; 32-битные TIM2/TIM5 — `uint32_t`/Word.
- На F7 System очищает TX-буфер в кеше.
- Каждый обработчик DMA читает только свои флаги, останавливает поток без ожидания, публикует статус OK/Error своего канала и вызывает его callback; остальные каналы продолжают работу. TC означает «буфер отправлен», а не «последний период PWM закончился на выводе».
- Изменения DIER из `main` вызывающий код окружает маскированием затронутых прерываний TIM/DMA; обработчики одного объекта не должны вытеснять друг друга. `DeInit()` освобождает DMA и регистрации из `main`.
- `SetUpPWM()` включает NVIC при инициализации; `SendDMA()` снова разрешает TE/DME через `Enable_IRQ(..., false)`, сохраняя маску NVIC вызывающего кода. См. [TIM.md](TIM.md).

---

## 11. Примеры использования

### G0 — отдельный DMA_Sx с явным каналом и запросом

```cpp
constinit DMA_Sx dma_tx(DMA1_Channel4_BASE, USART::_1::DMA::TX);
constinit DMA_Sx dma_rx(DMA1_Channel5_BASE, USART::_1::DMA::RX);
// Ch2/Ch3 и Ch4/Ch5 делят векторы — выбирайте пары, не пересекающиеся с другой периферией.
```

### F4/F7 — через определения _N::DMA (стрим уже зашит в DMAReq)

```cpp
constinit DMA_Sx dma_rx(USART::_1::DMA::RX_DMA2_Stream2);       // CHSEL = 4
// Альтернативный маршрут того же запроса (не одновременно):
// constinit DMA_Sx dma_rx(USART::_1::DMA::RX_DMA2_Stream5);    // CHSEL = 4
```

### G0 — автономный перенос память→память, без периферии

Только G0 — на F4/F7 `Mem_To_Mem` возвращает `InitError` (перенос память→память через FIFO не реализован).

```cpp
static constinit DMA_Sx copier(DMA1_Channel3_BASE, /*запрос, игнорируется для mem2mem*/ 0u);

DMA_Sx::StreamSettings cfg;
cfg.direction = DMA_Sx::DIR::Mem_To_Mem;
cfg.data_size = DMA_Sx::SIZE::Word;
cfg.minc      = true;
cfg.pinc      = true;

if (copier.SetUp(cfg) == SysInitStatus::InitOK
    && copier.SetPerAddr(reinterpret_cast<uint32_t>(src)) == SysStatus::OK
    && copier.SetMemAddr(reinterpret_cast<uint32_t>(dst)) == SysStatus::OK
    && copier.SetCount(len) == SysStatus::OK) {
    copier.Stream_EN(ENABLE);
    while (!copier.GetTC_Flag()) {}
}
```

### Через периферийный драйвер (типичный случай — см. UART.md/SPI.md)

```cpp
constinit USART a(USART::_3::instance, 115200, USART::_3::TX::PD8, USART::_3::RX::PD9,
                  USART::_3::DMA::TX_DMA1_Stream3);

if (a.SetUp() == SysInitStatus::InitOK)   // настраивает DMA и регистрирует его обработчик
    a.Send_DMA(buf, len);
```

---

## 12. Угловые случаи

- **Счётчик.** `SetUp()` допускает нулевые адреса/счётчик без запуска. `SetCount()` принимает 1…65535 **элементов** (не обязательно байт).
- **Методы адресов** требуют ненулевых адресов, выровненных по ширине порта. Возвращают `NotInit`, если объект ничем не владеет, `Busy` при EN = 1, `Error` при неверных аргументах; при отказе регистры не меняются.
- **Приоритет запроса.** Запрос из конструктора важнее `settings.channel`. Значение должно помещаться в CHSEL (0…7) или в поле DMAREQ_ID данного МК. Зарезервированные номера и маршруты проверяет драйвер периферии.
- **Остановка.** `Stream_EN(DISABLE)` ждёт EN = 0 без SysTick и таймаута — только основной цикл. Код прерываний использует `RequestStop()` и ждёт аппаратного события завершения.
- **Перед включением** вызывающий код готовит корректные аргументы передачи. MINC/PINC/CIRC меняются только у остановленного канала.
- **Общие векторы G0** зависят от модели (раздел 8). Задайте `IRQ_MAX_SHARED` по реальному числу независимых потребителей.

### Освобождение и передача ресурса

`Release()`:

- возвращает `OK` после остановки и сброса своего ресурса, очистки флагов и снятия занятости; `NotInit`, если объект ничем не владеет;
- на F4/F7 также сбрасывает M1AR/FIFO; на G0 — конфигурацию своего канала DMAMUX и флаг переполнения;
- сохраняет выбор потока/запроса из конструктора;
- оставляет включёнными общие линии NVIC и тактирование контроллера.

После этого другой объект может занять ресурс с другим запросом. **До** освобождения владелец-периферия отключает свои DMA-запросы и удаляет свой обработчик.

### Обслуживание кеша

| Момент | Вызов (размер в байтах) |
| --- | --- |
| перед TX | `System::CacheClean` |
| перед RX | `System::CacheCleanInvalidate` |
| после подтверждённого завершения/остановки RX | `System::CacheInvalidate` |

- Драйверы периферии делают это сами и определяют фактически принятый диапазон до передачи данных приложению. При прямом использовании `DMA_Sx` вызовы делает пользователь; `DMA_Sx` не хранит состояние кеша/буфера.
- На F7 диапазоны целиком внутри настроенных некешируемых областей пропускаются. Кешируемый RX-буфер должен владеть всеми 32-байтными линиями, которые задевает, включая выравнивающие байты. Процессор не обращается к части буфера, принадлежащей DMA, во время передачи.
- На F4/G0 функции пустые и определены inline. Подробнее — [System.md](System.md).

---

## См. также

- [IRQ_Registry.md](IRQ_Registry.md) — обработчиками прерываний DMA регистрируются драйверы периферии (а не `DMA_Sx`), в том числе на общих векторах вроде `DMA1_Channel2_3_IRQn`.
- [UART.md](UART.md#6-dma-кеш-и-ошибки), [SPI.md](SPI.md#8-dma-и-кеш), [TIM.md](TIM.md#8-независимые-pwm-dma) — потребители DMA: USART и SPI привязывают DMA в конструкторе и настраивают в `SetUp()`; TIM настраивает DMA каналов в `SetUpPWM()`.
- [System.md](System.md) — `SysStatus`/`SysInitStatus`, функции кеша.
- [Initialization.md](Initialization.md)
- [UART.md](UART.md)
- [SPI.md](SPI.md)
- [ADC.md](ADC.md)
- [I2C.md](I2C.md)
- [TIM.md](TIM.md)
