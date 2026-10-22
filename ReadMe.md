# Drivers — STM32 Peripheral Driver Library

## Constant initialization / Постоянная инициализация

**EN.** Peripheral objects are declared at file scope (or as `static` locals) with **`constinit`**; their constructors are `constexpr`. The first argument is `_N::instance` from the driver's `*_defs.hpp`; pins and DMA routes are named constants (`_N::TX::PA9`, `_N::DMA::TX_DMA2_Stream7`); on G0 the DMA channel is a number, `DMAx_Channely_BASE`. A wrong binding is a **compile error** whose message contains the reason (`STM32_CONFIG_ERROR`). There is no separate Config object or template driver API. Hardware is touched only in `SetUp()`. Details: [Initialization.md](Initialization.md).

**RU.** Объекты периферии объявляются глобально (или как `static` внутри функции) с **`constinit`**; их конструкторы — `constexpr`. Первый аргумент — `_N::instance` из `*_defs.hpp` драйвера; выводы и маршруты DMA — именованные константы (`_N::TX::PA9`, `_N::DMA::TX_DMA2_Stream7`); на G0 канал DMA задаётся числом `DMAx_Channely_BASE`. Неверная привязка — **ошибка компиляции**, в сообщении которой есть причина (`STM32_CONFIG_ERROR`). Отдельного Config и шаблонного API нет. К аппаратуре драйвер обращается только в `SetUp()`. Подробнее — порядок настройки, варианты с DMA и без, триггеры, каналы TIM, наследование и тексты ошибок: [Initialization.md](Initialization.md).

| | `constinit` object / объект с `constinit` | without / без `constinit` |
| --- | --- | --- |
| Wrong binding / Неверная привязка | compile error with the reason / ошибка компиляции с причиной | stops in `DebugTrap(reason)` at start-up / остановка в `DebugTrap(причина)` при старте |
| Start-up code / Код при старте | none / нет | constructor runs / выполняется конструктор |

`__builtin_unreachable` is not used for these checks. In Release `DebugTrap` is an inline endless loop without the message. / `__builtin_unreachable` для этих проверок не используется. В Release `DebugTrap` — встроенный бесконечный цикл без вывода сообщения.

Every module document follows the same layout as [GPIO.md](GPIO.md): English/Русский, a table of contents per language, numbered sections, usage examples, edge cases and links to related modules. / Документация каждого модуля оформлена по образцу [GPIO.md](GPIO.md): English/Русский, оглавление для каждого языка, нумерованные разделы, примеры, угловые случаи и ссылки на связанные модули.

*[English](#english) · [Русский](#русский)*

---

## English

A C++20 library on top of CMSIS for the **STM32G0**, **STM32F4** and **STM32F7** families.

- **Core** (always built): System, RCC, GPIO, Flash, IRQ_Registry.
- **Optional modules:** UART, SPI, DMA, TIM, RTC, EXTI, ADC, DAC, I2C, MODBUS, CONTAINERS, PID, DSP, CRC.

The library is not a standalone CMake project: it is cloned and wired in by the [stm32_cmake_base](https://github.com/vsm-stm/stm32_cmake_base) platform (see "Integration and build").

**`DSP` module** — a subset of Arm's CMSIS-DSP v1.16.2 (not stored in this repository; fetched at configuration time with `FetchContent`). Enable it like any other module by adding `"DSP"` to `"drivers"`. Third-party licenses: [THIRD_PARTY.md](THIRD_PARTY.md).

| Group | Functions | Types |
| --- | --- | --- |
| Trigonometry | `arm_sin_*`, `arm_cos_*` | `f32`, `q15`, `q31` |
| Square root | `arm_sqrt_*` | `f32` (inline), `q15`, `q31` |
| Angle | `arm_atan2_*` | `f32`, `q15`, `q31` |
| Division | `arm_divide_*` | `q15`, `q31` |
| Vector logarithm | `arm_vlog_*` | `f32`, `f64`, `q15`, `q31` |
| Vector exponential | `arm_vexp_*` | `f32`, `f64` |
| Combined sine/cosine | `arm_sin_cos_*` | `f32`, `q31` |
| PID | `arm_pid_init_*`, `arm_pid_reset_*`, `arm_pid_*` (step, inline) | `f32`, `q15`, `q31` |
| Clarke/Park transforms | `arm_clarke_*`, `arm_inv_clarke_*`, `arm_park_*`, `arm_inv_park_*` (inline) | `f32`, `q31` |

- Include `dsp/fast_math_functions.h` and `dsp/controller_functions.h`. The general `arm_math.h` also declares groups whose sources are **not** built here (FFT, FIR/IIR, statistics…) — a declaration does not mean an implementation is linked. Float16 kernels are excluded; shared tables are built once.
- `arm_sin_f32`/`arm_cos_f32` take **radians**; `arm_sin_cos_f32` takes **degrees**.
- Initialize each CMSIS PID instance with `arm_pid_init_*(..., 1)` before the first step.
- Check the returned `arm_status` of square root, division and atan2. Ranges and fixed-point formats: Arm's [FastMath header](https://github.com/ARM-software/CMSIS-DSP/blob/v1.16.2/Include/dsp/fast_math_functions.h) and [Controller header](https://github.com/ARM-software/CMSIS-DSP/blob/v1.16.2/Include/dsp/controller_functions.h).

**`PID` module** — the in-house PID controller (`math/PID.*`, C API callable from C++): variable step `dT`, dead zone `e_lim`, symmetric integral clamp `max_ki_I`, and the helper `PID_get_quality`.

- Zero-initialize the controller state or call `zero_PID_I_D()` before use. Set `e` on each step; `dT` is in seconds.
- Inputs must be finite, `dT > 0`, `e_lim`/`max_ki_I >= 0`. `dT == 0` returns zero without updating state. With `ki == 0`, `I` is held.
- Inside the dead zone `|e| <= e_lim`: `I` is held, `D` is zero, `e_old` is updated; the proportional term stays active.
- `PID_get_quality`: reset with `get=0` before a measurement and when changing setpoint or mode.
  - `get=1`: `e` is a percentage for non-zero `u` (absolute `y-u` for `u = 0`); `t_time` sums time outside the ±5 % corridor.
  - `get=2`: `e` is absolute; `t_time` sums every supplied `dt`.
  - This is accumulated time, not settling time. `max_y` tracks the maximum response for `u >= 0` and the minimum for `u < 0`, starting from zero; `sigma = (max_y - u)/u` (zero for `u = 0`).

## Driver API migration — 19.10.2026

- **DMA ownership.** USART/SPI/ADC/I2C and TIM channel bindings take the DMA selection in the constructor and own their `DMA_Sx` objects. F4/F7 use named routes with controller/stream in the name; G0 takes only the physical channel, and the peripheral supplies the request ID.
- **Interrupts.** Peripheral and DMA handlers are registered separately; no handler waits for hardware. SPI still finishes asynchronous transfers through `Process()` in the main loop.
- **`*_USE_ATOMIC`.** `UART_USE_ATOMIC`, `SPI_USE_ATOMIC`, `ADC_USE_ATOMIC`, `TIM_USE_ATOMIC`, `I2C_USE_ATOMIC`, `DAC_USE_ATOMIC`, `DMA_USE_ATOMIC` and `EXTI_USE_ATOMIC` are set in their headers. All default to **0** (plain read-modify-write); 1 selects ST `ATOMIC_*` macros. Rebuild drivers and application after a change. Atomic single updates do not serialize a whole transfer or buffer handover. There is no CMake option.
- **DMA flags.** `GetIRQFlags()` returns HT/TC/errors in one ISR read; decode with `FLAG_HT`, `FLAG_TC`, `FLAG_ERRORS` (FEIF is excluded in direct mode).
- **I2C.** Constructor-bound DMA, inherited callbacks, one-time setup and `DeInit()`; bus recovery is application code. Timing is computed at compile time: `SetUp<clocks, I2C::SPEED::FAST>()` takes the same `ClockSystem::Config` as RCC; before touching hardware it checks `System::APB1BusClock`. See [I2C.md](I2C.md).

## Integration and build

Drivers are enabled with one line in the `project.json` of a project built on `stm32_cmake_base`:

```json
"drivers": ["UART", "SPI", "DMA", "TIM"]
```

| Value | Result |
| --- | --- |
| `null` / `false` / key absent | drivers are not built at all |
| `true` or `[]` | core only (System, RCC, GPIO, Flash, IRQ_Registry) |
| `["UART", ...]` | core + the listed modules; a misspelt name is a configuration error listing the valid names |

**Version.** `"drivers_tag"` in `project.json` selects what is checked out: a tag, a branch or a commit (absent/`null` → `main`). A branch follows its latest commit on each fresh configure. The platform clones the repository into `Drivers/` on configuration and replaces it entirely when the selection changes — **do not keep local edits there**.

**Wiring.** The `stm32_platform` target (flags, CMSIS, chip defines) is linked by `stm32_drivers`. Chip-specific configs — `flash_config.h` (sector map) and `irq_registry_config.h` (vector stubs) — are generated by the library into `cmsis-core/drivers_gen/`.

**Headers.** Core: `<system.hpp>`, `<rcc.hpp>`, `<gpio.hpp>`, `<flash.hpp>`, `<irq_registry.hpp>`. Optional: `<uart.hpp>`, `<spi.hpp>`, `<tim.hpp>`, … (see the commented block in the platform's `inc/main.hpp`).

### Modules and verification status

Status from the October 2026 review: driver modules were **compiled** (debug and release, F4/F7/G0 test devices, all driver modules enabled) without warnings. Compilation does not confirm behaviour on real hardware.

| Module | Files | F4 | F7 | G0 |
| --- | --- | --- | --- | --- |
| Core: System, RCC, GPIO, Flash, IRQ_Registry | `src/system.*`, `rcc.*`, `gpio.*`, `flash.*`, `irq_registry.*` | builds | builds | builds |
| UART, SPI | `src/uart.*`, `spi.*` | builds | builds | builds |
| TIM | `src/tim.*` | builds | builds | builds |
| DMA | `src/dma.*` | builds | builds | builds |
| RTC | `src/rtc.*` | builds | builds | builds |
| EXTI | `src/exti.*` | builds | builds | builds |
| ADC | `src/adc.*` | builds | builds | builds |
| DAC | `src/dac.*` | builds | builds | builds |
| I2C | `src/i2c.*` | builds | builds | builds |
| MODBUS | `Interfaces/` | builds | builds | builds |
| CONTAINERS | `Containers/` (headers) | included | — | — |
| PID | `math/PID.*` | ok | not tested | not tested |
| DSP | subset of CMSIS-DSP v1.16.2 | ok | not tested | not tested |
| CRC | `crc/crc32.*` (no tables, no hardware peripheral — portable) | ok | ok | ok |

Behaviour and limitations of each module are in its own document (e.g. [RTC.md](RTC.md), [ADC.md](ADC.md), [DAC.md](DAC.md), [DMA.md](DMA.md)).

Families other than F4/F7/G0 (including G4) are not supported: set `"drivers": null` and write against registers directly, or port the library (see "Adding a new MCU").

---

## Detailed module documentation

This file is an overview with short examples. The full description of each module — registers, timing, edge cases — is in its own file:

| Document | Contents |
| --- | --- |
| [Initialization.md](Initialization.md) | `constinit` declarations for every module, compile-time diagnostics |
| [System.md](System.md) | `SysStatus`/`SysInitStatus`, clock variables, `GetTick()`/`Delay_us()`, cache/MPU, `DebugTrap()` |
| [RCC.md](RCC.md) | clock tree: HSI/HSE/PLL, bus dividers, Flash latency, compile-time validation |
| [GPIO.md](GPIO.md) | `PIN`/`PinArray`: mode encoding, registers, bit-banding |
| [IRQ_Registry.md](IRQ_Registry.md) | the single interrupt dispatcher, registration with and without inheritance, capacity |
| [DMA.md](DMA.md) | `DMA_Sx`: streams (F4/F7) and channels + DMAMUX (G0), request tables |
| [UART.md](UART.md) | `USART`: blocking/IRQ/DMA, packet reception on line IDLE |
| [SPI.md](SPI.md) | `SPI`: master/slave, software/hardware NSS, DMA, `Process()` |
| [I2C.md](I2C.md) | master, blocking/IRQ/DMA, callbacks, STOP handling, application recovery |
| [TIM.md](TIM.md) | common TIM: PWM/capture, encoder, pulse measurement, ITR, pulse series |
| [Modbus.md](Modbus.md) | RS-485 transport, CRC, frame formats, IDLE limits |
| [RTC.md](RTC.md) | `RTC_cl`: calendar, WakeUp timer, alarms, timestamp, calibration |
| [EXTI.md](EXTI.md) | external GPIO interrupts, inherited callback, shared-vector limits |
| [DAC.md](DAC.md) | `DAC_Module`: direct 12-bit output, statuses, channel lifetime |
| [Flash.md](Flash.md) | `flash_base`/`flash_data`: unlock, erase, program, one-sector record journal |

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

### Fixed peripheral descriptions (Instance)

USART/SPI/I2C/TIM/ADC/DAC receive a static `_N::instance` from their defs header. It describes:

- numeric peripheral and RCC register addresses;
- clock-enable/reset masks and the bus-clock variable;
- IRQ numbers and block capabilities.

`SetUp()` uses it directly — no peripheral table is scanned.

```cpp
using U = USART::_3;
constinit USART port(U::instance, 115200, U::TX::PD8, U::RX::PD9);
```

**State kept by the object:** one reference to the instance and one cached register pointer — nothing duplicated. The register pointer also marks initialization: null before setup, after a failed first setup and after `DeInit()`. A repeated `SetUp()` on a working object returns an error and leaves it working. Interrupt handlers use the cached pointer. DMA keeps its own hardware descriptor table and ownership flags.

### Compile-time pin validation

Pins come from nested structures:

```cpp
constinit USART debug(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7);
```

Each pin such as `USART::_1::TX::PB6` carries its owning peripheral's base address. A pin of another peripheral fails `constinit` construction through `STM32_CONFIG_ERROR`; GCC's message contains the reason (e.g. `USART: TX pin belongs to another peripheral`). Without `constinit`, the constructor calls `DebugTrap(reason)` at start-up and stops. The deprecated CMSIS-pointer USART constructor still checks at run time.

### IRQ_Registry — a single interrupt dispatcher

Every interrupt goes through one dispatcher: the generated stubs in `irq_registry_config.h` call `IRQ_Registry::Dispatch(IRQn)`, which calls the registered `IIRQHandler` objects.

- Drivers register themselves in `SetUp()` and unregister in `DeInit()` — you never write `*_IRQHandler` functions for them.
- `IRQ_en()` only enables/disables the peripheral's interrupt sources; it does not change registrations.

---

## 2. System

**Files:** `system.hpp`, `system.cpp` · **Details:** [System.md](System.md)

The base module: FPU, Flash accelerator/caches, SysTick, delays, cache helpers, `DebugTrap()`.

### Initialization

```cpp
System::Init();  // core features + SysTick (1 kHz) from HSI 16 MHz
```

Call it **first** in `main()`, before `ClockSystem::Init<config>()`, and check the returned `SysInitStatus`.

- **Normal start:** assumes the hardware-reset state of clocks, power and Flash latency. Call once, before changing clocks.
- **After a bootloader** (`STM32_USE_BOOTLOADER`, defined automatically when `bootloader` is set in `project.json`): F4/F7/G0 switch back to HSI 16 MHz with bus dividers /1; G0 also restores VOS Range 1 before Flash 0 WS. The HSI wait has no timeout; later waits (VOS/Flash, 100 ms) need interrupts enabled. PLL/HSE settings and RCC interrupts are kept.
- **L0** keeps the legacy assumption of HSI 16 MHz and restores nothing.
- The base defines `STM32_STARTUP_ENABLES_FPU`, so `Init()` skips FPU setup already done by startup. With another startup, drop the definition unless it enables the FPU itself.
- On F7 the core caches are enabled; MPU and external-memory access: [System.md](System.md#6-mpu-on-stm32f7).

### Working with time

```cpp
uint32_t t = System::GetTick();   // ms since start-up
System::Delay_ms(500);            // blocking 500 ms delay

// Non-blocking timer (wrap-safe subtraction):
uint32_t start = System::GetTick();
if ((System::GetTick() - start) > 1000) { /* one second has passed */ }
```

### Microsecond delays (F4/F7)

```cpp
System::Enable_CYCCNT();   // once at start-up
System::Delay_us(10);      // 10 µs via DWT
```

- `Delay_ms()` needs a running SysTick that can interrupt the caller — it cannot wait with ticks masked or inside an IRQ of equal/higher priority. `GetTick()` itself can be read anywhere. The wait is capped at `0x7FFFFFFF` ticks (~24.9 days at 1 kHz).
- `Delay_us()` uses 64-bit arithmetic and is capped at `0x7FFFFFFF` core cycles. **G0/L0 have no DWT** — `Delay_us()` does not exist there.
- `SWOTrace()` (F4/F7) is non-blocking and returns the number of bytes ITM accepted.
- `DebugTrap()` in Debug saves the message, prints it over blocking SWO (if enabled in the debugger), executes BKPT and halts; in Release it only halts. `SetDebugOutput()` was removed.

### Non-cacheable DMA areas

F7 projects can reserve non-cacheable DMA buffers via the base's `dma_regions` setting (`name`, `size`, `memory`: SRAM1/SRAM2, `mpu_region`). Addresses are automatic; startup zeroes the buffers; `System::Init()` programs their MPU attributes before enabling the D-cache. Place buffers in `.dma.<name>`. See [System.md](System.md).

Cache helpers for cacheable buffers: `System::CacheClean`, `CacheInvalidate`, `CacheCleanInvalidate` (address + byte count). F7 skips ranges wholly inside DTCM or configured non-cacheable areas; on F4/G0/L0 they are empty. Peripheral drivers call them at the right moments; direct DMA users call them themselves. RX buffers must own every 32-byte cache line they touch.

### Bus frequency variables

Published by `ClockSystem::Init<config>()`:

```cpp
System::SystemCoreClock  // core (AHB) frequency
System::APB1BusClock     // APB1
System::APB2BusClock     // APB2 (0 on G0)
System::TIMxAPB1Clock    // timers on APB1
System::TIMxAPB2Clock    // timers on APB2
```

---

## 3. RCC / ClockSystem

**Files:** `rcc.hpp`, `rcc.cpp`, `rcc_defs.hpp` · **Details:** [RCC.md](RCC.md)

One clock setup per hardware reset. The configuration is a `constexpr` value; `Init<config>()` validates and precomputes everything at **compile time**. Order: `System::Init()` → `ClockSystem::Init<config>()` → peripherals.

### HSI without PLL

```cpp
constexpr ClockSystem::Config clocks{};
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
    System::DebugTrap("Clock init");
```

### Exact automatic PLL selection

Example — G0, SYSCLK 64 MHz from HSI:

```cpp
constexpr auto clocks = ClockSystem::CalcPLL(
    64 * MHz, ClockSystem::PLL_ClockSource::HSI);
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
    System::DebugTrap("Clock init");
```

- For HSE pass the crystal frequency as the 3rd argument; the 4th is Q (default 2).
- `CalcPLL` finds an **exact** SYSCLK with AHB /1 and legal APB dividers, preferring the highest legal PLL input frequency, then the smallest P/R.
- No exact solution → build error at `Init<clocks>()`.

### Manual configuration

F407, HSE 8 MHz, SYSCLK/HCLK 168 MHz, APB1 42 MHz, APB2 84 MHz:

```cpp
constexpr ClockSystem::Config clocks{
    .ClkSrc  = ClockSystem::SystemClockSource::PLL,
    .HSE_Clk = 8 * MHz,
    .BusDiv  = {ClockSystem::AHB_Divider::DIV1,
                ClockSystem::APB_Divider::DIV4,
                ClockSystem::APB_Divider::DIV2},
    .PLLCfgr = {ClockSystem::PLL_ClockSource::HSE, 4, 168, 2, 7, 2},   // M, N, P, Q, R
};
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
    System::DebugTrap("Clock init");
```

- Supported: every F4/F7 selector in STM32-base_files, and G0 (which also supports LSI/LSE).
- VDD is assumed 3.0 V (same timing band as 3.3 V); voltage scaling, over-drive and latency follow the device/HCLK limits.
- Run-time errors are only hardware timeouts or a repeated call; a failed attempt also requires a reset.
- SysTick uses a whole number of HCLK cycles — e.g. LSE 32768 Hz with AHB /1 gives 1024 ticks/s, and `Delay_ms`/timeouts inherit that error. Limits, ordering and migration from `Init(...)`/`InitCalcPLL(...)`: [RCC.md](RCC.md).

---

## 4. GPIO / PIN

**Files:** `gpio.hpp`, `gpio.cpp` · **Details:** [GPIO.md](GPIO.md)

| Constructor | Use | Checks |
| --- | --- | --- |
| `PIN(GPIOA, 5)` (CMSIS pointer) | ordinary GPIO objects | none |
| `PIN(GPIOA_BASE, 5, af, periph)` (`consteval`) | compile-time pin tables, `constinit` objects | port, pin number and AF checked at build time |

- Port/number/stored AF are private and immutable; `periph_base` is read-only. Copy construction is allowed, rebinding by assignment is not. An AF override can be passed to `SetUp()`.
- ADC (`ADC_PIN`, names like `PA0_IN0`) and TIM (`TIM_PIN`) use compile-time-checked pin descriptors built on the same mechanism.

### Creating and configuring a pin

```cpp
PIN led(GPIOB, 8);                        // port + pin number
PIN btn(GPIOA, 0);

led.SetUp(PIN::TYPE::OUTPUT_PushPull);    // push-pull output
btn.SetUp(PIN::TYPE::INPUT_PullUp);       // input with pull-up

led.SetLevel(true);      // HIGH
led.SetLevel(false);     // LOW
led.TogglePin();         // toggle

bool state = btn.GetLevel();   // read the input
```

### All pin types

| Constant | Description |
| --- | --- |
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

Outputs start **LOW** by default (written through BSRR before the mode is enabled). To start HIGH:

```cpp
led.SetUp(PIN::TYPE::OUTPUT_PushPull, PIN::OUTPUT_SPEED::Low, PIN::LVL::HIGH);
```

`TogglePin()` is a read-modify-write of ODR — do not change other outputs of the same port from an IRQ during it. (`SetLevel()` uses BSRR and is safe.)

### Alternate function

```cpp
PIN sck(GPIOA, 5);
sck.SetUp(PIN::TYPE::AF_PushPull, 5);   // AF5 = SPI1 on STM32F429
```

Peripheral drivers configure their own pins with the AF stored in the pin tables.

### Assignment and conversion operator

```cpp
led = true;       // SetLevel(true)
led = false;      // SetLevel(false)
bool b = (bool)led;   // GetLevel() — the conversion is explicit
```

### IsValid()

`PIN{}` (default constructor) returns `IsValid() == false`. Drivers use it as "signal not connected":

```cpp
constinit SPI spi(SPI::_1::instance, SPI::Direction::TX, sck, mosi, PIN{}, PIN{});   // MISO and SS unused
```

### PinArray — an array of pins

```cpp
PIN segments[] = {
    PIN(GPIOA_BASE, 0), PIN(GPIOA_BASE, 1), PIN(GPIOA_BASE, 2), PIN(GPIOA_BASE, 3)
};
PinArray<4> seg_pins(segments);

seg_pins.SetUpAll(PIN::TYPE::OUTPUT_PushPull);
seg_pins.SetLevelAll(0b1010);              // bit i → pin i
uint32_t state = seg_pins.GetLevelAll();   // read all at once
```

`SetUpAll()` configures every pin and returns the first error unchanged. Pins may be on different ports.

---

## 5. IRQ_Registry

**Files:** `irq_registry.hpp`, `irq_registry.cpp`, `irq_registry_settings.hpp`; generated `irq_registry_config.h` · **Details:** [IRQ_Registry.md](IRQ_Registry.md)

Generated stubs call `Dispatch(irqn)`, which calls the registered `IIRQHandler` objects (or adapters for plain functions). One handler object serves all sources of a peripheral; different peripherals can share a vector.

**`Register()`** returns `InitOK` or `InitError` (invalid/null input, duplicate on the same IRQ, full row or function pool). A different `ctx` does not allow registering the same function twice — unregister first.

```cpp
if (IRQ_Registry::Register(TIM3_IRQn, &handler) == SysInitStatus::InitOK)
    NVIC_EnableIRQ(TIM3_IRQn);

// Later, from main, with this owner's sources disabled:
NVIC_DisableIRQ(TIM3_IRQn);
IRQ_Registry::Unregister(TIM3_IRQn, &handler);
if (IRQ_Registry::HasHandlers(TIM3_IRQn)) NVIC_EnableIRQ(TIM3_IRQn);
```

- Plain functions: `Register(irqn, fn, ctx)` — `ctx` is passed to `fn(ctx)`. Cancel with `Unregister(irqn, fn)`; the adapter is freed.
- `Unregister(irqn)` (one argument) clears the whole row.
- **Never** call `Register`/`Unregister` from `HandleIRQ()` or callbacks. Change the registry from `main` with the vector masked. A callback may disable its own peripheral source and leave unregistering to `main`.

### Handler capacity per IRQ

Settings live in `Drivers/src/irq_registry_settings.hpp` (shared by all source files). `IRQ_MAX_SHARED` defaults to **3**; for five handlers per vector:

```cpp
#define IRQ_MAX_SHARED 5
```

Each extra slot costs `IRQ_TABLE_SIZE × 4` bytes of RAM. The separate `IRQ_MAX_FUNCTION_HANDLERS` pool defaults to 8. Rebuild the library and images after a change; template updates are described in the detailed document.

---

## 6. DMA

**Files:** `dma.hpp`, `dma.cpp`, `dma_requests.hpp` · **Details:** [DMA.md](DMA.md)

One API for F4/F7 streams (`DMA_Stream_TypeDef`) and G0 channels (`DMA_Channel_TypeDef` + DMAMUX). Peripheral drivers embed their own `DMA_Sx` — the examples below are for standalone use.

- `SetUp()` accepts zero count/addresses (prepare without starting). It rejects an owned channel, invalid fields, misaligned addresses and counts above 65535.
- `SetCount()` takes 1…65535 **elements**. Address/count setters return `NotInit` before setup, `Busy` while EN is set, `Error` for invalid arguments — check them.
- `Stream_EN(DISABLE)` waits for EN = 0 without a timeout (main loop only).
- `Release()` stops, resets and frees the resource; constructor selection is kept. Before releasing, disable the peripheral's DMA requests and unregister its handler. NVIC lines and controller clocks stay enabled.
- Descriptors live in the `static const dma_streams` table in Flash; only the `dma_used` ownership flags use RAM.
- Cache helpers are in `System`; the consumer decides when and over which range to call them.

### Constructor (STM32G0)

DMAMUX1 connects any channel to any peripheral. You choose the channel; the request comes from the peripheral's `_N::DMA`:

```cpp
// Option 1: channel + named request (recommended)
constinit DMA_Sx dma_tx(DMA1_Channel4_BASE, USART::_1::DMA::TX);
constinit DMA_Sx dma_rx(DMA1_Channel5_BASE, USART::_1::DMA::RX);

// Option 2: channel + raw DMAMUX1 request ID
constinit DMA_Sx dma_raw(DMA1_Channel3_BASE, 51u);   // USART1_TX

// Option 3: channel only (request supplied later via StreamSettings::channel)
constinit DMA_Sx dma_late(DMA1_Channel3_BASE);
```

### Constructor (STM32F4/F7)

The stream and CHSEL are fixed by silicon and encoded in the route name:

```cpp
constinit DMA_Sx dma_tx(USART::_1::DMA::TX_DMA2_Stream7);   // CH4
constinit DMA_Sx dma_rx(USART::_1::DMA::RX_DMA2_Stream5);   // CH4
```

### Configuration via StreamSettings

```cpp
DMA_Sx::StreamSettings cfg;
cfg.direction   = DMA_Sx::DIR::To_Per;    // memory → peripheral
cfg.data_size   = DMA_Sx::SIZE::Byte;     // element width
cfg.minc        = true;                   // increment the memory address
cfg.pinc        = false;                  // fixed peripheral address
cfg.per_address = reinterpret_cast<uint32_t>(&USARTx->TDR);
cfg.count       = 0;                      // set before each transfer

if (dma.SetUp(cfg) != SysInitStatus::InitOK) { /* handle error */ }
```

### Manual transfer start

```cpp
dma.ClearFlags();
dma.SetMemAddr(reinterpret_cast<uint32_t>(buf));   // check SysStatus
dma.SetCount(len);                                  // check SysStatus
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
        _dma.RequestStop();   // TC has already confirmed completion
        _dma.ClearFlags();
        __DMB();
        // transfer complete
    }
}
```

### Directions

| `DIR` | Meaning |
| --- | --- |
| `From_Per` | Peripheral → Memory |
| `To_Per` | Memory → Peripheral |
| `Mem_To_Mem` | Memory → Memory, G0 only; F4/F7 return `InitError` |

### Element width

| `SIZE` | Size |
| --- | --- |
| `Byte` | 8 bits |
| `Half_Word` | 16 bits |
| `Word` | 32 bits |

### Important: shared IRQs on G0

| Channels | Vector |
| --- | --- |
| CH1 | `DMA1_Channel1_IRQn` (exclusive) |
| CH2, CH3 | `DMA1_Channel2_3_IRQn` (shared) |
| CH4, CH5 | `DMA1_Ch4_5_DMAMUX1_OVR_IRQn` (shared) |

The last row is for 5-channel devices. 7-channel DMA1 devices use `DMA1_Ch4_7_DMAMUX1_OVR_IRQn`; devices with DMA2 use `DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX1_OVR_IRQn` for DMA1 CH4–7 and DMA2 CH1–5. The driver picks the right vector from the device header.

Several channels on one vector is fine — `IRQ_Registry` calls every registered handler, and each checks only its own flags (mind `IRQ_MAX_SHARED`).

---

## 7. USART

**Files:** `uart.hpp`, `uart.cpp`, `uart_defs.hpp` · **Details:** [UART.md](UART.md)

**Key facts:**

- Oversampling ×16; BRR is rounded and validated; `SetBaud()` returns `SysStatus`. Before `SetBaud()`, wait for physical TX completion (TC / `OnTC()`).
- **Constructor:** `USART(instance, baud, tx, rx, dma_tx = {}, dma_rx = {} [, clock_source])`. F7/G0 accept `USART::ClockSource` (PCLK default, SYSCLK, HSI, LSE where supported); the caller starts HSI/LSE, `SetUp()` only checks readiness. F4 always uses PCLK.
- **Lengths:** 0 → `Error`; IRQ/DMA transfers 1…65535 bytes.
- **DMA** objects live inside the USART; with `constinit` their routes are checked at compile time; `SetUp()` acquires and configures them. USART and both DMA directions have separate handlers.
- **Packet end on IDLE:** F4/F7 — IDLE requests a DMA stop, the DMA RX interrupt publishes count/cache/ready (no waiting, no `Process()`). G0 — IDLE disables DMAR/DMA without waiting and completes in the USART interrupt; the last DMA request must already be serviced. Failed packets are discarded.
- **Reading a packet:** `GetDataReceivedCount()` no longer clears anything — call `ClearDataReceivedFlag()` after consuming the packet.
- **Errors** stop the affected operation and call `OnError(mask)`.
- **Bounded IRQ work:** at most the FIFO depth per direction per entry (8 words on G0 with FIFO, otherwise 1 byte).
- F7 cache maintenance goes through System and skips configured non-cacheable areas.

### Basic usage

```cpp
constinit USART debug(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7);

if (debug.SetUp() == SysInitStatus::InitOK) {
    debug.Send((uint8_t*)"Hello\r\n", 7);

    uint8_t buf[16];
    debug.Receive(buf, sizeof(buf), 500);   // 500 ms timeout for the whole call
}
```

### TX only (no RX)

```cpp
constinit USART log(USART::_2::instance, 115200, USART::_2::TX::PA2, PIN{});
log.SetUp();
log.Send((uint8_t*)"OK\r\n", 4);
```

### DMA transfer

```cpp
// G0: physical channels; on F4/F7 pass named routes such as USART::_1::DMA::TX_DMA2_Stream7.
constinit USART uart(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7,
                     DMA1_Channel4_BASE, DMA1_Channel5_BASE);

if (uart.SetUp() == SysInitStatus::InitOK) {        // acquires/configures the embedded DMA
    uart.Send_DMA(tx_buf, sizeof(tx_buf));
    uart.Receive_DMA(rx_buf, sizeof(rx_buf));       // completes on IDLE or when the buffer fills
}
```

### Interrupts

```cpp
uart.IRQ_en(USART::IRQ::RXNE, ENABLE);   // enable the RXNE source
uart.IRQ_en(USART::IRQ::TXE, DISABLE);   // disable the TXE source
```

`SetUp()` registers the UART interrupt once. `IRQ_en()` changes only peripheral sources (also allowed from callbacks). `DeInit()` removes only this object's registrations.

### Custom interrupt handling (inheritance)

```cpp
class MyUART : public USART {
    using USART::USART;   // inherit the constructor
    void OnRxByte(uint8_t b) override { USART::OnRxByte(b); ring_buf.push(b); }
    void OnIdle() override { USART::OnIdle(); process_packet(); }
};

constinit MyUART uart(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7);
```

Call the base implementation from overrides of `OnRxByte`/`OnTxEmpty`/`OnIdle`/`OnDma*Complete`.

### Changing baud between transfers

```cpp
uart.SetBaud(9600);   // after TC; check SysStatus
```

---

## 8. SPI

**Files:** `spi.hpp`, `spi.cpp`, `spi_defs.hpp` · **Details:** [SPI.md](SPI.md)

- SPI1–SPI6 on F4/F7 and SPI1–SPI3 on G0, where present in CMSIS.
- `Direction::TX`/`RX`/`TXRX` and optional DMA are fixed in the constructor; clock/mode/frame settings go to `SetUp()`.
- Polling, IRQ and DMA have separate start paths. Starts check required buffers and status `OK`; DMA also checks the required DMA selection. Count (1..65535), buffer width/alignment and operation/pin compatibility are caller preconditions. Common preparation only assigns software state; DMA IRQ sources are configured once in `SetUpDMA()`.
- After a transfer leaves status `Error`/`Timeout`, use `DeInit()` and `SetUp()` to recover. Missing-buffer/DMA rejections do not change object status. DMA preparation does not access NVIC.
- `TX` uses transmit-only hardware and one TX DMA. `RX` master uses full duplex: RX DMA plus the TX interrupt for `0xFF`/`0xFFFF` dummy clocks.

### Basic usage (blocking mode)

```cpp
constinit SPI spi(SPI::_1::instance, SPI::Direction::TXRX, SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7,
                  SPI::_1::MISO::PA6, PIN(GPIOB_BASE, 0));   // optional automatic software CS
if (spi.SetUp(SPI::Master_sel::Master, SPI::NSS_ctrl::Software,
              SPI::Data_frame_format::Byte, SPI::Frame_Format::MSB,
              SPI::cPolPha::None, SPI::BaudRate::DIV4) == SysInitStatus::InitOK) {
    uint8_t tx[] = {0x12, 0x34};
    SysStatus result = spi.Send(tx, sizeof(tx));   // CS released on success and on error
}
```

For several devices pass the selected pin as the final `PIN* ss` argument:
`spi.Send(tx, len, 100, &device_cs)` or `spi.Send_DMA(tx, len, ENABLE, &device_cs)`.
Configure each pin output/high in the application and keep it alive until completion.
The driver asserts it just before SPE and releases it after completion/error/cancellation;
IRQ/DMA require `Process()`. `nullptr` uses the constructor SS; if absent, CS stays
under application control. Hardware NSS needs its matching pin; slave mode always
uses hardware NSS. The per-call software CS is ignored in both hardware-NSS modes.

### 16-bit frames

```cpp
// After a successful SetUp(Master, Software, Half_Word):
uint16_t tx[] = {0x1234, 0xABCD}, rx[2];
SysStatus result = spi.Send_Receive(tx, rx, 2);   // len counts frames, not bytes
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
// In main while an IRQ/DMA transfer is active:
SysStatus state = spi.Process();   // Busy until the last frame has left the wire
// After OK, RX is available and GetDataReceivedCount() returns the frame count.
```

- **DMA TC is not the end of the transfer** — `Process()` checks the SPI itself and then does cache, CS and callbacks.
- F7 cache handover goes through System; configured non-cacheable areas are skipped. Do not access an active RX buffer or modify TX.
- `OnComplete()`/`OnError(mask)` run inside `Process()` after cleanup. `IRQ_en()` changes CR2 sources; `SetUp()` owns registration and NVIC.
- **Slave:** `len` is capacity. The application configures a rising-edge EXTI on NSS and calls `NssReleased()` from it; `Process()` completes the packet in `main`. Extra frames → `IsTruncated()`; after the TX payload, dummy values are sent. On G0 the last RX DMA request must be serviced before the NSS stop. See [SPI.md](SPI.md) for priorities and re-arm rules.

### Frequency divider (BaudRate)

| Constant | Divider |
| --- | --- |
| `DIV2` | PCLK / 2 |
| `DIV4` | PCLK / 4 |
| `DIV8` | PCLK / 8 |
| ... | ... |
| `DIV256` | PCLK / 256 |

### Polarity/phase (cPolPha)

| Constant | CPOL | CPHA | SPI Mode |
| --- | --- | --- | --- |
| `None` | 0 | 0 | Mode 0 |
| `cPha` | 0 | 1 | Mode 1 |
| `cPol` | 1 | 0 | Mode 2 |
| `cPolPha` | 1 | 1 | Mode 3 |

---

## 9. TIM

**Files:** `tim.hpp`, `tim.cpp`, `tim_defs.hpp` · **Details:** [TIM.md](TIM.md)

One `TIM` class owns the counter base and configures each channel independently. Pins and optional DMA come from up to four constructor bindings `{pin, dma}`. There are no separate `TIM_PWM`/`PeriodicIRQ`/`InputCapture` classes, no `Output` objects and no `AttachDMA()` (the old classes remain as comments in `tim.hpp`/`tim.cpp`).

### Base, PWM and compare

```cpp
void Tick();
void HalfTick();
constinit TIM timer(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7});
// In main, check every setup result before Start:
if (timer.SetUp(1000, &Tick, 1000) == SysInitStatus::InitOK          // 1 kHz Update, 1000 ticks
    && timer.SetUpPWM(TIM::Channel::CH1, 250) == SysInitStatus::InitOK   // 25 %
    && timer.SetUpPWM(TIM::Channel::CH2, 500) == SysInitStatus::InitOK   // 50 %
    && timer.SetCompareIRQ(TIM::Channel::CH3, 750, &HalfTick) == SysInitStatus::InitOK) {
    timer.Start();
}
timer.SetDuty(TIM::Channel::CH1, 75);   // check SysStatus
```

- `SetUp(frequency, callback, period)`: frequency = Update/PWM Hz (not the tick rate); period = counter ticks (ARR = period − 1); 0 = automatic. A repeated `SetUp()` needs `DeInit()`.
- All channels share PSC/ARR. Capture/counter modes need an explicit period. `GetCounterClock()` gives the real tick rate.
- PWM1 with CCR preload. A 16-bit PWM period is limited to 65535 ticks so that `CCR = period` gives exact 100 % (0 gives 0 %). `SetDuty()` clamps to 100 %, `SetCCR()` takes raw ticks; neither overwrites a CCR owned by DMA. `Start()` is always explicit.

### Capture and hardware trigger

```cpp
void Edge();
constinit TIM input(TIM::_3::instance, {TIM::_3::CH1::PA6});
// In main; target CNT clock: 100 Hz × 10000 ticks = 1 MHz.
if (input.SetUp(100, nullptr, 10000) == SysInitStatus::InitOK
    && input.SetUpCapture(TIM::Channel::CH1, &Edge) == SysInitStatus::InitOK) {
    input.Start();
}
// Edge() reads input.GetCapture(TIM::Channel::CH1).
```

- Capture: direct TIx, rising edge. `GetCapture()` reads CCR directly; the callback only notifies. Wraps/overcapture are the application's job.
- `SetCompareIRQ()` with a null callback makes a pin-free hardware event without an interrupt.
- `SetUpTrigger(Trigger::Update / ComparePulse / OCxRef)` selects TRGO without interrupts. The ADC takes `TIM*` and its source in the constructor and starts the timer after arming ADC/DMA; configure the timer before ADC `SetUp()` — see [ADC.md](ADC.md).

### Independent DMA and WS2812B

```cpp
// G0; F4/F7 use TIM::_1::DMA::CH1_DMA2_Stream1 instead.
constexpr TIM::DMAReq dma = DMA1_Channel3_BASE;
constinit TIM ws(TIM::_1::instance, {TIM::_1::CH1::PA8, dma});
WS2812B_Strip<8> strip(52, 26);
strip.SetAll(RGB::Red());
// Example: CNT 64 MHz, period 80 ticks → 800 kHz, ARR = 79.
if (ws.SetUp(800000, nullptr, 80) == SysInitStatus::InitOK
    && ws.SetUpPWM(TIM::Channel::CH1) == SysInitStatus::InitOK
    && ws.SendDMA(TIM::Channel::CH1, strip.Data(), strip.Len()) == SysStatus::OK) {
    ws.Start();
}
// In main: keep strip unchanged while ws.IsDMABusy(TIM::Channel::CH1).
```

- `SetUpPWM()` acquires the constructor DMA. Each channel uses its own TIMx_CHx request (CCDS = 1, separate CCxDE) and its own DMA handler.
- Buffers: `uint16_t`/Half_Word for 16-bit timers, `uint32_t`/Word for 32-bit TIM2/TIM5. F7 cleans the source through System.
- **TC releases the buffer** — the last PWM period may still be in progress. A DMA error stops only that channel and calls its callback; `GetDMAStatus()` reports OK/Busy/Error.
- Mask this object's timer/DMA interrupts around `main`-side DIER changes (including `SendDMA()` and `IRQ_en()`); these handlers must not preempt each other. `DeInit()` (main only) releases DMA and registrations. Route conflicts and limits: [TIM.md](TIM.md).

**Other modes** (all functions of `TIM`, fixed until `DeInit()`; callbacks optional; ETR not yet supported):

| Function | Mode |
| --- | --- |
| `SetUpEncoder()` + `Count()` | hardware quadrature encoder on CH1/CH2; reads raw CNT |
| `SetUpPulseMeasure()` | period + HIGH width of an input signal (CH1/CH2 + SMCR) |
| `SetUpSlave()` | ITR slave modes (reset/gated/trigger/external clock) from another timer's TRGO |
| `SetUpOnePulse()` | one-pulse mode (OPM) |
| `SetUpPulseBurst()` + `RunPulses(N)` | exactly N pulses, stopped in hardware by RCR + OPM |
| `SetUpCounter()` + `StopAfter()/StopAt()` | a second timer counts pulses and stops the source from its IRQ (may emit extra/partial pulses) |
| `SetUpQuadrature()` + `GenPulse(N)` | N full A/B quadrature cycles (sign = direction), counted by the constructor-bound second timer; IRQ stop |

---

## 10. RTC

**Files:** `src/rtc.hpp`, `src/rtc.cpp` · **Details:** [RTC.md](RTC.md)

`RTC_cl` is an object for F4/F7/G0. Override `OnWakeup`, `OnAlarmA`, `OnAlarmB` and `OnTimestamp(const Timestamp&)` in a subclass. Configure from `main` after System/RCC; callbacks never wait or change registrations.

```cpp
class Clock : public RTC_cl {
protected:
    void OnWakeup() override { /* notify main */ }
};
static Clock clock;
if (clock.SetUp(RTC_cl::CLK_Source::LSE, 127, 255) == SysInitStatus::InitOK) {   // 1 Hz calendar
    // Set the calendar only when the application really changes the time:
    // clock.SetTime({14, 25, 0});
    // clock.SetDate({22, 5, 26}, RTC_cl::WeekDay::Friday);
    clock.EnableWakeUpTimer(1);   // (1 + 1) / 1 Hz = 2 seconds
}
RTC_cl::RtcTime time;
RTC_cl::RtcDate date;
clock.GetDateTime(time, date);   // TR then DR: coherent pair, shadow lock released
```

- **Same source after reset** → calendar and dividers are kept (RSF resynchronized).
- **Different source** → ⚠ the backup domain is reset: **time and backup registers are lost**.
- A repeated `SetUp()` is an error until `DeInit()`. LSE waits up to 5000 ms, other flags 1000 ms; busy calibration returns `Busy` immediately.
- Dividers 127/255 turn LSE 32768 Hz into 1 Hz; for nominal LSI 32000 Hz use 127/249 (the old default arguments LSI,127,255 remain and need adjusting for LSI). HSE and its divider are prepared by the caller (G0 uses HSE/32).
- Setters validate decimal or BCD fields. Alarms use `AlarmConfig` with `Care`/`Ignore` fields (`day` = day of month). WakeUp clocks: /16, /8, /4, /2, ck_spre and 17-bit ck_spre (code 110). Timestamp is read before clearing TSF and has no year. RTC_TS/CALIB pins are prepared by the application.
- `DeInit()` stops its own events and removes only its registrations; the calendar keeps running.

---

## 11. Flash

**Files:** `flash.hpp`, `flash.cpp` · **Details:** [Flash.md](Flash.md)

### flash_base — low-level access

- F4/F7/G0. `init()` checks the generated bank map against the hardware mode and rejects active bank swap; it never changes option bytes.
- `erase_sector()` takes a **map index**; `mass_erase()` takes 1/2/3 for bank 1/2/both.
- `GetFlashSector(index)` gives `{address, size, bank, number}`: uniform maps are computed, irregular ones come from a table. Config format 4 needs the matching `flash_config.h.in` template.

```cpp
if (flash_base::init() != SysInitStatus::InitOK) return;
if (flash_base::enable_access() != SysStatus::OK) return;

// Use a sector reserved by the project, never one containing code.
if (flash_base::erase_sector(settings_sector) != SysStatus::OK) return;
const uint32_t address = GetFlashSector(settings_sector).address;
const uint32_t values[] = {0x12345678, 0xDEADBEEF};
const SysStatus written = flash_base::write(address, values, 2);
if (written != SysStatus::OK) return;
const uint32_t value = flash_base::read<uint32_t>(address);
```

- Call from the main loop with SysTick running — not from interrupts or critical sections. Timeout per hardware operation: **50 s on F4/F7, 1 s on G0**. Fetching code from the busy bank stalls SysTick too, so timeouts are not real-time guarantees.
- **G0** writes 64 bits to 8-byte-aligned addresses (source may be unaligned; the last block is padded with FF). **F4/F7** (3–3.3 V, no VPP) write 8/16/32 bits; a 64-bit element becomes two 32-bit writes.
- Stale Flash cache contents are invalidated; SRAM D-cache is not touched globally.
- Direct `write()` does not validate source, size, destination range or alignment — the caller does. `flash_data::SetUp()` validates the log area.

### flash_data — storing a structure in one sector

```cpp
static uint8_t settings_buf[32]{};
constinit flash_data settings(settings_sector, settings_buf, sizeof(settings_buf));

if (settings.SetUp() != SysInitStatus::InitOK) return;
const auto loaded = settings.read_data();     // DATA_OK / NO_DATA / DATA_NOT_VALID
// Update settings_buf, then save and check the result.
const SysStatus saved = settings.write_data();
if (saved != SysStatus::OK) {
    const uint32_t flags = FLASH->SR;
    (void)flags;   // inspect before starting another Flash operation
}
```

- Each record is written as **DATA_WRITING → payload → DATA_OK**; only then is the previous record invalidated. Scanning picks the last complete record.
- When the sector is full it is erased — power loss during erase/rewrite can lose the last saved version. No CRC.
- Header: 1 byte on F4/F7 (`FF → 7F → 0F`, `00` = invalidated); **16 bytes on G0** (first word FF… → `00000000FFFFFFFF` → all zero; second word `FFFFFFFF00000000` = invalidated, takes priority). All values are defined once in `Data_Status`; G0 numeric values changed — compare named enumerators. `DATA_CORRUPT` was removed.
- If invalidating the old record fails, `write_data()` returns the error but the new record stays selected.
- The old 8-byte G0 header is incompatible — start from an erased log sector.

---

## 12. DAC

**Files:** `dac.hpp`, `dac.cpp` · **Details:** [DAC.md](DAC.md)

Enable `"drivers": ["DAC"]`. CH1 = PA4, CH2 = PA5 where available.

```cpp
constinit DAC_Module output(DAC_Module::_1::instance, DAC_Module::CHANNEL::CH1);
// After System/RCC initialization:
if (output.SetUp() == SysInitStatus::InitOK) {
    SysStatus written = output.SetValue(2048);   // check written == SysStatus::OK
}
output.DeInit();
```

- `SetUp()` selects direct buffered 12-bit output, initially 0.
- `SetValue()`: `NotInit` before setup / after `DeInit()`; `Error` for values > 4095 (previous code kept); `OK` after one register write. Analog start-up/settling time is per the datasheet.
- A repeated `SetUp()` needs `DeInit()`. `DeInit()` disables only this channel and keeps the other channel, clocks and the pin's analog mode. Copy/move are disabled; the destructor does nothing.
- F410 has CH1 only; selecting the module on a device without a DAC is a compile error.

---

## Order of initialization in main()

```cpp
int main() {
    System::Init();      // 1. FPU, Flash accelerator/caches, SysTick @ HSI

    // 2. Clocks
    constexpr auto clocks = ClockSystem::CalcPLL(64 * MHz, ClockSystem::PLL_ClockSource::HSI);
    if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
        System::DebugTrap("Clock init");

    // 3. Peripherals — in any order; check every result
    uart.SetUp();
    spi.SetUp(...);
    rtc.SetUp(...);

    // 4. DMA is bound in constructors: UART/SPI SetUp and TIM SetUpPWM configure it.
    tim_pwm.SetUp(...);
    tim_pwm.SetUpPWM(TIM::Channel::CH1);

    // 5. Start timers explicitly
    periodic_timer.Start();

    for (;;) { /* main loop */ }
}
```

---

## Dependencies between modules

```text
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

`flash.hpp` depends only on `system.hpp` and `flash_config.h`. `dac.hpp` depends on `system.hpp` and `gpio.hpp`.

---

## Adding a new MCU

1. Add `#include "stm32xxxx.h"` to `system.hpp`.
2. Update the peripheral instance/pin/DMA definitions in the relevant `*_defs.hpp` and, if needed, the DMA hardware descriptors in `dma.cpp`.
3. Add the vector stubs to `irq_registry_config.h`.
4. Add verified frequency/PLL/power limits to `rcc_defs.hpp`.
5. If needed, add register-abstraction macros (e.g. `RCC_GPIO_EN_REG` in `gpio.hpp`).

---

## Русский

### Миграция API — 19.10.2026

- **Владение DMA.** USART/SPI/ADC/I2C и привязки каналов TIM получают выбор DMA в конструкторе и содержат свои `DMA_Sx` внутри. На F4/F7 — именованные маршруты с номером контроллера и потока в имени; на G0 — только физический канал, номер запроса подставляет периферия.
- **Прерывания.** Обработчики периферии и DMA регистрируются раздельно; ни один не ждёт аппаратуру. SPI по-прежнему завершает асинхронный обмен через `Process()` в основном цикле.
- **`*_USE_ATOMIC`.** `UART_USE_ATOMIC`, `SPI_USE_ATOMIC`, `ADC_USE_ATOMIC`, `TIM_USE_ATOMIC`, `I2C_USE_ATOMIC`, `DAC_USE_ATOMIC`, `DMA_USE_ATOMIC` и `EXTI_USE_ATOMIC` задаются в своих заголовках. По умолчанию **0** (обычное «прочитать-изменить-записать»); 1 — макросы ST `ATOMIC_*`. После изменения пересоберите драйверы и приложение. Атомарность отдельных операций не согласует целую передачу и владение буфером. Настройки CMake нет.
- **Флаги DMA.** `GetIRQFlags()` возвращает HT/TC/ошибки одним чтением ISR; разбор — `FLAG_HT`, `FLAG_TC`, `FLAG_ERRORS` (FEIF в direct mode исключён).
- **I2C.** DMA из конструктора, callback через наследование, однократная настройка и `DeInit()`; восстановление шины — код приложения. Тайминги считаются при компиляции: `SetUp<clocks, I2C::SPEED::FAST>()` получает ту же `ClockSystem::Config`, что и RCC; перед настройкой аппаратуры сверяется `System::APB1BusClock`. Поиск во время работы и массив на 2560 байт удалены. См. [I2C.md](I2C.md).

C++20-библиотека поверх CMSIS для семейств **STM32G0**, **STM32F4** и **STM32F7**.

- **Ядро** (собирается всегда): System, RCC, GPIO, Flash, IRQ_Registry.
- **Опциональные модули:** UART, SPI, DMA, TIM, RTC, EXTI, ADC, DAC, I2C, MODBUS, CONTAINERS, PID, DSP, CRC.

Библиотека — не самостоятельный CMake-проект: её клонирует и подключает платформа [stm32_cmake_base](https://github.com/vsm-stm/stm32_cmake_base) (см. «Подключение и сборка»).

**Модуль `DSP`** — подмножество CMSIS-DSP v1.16.2 от Arm (в репозитории не хранится; подтягивается при конфигурации через `FetchContent`). Включается как любой модуль — добавлением `"DSP"` в `"drivers"`. Лицензии сторонних частей: [THIRD_PARTY.md](THIRD_PARTY.md).

| Группа | Функции | Типы |
| --- | --- | --- |
| Тригонометрия | `arm_sin_*`, `arm_cos_*` | `f32`, `q15`, `q31` |
| Квадратный корень | `arm_sqrt_*` | `f32` (inline), `q15`, `q31` |
| Угол | `arm_atan2_*` | `f32`, `q15`, `q31` |
| Деление | `arm_divide_*` | `q15`, `q31` |
| Векторный логарифм | `arm_vlog_*` | `f32`, `f64`, `q15`, `q31` |
| Векторная экспонента | `arm_vexp_*` | `f32`, `f64` |
| Совместный синус/косинус | `arm_sin_cos_*` | `f32`, `q31` |
| PID | `arm_pid_init_*`, `arm_pid_reset_*`, `arm_pid_*` (шаг, inline) | `f32`, `q15`, `q31` |
| Преобразования Кларка/Парка | `arm_clarke_*`, `arm_inv_clarke_*`, `arm_park_*`, `arm_inv_park_*` (inline) | `f32`, `q31` |

- Подключайте `dsp/fast_math_functions.h` и `dsp/controller_functions.h`. Общий `arm_math.h` объявляет и группы, исходники которых здесь **не** собираются (FFT, FIR/IIR, статистика…) — объявление не означает, что реализация слинкуется. Ядра float16 исключены; общие таблицы собираются один раз.
- `arm_sin_f32`/`arm_cos_f32` принимают **радианы**; `arm_sin_cos_f32` — **градусы**.
- Каждый экземпляр CMSIS PID инициализируйте `arm_pid_init_*(..., 1)` перед первым шагом.
- Проверяйте `arm_status` у корня, деления и atan2. Диапазоны и форматы фиксированной точки — в заголовках Arm: [FastMath](https://github.com/ARM-software/CMSIS-DSP/blob/v1.16.2/Include/dsp/fast_math_functions.h) и [Controller](https://github.com/ARM-software/CMSIS-DSP/blob/v1.16.2/Include/dsp/controller_functions.h).

**Модуль `PID`** — собственный ПИД-регулятор (`math/PID.*`, C API, вызывается из C++): переменный шаг `dT`, мёртвая зона `e_lim`, симметричное ограничение интеграла `max_ki_I` и вспомогательная `PID_get_quality`.

- Перед использованием обнулите состояние или вызовите `zero_PID_I_D()`. На каждом шаге задавайте `e`; `dT` — в секундах.
- Входы должны быть конечными, `dT > 0`, `e_lim`/`max_ki_I >= 0`. При `dT == 0` возвращается ноль без изменения состояния. При `ki == 0` интеграл `I` сохраняется.
- В мёртвой зоне `|e| <= e_lim`: `I` сохраняется, `D` равна нулю, `e_old` обновляется; пропорциональная часть действует.
- `PID_get_quality`: сбрасывайте через `get=0` перед измерением и при смене уставки или режима.
  - `get=1`: `e` — в процентах при ненулевой `u` (при `u = 0` — абсолютная `y-u`); `t_time` суммирует время вне коридора ±5 %.
  - `get=2`: `e` абсолютная; `t_time` суммирует каждый переданный `dt`.
  - Это накопленное время, а не время установления. `max_y` — максимум отклика при `u >= 0` и минимум при `u < 0`, с нуля после сброса; `sigma = (max_y - u)/u` (ноль при `u = 0`).

## Подключение и сборка

Драйверы включаются одной строкой в `project.json` проекта на `stm32_cmake_base`:

```json
"drivers": ["UART", "SPI", "DMA", "TIM"]
```

| Значение | Результат |
| --- | --- |
| `null` / `false` / ключа нет | драйверы не собираются вообще |
| `true` или `[]` | только ядро (System, RCC, GPIO, Flash, IRQ_Registry) |
| `["UART", ...]` | ядро + перечисленные модули; опечатка в имени — ошибка конфигурации со списком допустимых имён |

**Версия.** `"drivers_tag"` в `project.json` выбирает, что извлечь: тег, ветку или коммит (нет ключа/`null` → `main`). Ветка при каждой новой конфигурации берёт свой последний коммит. Платформа клонирует репозиторий в `Drivers/` при конфигурации и полностью заменяет его при смене выбора — **не храните там локальных правок**.

**Связка.** Цель `stm32_platform` (флаги, CMSIS, определения чипа) линкуется из `stm32_drivers`. Конфиги под конкретный чип — `flash_config.h` (карта секторов) и `irq_registry_config.h` (заглушки векторов) — библиотека генерирует сама в `cmsis-core/drivers_gen/`.

**Заголовки.** Ядро: `<system.hpp>`, `<rcc.hpp>`, `<gpio.hpp>`, `<flash.hpp>`, `<irq_registry.hpp>`. Опциональные: `<uart.hpp>`, `<spi.hpp>`, `<tim.hpp>`, … (пример — закомментированный блок в `inc/main.hpp` платформы).

### Модули и статус проверки

Статус по итогам проверки в октябре 2026: модули драйверов **компилируются** (debug и release, тестовые МК F4/F7/G0, включены все модули драйверов) без предупреждений. Компиляция не подтверждает работу на реальном железе.

| Модуль | Файлы | F4 | F7 | G0 |
| --- | --- | --- | --- | --- |
| Ядро: System, RCC, GPIO, Flash, IRQ_Registry | `src/system.*`, `rcc.*`, `gpio.*`, `flash.*`, `irq_registry.*` | собирается | собирается | собирается |
| UART, SPI | `src/uart.*`, `spi.*` | собирается | собирается | собирается |
| TIM | `src/tim.*` | собирается | собирается | собирается |
| DMA | `src/dma.*` | собирается | собирается | собирается |
| RTC | `src/rtc.*` | собирается | собирается | собирается |
| EXTI | `src/exti.*` | собирается | собирается | собирается |
| ADC | `src/adc.*` | собирается | собирается | собирается |
| DAC | `src/dac.*` | собирается | собирается | собирается |
| I2C | `src/i2c.*` | собирается | собирается | собирается |
| MODBUS | `Interfaces/` | собирается | собирается | собирается |
| CONTAINERS | `Containers/` (заголовки) | подключаются | — | — |
| PID | `math/PID.*` | ок | не проверялось | не проверялось |
| DSP | подмножество CMSIS-DSP v1.16.2 | ок | не проверялось | не проверялось |
| CRC | `crc/crc32.*` (без таблиц и аппаратной периферии — переносимо) | ок | ок | ок |

Поведение и ограничения каждого модуля описаны в его документе (например, [RTC.md](RTC.md), [ADC.md](ADC.md), [DAC.md](DAC.md), [DMA.md](DMA.md)).

Семейства кроме F4/F7/G0 (в том числе G4) не поддерживаются: задайте `"drivers": null` и пишите под регистры напрямую, либо портируйте библиотеку (см. «Добавление нового МК»).

---

## Подробная документация по модулям

Этот файл — обзор с короткими примерами. Полное описание каждого модуля — регистры, тайминги, угловые случаи — в отдельных файлах:

| Документ | Содержание |
| --- | --- |
| [Initialization.md](Initialization.md) | объявления `constinit` для всех модулей, диагностика при компиляции |
| [System.md](System.md) | `SysStatus`/`SysInitStatus`, тактовые переменные, `GetTick()`/`Delay_us()`, кеш/MPU, `DebugTrap()` |
| [RCC.md](RCC.md) | дерево тактирования: HSI/HSE/PLL, делители шин, задержка Flash, проверка при компиляции |
| [GPIO.md](GPIO.md) | `PIN`/`PinArray`: кодирование режимов, регистры, bit-banding |
| [IRQ_Registry.md](IRQ_Registry.md) | единый диспетчер прерываний, регистрация с наследованием и без, вместимость |
| [DMA.md](DMA.md) | `DMA_Sx`: потоки (F4/F7) и каналы + DMAMUX (G0), таблицы запросов |
| [UART.md](UART.md) | `USART`: блокирующий/IRQ/DMA, приём пакетов по паузе IDLE |
| [SPI.md](SPI.md) | `SPI`: master/slave, программный/аппаратный NSS, DMA, `Process()` |
| [I2C.md](I2C.md) | master, блокирующий/IRQ/DMA, callback, обработка STOP, восстановление шины приложением |
| [TIM.md](TIM.md) | общий TIM: PWM/захват, энкодер, измерение импульса, ITR, серии импульсов |
| [Modbus.md](Modbus.md) | транспорт RS-485, CRC, форматы кадра, ограничения IDLE |
| [RTC.md](RTC.md) | `RTC_cl`: календарь, таймер пробуждения, будильники, timestamp, калибровка |
| [EXTI.md](EXTI.md) | внешние прерывания GPIO, callback через наследование, ограничения общих векторов |
| [DAC.md](DAC.md) | `DAC_Module`: прямой 12-битный вывод, статусы, время жизни канала |
| [Flash.md](Flash.md) | `flash_base`/`flash_data`: разблокировка, стирание, запись, журнал записей в одном секторе |

---

## Содержание

1. [Архитектурные принципы](#1-архитектурные-принципы)
2. [System](#2-system-1)
3. [RCC / ClockSystem](#3-rcc--clocksystem-1)
4. [GPIO / PIN](#4-gpio--pin-1)
5. [IRQ_Registry](#5-irq_registry-1)
6. [DMA](#6-dma-1)
7. [USART](#7-usart-1)
8. [SPI](#8-spi-1)
9. [TIM](#9-tim-1)
10. [RTC](#10-rtc-1)
11. [Flash](#11-flash-1)
12. [DAC](#12-dac-1)

---

## 1. Архитектурные принципы

### Постоянные описания периферии (Instance)

USART/SPI/I2C/TIM/ADC/DAC получают статический `_N::instance` из своего defs-заголовка. Он описывает:

- числовые адреса регистров периферии и RCC;
- маски включения тактирования/сброса и переменную частоты шины;
- номера IRQ и возможности блока.

`SetUp()` использует его напрямую — поиска по таблице периферии нет.

```cpp
using U = USART::_3;
constinit USART port(U::instance, 115200, U::TX::PD8, U::RX::PD9);
```

**Состояние объекта:** одна ссылка на instance и один кешированный указатель регистров — ничего не дублируется. Указатель регистров заодно служит признаком инициализации: нулевой до настройки, после неудачной первой настройки и после `DeInit()`. Повторный `SetUp()` работающего объекта возвращает ошибку и не нарушает его работу. Обработчики прерываний используют кешированный указатель. DMA хранит собственную таблицу аппаратных дескрипторов и флаги занятости.

### Валидация пинов при компиляции

Выводы выбираются из вложенных структур:

```cpp
constinit USART debug(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7);
```

Каждый вывод, например `USART::_1::TX::PB6`, содержит базовый адрес периферии-владельца. Вывод чужой периферии отклоняет `constinit`-конструирование через `STM32_CONFIG_ERROR`; в сообщении GCC есть причина (например, `USART: TX pin belongs to another peripheral`). Без `constinit` конструктор при старте вызывает `DebugTrap(причина)` и останавливается. Устаревший конструктор USART с CMSIS-указателем по-прежнему проверяет во время работы.

### IRQ_Registry — единый диспетчер прерываний

Все прерывания проходят через один диспетчер: сгенерированные заглушки в `irq_registry_config.h` вызывают `IRQ_Registry::Dispatch(IRQn)`, а он — зарегистрированные объекты `IIRQHandler`.

- Драйверы регистрируются сами в `SetUp()` и снимают регистрацию в `DeInit()` — писать для них функции `*_IRQHandler` не нужно.
- `IRQ_en()` только включает/выключает источники прерываний периферии; регистрации он не меняет.

---

## 2. System

**Файлы:** `system.hpp`, `system.cpp` · **Подробнее:** [System.md](System.md)

Базовый модуль: FPU, Flash-акселератор/кеши, SysTick, задержки, функции кеша, `DebugTrap()`.

### Инициализация

```cpp
System::Init();  // ядро + SysTick (1 кГц) от HSI 16 МГц
```

Вызывайте **первым** в `main()`, до `ClockSystem::Init<config>()`, и проверяйте возвращаемый `SysInitStatus`.

- **Обычный старт:** предполагается состояние тактирования, питания и задержки Flash после аппаратного сброса. Вызывать один раз, до изменения частот.
- **После загрузчика** (`STM32_USE_BOOTLOADER`, задаётся автоматически при настроенном `bootloader` в `project.json`): F4/F7/G0 возвращаются на HSI 16 МГц с делителями шин /1; G0 также восстанавливает VOS Range 1 перед Flash 0 WS. Ожидание HSI без таймаута; последующие ожидания (VOS/Flash, 100 мс) требуют разрешённых прерываний. Настройки PLL/HSE и прерывания RCC сохраняются.
- **L0** сохраняет старое предположение о HSI 16 МГц и ничего не восстанавливает.
- База определяет `STM32_STARTUP_ENABLES_FPU`, поэтому `Init()` пропускает настройку FPU, уже выполненную startup. При другом startup уберите это определение, если он сам не включает FPU.
- На F7 кеши ядра включены; MPU и доступ к внешней памяти: [System.md](System.md#6-mpu-на-stm32f7).

### Работа со временем

```cpp
uint32_t t = System::GetTick();   // мс с момента старта
System::Delay_ms(500);            // блокирующая задержка 500 мс

// Неблокирующий таймер (вычитание безопасно при переполнении):
uint32_t start = System::GetTick();
if ((System::GetTick() - start) > 1000) { /* прошла секунда */ }
```

### Микросекундные задержки (F4/F7)

```cpp
System::Enable_CYCCNT();   // один раз при старте
System::Delay_us(10);      // 10 мкс через DWT
```

- `Delay_ms()` требует работающего SysTick, способного прервать вызывающий код, — нельзя ждать при замаскированных тиках или внутри IRQ с равным/более высоким приоритетом. `GetTick()` можно читать где угодно. Ожидание ограничено `0x7FFFFFFF` тиками (~24,9 суток при 1 кГц).
- `Delay_us()` использует 64-битную арифметику и ограничен `0x7FFFFFFF` тактами ядра. **На G0/L0 нет DWT** — `Delay_us()` там не существует.
- `SWOTrace()` (F4/F7) неблокирующий и возвращает число байтов, принятых ITM.
- `DebugTrap()` в Debug сохраняет сообщение, выводит его через блокирующий SWO (если он включён в отладчике), выполняет BKPT и останавливается; в Release — только останавливается. `SetDebugOutput()` удалён.

### Некешируемые DMA-области

В проектах F7 настройка базы `dma_regions` резервирует некешируемые DMA-буферы (`name`, `size`, `memory`: SRAM1/SRAM2, `mpu_region`). Адреса вычисляются автоматически; startup обнуляет буферы; `System::Init()` задаёт их атрибуты MPU до включения D-cache. Буферы размещаются в `.dma.<name>`. См. [System.md](System.md).

Функции кеша для кешируемых буферов: `System::CacheClean`, `CacheInvalidate`, `CacheCleanInvalidate` (адрес + размер в байтах). На F7 диапазоны целиком внутри DTCM или настроенных некешируемых областей пропускаются; на F4/G0/L0 функции пустые. Драйверы периферии вызывают их сами в нужные моменты; при прямом использовании DMA — пользователь. RX-буфер должен владеть всеми 32-байтными линиями кеша, которые задевает.

### Переменные частот шин

Публикуются `ClockSystem::Init<config>()`:

```cpp
System::SystemCoreClock  // частота ядра (AHB)
System::APB1BusClock     // APB1
System::APB2BusClock     // APB2 (0 на G0)
System::TIMxAPB1Clock    // таймеры на APB1
System::TIMxAPB2Clock    // таймеры на APB2
```

---

## 3. RCC / ClockSystem

**Файлы:** `rcc.hpp`, `rcc.cpp`, `rcc_defs.hpp` · **Подробнее:** [RCC.md](RCC.md)

Одна настройка тактирования на аппаратный сброс. Конфигурация — `constexpr`-значение; `Init<config>()` проверяет и рассчитывает всё **при компиляции**. Порядок: `System::Init()` → `ClockSystem::Init<config>()` → периферия.

### HSI без PLL

```cpp
constexpr ClockSystem::Config clocks{};
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
    System::DebugTrap("Clock init");
```

### Точный автоподбор PLL

Пример — G0, SYSCLK 64 МГц от HSI:

```cpp
constexpr auto clocks = ClockSystem::CalcPLL(
    64 * MHz, ClockSystem::PLL_ClockSource::HSI);
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
    System::DebugTrap("Clock init");
```

- Для HSE передайте частоту кварца третьим аргументом; четвёртый — Q (по умолчанию 2).
- `CalcPLL` находит **точную** SYSCLK с AHB /1 и допустимыми делителями APB, предпочитая максимальную допустимую частоту входа PLL, затем минимальный P/R.
- Нет точного решения → ошибка сборки в `Init<clocks>()`.

### Ручная конфигурация

F407, HSE 8 МГц, SYSCLK/HCLK 168 МГц, APB1 42 МГц, APB2 84 МГц:

```cpp
constexpr ClockSystem::Config clocks{
    .ClkSrc  = ClockSystem::SystemClockSource::PLL,
    .HSE_Clk = 8 * MHz,
    .BusDiv  = {ClockSystem::AHB_Divider::DIV1,
                ClockSystem::APB_Divider::DIV4,
                ClockSystem::APB_Divider::DIV2},
    .PLLCfgr = {ClockSystem::PLL_ClockSource::HSE, 4, 168, 2, 7, 2},   // M, N, P, Q, R
};
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
    System::DebugTrap("Clock init");
```

- Поддерживаются все модели F4/F7 из STM32-base_files и G0 (на G0 доступны также LSI/LSE).
- VDD принимается 3,0 В (тот же диапазон таймингов, что и для 3,3 В); масштаб напряжения, over-drive и задержка Flash следуют пределам модели/HCLK.
- Ошибки во время работы — только аппаратные таймауты или повторный вызов; после неудачной попытки нужен сброс.
- SysTick использует целое число тактов HCLK — например, LSE 32768 Гц с AHB /1 даёт 1024 тика/с, и `Delay_ms`/таймауты получают ту же погрешность. Пределы, порядок переключения и переход со старых `Init(...)`/`InitCalcPLL(...)`: [RCC.md](RCC.md).

---

## 4. GPIO / PIN

**Файлы:** `gpio.hpp`, `gpio.cpp` · **Подробнее:** [GPIO.md](GPIO.md)

| Конструктор | Применение | Проверки |
| --- | --- | --- |
| `PIN(GPIOA, 5)` (CMSIS-указатель) | обычные объекты GPIO | нет |
| `PIN(GPIOA_BASE, 5, af, periph)` (`consteval`) | таблицы выводов, `constinit`-объекты | порт, номер и AF проверяются при сборке |

- Порт/номер/сохранённый AF закрыты и неизменны; `periph_base` только для чтения. Копирование при создании разрешено, перепривязка присваиванием — нет. Явный AF можно передать в `SetUp()`.
- ADC (`ADC_PIN`, имена вида `PA0_IN0`) и TIM (`TIM_PIN`) используют описания выводов с проверкой при компиляции на том же механизме.

### Создание и настройка пина

```cpp
PIN led(GPIOB, 8);                        // порт + номер вывода
PIN btn(GPIOA, 0);

led.SetUp(PIN::TYPE::OUTPUT_PushPull);    // выход push-pull
btn.SetUp(PIN::TYPE::INPUT_PullUp);       // вход с подтяжкой

led.SetLevel(true);      // HIGH
led.SetLevel(false);     // LOW
led.TogglePin();         // инверсия

bool state = btn.GetLevel();   // чтение входа
```

### Все типы пинов

| Константа | Описание |
| --- | --- |
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

Выходы по умолчанию стартуют в **LOW** (уровень записывается через BSRR до включения режима). Для начального HIGH:

```cpp
led.SetUp(PIN::TYPE::OUTPUT_PushPull, PIN::OUTPUT_SPEED::Low, PIN::LVL::HIGH);
```

`TogglePin()` — это «прочитать-изменить-записать» ODR: во время операции не меняйте другие выходы того же порта из IRQ. (`SetLevel()` использует BSRR и безопасен.)

### Альтернативная функция

```cpp
PIN sck(GPIOA, 5);
sck.SetUp(PIN::TYPE::AF_PushPull, 5);   // AF5 = SPI1 на STM32F429
```

Драйверы периферии сами настраивают свои выводы с AF из таблиц.

### Оператор присваивания и преобразования

```cpp
led = true;       // SetLevel(true)
led = false;      // SetLevel(false)
bool b = (bool)led;   // GetLevel() — преобразование явное
```

### IsValid()

`PIN{}` (конструктор по умолчанию) возвращает `IsValid() == false`. Драйверы используют это как признак «сигнал не подключён»:

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
seg_pins.SetLevelAll(0b1010);              // бит i → вывод i
uint32_t state = seg_pins.GetLevelAll();   // прочитать все сразу
```

`SetUpAll()` настраивает все выводы и возвращает первую ошибку без преобразования. Выводы могут быть на разных портах.

---

## 5. IRQ_Registry

**Файлы:** `irq_registry.hpp`, `irq_registry.cpp`, `irq_registry_settings.hpp`; сгенерированный `irq_registry_config.h` · **Подробнее:** [IRQ_Registry.md](IRQ_Registry.md)

Сгенерированные заглушки вызывают `Dispatch(irqn)`, который вызывает зарегистрированные объекты `IIRQHandler` (или адаптеры обычных функций). Один объект-обработчик обслуживает все источники периферии; разные периферии могут делить вектор.

**`Register()`** возвращает `InitOK` или `InitError` (неверный/нулевой аргумент, повтор на том же IRQ, заполненная строка или пул функций). Другой `ctx` не позволяет зарегистрировать ту же функцию дважды — сначала отмените регистрацию.

```cpp
if (IRQ_Registry::Register(TIM3_IRQn, &handler) == SysInitStatus::InitOK)
    NVIC_EnableIRQ(TIM3_IRQn);

// Позже, из main, при отключённых источниках этого владельца:
NVIC_DisableIRQ(TIM3_IRQn);
IRQ_Registry::Unregister(TIM3_IRQn, &handler);
if (IRQ_Registry::HasHandlers(TIM3_IRQn)) NVIC_EnableIRQ(TIM3_IRQn);
```

- Обычные функции: `Register(irqn, fn, ctx)` — `ctx` передаётся в `fn(ctx)`. Отмена — `Unregister(irqn, fn)`; адаптер освобождается.
- `Unregister(irqn)` (один аргумент) очищает всю строку.
- **Никогда** не вызывайте `Register`/`Unregister` из `HandleIRQ()` и callback. Меняйте реестр из `main` при замаскированном векторе. Callback может выключить свой источник в периферии, а снятие регистрации оставить `main`.

### Число обработчиков на один IRQ

Настройки — в `Drivers/src/irq_registry_settings.hpp` (общем для всех исходников). `IRQ_MAX_SHARED` по умолчанию **3**; для пяти обработчиков на вектор:

```cpp
#define IRQ_MAX_SHARED 5
```

Каждый дополнительный слот стоит `IRQ_TABLE_SIZE × 4` байт RAM. Отдельный пул `IRQ_MAX_FUNCTION_HANDLERS` по умолчанию равен 8. После изменения пересоберите библиотеку и прошивки; обновление сохранённых шаблонов описано в подробном документе.

---

## 6. DMA

**Файлы:** `dma.hpp`, `dma.cpp`, `dma_requests.hpp` · **Подробнее:** [DMA.md](DMA.md)

Единый API для потоков F4/F7 (`DMA_Stream_TypeDef`) и каналов G0 (`DMA_Channel_TypeDef` + DMAMUX). Драйверы периферии содержат свои `DMA_Sx` внутри — примеры ниже для самостоятельного использования.

- `SetUp()` допускает нулевые счётчик/адреса (подготовка без запуска). Отклоняет занятый канал, неверные поля, невыровненные адреса и счётчик больше 65535.
- `SetCount()` принимает 1…65535 **элементов**. Методы адресов/счётчика возвращают `NotInit` до настройки, `Busy` при установленном EN, `Error` при неверных аргументах — проверяйте.
- `Stream_EN(DISABLE)` ждёт EN = 0 без таймаута (только основной цикл).
- `Release()` останавливает, сбрасывает и освобождает ресурс; выбор из конструктора сохраняется. До освобождения отключите DMA-запросы периферии и удалите её обработчик. Линии NVIC и тактирование контроллера остаются включёнными.
- Дескрипторы хранятся в таблице `static const dma_streams` во Flash; RAM занимают только флаги `dma_used`.
- Функции кеша — в `System`; когда и для какого диапазона их вызывать, решает потребитель.

### Конструктор (STM32G0)

DMAMUX1 подключает любой канал к любой периферии. Канал выбираете вы, запрос берётся из `_N::DMA` периферии:

```cpp
// Вариант 1: канал + именованный запрос (рекомендуется)
constinit DMA_Sx dma_tx(DMA1_Channel4_BASE, USART::_1::DMA::TX);
constinit DMA_Sx dma_rx(DMA1_Channel5_BASE, USART::_1::DMA::RX);

// Вариант 2: канал + сырой номер запроса DMAMUX1
constinit DMA_Sx dma_raw(DMA1_Channel3_BASE, 51u);   // USART1_TX

// Вариант 3: только канал (запрос задаётся позже через StreamSettings::channel)
constinit DMA_Sx dma_late(DMA1_Channel3_BASE);
```

### Конструктор (STM32F4/F7)

Поток и CHSEL зафиксированы в кремнии и закодированы в имени маршрута:

```cpp
constinit DMA_Sx dma_tx(USART::_1::DMA::TX_DMA2_Stream7);   // CH4
constinit DMA_Sx dma_rx(USART::_1::DMA::RX_DMA2_Stream5);   // CH4
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

if (dma.SetUp(cfg) != SysInitStatus::InitOK) { /* обработка ошибки */ }
```

### Ручной запуск передачи

```cpp
dma.ClearFlags();
dma.SetMemAddr(reinterpret_cast<uint32_t>(buf));   // проверить SysStatus
dma.SetCount(len);                                  // проверить SysStatus
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
        _dma.RequestStop();   // TC уже подтвердил завершение
        _dma.ClearFlags();
        __DMB();
        // передача завершена
    }
}
```

### Направления

| `DIR` | Значение |
| --- | --- |
| `From_Per` | Периферия → Память |
| `To_Per` | Память → Периферия |
| `Mem_To_Mem` | Память → Память, только G0; на F4/F7 — `InitError` |

### Ширина элемента

| `SIZE` | Размер |
| --- | --- |
| `Byte` | 8 бит |
| `Half_Word` | 16 бит |
| `Word` | 32 бита |

### Важно о разделяемых IRQ на G0

| Каналы | Вектор |
| --- | --- |
| CH1 | `DMA1_Channel1_IRQn` (эксклюзивный) |
| CH2, CH3 | `DMA1_Channel2_3_IRQn` (общий) |
| CH4, CH5 | `DMA1_Ch4_5_DMAMUX1_OVR_IRQn` (общий) |

Последняя строка — для МК с пятью каналами. DMA1 с семью каналами использует `DMA1_Ch4_7_DMAMUX1_OVR_IRQn`; МК с DMA2 — `DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX1_OVR_IRQn` для DMA1 CH4–7 и DMA2 CH1–5. Драйвер выбирает нужный вектор по заголовку МК.

Несколько каналов на одном векторе — не проблема: `IRQ_Registry` вызывает все зарегистрированные обработчики, а каждый проверяет только свои флаги (учитывайте `IRQ_MAX_SHARED`).

---

## 7. USART

**Файлы:** `uart.hpp`, `uart.cpp`, `uart_defs.hpp` · **Подробнее:** [UART.md](UART.md)

**Главное:**

- Oversampling ×16; BRR округляется и проверяется; `SetBaud()` возвращает `SysStatus`. Перед `SetBaud()` дождитесь физического завершения передачи (TC / `OnTC()`).
- **Конструктор:** `USART(instance, baud, tx, rx, dma_tx = {}, dma_rx = {} [, clock_source])`. На F7/G0 можно выбрать `USART::ClockSource` (по умолчанию PCLK, также SYSCLK, HSI, LSE, если поддерживаются); HSI/LSE запускает вызывающий код, `SetUp()` только проверяет готовность. На F4 всегда PCLK.
- **Длины:** 0 → `Error`; IRQ/DMA — 1…65535 байт.
- **DMA** хранятся внутри USART; при `constinit` маршруты проверяются при компиляции; `SetUp()` захватывает и настраивает их. У USART и обоих направлений DMA отдельные обработчики.
- **Конец пакета по IDLE:** F4/F7 — IDLE запрашивает остановку DMA, прерывание DMA RX публикует длину/кеш/готовность (без ожиданий и без `Process()`). G0 — IDLE отключает DMAR/DMA без ожидания и завершает пакет в прерывании USART; последний запрос DMA должен быть уже обслужен. Ошибочные пакеты отбрасываются.
- **Чтение пакета:** `GetDataReceivedCount()` больше ничего не сбрасывает — после обработки вызовите `ClearDataReceivedFlag()`. Modbus делает это явно ([Modbus.md](Modbus.md)).
- **Ошибки** останавливают свою операцию и вызывают `OnError(mask)`.
- **Ограниченная работа в IRQ:** не больше глубины FIFO в каждом направлении за вход (8 слов на G0 с FIFO, иначе 1 байт).
- Кеш F7 обслуживается через System; настроенные некешируемые области пропускаются.

### Базовое использование

```cpp
constinit USART debug(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7);

if (debug.SetUp() == SysInitStatus::InitOK) {
    debug.Send((uint8_t*)"Hello\r\n", 7);

    uint8_t buf[16];
    debug.Receive(buf, sizeof(buf), 500);   // таймаут 500 мс на весь вызов
}
```

### Только TX (без RX)

```cpp
constinit USART log(USART::_2::instance, 115200, USART::_2::TX::PA2, PIN{});
log.SetUp();
log.Send((uint8_t*)"OK\r\n", 4);
```

### DMA-передача

```cpp
// G0: физические каналы; на F4/F7 — именованные маршруты, например USART::_1::DMA::TX_DMA2_Stream7.
constinit USART uart(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7,
                     DMA1_Channel4_BASE, DMA1_Channel5_BASE);

if (uart.SetUp() == SysInitStatus::InitOK) {        // захватывает/настраивает встроенные DMA
    uart.Send_DMA(tx_buf, sizeof(tx_buf));
    uart.Receive_DMA(rx_buf, sizeof(rx_buf));       // завершается по IDLE или по заполнению
}
```

### Прерывания

```cpp
uart.IRQ_en(USART::IRQ::RXNE, ENABLE);   // включить источник RXNE
uart.IRQ_en(USART::IRQ::TXE, DISABLE);   // выключить источник TXE
```

`SetUp()` один раз регистрирует прерывание UART. `IRQ_en()` меняет только источники в периферии (допустимо и из callback). `DeInit()` удаляет только регистрации этого объекта.

### Собственная обработка прерываний (наследование)

```cpp
class MyUART : public USART {
    using USART::USART;   // наследуем конструктор
    void OnRxByte(uint8_t b) override { USART::OnRxByte(b); ring_buf.push(b); }
    void OnIdle() override { USART::OnIdle(); process_packet(); }
};

constinit MyUART uart(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7);
```

В переопределениях `OnRxByte`/`OnTxEmpty`/`OnIdle`/`OnDma*Complete` вызывайте базовую реализацию.

### Изменение baudrate между обменами

```cpp
uart.SetBaud(9600);   // после TC; проверить SysStatus
```

---

## 8. SPI

**Файлы:** `spi.hpp`, `spi.cpp`, `spi_defs.hpp` · **Подробнее:** [SPI.md](SPI.md)

- SPI1–SPI6 на F4/F7 и SPI1–SPI3 на G0, если они есть в CMSIS.
- `Direction::TX`/`RX`/`TXRX` и необязательные DMA фиксируются в конструкторе; частота, режим и формат кадра задаются в `SetUp()`.
- Polling, IRQ и DMA запускаются раздельно. Проверяются обязательные буферы и статус `OK`; для DMA — ещё наличие нужного DMA. Длину (1..65535), ширину/выравнивание буфера и совместимость операции с выводами обеспечивает вызывающий код. Общая подготовка задаёт только программное состояние; источники DMA IRQ настраиваются один раз в `SetUpDMA()`.
- После обмена со статусом `Error`/`Timeout` восстановление — через `DeInit()` и `SetUp()`. Отказ из-за отсутствующего буфера/DMA не меняет статус объекта. Подготовка DMA не обращается к NVIC.
- `TX` использует аппаратную передачу без приёма и один TX DMA. `RX` в режиме master работает как full duplex: RX DMA плюс TX-прерывание для пустых кадров `0xFF`/`0xFFFF`.

### Базовое использование (блокирующий режим)

```cpp
constinit SPI spi(SPI::_1::instance, SPI::Direction::TXRX, SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7,
                  SPI::_1::MISO::PA6, PIN(GPIOB_BASE, 0));   // необязательный автоматический CS
if (spi.SetUp(SPI::Master_sel::Master, SPI::NSS_ctrl::Software,
              SPI::Data_frame_format::Byte, SPI::Frame_Format::MSB,
              SPI::cPolPha::None, SPI::BaudRate::DIV4) == SysInitStatus::InitOK) {
    uint8_t tx[] = {0x12, 0x34};
    SysStatus result = spi.Send(tx, sizeof(tx));   // CS снимается и при успехе, и при ошибке
}
```

Для нескольких устройств передавайте выбранный вывод последним параметром `PIN* ss`:
`spi.Send(tx, len, 100, &device_cs)` или `spi.Send_DMA(tx, len, ENABLE, &device_cs)`.
Приложение настраивает каждый вывод как выход с высоким уровнем и сохраняет PIN
до завершения обмена. Драйвер активирует его перед SPE и снимает после завершения,
ошибки или отмены; IRQ/DMA требуют `Process()`. `nullptr` выбирает SS конструктора,
а при его отсутствии сохраняет ручное управление. Аппаратный NSS требует своего
вывода; slave всегда использует аппаратный NSS. В обоих режимах аппаратного NSS
программный CS из параметра игнорируется.

### 16-битные кадры

```cpp
// После успешного SetUp(Master, Software, Half_Word):
uint16_t tx[] = {0x1234, 0xABCD}, rx[2];
SysStatus result = spi.Send_Receive(tx, rx, 2);   // len — число кадров, а не байт
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
// В main, пока идёт IRQ/DMA-обмен:
SysStatus state = spi.Process();   // Busy, пока последний кадр не ушёл на линию
// После OK доступен RX, GetDataReceivedCount() возвращает число кадров.
```

- **DMA TC — это ещё не конец обмена**: `Process()` проверяет сам SPI, затем обслуживает кеш, CS и callback.
- Кеш F7 обслуживается через System; настроенные некешируемые области пропускаются. Не обращайтесь к активному RX-буферу и не меняйте TX.
- `OnComplete()`/`OnError(mask)` вызываются внутри `Process()` после очистки. `IRQ_en()` меняет источники в CR2; регистрацией и NVIC управляет `SetUp()`.
- **Slave:** `len` — вместимость. Приложение настраивает EXTI по нарастающему фронту NSS и вызывает из него `NssReleased()`; `Process()` завершает пакет в `main`. Лишние кадры → `IsTruncated()`; после TX-данных передаются пустые кадры. На G0 последний запрос RX DMA должен быть обслужен до остановки по NSS. Приоритеты и правила повторного запуска — в [SPI.md](SPI.md).

### Делитель частоты (BaudRate)

| Константа | Делитель |
| --- | --- |
| `DIV2` | PCLK / 2 |
| `DIV4` | PCLK / 4 |
| `DIV8` | PCLK / 8 |
| ... | ... |
| `DIV256` | PCLK / 256 |

### Полярность/фаза (cPolPha)

| Константа | CPOL | CPHA | Режим SPI |
| --- | --- | --- | --- |
| `None` | 0 | 0 | Mode 0 |
| `cPha` | 0 | 1 | Mode 1 |
| `cPol` | 1 | 0 | Mode 2 |
| `cPolPha` | 1 | 1 | Mode 3 |

---

## 9. TIM

**Файлы:** `tim.hpp`, `tim.cpp`, `tim_defs.hpp` · **Подробнее:** [TIM.md](TIM.md)

Один класс `TIM` владеет базой счётчика и независимо настраивает каждый канал. Выводы и необязательные DMA задаются до четырьмя привязками `{вывод, dma}` в конструкторе. Отдельных классов `TIM_PWM`/`PeriodicIRQ`/`InputCapture`, объектов `Output` и `AttachDMA()` нет (старые классы оставлены комментариями в `tim.hpp`/`tim.cpp`).

### База, PWM и compare

```cpp
void Tick();
void HalfTick();
constinit TIM timer(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7});
// В main, проверить все результаты до Start:
if (timer.SetUp(1000, &Tick, 1000) == SysInitStatus::InitOK          // Update 1 кГц, 1000 тиков
    && timer.SetUpPWM(TIM::Channel::CH1, 250) == SysInitStatus::InitOK   // 25 %
    && timer.SetUpPWM(TIM::Channel::CH2, 500) == SysInitStatus::InitOK   // 50 %
    && timer.SetCompareIRQ(TIM::Channel::CH3, 750, &HalfTick) == SysInitStatus::InitOK) {
    timer.Start();
}
timer.SetDuty(TIM::Channel::CH1, 75);   // проверить SysStatus
```

- `SetUp(frequency, callback, period)`: frequency — частота Update/PWM в Гц (не частота тиков); period — тики счётчика (ARR = period − 1); 0 — автоматически. Повторный `SetUp()` требует `DeInit()`.
- PSC/ARR общие для всех каналов. Для захвата/счётчика период обязателен. Реальную частоту тиков показывает `GetCounterClock()`.
- PWM1 с предзагрузкой CCR. Период 16-битного PWM ограничен 65535 тиками, чтобы `CCR = period` давал точные 100 % (0 — 0 %). `SetDuty()` ограничивает значение 100 %, `SetCCR()` принимает сырые тики; ни один не трогает CCR, занятый DMA. `Start()` всегда явный.

### Capture и аппаратный триггер

```cpp
void Edge();
constinit TIM input(TIM::_3::instance, {TIM::_3::CH1::PA6});
// В main; целевая частота CNT: 100 Гц × 10000 тиков = 1 МГц.
if (input.SetUp(100, nullptr, 10000) == SysInitStatus::InitOK
    && input.SetUpCapture(TIM::Channel::CH1, &Edge) == SysInitStatus::InitOK) {
    input.Start();
}
// Edge() читает input.GetCapture(TIM::Channel::CH1).
```

- Захват: прямой TIx, нарастающий фронт. `GetCapture()` читает CCR напрямую; callback только уведомляет. Переполнения/overcapture — задача приложения.
- `SetCompareIRQ()` с `nullptr` создаёт аппаратное событие без вывода и без прерывания.
- `SetUpTrigger(Trigger::Update / ComparePulse / OCxRef)` выбирает TRGO без прерываний. АЦП получает `TIM*` и источник в конструкторе и запускает таймер после подготовки ADC/DMA; настройте таймер до `SetUp()` АЦП — см. [ADC.md](ADC.md).

### Независимые DMA и WS2812B

```cpp
// G0; на F4/F7 — TIM::_1::DMA::CH1_DMA2_Stream1.
constexpr TIM::DMAReq dma = DMA1_Channel3_BASE;
constinit TIM ws(TIM::_1::instance, {TIM::_1::CH1::PA8, dma});
WS2812B_Strip<8> strip(52, 26);
strip.SetAll(RGB::Red());
// Пример: CNT 64 МГц, period = 80 тиков → 800 кГц, ARR = 79.
if (ws.SetUp(800000, nullptr, 80) == SysInitStatus::InitOK
    && ws.SetUpPWM(TIM::Channel::CH1) == SysInitStatus::InitOK
    && ws.SendDMA(TIM::Channel::CH1, strip.Data(), strip.Len()) == SysStatus::OK) {
    ws.Start();
}
// В main: не менять strip, пока ws.IsDMABusy(TIM::Channel::CH1).
```

- `SetUpPWM()` захватывает DMA из конструктора. Каждый канал использует свой запрос TIMx_CHx (CCDS = 1, свой CCxDE) и свой обработчик DMA.
- Буферы: `uint16_t`/Half_Word для 16-битных таймеров, `uint32_t`/Word для 32-битных TIM2/TIM5. На F7 System очищает источник в кеше.
- **TC освобождает буфер** — последний период PWM может ещё продолжаться. Ошибка DMA останавливает только свой канал и вызывает его callback; `GetDMAStatus()` показывает OK/Busy/Error.
- Изменения DIER из `main` (в том числе `SendDMA()` и `IRQ_en()`) окружайте маскированием прерываний таймера/DMA этого объекта; эти обработчики не должны вытеснять друг друга. `DeInit()` (только из main) освобождает DMA и регистрации. Конфликты маршрутов и ограничения — в [TIM.md](TIM.md).

**Другие режимы** (функции `TIM`, действуют до `DeInit()`; callback необязательны; ETR пока не поддерживается):

| Функция | Режим |
| --- | --- |
| `SetUpEncoder()` + `Count()` | аппаратный квадратурный энкодер на CH1/CH2; чтение CNT |
| `SetUpPulseMeasure()` | период и длительность HIGH входного сигнала (CH1/CH2 + SMCR) |
| `SetUpSlave()` | режимы slave по ITR (сброс/строб/запуск/внешний такт) от TRGO другого таймера |
| `SetUpOnePulse()` | режим одного импульса (OPM) |
| `SetUpPulseBurst()` + `RunPulses(N)` | ровно N импульсов, аппаратная остановка через RCR + OPM |
| `SetUpCounter()` + `StopAfter()/StopAt()` | второй таймер считает импульсы и останавливает источник из прерывания (возможны лишние/неполные импульсы) |
| `SetUpQuadrature()` + `GenPulse(N)` | N полных циклов A/B (знак — направление), считает второй таймер из конструктора; остановка из прерывания |

---

## 10. RTC

**Файлы:** `src/rtc.hpp`, `src/rtc.cpp` · **Подробнее:** [RTC.md](RTC.md)

`RTC_cl` — объект для F4/F7/G0. Переопределите `OnWakeup`, `OnAlarmA`, `OnAlarmB` и `OnTimestamp(const Timestamp&)` в наследнике. Настраивайте из `main` после System/RCC; callback ничего не ждут и не меняют регистрации.

```cpp
class Clock : public RTC_cl {
protected:
    void OnWakeup() override { /* уведомить main */ }
};
static Clock clock;
if (clock.SetUp(RTC_cl::CLK_Source::LSE, 127, 255) == SysInitStatus::InitOK) {   // календарь 1 Гц
    // Устанавливайте календарь, только когда приложение действительно меняет время:
    // clock.SetTime({14, 25, 0});
    // clock.SetDate({22, 5, 26}, RTC_cl::WeekDay::Friday);
    clock.EnableWakeUpTimer(1);   // (1 + 1) / 1 Гц = 2 секунды
}
RTC_cl::RtcTime time;
RTC_cl::RtcDate date;
clock.GetDateTime(time, date);   // TR, затем DR: согласованная пара, shadow-блокировка снята
```

- **Тот же источник после сброса** → календарь и делители сохраняются (RSF синхронизируется заново).
- **Другой источник** → ⚠ сбрасывается backup-домен: **время и backup-регистры теряются**.
- Повторный `SetUp()` — ошибка до `DeInit()`. LSE ждём до 5000 мс, остальные флаги — до 1000 мс; занятая калибровка сразу возвращает `Busy`.
- Делители 127/255 превращают LSE 32768 Гц в 1 Гц; для номинального LSI 32000 Гц используйте 127/249 (старые аргументы по умолчанию LSI,127,255 сохранены и для LSI требуют корректировки). HSE и его делитель готовит вызывающий код (на G0 — HSE/32).
- Установка времени/даты проверяет десятичные или BCD-поля. Будильники используют `AlarmConfig` с полями `Care`/`Ignore` (`day` — число месяца). Источники таймера пробуждения: /16, /8, /4, /2, ck_spre и 17-битный ck_spre (код 110). Timestamp читается до сброса TSF и не содержит года. Выводы RTC_TS/CALIB готовит приложение.
- `DeInit()` останавливает свои события и удаляет только свои регистрации; календарь продолжает идти.

---

## 11. Flash

**Файлы:** `flash.hpp`, `flash.cpp` · **Подробнее:** [Flash.md](Flash.md)

### flash_base — низкоуровневый доступ

- F4/F7/G0. `init()` сверяет сгенерированную карту банков с аппаратным режимом и отклоняет активный bank swap; option bytes никогда не меняет.
- `erase_sector()` принимает **индекс карты**; `mass_erase()` — 1/2/3 для банка 1/2/обоих.
- `GetFlashSector(index)` возвращает `{address, size, bank, number}`: равномерные карты вычисляются, неравномерные берутся из таблицы. Формат конфигурации 4 требует согласованного шаблона `flash_config.h.in`.

```cpp
if (flash_base::init() != SysInitStatus::InitOK) return;
if (flash_base::enable_access() != SysStatus::OK) return;

// Используйте сектор, зарезервированный проектом, и никогда — сектор с кодом.
if (flash_base::erase_sector(settings_sector) != SysStatus::OK) return;
const uint32_t address = GetFlashSector(settings_sector).address;
const uint32_t values[] = {0x12345678, 0xDEADBEEF};
const SysStatus written = flash_base::write(address, values, 2);
if (written != SysStatus::OK) return;
const uint32_t value = flash_base::read<uint32_t>(address);
```

- Вызывайте из основного цикла при работающем SysTick — не из прерываний и критических секций. Таймаут на аппаратную операцию: **50 с на F4/F7, 1 с на G0**. Выборка кода из занятого банка останавливает и SysTick, поэтому таймауты не гарантируют реальное время.
- **G0** пишет по 64 бита по адресам с выравниванием 8 байт (источник может быть невыровненным; последний блок дополняется FF). **F4/F7** (3–3,3 В, без VPP) пишут по 8/16/32 бита; 64-битный элемент — две записи по 32 бита.
- Устаревшее содержимое кешей Flash инвалидируется; D-cache SRAM глобально не трогается.
- Прямой `write()` не проверяет источник, размер, диапазон и выравнивание — это делает вызывающий код. Область журнала проверяет `flash_data::SetUp()`.

### flash_data — структура в одном секторе

```cpp
static uint8_t settings_buf[32]{};
constinit flash_data settings(settings_sector, settings_buf, sizeof(settings_buf));

if (settings.SetUp() != SysInitStatus::InitOK) return;
const auto loaded = settings.read_data();     // DATA_OK / NO_DATA / DATA_NOT_VALID
// Изменить settings_buf, затем сохранить и проверить результат.
const SysStatus saved = settings.write_data();
if (saved != SysStatus::OK) {
    const uint32_t flags = FLASH->SR;
    (void)flags;   // посмотреть до начала следующей операции Flash
}
```

- Каждая запись пишется как **DATA_WRITING → данные → DATA_OK**; только после этого предыдущая помечается невалидной. Поиск выбирает последнюю завершённую запись.
- Когда сектор заполнен, он стирается — потеря питания во время стирания/перезаписи может уничтожить последнюю версию. CRC нет.
- Заголовок: 1 байт на F4/F7 (`FF → 7F → 0F`, `00` — невалидна); **16 байт на G0** (первое слово FF… → `00000000FFFFFFFF` → все нули; второе слово `FFFFFFFF00000000` — невалидна, имеет приоритет). Все значения определены один раз в `Data_Status`; численные значения на G0 изменились — сравнивайте именованные значения. `DATA_CORRUPT` удалён.
- Если пометить старую запись невалидной не удалось, `write_data()` вернёт ошибку, но новая запись останется выбранной.
- Старый 8-байтный заголовок G0 несовместим — начните со стёртого сектора журнала.

---

## 12. DAC

**Файлы:** `dac.hpp`, `dac.cpp` · **Подробнее:** [DAC.md](DAC.md)

Подключение: `"drivers": ["DAC"]`. CH1 — PA4, CH2 — PA5 (при наличии).

```cpp
constinit DAC_Module output(DAC_Module::_1::instance, DAC_Module::CHANNEL::CH1);
// После инициализации System/RCC:
if (output.SetUp() == SysInitStatus::InitOK) {
    SysStatus written = output.SetValue(2048);   // проверить written == SysStatus::OK
}
output.DeInit();
```

- `SetUp()` выбирает прямой 12-битный вывод с буфером, начальный код 0.
- `SetValue()`: `NotInit` до настройки / после `DeInit()`; `Error` при значении > 4095 (прежний код сохраняется); `OK` после одной записи в регистр. Время запуска/установления аналоговой части — по datasheet.
- Повторный `SetUp()` требует `DeInit()`. `DeInit()` выключает только этот канал и сохраняет второй канал, тактирование и аналоговый режим вывода. Копирование/перемещение запрещены; деструктор ничего не делает.
- У F410 есть только CH1; подключение модуля на МК без ЦАП — ошибка компиляции.

---

## Порядок инициализации в main()

```cpp
int main() {
    System::Init();      // 1. FPU, Flash-акселератор/кеши, SysTick @ HSI

    // 2. Тактирование
    constexpr auto clocks = ClockSystem::CalcPLL(64 * MHz, ClockSystem::PLL_ClockSource::HSI);
    if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
        System::DebugTrap("Clock init");

    // 3. Периферия — в любом порядке; проверять каждый результат
    uart.SetUp();
    spi.SetUp(...);
    rtc.SetUp(...);

    // 4. DMA закреплены конструкторами: их настраивают UART/SPI SetUp и TIM SetUpPWM.
    tim_pwm.SetUp(...);
    tim_pwm.SetUpPWM(TIM::Channel::CH1);

    // 5. Явный запуск таймеров
    periodic_timer.Start();

    for (;;) { /* основной цикл */ }
}
```

---

## Зависимости между модулями

```text
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

`flash.hpp` зависит только от `system.hpp` и `flash_config.h`. `dac.hpp` зависит от `system.hpp` и `gpio.hpp`.

---

## Добавление нового МК

1. Добавить `#include "stm32xxxx.h"` в `system.hpp`.
2. Обновить описания instance/выводов/DMA в соответствующих `*_defs.hpp` и при необходимости аппаратные дескрипторы DMA в `dma.cpp`.
3. Добавить заглушки векторов в `irq_registry_config.h`.
4. Добавить проверенные пределы частот/PLL/питания в `rcc_defs.hpp`.
5. При необходимости добавить макросы регистровых абстракций (например, `RCC_GPIO_EN_REG` в `gpio.hpp`).
