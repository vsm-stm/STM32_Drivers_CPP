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

---

## 1. Why This Is Needed

`PIN` is one object per GPIO pin. It hides family differences (e.g. the clock-enable register is `RCC->AHB1ENR` on F4/F7 but `RCC->IOPENR` on G0) and can be used in two ways:

| Use | Constructor | Checked |
| --- | --- | --- |
| Ordinary GPIO — LED, button | `PIN(GPIOA, 5)` (CMSIS pointer) | no checks |
| Compile-time constant — pin tables of UART/SPI/TIM ("this pin belongs to USART1 with AF7") | `PIN(GPIOA_BASE, 9, 7, USART1_BASE)` (numeric address) | port, pin number and AF checked at build time |

```cpp
PIN led(GPIOA, 5);                 // ordinary GPIO object
led.SetUp(PIN::TYPE::OUTPUT_PushPull);
led.SetLevel(true);

// Compile-time table entry, as in uart_defs.hpp / tim_defs.hpp:
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
- All 9 predefined values (`INPUT_NO_Pull` … `ANALOG`) are listed in `TYPE`; meaningless combinations such as "ANALOG + PullUp" simply do not exist.

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

What happens in `gpio.cpp`, after unpacking `TYPE`:

1. **Clock enable.** The port index is computed from the address, not from a table:

   ```cpp
   gpio_id = (_port_base - GPIOA_BASE) / (GPIOB_BASE - GPIOA_BASE);  // 0=A, 1=B, 2=C, ...
   RCC_GPIO_EN_REG |= (RCC_GPIOA_EN << gpio_id);
   ```

   Ports sit at 0x400-byte steps on F4/F7/G0 (absent ports leave gaps). The step comes from the CMSIS addresses, so GCC turns the division into a shift — no software division call. No extra port index is stored.

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

- Available **only on F4**. On F7 (Cortex-M7) and G0 (Cortex-M0+) there is no peripheral bit-band region, so these methods do not exist outside `#if defined(STM32F4)` — calling them is a compile error.
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
- an AF outside 0…15.

Arguments are checked **before** they are stored in 8-bit fields, so a typo like 256 cannot silently wrap into a valid value. The error appears as a compiler message with the reason (via `STM32_CONFIG_ERROR`). Nothing is checked at run time.

**Immutable binding:**

- Port, pin number and stored AF are private const fields.
- `periph_base` (public, read-only) is the address of the peripheral the pin belongs to — **not** the GPIO port. USART/SPI/I2C constructors compare it with their instance.
- Copy/move construction is allowed; assigning another `PIN` is deleted, so an existing object cannot be rebound.
- Assigning a `bool` (`pin = true;`) still sets the output level.

**AF:** `SetUp(type, speed)` uses the stored AF. An explicit `af_override` selects another AF at run time without changing the stored one (the caller provides 0…15). Range checks do not verify that a given pin really supports a given function on your MCU.

**Peripheral tables.** USART/SPI/I2C tables use the format `{GPIOx_BASE, pin, af, peripheral}`. ADC and TIM have their own checked descriptors (`ADC_PIN`, `TIM_PIN`) built on the same consteval `PIN` constructor. The check "this pin belongs to the selected peripheral" lives in each driver's constructor.

**The pointer constructor** is kept for ordinary GPIO:

```cpp
PIN(GPIO_TypeDef* port, uint8_t p, uint8_t a = 0) noexcept;
PIN led(GPIOA, 5);
```

It stores the pointer, pin and AF **without any checks** — the caller passes a valid port and pin/AF in 0…15. It is not `consteval` and cannot be used in `constinit` declarations.

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

### A Compile-Time Table for a Peripheral (How uart_defs.hpp/tim_defs.hpp Are Organized)

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

## 9. Edge Cases

- **Two constructors.** `GPIOx_BASE` → checked at compile time. `GPIO_TypeDef*` → no checks at all.
- **`PIN{}`** represents an absent optional signal (e.g. an unused UART direction). Do not call `SetUp()` or level methods on it; `IsValid()` returns `false`.
- **`Reset()`** (private) puts the pin into `INPUT_NO_Pull`. It is never called automatically.
- **Bit-banding on F7/G0** — the `*_BB` methods do not exist; calling them is a compile error, not a run-time failure.
- **`PinArray` does not check for duplicates or a common port.** This is deliberate (buses may span ports), but a repeated pin makes several bit positions read/write the same physical pin.
- **`explicit operator bool()` / `operator=(bool)`.** `if (button)` reads the level and `pin = true;` sets it. The conversion is explicit, so `bool b = pin;` does not compile — use `GetLevel()`. Keep this in mind when refactoring: `if (pin)` is a level read, not a "pin exists" check.

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

---

## 1. Зачем это нужно

`PIN` — один объект на один вывод GPIO. Он скрывает различия семейств (например, регистр включения тактирования — `RCC->AHB1ENR` на F4/F7 и `RCC->IOPENR` на G0) и используется двумя способами:

| Применение | Конструктор | Проверки |
| --- | --- | --- |
| Обычный GPIO — светодиод, кнопка | `PIN(GPIOA, 5)` (CMSIS-указатель) | нет |
| Константа времени компиляции — таблицы выводов UART/SPI/TIM («этот вывод принадлежит USART1 с AF7») | `PIN(GPIOA_BASE, 9, 7, USART1_BASE)` (числовой адрес) | порт, номер и AF проверяются при сборке |

```cpp
PIN led(GPIOA, 5);                 // обычный объект GPIO
led.SetUp(PIN::TYPE::OUTPUT_PushPull);
led.SetLevel(true);

// Элемент таблицы времени компиляции, как в uart_defs.hpp / tim_defs.hpp:
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

Что происходит в `gpio.cpp` после распаковки `TYPE`:

1. **Включение тактирования.** Индекс порта вычисляется из адреса, а не из таблицы:

   ```cpp
   gpio_id = (_port_base - GPIOA_BASE) / (GPIOB_BASE - GPIOA_BASE);  // 0=A, 1=B, 2=C, ...
   RCC_GPIO_EN_REG |= (RCC_GPIOA_EN << gpio_id);
   ```

   Порты на F4/F7/G0 расположены с шагом 0x400 (отсутствующие оставляют пропуски). Шаг берётся из адресов CMSIS, поэтому GCC превращает деление в сдвиг — вызова программного деления нет. Дополнительный индекс порта не хранится.

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

- Доступно **только на F4**. На F7 (Cortex-M7) и G0 (Cortex-M0+) периферийной bit-band-области нет, поэтому эти методы вне `#if defined(STM32F4)` не существуют — их вызов даёт ошибку компиляции.
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
- AF вне 0…15.

Аргументы проверяются **до** записи в 8-битные поля, поэтому опечатка вроде 256 не может незаметно превратиться в допустимое значение. Ошибка выводится компилятором с текстом причины (через `STM32_CONFIG_ERROR`). Во время работы ничего не проверяется.

**Неизменяемая привязка:**

- Порт, номер и сохранённый AF — private const.
- `periph_base` (публичное, только чтение) — адрес периферии, которой принадлежит вывод, **а не** порта GPIO. Конструкторы USART/SPI/I2C сравнивают его со своим экземпляром.
- Копирование и перемещение при создании разрешены; присваивание другого `PIN` запрещено — существующий объект нельзя перепривязать.
- Присваивание `bool` (`pin = true;`) по-прежнему устанавливает уровень выхода.

**AF:** `SetUp(type, speed)` использует сохранённый AF. Явный `af_override` выбирает другой AF во время работы, не меняя сохранённый (значение 0…15 обеспечивает вызывающий). Проверки диапазона не гарантируют, что данный вывод действительно поддерживает данную функцию на вашем МК.

**Таблицы периферии.** Таблицы USART/SPI/I2C используют формат `{GPIOx_BASE, pin, af, peripheral}`. У ADC и TIM свои проверяемые описания (`ADC_PIN`, `TIM_PIN`), построенные на том же consteval-конструкторе `PIN`. Проверка «вывод принадлежит выбранной периферии» находится в конструкторе каждого драйвера.

**Конструктор с указателем** сохранён для обычного GPIO:

```cpp
PIN(GPIO_TypeDef* port, uint8_t p, uint8_t a = 0) noexcept;
PIN led(GPIOA, 5);
```

Он сохраняет указатель, номер и AF **без каких-либо проверок** — вызывающий передаёт допустимый порт и номер/AF в 0…15. Он не `consteval` и не подходит для объявлений `constinit`.

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

### Compile-time таблица для периферии (как устроены uart_defs.hpp/tim_defs.hpp)

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

## 9. Угловые случаи

- **Два конструктора.** `GPIOx_BASE` → проверка при компиляции. `GPIO_TypeDef*` → никаких проверок.
- **`PIN{}`** обозначает отсутствующий необязательный сигнал (например, неиспользуемое направление UART). Не вызывайте для него `SetUp()` и методы уровня; `IsValid()` возвращает `false`.
- **`Reset()`** (приватный) переводит вывод в `INPUT_NO_Pull`. Автоматически нигде не вызывается.
- **Bit-banding на F7/G0** — методов `*_BB` нет; их вызов — ошибка компиляции, а не сбой во время работы.
- **`PinArray` не проверяет повторы и общий порт.** Это осознанно (шина может охватывать разные порты), но повторённый вывод заставит несколько битовых позиций читать/писать один и тот же физический вывод.
- **`explicit operator bool()` / `operator=(bool)`.** `if (button)` читает уровень, `pin = true;` — устанавливает. Преобразование явное, поэтому `bool b = pin;` не скомпилируется — используйте `GetLevel()`. Помните при рефакторинге: `if (pin)` — это чтение уровня, а не проверка «вывод задан».

---

## См. также

- [System.md](System.md) — `BIT_BB`/`BB_RD`/`BB_WR`, на которых построены методы `*_BB()` (§5).
- [UART.md](UART.md), [TIM.md](TIM.md), [SPI.md](SPI.md) — используют constexpr-таблицы выводов (§6) и проверку `periph_base`, чтобы отклонить «вывод чужой периферии».
