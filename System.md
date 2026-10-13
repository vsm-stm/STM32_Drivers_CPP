# System

*[English](#english) · [Русский](#русский)*

---

## English

## System — Detailed Description

### Contents

- [System — Detailed Description](#system--detailed-description)
	- [Contents](#contents)
	- [1. Why this exists — the connective layer between all drivers](#1-why-this-exists--the-connective-layer-between-all-drivers)
	- [2. System::Init() — what gets configured at startup](#2-systeminit--what-gets-configured-at-startup)
	- [3. Clock variables — shared memory between RCC and all drivers](#3-clock-variables--shared-memory-between-rcc-and-all-drivers)
	- [4. SysTick and the millisecond counter](#4-systick-and-the-millisecond-counter)
	- [5. Microsecond delay via DWT CYCCNT](#5-microsecond-delay-via-dwt-cyccnt)
	- [MPU on STM32F7](#mpu-on-stm32f7)
	- [6. DebugTrap — a single failure point for all drivers](#6-debugtrap--a-single-failure-point-for-all-drivers)
	- [7. Bit-banding and SysStatus/SysInitStatus](#7-bit-banding-and-sysstatussysinitstatus)
	- [8. Usage examples](#8-usage-examples)
		- [Standard startup sequence (as in main.cpp)](#standard-startup-sequence-as-in-maincpp)
		- [Microsecond delay](#microsecond-delay)
		- [DebugTrap with SWO output](#debugtrap-with-swo-output)
	- [9. Edge cases](#9-edge-cases)
	- [See also](#see-also)

---

### 1. Why this exists — the connective layer between all drivers

`system.hpp`/`system.cpp` is not a separate peripheral but **shared infrastructure** that
literally every other driver in this project depends on:

```
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

Three things that `System` gives to all other drivers:

1. **`SysStatus`/`SysInitStatus`** — two shared result enums for operations/initialization,
   used by literally every `.hpp` in `Drivers/src` (see §7) — a single "success/error/busy/
   timeout" vocabulary instead of each driver inventing its own set of codes.
2. **Clock variables** (`SystemCoreClock`, `APB1BusClock`, ...) — written by
   [`ClockSystem::Init()`](RCC.md), read by `UART`/`TIM`/`SPI` to calculate dividers/BRR
   (see §3).
3. **`System::GetTick()`/`Delay_ms()`/`Delay_us()`** — timeouts for blocking operations
   (`USART::Send()`, `SPI::Send()`, the HSE/PLL waits in `ClockSystem::Init()`) are all built
   on the same millisecond counter (§4).

---

### 2. System::Init() — what gets configured at startup

```cpp
static SysInitStatus Init();
```

Called once at the very start of `main()`, before any other driver (see `src/main.cpp`).
Configures low-level things that don't belong to any particular peripheral:

1. **FPU** (with `__FPU_PRESENT`/`__FPU_USED`) — unless
   `STM32_STARTUP_ENABLES_FPU` is defined, grants access to CP10/CP11 and executes
   DSB/ISB. The current base exports that definition because startup enables
   FPU before constructors. An alternate startup must omit the definition
   unless it performs that initialization itself.
2. **Restore system clocking (F4/F7/G0)** — enable HSI and wait for readiness
   without a timeout, select HSI and wait for the actual source switch without
   a timeout. Preserve bus dividers during that switch, then restore AHB/APB
   dividers to /1 (and G0 HSIDIV to /1).
   Update the frequency variables and start SysTick at 16 MHz. On G0 the APB2
   and APB2 timer frequencies are zero because that bus does not exist.
3. **Power and Flash latency with tick timeouts** — G0 restores VOS Range 1
   (100 ms). Finally reduce Flash latency to 0 WS and check the register
   (100 ms). A failed timed wait returns `InitError`;
   initialization stops at that step, with SYSCLK already on HSI.
4. **Flash accelerator/cache** — family-specific:
   - F4: `PRFTEN` (prefetch) + `ICEN`/`DCEN` (instruction/data cache).
   - F7: `PRFTEN` + `ARTEN` (ART Accelerator) + `SCB_EnableICache()`/`SCB_EnableDCache()`
     (caches of the Cortex-M7 core itself, not the Flash controller) + enabling `PWR` clocking
     (needed for voltage scaling, which [`ClockSystem`](RCC.md) controls).
   - G0: `PRFTEN` + `ICEN`.
   - L0: nothing — this core has no ART/cache, wait states are configured differently.

Call with interrupts enabled, outside interrupt handlers, so the newly started
SysTick can service the timed waits. No IPSR/PRIMASK checks or alternate timer
are added. The initial HSI waits deliberately have no timeout; HSI failure can
keep initialization there. G0 temporarily raises 0 WS to 1 WS before selecting
16 MHz, so a previous VOS Range 2 configuration remains safe until Range 1 is ready.

This is a startup operation, before peripheral setup. It selects HSI and changes
bus dividers; PLL/HSE enable and configuration, HSI trim, MCO, RCC interrupts,
peripheral enable/reset registers, RTC, LSE/LSI, other peripheral clock muxes
and the tick count are retained. After a later clock change, `ClockSystem`
updates the frequencies and calls `InitTicks()` again.

**L0 limitation:** its existing path is retained. It only sets the software
frequencies to 16 MHz and initializes SysTick; it assumes hardware already uses
HSI at that frequency. RCC, power and Flash latency are not restored on L0.

```cpp
int main() {
    System::Init();                                    // FPU, Flash/cache, SysTick @ HSI
    ClockSystem::InitCalcPLL(180'000'000, ..., 8'000'000); // changes the frequency, calls InitTicks() itself
    System::Enable_CYCCNT();                            // for Delay_us(), see §5
    ...
}
```

Startup initializes RAM and invokes global/static constructors before main.
The current base also enables FPU there. Such constructors must not wait for
SysTick: it has not been started and startup still masks interrupts.

---

### 3. Clock variables — shared memory between RCC and all drivers

```cpp
static uint32_t SystemCoreClock; // AHB/core/GPIO/DMA/USB
static uint32_t APB1BusClock;    // WWDG, SPI2/3, USART2/3, UART4/5, I2C1-3, CAN1/2, DAC
static uint32_t APB2BusClock;    // USART1/6, ADC1-3, SPI1/4, SYSCFG, SAI1/2
static uint32_t TIMxAPB1Clock;   // TIM2-7, TIM12-14 (taking the "x2" rule into account, see RCC.md §2)
static uint32_t TIMxAPB2Clock;   // TIM1, TIM8-11
```

These are not constants but ordinary static variables — **`ClockSystem::Init()`/
`InitCalcPLL()` are the only places that write them** (see [RCC.md](RCC.md)); every other
driver only reads them:

- `USART::SetUp()` — `USARTx->BRR = *_info->bus_clk / BaudRate;` (where `bus_clk` points to
  `APB1BusClock` or `APB2BusClock` depending on which bus the particular USART sits on — see
  [UART.md](UART.md)).
- `TIM::SetFrequency()` — the same thing through `_info->bus_clk`, but against
  `TIMxAPB1Clock`/`TIMxAPB2Clock` (see [TIM.md](TIM.md)).
- `SPI` — calculates `BaudRate` through a clock divider the same way.

So the order of calls in `main()` matters: `ClockSystem::Init()` must run **before**
`SetUp()` of any peripheral driver — otherwise `USART::SetUp()` will compute `BRR` from
16 MHz (HSI, the default value set by `System::Init()`) instead of the real post-PLL
frequency.

---

### 4. SysTick and the millisecond counter

```cpp
static SysInitStatus InitTicks();   // SysTick_Config(SystemCoreClock / TICK_BASE)
static void          TickIncrease(); // called from SysTick_Handler
static uint32_t      GetTick();
static void          Delay_ms(uint32_t delay);
```

`TICK_BASE = 1 kHz` — SysTick is configured to interrupt once every 1 ms.
`SysTick_Config()` assigns its fixed lowest priority on each call, including
reconfiguration after a clock change. The handler:

```cpp
extern "C" void SysTick_Handler(void) { System::TickIncrease(); }
```

increments a single `static volatile uint32_t Tick` (a module-level variable, not a class
field) — `GetTick()` simply reads it. On a 32-bit ARM with an aligned address this read is
atomic by itself, so `GetTick()` needs neither `__disable_irq()` nor atomics.

`Delay_ms()` is not `while(GetTick() < target)`, but uses a difference instead:

```cpp
const uint32_t wait = (delay < 0x7FFFFFFFUL) ? delay + 1UL : 0x7FFFFFFFUL;
while ((GetTick() - tick_start) < wait) {}
```

The wait includes one tick to compensate for the partial first tick and is
capped at `0x7FFFFFFF` ticks (about 24.9 days at 1 kHz). Larger requests saturate;
`Delay_ms(0)` still waits for the next tick. To observe completion before the
subtraction wraps again, polling must resume within half the counter range.

Subtraction instead of a direct comparison is the same "wrap-around safe" reasoning as in
[`TIM_HWCounter`](TIM.md#67-tim_hwcounter)/`Delay_us()` below: unsigned subtraction works
correctly even when `Tick` manages to wrap around (after ~49.7 days of continuous operation)
right in the middle of a wait — `target - tick_start` stays the correct distance in ticks,
while `GetTick() < target` would give a wrong answer at that moment. All timeouts in the
project (`USART::Send`, the HSE/PLL/CLOCKSWITCH timeouts in `ClockSystem::Init`, ...) use this
same `GetTick() - tick_start > timeout` pattern instead of storing an absolute "deadline".

---

`Delay_ms()` requires a running, serviceable SysTick. It cannot finish with
SysTick masked, before initialization or from a handler of equal/higher
preemption priority. Callbacks invoked by an ISR still execute in that ISR
after a local interrupt mask is restored. `GetTick()` itself may be read there.
No IPSR/PRIMASK checks or alternate timer are introduced. Long periods without
SysTick servicing lose elapsed ticks: one pending exception does not count all
missed periods. This counter is not a wall clock across long critical sections.

`IPSR` identifies the active exception (zero in ordinary code); `PRIMASK` masks
configurable interrupts when set. On F4/F7, `BASEPRI` can also mask SysTick by
priority. These registers explain the restriction; the driver does not inspect
them. `InitTicks()` gives SysTick the lowest priority. Other IRQs left at their
default priority 0 therefore run ahead of SysTick and cannot call `Delay_ms()`
or wait for a `GetTick()` timeout inside their handlers. Equal preemption
priorities also cannot interrupt one another, regardless of subpriority.

### 5. Microsecond delay via DWT CYCCNT

```cpp
#if not defined(STM32L0) && not defined(STM32G0)
static void     Enable_CYCCNT();
static void     Delay_us(uint32_t delay);
static uint32_t SWOTrace(const uint8_t* ptr, uint32_t len);
#endif
```

Available only where a DWT block exists (Cortex-M4/M7 — **not** on Cortex-M0/M0+, hence the
`!L0 && !G0` condition). `Enable_CYCCNT()` enables tracing (`DEMCR::TRCENA`) and the cycle
counter itself (`DWT->CTRL::CYCCNTENA`) — mandatory before the first `Delay_us()`.

```cpp
void System::Delay_us(uint32_t delay) {
    const uint64_t cycles = (uint64_t(delay) * SystemCoreClock + 999999ULL) / 1000000ULL;
    const uint32_t wait = cycles > 0x7FFFFFFFULL ? 0x7FFFFFFFUL : uint32_t(cycles);
    const uint32_t start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < wait) {}
}
```

The 64-bit product avoids overflow and rounding up avoids losing fractional
cycles when the core frequency is not a whole number of MHz. Large arguments
are clamped to **`0x7FFFFFFF` cycles**, not to a fixed microsecond duration.
The maximum duration is `0x7FFFFFFF / SystemCoreClock` seconds: about 134.2 s at
16 MHz, 11.9 s at 180 MHz and 9.94 s at 216 MHz.

Unsigned subtraction handles counter wrap-around. The half-range limit gives
the polling loop half the counter period to observe completion. Suspending
polling for that long can still miss the interval. CYCCNT must be running and
`SystemCoreClock` must match hardware and remain unchanged during the delay.

F7 initialization unlocks DWT before enabling CYCCNT. It does not reset an
already-running cycle counter and does not configure the ITM output.

`SWOTrace()` writes to ITM channel 0 without waiting. It returns the number of
bytes accepted by the port, which may be smaller than the requested length.
Disabled tracing, a disabled channel, an unready port or a null pointer yields
an immediate return. The debugger must configure ITM/SWO; enabling CYCCNT does
not do that. Availability on F4/F7 follows the core's ITM capabilities.

---

### MPU on STM32F7

`System::MPU_Init()` installs an access guard in MPU region 0. It prevents
ordinary and speculative CPU accesses to unused external-memory addresses.
The guard does not initialize an external-memory controller or DMA buffers.

The region spans 4 GiB and is divided into eight 512 MiB subregions. The
`0x87` mask disables subregions 0, 1, 2 and 7; only 3 through 6 are guarded:

| Address range | Effect for privileged CPU code |
|---|---|
| `0x00000000–0x5FFFFFFF` | Standard memory map unless another MPU region overrides it |
| `0x60000000–0xDFFFFFFF` | No read, write or instruction execution |
| `0xE0000000–0xFFFFFFFF` | Standard memory map unless another MPU region overrides it |

The guarded range includes FMC memory, FMC control mappings and memory-mapped
QSPI. Without permission from a higher-numbered region, accesses there fault:
MemManage if enabled, otherwise escalation to HardFault. Region 0 is replaced;
regions 1 and above retain their configurations and take precedence where they
overlap. `PRIVDEFENA` retains the default map for privileged accesses outside
the guard. Unprivileged code needs its own explicit region permissions.
`HFNMIENA` is not enabled, so this MPU configuration is not enforced in NMI or
HardFault handlers.

Call the guard setup and configure any used external-memory regions before
enabling caches in `System::Init()`. The following example grants access to
64 KiB of data RAM at `0xC0000000`; adapt the address, size and attributes to
your actual board. It only configures the MPU: configure FMC separately before
reading or writing that memory.

```cpp
System::MPU_Init();

__DMB();
MPU->CTRL = 0;
MPU->RNR  = 1;                 // Higher priority than the region 0 guard
MPU->RBAR = 0xC0000000UL;      // Base aligned to the 64 KiB region size
MPU->RASR = MPU_RASR_ENABLE_Msk
          | (15UL << MPU_RASR_SIZE_Pos) // 2^(15 + 1) = 64 KiB
          | (3UL  << MPU_RASR_AP_Pos)   // Read/write access
          | (1UL  << MPU_RASR_TEX_Pos)
          | MPU_RASR_C_Msk | MPU_RASR_B_Msk // Normal, write-back/write-allocate
          | MPU_RASR_XN_Msk;           // Data only, no execution
MPU->CTRL = MPU_CTRL_ENABLE_Msk | MPU_CTRL_PRIVDEFENA_Msk;
__DSB();
__ISB();

System::Init();                // Enables the F7 caches after the MPU setup
```

The guard itself does not change the cacheability of internal SRAM.
`MPU_Init()` remains optional and does not create DMA areas.

#### Optional non-cacheable DMA areas (F7 + base)

In the base's `project.json`, enable drivers and specify named areas:

```json
"dma_regions": [
  {"name": "adc", "size": "0x4000", "memory": "SRAM2", "mpu_region": 7}
]
```

The address is automatic: CMake allocates each area from the end of the selected
internal SRAM1/SRAM2 bank, in list order, aligning its base to its size. Sizes
must be powers of two, at least 32 bytes. Alignment gaps are reserved too;
ordinary `.data`/`.bss`, heap, stack and `.sram2_bss` cannot overlap those areas.
SRAM1 must retain room for ordinary data/heap/stack; SRAM2 can be used entirely.
Insufficient memory is a configuration or link error. Names must be unique
identifiers; MPU numbers must be unique in 1..7 (region 0 remains reserved).
An empty list, null or missing key reserves no DMA area. Nonempty configuration
requires F7 with drivers; external RAM and explicit `address` are unsupported.

Place global or static zero-initialized buffers in the corresponding section:

```cpp
__attribute__((section(".dma.adc"))) static uint16_t adc_buffer[1024];
```

The linker uses `NOLOAD`; the existing startup zero table clears the placed
buffers before constructors. Nonzero initializers and objects requiring dynamic
initialization are unsupported: NOLOAD does not copy their initial data from
Flash. A buffer in an unconfigured `.dma.*` section causes a link error.
Automatic local variables and heap allocations do not enter these sections.

`System::Init()` reads the linker table and applies only the specified MPU
regions: Normal non-cacheable, shareable, read/write, execute-never
(`TEX=1, C=0, B=0, S=1, AP=3, XN=1`). If D-cache is already enabled, the CMSIS
disable function cleans/invalidates it, preserving dirty data in RAM, before
the MPU change. Init then enables D-cache again. This whole-cache operation
occurs during initialization, not on every DMA transfer.

Other MPU regions remain configured. Select MPU numbers that do not conflict
with your own map; a higher-numbered overlapping region can override the DMA
attributes. Init enables the privileged default memory map and retains other
MPU control flags. It does not automatically install the region 0 external-memory
guard. Call Init at the beginning of main before DMA/peripheral setup; constructors
must not start DMA. The bootloader must stop its DMA before handing over control.

The base needs the updated `STM32-base_files/linker/linker-f7.ld.in` template.
It also requires a driver version with DMA-region support; configuration rejects
older versions rather than silently leaving the buffers cacheable.
For an existing vendored template, refresh that file or configure with
`STM32_BASE_FILES_DIR` pointing to the updated checkout. Each firmware image
uses the areas from its own project.json; an application's areas are not inherited
by the bootloader. Standalone/custom linker scripts without the table continue
to work through weak symbols, with no automatic DMA MPU setup.

Non-cacheable buffers do not need cache maintenance, but CPU/DMA ownership and
transfer ordering still apply. DMA with ordinary cacheable buffers still needs
maintenance at transfer boundaries. Adding this to the remaining drivers is a
separate agreed step.

For subsequent driver changes, the agreed transfer rules are:

- TX: clean the buffer's cache lines before DMA reads the data.
- RX: clean/invalidate the buffer before DMA starts, then invalidate after
  completion before the CPU reads it. Do not clean stale RX lines afterwards.
- Operate on the affected lines, not the entire cache. F7 lines are 32 bytes;
  buffers and independently processed portions need exclusive, aligned lines.
  Rounding a range must not include unrelated writable data in adjacent bytes.
- CPU must not access the DMA-owned portion during transfer. Circular buffers
  require ownership and cache maintenance for each completed portion.

These operations are to be added inside the relevant DMA/peripheral drivers
later; this System change does not implement those transfer paths.

See [ST AN4838](https://www.st.com/resource/en/application_note/DM00272912-.pdf)
for MPU region attributes and
[ST AN4839](https://www.st.com/resource/en/application_note/an4839-level-1-cache-on-stm32f7-series-and-stm32h7-series-stmicroelectronics.pdf)
for cache/DMA coherency.

### 6. DebugTrap — a single failure point for all drivers

```cpp
[[noreturn]] static void DebugTrap(const char* msg);
```

The one project-wide way to report "this is a software configuration error, not a runtime
peripheral failure" — used in the constructors of `USART`/`SPI`/`TIM_PWM`/... to check "this
pin belongs to the wrong peripheral module" (see
[GPIO.md §6](GPIO.md#6-constexpr-таблицы-пинов-и-защита-от-опечаток)).

Behavior depends on the build:

- **Debug** (`DEBUG` defined by CMake): `msg` is stored in `volatile _debug_trap_msg`
  (visible in the debugger's Watch/Memory window even without a call stack — useful if
  optimization inlines/destroys the stack). On F4/F7, when trace and ITM channel 0
  are enabled, the null-terminated message is sent through **blocking SWO** using
  `ITM_SendChar()`. Then `__BKPT(0)` is executed and the function loops forever.
- **Release**: ignores the argument and loops forever. String elimination
  depends on compiler/linker optimization; it is not guaranteed by this function.

The debugger must configure ITM/SWO. Disabled trace or `nullptr` skips the
output; G0/L0 have no ITM output. An enabled but stalled ITM channel can keep
the function waiting before BKPT. This path intentionally blocks and uses no
SysTick, so it can report an error before `System::Init()` or from an interrupt.
Ordinary `SWOTrace()` remains nonblocking. `SetDebugOutput()` has been removed;
there is no output callback or UART setup for `DebugTrap()`.

---

### 7. Bit-banding and SysStatus/SysInitStatus

```cpp
enum class SysStatus     : uint8_t { OK, Error, Busy, NotInit, Timeout };
enum class SysInitStatus : uint8_t { NotInit, InitOK, InitError = 0xFF };
```

A shared result vocabulary for all drivers — `SysStatus` for runtime operations (`Send()`,
`Receive()`, ...), `SysInitStatus` for one-time setup (`SetUp()`, `Init()`). Not the most
granular scheme (there's no separate code for "invalid argument" vs. "device not found" —
both are usually just `Error`/`InitError`), but it's uniform across the whole project, so
calling code doesn't need to remember different enums for different peripherals.

`BIT_BB`/`BB_RD`/`BB_WR` — low-level bit-banding address-arithmetic functions
(`(addr - PERIPH_BASE) * 32 + bit * 4 + PERIPH_BB_BASE`), on which `PIN::SetLevel_BB()`/
`GetLevel_BB()`/`TogglePin_BB()` are built — a detailed explanation of the bit-banding
technique itself, and why it's only available on F4, is in
[GPIO.md §5](GPIO.md#5-bit-banding-только-f4); here these functions are simply defined
(F4-only, `#if defined(STM32F4)`), since historically this is a system-wide core capability
rather than a GPIO-specific one.

---

### 8. Usage examples

#### Standard startup sequence (as in main.cpp)

```cpp
int main() {
    System::Init();
    ClockSystem::InitCalcPLL(180'000'000, ClockSystem::PLL_ClockSource::HSE, 8'000'000);
    System::Enable_CYCCNT();   // required before the first Delay_us()

    uint32_t tick = System::GetTick();
    for (;;) {
        if (System::GetTick() - tick > 500) {
            tick = System::GetTick();
            // once every 500 ms...
        }
    }
}

extern "C" void NMI_Handler(void)       { while(1){} }
extern "C" void HardFault_Handler(void) { while(1){} }
```

#### Microsecond delay

```cpp
System::Enable_CYCCNT();     // once
System::Delay_us(50);        // 50 us, blocking
```

#### DebugTrap with SWO output

```cpp
// Enable ITM channel 0 / SWO in the debugger (F4/F7, DEBUG build).
System::DebugTrap("Invalid peripheral pin"); // blocking output, breakpoint, halt
```

---

### 9. Edge cases

- **Call order matters**: `System::Init()` →
  `ClockSystem::Init()`/`InitCalcPLL()` → `System::Enable_CYCCNT()` → `SetUp()` of any
  peripheral drivers. A peripheral `SetUp()` called before `ClockSystem::Init()` will compute
  dividers from HSI (16 MHz) rather than the real frequency (see §3).
- **`Enable_CYCCNT()`/`Delay_us()`/`SWOTrace()` are unavailable on STM32L0/G0** — both use
  Cortex-M0/M0+, which has no DWT block with `CYCCNT`. Calling them on these targets is a
  compile error (the functions simply don't exist in this build), not a runtime failure.
- **Large `Delay_ms()` requests** — the wait, including its compensating tick,
  saturates at `0x7FFFFFFF` ticks. `Delay_ms(0xFFFFFFFF)` uses this same limit.
- **`DebugTrap()` in a release build doesn't store `msg` anywhere** — the string may not even
  be included in the binary by the compiler (if `msg` isn't used anywhere else), unlike the
  debug build, where `_debug_trap_msg` always points to it.
- **`TickIncrease()` is only meant to be called from `SysTick_Handler`** — calling it from
  anywhere else isn't an error (it's just a variable increment), but it will break the
  correspondence between `GetTick()` and real time.

---

### See also

- [RCC.md](RCC.md) — the only writer of the clock variables described in §3.
- [GPIO.md](GPIO.md) — `BIT_BB`/`BB_RD`/`BB_WR` (§7) are used by the `PIN::*_BB()` methods.
- [IRQ_Registry.md](IRQ_Registry.md) — an independent mechanism for dispatching peripheral
  IRQs; `SysTick_Handler`/`NMI_Handler`/`HardFault_Handler` and similar Cortex-M exceptions
  are not part of it and are configured separately (see `src/main.cpp`).
- [UART.md](UART.md), [TIM.md](TIM.md), [SPI.md](SPI.md), [RTC.md](RTC.md), [DMA.md](DMA.md),
  [Flash.md](Flash.md) — all read `SysStatus`/`SysInitStatus` (§7) and `System::GetTick()`
  (§4) from this module.

---

## Русский

## System — подробное описание

### Содержание

- [System — подробное описание](#system--подробное-описание)
	- [Содержание](#содержание)
	- [1. Зачем это нужно — связующая часть между всеми драйверами](#1-зачем-это-нужно--связующая-часть-между-всеми-драйверами)
	- [2. System::Init() — что настраивается при старте](#2-systeminit--что-настраивается-при-старте)
	- [3. Тактовые переменные — общая память между RCC и всеми драйверами](#3-тактовые-переменные--общая-память-между-rcc-и-всеми-драйверами)
	- [4. SysTick и миллисекундный счётчик](#4-systick-и-миллисекундный-счётчик)
	- [5. Микросекундная задержка через DWT CYCCNT](#5-микросекундная-задержка-через-dwt-cyccnt)
	- [MPU на STM32F7](#mpu-на-stm32f7)
	- [6. DebugTrap — единая точка отказа для всех драйверов](#6-debugtrap--единая-точка-отказа-для-всех-драйверов)
	- [7. Bit-banding и SysStatus/SysInitStatus](#7-bit-banding-и-sysstatussysinitstatus)
	- [8. Примеры использования](#8-примеры-использования)
		- [Стандартная последовательность старта (как в main.cpp)](#стандартная-последовательность-старта-как-в-maincpp)
		- [Микросекундная задержка](#микросекундная-задержка)
		- [DebugTrap с выводом через SWO](#debugtrap-с-выводом-через-swo)
	- [9. Угловые случаи](#9-угловые-случаи)
	- [См. также](#см-также)

---

### 1. Зачем это нужно — связующая часть между всеми драйверами

`system.hpp`/`system.cpp` — не отдельная периферия, а **общая инфраструктура**, от которой
зависят буквально все остальные драйверы этого проекта:

```
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

Три вещи, которые `System` даёт всем остальным драйверам:

1. **`SysStatus`/`SysInitStatus`** — два общих enum'а результата операции/инициализации,
   которые использует буквально каждый `.hpp` в `Drivers/src` (см. §7) — единый словарь
   "успех/ошибка/занято/таймаут" вместо своего набора кодов в каждом драйвере.
2. **Тактовые переменные** (`SystemCoreClock`, `APB1BusClock`, ...) — записываются
   [`ClockSystem::Init()`](RCC.md), читаются `UART`/`TIM`/`SPI` для расчёта делителей/BRR
   (см. §3).
3. **`System::GetTick()`/`Delay_ms()`/`Delay_us()`** — таймауты блокирующих операций
   (`USART::Send()`, `SPI::Send()`, `ClockSystem::Init()`'s HSE/PLL ожидания) все построены
   на одном и том же миллисекундном счётчике (§4).

---

### 2. System::Init() — что настраивается при старте

```cpp
static SysInitStatus Init();
```

Вызывается один раз в начале `main()`, до любого другого драйвера (см. `src/main.cpp`).
Настраивает низкоуровневые вещи, которые не относятся ни к одной конкретной периферии:

1. **FPU** (с `__FPU_PRESENT`/`__FPU_USED`) — при отсутствии определения
   `STM32_STARTUP_ENABLES_FPU` включает доступ к CP10/CP11 и выполняет DSB/ISB.
   Текущий base передаёт это определение, поскольку startup включает FPU до
   конструкторов. При другом startup определение следует убрать, если он
   самостоятельно не выполняет эту инициализацию.
2. **Возврат системного тактирования (F4/F7/G0)** — включить HSI и дождаться
   готовности без таймаута, выбрать HSI и дождаться фактического переключения
   без таймаута. До подтверждения переключения сохранить делители шин, затем
   вернуть AHB/APB к /1 (HSIDIV G0 тоже к /1). Обновить переменные частот и запустить SysTick
   на 16 МГц. На G0 частоты APB2 и её таймеров равны нулю: этой шины нет.
3. **Питание и Flash latency с таймаутами по тикам** — на G0 вернуть
   VOS Range 1 (100 мс). Затем снизить Flash latency до 0 WS
   и проверить регистр (100 мс). Таймаут возвращает `InitError`; инициализация
   прекращается на этом шаге, SYSCLK к этому моменту уже работает от HSI.
4. **Flash-акселератор/кэш** — по семействам:
   - F4: `PRFTEN` (prefetch) + `ICEN`/`DCEN` (инструкционный/данных кэш).
   - F7: `PRFTEN` + `ARTEN` (ART Accelerator) + `SCB_EnableICache()`/`SCB_EnableDCache()`
     (кэши самого ядра Cortex-M7, не Flash-контроллера) + включение тактирования `PWR`
     (нужно для voltage scaling, которым управляет [`ClockSystem`](RCC.md)).
   - G0: `PRFTEN` + `ICEN`.
   - L0: ничего — нет ART/кэша на этом ядре, wait states настраиваются иначе.

Вызывать с разрешёнными прерываниями, вне обработчиков, чтобы запущенный
SysTick обслуживал таймауты. Проверок IPSR/PRIMASK и дополнительного таймера
нет. Первые ожидания HSI намеренно без таймаута: неисправный HSI может оставить
инициализацию на этом этапе. На G0 перед переходом к 16 МГц значение 0 WS
временно повышается до 1 WS для безопасной работы при оставшемся VOS Range 2.

Это операция старта до настройки периферии. Выбирается HSI и меняются делители
шин; включение и настройки PLL/HSE, подстройка HSI, MCO, RCC-прерывания,
регистры включения/сброса периферии, RTC, LSE/LSI, отдельные мультиплексоры
периферийных тактов и счётчик тиков сохраняются. После дальнейшей смены частоты
`ClockSystem` обновляет переменные и повторно вызывает `InitTicks()`.

**Ограничение L0:** прежняя ветка сохранена. Она только записывает программные
частоты 16 МГц и запускает SysTick, предполагая, что аппаратно уже выбран HSI
с этой частотой. RCC, питание и Flash latency на L0 не восстанавливаются.

```cpp
int main() {
    System::Init();                                    // FPU, Flash/cache, SysTick @ HSI
    ClockSystem::InitCalcPLL(180'000'000, ..., 8'000'000); // меняет частоту, сам вызывает InitTicks()
    System::Enable_CYCCNT();                            // для Delay_us(), см. §5
    ...
}
```

---

### 3. Тактовые переменные — общая память между RCC и всеми драйверами

```cpp
static uint32_t SystemCoreClock; // AHB/ядро/GPIO/DMA/USB
static uint32_t APB1BusClock;    // WWDG, SPI2/3, USART2/3, UART4/5, I2C1-3, CAN1/2, DAC
static uint32_t APB2BusClock;    // USART1/6, ADC1-3, SPI1/4, SYSCFG, SAI1/2
static uint32_t TIMxAPB1Clock;   // TIM2-7, TIM12-14 (с учётом правила "x2", см. RCC.md §2)
static uint32_t TIMxAPB2Clock;   // TIM1, TIM8-11
```

Это не константы, а обычные статические переменные — **`ClockSystem::Init()`/`InitCalcPLL()`
единственные места, которые их пишут** (см. [RCC.md](RCC.md)); все остальные драйверы только
читают:

- `USART::SetUp()` — `USARTx->BRR = *_info->bus_clk / BaudRate;` (где `bus_clk` указывает на
  `APB1BusClock` или `APB2BusClock` в зависимости от того, на какой шине сидит конкретный
  USART — см. [UART.md](UART.md)).
- `TIM::SetFrequency()` — то же самое через `_info->bus_clk`, только на `TIMxAPB1Clock`/
  `TIMxAPB2Clock` (см. [TIM.md](TIM.md)).
- `SPI` — расчёт `BaudRate` через делитель тактовой БД аналогично.

Так что порядок вызовов в `main()` важен: `ClockSystem::Init()` должен отработать **до**
`SetUp()` любого периферийного драйвера — иначе `USART::SetUp()` посчитает `BRR` от 16 МГц
(HSI, значение по умолчанию из `System::Init()`), а не от реальной частоты после PLL.

---

### 4. SysTick и миллисекундный счётчик

```cpp
static SysInitStatus InitTicks();   // SysTick_Config(SystemCoreClock / TICK_BASE)
static void          TickIncrease(); // вызывается из SysTick_Handler
static uint32_t      GetTick();
static void          Delay_ms(uint32_t delay);
```

`TICK_BASE = 1 кГц` — SysTick настроен прерываться раз в 1 мс.
`SysTick_Config()` при каждом вызове назначает ему фиксированный самый низкий
приоритет, в том числе после изменения частоты. Обработчик:

```cpp
extern "C" void SysTick_Handler(void) { System::TickIncrease(); }
```

инкрементирует единственный `static volatile uint32_t Tick` (модульная переменная, не поле
класса) — `GetTick()` просто читает её. На 32-битном ARM с выровненным адресом это чтение
атомарно само по себе, поэтому `GetTick()` не требует ни `__disable_irq()`, ни атомиков.

`Delay_ms()` — не `while(GetTick() < target)`, а через разность:

```cpp
const uint32_t wait = (delay < 0x7FFFFFFFUL) ? delay + 1UL : 0x7FFFFFFFUL;
while ((GetTick() - tick_start) < wait) {}
```

Ожидание включает один тик для компенсации неполного первого тика и ограничено
`0x7FFFFFFF` тиками (около 24,9 суток при 1 кГц). Большие запросы ограничиваются
этим пределом; `Delay_ms(0)` по-прежнему ждёт следующего тика. Чтобы заметить
завершение до нового переполнения разности, опрос должен возобновиться в пределах
половины диапазона счётчика.

Вычитание вместо прямого сравнения — это то же самое "wrap-around safe" рассуждение, что и в
[`TIM_HWCounter`](TIM.md#67-tim_hwcounter)/`Delay_us()` ниже: беззнаковое вычитание корректно
работает даже когда `Tick` успевает переполниться (после ~49.7 суток непрерывной работы) прямо
посреди ожидания — `target - tick_start` останется верным расстоянием в тиках, а `GetTick() <
target` в этот момент дало бы неверный результат. Все таймауты в проекте (`USART::Send`,
`ClockSystem::Init`'s HSE/PLL/CLOCKSWITCH таймауты, ...) используют этот же паттерн
`GetTick() - tick_start > timeout`, а не хранят абсолютный "дедлайн".

---

Для `Delay_ms()` SysTick должен быть запущен и иметь возможность выполнить
обработчик. Ожидание не завершится до инициализации, при маскировке SysTick
или внутри IRQ с равным/более высоким приоритетом вытеснения. Callback из ISR
остаётся в контексте ISR даже после восстановления локальной маски IRQ.
Сам `GetTick()` читать там можно. Проверки IPSR/PRIMASK и запасной таймер не
добавляются. Долгий запрет обслуживания SysTick теряет прошедшие тики: одно
ожидающее исключение не считает все пропущенные периоды. Этот счётчик не
является точными часами при длительных критических секциях.

`IPSR` содержит номер активного исключения (ноль в обычном коде); установленный
`PRIMASK` запрещает маскируемые прерывания. На F4/F7 `BASEPRI` также может
маскировать SysTick по приоритету. Эти регистры объясняют ограничение, драйвер
их не проверяет. `InitTicks()` назначает SysTick самый низкий приоритет.
Остальные IRQ с исходным приоритетом 0 выполняются раньше SysTick, поэтому
из их обработчиков нельзя вызывать `Delay_ms()` или ждать таймаут по `GetTick()`.
Равные приоритеты вытеснения тоже не прерывают друг друга независимо от subpriority.

### 5. Микросекундная задержка через DWT CYCCNT

```cpp
#if not defined(STM32L0) && not defined(STM32G0)
static void     Enable_CYCCNT();
static void     Delay_us(uint32_t delay);
static uint32_t SWOTrace(const uint8_t* ptr, uint32_t len);
#endif
```

Доступно только там, где есть блок DWT (Cortex-M4/M7 — **не** на Cortex-M0/M0+, откуда и
условие `!L0 && !G0`). `Enable_CYCCNT()` включает трассировку (`DEMCR::TRCENA`) и сам счётчик
тактов (`DWT->CTRL::CYCCNTENA`) — обязателен до первого `Delay_us()`.

```cpp
void System::Delay_us(uint32_t delay) {
    const uint64_t cycles = (uint64_t(delay) * SystemCoreClock + 999999ULL) / 1000000ULL;
    const uint32_t wait = cycles > 0x7FFFFFFFULL ? 0x7FFFFFFFUL : uint32_t(cycles);
    const uint32_t start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < wait) {}
}
```

64-битное произведение исключает переполнение; округление вверх сохраняет
дробные такты при частоте, не кратной целому МГц. Большие аргументы ограничены
**`0x7FFFFFFF` тактами**, а не постоянным количеством микросекунд.
Предел длительности — `0x7FFFFFFF / SystemCoreClock` секунд: примерно 134,2 с
на 16 МГц, 11,9 с на 180 МГц и 9,94 с на 216 МГц.

Беззнаковое вычитание корректно при переполнении счётчика. Ограничение половиной
диапазона оставляет циклу половину периода счётчика для обнаружения завершения.
При приостановке опроса на такое время интервал всё ещё можно пропустить.
CYCCNT должен работать, `SystemCoreClock` — соответствовать аппаратной частоте,
а частота — оставаться неизменной на время задержки.

На F7 запуск разблокирует DWT перед включением CYCCNT. Уже работающий счётчик
не сбрасывается; выход ITM этим не настраивается.

`SWOTrace()` пишет в канал ITM №0 без ожидания. Возвращается число байтов,
принятых портом, которое может быть меньше запрошенной длины. Если трассировка
или канал выключены, порт не готов либо указатель нулевой, функция сразу
возвращается. ITM/SWO настраивает отладчик; включение CYCCNT этого не делает.
Доступность функции на F4/F7 определяется наличием ITM у ядра.

---

### MPU на STM32F7

`System::MPU_Init()` устанавливает защиту от доступа в регионе MPU №0.
Она закрывает неиспользуемые адреса внешней памяти от обычных и спекулятивных
обращений CPU. Функция не инициализирует контроллер внешней памяти или DMA-буферы.

Регион охватывает 4 ГиБ и разделён на восемь подрегионов по 512 МиБ. Маска
`0x87` отключает подрегионы 0, 1, 2 и 7; защита действует только в 3–6:

| Диапазон адресов | Поведение для привилегированного кода CPU |
|---|---|
| `0x00000000–0x5FFFFFFF` | Стандартная карта памяти, если её не перекрывает другой регион |
| `0x60000000–0xDFFFFFFF` | Чтение, запись и исполнение запрещены |
| `0xE0000000–0xFFFFFFFF` | Стандартная карта памяти, если её не перекрывает другой регион |

Закрытый диапазон включает память FMC, отображения управляющих регистров FMC
и memory-mapped QSPI. Без разрешающего региона с большим номером обращения
сюда вызывают исключение: MemManage, если он включён, иначе HardFault.
Регион №0 перезаписывается; настройки регионов №1 и выше сохраняются и имеют
приоритет при перекрытии адресов. `PRIVDEFENA` сохраняет стандартную карту для
привилегированного доступа вне защиты. Непривилегированному коду нужны свои
явные разрешения регионов. `HFNMIENA` не включён: эта настройка MPU не действует
в обработчиках NMI и HardFault.

Защиту и разрешающие регионы используемой внешней памяти следует настроить
до включения кешей в `System::Init()`. Пример ниже разрешает 64 КиБ памяти
данных по адресу `0xC0000000`; адрес, размер и атрибуты нужно выбрать по своей
плате. Это только настройка MPU: FMC отдельно настраивается до первого доступа
к этой памяти.

```cpp
System::MPU_Init();

__DMB();
MPU->CTRL = 0;
MPU->RNR  = 1;                 // Приоритет выше защиты региона №0
MPU->RBAR = 0xC0000000UL;      // Начало выровнено по размеру 64 КиБ
MPU->RASR = MPU_RASR_ENABLE_Msk
          | (15UL << MPU_RASR_SIZE_Pos) // 2^(15 + 1) = 64 КиБ
          | (3UL  << MPU_RASR_AP_Pos)   // Чтение и запись
          | (1UL  << MPU_RASR_TEX_Pos)
          | MPU_RASR_C_Msk | MPU_RASR_B_Msk // Normal, write-back/write-allocate
          | MPU_RASR_XN_Msk;           // Данные, исполнение запрещено
MPU->CTRL = MPU_CTRL_ENABLE_Msk | MPU_CTRL_PRIVDEFENA_Msk;
__DSB();
__ISB();

System::Init();                // Кеши F7 включаются после настройки MPU
```

Сама защита не меняет кешируемость внутренней SRAM.
`MPU_Init()` остаётся необязательным и не создаёт DMA-области.

#### Необязательные некешируемые DMA-области (F7 + base)

В `project.json` базы включите драйверы и задайте именованные области:

```json
"dma_regions": [
  {"name": "adc", "size": "0x4000", "memory": "SRAM2", "mpu_region": 7}
]
```

Адрес вычисляется автоматически: CMake выделяет области с конца выбранной
внутренней SRAM1/SRAM2 в порядке списка, выравнивая начало по размеру области.
Размер — степень двойки, не меньше 32 байт. Отступы для выравнивания также
резервируются; обычные `.data`/`.bss`, heap, stack и `.sram2_bss` не могут
занять эту память. В SRAM1 должно остаться место для обычных данных/heap/stack;
SRAM2 можно занять целиком. Нехватка памяти — ошибка конфигурации или линковки.
Имена — уникальные идентификаторы; номера MPU — уникальные в диапазоне 1..7
(регион 0 остаётся зарезервированным). Пустой список, null или отсутствие ключа
ничего не резервируют. Непустая настройка требует F7 с драйверами; внешняя RAM
и явный `address` не поддерживаются.

Глобальный или static-буфер с нулевой инициализацией размещается в своей секции:

```cpp
__attribute__((section(".dma.adc"))) static uint16_t adc_buffer[1024];
```

Linker использует `NOLOAD`; существующая таблица startup обнуляет размещённые
буферы до конструкторов. Ненулевые начальные значения и объекты с динамической
инициализацией не поддерживаются: NOLOAD не копирует их начальные данные из
Flash. Буфер в ненастроенной секции `.dma.*` вызывает ошибку линковки.
Обычные локальные переменные и выделения из heap в эти секции не попадают.

`System::Init()` читает таблицу linker и настраивает только заданные регионы MPU:
Normal non-cacheable, shareable, чтение/запись, запрет исполнения
(`TEX=1, C=0, B=0, S=1, AP=3, XN=1`). Если D-cache уже включён, функция CMSIS
при выключении выполняет clean/invalidate и сохраняет изменённые данные в RAM
до изменения MPU. Затем Init снова включает D-cache. Эта операция со всем кешем
происходит при инициализации, а не при каждой передаче DMA.

Остальные регионы MPU сохраняются. Выбирайте номера без конфликта с собственной
картой: перекрывающий регион с большим номером может изменить атрибуты DMA-памяти.
Init включает стандартную карту для привилегированного доступа и сохраняет
остальные флаги управления MPU. Защита внешней памяти в регионе 0 автоматически
не устанавливается. Вызывайте Init в начале main до настройки DMA/периферии;
конструкторы не должны запускать DMA. Загрузчик должен остановить свой DMA
перед передачей управления приложению.

Базе нужен обновлённый шаблон `STM32-base_files/linker/linker-f7.ld.in`.
Также нужна версия драйверов с поддержкой DMA-областей; конфигурация отклоняет
старую версию, чтобы буферы не остались кешируемыми без сообщения об ошибке.
Если шаблон уже сохранён в проекте, обновите этот файл либо задайте
`STM32_BASE_FILES_DIR` с путём к обновлённой рабочей копии. Каждый образ использует
области из собственного project.json; настройки приложения не наследуются
загрузчиком. Собственные linker scripts без таблицы продолжают работать через
weak-символы, без автоматической настройки DMA MPU.

Некешируемым буферам обслуживание кеша не требуется, но владение CPU/DMA и
порядок передачи сохраняют значение. Для DMA с обычными кешируемыми буферами
по-прежнему нужно обслуживание на границах передачи. Его добавление в остальные
драйверы остаётся отдельным согласованным этапом.

Для последующих правок драйверов согласованы правила передачи:

- TX: clean строк буфера до чтения данных DMA.
- RX: clean/invalidate буфера перед запуском DMA, затем invalidate после
  завершения до чтения CPU. После RX нельзя делать clean устаревших строк.
- Обрабатываются затронутые строки, а не весь кеш. На F7 строка — 32 байта;
  буфер и независимо обрабатываемые части должны владеть целыми выровненными
  строками. Округление диапазона не должно затрагивать посторонние изменяемые данные.
- CPU не обращается к части буфера, принадлежащей DMA, во время передачи.
  Для кольцевого буфера владение и обслуживание нужны для каждой завершённой части.

Эти операции добавим внутри соответствующих DMA/периферийных драйверов позже;
текущие изменения System не реализуют их пути передачи.

Описание атрибутов регионов — в
[ST AN4838](https://www.st.com/resource/en/application_note/DM00272912-.pdf),
согласование кеша и DMA — в
[ST AN4839](https://www.st.com/resource/en/application_note/an4839-level-1-cache-on-stm32f7-series-and-stm32h7-series-stmicroelectronics.pdf).

### 6. DebugTrap — единая точка отказа для всех драйверов

```cpp
[[noreturn]] static void DebugTrap(const char* msg);
```

Единственный по всему проекту способ сообщить "это программная ошибка конфигурации, а не
рантайм-сбой периферии" — используется в конструкторах `USART`/`SPI`/`TIM_PWM`/... для проверки
"пин принадлежит не тому периферийному модулю" (см. [GPIO.md §6](GPIO.md#6-constexpr-таблицы-пинов-и-защита-от-опечаток)).

Поведение зависит от сборки:

- **Debug** (`DEBUG` определён CMake'ом): `msg` сохраняется в `volatile _debug_trap_msg`
  (видно в Watch/Memory окне отладчика даже без стека вызовов — полезно, если оптимизация
  инлайнит/уничтожает стек). На F4/F7 при включённой трассировке и канале ITM №0
  строка с нулевым завершением выводится через **блокирующий SWO** посредством
  `ITM_SendChar()`. Затем выполняется `__BKPT(0)` и бесконечный цикл.
- **Release**: аргумент игнорируется, выполняется бесконечный цикл. Удаление
  строк зависит от оптимизации компилятора/линкера и функцией не гарантируется.

ITM/SWO настраивает отладчик. При выключенной трассировке или `nullptr` вывод
пропускается; на G0/L0 ITM-вывода нет. Включённый, но остановившийся канал ITM
может задержать функцию до BKPT. Этот путь намеренно блокирующий и не использует
SysTick: он применим до `System::Init()` и из прерывания. Обычный `SWOTrace()`
остаётся неблокирующим. `SetDebugOutput()` удалён; колбэка вывода и настройки
UART для `DebugTrap()` нет.

---

### 7. Bit-banding и SysStatus/SysInitStatus

```cpp
enum class SysStatus     : uint8_t { OK, Error, Busy, NotInit, Timeout };
enum class SysInitStatus : uint8_t { NotInit, InitOK, InitError = 0xFF };
```

Общий словарь результатов для всех драйверов — `SysStatus` для рантайм-операций (`Send()`,
`Receive()`, ...), `SysInitStatus` для однократных настроек (`SetUp()`, `Init()`). Не самая
детализированная схема (нет отдельного кода "неверный аргумент" vs "устройство не найдено" —
оба варианта чаще всего `Error`/`InitError`), но единая на весь проект, так что вызывающему коду
не нужно помнить разные enum'ы для разных периферий.

`BIT_BB`/`BB_RD`/`BB_WR` — низкоуровневые функции адресной арифметики bit-banding
(`(addr - PERIPH_BASE) * 32 + bit * 4 + PERIPH_BB_BASE`), на которых построены
`PIN::SetLevel_BB()`/`GetLevel_BB()`/`TogglePin_BB()` — подробное объяснение самой техники
bit-banding и почему она доступна только на F4 — в [GPIO.md §5](GPIO.md#5-bit-banding-только-f4);
здесь эти функции просто определены (F4-only, `#if defined(STM32F4)`), так как исторически это
общесистемная, а не GPIO-специфичная возможность ядра.

---

### 8. Примеры использования

#### Стандартная последовательность старта (как в main.cpp)

```cpp
int main() {
    System::Init();
    ClockSystem::InitCalcPLL(180'000'000, ClockSystem::PLL_ClockSource::HSE, 8'000'000);
    System::Enable_CYCCNT();   // требуется перед первым Delay_us()

    uint32_t tick = System::GetTick();
    for (;;) {
        if (System::GetTick() - tick > 500) {
            tick = System::GetTick();
            // раз в 500 мс...
        }
    }
}

extern "C" void NMI_Handler(void)       { while(1){} }
extern "C" void HardFault_Handler(void) { while(1){} }
```

#### Микросекундная задержка

```cpp
System::Enable_CYCCNT();     // один раз
System::Delay_us(50);        // 50 мкс, блокирующая
```

#### DebugTrap с выводом через SWO

```cpp
// Включить канал ITM №0 / SWO в отладчике (F4/F7, сборка DEBUG).
System::DebugTrap("Invalid peripheral pin"); // блокирующий вывод, брейкпоинт, останов
```

---

### 9. Угловые случаи

- **Порядок вызовов важен**: `System::Init()` →
  `ClockSystem::Init()`/`InitCalcPLL()` → `System::Enable_CYCCNT()` → `SetUp()` любых
  периферийных драйверов. Периферийный `SetUp()` до `ClockSystem::Init()` посчитает делители
  от HSI (16 МГц), а не от реальной частоты (см. §3).
- **`Enable_CYCCNT()`/`Delay_us()`/`SWOTrace()` недоступны на STM32L0/G0** — оба используют
  Cortex-M0/M0+, где нет блока DWT с `CYCCNT`. Вызов на этих целях — ошибка компиляции
  (функции просто не существуют в этой сборке), а не рантайм-сбой.
- **Большие запросы `Delay_ms()`** — ожидание вместе с компенсирующим тиком
  ограничено `0x7FFFFFFF` тиками. `Delay_ms(0xFFFFFFFF)` использует тот же предел.
- **`DebugTrap()` в release-сборке не сохраняет `msg` нигде** — строка может быть даже не
  включена в бинарник компилятором (если `msg` не используется больше нигде), в отличие от
  debug-сборки, где `_debug_trap_msg` всегда указывает на неё.
- **`TickIncrease()` вызывается только из `SysTick_Handler`** — вызов из любого другого места
  не является ошибкой (это просто инкремент переменной), но нарушит соответствие между
  `GetTick()` и реальным временем.

---

### См. также

- [RCC.md](RCC.md) — единственный писатель тактовых переменных, описанных в §3.
- [GPIO.md](GPIO.md) — `BIT_BB`/`BB_RD`/`BB_WR` (§7) используются `PIN::*_BB()`-методами.
- [IRQ_Registry.md](IRQ_Registry.md) — независимый механизм диспетчеризации периферийных IRQ;
  `SysTick_Handler`/`NMI_Handler`/`HardFault_Handler` и подобные исключения Cortex-M сюда не
  входят и настраиваются отдельно (см. `src/main.cpp`).
- [UART.md](UART.md), [TIM.md](TIM.md), [SPI.md](SPI.md), [RTC.md](RTC.md), [DMA.md](DMA.md),
  [Flash.md](Flash.md) — все читают `SysStatus`/`SysInitStatus` (§7) и `System::GetTick()` (§4)
  из этого модуля.
