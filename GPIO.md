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

`PIN` is one class per one GPIO pin, which hides the difference between families (the clock-enable
register: `RCC->AHB1ENR` on F4/F7 vs `RCC->IOPENR` on G0) and allows the pin to be declared
either as a **compile-time constant** (for the UART/SPI/TIM tables — "this pin belongs to this
peripheral module with such-and-such AF") or as a **runtime object** (for ordinary GPIO — an LED, a
button).

```cpp
PIN led(GPIOA, 5);                      // runtime — as in main.cpp
led.SetUp(PIN::TYPE::OUTPUT_PushPull);
led.SetLevel(true);

// compile-time — as in uart_defs.hpp / tim_defs.hpp:
static constexpr PIN PA9 = { GPIOA_BASE, 9, 7, USART1_BASE };  // AF7, "belongs to" USART1
```

Both variants are one and the same `PIN` type, just different constructors (see section 6).

---

## 2. TYPE Encoding — One Byte Instead of Three Registers

Instead of storing three separate fields (mode/pull/output_type) in `PIN` and duplicating the
"this is an output — must a pull or an output type be specified" logic every time, all valid
combinations are collected into one `enum class TYPE : uint8_t` at compile time:

```
Bit:     7 6 5 4 3 2 1 0
         │     │ │ │ │ └─ mode  [1:0]  — 00 INPUT, 01 OUTPUT, 10 AF, 11 ANALOG
         │     │ │ └─┴─── pull  [3:2]  — 00 NoPull, 01 PullUp, 10 PullDown
         │     └─┴─────── output_type [4] — 0 PushPull, 1 OpenDrain
         └───────────────  (unused)
```

```cpp
OUTPUT_OD_PulUp = (OUTPUT << mode_pos) | (OpenDrain << output_type_pos) | (PullUP << pull_pos)
```

i.e. `TYPE::OUTPUT_OD_PulUp` is one constant, not three calls with three parameters; `SetUp()`
unpacks it back using three masks (`raw >> mode_pos`, `>> pull_pos`, `>> output_type_pos`).
All 9 predefined values (`INPUT_NO_Pull` … `ANALOG`) are listed directly in `TYPE` — invalid
combinations (for example "ANALOG + PullUp") simply do not exist in the type itself.

---

## 3. SetUp() — What Actually Gets Written to the Registers

```cpp
SysInitStatus SetUp(TYPE type, OUTPUT_SPEED speed, uint8_t af_override);
```

The order of operations in `gpio.cpp` — **first unpack `TYPE` into three fields**, then:

1. **Clocking** — `gpio_id` is computed via pointer arithmetic, not a table:
   ```cpp
   gpio_id = (_port_base - GPIOA_BASE) / (GPIOB_BASE - GPIOA_BASE);  // 0=A, 1=B, 2=C, ...
   RCC_GPIO_EN_REG |= (RCC_GPIOA_EN << gpio_id);
   ```
   This works because `GPIOx_BASE` is laid out consecutively with the same stride in all supported
   families — if that weren't the case, an explicit table would be needed (as in
   `usart_table`/`tim_table`).

2. **Clearing before writing** — each of `MODER`/`PUPDR`/`OTYPER`/`OSPEEDR`/`AFR[]` is first
   cleared with a mask for this pin, and only then is the new value written. This matters when
   `SetUp()` is called again on an already-configured pin (changing mode) — without clearing, the old
   mode/pull bits could "mix" with the new ones via `|=`.

3. **ANALOG — a special case**: if `mode == ANALOG`, the steps for
   `OTYPER`/`OSPEEDR`/`PUPDR`/`AFR` are skipped entirely (there's no point configuring pull resistors
   or the edge speed on a pin disconnected from digital logic) — only `MODER` gets `0b11`.

4. **`OTYPER`/`OSPEEDR` are written only for OUTPUT and AF** — INPUT needs neither the
   output type nor the edge speed.

5. **`AFR[]` is written only for AF** — `pin >> 3` selects `AFR[0]` (pins 0-7) or `AFR[1]`
   (pins 8-15), `(pin & 0x7) * 4` is the shift of the 4-bit field within the register.

6. **`MODER` is written last** — this is the only step that actually switches the pin into
   the new mode; all other registers are already ready by this point, so there is no moment where the
   pin ends up, for example, in OUTPUT mode with `OTYPER` not yet configured.

---

## 4. Levels: GetLevel/SetLevel/TogglePin

```cpp
inline bool GetLevel() const { return (PORT()->IDR >> pin) & 0x1u; }

inline void SetLevel(bool lvl) {
    PORT()->BSRR = 1u << (pin + 16 * static_cast<uint8_t>(!lvl));
}

inline void TogglePin() { PORT()->ODR ^= (0x1u << pin); }
```

`SetLevel()` uses `BSRR` (Bit Set/Reset Register) rather than a read-modify-write through
`ODR`: writing `1` into bits `[15:0]` **sets** the corresponding pin, writing `1` into bits `[31:16]`
**resets** it. One register, one atomic write — `pin + 16*(!lvl)` selects the needed half without
branching the code into two separate cases. This eliminates a data race: if `SetLevel()` did
`ODR |= (1<<pin)` / `ODR &= ~(1<<pin)`, an interrupt between reading and writing `ODR` could roll
back someone else's change to the same register (another pin of the same port, changed inside an
ISR) — `BSRR` doesn't read `ODR` at all, so there is no such race.

`TogglePin()` is the **only** operation that goes through `ODR ^=` (not atomically) —
toggling fundamentally requires the "current value", and `BSRR` is not suited for that.

---

## 5. Bit-Banding (F4 Only)

```cpp
#if defined(STM32F4)
inline bool GetLevel_BB() const { return BIT_BB(&PORT()->IDR, pin); }
inline void SetLevel_BB(bool lvl) { BIT_BB(&PORT()->ODR, pin) = lvl; }
inline void TogglePin_BB() { BIT_BB(&PORT()->ODR, pin) = !BIT_BB(&PORT()->ODR, pin); }
#endif
```

Bit-banding is a hardware feature of Cortex-M3/M4 (available on F4, physically absent on
F7/G0 in this project — the peripheral alias-memory region is not mapped on these cores in this
CMSIS configuration, so `BIT_BB()` is only available under `#if defined(STM32F4)`): each bit of a
peripheral region is mapped to a separate 32-bit word in the alias region. Writing `1`/`0` into that
word atomically sets/clears exactly one bit of the original register, with no read-modify-write at
all — meaning `SetLevel_BB()` physically cannot race with a change to a neighboring bit of the same
`ODR`, even without using `BSRR`.

The practical difference from the ordinary `SetLevel()`/`BSRR`: the `BSRR` path is already
atomic for **setting**, but `TogglePin_BB()` provides an atomic **toggle** — that is, exactly where
`TogglePin()` (via `ODR ^=`) is theoretically vulnerable to a race, `TogglePin_BB()` eliminates it at
the cost of being available only on F4.

---

## 6. Constexpr Pin Tables and Protection Against Typos

```cpp
constexpr PIN(uintptr_t port_base, uint8_t p, uint8_t a = 0, uint32_t periph = 0) noexcept;
```

This constructor is used in all compile-time tables (`USART::_1::TX::PA9`,
`TIM::_3::CH1::PA6`, ...). The fourth parameter (`periph`) is not for configuring the pin itself, but
a "stamp" in the spirit of "this pin is physically routed for precisely this peripheral module". The
drivers (`USART`, `TIM`, `SPI`) compare it in their constructor:

```cpp
if (tx.IsValid() && tx.periph_base && tx.periph_base != (uint32_t)usartx)
    System::DebugTrap("USART: TX pin belongs to wrong peripheral");
```

In other words: if a pin from the `USART::_2::TX` table is mistakenly passed to the
`USART` constructor, while the object itself is created on `USART1` — this is caught by `DebugTrap()`
(a breakpoint + infinite loop) immediately when the object is constructed, rather than as a silently
non-working UART at runtime. The second, runtime constructor (`PIN(GPIO_TypeDef*, uint8_t, uint8_t)`)
has no such stamp (`periph_base` = 0 by default) — for ordinary user GPIO this check is unnecessary
and doesn't get in the way.

---

## 7. PinArray<N> — a Parallel Bus

```cpp
template<size_t N> class PinArray { PIN pins[N]; ... };
```

A wrapper over `N` independent `PIN`s, with group operations via C++17 fold expressions
(`std::index_sequence`) — not a runtime loop, but a sequence of calls unrolled at compile time:

```cpp
uint32_t GetLevelAll() const {
    return ((static_cast<uint32_t>(pins[I].GetLevel()) << I) | ...);  // I = 0..N-1
}
```

So `GetLevelAll()`/`SetLevelAll()`/`ToggleAll()` are nothing more than a readable way of
writing `N` separate calls to `PIN::GetLevel()`/`SetLevel()`/`TogglePin()` for pins that may
physically belong to **different ports** (`PinArray` doesn't assume all pins are on the same GPIOx) —
you pay for one register access per pin, but get one call and one 32-bit value instead of manually
packing/unpacking bit by bit in the application code.

`N` is limited to `[1, 32]` (`static_assert`) — the bus value is packed into a `uint32_t`.

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

### Bit-Banding on F4 — Toggling a Pin from an ISR Without a Race

```cpp
PIN strobe(GPIOB, 0);
strobe.SetUp(PIN::TYPE::OUTPUT_PushPull);
strobe.TogglePin_BB();   // atomic, safe even if the main code also touches GPIOB->ODR
```

### PinArray — a 4-Bit Bus for a Segment Display

```cpp
static constexpr PIN seg_pins_raw[] = {
    PIN(GPIOB, 0), PIN(GPIOB, 1), PIN(GPIOB, 4), PIN(GPIOB, 5)
};
PinArray<4> seg_pins(seg_pins_raw);

seg_pins.SetUpAll(PIN::TYPE::OUTPUT_PushPull);
seg_pins.SetLevelAll(0b1010);              // set all 4 bits at once
uint32_t state = seg_pins.GetLevelAll();   // read all 4 bits at once
seg_pins[2].SetLevel(true);                // access a single pin within the array
```

### A Compile-Time Table for a Peripheral (How uart_defs.hpp/tim_defs.hpp Are Organized)

```cpp
struct MySpiPins {
    struct SCK { static constexpr PIN PA5 = { GPIOA_BASE, 5, 5, SPI1_BASE }; };
};
// usage: SPI spi(SPI1, MySpiPins::SCK::PA5, ...);
// the SPI constructor will compare .periph_base with (uint32_t)SPI1 and stop in DebugTrap on a mismatch.
```

---

## 9. Edge Cases

- **`PIN(GPIO_TypeDef*, uint8_t, uint8_t)` (the runtime constructor) checks `p < 16`** —
  a breakpoint trap (`__BKPT(0); while(1)`) on an invalid pin number; the compile-time constructor
  does not perform this check (the values there are already constants known at build time).
- **`Reset()` (a private method) puts the pin into `INPUT_NO_Pull`** — it is not called
  automatically anywhere in the current code of the `PIN` class; it is meant to be called explicitly
  if the calling code needs it (for example, when deinitializing a peripheral).
- **Bit-banding is not available on F7/G0 in this project** — `GetLevel_BB`/`SetLevel_BB`/
  `TogglePin_BB` simply do not exist outside `#if defined(STM32F4)`; trying to call them on F7/G0 is a
  compile-time error, not a runtime failure.
- **`PinArray` does not check that all pins are unique or on the same port** — this is
  deliberate (otherwise it would be impossible to assemble a bus from pins on different ports), but a
  duplicate pin in the list will cause `SetLevelAll()`/`GetLevelAll()` to write/read the same physical
  pin for several bit positions of the result.
- **`operator bool()`/`operator=(bool)`** — `PIN` implicitly converts to `bool` (=
  `GetLevel()`) and accepts the assignment `pin = true;` (= `SetLevel()`), which is convenient in
  conditions (`if (button)`), but care must be taken not to confuse it with a pointer/address
  comparison when refactoring code that uses `PIN`.

---

## See Also

- [System.md](System.md) — `BIT_BB`/`BB_RD`/`BB_WR`, on which the `*_BB()` methods (§5) are
  built, are defined there, alongside the rest of the system-wide infrastructure.
- [UART.md](UART.md), [TIM.md](TIM.md), [SPI.md](SPI.md) — use the constexpr pin tables
  (§6) and the `periph_base` check to guard against "a pin from the wrong peripheral module".

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

`PIN` — один класс на один вывод GPIO, который скрывает разницу между семействами (регистр
включения тактирования: `RCC->AHB1ENR` на F4/F7 vs `RCC->IOPENR` на G0) и позволяет объявлять
пин как **compile-time константу** (для таблиц UART/SPI/TIM — "этот пин относится к этому
периферийному модулю с таким-то AF") и как **runtime-объект** (для обычных GPIO — LED, кнопка).

```cpp
PIN led(GPIOA, 5);                      // runtime — как в main.cpp
led.SetUp(PIN::TYPE::OUTPUT_PushPull);
led.SetLevel(true);

// compile-time — как в uart_defs.hpp / tim_defs.hpp:
static constexpr PIN PA9 = { GPIOA_BASE, 9, 7, USART1_BASE };  // AF7, "принадлежит" USART1
```

Оба варианта — один и тот же тип `PIN`, просто разные конструкторы (см. раздел 6).

---

## 2. Кодирование TYPE — один байт вместо трёх регистров

Вместо того чтобы хранить в `PIN` три отдельных поля (mode/pull/output_type) и лишний раз
дублировать логику "это output — обязательно ли задавать pull или otype", все допустимые
комбинации собраны в один `enum class TYPE : uint8_t` на этапе компиляции:

```
Бит:     7 6 5 4 3 2 1 0
         │     │ │ │ │ └─ mode  [1:0]  — 00 INPUT, 01 OUTPUT, 10 AF, 11 ANALOG
         │     │ │ └─┴─── pull  [3:2]  — 00 NoPull, 01 PullUp, 10 PullDown
         │     └─┴─────── output_type [4] — 0 PushPull, 1 OpenDrain
         └───────────────  (не используется)
```

```cpp
OUTPUT_OD_PulUp = (OUTPUT << mode_pos) | (OpenDrain << output_type_pos) | (PullUP << pull_pos)
```

т.е. `TYPE::OUTPUT_OD_PulUp` — это одна константа, а не три вызова с тремя параметрами; `SetUp()`
распаковывает её обратно тремя масками (`raw >> mode_pos`, `>> pull_pos`, `>> output_type_pos`).
Все 9 предопределённых значений (`INPUT_NO_Pull` … `ANALOG`) перечислены прямо в `TYPE` —
недопустимых комбинаций (например "ANALOG + PullUp") в самом типе не существует.

---

## 3. SetUp() — что реально пишется в регистры

```cpp
SysInitStatus SetUp(TYPE type, OUTPUT_SPEED speed, uint8_t af_override);
```

Порядок операций в `gpio.cpp` — **сначала распаковать `TYPE` в три поля**, потом:

1. **Тактирование** — `gpio_id` считается арифметикой указателей, а не таблицей:
   ```cpp
   gpio_id = (_port_base - GPIOA_BASE) / (GPIOB_BASE - GPIOA_BASE);  // 0=A, 1=B, 2=C, ...
   RCC_GPIO_EN_REG |= (RCC_GPIOA_EN << gpio_id);
   ```
   Работает, потому что `GPIOx_BASE` во всех поддерживаемых семействах идут подряд с одинаковым
   шагом — если бы это было не так, потребовалась бы явная таблица (как в `usart_table`/`tim_table`).

2. **Очистка перед записью** — каждое из `MODER`/`PUPDR`/`OTYPER`/`OSPEEDR`/`AFR[]` сначала
   обнуляется маской для этого пина, только потом записывается новое значение. Это важно при
   повторном вызове `SetUp()` на уже настроенном пине (смена режима) — без очистки старые биты
   мода/pull могли бы "смешаться" с новыми через `|=`.

3. **ANALOG — особый случай**: если `mode == ANALOG`, шаги для `OTYPER`/`OSPEEDR`/`PUPDR`/`AFR`
   пропускаются целиком (незачем настраивать pull-резисторы или скорость на выводе, отключённом от
   цифровой логики) — только `MODER` получает `0b11`.

4. **`OTYPER`/`OSPEEDR` пишутся только для OUTPUT и AF** — INPUT не нуждается ни в типе выхода, ни
   в скорости фронта.

5. **`AFR[]` пишется только для AF** — `pin >> 3` выбирает `AFR[0]` (пины 0-7) или `AFR[1]`
   (пины 8-15), `(pin & 0x7) * 4` — сдвиг 4-битного поля внутри регистра.

6. **`MODER` пишется последним** — это единственный шаг, который реально переключает вывод в
   новый режим; все остальные регистры к этому моменту уже готовы, так что нет момента, когда
   вывод оказывается, например, в режиме OUTPUT с ещё не настроенным `OTYPER`.

---

## 4. Уровни: GetLevel/SetLevel/TogglePin

```cpp
inline bool GetLevel() const { return (PORT()->IDR >> pin) & 0x1u; }

inline void SetLevel(bool lvl) {
    PORT()->BSRR = 1u << (pin + 16 * static_cast<uint8_t>(!lvl));
}

inline void TogglePin() { PORT()->ODR ^= (0x1u << pin); }
```

`SetLevel()` использует `BSRR` (Bit Set/Reset Register), а не read-modify-write через `ODR`:
запись `1` в биты `[15:0]` **устанавливает** соответствующий вывод, запись `1` в биты `[31:16]`
**сбрасывает** его. Один регистр, одна атомарная запись — `pin + 16*(!lvl)` выбирает нужную
половину без ветвления кода на два отдельных случая. Это устраняет гонку данных: если бы
`SetLevel()` делал `ODR |= (1<<pin)` / `ODR &= ~(1<<pin)`, прерывание между чтением и записью
`ODR` могло бы откатить чужое изменение того же регистра (другой вывод того же порта, изменённый
внутри ISR) — `BSRR` не читает `ODR` вообще, поэтому такой гонки нет.

`TogglePin()` **единственная** операция, которая идёт через `ODR ^=` (не атомарно) — переключение
принципиально требует "текущее значение", `BSRR` для этого не подходит.

---

## 5. Bit-banding (только F4)

```cpp
#if defined(STM32F4)
inline bool GetLevel_BB() const { return BIT_BB(&PORT()->IDR, pin); }
inline void SetLevel_BB(bool lvl) { BIT_BB(&PORT()->ODR, pin) = lvl; }
inline void TogglePin_BB() { BIT_BB(&PORT()->ODR, pin) = !BIT_BB(&PORT()->ODR, pin); }
#endif
```

Bit-banding — аппаратная возможность Cortex-M3/M4 (есть на F4, физически отсутствует на F7/G0
в этом проекте — область alias-памяти для периферии не отображена на этих ядрах в данной
конфигурации CMSIS, поэтому `BIT_BB()` доступен только под `#if defined(STM32F4)`): каждый бит
периферийного региона отображается на отдельное 32-битное слово в alias-регионе. Запись `1`/`0`
в это слово атомарно устанавливает/сбрасывает ровно один бит исходного регистра, без
read-modify-write вообще — то есть `SetLevel_BB()` физически не может гоняться с изменением
соседнего бита того же `ODR` даже без использования `BSRR`.

Практическая разница с обычным `SetLevel()`/`BSRR`: `BSRR`-путь уже атомарен для **установки**,
но `TogglePin_BB()` даёт атомарное **переключение** — то есть именно там, где `TogglePin()` (через
`ODR ^=`) теоретически уязвим к гонке, `TogglePin_BB()` устраняет её ценой доступности только на F4.

---

## 6. Constexpr-таблицы пинов и защита от опечаток

```cpp
constexpr PIN(uintptr_t port_base, uint8_t p, uint8_t a = 0, uint32_t periph = 0) noexcept;
```

Этот конструктор используется во всех compile-time таблицах (`USART::_1::TX::PA9`,
`TIM::_3::CH1::PA6`, ...). Четвёртый параметр (`periph`) — не для настройки самого пина, а
"клеймо" в духе "этот пин физически разведён под именно этот периферийный модуль". Драйверы
(`USART`, `TIM`, `SPI`) сравнивают его в конструкторе:

```cpp
if (tx.IsValid() && tx.periph_base && tx.periph_base != (uint32_t)usartx)
    System::DebugTrap("USART: TX pin belongs to wrong peripheral");
```

Иначе говоря: если по ошибке передать в конструктор `USART` пин из таблицы `USART::_2::TX`, а сам
объект создать на `USART1` — это поймается `DebugTrap()` (breakpoint + бесконечный цикл) сразу
при конструировании объекта, а не тихим неработающим UART в рантайме. Второй, runtime-конструктор
(`PIN(GPIO_TypeDef*, uint8_t, uint8_t)`) такого клейма не имеет (`periph_base` = 0 по умолчанию) —
для обычных пользовательских GPIO эта проверка не нужна и не мешает.

---

## 7. PinArray<N> — параллельная шина

```cpp
template<size_t N> class PinArray { PIN pins[N]; ... };
```

Обёртка над `N` независимых `PIN`, с групповыми операциями через C++17 fold-expressions
(`std::index_sequence`) — не цикл в рантайме, а развёрнутая на этапе компиляции
последовательность вызовов:

```cpp
uint32_t GetLevelAll() const {
    return ((static_cast<uint32_t>(pins[I].GetLevel()) << I) | ...);  // I = 0..N-1
}
```

Так что `GetLevelAll()`/`SetLevelAll()`/`ToggleAll()` — это не более чем читаемая запись `N`
отдельных вызовов `PIN::GetLevel()`/`SetLevel()`/`TogglePin()` для пинов, которые физически могут
принадлежать **разным портам** (`PinArray` не предполагает, что все пины на одном GPIOx) — платите
по одному обращению к регистру на пин, но получаете один вызов и одно 32-битное значение вместо
ручной упаковки/распаковки бит за битом в коде приложения.

`N` ограничен `[1, 32]` (`static_assert`) — значение шины пакуется в `uint32_t`.

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

### Bit-banding на F4 — переключение вывода из ISR без гонки

```cpp
PIN strobe(GPIOB, 0);
strobe.SetUp(PIN::TYPE::OUTPUT_PushPull);
strobe.TogglePin_BB();   // атомарно, безопасно даже если основной код тоже трогает GPIOB->ODR
```

### PinArray — 4-битная шина сегментного индикатора

```cpp
static constexpr PIN seg_pins_raw[] = {
    PIN(GPIOB, 0), PIN(GPIOB, 1), PIN(GPIOB, 4), PIN(GPIOB, 5)
};
PinArray<4> seg_pins(seg_pins_raw);

seg_pins.SetUpAll(PIN::TYPE::OUTPUT_PushPull);
seg_pins.SetLevelAll(0b1010);              // выставить все 4 бита разом
uint32_t state = seg_pins.GetLevelAll();   // прочитать все 4 бита разом
seg_pins[2].SetLevel(true);                // доступ к одному пину внутри массива
```

### Compile-time таблица для периферии (как устроены uart_defs.hpp/tim_defs.hpp)

```cpp
struct MySpiPins {
    struct SCK { static constexpr PIN PA5 = { GPIOA_BASE, 5, 5, SPI1_BASE }; };
};
// использование: SPI spi(SPI1, MySpiPins::SCK::PA5, ...);
// конструктор SPI сверит .periph_base с (uint32_t)SPI1 и остановится в DebugTrap при несовпадении.
```

---

## 9. Угловые случаи

- **`PIN(GPIO_TypeDef*, uint8_t, uint8_t)` (runtime-конструктор) проверяет `p < 16`** —
  breakpoint-ловушка (`__BKPT(0); while(1)`) при недопустимом номере пина; compile-time
  конструктор такой проверки не делает (значения там и так константы, известные на этапе сборки).
- **`Reset()` (приватный метод) переводит пин в `INPUT_NO_Pull`** — не вызывается автоматически
  нигде в текущем коде класса `PIN`; предназначен для явного вызова, если он понадобится
  вызывающему коду (например, при деинициализации периферии).
- **Bit-banding недоступен на F7/G0 в этом проекте** — `GetLevel_BB`/`SetLevel_BB`/`TogglePin_BB`
  просто не существуют вне `#if defined(STM32F4)`; попытка вызвать их на F7/G0 — ошибка
  компиляции, а не рантайм-сбой.
- **`PinArray` не проверяет, что все пины уникальны или на одном порту** — это осознанно (иначе
  нельзя было бы собрать шину из пинов разных портов), но повторяющийся пин в списке приведёт к
  тому, что `SetLevelAll()`/`GetLevelAll()` будут писать/читать один и тот же физический вывод
  для нескольких битовых позиций результата.
- **`operator bool()`/`operator=(bool)`** — `PIN` неявно конвертируется в `bool` (= `GetLevel()`)
  и принимает присваивание `pin = true;` (= `SetLevel()`), что удобно в условиях (`if (button)`),
  но нужно не перепутать с сравнением указателей/адресов при рефакторинге кода, использующего `PIN`.

---

## См. также

- [System.md](System.md) — `BIT_BB`/`BB_RD`/`BB_WR`, на которых построены `*_BB()`-методы (§5),
  определены там же, где остальная общесистемная инфраструктура.
- [UART.md](UART.md), [TIM.md](TIM.md), [SPI.md](SPI.md) — используют constexpr-таблицы пинов
  (§6) и `periph_base`-проверку для защиты от "пин не того периферийного модуля".
