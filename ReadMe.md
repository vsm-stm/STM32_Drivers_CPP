# Drivers — STM32 Peripheral Driver Library

## Constant initialization / Постоянная инициализация

Fixed peripheral bindings now use `constexpr` constructors and static/global
objects declared with `constinit`. The first argument is `_N::instance` from the
driver's defs header. Use named constexpr pins and DMA routes from `_N::DMA`;
G0 physical DMA channels are numeric `DMAx_Channely_BASE` values. There is no
separate Config object or template driver API. See [Initialization.md](Initialization.md).

Постоянные привязки проверяются при объявлении статического/глобального объекта
через `constinit`. Первый аргумент — `_N::instance`; пины и DMA должны быть
константами. Маршруты находятся в `_N::DMA`, на G0 физический канал задаётся через
`DMAx_Channely_BASE`. Отдельного Config и шаблонного API нет.
Подробнее: [Initialization.md](Initialization.md): порядок настройки, варианты
с DMA/без DMA, триггеры, каналы TIM, наследование и сообщения ошибок.

Документация существующих модулей оформлена по образцу [GPIO.md](GPIO.md):
English/Русский, оглавление для каждого языка, нумерованные разделы с описанием
работы, примеры использования, ограничения и ссылки на связанные модули.

`constinit` обязателен по контракту API. Каждая неверная привязка вызывает
`DebugTrap` с конкретной причиной: при `constinit` это ошибка компиляции, без него
при runtime-создании — останов программы. `__builtin_unreachable` для этих проверок
не используется. В Release DebugTrap определён inline без вывода сообщения.


*[English](#english) · [Русский](#русский)*

---

## English

A universal C++ library on top of CMSIS for the **STM32G0**, **STM32F4**, and **STM32F7** families.
Core (always built): System, RCC, GPIO, Flash, IRQ_Registry. Optional modules: UART, SPI, DMA, TIM, RTC, EXTI, ADC, DAC, I2C, MODBUS, CONTAINERS, PID, DSP, CRC.

The library is not a standalone CMake project: it is cloned and wired in by the [stm32_cmake_base](https://github.com/vsm-stm/stm32_cmake_base) platform (see "Integration and build").

Additionally, the **`DSP`** module: a subset of Arm's current CMSIS-DSP (fast math `arm_sin_f32`/`arm_cos_f32`/`arm_sqrt_q31`/`arm_atan2_*`, controllers `arm_pid_*`, shared tables). Not part of the repository — pulled in at configuration time (`FetchContent`, tag v1.16.2). Enabled the same way as the other optional modules — by adding `"DSP"` to the project's `"drivers"` list. Third-party licenses are in `THIRD_PARTY.md`.

The module provides the following API from CMSIS-DSP v1.16.2:

| Group | Functions | Types |
|---|---|---|
| Trigonometry | `arm_sin_*`, `arm_cos_*` | `f32`, `q15`, `q31` |
| Square root | `arm_sqrt_*` | `f32` (inline), `q15`, `q31` |
| Angle | `arm_atan2_*` | `f32`, `q15`, `q31` |
| Division | `arm_divide_*` | `q15`, `q31` |
| Vector logarithm | `arm_vlog_*` | `f32`, `f64`, `q15`, `q31` |
| Vector exponential | `arm_vexp_*` | `f32`, `f64` |
| Combined sine/cosine | `arm_sin_cos_*` | `f32`, `q31` |
| PID | `arm_pid_init_*`, `arm_pid_reset_*`, `arm_pid_*` (step, inline) | `f32`, `q15`, `q31` |
| Clarke/Park transforms | `arm_clarke_*`, `arm_inv_clarke_*`, `arm_park_*`, `arm_inv_park_*` (inline) | `f32`, `q31` |

Use `dsp/fast_math_functions.h` and `dsp/controller_functions.h` for these groups. The general `arm_math.h` also declares functions from groups whose sources are absent from this module: FFT, FIR/IIR filters, statistics and other groups are not linked; their declarations alone do not provide implementations. Float16 kernels are excluded. Shared tables are built once with the individual kernels; aggregate source files are excluded.

`arm_sin_f32`/`arm_cos_f32` take radians; `arm_sin_cos_f32` takes degrees. Initialize each CMSIS PID instance with `arm_pid_init_*(..., 1)` before its first step. Check the returned `arm_status` for square root, division and atan2. Parameter ranges and fixed-point formats are described in Arm's [FastMath header](https://github.com/ARM-software/CMSIS-DSP/blob/v1.16.2/Include/dsp/fast_math_functions.h) and [Controller header](https://github.com/ARM-software/CMSIS-DSP/blob/v1.16.2/Include/dsp/controller_functions.h).

The **`PID`** module — an in-house PID controller (`math/PID.*`): variable step `dT`, dead zone `e_lim`, symmetric integral-term clamp `max_ki_I`, and a helper `PID_get_quality` (relative peak deviation and accumulated time). Its C API can be called directly from C++.

Zero-initialize the controller state or call `zero_PID_I_D()` before use. Set `e` on each step; `dT` is in seconds. Inputs must be finite, `dT > 0`, and `e_lim`/`max_ki_I >= 0`; `dT == 0` returns zero without updating state. With `ki == 0`, `I` is held. Inside `|e| <= e_lim`, `I` is held, `D` is zero and `e_old` is updated; the proportional term remains active.

Reset `PID_get_quality` with `get=0` before a measurement and when changing the setpoint or mode. For `get=1`, `e` is a percentage for nonzero `u`, or the absolute error `y-u` for zero `u`; `t_time` sums time outside the ±5% corridor. For `get=2`, `e` is absolute and `t_time` sums every supplied `dt`. This is accumulated time, not settling time. `max_y` tracks the maximum response for `u >= 0` and the minimum for `u < 0`, starting from zero after reset; `sigma=(max_y-u)/u` is a ratio, and is zero at `u=0`.

## Driver API migration — 19.10.2026

USART/SPI/ADC/I2C and TIM channel descriptions accept DMA selections and own
their DMA objects. F4/F7 use named DMAReq with controller/stream in the name.
G0 takes only the physical DMA channel; the peripheral derives its request source.
Peripheral and DMA handlers are registered separately; IRQs do not wait for
hardware completion. SPI still requires its agreed main-loop Process().

UART_USE_ATOMIC, SPI_USE_ATOMIC, ADC_USE_ATOMIC, TIM_USE_ATOMIC,
I2C_USE_ATOMIC, DAC_USE_ATOMIC, DMA_USE_ATOMIC and EXTI_USE_ATOMIC are configured in their
respective headers. All default to 0; 1 selects ST ATOMIC_* control updates.
Rebuild drivers and application after changing them. Individual atomic RMW
does not serialize a full transfer or buffer handover. No CMake setting is added.

DMA GetIRQFlags() returns HT/TC/errors in one ISR access; public FLAG_HT,
FLAG_TC and FLAG_ERRORS decode it. FEIF remains excluded in direct mode.
I2C has fixed constructor DMA, inherited callbacks, one-time setup and DeInit;
bus recovery is application code. Timing is calculated at compile time: SetUp<clocks, I2C::SPEED::FAST>() takes the same ClockSystem::Config as RCC Init. APB1 comes from RCC preparation; Apply checks System::APB1BusClock before hardware setup. See [I2C.md](I2C.md).
These source changes have not been built or hardware-tested.

## Integration and build

Drivers are enabled with a single line in the `project.json` of a project built on `stm32_cmake_base`:

```json
"drivers": ["UART", "SPI", "DMA", "TIM"]
```

- `null` / `false` / no key — drivers are not built at all;
- `true` or `[]` — core only (System, RCC, GPIO, Flash, IRQ_Registry);
- `["UART", ...]` — core + the listed modules. A typo in a name is a configuration error with a list of valid names.

The first configuration clones the repository into `Drivers/`; the version is set by the tag (`GIT_TAG` in the platform's `CMakeLists.txt`). The `stm32_platform` target links against `stm32_drivers` (flags, CMSIS, chip defines). Chip-specific configs (`flash_config.h` — the sector map, `irq_registry_config.h` — the vector table) are generated by the library itself into `cmsis-core/drivers_gen/`.

The core is included via `#include <system.hpp>`, `<rcc.hpp>`, `<gpio.hpp>`, `<flash.hpp>`, `<irq_registry.hpp>`; optional modules — `<uart.hpp>`, `<spi.hpp>`, `<tim.hpp>`, etc. (an example is the commented-out block in the platform's `inc/main.hpp`).

### Modules and verification status

Only **compilation** has been checked (release, one module at a time; F446RE, F746ZG, G071RB). This does not confirm operation on real hardware.

| Module | Files | F4 (F446) | F7 (F746) | G0 (G071) |
|---|---|---|---|---|
| Core: System, RCC, GPIO, Flash, IRQ_Registry | `src/system.*`, `rcc.*`, `gpio.*`, `flash.*`, `irq_registry.*` | ok | ok | ok |
| UART, SPI | `src/uart.*`, `spi.*` | ok | not tested | ok |
| TIM | `src/tim.*` | common TIM API migrated; not rebuilt | common TIM API migrated; not rebuilt | common TIM API migrated; not rebuilt |
| DMA | `src/dma.*` | revised, not rebuilt | revised, not rebuilt | device-specific channel/IRQ table fixed; not rebuilt |
| RTC | `src/rtc.*` | implemented, not built | implemented, not built | updated, not built |
| EXTI | `src/exti.*` | GPIO 0..15 added; not built | GPIO 0..15 added; not built | GPIO 0..15 added; not built |
| ADC | `src/adc.*` | fixed constructor API, separate IRQ; not rebuilt | fixed constructor API, separate IRQ; not rebuilt | fixed constructor API, separate IRQ; not rebuilt |
| DAC | `src/dac.*` | revised, not rebuilt | revised, not rebuilt | revised, not rebuilt |
| I2C | `src/i2c.*` | API migrated; not rebuilt | API migrated; not rebuilt | API migrated; not rebuilt |
| MODBUS | `Interfaces/` | migrated with USART; not rebuilt | not tested | migrated with USART; not rebuilt |
| CONTAINERS | `Containers/` (headers) | included | — | — |
| PID | `math/PID.*` | ok | not tested | not tested |
| DSP | subset of CMSIS-DSP v1.16.2 | ok | not tested | not tested |
| CRC | `crc/crc32.*` (no tables, no HW peripheral — portable to any family) | ok | ok | ok |

RTC/ADC behavior and limitations are documented in [RTC.md](RTC.md) and
[ADC.md](ADC.md); implementation without a build does not establish hardware
correctness. DAC and DMA contracts are described in [DAC.md](DAC.md) and [DMA.md](DMA.md).

ADC configuration, status/callback API and cache handover are described in [ADC.md](ADC.md).
Conversion policy, DMA and TIM/source are fixed by the constructor. One Start()
uses the bound resources; Start(buffer, count) is required when DMA is present.
ADC and DMA have separate handlers with no hardware waits. HT is explicitly enabled
through IRQ_en; finite DMA always uses TC. See [ADC.md](ADC.md).
The new ADC API has not been rebuilt or tested on hardware.

Families other than F4 / F7 / G0 (including G4) are not supported by the drivers: set `"drivers": null` and write directly against the registers, or port the library (see "Adding a new MCU").

---

## Detailed module documentation

This file gives an overview and usage examples for each module. The full description of the internal workings (registers, timings, edge cases, line-by-line code walkthroughs) is in separate files:

- [System.md](System.md) — the connective infrastructure: `SysStatus`/`SysInitStatus`, clock variables, `GetTick()`/`Delay_us()`, `DebugTrap()`.
- [RCC.md](RCC.md) — the clock tree: HSI/HSE/PLL, bus dividers, Flash latency.
- [GPIO.md](GPIO.md) — `PIN`/`PinArray`: mode encoding, registers, bit-banding.
- [IRQ_Registry.md](IRQ_Registry.md) — the single interrupt dispatcher, registration with and without inheritance, cycle-cost overhead estimates.
- [DMA.md](DMA.md) — `DMA_Sx`: streams (F4/F7) and channels+DMAMUX (G0), request tables.
- [UART.md](UART.md) — `USART`: blocking/IRQ/DMA modes, reception on the IDLE line.
- [SPI.md](SPI.md) — `SPI`: Master/Slave roles, Software/Hardware NSS, DMA.
- [I2C.md](I2C.md) — master FSM, blocking/IRQ/DMA, callbacks, STOP and application recovery.
- [TIM.md](TIM.md) — common TIM, PWM/capture, encoder, pulse measurement, ITR and finite pulse series.
- [Modbus.md](Modbus.md) — RS-485 transport, CRC, frame formats and IDLE limits.
- [RTC.md](RTC.md) — `RTC_cl`: BCD time, WakeUp Timer, alarms, calibration.
- [EXTI.md](EXTI.md) — external GPIO IRQs, fixed PIN/edge, inherited callback and shared-vector limits.
- [DAC.md](DAC.md) — `DAC_Module`: direct 12-bit output, statuses and channel lifetime.
- [Flash.md](Flash.md) — `flash_base`/`flash_data`: unlocking, erasing, wear-leveling.

---

## Table of contents

1. [Architectural Principles](#1-architectural-principles)
2. [System](#2-system)
3. [RCC / ClockSystem](#3-rcc--clocksystem)
4. [GPIO / PIN](#4-gpio--pin)
5. [IRQ_Registry](#5-irq_registry)
6. [DMA](#6-dma)
7. [USART](#7-usart)
8. [SPI](#8-spi)
9. [TIM](#9-tim)
10. [RTC](#10-rtc)
11. [Flash](#11-flash)
12. [DAC](#12-dac)

---

## 1. Architectural Principles

### Peripheral tables (PeriphInfo)

Instead of long if-else chains like `if (USARTx == USART1) { RCC->APB2ENR |= ...; }`, each driver keeps a **static table** describing every instance of the peripheral:

```cpp
struct PeriphInfo {
    USART_TypeDef*      periph;   // pointer to the peripheral (USART1, USART2...)
    volatile uint32_t*  clk_reg;  // clock-enable register (APBxENR)
    uint32_t            clk_bit;  // bit in clk_reg
    uint32_t const*     bus_clk;  // pointer to the bus-frequency variable
    IRQn_Type           irq;      // interrupt vector number
};
static const PeriphInfo usart_table[];
```

When `SetUp()` is called, the driver scans the table, finds the right entry by the peripheral pointer, and automatically:
- enables the clock
- reads the current bus frequency
- knows the interrupt vector

This is the only place that needs updating when adding a new MCU.

### Compile-time pin validation

Pins for USART and SPI are selected from nested structures:

```cpp
constinit USART debug(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7);
```

Each pin from `USART::_1::TX::PB6` carries its owning peripheral base. A mismatched
pin rejects constinit construction at compilation through a specific DebugTrap
call. Without constinit, an invalid runtime construction stops in that trap.
The deprecated CMSIS-pointer constructor also retains runtime instance lookup.

### IRQ_Registry — a single interrupt dispatcher

All interrupts pass through a single dispatcher. The stub handlers in `irq_registry_config.h` call `IRQ_Registry::Dispatch(IRQn)`, which forwards the call to the registered `IIRQHandler` object.

Drivers register and unregister automatically when an interrupt is enabled/disabled through `IRQ_en()`.

---

## 2. System

**Files:** `system.hpp`, `system.cpp`
**Details:** [System.md](System.md)

The base module: FPU, Flash accelerator, SysTick, delays.

### Initialization

```cpp
System::Init();  // initializes core features and SysTick (1 kHz)
```

Must be called **first** in `main()`, before `ClockSystem::Init<config>()`.

Without `STM32_USE_BOOTLOADER`, assumes hardware-reset clock/power/latency state;
call once at the start of main, before changing clock settings.
The base defines `STM32_USE_BOOTLOADER` automatically when `bootloader` is
configured in project.json. With it, F4/F7/G0 select HSI 16 MHz and bus dividers /1;
G0 also restores VOS Range 1 before Flash 0 WS.
HSI readiness and the initial source switch wait without a timeout. Subsequent
waits use SysTick (VOS/Flash: 100 ms), so interrupts must be
enabled. PLL/HSE settings and RCC interrupts are retained.
Check the returned `SysInitStatus`. L0 retains its legacy 16 MHz HSI
assumption and does not restore the hardware clock configuration.

The base defines `STM32_STARTUP_ENABLES_FPU`, so `System::Init()` skips the FPU
setup already performed by startup. With another startup, omit this definition
unless it initializes FPU itself. On F7 the core caches remain enabled;
System provides common cache operations; peripheral drivers will call them
during buffer handover as their individual reviews are completed.
MPU region setup and external-memory access permissions are described in
[System.md](System.md#6-mpu-on-stm32f7).

### Working with time

```cpp
uint32_t t = System::GetTick();   // ms since startup
System::Delay_ms(500);            // blocking 500 ms delay

// Non-blocking timer:
uint32_t start = System::GetTick();
if ((System::GetTick() - start) > 1000) { /* one second has passed */ }
```

### Microsecond delays (F4/F7)

```cpp
System::Enable_CYCCNT();   // once at startup
System::Delay_us(10);      // 10 us delay via DWT
```

`Delay_ms()` requires a running SysTick that can interrupt the caller: it cannot
wait with ticks masked or from an IRQ with equal/higher priority. `GetTick()`
itself only reads the counter. The `Delay_ms()` wait, including its compensating
tick, is capped at `0x7FFFFFFF` ticks (about 24.9 days at 1 kHz).
`Delay_us()` uses 64-bit arithmetic and clamps
to `0x7FFFFFFF` core cycles; its maximum duration depends on the current clock.
On **STM32G0/L0** the DWT is unavailable — `Delay_us` does not exist.

On F4/F7, `SWOTrace()` is nonblocking and returns the actual byte count accepted
by ITM. In DEBUG, `DebugTrap()` saves the diagnostic pointer, writes through
blocking SWO when enabled, executes BKPT and halts. Configure SWO in the debugger.
`SetDebugOutput()` has been removed. Release only halts; string elimination
depends on compiler/linker optimization.

### Non-cacheable DMA areas

F7 projects can reserve non-cacheable DMA buffers through the base's
`dma_regions` setting (`name`, `size`, `memory`: SRAM1/SRAM2, `mpu_region`).
Addresses are automatic; startup zeros the buffers, and `System::Init()` applies
their MPU attributes before enabling D-cache. Place buffers in `.dma.<name>`.
For the configuration, limits and initialization order, see [System.md](System.md).

Shared cache operations are `System::CacheClean`, `CacheInvalidate` and
`CacheCleanInvalidate` (address + byte count). F7 skips ranges wholly inside
configured non-cacheable DMA areas; F4/G0/L0 use inline no-ops. The peripheral
driver decides when to call them; direct DMA users do so themselves. RX buffers
must own every affected 32-byte cache line. See [System.md](System.md).

### Bus frequency variables

After `ClockSystem::Init<config>()`, these are available:

```cpp
System::SystemCoreClock  // core frequency (AHB)
System::APB1BusClock     // APB1 frequency
System::APB2BusClock     // APB2 frequency
System::TIMxAPB1Clock    // frequency for timers on APB1
System::TIMxAPB2Clock    // frequency for timers on APB2
```

---

## 3. RCC / ClockSystem

**Files:** `rcc.hpp`, `rcc.cpp`, `rcc_defs.hpp`
**Details:** [RCC.md](RCC.md)

One clock setup per hardware reset. Configuration and calculations are constant
expressions; `Init<config>()` validates them at compile time. Call System::Init()
first, before peripheral setup.

### HSI without PLL

```cpp
constexpr ClockSystem::Config clocks{};
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
    System::DebugTrap("Clock init");
```

### Exact automatic PLL selection

Example: G0, SYSCLK 64 MHz from HSI:

```cpp
constexpr auto clocks = ClockSystem::CalcPLL(
    64 * MHz, ClockSystem::PLL_ClockSource::HSI);
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
    System::DebugTrap("Clock init");
```

For HSE supply its nominal frequency as the third argument. The fourth argument
is Q (default 2). CalcPLL chooses exact SYSCLK, AHB /1 and legal APB dividers.
Among exact solutions it prefers the highest legal PLL input frequency, then
the smallest P/R. No exact solution produces a build error at Init<clocks>().

### Manual configuration

F407, HSE 8 MHz, SYSCLK/HCLK 168 MHz, APB1 42 MHz, APB2 84 MHz:

```cpp
constexpr ClockSystem::Config clocks{
    .ClkSrc = ClockSystem::SystemClockSource::PLL,
    .HSE_Clk = 8 * MHz,
    .BusDiv = {ClockSystem::AHB_Divider::DIV1,
               ClockSystem::APB_Divider::DIV4,
               ClockSystem::APB_Divider::DIV2},
    .PLLCfgr = {ClockSystem::PLL_ClockSource::HSE, 4, 168, 2, 7, 2},
};
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
    System::DebugTrap("Clock init");
```

Supported: all F4/F7 selectors in STM32-base_files and G0. G0 also supports
LSI/LSE. VDD is fixed at 3.0 V (same timing band at 3.3 V); power scaling,
overdrive and latency follow the device/HCLK limits. Runtime errors are
hardware timeouts or a repeated attempt. A failed attempt also requires reset.

SysTick uses a whole number of HCLK cycles. At LSE 32768 Hz and AHB /1 it runs
at 1024 ticks/s; Delay_ms and timeouts inherit that quantization error. See
RCC.md for limits, ordering and migration from Init(...)/InitCalcPLL(...).

---

## 4. GPIO / PIN

**Files:** `gpio.hpp`, `gpio.cpp`
**Details:** [GPIO.md](GPIO.md)

Use PIN(GPIOA_BASE, 5) for ordinary GPIO. The pointer constructor binds the supplied
port/pin/AF without runtime validation. Compile-time tables use GPIOx_BASE with
the consteval constructor, which checks port, pin number and stored AF.
Port/number/stored AF are private const; periph_base is read-only. Copy construction
is supported, but an existing PIN cannot be rebound by assignment. AF override
remains in SetUp. ADC now uses immutable, compile-time checked PIN bindings with
names such as PA0_IN0. TIM pin descriptors now have compile-time constructor checks; mode-specific revisions remain pending. Direct GPIO pointer construction stays supported.

### Creating and configuring a pin

```cpp
PIN led(GPIOB, 8);                        // port + pin number
PIN btn(GPIOA, 0);

led.SetUp(PIN::TYPE::OUTPUT_PushPull);    // push-pull output
btn.SetUp(PIN::TYPE::INPUT_PullUp);       // input with pull-up

led.SetLevel(true);      // HIGH
led.SetLevel(false);     // LOW
led.TogglePin();         // toggle

bool state = btn.GetLevel();   // read the state
```

### All pin types

| Constant | Description |
|---|---|
| `INPUT_NO_Pull` | Input without pull |
| `INPUT_PullUp` | Input pulled up to VCC |
| `INPUT_PullDown` | Input pulled down to GND |
| `OUTPUT_PushPull` | Push-pull output |
| `OUTPUT_OD` | Open-drain output |
| `OUTPUT_OD_PulUp` | Open-drain + pull-up |
| `AF_PushPull` | Alternate function, push-pull |
| `AF_OD` | Alternate function, open-drain |
| `ANALOG` | Analog mode (for ADC/DAC) |

### Output speed

```cpp
led.SetUp(PIN::TYPE::OUTPUT_PushPull, PIN::OUTPUT_SPEED::High);
```

OUTPUT setup defaults to LOW, written through BSRR before enabling the mode.
To start HIGH:

```cpp
led.SetUp(PIN::TYPE::OUTPUT_PushPull, PIN::OUTPUT_SPEED::Low, PIN::LVL::HIGH);
```

`TogglePin()` uses ODR read-modify-write. Do not change outputs on the same port
from IRQ during that operation. The port index in SetUp divides the address
offset by the constant stride GPIOB_BASE - GPIOA_BASE. On the checked GCC 13.3.1
builds for F429/F767/G031 this produces the same code as an explicit shift.

### Alternate function

```cpp
PIN sck(GPIOA, 5);
sck.SetUp(PIN::TYPE::AF_PushPull, 5);   // AF5 = SPI1 on STM32F429
```

USART/SPI use the stored AF when configuring their pins. TIM descriptor construction is compile-time checked; mode-specific revisions remain pending.

### Assignment and conversion operator

```cpp
led = true;       // SetLevel(true)
led = false;      // SetLevel(false)
bool b = (bool)led;   // GetLevel()
```

### IsValid()

A pin created via `PIN{}` (the default constructor) returns `IsValid() == false`. Drivers use this as a "channel not connected" marker:

```cpp
constinit SPI spi(SPI::_1::instance, SPI::Direction::TX, sck, mosi, PIN{}, PIN{});   // MISO and SS are not used
```

### PinArray — an array of pins

```cpp
PIN segments[] = {
    PIN(GPIOA_BASE, 0), PIN(GPIOA_BASE, 1), PIN(GPIOA_BASE, 2), PIN(GPIOA_BASE, 3)
};
PinArray<4> seg_pins(segments);

seg_pins.SetUpAll(PIN::TYPE::OUTPUT_PushPull);
seg_pins.SetLevelAll(0b1010);    // set by bits
uint32_t state = seg_pins.GetLevelAll();  // read all at once
```

`SetUpAll()` configures every element and returns the first initialization error unchanged.

---

## 5. IRQ_Registry

**Files:** `irq_registry.hpp`, `irq_registry.cpp`, `irq_registry_settings.hpp`; generated `irq_registry_config.h`.
**Details:** [IRQ_Registry.md](IRQ_Registry.md)

Generated ISR stubs call Dispatch(irqn), which invokes registered IIRQHandler
objects or static adapters for plain functions. A peripheral uses one handler
object for its sources; separate peripherals can share an IRQ line.

Register returns SysInitStatus::InitOK or InitError. Invalid/null input,
duplicate object/function on the same IRQ and capacity exhaustion return
InitError. A different ctx does not permit registering the same function twice.
Change a registration through cancellation followed by Register.

```cpp
if (IRQ_Registry::Register(TIM3_IRQn, &handler) == SysInitStatus::InitOK)
    NVIC_EnableIRQ(TIM3_IRQn);

// Later, from main, with this owner's sources disabled:
NVIC_DisableIRQ(TIM3_IRQn);
IRQ_Registry::Unregister(TIM3_IRQn, &handler);
if (IRQ_Registry::HasHandlers(TIM3_IRQn)) NVIC_EnableIRQ(TIM3_IRQn);
```

Functions use Register(irqn, fn, ctx); ctx is an opaque data pointer passed to
fn(ctx). Cancel with Unregister(irqn, fn); its adapter becomes reusable.
The existing Unregister(irqn) overload explicitly clears the entire row.

Register/Unregister are forbidden from HandleIRQ and its callbacks. Modify
the registry from main with the affected IRQ masked. Callback completion may
disable the peripheral source and keep its registration until cleanup from main.
Existing peripheral call sites are updated during their separate module reviews.

### Handler capacity per IRQ

Settings are edited in `Drivers/src/irq_registry_settings.hpp`, shared by all
source files. IRQ_MAX_SHARED defaults to 3 for F4/F7/G0; for five handlers:

```cpp
#define IRQ_MAX_SHARED 5
```

Each extra slot adds IRQ_TABLE_SIZE * 4 bytes on these MCUs. The separate
IRQ_MAX_FUNCTION_HANDLERS pool defaults to 8. Rebuild the library and images
after changing settings; see the detailed documentation for template updates.

---

## 6. DMA

**Files:** `dma.hpp`, `dma.cpp`, `dma_requests.hpp`
**Details:** [DMA.md](DMA.md)

Supports F4/F7 (streams, `DMA_Stream_TypeDef`) and G0 (channels, `DMA_Channel_TypeDef`). The API is identical for both families.

`SetUp()` accepts zero count/addresses for preparation without starting DMA.
It rejects repeated setup of an owned channel, invalid register fields,
misaligned addresses and counts above 65535. `SetCount()` accepts 1..65535
elements. Address/count setters return `NotInit` before setup, `Busy` while
EN is set, and `Error` for invalid arguments; check their results.

`Stream_EN(DISABLE)` waits for EN=0 without a timeout. `Release()` stops and
resets the owned resource, then releases it; constructor selection is retained.
Before release, disable the peripheral's DMA requests and unregister its own
handler. Shared NVIC lines and controller clocks remain enabled. SetUp/Release
are used from the main loop. Each configured object belongs to one consumer.

DMA descriptors are stored in the `static const` table `dma_streams` in Flash.
Only the separate `dma_used` ownership flags occupy RAM.

Cache operations live in `System`; the consuming peripheral driver chooses
their timing and range. Integration into the remaining consumers is pending.

### Constructor (STM32G0)

On G0, DMAMUX1 lets you connect any channel to any peripheral. The user picks the channel; the request comes from the peripheral’s `_N::DMA` definitions:

```cpp
// Option 1: channel + request from the table (recommended)
DMA_Sx dma_tx(DMA1_Channel4_BASE, USART::_1::DMA::TX);
DMA_Sx dma_rx(DMA1_Channel5_BASE, USART::_1::DMA::RX);

// Option 2: channel + raw DMAMUX1 request ID
DMA_Sx dma(DMA1_Channel3_BASE, 51u); // USART1_TX

// Option 3: channel without a request (ID supplied later via StreamSettings)
DMA_Sx dma(DMA1_Channel3_BASE);
```

### Constructor (STM32F4/F7)

On F4, the stream and CHSEL are hard-wired to the peripheral:

```cpp
DMA_Sx dma_tx(USART::_1::DMA::TX_DMA2_Stream7); // CH4
DMA_Sx dma_rx(USART::_1::DMA::RX_DMA2_Stream5); // CH4
```

### Configuration via StreamSettings

```cpp
DMA_Sx::StreamSettings cfg;
cfg.direction   = DMA_Sx::DIR::To_Per;    // memory → peripheral
cfg.data_size   = DMA_Sx::SIZE::Byte;     // element width
cfg.minc        = true;                   // increment the memory address
cfg.pinc        = false;                  // peripheral address is fixed
cfg.per_address = reinterpret_cast<uint32_t>(&USARTx->TDR);
cfg.count       = 0;                      // set before each transfer

dma.SetUp(cfg);
```

### Manual transfer start

```cpp
dma.ClearFlags();
dma.SetMemAddr(reinterpret_cast<uint32_t>(buf));
dma.SetCount(len);
dma.Stream_EN(ENABLE);
```

### TC interrupt

```cpp
IRQ_Registry::Register(dma.GetIRQn(), &my_handler);
NVIC_EnableIRQ(dma.GetIRQn());
dma.Enable_IRQ(DMA_Sx::IRQ::TC);
```

In the handler, for a non-circular transfer:

```cpp
void HandleIRQ() override {
    if (_dma.GetTC_Flag()) {
        _dma.RequestStop(); // TC has already confirmed completion.
        _dma.ClearFlags();
        __DMB();
        // transfer complete
    }
}
```

### Directions

| `DIR` | Value |
|---|---|
| `From_Per` | Peripheral → Memory |
| `To_Per` | Memory → Peripheral |
| `Mem_To_Mem` | Memory → Memory on G0; F4/F7 return `InitError` |

### Element width

| `SIZE` | Size |
|---|---|
| `Byte` | 8 bits |
| `Half_Word` | 16 bits |
| `Word` | 32 bits |

### Important: shared IRQs on G0

| Channels | Vector |
|---|---|
| CH1 | `DMA1_Channel1_IRQn` (exclusive) |
| CH2, CH3 | `DMA1_Channel2_3_IRQn` (shared) |
| CH4, CH5 | `DMA1_Ch4_5_DMAMUX1_OVR_IRQn` (shared) |

The last row applies to five-channel models. Seven-channel DMA1 models use
`DMA1_Ch4_7_DMAMUX1_OVR_IRQn`; models with DMA2 use
`DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX1_OVR_IRQn` for DMA1 CH4–7 and DMA2 CH1–5.
The driver selects the channel count and vector from the device header.

Two channels on one vector is not a problem: `IRQ_Registry` supports a chain of handlers.

---

## 7. USART

**Files:** `uart.hpp`, `uart.cpp`, `uart_defs.hpp`
**Details:** [UART.md](UART.md)

UART uses oversampling x16, validates BRR, and returns SysStatus from SetBaud().
F7/G0 accept a final constructor argument `USART::ClockSource`:
PCLK (default), SYSCLK, HSI or LSE, subject to instance availability. The source
is immutable. The caller enables HSI/LSE; SetUp only checks enabled/ready state.
F4 retains PCLK. See [UART.md](UART.md) for the clock selection example.
Zero transfer length is Error; IRQ/DMA lengths are 1..65535 bytes.
Before SetBaud(), the caller waits for physical TX completion (TC/OnTC); Busy
checks software operations only. No additional TX tracking field is used.
DMA TX/RX requests follow the pins in the constructor; ClockSource follows DMA.
USART embeds the DMA objects. With constinit, its constructor checks request
ownership/direction during compilation; SetUp() acquires/configures the resources. USART and both
DMA directions register separate handlers. On F4/F7, IDLE requests a stop and
DMA RX TC finishes count/cache/ready without waiting in IRQ or a Process() call.
On G0 IDLE disables DMAR/DMA without waiting and finishes in USART IRQ; the last
DMA request must already be serviced. Failed packets are discarded without
reading their DMA length. See UART.md for this limitation. F7 cache helpers
skip configured non-cacheable DMA regions.
GetDataReceivedCount() no longer clears ready/count: call ClearDataReceivedFlag()
after consuming the packet. Errors stop the affected operation and call OnError(mask).
IRQ FIFO processing is bounded per direction: eight words on G0 with FIFO enabled,
one byte otherwise. IDLE owns its bounded RX tail. See UART.md for examples.
Modbus adaptation to the explicit flag reset is deferred to its module review.

### Basic usage

```cpp
constinit USART debug(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7);

debug.SetUp();
debug.Send((uint8_t*)"Hello\r\n", 7);

uint8_t buf[16];
debug.Receive(buf, sizeof(buf), 500);  // 500 ms timeout
```

### TX only (no RX)

```cpp
constinit USART log(USART::_2::instance, 115200, USART::_2::TX::PA2, PIN{});
log.SetUp();
log.Send((uint8_t*)"OK\r\n", 4);
```

### DMA transfer

```cpp
constinit USART uart(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7,
           DMA1_Channel4_BASE,
           DMA1_Channel5_BASE);

uart.SetUp(); // Check InitOK; acquires/configures the internally stored DMA.

uart.Send_DMA(tx_buf, sizeof(tx_buf));
uart.Receive_DMA(rx_buf, sizeof(rx_buf));  // completes on IDLE or when the buffer fills
```

### Interrupts

```cpp
uart.IRQ_en(USART::IRQ::RXNE, ENABLE);   // enable the RXNE interrupt
uart.IRQ_en(USART::IRQ::TXE, DISABLE);   // disable the TXE interrupt
```

`SetUp()` registers the UART IRQ once. `IRQ_en()` changes only peripheral sources,
including from callbacks. `DeInit()` removes only this object's registrations; shared
handlers remain registered.

### Custom interrupt handling (inheritance)

```cpp
class MyUART : public USART {
    using USART::USART;  // inherit the constructor
    void OnRxByte(uint8_t b) override { USART::OnRxByte(b); ring_buf.push(b); }
    void OnIdle() override { USART::OnIdle(); process_packet(); }
};

constinit MyUART uart(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7);
```

### Changing baud between transfers

```cpp
uart.SetBaud(9600);
```

---

## 8. SPI

**Files:** `spi.hpp`, `spi.cpp`, `spi_defs.hpp`
**Details:** [SPI.md](SPI.md)

SPI1–SPI6 on F4/F7 and SPI1–SPI3 on G0 are included when present in CMSIS.
Direction::TX/RX/TXRX and optional DMA are fixed in the constructor; the settings
structure stays in SetUp. TX uses transmit-only hardware and one TX DMA. RX master
uses full duplex: one RX DMA plus TX IRQ for 0xFF/0xFFFF dummy clocks.

### Basic usage (blocking mode)

```cpp
constinit SPI spi(SPI::_1::instance, SPI::Direction::TXRX, SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7,
        SPI::_1::MISO::PA6, PIN(GPIOB_BASE, 0)); // Optional automatic software CS
if (spi.SetUp(SPI::Master_sel::Master, SPI::NSS_ctrl::Software,
              SPI::Data_frame_format::Byte, SPI::Frame_Format::MSB,
              SPI::cPolPha::None, SPI::BaudRate::DIV4) == SysInitStatus::InitOK) {
    uint8_t tx[] = {0x12, 0x34};
    SysStatus result = spi.Send(tx, sizeof(tx)); // CS released on success/error
    // Inspect result.
}
```

For multiple devices omit SS in the constructor and control separate CS pins
in the application. Hard NSS requires its matching pin; slave uses hardware NSS.

### 16-bit frames

```cpp
// After successful SetUp(Master, Software, Half_Word):
uint16_t tx[] = {0x1234, 0xABCD}, rx[2];
SysStatus result = spi.Send_Receive(tx, rx, 2); // len counts frames
```

### DMA transfer and completion

```cpp
// F4/F7:
constexpr SPI::DMAReq dma_tx = SPI::_1::DMA::TX_DMA2_Stream3;
constexpr SPI::DMAReq dma_rx = SPI::_1::DMA::RX_DMA2_Stream0;
// G0: constexpr SPI::DMAReq dma_tx = DMA1_Channel2_BASE;
//     constexpr SPI::DMAReq dma_rx = DMA1_Channel3_BASE;
constinit SPI spi(SPI::_1::instance, SPI::Direction::TXRX, SPI::_1::SCK::PA5,
        SPI::_1::MOSI::PA7, SPI::_1::MISO::PA6, PIN{}, dma_tx, dma_rx);
if (spi.SetUp(SPI::Master_sel::Master, SPI::NSS_ctrl::Software) == SysInitStatus::InitOK) {
    // Start spi.SendReceive_DMA(tx_buffer, rx_buffer, frame_count); check OK.
}
// In main while a started IRQ/DMA operation is active:
SysStatus state = spi.Process(); // Busy until physical SPI completion
// After OK, RX is available and GetDataReceivedCount() returns its frame count.
```

F7 cache handover is performed through System; configured non-cacheable DMA
regions are skipped. CPU must not access active RX buffers or change TX.
OnComplete()/OnError(mask) run after cleanup in Process. IRQ_en changes CR2
sources; SetUp owns IRQ registration and NVIC.

In slave, len is capacity. The application configures NSS rising-edge EXTI and
calls NssReleased() from its handler. Process completes the packet in main.
Extra RX is discarded with IsTruncated(); after TX ends, dummy values are sent.
SPI, DMA TX and DMA RX use separate handlers; no IRQ waits for hardware. On G0,
the last slave RX DMA request must be serviced before the NSS stop; Process reads
the stopped length. An error discards the packet; see SPI.md.
See SPI.md for interrupt priorities, servicing limits and rearm requirements.

### Frequency divider (BaudRate)

| Constant | Divider |
|---|---|
| `DIV2` | PCLK / 2 |
| `DIV4` | PCLK / 4 |
| `DIV8` | PCLK / 8 |
| ... | ... |
| `DIV256` | PCLK / 256 |

### Polarity/phase (cPolPha)

| Constant | CPOL | CPHA | SPI Mode |
|---|---|---|---|
| `None` | 0 | 0 | Mode 0 |
| `cPha` | 0 | 1 | Mode 1 |
| `cPol` | 1 | 0 | Mode 2 |
| `cPolPha` | 1 | 1 | Mode 3 |

---

## 9. TIM

**Files:** `tim.hpp`, `tim.cpp`, `tim_defs.hpp`
**Details:** [TIM.md](TIM.md)

One common TIM owns the base and independently configured channels. Pins and
optional DMA are fixed by up to four constructor descriptions `{pin, dma}`;
there are no TIM_PWM/PeriodicIRQ/InputCapture classes, Output objects or AttachDMA.
Composite modes remain archived comments pending separate review.

### Base, PWM and compare

```cpp
void Tick();
void HalfTick();
constinit TIM timer(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7});
// In main, check every setup result before Start:
if (timer.SetUp(1000, &Tick, 1000) == SysInitStatus::InitOK
    && timer.SetUpPWM(TIM::Channel::CH1, 250) == SysInitStatus::InitOK
    && timer.SetUpPWM(TIM::Channel::CH2, 500) == SysInitStatus::InitOK
    && timer.SetCompareIRQ(TIM::Channel::CH3, 750, &HalfTick) == SysInitStatus::InitOK) {
    timer.Start();
}
timer.SetDuty(TIM::Channel::CH1, 75); // Check SysStatus.
```

SetUp keeps `(frequency, callback, period)`; frequency means Update/PWM Hz,
period means CNT ticks (ARR=period-1), zero selects the period automatically.
Repeated setup requires DeInit. All channels share PSC/ARR. Counter/capture
operation requires an explicit period. Read GetCounterClock for actual tick Hz.

PWM1 uses CCR preload. Period is limited to 65535 ticks for 16-bit PWM, leaving
CCR=period available for exact 100%; zero gives 0%. No forced-active mode switch.
SetDuty clamps percent to 100, SetCCR accepts raw ticks. Neither overwrites an
active DMA channel. Start remains explicit.

### Capture and hardware trigger

```cpp
void Edge();
constinit TIM input(TIM::_3::instance, {TIM::_3::CH1::PA6});
// In main; target CNT clock: 100 Hz * 10000 ticks = 1 MHz.
if (input.SetUp(100, nullptr, 10000) == SysInitStatus::InitOK
    && input.SetUpCapture(TIM::Channel::CH1, &Edge) == SysInitStatus::InitOK) {
    input.Start();
}
// Edge reads input.GetCapture(TIM::Channel::CH1).
```

Capture is direct rising-edge TIx. GetCapture always reads CCR directly;
the optional callback only notifies. Wraps and overcapture belong to the app.
SetCompareIRQ with a null callback configures a pin-free hardware event without
IRQ. SetUpTrigger(Trigger::Update/ComparePulse/OCxRef) selects TRGO without IRQ;
ADC receives TIM* and its source in the constructor and starts TIM after arming
ADC/DMA. The caller configures the timer before ADC SetUp; see [ADC.md](ADC.md).

### Independent DMA and WS2812B

```cpp
// G0; F4/F7 use TIM::_1::DMA::CH1_DMA2_Stream1 instead.
constexpr TIM::DMAReq dma = DMA1_Channel3_BASE;
constinit TIM ws(TIM::_1::instance, {TIM::_1::CH1::PA8, dma});
WS2812B_Strip<8> strip(52, 26);
strip.SetAll(RGB::Red());
// Example: CNT 64 MHz, period 80 ticks => 800 kHz, ARR=79.
if (ws.SetUp(800000, nullptr, 80) == SysInitStatus::InitOK
    && ws.SetUpPWM(TIM::Channel::CH1) == SysInitStatus::InitOK
    && ws.SendDMA(TIM::Channel::CH1, strip.Data(), strip.Len()) == SysStatus::OK) {
    ws.Start();
}
// In main: keep strip unchanged while ws.IsDMABusy(TIM::Channel::CH1).
```

SetUpPWM acquires the constructor DMA. Each channel uses TIMx_CHx with CCDS=1
and a separate CCxDE gate. DMA has one handler per channel; timer IRQ never scans
DMA flags. uint16_t/Half_Word serves 16-bit timers, uint32_t/Word serves 32-bit
TIM2/TIM5. F7 source clean uses System and skips configured non-cacheable areas.

TC releases the buffer; the final CCR preload/PWM period may still be pending.
WS2812B data includes its LOW reset trailer. Choose CCR/reset timing for actual
clocks and LED requirements. Errors stop the affected DMA and invoke its bound
callback; GetDMAStatus reports OK/Busy/Error. Other channels continue.

The caller masks this object's affected timer/DMA IRQs around main-side DIER
updates (including SendDMA and IRQ_en) whenever handlers can run. The object's
handlers must not preempt one another. No IrqLock/PRIMASK changes or hardware
waiting occur in IRQ. DeInit is main-only and releases acquired DMA/registrations.
See TIM.md for hardware route conflicts and limitations.

Additional modes are functions of TIM: SetUpEncoder reserves the timer for
CH1/CH2 and Count reads raw CNT. SetUpPulseMeasure reserves CH1/CH2 and SMCR;
GetCapture reads the selected input's period and its paired channel's HIGH width.
SetUpSlave selects a normal TRGO/ITR connection; SetUpOnePulse fixes OPM.
Finite series have separate APIs: SetUpPulseBurst/RunPulses uses hardware RCR+OPM;
SetUpCounter/StopAfter/StopAt uses a second TIM and stops the source from IRQ.
The latter can emit extra/partial pulses due to IRQ latency. SetUpQuadrature uses
the constructor-bound second TIM with PSC=1; GenPulse(N) starts a signed number
of full A/B cycles. Stop stops both timers, DeInit releases both. Its stop is
also IRQ-based, without per-cycle software counting. Callbacks are optional;
initialization and channel roles are fixed until DeInit. ETR is deferred.

The new API has not been built or hardware-tested in this migration.

---

## 10. RTC

**Files:** src/rtc.hpp, src/rtc.cpp. **Details:** [RTC.md](RTC.md).

RTC_cl is an object for F4/F7/G0. Override OnWakeup, OnAlarmA, OnAlarmB and
OnTimestamp(const Timestamp&) in a subclass. Configure in main after System/RCC;
IRQ callbacks do not wait or change registrations.

~~~cpp
class Clock : public RTC_cl {
protected:
    void OnWakeup() override { /* notify main */ }
};
static Clock clock;
if (clock.SetUp(RTC_cl::CLK_Source::LSE, 127, 255) == SysInitStatus::InitOK) {
    // Set these only when the application intends to change the calendar:
    // clock.SetTime({14, 25, 0});
    // clock.SetDate({22, 5, 26}, RTC_cl::WeekDay::Friday);
    clock.EnableWakeUpTimer(1); // Two seconds with 1 Hz calendar.
}
RTC_cl::RtcTime time;
RTC_cl::RtcDate date;
clock.GetDateTime(time, date); // TR then DR: coherent pair, shadow lock released.
~~~

Same retained source preserves the calendar and dividers, then synchronizes RSF.
A different selected source **resets the backup domain and loses time/backup
registers** before applying the new source. Repeat setup is an error until DeInit.
LSE timeout is 5000 ms; other flag waits are 1000 ms using System::WaitBits.
Calibration returns Busy immediately while RECALPF is set.

Default 127/255 divides 32768 Hz to 1 Hz. For nominal LSI 32000 Hz pass 127/249;
old default arguments remain LSI,127,255 and need explicit adjustment for LSI.
HSE and its divider are caller prepared (G0 uses HSE/32).

Time/date setters validate decimal or BCD fields and return SysStatus. Alarm A/B
use AlarmConfig with Care/Ignore fields; day means day-of-month. MASKSS=0 ignores
subseconds. WUT clock codes are /16,/8,/4,/2,ck_spre and 17-bit ck_spre (110).
EnableTimestamp configures the edge and reads captured data before clearing
TSF; hardware timestamp has no year. Application prepares RTC_TS/CALIB pins.

DeInit stops owned events and releases only this object's IRQ registrations,
preserving shared TAMP handlers and the running calendar. Configuration, examples,
calibration and migration are described in RTC.md. These changes have not been
built or tested on hardware in this pass.

---

## 11. Flash

**Files:** `flash.hpp`, `flash.cpp`

**Details:** [Flash.md](Flash.md)

### flash_base — low-level access

F4/F7/G0 are supported. `init()` checks the generated bank map against the
hardware mode and rejects bank swap; it does not change option bytes.
`erase_sector()` uses a map index. `mass_erase()` takes 1/2/3 for bank 1/2/both.
GetFlashSector(index) provides geometry: uniform pages use bank constants and arithmetic, irregular sectors use a table. Config format 4 requires the matching flash_config.h.in template; see [Flash.md](Flash.md).

```cpp
if (flash_base::init() != SysInitStatus::InitOK) return;
if (flash_base::enable_access() != SysStatus::OK) return;

// Use a sector reserved by the project, not one containing code.
if (flash_base::erase_sector(settings_sector) != SysStatus::OK) return;
const uint32_t address = GetFlashSector(settings_sector).address;
const uint32_t values[] = {0x12345678, 0xDEADBEEF};
const SysStatus written = flash_base::write(address, values, 2);
if (written != SysStatus::OK) return;
const uint32_t value = flash_base::read<uint32_t>(address);
```

Unlock/write/erase return `SysStatus`. Call from the main loop with SysTick
running, outside interrupts and critical sections. Timeouts per hardware
operation: **50 s on F4/F7, 1 s on G0**. Fetching from the busy bank can stall
SysTick, so these do not guarantee a real-time deadline.

G0 writes 64 bits with an 8-byte aligned destination; source alignment is not
required, and a partial last block is padded with FF. F4/F7 at 3–3.3 V use
8/16/32-bit writes; 64-bit input is split into two 32-bit operations without VPP.
Modified Flash cache contents are invalidated; SRAM D-cache is not globally cleared.
Check `init()` once before direct operations. Direct `write()` assumes the caller
has validated the source, size/rounding overflow, destination range and alignment.
Hardware width is selected at compile time; log bounds are checked in `SetUp()`.

### flash_data — storing a structure in one sector

```cpp
uint8_t settings_buf[32]{};
flash_data settings(settings_sector, settings_buf, sizeof(settings_buf));
if (settings.SetUp() != SysInitStatus::InitOK) return;
const auto loaded = settings.read_data();
// Update settings_buf, then check the save result.
const SysStatus saved = settings.write_data();
if (saved != SysStatus::OK) {
    const uint32_t flags = FLASH->SR;
    (void)flags; // Inspect before starting another Flash operation.
}
```

Each record is written as **DATA_WRITING → payload → DATA_OK**. Only after the
new commit succeeds is the previous ready record marked invalid. Scanning selects
the last ready record and skips incomplete/invalid slots. A full sector is erased,
so power loss during erase/rewrite can lose the last saved version.

F4/F7 keep a one-byte header: `FF → 7F → 0F`, then `00` for invalidation.
G0 uses a **16-byte header**: the first word goes from all FF to
`00000000FFFFFFFF` to full zero; the second remains all FF until invalidation,
when it is programmed with `FFFFFFFF00000000`. Its invalidation marker takes priority.
All values are defined once in `Data_Status`; programming and scanning use that enum.
G0 enum values have changed; compare named enumerators rather than numeric constants.
`DATA_CORRUPT` is removed; incomplete/invalid records use `DATA_NOT_VALID`.
If invalidating the old record fails, `write_data()` returns the error, but the
new committed record remains selected.

The old 8-byte G0 header is incompatible: use an erased log sector before testing
the new format. Hardware verification is performed by the user.
See [Flash.md](Flash.md) for the format, timeout and ECC/NMI limitations.

---

## 12. DAC

**Files:** `dac.hpp`, `dac.cpp`
**Details:** [DAC.md](DAC.md)

Enable `"drivers": ["DAC"]` in project.json. CH1 is PA4; CH2 is PA5 where supported.

```cpp
constinit DAC_Module output(DAC_Module::_1::instance, DAC_Module::CHANNEL::CH1);
// After System/RCC initialization:
if (output.SetUp() == SysInitStatus::InitOK) {
    SysStatus written = output.SetValue(2048); // Check written == SysStatus::OK.
}
output.DeInit();
```

SetUp selects direct buffered 12-bit output, initially zero. SetValue returns
NotInit before setup/after DeInit, Error for values above 4095 without changing
the code, and OK after a volatile register write. Analog startup/settling time
must be observed according to the device datasheet.

Repeated SetUp requires DeInit. DeInit disables only this channel, preserving
the other channel, DAC/GPIO clocks and analog pin configuration. Copy/move are
forbidden; the destructor is passive. F410 has CH1 only; a device without DAC
produces a compile-time diagnostic when this module is included.

---

## Order of initialization in main()

```cpp
int main() {
    System::Init();      // 1. FPU, Flash latency, SysTick

    constexpr auto clocks = ClockSystem::CalcPLL(64 * MHz, ClockSystem::PLL_ClockSource::HSI);
    if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
        System::DebugTrap("Clock init");

    // 3. Peripherals — in any order
    uart.SetUp();
    spi.SetUp(...);
    rtc.SetUp(...);

    // 4. DMA is constructor-bound: UART/SPI SetUp and TIM SetUpPWM configure it.
    tim_pwm.SetUp(...);
    tim_pwm.SetUpPWM(TIM::Channel::CH1);

    // 5. Start timers that require an explicit Start()
    periodic_timer.Start();

    for (;;) { /* main loop */ }
}
```

---

## Dependencies between modules

```
system.hpp
    └── rcc.hpp
    └── gpio.hpp
    └── irq_registry.hpp
            └── dma.hpp
                    ├── uart.hpp
                    ├── spi.hpp
                    └── tim.hpp
                            └── rtc.hpp (uses irq_registry.hpp)
```

`flash.hpp` depends only on `system.hpp` and `flash_config.h`.
`dac.hpp` depends on `system.hpp` and `gpio.hpp`.

---

## Adding a new MCU

1. Add `#include "stm32xxxx.h"` to `system.hpp`.
2. Add entries to the `PeriphInfo` tables in `uart.cpp`, `spi.cpp`, `tim.cpp`, `dma.cpp`.
3. Add stubs to `irq_registry_config.h`.
4. Add verified frequency/PLL/power limits to `rcc_defs.hpp`.
5. If needed, add register-abstraction macros (e.g. `RCC_GPIO_EN_REG` in `gpio.hpp`).

---

## Русский

### Миграция API — 19.10.2026

USART/SPI/ADC/I2C и описания каналов TIM принимают выбор DMA и содержат DMA_Sx
внутри. F4/F7 получают именованный DMAReq с номером контроллера/потока,
G0 — только физический DMA-канал; источник запроса выбирает периферия.
IRQ периферии и DMA разделены, ожиданий аппаратного завершения в IRQ нет.
Для SPI сохраняется согласованный Process() из main.

UART_USE_ATOMIC, SPI_USE_ATOMIC, ADC_USE_ATOMIC, TIM_USE_ATOMIC,
I2C_USE_ATOMIC, DAC_USE_ATOMIC, DMA_USE_ATOMIC и EXTI_USE_ATOMIC задаются в своих заголовках:
0 по умолчанию — обычные RMW, 1 — ATOMIC_* ST. Пересобираются драйверы
и приложение. Настройки CMake не добавляются. Атомарность отдельного RMW
не заменяет согласование целой операции и владения буфером.

GetIRQFlags возвращает HT/TC/ошибки одним чтением, публичные FLAG_HT/FLAG_TC/
FLAG_ERRORS используются потребителями. В I2C добавлены фиксированные DMA,
callbacks через наследование и DeInit; Recover перенесён в приложение.
Тайминги рассчитываются при компиляции: SetUp<clocks, I2C::SPEED::FAST>() получает ту же ClockSystem::Config, что и RCC Init. APB1 берётся из расчёта RCC; Apply сверяет её с System::APB1BusClock до настройки оборудования. Runtime-поиск и массив 2560 байт удалены. Подробности — [I2C.md](I2C.md). Сборки и аппаратные тесты миграции не запускались.


Универсальная C++ библиотека поверх CMSIS для семейств **STM32G0**, **STM32F4**, **STM32F7**.
Ядро (всегда собирается): System, RCC, GPIO, Flash, IRQ_Registry. Опциональные модули: UART, SPI, DMA, TIM, RTC, EXTI, ADC, DAC, I2C, MODBUS, CONTAINERS, PID, DSP, CRC.

Библиотека — не самостоятельный CMake-проект: её клонирует и подключает платформа [stm32_cmake_base](https://github.com/vsm-stm/stm32_cmake_base) (см. «Подключение и сборка»).

Дополнительно — модуль **`DSP`**: подмножество актуального CMSIS-DSP от Arm (быстрая математика `arm_sin_f32`/`arm_cos_f32`/`arm_sqrt_q31`/`arm_atan2_*`, регуляторы `arm_pid_*`, общие таблицы). Не входит в репозиторий, подтягивается при конфигурации (`FetchContent`, тег v1.16.2). Включается так же, как остальные опциональные модули — добавлением `"DSP"` в список `"drivers"` проекта. Лицензии сторонних частей — в `THIRD_PARTY.md`.

Модуль предоставляет следующий API CMSIS-DSP v1.16.2:

| Группа | Функции | Типы |
|---|---|---|
| Тригонометрия | `arm_sin_*`, `arm_cos_*` | `f32`, `q15`, `q31` |
| Квадратный корень | `arm_sqrt_*` | `f32` (inline), `q15`, `q31` |
| Угол | `arm_atan2_*` | `f32`, `q15`, `q31` |
| Деление | `arm_divide_*` | `q15`, `q31` |
| Векторный логарифм | `arm_vlog_*` | `f32`, `f64`, `q15`, `q31` |
| Векторная экспонента | `arm_vexp_*` | `f32`, `f64` |
| Совместный синус/косинус | `arm_sin_cos_*` | `f32`, `q31` |
| PID | `arm_pid_init_*`, `arm_pid_reset_*`, `arm_pid_*` (шаг, inline) | `f32`, `q15`, `q31` |
| Преобразования Кларка/Парка | `arm_clarke_*`, `arm_inv_clarke_*`, `arm_park_*`, `arm_inv_park_*` (inline) | `f32`, `q31` |

Для этих групп используйте `dsp/fast_math_functions.h` и `dsp/controller_functions.h`. Общий `arm_math.h` объявляет также функции групп, исходники которых модуль не собирает: FFT, FIR/IIR-фильтры, статистика и другие группы не подключены; наличие объявления не означает наличие реализации. Ядра float16 исключены. Общие таблицы собираются однократно вместе с отдельными функциями; агрегирующие исходники исключены.

`arm_sin_f32`/`arm_cos_f32` принимают радианы, `arm_sin_cos_f32` — градусы. Каждый экземпляр CMSIS PID перед первым шагом инициализируйте через `arm_pid_init_*(..., 1)`. Проверяйте возвращаемый `arm_status` у корня, деления и atan2. Диапазоны аргументов и форматы фиксированной точки описаны в заголовках Arm: [FastMath](https://github.com/ARM-software/CMSIS-DSP/blob/v1.16.2/Include/dsp/fast_math_functions.h) и [Controller](https://github.com/ARM-software/CMSIS-DSP/blob/v1.16.2/Include/dsp/controller_functions.h).

Модуль **`PID`** — собственный ПИД-регулятор (`math/PID.*`): переменный шаг `dT`, мёртвая зона `e_lim`, симметричное ограничение интегральной составляющей `max_ki_I` и вспомогательная `PID_get_quality` (относительное отклонение пика и накопленное время). C API доступен напрямую из C++.

Перед использованием обнулите состояние регулятора либо вызовите `zero_PID_I_D()`. На каждом шаге задавайте `e`; `dT` задаётся в секундах. Входные значения должны быть конечными, `dT > 0`, `e_lim`/`max_ki_I >= 0`; при `dT == 0` возвращается ноль без изменения состояния. При `ki == 0` интеграл `I` сохраняется. В зоне `|e| <= e_lim` интеграл сохраняется, `D` обнуляется, `e_old` обновляется; пропорциональная составляющая продолжает действовать.

Перед измерением и при смене уставки или режима сбрасывайте `PID_get_quality` через `get=0`. При `get=1` ошибка `e` выражается в процентах для ненулевой уставки `u`, а при `u=0` равна абсолютной ошибке `y-u`; `t_time` суммирует время вне коридора ±5%. При `get=2` ошибка абсолютная, `t_time` суммирует каждый переданный `dt`. Это накопленное время, а не время установления. Поле `max_y` хранит максимум отклика при `u >= 0` и минимум при `u < 0`, начиная с нуля после сброса; `sigma=(max_y-u)/u` — относительная величина, при `u=0` она равна нулю.

## Подключение и сборка

Драйверы включаются одной строкой в `project.json` проекта на `stm32_cmake_base`:

```json
"drivers": ["UART", "SPI", "DMA", "TIM"]
```

- `null` / `false` / ключа нет — драйверы не собираются вообще;
- `true` или `[]` — только ядро (System, RCC, GPIO, Flash, IRQ_Registry);
- `["UART", ...]` — ядро + перечисленные модули. Опечатка в имени — ошибка конфигурации со списком допустимых имён.

Первая конфигурация клонирует репозиторий в `Drivers/`; версию задаёт тег (`GIT_TAG` в `CMakeLists.txt` платформы). Из `stm32_drivers` линкуется цель `stm32_platform` (флаги, CMSIS, определения чипа). Конфиги под конкретный чип (`flash_config.h` — карта секторов, `irq_registry_config.h` — таблица векторов) библиотека генерирует сама в `cmsis-core/drivers_gen/`.

Ядро подключается заголовками `#include <system.hpp>`, `<rcc.hpp>`, `<gpio.hpp>`, `<flash.hpp>`, `<irq_registry.hpp>`, опциональные модули — `<uart.hpp>`, `<spi.hpp>`, `<tim.hpp>` и т.д. (пример — закомментированный блок в `inc/main.hpp` платформы).

### Модули и статус проверки

Проверялась только **компиляция** (release, по одному модулю; F446RE, F746ZG, G071RB). Работа на железе этим не подтверждается.

| Модуль | Файлы | F4 (F446) | F7 (F746) | G0 (G071) |
|---|---|---|---|---|
| Ядро: System, RCC, GPIO, Flash, IRQ_Registry | `src/system.*`, `rcc.*`, `gpio.*`, `flash.*`, `irq_registry.*` | ок | ок | ок |
| UART, SPI | `src/uart.*`, `spi.*` | ок | не проверялось | ок |
| TIM | `src/tim.*` | общий API TIM перенесён; сборка не запускалась | общий API TIM перенесён; сборка не запускалась | общий API TIM перенесён; сборка не запускалась |
| DMA | `src/dma.*` | исправлен, сборка не запускалась | исправлен, сборка не запускалась | таблица каналов/IRQ уточнена по МК; сборка не запускалась |
| RTC | `src/rtc.*` | реализовано, без сборки | реализовано, без сборки | обновлено, без сборки |
| EXTI | `src/exti.*` | GPIO 0..15 добавлены; без сборки | GPIO 0..15 добавлены; без сборки | GPIO 0..15 добавлены; без сборки |
| ADC | `src/adc.*` | фиксированный API, отдельные IRQ; не собирался | фиксированный API, отдельные IRQ; не собирался | фиксированный API, отдельные IRQ; не собирался |
| DAC | `src/dac.*` | исправлен, сборка не запускалась | исправлен, сборка не запускалась | исправлен, сборка не запускалась |
| I2C | `src/i2c.*` | API обновлён, без сборки | API обновлён, без сборки | API обновлён, без сборки |
| MODBUS | `Interfaces/` | миграция USART, без сборки | не проверялось | миграция USART, без сборки |
| CONTAINERS | `Containers/` (заголовки) | подключаются | — | — |
| PID | `math/PID.*` | ок | не проверялось | не проверялось |
| DSP | подмножество CMSIS-DSP v1.16.2 | ок | не проверялось | не проверялось |
| CRC | `crc/crc32.*` (без таблиц, без HW-периферии — переносимо на любое семейство) | ок | ок | ок |

Поведение и ограничения RTC и ADC описаны в [RTC.md](RTC.md) и [ADC.md](ADC.md);
реализация без сборки не считается проверенной на плате. Условия работы DAC и DMA
описаны в [DAC.md](DAC.md) и [DMA.md](DMA.md).

Настройка ADC, статусы/callbacks и обслуживание кеша описаны в [ADC.md](ADC.md).
Режим преобразований, DMA и TIM/источник закреплены конструктором. Используется
один Start(); при наличии DMA — Start(buffer, count). ADC и DMA обслуживаются
отдельными обработчиками без аппаратных ожиданий. HT включается явно через
IRQ_en, конечный DMA всегда использует TC. См. [ADC.md](ADC.md).
Прежнее сравнение ассемблера относится к общему IRQ
старой версии. Сборка и аппаратные тесты нового ADC API не запускались.

Семейства, отличные от F4 / F7 / G0 (в т.ч. G4), драйверами не поддерживаются: ставьте `"drivers": null` и пишите под регистры, либо портируйте (см. «Добавление нового МК»).

---

## Подробная документация по модулям

Этот файл даёт обзор и примеры использования каждого модуля. Полное описание внутреннего
устройства (регистры, тайминги, угловые случаи, разбор кода построчно) — в отдельных файлах:

- [System.md](System.md) — связующая инфраструктура: `SysStatus`/`SysInitStatus`, тактовые
  переменные, `GetTick()`/`Delay_us()`, `DebugTrap()`.
- [RCC.md](RCC.md) — дерево тактирования: HSI/HSE/PLL, делители шин, Flash latency.
- [GPIO.md](GPIO.md) — `PIN`/`PinArray`: кодирование режимов, регистры, bit-banding.
- [IRQ_Registry.md](IRQ_Registry.md) — единый диспетчер прерываний, регистрация с наследованием
  и без него, оценка накладных расходов по тактам.
- [DMA.md](DMA.md) — `DMA_Sx`: потоки (F4/F7) и каналы+DMAMUX (G0), таблицы запросов.
- [UART.md](UART.md) — `USART`: блокирующий/IRQ/DMA режимы, приём по IDLE-линии.
- [SPI.md](SPI.md) — `SPI`: роли Master/Slave, Software/Hardware NSS, DMA.
- [TIM.md](TIM.md) — общий TIM: PWM/capture, энкодер, измерение импульса, ITR и конечные серии.
- [Modbus.md](Modbus.md) — RS-485 transport, CRC, frame formats and IDLE limits.
- [RTC.md](RTC.md) — `RTC_cl`: BCD-время, WakeUp Timer, будильники, калибровка.
- [EXTI.md](EXTI.md) — внешние GPIO IRQ, фиксированные PIN/фронт, callback через наследование и ограничения общих векторов.
- [DAC.md](DAC.md) — `DAC_Module`: прямой 12-битный вывод, статусы и время жизни канала.
- [Flash.md](Flash.md) — `flash_base`/`flash_data`: разблокировка, стирание, wear-leveling.

---

## Содержание

1. [Архитектурные принципы](#1-архитектурные-принципы)
2. [System](#2-system)
3. [RCC / ClockSystem](#3-rcc--clocksystem)
4. [GPIO / PIN](#4-gpio--pin)
5. [IRQ_Registry](#5-irq_registry)
6. [DMA](#6-dma)
7. [USART](#7-usart)
8. [SPI](#8-spi)
9. [TIM](#9-tim)
10. [RTC](#10-rtc)
11. [Flash](#11-flash)
12. [DAC](#12-dac)

---

## 1. Архитектурные принципы

### Таблицы периферии (PeriphInfo)

Вместо длинных if-else цепочек вида `if (USARTx == USART1) { RCC->APB2ENR |= ...; }` каждый драйвер хранит **статическую таблицу** с описанием всех экземпляров периферии:

```cpp
struct PeriphInfo {
    USART_TypeDef*      periph;   // указатель на периферию (USART1, USART2...)
    volatile uint32_t*  clk_reg;  // регистр включения тактирования (APBxENR)
    uint32_t            clk_bit;  // бит в clk_reg
    uint32_t const*     bus_clk;  // указатель на переменную с частотой шины
    IRQn_Type           irq;      // номер вектора прерывания
};
static const PeriphInfo usart_table[];
```

При вызове `SetUp()` драйвер проходит по таблице, находит нужную запись по указателю на периферию, и автоматически:
- включает тактирование
- читает актуальную частоту шины
- знает вектор прерывания

Это единственное место, которое нужно обновлять при добавлении нового МК.

### Валидация пинов при компиляции

Пины для USART, SPI выбираются из вложенных структур:

```cpp
constinit USART debug(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7);
```

Каждый пин из `USART::_1::TX::PB6` содержит базовый адрес периферии. В конструкторе проверяется соответствие — если передать пин от другого USART, сработает `BKPT` и бесконечный цикл, что мгновенно обнаруживается при отладке.

### IRQ_Registry — единый диспетчер прерываний

Все прерывания проходят через один диспетчер. Стаб-обработчики в `irq_registry_config.h` вызывают `IRQ_Registry::Dispatch(IRQn)`, который перенаправляет вызов зарегистрированному объекту `IIRQHandler`.

Драйверы регистрируются и отменяют регистрацию автоматически при включении/выключении прерывания через `IRQ_en()`.

---

## 2. System

**Файлы:** `system.hpp`, `system.cpp`
**Подробнее:** [System.md](System.md)

Базовый модуль: FPU, Flash-акселератор, SysTick, задержки.

### Инициализация

```cpp
System::Init();  // настраивает ядро и запускает SysTick (1 кГц)
```

Должен вызываться **первым** в `main()`, до `ClockSystem::Init<config>()`.

Без `STM32_USE_BOOTLOADER` предполагает исходное состояние тактирования,
питания и latency после аппаратного сброса; вызывайте один раз в начале main,
до изменения частот. База автоматически задаёт `STM32_USE_BOOTLOADER`, когда
в project.json настроен `bootloader`. С макросом на F4/F7/G0 выбирает HSI 16 МГц
и делители шин /1;
G0 также возвращает VOS Range 1 перед Flash 0 WS.
Готовность HSI и первое переключение источника ожидаются без таймаута. Дальнейшие
ожидания используют SysTick (VOS/Flash: 100 мс), поэтому прерывания
должны быть разрешены. Настройки PLL/HSE и RCC-прерывания сохраняются.
Проверяйте возвращённый `SysInitStatus`. На L0 сохранено
прежнее предположение о HSI 16 МГц без восстановления аппаратного тактирования.

Base определяет `STM32_STARTUP_ENABLES_FPU`: `System::Init()` пропускает настройку
FPU, выполненную startup. При другом startup уберите это определение, если он
сам не включает FPU. На F7 кеши ядра остаются включёнными; обслуживание кеша
реализовано общими функциями System, которые подключим в периферии при её
разборе. Настройка MPU и разрешение
доступа к внешней памяти описаны в [System.md](System.md#6-mpu-на-stm32f7).

### Работа со временем

```cpp
uint32_t t = System::GetTick();   // мс с момента старта
System::Delay_ms(500);            // блокирующая задержка 500 мс

// Неблокирующий таймер:
uint32_t start = System::GetTick();
if ((System::GetTick() - start) > 1000) { /* прошла секунда */ }
```

### Микросекундные задержки (F4/F7)

```cpp
System::Enable_CYCCNT();   // один раз при старте
System::Delay_us(10);      // задержка 10 мкс через DWT
```

`Delay_ms()` требует работающего SysTick, способного прервать вызывающий код:
нельзя ждать при маскировке тиков или из IRQ с равным/более высоким приоритетом.
Сам `GetTick()` только читает счётчик. Ожидание `Delay_ms()` вместе с компенсирующим
тиком ограничено `0x7FFFFFFF` тиками (около 24,9 суток при 1 кГц).
`Delay_us()` использует 64-битную арифметику
и ограничен `0x7FFFFFFF` тактами ядра; предел длительности зависит от частоты.
На **STM32G0/L0** DWT недоступен — `Delay_us` не существует.

На F4/F7 `SWOTrace()` неблокирующий и возвращает число байтов, принятых ITM.
В DEBUG `DebugTrap()` сохраняет указатель сообщения, выводит его через
блокирующий SWO при включённой трассировке, выполняет BKPT и останавливается.
SWO настраивается в отладчике. `SetDebugOutput()` удалён. Release только
останавливается; удаление строк зависит от оптимизации компилятора/линкера.

### Некешируемые DMA-области

В проектах F7 настройка базы `dma_regions` резервирует некешируемые DMA-буферы
(`name`, `size`, `memory`: SRAM1/SRAM2, `mpu_region`). Адреса вычисляются
автоматически; startup обнуляет буферы, а `System::Init()` настраивает MPU до
включения D-cache. Буферы размещаются в `.dma.<name>`. Настройка, ограничения
и порядок инициализации описаны в [System.md](System.md).

Общие операции кеша — `System::CacheClean`, `CacheInvalidate` и
`CacheCleanInvalidate` (адрес + размер в байтах). На F7 диапазон целиком внутри
настроенной некешируемой DMA-области пропускается; на F4/G0/L0 функции пустые
и определены inline. Периферийный драйвер определяет момент вызова; при прямом
использовании DMA это делает пользователь. RX-буфер должен владеть всеми
затронутыми 32-байтными строками кеша. Подробнее — [System.md](System.md).

### Переменные частот шин

После `ClockSystem::Init<config>()` доступны:

```cpp
System::SystemCoreClock  // частота ядра (AHB)
System::APB1BusClock     // частота APB1
System::APB2BusClock     // частота APB2
System::TIMxAPB1Clock    // частота для таймеров на APB1
System::TIMxAPB2Clock    // частота для таймеров на APB2
```

---

## 3. RCC / ClockSystem

**Файлы:** `rcc.hpp`, `rcc.cpp`, `rcc_defs.hpp`
**Подробнее:** [RCC.md](RCC.md)

Одна настройка тактирования после аппаратного сброса. Конфигурация и расчёты —
константные выражения; Init<config>() проверяет их при компиляции. Сначала
System::Init(), затем RCC, затем настройка периферии.

### HSI без PLL

```cpp
constexpr ClockSystem::Config clocks{};
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
    System::DebugTrap("Clock init");
```

### Точный автоподбор PLL

Пример: G0, SYSCLK 64 МГц от HSI:

```cpp
constexpr auto clocks = ClockSystem::CalcPLL(
    64 * MHz, ClockSystem::PLL_ClockSource::HSI);
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
    System::DebugTrap("Clock init");
```

Для HSE третий аргумент задаёт номинальную частоту, четвёртый — Q (по умолчанию 2).
CalcPLL выбирает точный SYSCLK, AHB /1 и допустимые делители APB. Среди точных
решений приоритет максимальной допустимой частоте входа PLL, затем минимальному
P/R. Если решения нет, Init<clocks>() выдаёт ошибку сборки.

### Ручная конфигурация

F407, HSE 8 МГц, SYSCLK/HCLK 168 МГц, APB1 42 МГц, APB2 84 МГц:

```cpp
constexpr ClockSystem::Config clocks{
    .ClkSrc = ClockSystem::SystemClockSource::PLL,
    .HSE_Clk = 8 * MHz,
    .BusDiv = {ClockSystem::AHB_Divider::DIV1,
               ClockSystem::APB_Divider::DIV4,
               ClockSystem::APB_Divider::DIV2},
    .PLLCfgr = {ClockSystem::PLL_ClockSource::HSE, 4, 168, 2, 7, 2},
};
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
    System::DebugTrap("Clock init");
```

Поддерживаются все F4/F7 из карт STM32-base_files и G0. На G0 также доступны
LSI/LSE. VDD фиксирован: 3,0 В, тот же диапазон timing при 3,3 В. Питание,
overdrive и latency выбираются по модели/HCLK. Ошибки во время исполнения —
таймаут аппаратуры или повторная попытка. После ошибки также требуется сброс.

SysTick использует целое число тактов HCLK. При LSE 32768 Гц и AHB /1 получается
1024 тика/с; Delay_ms и таймауты имеют ту же погрешность. Ограничения, порядок
переключения и переход со старых Init(...)/InitCalcPLL(...) описаны в RCC.md.

---

## 4. GPIO / PIN

**Файлы:** `gpio.hpp`, `gpio.cpp`
**Подробнее:** [GPIO.md](GPIO.md)

Для обычного GPIO используйте PIN(GPIOA_BASE, 5): конструктор с указателем закрепляет
порт/номер/AF без runtime-проверок. Для compile-time таблиц используется GPIOx_BASE
и consteval-конструктор с проверкой порта, номера и сохранённого AF.
Порт/номер/сохранённый AF закрыты и неизменны, periph_base доступен только для чтения.
Копирование при создании разрешено, перепривязка существующего PIN присваиванием
запрещена. Явный AF можно передать в SetUp. ADC уже использует неизменяемый PIN
с проверками при компиляции и именами PA0_IN0. Миграция TIM остаётся в его плане;
создание обычного GPIO через указатель поддерживается.

### Создание и настройка пина

```cpp
PIN led(GPIOB, 8);                        // порт + номер пина
PIN btn(GPIOA, 0);

led.SetUp(PIN::TYPE::OUTPUT_PushPull);    // выход push-pull
btn.SetUp(PIN::TYPE::INPUT_PullUp);       // вход с подтяжкой

led.SetLevel(true);      // HIGH
led.SetLevel(false);     // LOW
led.TogglePin();         // инверсия

bool state = btn.GetLevel();   // читаем состояние
```

### Все типы пинов

| Константа | Описание |
|---|---|
| `INPUT_NO_Pull` | Вход без подтяжки |
| `INPUT_PullUp` | Вход с подтяжкой к VCC |
| `INPUT_PullDown` | Вход с подтяжкой к GND |
| `OUTPUT_PushPull` | Выход push-pull |
| `OUTPUT_OD` | Выход open-drain |
| `OUTPUT_OD_PulUp` | Open-drain + подтяжка |
| `AF_PushPull` | Альтернативная функция, push-pull |
| `AF_OD` | Альтернативная функция, open-drain |
| `ANALOG` | Аналоговый режим (для ADC/DAC) |

### Скорость вывода

```cpp
led.SetUp(PIN::TYPE::OUTPUT_PushPull, PIN::OUTPUT_SPEED::High);
```

OUTPUT по умолчанию начинается с LOW: уровень записывается через BSRR до
включения режима. Для начального HIGH:

```cpp
led.SetUp(PIN::TYPE::OUTPUT_PushPull, PIN::OUTPUT_SPEED::Low, PIN::LVL::HIGH);
```

`TogglePin()` использует чтение и перезапись ODR. Во время этой операции нельзя
менять выводы того же порта из IRQ. Индекс порта в SetUp вычисляется делением
смещения на константу GPIOB_BASE - GPIOA_BASE. В проверенных сборках GCC 13.3.1
для F429/F767/G031 машинный код совпадает с вариантом явного сдвига.

### Альтернативная функция

```cpp
PIN sck(GPIOA, 5);
sck.SetUp(PIN::TYPE::AF_PushPull, 5);   // AF5 = SPI1 на STM32F429
```

USART/SPI используют сохранённый AF при настройке пинов. Миграция TIM отложена.

### Оператор присваивания и преобразования

```cpp
led = true;       // SetLevel(true)
led = false;      // SetLevel(false)
bool b = (bool)led;   // GetLevel()
```

### IsValid()

Пин, созданный через `PIN{}` (конструктор по умолчанию), возвращает `IsValid() == false`. Используется в драйверах как признак "канал не подключён":

```cpp
constinit SPI spi(SPI::_1::instance, SPI::Direction::TX, sck, mosi, PIN{}, PIN{});   // MISO и SS не используются
```

### PinArray — массив пинов

```cpp
PIN segments[] = {
    PIN(GPIOA_BASE, 0), PIN(GPIOA_BASE, 1), PIN(GPIOA_BASE, 2), PIN(GPIOA_BASE, 3)
};
PinArray<4> seg_pins(segments);

seg_pins.SetUpAll(PIN::TYPE::OUTPUT_PushPull);
seg_pins.SetLevelAll(0b1010);    // установить по битам
uint32_t state = seg_pins.GetLevelAll();  // прочитать все сразу
```

`SetUpAll()` настраивает все элементы и возвращает первый статус ошибки без преобразования.

---

## 5. IRQ_Registry

**Файлы:** `irq_registry.hpp`, `irq_registry.cpp`, `irq_registry_settings.hpp`; сгенерированный `irq_registry_config.h`.
**Подробнее:** [IRQ_Registry.md](IRQ_Registry.md)

Сгенерированные ISR вызывают Dispatch(irqn), который вызывает зарегистрированные
объекты IIRQHandler или статические адаптеры обычных функций. Источники одной
периферии обслуживает один объект; разные периферийные объекты могут делить IRQ.

Register возвращает SysInitStatus::InitOK или InitError. Неверный/нулевой параметр,
повтор объекта/функции на том же IRQ и заполнение таблицы/пула дают InitError.
Другой ctx не разрешает повтор той же функции. Изменение регистрации выполняется
через отмену и новый Register.

```cpp
if (IRQ_Registry::Register(TIM3_IRQn, &handler) == SysInitStatus::InitOK)
    NVIC_EnableIRQ(TIM3_IRQn);

// Позже, из main, при отключённых источниках этого владельца:
NVIC_DisableIRQ(TIM3_IRQn);
IRQ_Registry::Unregister(TIM3_IRQn, &handler);
if (IRQ_Registry::HasHandlers(TIM3_IRQn)) NVIC_EnableIRQ(TIM3_IRQn);
```

Для функций используется Register(irqn, fn, ctx); ctx — указатель на данные,
передаваемый в fn(ctx). Отмена — Unregister(irqn, fn), адаптер освобождается.
Существующая перегрузка Unregister(irqn) явно очищает всю строку.

Register/Unregister запрещены из HandleIRQ и его callback. Изменения выполняются
из main при замаскированной затронутой IRQ-линии. При завершении callback может
отключить источник периферии, сохранив регистрацию до отмены из main.
Существующие вызовы в периферийных драйверах обновляются при разборе их модулей.

### Число обработчиков на один IRQ

Настройки изменяются в общем для всех исходных файлов заголовке
`Drivers/src/irq_registry_settings.hpp`. IRQ_MAX_SHARED по умолчанию равен 3
для F4/F7/G0; для пяти обработчиков:

```cpp
#define IRQ_MAX_SHARED 5
```

Каждое дополнительное место требует IRQ_TABLE_SIZE * 4 байта на этих МК.
Отдельный пул IRQ_MAX_FUNCTION_HANDLERS по умолчанию равен 8. После изменения
пересоберите библиотеку и образы; обновление сохранённых шаблонов описано
в подробной документации.

---

## 6. DMA

**Файлы:** `dma.hpp`, `dma.cpp`, `dma_requests.hpp`
**Подробнее:** [DMA.md](DMA.md)

Поддерживает F4/F7 (потоки, `DMA_Stream_TypeDef`) и G0 (каналы, `DMA_Channel_TypeDef`). API идентичен для обоих семейств.

`SetUp()` допускает нулевые счётчик/адреса для настройки без запуска. Повторная
настройка занятого канала, некорректные поля регистров, невыровненные адреса
и счётчик больше 65535 возвращают ошибку. `SetCount()` принимает 1..65535
элементов. Методы адресов/счётчика возвращают `NotInit` до настройки, `Busy`
при установленном EN и `Error` для неверных аргументов; проверяйте результат.

`Stream_EN(DISABLE)` ждёт EN=0 без таймаута. `Release()` останавливает и
сбрасывает принадлежащий объекту ресурс, затем снимает занятость; выбор из
конструктора сохраняется. До освобождения отключите DMA-запросы периферии
и удалите её обработчик. Общая линия NVIC и тактирование контроллера остаются
включёнными. SetUp/Release вызываются из mainloop. Один настроенный объект
используется одним потребителем.

Дескрипторы DMA хранятся в таблице `static const dma_streams` во Flash.
В RAM остаётся отдельный массив флагов занятости `dma_used`.

Операции кеша находятся в `System`; периферийный драйвер определяет их момент
и диапазон. Подключение в остальных потребителях выполняется при их разборе.

### Конструктор (STM32G0)

На G0 DMAMUX1 позволяет подключить любой канал к любой периферии. Канал выбирает пользователь, запрос — из определений `_N::DMA` периферии:

```cpp
// Вариант 1: канал + запрос из таблицы (рекомендуется)
DMA_Sx dma_tx(DMA1_Channel4_BASE, USART::_1::DMA::TX);
DMA_Sx dma_rx(DMA1_Channel5_BASE, USART::_1::DMA::RX);

// Вариант 2: канал + сырой ID запроса DMAMUX1
DMA_Sx dma(DMA1_Channel3_BASE, 51u); // USART1_TX

// Вариант 3: канал без запроса (ID передаётся позже через StreamSettings)
DMA_Sx dma(DMA1_Channel3_BASE);
```

### Конструктор (STM32F4/F7)

На F4 поток и CHSEL жёстко привязаны к периферии:

```cpp
DMA_Sx dma_tx(USART::_1::DMA::TX_DMA2_Stream7); // CH4
DMA_Sx dma_rx(USART::_1::DMA::RX_DMA2_Stream5); // CH4
```

### Настройка через StreamSettings

```cpp
DMA_Sx::StreamSettings cfg;
cfg.direction   = DMA_Sx::DIR::To_Per;    // память → периферия
cfg.data_size   = DMA_Sx::SIZE::Byte;     // ширина элемента
cfg.minc        = true;                   // инкремент адреса памяти
cfg.pinc        = false;                  // адрес периферии фиксирован
cfg.per_address = reinterpret_cast<uint32_t>(&USARTx->TDR);
cfg.count       = 0;                      // задаётся перед каждой передачей

dma.SetUp(cfg);
```

### Ручной запуск передачи

```cpp
dma.ClearFlags();
dma.SetMemAddr(reinterpret_cast<uint32_t>(buf));
dma.SetCount(len);
dma.Stream_EN(ENABLE);
```

### Прерывание по TC

```cpp
IRQ_Registry::Register(dma.GetIRQn(), &my_handler);
NVIC_EnableIRQ(dma.GetIRQn());
dma.Enable_IRQ(DMA_Sx::IRQ::TC);
```

В обработчике, для некольцевой передачи:

```cpp
void HandleIRQ() override {
    if (_dma.GetTC_Flag()) {
        _dma.RequestStop(); // TC уже подтвердил завершение.
        _dma.ClearFlags();
        __DMB();
        // передача завершена
    }
}
```

### Направления

| `DIR` | Значение |
|---|---|
| `From_Per` | Периферия → Память |
| `To_Per` | Память → Периферия |
| `Mem_To_Mem` | Память → Память на G0; на F4/F7 — `InitError` |

### Ширина элемента

| `SIZE` | Размер |
|---|---|
| `Byte` | 8 бит |
| `Half_Word` | 16 бит |
| `Word` | 32 бита |

### Важно о разделяемых IRQ на G0

| Каналы | Вектор |
|---|---|
| CH1 | `DMA1_Channel1_IRQn` (эксклюзивный) |
| CH2, CH3 | `DMA1_Channel2_3_IRQn` (общий) |
| CH4, CH5 | `DMA1_Ch4_5_DMAMUX1_OVR_IRQn` (общий) |

Последняя строка относится к моделям с пятью каналами. Для DMA1 с семью
каналами используется `DMA1_Ch4_7_DMAMUX1_OVR_IRQn`; при наличии DMA2 —
`DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX1_OVR_IRQn` для DMA1 CH4–7 и DMA2 CH1–5.
Драйвер выбирает состав каналов и вектор по заголовку выбранного МК.

Два канала на одном векторе — не проблема: `IRQ_Registry` поддерживает цепочку обработчиков.

---

## 7. USART

**Файлы:** `uart.hpp`, `uart.cpp`, `uart_defs.hpp`
**Подробнее:** [UART.md](UART.md)

UART использует oversampling x16, проверяет BRR; SetBaud() возвращает SysStatus.
На F7/G0 последний параметр конструктора `USART::ClockSource` задаёт неизменяемый
источник: PCLK (по умолчанию), SYSCLK, HSI или LSE, если он доступен у экземпляра.
HSI/LSE запускает вызывающий код; SetUp только проверяет включение и готовность.
F4 сохраняет PCLK. Пример выбора источника приведён в [UART.md](UART.md).
Перед SetBaud() вызывающий код ждёт физического завершения TX (TC/OnTC).
Busy проверяет только программные операции; дополнительного поля контроля TX нет.
Нулевая длина — Error, IRQ/DMA — 1..65535 байт. Описания DMA TX/RX задаются
после пинов в конструкторе, затем ClockSource. DMA хранятся внутри USART.
При constinit принадлежность и направление DMA проверяются при компиляции;
SetUp приобретает и настраивает ресурсы. USART и DMA имеют отдельные обработчики.
За вход обслуживается до глубины FIFO в каждом направлении: 8 слов G0 при
включённом FIFO, иначе один байт. При IDLE хвост имеет свой единственный предел.
На F4/F7 IDLE запрашивает остановку, DMA RX TC завершает count/cache/ready:
без ожидания в IRQ и без Process(). На G0 IDLE отключает DMAR/DMA без ожидания
и завершает пакет в USART IRQ; последний запрос DMA должен быть уже обслужен.
Повреждённый пакет отбрасывается без чтения длины из DMA. Ограничение — в UART.md.
Кеш F7 обслуживается через System; для настроенных некешируемых DMA-областей
кешевые операции пропускаются.
GetDataReceivedCount() больше не сбрасывает флаг/счётчик: после обработки нужен
ClearDataReceivedFlag(). Ошибка останавливает свою операцию и вызывает OnError(mask).
Modbus явно сбрасывает признак после обработки пакета; обновлённый контракт — [Modbus.md](Modbus.md).

### Базовое использование

```cpp
constinit USART debug(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7);

debug.SetUp();
debug.Send((uint8_t*)"Hello\r\n", 7);

uint8_t buf[16];
debug.Receive(buf, sizeof(buf), 500);  // таймаут 500 мс
```

### Только TX (без RX)

```cpp
constinit USART log(USART::_2::instance, 115200, USART::_2::TX::PA2, PIN{});
log.SetUp();
log.Send((uint8_t*)"OK\r\n", 4);
```

### DMA-передача

```cpp
constinit USART uart(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7,
           DMA1_Channel4_BASE,
           DMA1_Channel5_BASE);

uart.SetUp(); // Проверить InitOK; приобретает/настраивает внутренние DMA.

uart.Send_DMA(tx_buf, sizeof(tx_buf));
uart.Receive_DMA(rx_buf, sizeof(rx_buf));  // завершается по IDLE или по заполнению
```

### Прерывания

```cpp
uart.IRQ_en(USART::IRQ::RXNE, ENABLE);   // включить RXNE-прерывание
uart.IRQ_en(USART::IRQ::TXE, DISABLE);   // выключить TXE-прерывание
```

`SetUp()` один раз регистрирует UART IRQ. `IRQ_en()` меняет только источники
периферии, в том числе из callback. `DeInit()` удаляет только свои регистрации,
сохраняя обработчики других устройств на общем IRQ.

### Собственная обработка прерываний (наследование)

```cpp
class MyUART : public USART {
    using USART::USART;  // наследуем конструктор
    void OnRxByte(uint8_t b) override { USART::OnRxByte(b); ring_buf.push(b); }
    void OnIdle() override { USART::OnIdle(); process_packet(); }
};

constinit MyUART uart(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7);
```

### Изменение baudrate между обменами

```cpp
uart.SetBaud(9600);
```

---

## 8. SPI

**Файлы:** `spi.hpp`, `spi.cpp`, `spi_defs.hpp`
**Подробнее:** [SPI.md](SPI.md)

SPI1–SPI6 у F4/F7 и SPI1–SPI3 у G0 подключаются по наличию в CMSIS.
Direction::TX/RX/TXRX и необязательные DMA фиксируются в конструкторе; структура
настроек остаётся в SetUp. TX использует аппаратную передачу без RX и один TX DMA.
RX master работает через full duplex: RX DMA и TX IRQ для dummy 0xFF/0xFFFF.

### Базовое использование (блокирующий режим)

```cpp
constinit SPI spi(SPI::_1::instance, SPI::Direction::TXRX, SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7,
        SPI::_1::MISO::PA6, PIN(GPIOB_BASE, 0)); // Необязательный программный CS
if (spi.SetUp(SPI::Master_sel::Master, SPI::NSS_ctrl::Software,
              SPI::Data_frame_format::Byte, SPI::Frame_Format::MSB,
              SPI::cPolPha::None, SPI::BaudRate::DIV4) == SysInitStatus::InitOK) {
    uint8_t tx[] = {0x12, 0x34};
    SysStatus result = spi.Send(tx, sizeof(tx)); // CS снимается и при ошибке
    // Проверить result.
}
```

Для нескольких устройств SS в конструкторе не передаётся, отдельными CS
управляет приложение. Hard требует свой NSS-пин; slave использует аппаратный NSS.

### 16-битные кадры

```cpp
// После успешного SetUp(Master, Software, Half_Word):
uint16_t tx[] = {0x1234, 0xABCD}, rx[2];
SysStatus result = spi.Send_Receive(tx, rx, 2); // len — количество кадров
```

### DMA и завершение обмена

```cpp
// F4/F7:
constexpr SPI::DMAReq dma_tx = SPI::_1::DMA::TX_DMA2_Stream3;
constexpr SPI::DMAReq dma_rx = SPI::_1::DMA::RX_DMA2_Stream0;
// G0: constexpr SPI::DMAReq dma_tx = DMA1_Channel2_BASE;
//     constexpr SPI::DMAReq dma_rx = DMA1_Channel3_BASE;
constinit SPI spi(SPI::_1::instance, SPI::Direction::TXRX, SPI::_1::SCK::PA5,
        SPI::_1::MOSI::PA7, SPI::_1::MISO::PA6, PIN{}, dma_tx, dma_rx);
if (spi.SetUp(SPI::Master_sel::Master, SPI::NSS_ctrl::Software) == SysInitStatus::InitOK) {
    // Запустить spi.SendReceive_DMA(tx_buffer, rx_buffer, frame_count); проверить OK.
}
// В main для запущенного IRQ/DMA обмена:
SysStatus state = spi.Process(); // Busy до физического завершения SPI
// После OK доступен RX, GetDataReceivedCount() возвращает число кадров.
```

Кеш F7 обслуживается через System; настроенные некешируемые DMA-области
пропускаются. CPU не обращается к активному RX и не изменяет TX.
OnComplete()/OnError(mask) вызываются после очистки в Process. IRQ_en меняет
источники CR2; регистрацией и NVIC управляет SetUp.

В slave len — вместимость. Приложение настраивает EXTI снятия NSS и вызывает
NssReleased() из обработчика. Process завершает пакет в main. Лишние RX-кадры
отбрасываются с IsTruncated(); после TX-буфера передаётся dummy. Приоритеты IRQ,
ограничения обслуживания и повторное взведение описаны в SPI.md. SPI и DMA TX/RX
имеют отдельные обработчики без ожиданий. На G0 последний запрос DMA должен
быть обслужен до остановки по NSS; длина читается в Process. Ошибочный пакет
отбрасывается.

### Делитель частоты (BaudRate)

| Константа | Делитель |
|---|---|
| `DIV2` | PCLK / 2 |
| `DIV4` | PCLK / 4 |
| `DIV8` | PCLK / 8 |
| ... | ... |
| `DIV256` | PCLK / 256 |

### Полярность/фаза (cPolPha)

| Константа | CPOL | CPHA | Режим SPI |
|---|---|---|---|
| `None` | 0 | 0 | Mode 0 |
| `cPha` | 0 | 1 | Mode 1 |
| `cPol` | 1 | 0 | Mode 2 |
| `cPolPha` | 1 | 1 | Mode 3 |

---

## 9. TIM

**Файлы:** `tim.hpp`, `tim.cpp`, `tim_defs.hpp`
**Подробнее:** [TIM.md](TIM.md)

Один общий TIM обслуживает базу и независимо настроенные каналы. До четырёх
описаний `{pin, dma}` закрепляют пины/DMA в конструкторе. TIM_PWM/PeriodicIRQ/
InputCapture, объектов Output и AttachDMA нет. Составные режимы сохранены
комментариями и разбираются отдельно.

### База, PWM и compare

```cpp
void Tick();
void HalfTick();
constinit TIM timer(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7});
// В main, проверить все результаты до Start:
if (timer.SetUp(1000, &Tick, 1000) == SysInitStatus::InitOK
    && timer.SetUpPWM(TIM::Channel::CH1, 250) == SysInitStatus::InitOK
    && timer.SetUpPWM(TIM::Channel::CH2, 500) == SysInitStatus::InitOK
    && timer.SetCompareIRQ(TIM::Channel::CH3, 750, &HalfTick) == SysInitStatus::InitOK) {
    timer.Start();
}
timer.SetDuty(TIM::Channel::CH1, 75); // Проверить SysStatus.
```

SetUp сохраняет `(frequency, callback, period)`: частота — Update/PWM в Гц,
period — число тиков CNT (ARR=period-1), ноль выбирает период автоматически.
Повторный SetUp требует DeInit. PSC/ARR общие. Для counter/capture период
обязателен; фактическую частоту CNT показывает GetCounterClock.

PWM1 использует preload CCR. Предел периода 16-битного PWM — 65535 тиков:
CCR=period даёт 100%, ноль — 0%, без переключения в forced active. SetDuty
ограничивает проценты сотней, SetCCR принимает сырые тики. Пока канал занят
DMA, эти методы его CCR не меняют. Start всегда явный.

### Capture и аппаратный триггер

```cpp
void Edge();
constinit TIM input(TIM::_3::instance, {TIM::_3::CH1::PA6});
// В main: целевая частота CNT = 100 Гц * 10000 тиков = 1 МГц.
if (input.SetUp(100, nullptr, 10000) == SysInitStatus::InitOK
    && input.SetUpCapture(TIM::Channel::CH1, &Edge) == SysInitStatus::InitOK) {
    input.Start();
}
// Edge читает input.GetCapture(TIM::Channel::CH1).
```

Capture — прямой TIx по нарастающему фронту. GetCapture всегда читает CCR напрямую,
необязательный callback только уведомляет. Переполнение/overcapture обрабатывает
приложение. SetCompareIRQ с nullptr создаёт аппаратный compare без GPIO и IRQ.
SetUpTrigger(Trigger::Update/ComparePulse/OCxRef) выбирает TRGO без IRQ. ADC получает
TIM* и источник конструктором, запускает TIM после вооружения ADC/DMA. Таймер
настраивает вызывающий код до ADC SetUp; см. [ADC.md](ADC.md).

### Независимые DMA и WS2812B

```cpp
// G0; для F4/F7 используется TIM::_1::DMA::CH1_DMA2_Stream1.
constexpr TIM::DMAReq dma = DMA1_Channel3_BASE;
constinit TIM ws(TIM::_1::instance, {TIM::_1::CH1::PA8, dma});
WS2812B_Strip<8> strip(52, 26);
strip.SetAll(RGB::Red());
// Пример: CNT 64 МГц, period=80 => 800 кГц, ARR=79.
if (ws.SetUp(800000, nullptr, 80) == SysInitStatus::InitOK
    && ws.SetUpPWM(TIM::Channel::CH1) == SysInitStatus::InitOK
    && ws.SendDMA(TIM::Channel::CH1, strip.Data(), strip.Len()) == SysStatus::OK) {
    ws.Start();
}
// Не менять strip, пока ws.IsDMABusy(TIM::Channel::CH1).
```

DMA из конструктора настраивается внутри SetUpPWM. Каждый канал использует
TIMx_CHx, CCDS=1 и собственный CCxDE. У каждого DMA отдельный обработчик;
таймерный IRQ DMA-флаги не опрашивает. 16-битные таймеры используют uint16_t/
Half_Word, 32-битные TIM2/TIM5 — uint32_t/Word. Clean F7 выполняет System,
некешируемые области проекта пропускаются.

TC освобождает буфер; последний preload/PWM-период может ещё не завершиться.
WS2812B содержит нулевой reset trailer; CCR и reset timing выбираются под
реальные частоты и светодиоды. Ошибка останавливает свой DMA и уведомляет его
callback; результат показывает GetDMAStatus. Остальные каналы продолжают работу.

При записи DIER из main вызывающий код маскирует соответствующие TIM/DMA IRQ,
если они могут выполняться, в том числе вокруг SendDMA и IRQ_en. Обработчики
одного объекта не прерывают друг друга. Внутри нет IrqLock/изменений PRIMASK;
ожиданий аппаратуры в IRQ нет. DeInit из main освобождает занятые DMA/регистрации.
Ограничения и конфликты маршрутов описаны в TIM.md.

Дополнительные режимы — функции TIM: SetUpEncoder занимает таймер с CH1/CH2,
Count возвращает CNT напрямую. SetUpPulseMeasure занимает CH1/CH2 и SMCR;
GetCapture читает период выбранного входа и длительность HIGH парного канала.
SetUpSlave выбирает обычный маршрут TRGO/ITR, SetUpOnePulse однократно задаёт OPM.
Для конечных серий два отдельных API: SetUpPulseBurst/RunPulses — аппаратный RCR+OPM;
SetUpCounter/StopAfter/StopAt — второй TIM и остановка источника из IRQ.
Во втором случае возможны лишние/неполные импульсы из-за задержки IRQ.
SetUpQuadrature настраивает второй TIM из конструктора с PSC=1; GenPulse(N)
запускает полные A/B-циклы, знак задаёт направление. Stop останавливает оба TIM,
DeInit освобождает оба. Остановка тоже из IRQ, без программного подсчёта циклов.
Callbacks необязательны, роли/инициализация сохраняются до DeInit. ETR отложен.

Сборка и аппаратная проверка нового API в этом проходе не выполнялись.

---

## 10. RTC

**Файлы:** src/rtc.hpp, src/rtc.cpp. **Подробнее:** [RTC.md](RTC.md).

RTC_cl — объект для F4/F7/G0. События OnWakeup, OnAlarmA, OnAlarmB и
OnTimestamp(const Timestamp&) переопределяются в наследнике. Настройка — из main
после System/RCC; callbacks не ждут и не меняют реестр IRQ.

~~~cpp
class Clock : public RTC_cl {
protected:
    void OnWakeup() override { /* уведомить main */ }
};
static Clock clock;
if (clock.SetUp(RTC_cl::CLK_Source::LSE, 127, 255) == SysInitStatus::InitOK) {
    // Устанавливать календарь только когда приложение действительно меняет время:
    // clock.SetTime({14, 25, 0});
    // clock.SetDate({22, 5, 26}, RTC_cl::WeekDay::Friday);
    clock.EnableWakeUpTimer(1); // Две секунды при календаре 1 Гц.
}
RTC_cl::RtcTime time;
RTC_cl::RtcDate date;
clock.GetDateTime(time, date); // Согласованная пара TR/DR, shadow lock освобождён.
~~~

При сохранённом источнике календарь и делители не меняются, RSF синхронизируется.
При другом выбранном источнике **сбрасывается backup domain с потерей времени
и backup-регистров**, затем применяется новый источник. Повторный SetUp до DeInit
возвращает ошибку. LSE ждём до 5000 мс, остальные флаги — до 1000 мс через
System::WaitBits; RECALPF сразу возвращает Busy без ожидания.

127/255 делит 32768 Гц до 1 Гц. Для номинального LSI 32000 Гц задавайте 127/249;
старые defaults LSI,127,255 сохранены, для LSI требуется явный выбор делителей.
HSE и его RTC-делитель готовит приложение; на G0 используется HSE/32.

SetTime/SetDate и BCD-варианты проверяют диапазоны и возвращают SysStatus.
Alarm A/B настраиваются AlarmConfig с полями Care/Ignore; day — число месяца,
MASKSS=0 игнорирует подсекунды. WUT: /16,/8,/4,/2,ck_spre и 17-битный ck_spre
(код 110). Timestamp читает захват до очистки TSF, года в аппаратном захвате нет.
Пины RTC_TS/CALIB готовит приложение.

DeInit выключает собственные события и удаляет только свои регистрации, сохраняя
общие обработчики TAMP и ход календаря. Полный контракт, калибровка, примеры и
миграция — в RTC.md. Сборки и аппаратные тесты этих правок не запускались.

---

## 11. Flash

**Файлы:** `flash.hpp`, `flash.cpp`

**Подробнее:** [Flash.md](Flash.md)

### flash_base — низкоуровневый доступ

Поддерживаются F4/F7/G0. `init()` сверяет карту банков с аппаратным режимом и
отклоняет bank swap; option bytes не меняет. `erase_sector()` принимает индекс
карты. `mass_erase()` принимает 1/2/3 для первого/второго/обоих банков.
Геометрия доступна через GetFlashSector(index): равномерные страницы вычисляются по константам банков, неравномерные используют таблицу. Формат конфигурации 4 требует согласованного flash_config.h.in; описание — [Flash.md](Flash.md).

```cpp
if (flash_base::init() != SysInitStatus::InitOK) return;
if (flash_base::enable_access() != SysStatus::OK) return;

// Сектор должен быть зарезервирован проектом, без кода.
if (flash_base::erase_sector(settings_sector) != SysStatus::OK) return;
const uint32_t address = GetFlashSector(settings_sector).address;
const uint32_t values[] = {0x12345678, 0xDEADBEEF};
const SysStatus written = flash_base::write(address, values, 2);
if (written != SysStatus::OK) return;
const uint32_t value = flash_base::read<uint32_t>(address);
```

Разблокировка, запись и стирание возвращают `SysStatus`. Вызовы — из main loop
при работающем SysTick, вне прерываний и критических секций. Предел на каждую
аппаратную операцию: **F4/F7 — 50 с, G0 — 1 с**. Выборка кода из занятого банка
может остановить SysTick, поэтому реальное время таймаута не гарантируется.

G0 записывает 64 бита, назначение выровнено на 8 байт; источник может быть
невыровненным, хвост дополняется FF. F4/F7 при 3–3,3 В записывают 8/16/32 бита;
64-битный вход разбивается на две операции по 32 бита без VPP. Кеш изменяемой
Flash инвалидируется; глобальная очистка D-cache SRAM не выполняется.
Перед прямыми операциями `init()` проверяется один раз. Аргументы прямого `write()`
проверяет вызывающий код: источник, переполнение размера/округления, диапазон и
выравнивание. Ширина выбирается при компиляции; область журнала проверяет `SetUp()`.

### flash_data — структура в одном секторе

```cpp
uint8_t settings_buf[32]{};
flash_data settings(settings_sector, settings_buf, sizeof(settings_buf));
if (settings.SetUp() != SysInitStatus::InitOK) return;
const auto loaded = settings.read_data();
// Изменить settings_buf, затем проверить результат сохранения.
const SysStatus saved = settings.write_data();
if (saved != SysStatus::OK) {
    const uint32_t flags = FLASH->SR;
    (void)flags; // Посмотреть до начала следующей операции Flash.
}
```

Порядок записи: **DATA_WRITING → данные → DATA_OK**. Только после успешного
сохранения новой записи предыдущая помечается невалидной. Поиск выбирает последнюю
готовую и пропускает незавершённые/невалидные слоты. При заполнении сектор стирается:
потеря питания во время стирания/перезаписи может уничтожить последнюю версию.

F4/F7 сохраняют однобайтный заголовок: `FF → 7F → 0F`, затем `00` для инвалидации.
G0 использует **16-байтный заголовок**: первый блок — все FF → `00000000FFFFFFFF`
→ полный ноль; второй остаётся FF до инвалидации, затем в него записывается
`FFFFFFFF00000000`. Маркер второго блока имеет приоритет.
Все значения определены один раз в `Data_Status`; запись и поиск используют этот enum.
Численные значения enum G0 изменены; сравнивайте именованные значения, а не числа.
`DATA_CORRUPT` удалён, незавершённые/невалидные записи дают `DATA_NOT_VALID`.
При ошибке инвалидации предыдущей записи `write_data()` возвращает ошибку,
но новая готовая запись остаётся выбранной.

Прежний 8-байтный заголовок G0 несовместим: для проверки нового формата нужен
стёртый сектор журнала. Проверку на железе выполняет пользователь.
Формат, ограничения таймаута и ECC/NMI описаны в [Flash.md](Flash.md).

---

## 12. DAC

**Файлы:** `dac.hpp`, `dac.cpp`
**Подробнее:** [DAC.md](DAC.md)

Подключение: `"drivers": ["DAC"]` в project.json. CH1 — PA4, CH2 — PA5 при наличии.

```cpp
constinit DAC_Module output(DAC_Module::_1::instance, DAC_Module::CHANNEL::CH1);
// После настройки System/RCC:
if (output.SetUp() == SysInitStatus::InitOK) {
    SysStatus written = output.SetValue(2048); // Проверить written == SysStatus::OK.
}
output.DeInit();
```

SetUp задаёт прямой 12-битный вывод с буфером и начальным кодом ноль.
SetValue возвращает NotInit до настройки/после DeInit, Error при значении >4095
без изменения кода, OK после volatile-записи. Аналоговое время запуска/установления
учитывает приложение по datasheet выбранного МК.

Повторный SetUp требует DeInit. DeInit отключает только выбранный канал,
сохраняя второй канал, тактирование DAC/GPIO и analog-настройку пина.
Копирование/перемещение запрещены, деструктор пассивный. У F410 есть только CH1;
подключение модуля на МК без DAC даёт понятную ошибку компиляции.

---

## Порядок инициализации в main()

```cpp
int main() {
    System::Init();      // 1. FPU, Flash latency, SysTick

    constexpr auto clocks = ClockSystem::CalcPLL(64 * MHz, ClockSystem::PLL_ClockSource::HSI);
    if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
        System::DebugTrap("Clock init");

    // 3. Периферия — в любом порядке
    uart.SetUp();
    spi.SetUp(...);
    rtc.SetUp(...);

    // 4. DMA закреплены конструкторами; их настраивают UART/SPI SetUp и TIM SetUpPWM.
    tim_pwm.SetUp(...);
    tim_pwm.SetUpPWM(TIM::Channel::CH1);

    // 5. Запуск таймеров, которые требуют явного Start()
    periodic_timer.Start();

    for (;;) { /* main loop */ }
}
```

---

## Зависимости между модулями

```
system.hpp
    └── rcc.hpp
    └── gpio.hpp
    └── irq_registry.hpp
            └── dma.hpp
                    ├── uart.hpp
                    ├── spi.hpp
                    └── tim.hpp
                            └── rtc.hpp (использует irq_registry.hpp)
```

`flash.hpp` зависит только от `system.hpp` и `flash_config.h`.
`dac.hpp` зависит от `system.hpp` и `gpio.hpp`.

---

## Добавление нового МК

1. Добавить `#include "stm32xxxx.h"` в `system.hpp`.
2. Добавить записи в `PeriphInfo`-таблицы в `uart.cpp`, `spi.cpp`, `tim.cpp`, `dma.cpp`.
3. Добавить стабы в `irq_registry_config.h`.
4. Добавить проверенные частотные лимиты и особенности PLL/VOS в `rcc_defs.hpp`.
5. При необходимости добавить макросы регистровых абстракций (например `RCC_GPIO_EN_REG` в `gpio.hpp`).
</content>
