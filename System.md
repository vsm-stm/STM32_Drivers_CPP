# System — Detailed Description

*[English](#english) · [Русский](#русский)*

---

## English

## Table of Contents

1. [Why this exists — the connective layer between all drivers](#1-why-this-exists--the-connective-layer-between-all-drivers)
2. [System::Init() — what gets configured at startup](#2-systeminit--what-gets-configured-at-startup)
3. [Clock variables — shared memory between RCC and all drivers](#3-clock-variables--shared-memory-between-rcc-and-all-drivers)
4. [SysTick and the millisecond counter](#4-systick-and-the-millisecond-counter)
5. [Microsecond delay via DWT CYCCNT](#5-microsecond-delay-via-dwt-cyccnt)
6. [MPU on STM32F7](#6-mpu-on-stm32f7)
7. [DebugTrap — a single failure point for all drivers](#7-debugtrap--a-single-failure-point-for-all-drivers)
8. [Bit-banding and SysStatus/SysInitStatus](#8-bit-banding-and-sysstatussysinitstatus)
9. [Usage Examples](#9-usage-examples)
10. [Edge Cases](#10-edge-cases)
11. [See Also](#see-also)

---

## 1. Why this exists — the connective layer between all drivers

`system.hpp`/`system.cpp` is not a peripheral driver but **shared infrastructure** that every other driver depends on:

```text
                              ┌────────────────────┐
                              │   system.hpp/cpp   │
                              │  System, SysStatus,│
                              │  SysInitStatus,    │
                              │  kHz/MHz, BIT_BB   │
                              └──────────┬─────────┘
                                         │  #include "system.hpp"
        ┌──────────────┬──────────────┬──┴───────────┬──────────────┬──────────────┐
        ▼              ▼              ▼              ▼              ▼              ▼
   [RCC.md]        [GPIO.md]      [DMA.md]        [UART.md]       [TIM.md]      [RTC.md] / [SPI.md] / [Flash.md]
   ClockSystem      PIN            DMA_Sx           USART           TIM          ...
   writes          System::       System::         System::        System::
   System::        BB_RD/WR       GetTick()         GetTick()       GetTick()
   SystemCoreClock                (timeouts)        (timeouts)      (Delay, if needed)
   and APBx/TIMx...
```

What `System` provides to all drivers:

| What | Purpose | Section |
| --- | --- | --- |
| `SysStatus` / `SysInitStatus` | one shared vocabulary of results (OK/Error/Busy/Timeout…) instead of per-driver codes | §8 |
| Clock variables (`SystemCoreClock`, `APB1BusClock`, …) | written by [`ClockSystem::Init<config>()`](RCC.md), read by drivers to compute dividers/BRR | §3 |
| `GetTick()`, `Delay_ms()`, `Delay_us()`, `WaitBits()` | the millisecond counter behind every timeout (USART/SPI blocking calls, RCC oscillator waits…) | §4, §5 |
| Cache helpers, MPU setup (F7) | DMA/cache coherency | §6 |
| `DebugTrap()`, `STM32_CONFIG_ERROR` | configuration-error reporting | §7 |

> **SysTick accuracy:** the SysTick period is an integer number of HCLK cycles. If HCLK is not divisible by 1000, `GetTick()`/`Delay_ms()`/timeouts inherit that rate error — e.g. G0 on LSE 32768 Hz with AHB /1 gives 1024 ticks/s. See [RCC.md](RCC.md).

---

## 2. System::Init() — what gets configured at startup

```cpp
static SysInitStatus Init();
```

Call **once at the very start of `main()`**, before any other driver. It configures low-level things that belong to no particular peripheral:

1. **FPU** (if `__FPU_PRESENT`/`__FPU_USED`): grants CP10/CP11 access and executes DSB/ISB — **unless** `STM32_STARTUP_ENABLES_FPU` is defined. The current base defines it because its startup already enables the FPU before constructors. A different startup must drop the definition unless it does the same.
2. **Clock state and SysTick:** sets the frequency variables and starts SysTick at 16 MHz (HSI). On G0 the APB2 and APB2-timer frequencies are 0 (no such bus). Without `STM32_USE_BOOTLOADER`, call it once after hardware reset, before changing clocks, power or Flash latency.
3. **After a bootloader (F4/F7/G0)** — compiled only with `STM32_USE_BOOTLOADER` (set automatically by the base when `bootloader` is configured in `project.json`; it reaches both images and the shared driver library). `InitAfterBootloader()`:
   - enables HSI and waits for readiness and the actual switch (**no timeout**);
   - restores bus dividers to /1 (including G0 HSIDIV);
   - starts SysTick through `InitClockState()`;
   - restores G0 VOS Range 1, then Flash latency 0 WS (these waits keep 100 ms timeouts; failure → `InitError`).
4. **Flash accelerator / caches:**

| Family | Enabled |
| --- | --- |
| F4 | `PRFTEN` (prefetch), `ICEN`/`DCEN` (Flash instruction/data cache) |
| F7 | `PRFTEN`, `ARTEN` (ART accelerator), Cortex-M7 I-cache and D-cache (`SCB_EnableICache()`/`SCB_EnableDCache()`), `PWR` clock (needed by [ClockSystem](RCC.md) for voltage scaling) |
| G0 | `PRFTEN`, `ICEN` |
| L0 | nothing (no ART/cache) |

**Calling rules:**

- Call with interrupts enabled, outside interrupt handlers, so the newly started SysTick can serve the timed waits. No IPSR/PRIMASK checks are made.
- With `STM32_USE_BOOTLOADER` the initial HSI waits deliberately have no timeout — a dead HSI will hang there.
- On G0, 0 WS is temporarily raised to 1 WS before switching to 16 MHz, so a leftover VOS Range 2 stays safe until Range 1 is ready.

**What it does not touch** (bootloader path): PLL/HSE enable and configuration, HSI trim, MCO, RCC interrupts, peripheral enable/reset registers, RTC, LSE/LSI, peripheral clock muxes, tick count. After a later clock change, `ClockSystem` publishes the new frequencies and calls `InitTicks(ticks)` with a precomputed period.

**L0 limitation:** the old path is kept — it only sets the software frequencies to 16 MHz and starts SysTick, assuming hardware already runs from HSI. RCC, power and Flash latency are not restored.

```cpp
int main() {
    System::Init();                                    // FPU, Flash/cache, SysTick @ HSI
    constexpr auto clocks = ClockSystem::CalcPLL(64 * MHz, ClockSystem::PLL_ClockSource::HSI);
    if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
        System::DebugTrap("Clock init");
    System::Enable_CYCCNT();                            // F4/F7, for Delay_us() — §5
    ...
}
```

> Startup initializes RAM and runs global/static constructors **before** `main()` (the current base also enables the FPU there). Such constructors must not wait on SysTick — it is not running yet and interrupts are still masked. `constinit` driver objects have no constructor code at all.

---

## 3. Clock variables — shared memory between RCC and all drivers

```cpp
static uint32_t SystemCoreClock; // AHB/core/GPIO/DMA/USB
static uint32_t APB1BusClock;    // WWDG, SPI2/3, USART2/3, UART4/5, I2C1-3, CAN1/2, DAC
static uint32_t APB2BusClock;    // USART1/6, ADC1-3, SPI1/4, SYSCFG, SAI1/2
static uint32_t TIMxAPB1Clock;   // TIM2-7, TIM12-14 (with the "×2" rule, see RCC.md)
static uint32_t TIMxAPB2Clock;   // TIM1, TIM8-11
```

- `System::Init()` sets the initial HSI values (16 MHz).
- **`ClockSystem::Init<config>()` publishes the configured frequencies** (see [RCC.md](RCC.md)).
- Every other driver only **reads** them. Each peripheral's `_N::instance` points to the variable of its bus, e.g. USART computes BRR from `APB1BusClock` or `APB2BusClock` depending on the instance ([UART.md](UART.md)); TIM uses `TIMxAPB1Clock`/`TIMxAPB2Clock` ([TIM.md](TIM.md)); SPI and I2C likewise.

**Hence the call order matters:** `ClockSystem::Init<config>()` must run **before** any peripheral `SetUp()`. Otherwise, for example, `USART::SetUp()` computes BRR from 16 MHz instead of the real post-PLL frequency.

---

## 4. SysTick and the millisecond counter

```cpp
static SysInitStatus InitTicks();               // SysTick_Config(SystemCoreClock / TICK_BASE)
static SysInitStatus InitTicks(uint32_t ticks); // precomputed HCLK cycles (used by ClockSystem)
static void          TickIncrease();            // called from SysTick_Handler
static uint32_t      GetTick();
static bool          WaitBits(volatile uint32_t& reg, uint32_t mask, uint32_t value, uint32_t timeout);
static void          Delay_ms(uint32_t delay);
```

**`WaitBits(reg, mask, value, timeout)`** waits until `(reg & mask) == value`:

- `value = 0` → wait for bits cleared; `value = mask` → all bits set; or an encoded field value.
- Returns `true` on match, `false` on timeout (measured with `GetTick()`). A zero timeout checks once.
- RCC and other drivers use it for hardware waits. SysTick must be able to interrupt the caller.

**The tick:**

- `TICK_BASE = 1 kHz` is the target rate; exact 1 ms only when HCLK is divisible by it (see [RCC.md](RCC.md)).
- `SysTick_Config()` sets SysTick to the lowest priority on every call, including after clock changes.
- The handler `extern "C" void SysTick_Handler(void) { System::TickIncrease(); }` increments one `static volatile uint32_t Tick`. On 32-bit ARM an aligned 32-bit read is atomic, so `GetTick()` needs no interrupt disabling.

**`Delay_ms()` — wrap-safe:**

```cpp
const uint32_t wait = (delay < 0x7FFFFFFFUL) ? delay + 1UL : 0x7FFFFFFFUL;
while ((GetTick() - tick_start) < wait) {}
```

- One extra tick compensates for the partial first tick, so `Delay_ms(N)` waits **at least** N ms; `Delay_ms(0)` waits for the next tick.
- Capped at `0x7FFFFFFF` ticks (~24.9 days at 1 kHz).
- **Why subtraction:** unsigned `GetTick() - tick_start` stays correct even if `Tick` wraps (after ~49.7 days) in the middle of a wait; `GetTick() < target` would fail at that moment. All timeouts in the project use this `GetTick() - start > timeout` pattern instead of an absolute deadline (the same idea as `Delay_us()` below and TIM's hardware counters).

**When `Delay_ms()`/timeouts cannot work:**

- before SysTick is started, while SysTick is masked, or inside an interrupt of equal or higher preemption priority — the tick never advances;
- a callback called from an ISR still runs in that ISR, even after a local mask is restored;
- `GetTick()` itself may be read anywhere.

Because SysTick has the **lowest** priority, any IRQ at the default priority 0 runs ahead of it — such handlers must not call `Delay_ms()` or wait on a `GetTick()` timeout. Equal preemption priorities never interrupt each other, whatever the sub-priority.

**Background:** `IPSR` holds the active exception number (0 in thread code); a set `PRIMASK` masks configurable interrupts; on F4/F7 `BASEPRI` can also mask SysTick by priority. The driver does not inspect these registers.

Long periods without SysTick service **lose ticks** — one pending exception does not count all missed periods. The counter is not a wall clock across long critical sections.

---

## 5. Microsecond delay via DWT CYCCNT

```cpp
#if not defined(STM32L0) && not defined(STM32G0)
static void     Enable_CYCCNT();
static void     Delay_us(uint32_t delay);
static uint32_t SWOTrace(const uint8_t* ptr, uint32_t len);
#endif
```

Available only where a DWT block exists — Cortex-M4/M7 (F4/F7), **not** Cortex-M0/M0+ (G0/L0).

**`Enable_CYCCNT()`** enables trace (`DEMCR.TRCENA`) and the cycle counter (`DWT->CTRL.CYCCNTENA`). Required before the first `Delay_us()`. On F7 it unlocks DWT first. It does not reset an already-running counter and does not configure ITM output.

```cpp
void System::Delay_us(uint32_t delay) {
    const uint64_t cycles = (uint64_t(delay) * SystemCoreClock + 999999ULL) / 1000000ULL;
    const uint32_t wait = cycles > 0x7FFFFFFFULL ? 0x7FFFFFFFUL : uint32_t(cycles);
    const uint32_t start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < wait) {}
}
```

- The 64-bit product avoids overflow; rounding **up** keeps fractional cycles when the core clock is not a whole number of MHz.
- Large arguments are capped at **`0x7FFFFFFF` cycles**, i.e. `0x7FFFFFFF / SystemCoreClock` seconds:

| Core clock | Maximum delay |
| --- | --- |
| 16 MHz | ≈ 134.2 s |
| 180 MHz | ≈ 11.9 s |
| 216 MHz | ≈ 9.94 s |

- Unsigned subtraction handles counter wrap. If polling is suspended for half the counter period, the end can still be missed.
- CYCCNT must be running, and `SystemCoreClock` must match the hardware and not change during the delay.

**`SWOTrace(ptr, len)`** writes to ITM channel 0 **without waiting** and returns how many bytes the port accepted (possibly fewer than `len`). Trace disabled, channel disabled, port not ready or `nullptr` → returns immediately. The debugger must configure ITM/SWO; `Enable_CYCCNT()` does not.

---

## 6. MPU on STM32F7

### External-memory guard

`System::MPU_Init()` installs a guard in **MPU region 0** that blocks ordinary and **speculative** CPU accesses to unused external-memory addresses (Cortex-M7 can speculatively read from them). It does not initialize an external-memory controller or DMA buffers.

The region spans 4 GiB in eight 512 MiB subregions; mask `0x87` disables subregions 0, 1, 2 and 7, so only 3…6 are guarded:

| Address range | Effect for privileged CPU code |
| --- | --- |
| `0x00000000–0x5FFFFFFF` | Standard memory map unless another MPU region overrides it |
| `0x60000000–0xDFFFFFFF` | No read, write or instruction execution |
| `0xE0000000–0xFFFFFFFF` | Standard memory map unless another MPU region overrides it |

- The guarded range covers FMC memory, FMC control mappings and memory-mapped QSPI. Accesses there fault — MemManage if enabled, otherwise HardFault — unless a higher-numbered region allows them.
- Region 0 is replaced; regions 1+ keep their settings and win where they overlap.
- `PRIVDEFENA` keeps the default map for privileged accesses outside the guard. Unprivileged code needs explicit region permissions.
- `HFNMIENA` is not set, so the MPU is not enforced inside NMI/HardFault handlers.

**Using external memory:** call the guard and configure your external-memory regions **before** caches are enabled in `System::Init()`. Example — 64 KiB of data RAM at `0xC0000000` (adapt address, size and attributes to your board; configure FMC separately before accessing it):

```cpp
System::MPU_Init();

__DMB();
MPU->CTRL = 0;
MPU->RNR  = 1;                 // higher priority than the region 0 guard
MPU->RBAR = 0xC0000000UL;      // base aligned to the 64 KiB region size
MPU->RASR = MPU_RASR_ENABLE_Msk
          | (15UL << MPU_RASR_SIZE_Pos) // 2^(15 + 1) = 64 KiB
          | (3UL  << MPU_RASR_AP_Pos)   // read/write
          | (1UL  << MPU_RASR_TEX_Pos)
          | MPU_RASR_C_Msk | MPU_RASR_B_Msk // Normal, write-back/write-allocate
          | MPU_RASR_XN_Msk;           // data only, no execution
MPU->CTRL = MPU_CTRL_ENABLE_Msk | MPU_CTRL_PRIVDEFENA_Msk;
__DSB();
__ISB();

System::Init();                // enables the F7 caches after the MPU setup
```

The guard does not change internal SRAM cacheability. `MPU_Init()` is optional and does not create DMA areas.

### Optional non-cacheable DMA areas (F7 + base)

Non-cacheable buffers let DMA and CPU share memory **without any cache maintenance**. A step-by-step guide with driver examples is in the base: [DMA_REGIONS.md](https://github.com/vsm-stm/stm32_cmake_base/blob/main/DMA_REGIONS.md). Declare the areas in the base's `project.json`:

```json
"dma_regions": [
  {"name": "adc", "size": "0x4000", "memory": "SRAM2", "mpu_region": 7}
]
```

**Allocation rules:**

- Addresses are automatic: CMake allocates areas from the **end** of the chosen internal SRAM1/SRAM2, in list order, base aligned to the area size.
- `size` — a power of two, ≥ 32 bytes. Alignment gaps are reserved too; `.data`/`.bss`, heap, stack and `.sram2_bss` cannot overlap the areas.
- SRAM1 must keep room for ordinary data/heap/stack; SRAM2 may be used entirely. Not enough memory → configuration or link error.
- `name` — unique identifier; `mpu_region` — unique, 1…7 (region 0 is reserved).
- Empty list, `null` or missing key → no areas. A non-empty list requires F7 with drivers; external RAM and explicit `address` are not supported.

**Placing buffers** (global or static, zero-initialized only):

```cpp
__attribute__((section(".dma.adc"))) static uint16_t adc_buffer[1024];
```

- The section is `NOLOAD`; startup's zero table clears it before constructors.
- Non-zero initializers and objects with dynamic initialization are **not** supported (NOLOAD copies nothing from Flash).
- A buffer in an unconfigured `.dma.*` section → link error. Locals and heap allocations never go there.

**What `System::Init()` does with them:** reads the linker table and programs only those MPU regions as Normal, non-cacheable, shareable, read/write, execute-never (`TEX=1, C=0, B=0, S=1, AP=3, XN=1`). If the D-cache is already on, CMSIS cleans/invalidates it while disabling (dirty data are kept in RAM), the MPU is changed, then the D-cache is re-enabled. This whole-cache operation happens once at init, not per transfer.

- Other MPU regions stay; choose numbers that do not clash with your map — a higher-numbered overlapping region can override the DMA attributes.
- Init enables the privileged default map and keeps other MPU control flags. It does **not** install the region-0 external-memory guard automatically.
- Call `Init()` at the start of `main`, before DMA/peripheral setup; constructors must not start DMA. A bootloader must stop its DMA before handing over.

**Requirements:** the updated `STM32-base_files/linker/linker-f7.ld.in` template and a driver version with DMA-region support (configuration rejects older drivers, so buffers never silently stay cacheable). For a vendored template, refresh the file or point `STM32_BASE_FILES_DIR` to the updated checkout. Each firmware image uses areas from its own `project.json` (the bootloader does not inherit the application's). Custom linker scripts without the table still work via weak symbols, just without automatic DMA MPU setup.

### Cache rules for DMA with cacheable buffers

Non-cacheable buffers need no maintenance, but CPU/DMA ownership and ordering still matter. For ordinary cacheable buffers:

| Transfer | Rule |
| --- | --- |
| TX | clean the buffer's lines **before** DMA reads them |
| RX | clean+invalidate **before** DMA starts; invalidate **after** completion, before the CPU reads. Never clean stale RX lines afterwards |

- Operate on the affected lines only. F7 lines are 32 bytes; buffers (and independently processed parts) must own whole, aligned lines — rounding must not touch unrelated writable data.
- The CPU must not access the DMA-owned part during a transfer. Circular buffers need ownership and maintenance per completed part.

The shared helpers:

```cpp
System::CacheClean(tx_buffer, tx_bytes);
System::CacheCleanInvalidate(rx_buffer, rx_bytes);   // before starting RX
System::CacheInvalidate(rx_buffer, received_bytes);  // after DMA has stopped
```

- Peripheral drivers choose address, byte count and timing themselves; direct DMA users call these explicitly. DMA keeps no cache state.
- F7: the helpers call CMSIS directly for non-null/nonempty ranges. They do not inspect DTCM addresses, MPU attributes, linker records or D-cache enable state.
- The last constructor argument is `System::CachePolicy`, fixed for the object. `Cacheable` is the default and accepts any valid DMA buffer, including non-cacheable memory; the F7 driver performs its normal cache operations without detecting the memory region. `NonCacheable` skips every cache call for both TX and RX (all DMA channels for TIM). The application must then place **every** buffer used by that object entirely in DTCM or non-cacheable MPU memory; a cacheable buffer in this mode is a caller error. The policy does not place buffers or configure MPU. F4/G0 perform no cache operations with either value. DMA completion and buffer ownership requirements remain unchanged.
- Direct DMA users omit these calls for non-cacheable buffers. DTCM bypasses D-cache without a dedicated MPU region.
- F4/G0/L0: inline no-ops. Zero size or `nullptr` does nothing.
- Use valid ranges that fit CMSIS's signed byte count (including rounding). Call after `System::Init()`.

References: [ST AN4838](https://www.st.com/resource/en/application_note/DM00272912-.pdf) (MPU attributes), [ST AN4839](https://www.st.com/resource/en/application_note/an4839-level-1-cache-on-stm32f7-series-and-stm32h7-series-stmicroelectronics.pdf) (cache/DMA coherency).

---

## 7. DebugTrap — a single failure point for all drivers

```cpp
[[noreturn]] static void DebugTrap(const char* msg);
```

The project-wide way to stop on a **software/configuration error** (as opposed to a run-time peripheral failure, which is returned as a status).

| Build | Behaviour |
| --- | --- |
| **Debug** (`DEBUG` defined) | stores `msg` in `volatile _debug_trap_msg` (visible in Watch/Memory even if the stack is lost to optimization); on F4/F7 with trace and ITM channel 0 enabled, prints the message over **blocking SWO** (`ITM_SendChar()`); then `__BKPT(0)` and an endless loop |
| **Release** | inline endless loop in `system.hpp`; the argument is ignored, so unused message strings are removed by the optimizer |

**Configuration errors in constructors** use `STM32_CONFIG_ERROR("reason")` (see also [GPIO.md §6](GPIO.md#6-constexpr-таблицы-пинов-и-защита-от-опечаток)):

```cpp
#define STM32_CONFIG_ERROR(text) \
  (std::is_constant_evaluated() ? ::stm32::CONFIGURATION_ERROR<text>() : ::System::DebugTrap(text))
```

- **At compile time** (`constinit`/`consteval`): calls the deliberately non-`constexpr` `stm32::CONFIGURATION_ERROR<reason>()`. The text is a template argument, so GCC prints the reason **inside the error message**.
- **At run time**: calls `DebugTrap(text)` directly — the debugger shows a readable message, not a template frame.
- `constinit` is required by the driver API. Examples and diagnostics: [Initialization.md](Initialization.md).

**Notes:**

- The debugger must configure ITM/SWO. With trace disabled or `msg == nullptr` the output is skipped; G0/L0 have no ITM.
- An enabled but stalled ITM channel can keep `DebugTrap` waiting before the breakpoint.
- This path is intentionally blocking and uses no SysTick, so it works before `System::Init()` and inside interrupts.
- Ordinary `SWOTrace()` stays non-blocking. `SetDebugOutput()` was removed — there is no output callback or UART for `DebugTrap()`.

---

## 8. Bit-banding and SysStatus/SysInitStatus

```cpp
enum class SysStatus     : uint8_t { OK, Error, Busy, NotInit, Timeout };
enum class SysInitStatus : uint8_t { NotInit, InitOK, InitError = 0xFF };
```

| Enum | Used for | Values |
| --- | --- | --- |
| `SysStatus` | run-time operations (`Send()`, `Receive()`, …) | `OK`, `Error`, `Busy`, `NotInit`, `Timeout` |
| `SysInitStatus` | one-time setup (`SetUp()`, `Init()`) | `NotInit`, `InitOK`, `InitError` |

Deliberately coarse — there is no separate "invalid argument" vs "device not found" (both are `Error`/`InitError`) — but uniform across the whole project, so callers never juggle per-peripheral enums. Drivers expose details through their own `GetError()`/error masks where needed.

**`BIT_BB`/`BB_RD`/`BB_WR`** — bit-band address arithmetic (`(addr - PERIPH_BASE) * 32 + bit * 4 + PERIPH_BB_BASE`), used by `PIN::SetLevel_BB()`/`GetLevel_BB()`/`TogglePin_BB()`. Defined only for F4 (`#if defined(STM32F4)`), since it is a core capability rather than a GPIO feature. The technique itself and why it is F4-only: [GPIO.md §5](GPIO.md#5-bit-banding-только-f4).

---

## 9. Usage Examples

### Standard startup sequence (as in main.cpp)

```cpp
int main() {
    System::Init();
    constexpr auto clocks = ClockSystem::CalcPLL(64 * MHz, ClockSystem::PLL_ClockSource::HSI);
    if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
        System::DebugTrap("Clock init");
    System::Enable_CYCCNT();   // F4/F7 only; required before the first Delay_us()

    uint32_t tick = System::GetTick();
    for (;;) {
        if (System::GetTick() - tick > 500) {   // wrap-safe
            tick = System::GetTick();
            // once every 500 ms...
        }
    }
}

extern "C" void NMI_Handler(void)       { while(1){} }
extern "C" void HardFault_Handler(void) { while(1){} }
```

### Microsecond delay

```cpp
System::Enable_CYCCNT();     // once
System::Delay_us(50);        // 50 µs, blocking
```

### Waiting for a hardware flag with a timeout

```cpp
// Wait up to 10 ms for HSERDY to be set.
if (!System::WaitBits(RCC->CR, RCC_CR_HSERDY, RCC_CR_HSERDY, 10)) {
    // timeout
}
```

### DebugTrap with SWO output

```cpp
// Enable ITM channel 0 / SWO in the debugger (F4/F7, DEBUG build).
System::DebugTrap("Invalid peripheral pin"); // blocking output, breakpoint, halt
```

---

## 10. Edge Cases

| Situation | Behaviour |
| --- | --- |
| Peripheral `SetUp()` before `ClockSystem::Init<config>()` | Dividers computed from HSI 16 MHz, not the real clock (§3). Order: `System::Init()` → `ClockSystem::Init<config>()` → `Enable_CYCCNT()` → peripheral `SetUp()`. |
| `Enable_CYCCNT()`/`Delay_us()`/`SWOTrace()` on G0/L0 | Compile error — Cortex-M0/M0+ has no DWT/CYCCNT, so the functions do not exist. |
| `Delay_ms(0xFFFFFFFF)` | Saturates at `0x7FFFFFFF` ticks, including the compensating tick. |
| `DebugTrap()` in release | `msg` is not stored; the string may not be in the binary at all. |
| `TickIncrease()` called outside `SysTick_Handler` | Not an error, but `GetTick()` stops matching real time. |
| `Delay_ms()` in a priority-0 IRQ | Never finishes — SysTick has the lowest priority. |

---

## See Also

- [RCC.md](RCC.md) — the only writer of the clock variables (§3).
- [GPIO.md](GPIO.md) — `BIT_BB`/`BB_RD`/`BB_WR` (§8) are used by the `PIN::*_BB()` methods.
- [IRQ_Registry.md](IRQ_Registry.md) — dispatching of peripheral IRQs; `SysTick_Handler`, `NMI_Handler`, `HardFault_Handler` and other Cortex-M exceptions are not part of it.
- [UART.md](UART.md), [TIM.md](TIM.md), [SPI.md](SPI.md), [RTC.md](RTC.md), [DMA.md](DMA.md), [Flash.md](Flash.md) — use `SysStatus`/`SysInitStatus` (§8) and `GetTick()` (§4).
- [Initialization.md](Initialization.md) — `constinit` and `STM32_CONFIG_ERROR` diagnostics.

---

## Русский

## Содержание

1. [Зачем это нужно — связующая часть между всеми драйверами](#1-зачем-это-нужно--связующая-часть-между-всеми-драйверами)
2. [System::Init() — что настраивается при старте](#2-systeminit--что-настраивается-при-старте)
3. [Тактовые переменные — общая память между RCC и всеми драйверами](#3-тактовые-переменные--общая-память-между-rcc-и-всеми-драйверами)
4. [SysTick и миллисекундный счётчик](#4-systick-и-миллисекундный-счётчик)
5. [Микросекундная задержка через DWT CYCCNT](#5-микросекундная-задержка-через-dwt-cyccnt)
6. [MPU на STM32F7](#6-mpu-на-stm32f7)
7. [DebugTrap — единая точка отказа для всех драйверов](#7-debugtrap--единая-точка-отказа-для-всех-драйверов)
8. [Bit-banding и SysStatus/SysInitStatus](#8-bit-banding-и-sysstatussysinitstatus)
9. [Примеры использования](#9-примеры-использования)
10. [Угловые случаи](#10-угловые-случаи)
11. [См. также](#см-также)

---

## 1. Зачем это нужно — связующая часть между всеми драйверами

`system.hpp`/`system.cpp` — не драйвер периферии, а **общая инфраструктура**, от которой зависят все остальные драйверы:

```text
                              ┌────────────────────┐
                              │   system.hpp/cpp   │
                              │  System, SysStatus,│
                              │  SysInitStatus,    │
                              │  kHz/MHz, BIT_BB   │
                              └──────────┬─────────┘
                                         │  #include "system.hpp"
        ┌──────────────┬──────────────┬──┴───────────┬──────────────┬──────────────┐
        ▼              ▼              ▼              ▼              ▼              ▼
   [RCC.md]        [GPIO.md]      [DMA.md]        [UART.md]       [TIM.md]      [RTC.md] / [SPI.md] / [Flash.md]
   ClockSystem      PIN            DMA_Sx           USART           TIM          ...
   пишет           System::       System::         System::        System::
   System::        BB_RD/WR       GetTick()         GetTick()       GetTick()
   SystemCoreClock                (таймауты)        (таймауты)      (Delay, если нужно)
   и APBx/TIMx...
```

Что `System` даёт всем драйверам:

| Что | Зачем | Раздел |
| --- | --- | --- |
| `SysStatus` / `SysInitStatus` | единый словарь результатов (OK/Error/Busy/Timeout…) вместо своих кодов в каждом драйвере | §8 |
| Тактовые переменные (`SystemCoreClock`, `APB1BusClock`, …) | записывает [`ClockSystem::Init<config>()`](RCC.md), драйверы читают для расчёта делителей/BRR | §3 |
| `GetTick()`, `Delay_ms()`, `Delay_us()`, `WaitBits()` | миллисекундный счётчик, на котором построены все таймауты (блокирующие вызовы USART/SPI, ожидания генераторов в RCC…) | §4, §5 |
| Функции кеша, настройка MPU (F7) | согласование DMA и кеша | §6 |
| `DebugTrap()`, `STM32_CONFIG_ERROR` | сообщения об ошибках конфигурации | §7 |

> **Точность SysTick:** период SysTick — целое число тактов HCLK. Если HCLK не кратна 1000, `GetTick()`/`Delay_ms()`/таймауты получают ту же погрешность — например, G0 от LSE 32768 Гц с AHB /1 даёт 1024 тика/с. См. [RCC.md](RCC.md).

---

## 2. System::Init() — что настраивается при старте

```cpp
static SysInitStatus Init();
```

Вызывайте **один раз в самом начале `main()`**, до любого другого драйвера. Настраивает низкоуровневые вещи, не относящиеся к конкретной периферии:

1. **FPU** (если `__FPU_PRESENT`/`__FPU_USED`): открывает доступ к CP10/CP11 и выполняет DSB/ISB — **если не** определён `STM32_STARTUP_ENABLES_FPU`. Текущая база определяет его, поскольку её startup включает FPU ещё до конструкторов. При другом startup определение нужно убрать, если он сам этого не делает.
2. **Частоты и SysTick:** записывает переменные частот и запускает SysTick на 16 МГц (HSI). На G0 частоты APB2 и её таймеров равны 0 (такой шины нет). Без `STM32_USE_BOOTLOADER` вызывать один раз после аппаратного сброса, до изменения тактирования, питания или задержки Flash.
3. **После загрузчика (F4/F7/G0)** — компилируется только с `STM32_USE_BOOTLOADER` (база задаёт его автоматически при настроенном `bootloader` в `project.json`; макрос получают оба образа и общая библиотека драйверов). `InitAfterBootloader()`:
   - включает HSI и ждёт готовности и фактического переключения (**без таймаута**);
   - возвращает делители шин в /1 (включая HSIDIV на G0);
   - запускает SysTick через `InitClockState()`;
   - восстанавливает VOS Range 1 на G0, затем задержку Flash 0 WS (эти ожидания с таймаутом 100 мс; ошибка → `InitError`).
4. **Flash-акселератор / кеши:**

| Семейство | Что включается |
| --- | --- |
| F4 | `PRFTEN` (предвыборка), `ICEN`/`DCEN` (кеши инструкций/данных Flash) |
| F7 | `PRFTEN`, `ARTEN` (ART-акселератор), I-cache и D-cache ядра Cortex-M7 (`SCB_EnableICache()`/`SCB_EnableDCache()`), тактирование `PWR` (нужно [ClockSystem](RCC.md) для масштабирования напряжения) |
| G0 | `PRFTEN`, `ICEN` |
| L0 | ничего (ART/кеша нет) |

**Правила вызова:**

- Вызывайте с разрешёнными прерываниями, вне обработчиков, чтобы запущенный SysTick обслуживал ожидания. Проверок IPSR/PRIMASK нет.
- С `STM32_USE_BOOTLOADER` первые ожидания HSI намеренно без таймаута — неисправный HSI оставит программу там.
- На G0 перед переходом на 16 МГц задержка 0 WS временно повышается до 1 WS, чтобы оставшийся VOS Range 2 был безопасен до готовности Range 1.

**Что не трогается** (путь после загрузчика): включение и настройки PLL/HSE, подстройка HSI, MCO, прерывания RCC, регистры включения/сброса периферии, RTC, LSE/LSI, мультиплексоры тактирования периферии, счётчик тиков. После последующей смены частоты `ClockSystem` публикует новые частоты и вызывает `InitTicks(ticks)` с готовым периодом.

**Ограничение L0:** сохранена старая ветка — она только записывает программные частоты 16 МГц и запускает SysTick, предполагая, что аппаратура уже работает от HSI. RCC, питание и задержка Flash не восстанавливаются.

```cpp
int main() {
    System::Init();                                    // FPU, Flash/кеш, SysTick @ HSI
    constexpr auto clocks = ClockSystem::CalcPLL(64 * MHz, ClockSystem::PLL_ClockSource::HSI);
    if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
        System::DebugTrap("Clock init");
    System::Enable_CYCCNT();                            // F4/F7, для Delay_us() — §5
    ...
}
```

> Startup инициализирует RAM и вызывает глобальные/статические конструкторы **до** `main()` (текущая база там же включает FPU). Такие конструкторы не должны ждать SysTick — он ещё не запущен, прерывания замаскированы. У `constinit`-объектов драйверов кода конструктора нет вовсе.

---

## 3. Тактовые переменные — общая память между RCC и всеми драйверами

```cpp
static uint32_t SystemCoreClock; // AHB/ядро/GPIO/DMA/USB
static uint32_t APB1BusClock;    // WWDG, SPI2/3, USART2/3, UART4/5, I2C1-3, CAN1/2, DAC
static uint32_t APB2BusClock;    // USART1/6, ADC1-3, SPI1/4, SYSCFG, SAI1/2
static uint32_t TIMxAPB1Clock;   // TIM2-7, TIM12-14 (с учётом правила «×2», см. RCC.md)
static uint32_t TIMxAPB2Clock;   // TIM1, TIM8-11
```

- `System::Init()` задаёт начальные значения HSI (16 МГц).
- **`ClockSystem::Init<config>()` публикует выбранные частоты** (см. [RCC.md](RCC.md)).
- Все остальные драйверы их только **читают**. `_N::instance` каждой периферии указывает на переменную своей шины: например, USART считает BRR от `APB1BusClock` или `APB2BusClock` в зависимости от экземпляра ([UART.md](UART.md)); TIM использует `TIMxAPB1Clock`/`TIMxAPB2Clock` ([TIM.md](TIM.md)); SPI и I2C — аналогично.

**Поэтому порядок вызовов важен:** `ClockSystem::Init<config>()` должен выполниться **до** `SetUp()` любой периферии. Иначе, например, `USART::SetUp()` посчитает BRR от 16 МГц, а не от реальной частоты после PLL.

---

## 4. SysTick и миллисекундный счётчик

```cpp
static SysInitStatus InitTicks();               // SysTick_Config(SystemCoreClock / TICK_BASE)
static SysInitStatus InitTicks(uint32_t ticks); // готовое число тактов HCLK (использует ClockSystem)
static void          TickIncrease();            // вызывается из SysTick_Handler
static uint32_t      GetTick();
static bool          WaitBits(volatile uint32_t& reg, uint32_t mask, uint32_t value, uint32_t timeout);
static void          Delay_ms(uint32_t delay);
```

**`WaitBits(reg, mask, value, timeout)`** ждёт `(reg & mask) == value`:

- `value = 0` — ждать сброса битов; `value = mask` — установки всех битов; либо закодированное значение поля.
- Возвращает `true` при совпадении, `false` по таймауту (по `GetTick()`). Нулевой таймаут — однократная проверка.
- RCC и другие драйверы используют её для аппаратных ожиданий. SysTick должен иметь возможность прервать вызывающий код.

**Тик:**

- `TICK_BASE = 1 кГц` — целевая частота; ровно 1 мс только при HCLK, кратной ей (см. [RCC.md](RCC.md)).
- `SysTick_Config()` при каждом вызове назначает SysTick самый низкий приоритет, в том числе после смены частоты.
- Обработчик `extern "C" void SysTick_Handler(void) { System::TickIncrease(); }` увеличивает единственную `static volatile uint32_t Tick`. На 32-битном ARM чтение выровненного 32-битного значения атомарно, поэтому `GetTick()` не требует запрета прерываний.

**`Delay_ms()` — безопасен при переполнении:**

```cpp
const uint32_t wait = (delay < 0x7FFFFFFFUL) ? delay + 1UL : 0x7FFFFFFFUL;
while ((GetTick() - tick_start) < wait) {}
```

- Лишний тик компенсирует неполный первый тик, поэтому `Delay_ms(N)` ждёт **не меньше** N мс; `Delay_ms(0)` ждёт следующего тика.
- Ограничено `0x7FFFFFFF` тиками (~24,9 суток при 1 кГц).
- **Почему вычитание:** беззнаковое `GetTick() - tick_start` остаётся верным, даже если `Tick` переполнится (через ~49,7 суток) посреди ожидания; `GetTick() < target` в этот момент дало бы неверный результат. Все таймауты проекта используют шаблон `GetTick() - start > timeout`, а не абсолютный «дедлайн» (та же идея, что в `Delay_us()` ниже и аппаратных счётчиках TIM — [TIM.md](TIM.md#11-дополнительные-режимы-и-интеграция)).

**Когда `Delay_ms()`/таймауты не работают:**

- до запуска SysTick, при замаскированном SysTick или внутри прерывания с равным или более высоким приоритетом вытеснения — тик не растёт;
- callback, вызванный из ISR, остаётся в контексте этого ISR даже после восстановления локальной маски;
- сам `GetTick()` читать можно где угодно.

Поскольку у SysTick **самый низкий** приоритет, любое IRQ с приоритетом по умолчанию 0 выполняется раньше него — из таких обработчиков нельзя вызывать `Delay_ms()` или ждать таймаут по `GetTick()`. Равные приоритеты вытеснения не прерывают друг друга независимо от subpriority.

**Справка:** `IPSR` содержит номер активного исключения (0 в обычном коде); установленный `PRIMASK` маскирует настраиваемые прерывания; на F4/F7 `BASEPRI` также может маскировать SysTick по приоритету. Драйвер эти регистры не проверяет.

Долгий запрет обслуживания SysTick **теряет тики** — одно ожидающее исключение не учитывает все пропущенные периоды. Счётчик не является точными часами при длительных критических секциях.

---

## 5. Микросекундная задержка через DWT CYCCNT

```cpp
#if not defined(STM32L0) && not defined(STM32G0)
static void     Enable_CYCCNT();
static void     Delay_us(uint32_t delay);
static uint32_t SWOTrace(const uint8_t* ptr, uint32_t len);
#endif
```

Доступно только там, где есть блок DWT — Cortex-M4/M7 (F4/F7), **не** Cortex-M0/M0+ (G0/L0).

**`Enable_CYCCNT()`** включает трассировку (`DEMCR.TRCENA`) и счётчик тактов (`DWT->CTRL.CYCCNTENA`). Обязателен до первого `Delay_us()`. На F7 сначала разблокирует DWT. Уже работающий счётчик не сбрасывается; выход ITM не настраивается.

```cpp
void System::Delay_us(uint32_t delay) {
    const uint64_t cycles = (uint64_t(delay) * SystemCoreClock + 999999ULL) / 1000000ULL;
    const uint32_t wait = cycles > 0x7FFFFFFFULL ? 0x7FFFFFFFUL : uint32_t(cycles);
    const uint32_t start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < wait) {}
}
```

- 64-битное произведение исключает переполнение; округление **вверх** сохраняет дробные такты, если частота ядра не кратна целому МГц.
- Большие аргументы ограничены **`0x7FFFFFFF` тактами**, то есть `0x7FFFFFFF / SystemCoreClock` секунд:

| Частота ядра | Максимальная задержка |
| --- | --- |
| 16 МГц | ≈ 134,2 с |
| 180 МГц | ≈ 11,9 с |
| 216 МГц | ≈ 9,94 с |

- Беззнаковое вычитание корректно при переполнении счётчика. Если опрос приостановлен на половину периода счётчика, конец всё же можно пропустить.
- CYCCNT должен работать, а `SystemCoreClock` — соответствовать аппаратуре и не меняться во время задержки.

**`SWOTrace(ptr, len)`** пишет в канал ITM 0 **без ожидания** и возвращает число принятых портом байтов (может быть меньше `len`). Трассировка или канал выключены, порт не готов или `nullptr` — сразу возврат. ITM/SWO настраивает отладчик; `Enable_CYCCNT()` этого не делает.

---

## 6. MPU на STM32F7

### Защита внешней памяти

`System::MPU_Init()` устанавливает в **регионе MPU 0** защиту, которая блокирует обычные и **спекулятивные** обращения процессора к неиспользуемым адресам внешней памяти (Cortex-M7 может спекулятивно читать оттуда). Контроллер внешней памяти и DMA-буферы она не настраивает.

Регион охватывает 4 ГиБ в восьми подрегионах по 512 МиБ; маска `0x87` отключает подрегионы 0, 1, 2 и 7, поэтому защищены только 3…6:

| Диапазон адресов | Поведение для привилегированного кода |
| --- | --- |
| `0x00000000–0x5FFFFFFF` | Стандартная карта памяти, если её не перекрывает другой регион |
| `0x60000000–0xDFFFFFFF` | Чтение, запись и исполнение запрещены |
| `0xE0000000–0xFFFFFFFF` | Стандартная карта памяти, если её не перекрывает другой регион |

- Закрытый диапазон включает память FMC, отображения управляющих регистров FMC и memory-mapped QSPI. Обращения туда вызывают исключение — MemManage, если он включён, иначе HardFault, — если их не разрешает регион с большим номером.
- Регион 0 перезаписывается; регионы 1+ сохраняют настройки и имеют приоритет при перекрытии.
- `PRIVDEFENA` сохраняет стандартную карту для привилегированного доступа вне защиты. Непривилегированному коду нужны явные разрешения.
- `HFNMIENA` не установлен — внутри обработчиков NMI/HardFault MPU не действует.

**Использование внешней памяти:** вызовите защиту и настройте свои регионы внешней памяти **до** включения кешей в `System::Init()`. Пример — 64 КиБ RAM данных по адресу `0xC0000000` (адрес, размер и атрибуты подберите под свою плату; FMC настраивается отдельно до первого обращения):

```cpp
System::MPU_Init();

__DMB();
MPU->CTRL = 0;
MPU->RNR  = 1;                 // приоритет выше защиты региона 0
MPU->RBAR = 0xC0000000UL;      // начало выровнено по размеру 64 КиБ
MPU->RASR = MPU_RASR_ENABLE_Msk
          | (15UL << MPU_RASR_SIZE_Pos) // 2^(15 + 1) = 64 КиБ
          | (3UL  << MPU_RASR_AP_Pos)   // чтение и запись
          | (1UL  << MPU_RASR_TEX_Pos)
          | MPU_RASR_C_Msk | MPU_RASR_B_Msk // Normal, write-back/write-allocate
          | MPU_RASR_XN_Msk;           // только данные, исполнение запрещено
MPU->CTRL = MPU_CTRL_ENABLE_Msk | MPU_CTRL_PRIVDEFENA_Msk;
__DSB();
__ISB();

System::Init();                // кеши F7 включаются после настройки MPU
```

Защита не меняет кешируемость внутренней SRAM. `MPU_Init()` необязателен и DMA-области не создаёт.

### Необязательные некешируемые DMA-области (F7 + base)

Некешируемые буферы позволяют DMA и процессору делить память **без обслуживания кеша**. Пошаговое руководство с примерами для драйверов — в базе: [DMA_REGIONS.md](https://github.com/vsm-stm/stm32_cmake_base/blob/main/DMA_REGIONS.md#русский). Объявите области в `project.json` базы:

```json
"dma_regions": [
  {"name": "adc", "size": "0x4000", "memory": "SRAM2", "mpu_region": 7}
]
```

**Правила размещения:**

- Адреса автоматические: CMake выделяет области с **конца** выбранной внутренней SRAM1/SRAM2 в порядке списка, начало выравнивается по размеру области.
- `size` — степень двойки, ≥ 32 байт. Промежутки выравнивания тоже резервируются; `.data`/`.bss`, heap, stack и `.sram2_bss` не могут пересечься с областями.
- В SRAM1 должно остаться место для обычных данных/heap/stack; SRAM2 можно занять целиком. Не хватает памяти → ошибка конфигурации или линковки.
- `name` — уникальный идентификатор; `mpu_region` — уникальный, 1…7 (регион 0 зарезервирован).
- Пустой список, `null` или отсутствие ключа → областей нет. Непустой список требует F7 с драйверами; внешняя RAM и явный `address` не поддерживаются.

**Размещение буферов** (глобальные или static, только с нулевой инициализацией):

```cpp
__attribute__((section(".dma.adc"))) static uint16_t adc_buffer[1024];
```

- Секция `NOLOAD`; таблица обнуления startup очищает её до конструкторов.
- Ненулевые начальные значения и объекты с динамической инициализацией **не** поддерживаются (NOLOAD ничего не копирует из Flash).
- Буфер в ненастроенной секции `.dma.*` → ошибка линковки. Локальные переменные и heap туда не попадают.

**Что делает с ними `System::Init()`:** читает таблицу линкера и программирует только эти регионы MPU как Normal, некешируемые, shareable, чтение/запись, без исполнения (`TEX=1, C=0, B=0, S=1, AP=3, XN=1`). Если D-cache уже включён, CMSIS при выключении делает clean/invalidate (изменённые данные сохраняются в RAM), MPU меняется, затем D-cache включается снова. Эта операция со всем кешем выполняется один раз при инициализации, а не на каждую передачу.

- Остальные регионы MPU сохраняются; выбирайте номера без конфликта с вашей картой — перекрывающий регион с большим номером может изменить атрибуты DMA-памяти.
- Init включает стандартную карту для привилегированного доступа и сохраняет остальные флаги MPU. Защиту внешней памяти в регионе 0 он **не** устанавливает автоматически.
- Вызывайте `Init()` в начале `main`, до настройки DMA/периферии; конструкторы не должны запускать DMA. Загрузчик должен остановить свой DMA перед передачей управления.

**Требования:** обновлённый шаблон `STM32-base_files/linker/linker-f7.ld.in` и версия драйверов с поддержкой DMA-областей (конфигурация отклоняет старые драйверы, чтобы буферы не остались кешируемыми молча). Для сохранённого в проекте шаблона обновите файл или укажите `STM32_BASE_FILES_DIR` на обновлённую рабочую копию. Каждый образ использует области из своего `project.json` (загрузчик не наследует области приложения). Собственные linker-скрипты без таблицы продолжают работать через weak-символы, но без автоматической настройки MPU для DMA.

### Правила кеша для DMA с кешируемыми буферами

Некешируемым буферам обслуживание не нужно, но владение CPU/DMA и порядок по-прежнему важны. Для обычных кешируемых буферов:

| Передача | Правило |
| --- | --- |
| TX | clean линий буфера **до** того, как DMA их прочитает |
| RX | clean+invalidate **до** запуска DMA; invalidate **после** завершения, до чтения процессором. Нельзя делать clean устаревших RX-линий после приёма |

- Обрабатываются только затронутые линии. На F7 линия — 32 байта; буфер (и независимо обрабатываемые части) должен владеть целыми выровненными линиями — округление не должно задевать посторонние изменяемые данные.
- Процессор не обращается к части буфера, принадлежащей DMA, во время передачи. Кольцевым буферам нужны владение и обслуживание для каждой завершённой части.

Общие функции:

```cpp
System::CacheClean(tx_buffer, tx_bytes);
System::CacheCleanInvalidate(rx_buffer, rx_bytes);   // до запуска RX
System::CacheInvalidate(rx_buffer, received_bytes);  // после остановки DMA
```

- Драйверы периферии сами выбирают адрес, размер и момент вызова; при прямом использовании DMA вызовы делает пользователь. DMA не хранит состояние кеша.
- F7: для ненулевого адреса/размера функции напрямую вызывают CMSIS. Адреса DTCM, атрибуты MPU, записи линкера и состояние включения D-cache не анализируются.
- Последний аргумент конструктора — `System::CachePolicy`, неизменный для объекта. По умолчанию `Cacheable`: можно передавать любой допустимый DMA-буфер, включая некешируемую память; на F7 драйвер выполняет обычное обслуживание кеша без определения области памяти. `NonCacheable` пропускает все вызовы обслуживания кеша одновременно для TX и RX (для TIM — всех DMA-каналов). В этом режиме приложение обязано размещать **все** используемые объектом буферы целиком в DTCM или некешируемой памяти MPU; передача кешируемого буфера — ошибка вызывающего кода. Политика не размещает буферы и не настраивает MPU. На F4/G0 при обоих значениях операций с кешем нет. Требования к завершению DMA и владению буфером сохраняются.
- При прямой работе с DMA для некешируемых буферов эти вызовы не нужны. DTCM обходит D-cache без отдельного региона MPU.
- F4/G0/L0: пустые inline-функции. Нулевой размер или `nullptr` ничего не делает.
- Диапазон должен быть корректным и помещаться в знаковый размер CMSIS (с учётом округления). Вызывайте после `System::Init()`.

Источники: [ST AN4838](https://www.st.com/resource/en/application_note/DM00272912-.pdf) (атрибуты регионов MPU), [ST AN4839](https://www.st.com/resource/en/application_note/an4839-level-1-cache-on-stm32f7-series-and-stm32h7-series-stmicroelectronics.pdf) (согласование кеша и DMA).

---

## 7. DebugTrap — единая точка отказа для всех драйверов

```cpp
[[noreturn]] static void DebugTrap(const char* msg);
```

Общий для проекта способ остановиться на **программной ошибке/ошибке конфигурации** (в отличие от сбоя периферии во время работы, который возвращается статусом).

| Сборка | Поведение |
| --- | --- |
| **Debug** (`DEBUG` определён) | сохраняет `msg` в `volatile _debug_trap_msg` (видно в Watch/Memory, даже если стек испорчен оптимизацией); на F4/F7 при включённых трассировке и канале ITM 0 выводит сообщение через **блокирующий SWO** (`ITM_SendChar()`); затем `__BKPT(0)` и бесконечный цикл |
| **Release** | встроенный бесконечный цикл в `system.hpp`; аргумент игнорируется, поэтому неиспользуемые строки удаляет оптимизатор |

**Ошибки конфигурации в конструкторах** используют `STM32_CONFIG_ERROR("причина")` (см. также [GPIO.md §6](GPIO.md#6-constexpr-таблицы-пинов-и-защита-от-опечаток)):

```cpp
#define STM32_CONFIG_ERROR(text) \
  (std::is_constant_evaluated() ? ::stm32::CONFIGURATION_ERROR<text>() : ::System::DebugTrap(text))
```

- **При компиляции** (`constinit`/`consteval`): вызывается намеренно не-`constexpr` функция `stm32::CONFIGURATION_ERROR<reason>()`. Текст — параметр шаблона, поэтому GCC печатает причину **в самом сообщении об ошибке**.
- **Во время работы**: напрямую вызывается `DebugTrap(text)` — отладчик показывает читаемое сообщение, а не кадр шаблонной функции.
- `constinit` обязателен по правилам API драйверов. Примеры и диагностика: [Initialization.md](Initialization.md).

**Замечания:**

- ITM/SWO настраивает отладчик. При выключенной трассировке или `msg == nullptr` вывод пропускается; на G0/L0 ITM нет.
- Включённый, но остановившийся канал ITM может задержать `DebugTrap` до точки останова.
- Этот путь намеренно блокирующий и не использует SysTick, поэтому работает до `System::Init()` и внутри прерываний.
- Обычный `SWOTrace()` остаётся неблокирующим. `SetDebugOutput()` удалён — callback вывода и UART для `DebugTrap()` нет.

---

## 8. Bit-banding и SysStatus/SysInitStatus

```cpp
enum class SysStatus     : uint8_t { OK, Error, Busy, NotInit, Timeout };
enum class SysInitStatus : uint8_t { NotInit, InitOK, InitError = 0xFF };
```

| Enum | Для чего | Значения |
| --- | --- | --- |
| `SysStatus` | операции во время работы (`Send()`, `Receive()`, …) | `OK`, `Error`, `Busy`, `NotInit`, `Timeout` |
| `SysInitStatus` | однократная настройка (`SetUp()`, `Init()`) | `NotInit`, `InitOK`, `InitError` |

Намеренно грубая схема — нет отдельных кодов «неверный аргумент» и «устройство не найдено» (оба — `Error`/`InitError`), — зато единая на весь проект: вызывающему коду не нужно помнить разные enum для разной периферии. Подробности драйверы сообщают через свои `GetError()`/маски ошибок там, где это нужно.

**`BIT_BB`/`BB_RD`/`BB_WR`** — адресная арифметика bit-banding (`(addr - PERIPH_BASE) * 32 + bit * 4 + PERIPH_BB_BASE`), на которой построены `PIN::SetLevel_BB()`/`GetLevel_BB()`/`TogglePin_BB()`. Определены только для F4 (`#if defined(STM32F4)`), поскольку это возможность ядра, а не GPIO. Сама техника и почему она только на F4: [GPIO.md §5](GPIO.md#5-bit-banding-только-f4).

---

## 9. Примеры использования

### Стандартная последовательность старта (как в main.cpp)

```cpp
int main() {
    System::Init();
    constexpr auto clocks = ClockSystem::CalcPLL(64 * MHz, ClockSystem::PLL_ClockSource::HSI);
    if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
        System::DebugTrap("Clock init");
    System::Enable_CYCCNT();   // только F4/F7; требуется перед первым Delay_us()

    uint32_t tick = System::GetTick();
    for (;;) {
        if (System::GetTick() - tick > 500) {   // безопасно при переполнении
            tick = System::GetTick();
            // раз в 500 мс...
        }
    }
}

extern "C" void NMI_Handler(void)       { while(1){} }
extern "C" void HardFault_Handler(void) { while(1){} }
```

### Микросекундная задержка

```cpp
System::Enable_CYCCNT();     // один раз
System::Delay_us(50);        // 50 мкс, блокирующая
```

### Ожидание аппаратного флага с таймаутом

```cpp
// Ждать установки HSERDY не дольше 10 мс.
if (!System::WaitBits(RCC->CR, RCC_CR_HSERDY, RCC_CR_HSERDY, 10)) {
    // таймаут
}
```

### DebugTrap с выводом через SWO

```cpp
// Включить канал ITM 0 / SWO в отладчике (F4/F7, сборка DEBUG).
System::DebugTrap("Invalid peripheral pin"); // блокирующий вывод, брейкпоинт, останов
```

---

## 10. Угловые случаи

| Ситуация | Поведение |
| --- | --- |
| `SetUp()` периферии до `ClockSystem::Init<config>()` | Делители считаются от HSI 16 МГц, а не от реальной частоты (§3). Порядок: `System::Init()` → `ClockSystem::Init<config>()` → `Enable_CYCCNT()` → `SetUp()` периферии. |
| `Enable_CYCCNT()`/`Delay_us()`/`SWOTrace()` на G0/L0 | Ошибка компиляции — у Cortex-M0/M0+ нет DWT/CYCCNT, функций не существует. |
| `Delay_ms(0xFFFFFFFF)` | Ограничивается `0x7FFFFFFF` тиками вместе с компенсирующим тиком. |
| `DebugTrap()` в release | `msg` не сохраняется; строки может вообще не быть в прошивке. |
| `TickIncrease()` вне `SysTick_Handler` | Не ошибка, но `GetTick()` перестанет соответствовать реальному времени. |
| `Delay_ms()` в IRQ с приоритетом 0 | Никогда не завершится — у SysTick самый низкий приоритет. |

---

## См. также

- [RCC.md](RCC.md) — единственный, кто записывает тактовые переменные (§3).
- [GPIO.md](GPIO.md) — `BIT_BB`/`BB_RD`/`BB_WR` (§8) используются методами `PIN::*_BB()`.
- [IRQ_Registry.md](IRQ_Registry.md) — диспетчеризация периферийных IRQ; `SysTick_Handler`, `NMI_Handler`, `HardFault_Handler` и другие исключения Cortex-M в неё не входят.
- [UART.md](UART.md), [TIM.md](TIM.md), [SPI.md](SPI.md), [RTC.md](RTC.md), [DMA.md](DMA.md), [Flash.md](Flash.md) — используют `SysStatus`/`SysInitStatus` (§8) и `GetTick()` (§4).
- [Initialization.md](Initialization.md) — `constinit` и диагностика `STM32_CONFIG_ERROR`.
