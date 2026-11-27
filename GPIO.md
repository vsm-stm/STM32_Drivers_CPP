# GPIO / PIN — Detailed Description

*[English](#english) · [Русский](#русский)*

---

## English

## Table of Contents

1. [Why This Is Needed](#1-why-this-is-needed)
2. [TYPE Encoding — One Byte Instead of Three Registers](#2-type-encoding--one-byte-instead-of-three-registers)
3. [SetUp() — What Actually Gets Written to the Registers](#3-setup--what-actually-gets-written-to-the-registers)
4. [Levels: GetLevel/SetLevel/TogglePin](#4-levels-getlevelsetleveltogglepin)
5. [Bit-Banding (F4 Only)](#5-bit-banding-f4-only)
6. [Constexpr Pin Tables and Protection Against Typos](#6-constexpr-pin-tables-and-protection-against-typos)
7. [PinArray<N> — a Parallel Bus](#7-pinarrayn--a-parallel-bus)
8. [Usage Examples](#8-usage-examples)
9. [Edge Cases](#9-edge-cases)
10. [Hardware Implementation and Family Support](#10-hardware-implementation-and-family-support)

---

## 1. Why This Is Needed

`PIN` is one object per GPIO pin. The public API is shared by F4/F7/G0/L0/G4. Family clocking and capabilities are selected at compilation; the common register operations need no additional layer. See section 10. It can be used in two ways:

| Use | Constructor | Checked |
| --- | --- | --- |
| Ordinary GPIO — LED, button | `PIN(GPIOA, 5)` (CMSIS pointer) | no checks |
| Compile-time constant — pin tables of UART/SPI/TIM ("this pin belongs to USART1 with AF7") | `PIN(GPIOA_BASE, 9, 7, USART1_BASE)` (numeric address) | port, pin number and AF checked at build time |

```cpp
PIN led(GPIOA, 5);                 // ordinary GPIO object
led.SetUp(PIN::TYPE::OUTPUT_PushPull);
led.SetLevel(true);

// Compile-time table entry, as in the USART and TIM hardware definitions.
static constexpr PIN PA9 = { GPIOA_BASE, 9, 7, USART1_BASE };  // AF7, belongs to USART1
```

C++20 or later is required.

Read-only accessors: `PORT()` returns the bound port pointer, `GetNumber()` the pin number (e.g. for EXTI routing). Neither can rebind the pin.

---

## 2. TYPE Encoding — One Byte Instead of Three Registers

GPIO configuration has three parts — mode, pull resistor and output type. Instead of passing three parameters (and re-checking which combinations make sense), every **valid** combination is a single value of `enum class TYPE : uint8_t`:

```text
Bit:     7 6 5 4 3 2 1 0
         │     │ │ │ │ └─ mode  [1:0]  — 00 INPUT, 01 OUTPUT, 10 AF, 11 ANALOG
         │     │ │ └─┴─── pull  [3:2]  — 00 NoPull, 01 PullUp, 10 PullDown
         │     └─┴─────── output_type [4] — 0 PushPull, 1 OpenDrain
         └───────────────  (unused)
```

```cpp
OUTPUT_OD_PulUp = (OUTPUT << mode_pos) | (OpenDrain << output_type_pos) | (PullUP << pull_pos)
```

- `TYPE::OUTPUT_OD_PulUp` is one constant, not three arguments.
- `SetUp()` unpacks it with three shifts/masks.
- All 10 predefined values (`INPUT_NO_Pull` … `ANALOG`) are listed in `TYPE`; meaningless combinations such as "ANALOG + PullUp" simply do not exist.

---

## 3. SetUp() — What Actually Gets Written to the Registers

```cpp
SysInitStatus SetUp(TYPE type = TYPE::INPUT_NO_Pull,
                    OUTPUT_SPEED speed = OUTPUT_SPEED::Low,
                    LVL initial_level = LVL::LOW);                // uses the stored AF
SysInitStatus SetUp(TYPE type, OUTPUT_SPEED speed, uint8_t af_override,
                    LVL initial_level = LVL::LOW);                // explicit AF
SysInitStatus SetUp(TYPE type, uint8_t af_override,
                    LVL initial_level = LVL::LOW);                // explicit AF, Low speed
```

`PIN::SetUp()` programs the registers directly in `src/gpio.cpp`. After unpacking `TYPE`:

1. **Clock enable.** The port index is computed from the address, not from a table:

   ```cpp
   gpio_id = (_port_base - GPIOA_BASE) / (GPIOB_BASE - GPIOA_BASE);  // 0=A, 1=B, 2=C, ...
   auto& clock = gpio_hw::ClockEnableRegister();
   clock |= gpio_hw::port_a_clock << gpio_id;
   (void)static_cast<uint32_t>(clock); // Read back before GPIO access.
   ```

   Ports sit at 0x400-byte steps on F4/F7/G0/L0/G4 (absent ports leave gaps). The step comes from the CMSIS addresses, so GCC turns the division into a shift — no software division call. No extra port index is stored.

2. **One read-modify-write per register.** The new field replaces the old one in a single write (no separate "clear, then OR").

3. **ANALOG** clears the pin's fields in OTYPER/OSPEEDR/PUPDR/AFR and sets MODER = 0b11. No pull resistor.

4. **OUTPUT and AF** get the requested output type and speed. For INPUT/ANALOG these fields are cleared.

5. **AFR** gets the AF in AF mode, otherwise 0. `pin >> 3` selects AFR[0]/AFR[1], `(pin & 7) * 4` the field inside it.

6. **Initial OUTPUT level** is written through BSRR **before** the pin becomes an output (default LOW, also on repeated setup). Other modes leave the output latch alone. MODER is written **last and once**, so an already-driven line is never released through a temporary INPUT state.

The register writes are sequential, not one atomic transaction — serialize GPIO configuration yourself.

```cpp
led.SetUp(PIN::TYPE::OUTPUT_PushPull, PIN::OUTPUT_SPEED::Low, PIN::LVL::HIGH);
```

---

## 4. Levels: GetLevel/SetLevel/TogglePin

```cpp
inline bool GetLevel() const { return (PORT()->IDR >> pin) & 0x1u; }

inline void SetLevel(bool lvl) {
    PORT()->BSRR = 1u << (pin + 16 * static_cast<uint8_t>(!lvl));
}

inline void TogglePin() { PORT()->ODR ^= (0x1u << pin); }
```

| Method | Register | Safe against an ISR changing another pin of the same port? |
| --- | --- | --- |
| `GetLevel()` | reads IDR (the real input level, not the output latch) | yes |
| `SetLevel()` | one write to BSRR | **yes** |
| `TogglePin()` | read-modify-write of ODR | **no** |

**Why `SetLevel()` uses BSRR.** Writing 1 to BSRR bits [15:0] *sets* a pin, writing 1 to bits [31:16] *resets* it; zeros change nothing. `pin + 16*(!lvl)` selects the half without a branch. Because BSRR never reads ODR, an interrupt cannot "roll back" a change to another pin of the same port. With `ODR |= …` / `ODR &= …` it could: an ISR changing another pin between the read and the write would be overwritten.

**`TogglePin()`** still uses `ODR ^=`, which reads and rewrites the whole output register. Do not change other outputs of the same port from an interrupt during the toggle. (An "ODR read + BSRR write" toggle would protect other pins, but concurrent toggles of the *same* pin would still not be atomic.)

---

## 5. Bit-Banding (F4 Only)

```cpp
#if defined(STM32F4)
inline bool GetLevel_BB() const { return BIT_BB(&PORT()->IDR, pin); }
inline void SetLevel_BB(bool lvl) { BIT_BB(&PORT()->ODR, pin) = lvl; }
inline void TogglePin_BB() { BIT_BB(&PORT()->ODR, pin) = !BIT_BB(&PORT()->ODR, pin); }
#endif
```

**What bit-banding is.** A Cortex-M3/M4 hardware feature: every bit of the peripheral region is mirrored as a separate 32-bit word in an "alias" region. Writing 1/0 to that word sets/clears exactly one bit of the real register — in hardware, without a read-modify-write. So `SetLevel_BB()` cannot race with a change to a neighbouring ODR bit, even without BSRR.

- This API remains available **only on F4**. The G4 GPIO port does not add bit-band methods; on F7/G0/L0 they are also absent. Calling them outside `#if defined(STM32F4)` is a compile error.
- `TogglePin_BB()` is a separate bit read and bit write. Each touches only that bit, but the toggle as a whole is not atomic: another operation on the same pin can happen in between.
- A whole-register ODR write elsewhere can still overwrite a bit changed through bit-banding.

---

## 6. Constexpr Pin Tables and Protection Against Typos

```cpp
consteval PIN(uintptr_t port_base, uint32_t p, uint32_t a = 0,
              uint32_t periph = 0) noexcept;
```

The **`consteval`** constructor takes `GPIOx_BASE` and rejects at compile time:

- a GPIO port that does not exist on the selected device;
- a pin number outside 0…15;
- an AF outside 0…7 on L0 or 0…15 on F4/F7/G0/G4.

Arguments are checked **before** they are stored in 8-bit fields, so a typo like 256 cannot silently wrap into a valid value. The error appears as a compiler message with the reason (via `STM32_CONFIG_ERROR`). Nothing is checked at run time.

**Immutable binding:**

- Port, pin number and stored AF are private const fields.
- `periph_base` (public, read-only) is the address of the peripheral the pin belongs to — **not** the GPIO port. USART/SPI/I2C constructors compare it with their instance.
- Copy/move construction is allowed; assigning another `PIN` is deleted, so an existing object cannot be rebound.
- Assigning a `bool` (`pin = true;`) still sets the output level.

**AF:** `SetUp(type, speed)` uses the stored AF. An explicit `af_override` selects another AF at run time without changing the stored one (the caller provides 0…7 on L0 or 0…15 on F4/F7/G0/G4). Range checks do not verify that a given pin really supports a given function on your MCU.

**Peripheral tables.** USART/SPI/I2C tables use the format `{GPIOx_BASE, pin, af, peripheral}`. ADC and TIM have their own checked descriptors (`ADC_PIN`, `TIM_PIN`) built on the same consteval `PIN` constructor. The check "this pin belongs to the selected peripheral" lives in each driver's constructor.

**The pointer constructor** is kept for ordinary GPIO:

```cpp
PIN(GPIO_TypeDef* port, uint8_t p, uint8_t a = 0) noexcept;
PIN led(GPIOA, 5);
```

It stores the pointer, pin and AF **without any checks** — the caller passes a valid port, pin in 0…15 and AF in the selected family's range. It is not `consteval` and cannot be used in `constinit` declarations.

---

## 7. PinArray<N> — a Parallel Bus

```cpp
template<size_t N> class PinArray { PIN pins[N]; ... };
```

`PinArray` groups `N` independent pins so you can read or write them as one number. Bit `i` of the value corresponds to pin `i` of the constructor list — and the pins may be on **different ports**.

Group operations are generated with C++17 fold expressions (`std::index_sequence`) — unrolled at compile time, not a run-time loop:

```cpp
uint32_t GetLevelAll() const {
    return ((static_cast<uint32_t>(pins[I].GetLevel()) << I) | ...);  // I = 0..N-1
}
```

So `GetLevelAll()`/`SetLevelAll()`/`ToggleAll()` are simply `N` calls of `GetLevel()`/`SetLevel()`/`TogglePin()`. You pay one register access per pin, and a group write is **not** one simultaneous hardware transaction — but your code gets a single call and a single `uint32_t` instead of packing bits by hand.

- `N` is limited to 1…32 (`static_assert`) — the value must fit a `uint32_t`.
- Elements are copy-constructed directly, never rebound by assignment.
- `SetUpAll(type, …)` configures every pin with one common AF (default 0, **not** each pin's stored AF) and returns the first status other than `InitOK`. Outputs start LOW.

---

## 8. Usage Examples

- [Input buses, initial HIGH and read-only pin queries](#input-buses-initial-high-and-read-only-pin-queries)

### An Ordinary Output (LED)

```cpp
PIN led(GPIOA, 5);
led.SetUp(PIN::TYPE::OUTPUT_PushPull);
led.SetLevel(true);
led.TogglePin();
```

### An Input with Pull-Up (a Button on PullUp, Active Level LOW)

```cpp
PIN button(GPIOC, 13);
button.SetUp(PIN::TYPE::INPUT_PullUp);
bool pressed = !button.GetLevel();
```

### An Alternate-Function Pin Without a Table (Manual AF)

```cpp
PIN pa9(GPIOA, 9);
pa9.SetUp(PIN::TYPE::AF_PushPull, /*af_override=*/7);   // AF7 = USART1_TX on F4
```

### Bit-Banding on F4 — Access to One Output Bit

```cpp
PIN strobe(GPIOB, 0);
strobe.SetUp(PIN::TYPE::OUTPUT_PushPull);
strobe.TogglePin_BB();   // separate bit read/write; do not change this pin concurrently
```

### PinArray — a 4-Bit Bus for a Segment Display

```cpp
static constexpr PIN seg_pins_raw[] = {
    PIN(GPIOB_BASE, 0), PIN(GPIOB_BASE, 1), PIN(GPIOB_BASE, 4), PIN(GPIOB_BASE, 5)
};
PinArray<4> seg_pins(seg_pins_raw);

seg_pins.SetUpAll(PIN::TYPE::OUTPUT_PushPull);
seg_pins.SetLevelAll(0b1010);              // write all 4 bits with one call
uint32_t state = seg_pins.GetLevelAll();   // read all 4 bits with one call
seg_pins[2].SetLevel(true);                // access a single pin of the array
```

### A Compile-Time Table for a Peripheral (USART and TIM)

```cpp
struct MySpiPins {
    struct SCK { static constexpr PIN PA5 = { GPIOA_BASE, 5, 5, SPI1_BASE }; };
};
// Usage:
// constinit SPI spi(SPI::_1::instance, SPI::Direction::TXRX, MySpiPins::SCK::PA5, ...);
// The SPI constructor compares .periph_base with SPI1; with constinit a mismatch
// is a compile error that names the reason.
```

---

### Input buses, initial HIGH and read-only pin queries

PinArray bit positions follow array order, even across GPIO ports. INPUT_PullUp below reads two independent inputs. The output starts HIGH before enabling its mode, avoiding an initial LOW pulse. PORT/GetNumber/IsValid query the binding; operator bool reads a level and is not a binding-validity check. Group operations are separate per-pin MMIO accesses, not simultaneous multi-port transactions. PinArray::operator[] permits per-pin level/configuration changes; the hardware binding itself cannot be reassigned.

```cpp
#include "gpio.hpp"
constexpr PIN input_pins[] = {PIN(GPIOA_BASE, 0), PIN(GPIOB_BASE, 1)};
PinArray<2> inputs(input_pins);
constinit PIN enable(GPIOC_BASE, 13);
void Setup() {
    if (inputs.SetUpAll(PIN::TYPE::INPUT_PullUp) != SysInitStatus::InitOK ||
        enable.SetUp(PIN::TYPE::OUTPUT_PushPull, PIN::OUTPUT_SPEED::Low,
                     PIN::LVL::HIGH) != SysInitStatus::InitOK)
        System::DebugTrap("GPIO");
}
uint32_t ReadInputs() { return inputs.GetLevelAll(); }
void SetEnabled(bool on) { enable = on; }
bool IsHigh() { return static_cast<bool>(enable); }
```

---

## 9. Edge Cases

- **Two constructors.** `GPIOx_BASE` → checked at compile time. `GPIO_TypeDef*` → no checks at all.
- **`PIN{}`** represents an absent optional signal (e.g. an unused UART direction). Do not call `SetUp()` or level methods on it; `IsValid()` returns `false`.
- **`Reset()`** (private) puts the pin into `INPUT_NO_Pull`. It is never called automatically.
- **Bit-banding on F7/G0** — the `*_BB` methods do not exist; calling them is a compile error, not a run-time failure.
- **`PinArray` does not check for duplicates or a common port.** This is deliberate (buses may span ports), but a repeated pin makes several bit positions read/write the same physical pin.
- **`explicit operator bool()` / `operator=(bool)`.** `if (button)` reads the level and `pin = true;` sets it. The conversion is explicit, so `bool b = pin;` does not compile — use `GetLevel()`. Keep this in mind when refactoring: `if (pin)` is a level read, not a "pin exists" check.

---

## 10. Hardware Implementation and Family Support

| File | Responsibility |
| --- | --- |
| `src/gpio.hpp` | Fixed pin binding, constructor diagnostics, short inline register operations and `PinArray` |
| `src/gpio.cpp` | Direct implementation of `PIN::SetUp()` |
| `src/hardware/gpio.hpp` | Compile-time choice of clock register, AF range and available CMSIS ports |

| Family | Port clock enable | AF range |
| --- | --- | --- |
| F4/F7 | `RCC->AHB1ENR` | 0…15 |
| G0 | `RCC->IOPENR` | 0…15 |
| L0 | `RCC->IOPENR` | 0…7 |
| G4 | `RCC->AHB2ENR` | 0…15 |

Family differences are limited to compile-time clock selection and capability descriptions in `gpio_hw`. No hardware object, virtual dispatch or additional per-pin fields are needed. Short IDR/BSRR/ODR and F4 bit-band operations remain directly in the public header. `SetUp()` is implemented in `gpio.cpp`, without an intermediate `Configure()` function. The API, register programming order and caller's synchronization responsibility are retained.

The numeric-address constructor checks pin, AF range and CMSIS port presence at compilation. Runtime AF overrides and the pointer constructor remain the caller's responsibility. G0 allows AF8…AF15 where the pin and peripheral support them, for example AF10 on G0B1 LPUART2. Port presence does not prove that every pin is bonded in the selected package.

```cpp
// Works with the same API on L0 and G4, and on the existing families.
constinit PIN output(GPIOA_BASE, 5);
constinit PIN button(GPIOB_BASE, 0);

void InitPins()
{
    output.SetUp(PIN::TYPE::OUTPUT_PushPull, PIN::OUTPUT_SPEED::Low,
                 PIN::LVL::HIGH);
    button.SetUp(PIN::TYPE::INPUT_PullUp);
}
```

The example assumes these pins are available in the selected package. Electrical speed limits must be taken from that device's datasheet: enum names are slew-rate choices, not guaranteed frequencies. AF ranges and register operations can be checked against ST's [L0 GPIO LL](https://github.com/STMicroelectronics/stm32l0xx-hal-driver/blob/master/Inc/stm32l0xx_ll_gpio.h) and [G4 GPIO LL](https://github.com/STMicroelectronics/stm32g4xx-hal-driver/blob/master/Inc/stm32g4xx_ll_gpio.h); these sources are references, not library dependencies.

Validation of this GPIO-only change (GCC 13.3.1, C++20):

- GPIO and declarations for every CMSIS-available port compile for 81 header selections: F4 23, F7 14, G0 12, L0 19, G4 13.
- Five representative GPIO/probe links passed without the rest of the driver library: F429, F767, G031, L072, G474.
- 25 invalid constructor cases fail with the expected diagnostic; the ARM `PIN` size remains 12 bytes and the probes need no dynamic global initialization.
- A host register model passed 40,960 configurations, including preservation of unrelated fields, a single write per configuration register, initial OUTPUT level and MODER-last ordering. This is not an electrical hardware test.
- At `-Os`, the checked level operations and pin-group writes retain their instruction mix and code size. No out-of-line backend functions or software division helpers are emitted. The `gpio.o` text size changes from 220 to 224 bytes on F429, 224 to 228 on F767, and remains 224 on G031; the split is not claimed to be byte-for-byte identical.

Full L0/G4 firmware builds and hardware operation remain unverified.

GPIO, System and RCC now support L0/G4 in isolation. The whole driver library still needs Flash and the remaining peripheral ports. Reset-clock startup and System limitations are described in [System.md](System.md#11-source-layout-and-family-selection).

---

## See Also

- [System.md](System.md) — `BIT_BB`/`BB_RD`/`BB_WR`, used by the `*_BB()` methods (§5).
- [UART.md](UART.md), [TIM.md](TIM.md), [SPI.md](SPI.md) — use the constexpr pin tables (§6) and the `periph_base` check to reject "a pin of the wrong peripheral".

---

## Русский

## Содержание

1. [Зачем это нужно](#1-зачем-это-нужно)
2. [Кодирование TYPE — один байт вместо трёх регистров](#2-кодирование-type--один-байт-вместо-трёх-регистров)
3. [SetUp() — что реально пишется в регистры](#3-setup--что-реально-пишется-в-регистры)
4. [Уровни: GetLevel/SetLevel/TogglePin](#4-уровни-getlevelsetleveltogglepin)
5. [Bit-banding (только F4)](#5-bit-banding-только-f4)
6. [Constexpr-таблицы пинов и защита от опечаток](#6-constexpr-таблицы-пинов-и-защита-от-опечаток)
7. [PinArray<N> — параллельная шина](#7-pinarrayn--параллельная-шина)
8. [Примеры использования](#8-примеры-использования)
9. [Угловые случаи](#9-угловые-случаи)
10. [Аппаратная реализация и поддержка семейств](#10-аппаратная-реализация-и-поддержка-семейств)

---

## 1. Зачем это нужно

`PIN` — один объект на один вывод GPIO. Публичный API общий для F4/F7/G0/L0/G4. Тактирование и возможности семейства выбираются при компиляции; общие операции с регистрами не требуют дополнительного слоя. См. раздел 10. Используются два варианта конструктора:

| Применение | Конструктор | Проверки |
| --- | --- | --- |
| Обычный GPIO — светодиод, кнопка | `PIN(GPIOA, 5)` (CMSIS-указатель) | нет |
| Константа времени компиляции — таблицы выводов UART/SPI/TIM («этот вывод принадлежит USART1 с AF7») | `PIN(GPIOA_BASE, 9, 7, USART1_BASE)` (числовой адрес) | порт, номер и AF проверяются при сборке |

```cpp
PIN led(GPIOA, 5);                 // обычный объект GPIO
led.SetUp(PIN::TYPE::OUTPUT_PushPull);
led.SetLevel(true);

// Элемент таблицы времени компиляции, как в аппаратных определениях USART и TIM.
static constexpr PIN PA9 = { GPIOA_BASE, 9, 7, USART1_BASE };  // AF7, принадлежит USART1
```

Нужен C++20 или новее.

Методы только для чтения: `PORT()` возвращает указатель на порт, `GetNumber()` — номер вывода (например, для маршрутизации EXTI). Перепривязать вывод через них нельзя.

---

## 2. Кодирование TYPE — один байт вместо трёх регистров

Настройка GPIO состоит из трёх частей — режим, подтяжка и тип выхода. Вместо трёх параметров (и повторной проверки, какие сочетания допустимы) каждое **допустимое** сочетание — одно значение `enum class TYPE : uint8_t`:

```text
Бит:     7 6 5 4 3 2 1 0
         │     │ │ │ │ └─ mode  [1:0]  — 00 INPUT, 01 OUTPUT, 10 AF, 11 ANALOG
         │     │ │ └─┴─── pull  [3:2]  — 00 NoPull, 01 PullUp, 10 PullDown
         │     └─┴─────── output_type [4] — 0 PushPull, 1 OpenDrain
         └───────────────  (не используется)
```

```cpp
OUTPUT_OD_PulUp = (OUTPUT << mode_pos) | (OpenDrain << output_type_pos) | (PullUP << pull_pos)
```

- `TYPE::OUTPUT_OD_PulUp` — одна константа, а не три аргумента.
- `SetUp()` распаковывает её тремя сдвигами/масками.
- Все 9 предопределённых значений (`INPUT_NO_Pull` … `ANALOG`) перечислены в `TYPE`; бессмысленных сочетаний вроде «ANALOG + PullUp» просто не существует.

---

## 3. SetUp() — что реально пишется в регистры

```cpp
SysInitStatus SetUp(TYPE type = TYPE::INPUT_NO_Pull,
                    OUTPUT_SPEED speed = OUTPUT_SPEED::Low,
                    LVL initial_level = LVL::LOW);                // сохранённый AF
SysInitStatus SetUp(TYPE type, OUTPUT_SPEED speed, uint8_t af_override,
                    LVL initial_level = LVL::LOW);                // явный AF
SysInitStatus SetUp(TYPE type, uint8_t af_override,
                    LVL initial_level = LVL::LOW);                // явный AF, скорость Low
```

`PIN::SetUp()` напрямую настраивает регистры в `src/gpio.cpp`. После распаковки `TYPE`:

1. **Включение тактирования.** Индекс порта вычисляется из адреса, а не из таблицы:

   ```cpp
   gpio_id = (_port_base - GPIOA_BASE) / (GPIOB_BASE - GPIOA_BASE);  // 0=A, 1=B, 2=C, ...
   auto& clock = gpio_hw::ClockEnableRegister();
   clock |= gpio_hw::port_a_clock << gpio_id;
   (void)static_cast<uint32_t>(clock); // Read back before GPIO access.
   ```

   Порты на F4/F7/G0/L0/G4 расположены с шагом 0x400 (отсутствующие оставляют пропуски). Шаг берётся из адресов CMSIS, поэтому GCC превращает деление в сдвиг — вызова программного деления нет. Дополнительный индекс порта не хранится.

2. **Одно «прочитать-изменить-записать» на регистр.** Новое поле заменяет старое одной записью (без отдельных «очистить, затем OR»).

3. **ANALOG** обнуляет поля вывода в OTYPER/OSPEEDR/PUPDR/AFR и ставит MODER = 0b11. Подтяжка не включается.

4. **OUTPUT и AF** получают заданные тип выхода и скорость. Для INPUT/ANALOG эти поля обнуляются.

5. **AFR** получает AF только в режиме AF, иначе 0. `pin >> 3` выбирает AFR[0]/AFR[1], `(pin & 7) * 4` — поле внутри него.

6. **Начальный уровень выхода** записывается через BSRR **до** того, как вывод станет выходом (по умолчанию LOW, в том числе при повторной настройке). Другие режимы выходную защёлку не трогают. MODER записывается **последним и один раз**, поэтому уже управляемая линия никогда не «отпускается» через временный режим INPUT.

Записи регистров последовательны и не образуют одной атомарной операции — согласование настройки GPIO остаётся за вызывающим кодом.

```cpp
led.SetUp(PIN::TYPE::OUTPUT_PushPull, PIN::OUTPUT_SPEED::Low, PIN::LVL::HIGH);
```

---

## 4. Уровни: GetLevel/SetLevel/TogglePin

```cpp
inline bool GetLevel() const { return (PORT()->IDR >> pin) & 0x1u; }

inline void SetLevel(bool lvl) {
    PORT()->BSRR = 1u << (pin + 16 * static_cast<uint8_t>(!lvl));
}

inline void TogglePin() { PORT()->ODR ^= (0x1u << pin); }
```

| Метод | Регистр | Безопасен, если ISR меняет другой вывод того же порта? |
| --- | --- | --- |
| `GetLevel()` | читает IDR (реальный уровень входа, а не выходную защёлку) | да |
| `SetLevel()` | одна запись в BSRR | **да** |
| `TogglePin()` | «прочитать-изменить-записать» ODR | **нет** |

**Почему `SetLevel()` использует BSRR.** Запись 1 в биты [15:0] BSRR *устанавливает* вывод, запись 1 в биты [31:16] — *сбрасывает*; нули ничего не меняют. `pin + 16*(!lvl)` выбирает половину без ветвления. Поскольку BSRR вообще не читает ODR, прерывание не может «откатить» изменение другого вывода того же порта. С `ODR |= …` / `ODR &= …` это возможно: изменение другого вывода из ISR между чтением и записью было бы затёрто.

**`TogglePin()`** по-прежнему использует `ODR ^=`, то есть читает и перезаписывает весь выходной регистр. Не меняйте другие выходы того же порта из прерывания во время переключения. (Переключение «чтение ODR + запись BSRR» защитило бы другие выводы, но одновременные переключения *одного и того же* вывода всё равно не были бы атомарными.)

---

## 5. Bit-banding (только F4)

```cpp
#if defined(STM32F4)
inline bool GetLevel_BB() const { return BIT_BB(&PORT()->IDR, pin); }
inline void SetLevel_BB(bool lvl) { BIT_BB(&PORT()->ODR, pin) = lvl; }
inline void TogglePin_BB() { BIT_BB(&PORT()->ODR, pin) = !BIT_BB(&PORT()->ODR, pin); }
#endif
```

**Что такое bit-banding.** Аппаратная возможность Cortex-M3/M4: каждый бит периферийной области отображается на отдельное 32-битное слово в области-«псевдониме» (alias). Запись 1/0 в это слово устанавливает/сбрасывает ровно один бит настоящего регистра — аппаратно, без «прочитать-изменить-записать». Поэтому `SetLevel_BB()` не может конфликтовать с изменением соседнего бита ODR даже без BSRR.

- Этот API остаётся доступным **только на F4**. При переносе GPIO на G4 bit-band-методы не добавляются; на F7/G0/L0 они также отсутствуют. Вне `#if defined(STM32F4)` их вызов даёт ошибку компиляции.
- `TogglePin_BB()` — это отдельные чтение и запись бита. Каждое обращение затрагивает только этот бит, но переключение целиком не атомарно: между ними может произойти другая операция с тем же выводом.
- Запись всего ODR в другом месте может затереть бит, изменённый через bit-banding.

---

## 6. Constexpr-таблицы пинов и защита от опечаток

```cpp
consteval PIN(uintptr_t port_base, uint32_t p, uint32_t a = 0,
              uint32_t periph = 0) noexcept;
```

**`consteval`**-конструктор принимает `GPIOx_BASE` и при компиляции отклоняет:

- порт GPIO, которого нет на выбранном МК;
- номер вывода вне 0…15;
- AF вне 0…7 на L0 или 0…15 на F4/F7/G0/G4.

Аргументы проверяются **до** записи в 8-битные поля, поэтому опечатка вроде 256 не может незаметно превратиться в допустимое значение. Ошибка выводится компилятором с текстом причины (через `STM32_CONFIG_ERROR`). Во время работы ничего не проверяется.

**Неизменяемая привязка:**

- Порт, номер и сохранённый AF — private const.
- `periph_base` (публичное, только чтение) — адрес периферии, которой принадлежит вывод, **а не** порта GPIO. Конструкторы USART/SPI/I2C сравнивают его со своим экземпляром.
- Копирование и перемещение при создании разрешены; присваивание другого `PIN` запрещено — существующий объект нельзя перепривязать.
- Присваивание `bool` (`pin = true;`) по-прежнему устанавливает уровень выхода.

**AF:** `SetUp(type, speed)` использует сохранённый AF. Явный `af_override` выбирает другой AF во время работы, не меняя сохранённый (0…7 на L0 или 0…15 на F4/F7/G0/G4 обеспечивает вызывающий). Проверки диапазона не гарантируют, что данный вывод действительно поддерживает данную функцию на вашем МК.

**Таблицы периферии.** Таблицы USART/SPI/I2C используют формат `{GPIOx_BASE, pin, af, peripheral}`. У ADC и TIM свои проверяемые описания (`ADC_PIN`, `TIM_PIN`), построенные на том же consteval-конструкторе `PIN`. Проверка «вывод принадлежит выбранной периферии» находится в конструкторе каждого драйвера.

**Конструктор с указателем** сохранён для обычного GPIO:

```cpp
PIN(GPIO_TypeDef* port, uint8_t p, uint8_t a = 0) noexcept;
PIN led(GPIOA, 5);
```

Он сохраняет указатель, номер и AF **без каких-либо проверок** — вызывающий передаёт допустимый порт, номер в 0…15 и AF в диапазоне выбранного семейства. Он не `consteval` и не подходит для объявлений `constinit`.

---

## 7. PinArray<N> — параллельная шина

```cpp
template<size_t N> class PinArray { PIN pins[N]; ... };
```

`PinArray` объединяет `N` независимых выводов, чтобы читать и писать их одним числом. Бит `i` значения соответствует выводу `i` из списка конструктора — и выводы могут быть на **разных портах**.

Групповые операции генерируются fold-выражениями C++17 (`std::index_sequence`) — развёрнуты при компиляции, а не цикл во время работы:

```cpp
uint32_t GetLevelAll() const {
    return ((static_cast<uint32_t>(pins[I].GetLevel()) << I) | ...);  // I = 0..N-1
}
```

То есть `GetLevelAll()`/`SetLevelAll()`/`ToggleAll()` — это просто `N` вызовов `GetLevel()`/`SetLevel()`/`TogglePin()`. Вы платите одним обращением к регистру на вывод, и групповая запись **не** является одной одновременной аппаратной операцией — зато в коде один вызов и одно значение `uint32_t` вместо ручной упаковки битов.

- `N` ограничен 1…32 (`static_assert`) — значение должно помещаться в `uint32_t`.
- Элементы создаются копированием и никогда не перепривязываются присваиванием.
- `SetUpAll(type, …)` настраивает все выводы с одним общим AF (по умолчанию 0, **а не** сохранённым AF каждого вывода) и возвращает первый статус, отличный от `InitOK`. Выходы стартуют в LOW.

---

## 8. Примеры использования

- [Группа входов, начальный HIGH и запросы о пине](#группа-входов-начальный-high-и-запросы-о-пине)

### Обычный выход (LED)

```cpp
PIN led(GPIOA, 5);
led.SetUp(PIN::TYPE::OUTPUT_PushPull);
led.SetLevel(true);
led.TogglePin();
```

### Вход с подтяжкой (кнопка на PullUp, активный уровень — LOW)

```cpp
PIN button(GPIOC, 13);
button.SetUp(PIN::TYPE::INPUT_PullUp);
bool pressed = !button.GetLevel();
```

### Alternate-function пин без таблицы (ручной AF)

```cpp
PIN pa9(GPIOA, 9);
pa9.SetUp(PIN::TYPE::AF_PushPull, /*af_override=*/7);   // AF7 = USART1_TX на F4
```

### Bit-banding на F4 — доступ к одному выходному биту

```cpp
PIN strobe(GPIOB, 0);
strobe.SetUp(PIN::TYPE::OUTPUT_PushPull);
strobe.TogglePin_BB();   // отдельные чтение/запись; не менять этот вывод параллельно
```

### PinArray — 4-битная шина сегментного индикатора

```cpp
static constexpr PIN seg_pins_raw[] = {
    PIN(GPIOB_BASE, 0), PIN(GPIOB_BASE, 1), PIN(GPIOB_BASE, 4), PIN(GPIOB_BASE, 5)
};
PinArray<4> seg_pins(seg_pins_raw);

seg_pins.SetUpAll(PIN::TYPE::OUTPUT_PushPull);
seg_pins.SetLevelAll(0b1010);              // записать все 4 бита одним вызовом
uint32_t state = seg_pins.GetLevelAll();   // прочитать все 4 бита одним вызовом
seg_pins[2].SetLevel(true);                // доступ к одному выводу массива
```

### Compile-time таблица для периферии (USART и TIM)

```cpp
struct MySpiPins {
    struct SCK { static constexpr PIN PA5 = { GPIOA_BASE, 5, 5, SPI1_BASE }; };
};
// Использование:
// constinit SPI spi(SPI::_1::instance, SPI::Direction::TXRX, MySpiPins::SCK::PA5, ...);
// Конструктор SPI сравнит .periph_base с SPI1; с constinit несовпадение —
// ошибка компиляции с указанием причины.
```

---

### Группа входов, начальный HIGH и запросы о пине

Номера битов PinArray следуют порядку массива, даже на разных портах. INPUT_PullUp ниже читает два независимых входа. Выход сразу настраивается в HIGH до включения режима, без начального LOW-импульса. PORT/GetNumber/IsValid читают привязку; operator bool читает уровень и не проверяет наличие пина. Групповые операции — отдельные обращения к пинам, не одновременная запись нескольких портов. operator[] у PinArray допускает изменение уровня и настройки отдельного пина; сама аппаратная привязка не переназначается.

```cpp
#include "gpio.hpp"
constexpr PIN input_pins[] = {PIN(GPIOA_BASE, 0), PIN(GPIOB_BASE, 1)};
PinArray<2> inputs(input_pins);
constinit PIN enable(GPIOC_BASE, 13);
void Setup() {
    if (inputs.SetUpAll(PIN::TYPE::INPUT_PullUp) != SysInitStatus::InitOK ||
        enable.SetUp(PIN::TYPE::OUTPUT_PushPull, PIN::OUTPUT_SPEED::Low,
                     PIN::LVL::HIGH) != SysInitStatus::InitOK)
        System::DebugTrap("GPIO");
}
uint32_t ReadInputs() { return inputs.GetLevelAll(); }
void SetEnabled(bool on) { enable = on; }
bool IsHigh() { return static_cast<bool>(enable); }
```

---

## 9. Угловые случаи

- **Два конструктора.** `GPIOx_BASE` → проверка при компиляции. `GPIO_TypeDef*` → никаких проверок.
- **`PIN{}`** обозначает отсутствующий необязательный сигнал (например, неиспользуемое направление UART). Не вызывайте для него `SetUp()` и методы уровня; `IsValid()` возвращает `false`.
- **`Reset()`** (приватный) переводит вывод в `INPUT_NO_Pull`. Автоматически нигде не вызывается.
- **Bit-banding на F7/G0** — методов `*_BB` нет; их вызов — ошибка компиляции, а не сбой во время работы.
- **`PinArray` не проверяет повторы и общий порт.** Это осознанно (шина может охватывать разные порты), но повторённый вывод заставит несколько битовых позиций читать/писать один и тот же физический вывод.
- **`explicit operator bool()` / `operator=(bool)`.** `if (button)` читает уровень, `pin = true;` — устанавливает. Преобразование явное, поэтому `bool b = pin;` не скомпилируется — используйте `GetLevel()`. Помните при рефакторинге: `if (pin)` — это чтение уровня, а не проверка «вывод задан».

---

## 10. Аппаратная реализация и поддержка семейств

| Файл | Ответственность |
| --- | --- |
| `src/gpio.hpp` | Неизменяемая привязка пина, диагностика конструктора, короткие inline-операции с регистрами и `PinArray` |
| `src/gpio.cpp` | Прямая реализация `PIN::SetUp()` |
| `src/hardware/gpio.hpp` | Выбор регистра тактирования, диапазона AF и доступных CMSIS-портов при компиляции |

| Семейство | Включение тактирования порта | Диапазон AF |
| --- | --- | --- |
| F4/F7 | `RCC->AHB1ENR` | 0…15 |
| G0 | `RCC->IOPENR` | 0…15 |
| L0 | `RCC->IOPENR` | 0…7 |
| G4 | `RCC->AHB2ENR` | 0…15 |

Различия семейств ограничены выбором тактирования и описанием возможностей при компиляции в `gpio_hw`. Объект аппаратной реализации, виртуальные вызовы и дополнительные поля пина не нужны. Короткие операции IDR/BSRR/ODR и bit-band для F4 находятся прямо в публичном заголовке. `SetUp()` реализован в `gpio.cpp`, без промежуточной функции `Configure()`. Сохраняются API, порядок записи регистров и ответственность вызывающего за синхронизацию.

Конструктор с числовым адресом проверяет номер пина, диапазон AF и наличие порта CMSIS при компиляции. Runtime-переопределение AF и конструктор с указателем остаются ответственностью вызывающего. На G0 допустимы AF8…AF15, если их поддерживают вывод и периферия, например AF10 для LPUART2 на G0B1. Наличие порта не гарантирует вывод каждого пина в выбранном корпусе.

```cpp
// Один API для L0/G4 и уже поддерживаемых семейств.
constinit PIN output(GPIOA_BASE, 5);
constinit PIN button(GPIOB_BASE, 0);

void InitPins()
{
    output.SetUp(PIN::TYPE::OUTPUT_PushPull, PIN::OUTPUT_SPEED::Low,
                 PIN::LVL::HIGH);
    button.SetUp(PIN::TYPE::INPUT_PullUp);
}
```

Предполагается, что выбранные пины доступны в корпусе. Электрические ограничения скорости берутся из datasheet конкретного МК: имена enum обозначают настройки фронтов, а не гарантированные частоты. Диапазоны AF и операции с регистрами можно сверить с [GPIO LL для L0](https://github.com/STMicroelectronics/stm32l0xx-hal-driver/blob/master/Inc/stm32l0xx_ll_gpio.h) и [GPIO LL для G4](https://github.com/STMicroelectronics/stm32g4xx-hal-driver/blob/master/Inc/stm32g4xx_ll_gpio.h): это справочные источники, не зависимости библиотеки.

Проверка изменения только GPIO (GCC 13.3.1, C++20):

- GPIO и объявления для всех доступных CMSIS-портов компилируются для 81 варианта заголовков: F4 — 23, F7 — 14, G0 — 12, L0 — 19, G4 — 13.
- Пройдена изолированная линковка GPIO и проверочного кода для F429, F767, G031, L072, G474 без остальных драйверов.
- 25 ошибочных вариантов конструктора отклоняются с нужной диагностикой; размер `PIN` на ARM остаётся 12 байт, динамическая инициализация глобальных объектов в проверочном коде не нужна.
- Модель регистров на ПК прошла 40 960 конфигураций: проверены сохранение соседних полей, одна запись в каждый регистр настройки, начальный уровень OUTPUT и запись MODER последним. Это не электрическая проверка на железе.
- При `-Os` проверенные операции с уровнями и запись группы пинов сохраняют состав инструкций и размер кода. Отдельных функций аппаратного слоя и вызовов программного деления нет. Размер text у `gpio.o`: F429 — 220 → 224 байта, F767 — 224 → 228, G031 — без изменения, 224 байта. Побайтовая идентичность кода не заявляется.

Полная сборка прошивки L0/G4 и работа на железе не проверены.

GPIO, System и RCC поддерживают L0/G4 изолированно. Для всей библиотеки ещё нужны Flash и оставшиеся переносы периферии. Штатный старт и ограничения System описаны в [System.md](System.md#11-структура-файлов-и-выбор-семейства).

---

## См. также

- [System.md](System.md) — `BIT_BB`/`BB_RD`/`BB_WR`, на которых построены методы `*_BB()` (§5).
- [UART.md](UART.md), [TIM.md](TIM.md), [SPI.md](SPI.md) — используют constexpr-таблицы выводов (§6) и проверку `periph_base`, чтобы отклонить «вывод чужой периферии».
