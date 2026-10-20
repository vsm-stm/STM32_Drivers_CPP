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
PIN led(GPIOA, 5);                 // ordinary GPIO object
led.SetUp(PIN::TYPE::OUTPUT_PushPull);
led.SetLevel(true);

// compile-time — as in uart_defs.hpp / tim_defs.hpp:
static constexpr PIN PA9 = { GPIOA_BASE, 9, 7, USART1_BASE };  // AF7, "belongs to" USART1
```

The pointer constructor supports ordinary GPIO objects such as PIN(GPIOA, 5).
Compile-time tables use the consteval constructor with GPIOx_BASE; it validates
their port, pin number and stored AF at build time. C++20 or later is required.

PORT() returns the fixed port pointer; GetNumber() returns the fixed pin number,
for example for external EXTI routing. Neither accessor permits rebinding.

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
SysInitStatus SetUp(TYPE type, OUTPUT_SPEED speed, uint8_t af_override,
                    LVL initial_level = LVL::LOW);
```

The order of operations in `gpio.cpp` — **first unpack `TYPE` into three fields**, then:

1. **Clocking** — `gpio_id` is computed via pointer arithmetic, not a table:
   ```cpp
   gpio_id = (_port_base - GPIOA_BASE) / (GPIOB_BASE - GPIOA_BASE);  // 0=A, 1=B, 2=C, ...
   RCC_GPIO_EN_REG |= (RCC_GPIOA_EN << gpio_id);
   ```
   GPIO ports occupy 0x400-byte slots on F4/F7/G0; absent ports can leave gaps.
   The stride is taken from the CMSIS base addresses. GCC 13.3.1 generates the
   same instructions as an explicit shift for F429/F767/G031 with -O0/-Og/-Os;
   no software division helper is called. No additional port index is stored.

2. **One read/modify/write per register** — the new field replaces the old one
   in a single write. There is no separate clear followed by OR.

3. **ANALOG** clears the pin fields in OTYPER/OSPEEDR/PUPDR/AFR and selects
   MODER=0b11. No pull is enabled for the analog pin.

4. **OUTPUT and AF** use the requested output type and speed. For INPUT/ANALOG
   these fields are cleared, matching the previous configuration contract.

5. **AFR** uses the requested AF in AF mode, otherwise zero. pin >> 3 selects
   AFR[0]/AFR[1]; (pin & 7) * 4 selects the field inside that register.

6. **Initial OUTPUT level** is written through BSRR before configuration.
   It defaults to LOW, including repeated OUTPUT setup. Other modes do not
   change the output latch. MODER is written last, once: no temporary INPUT
   transition releases an already driven line. Register writes are sequential,
   not one atomic transaction; the caller still serializes configuration.

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

`SetLevel()` uses `BSRR` (Bit Set/Reset Register) rather than a read-modify-write through
`ODR`: writing `1` into bits `[15:0]` **sets** the corresponding pin, writing `1` into bits `[31:16]`
**resets** it. One register, one atomic write — `pin + 16*(!lvl)` selects the needed half without
branching the code into two separate cases. This eliminates a data race: if `SetLevel()` did
`ODR |= (1<<pin)` / `ODR &= ~(1<<pin)`, an interrupt between reading and writing `ODR` could roll
back someone else's change to the same register (another pin of the same port, changed inside an
ISR) — `BSRR` doesn't read `ODR` at all, so there is no such race.

`TogglePin()` retains `ODR ^=`: it reads and rewrites the whole output register.
Do not modify any output on the same port from an IRQ between that read and write.
A toggle implemented as ODR read plus BSRR write could preserve other bits, but
would still not make concurrent toggles of the same pin atomic. No cycle counts
have been measured for these alternatives.

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

`TogglePin_BB()` consists of a separate bit read and bit write. Each access
affects only the selected bit, but the combined toggle is not atomic: another
operation on the same pin can occur between them. A whole-port ODR write can
also overwrite a bit changed through bit-banding.

---

## 6. Constexpr Pin Tables and Protection Against Typos

```cpp
consteval PIN(uintptr_t port_base, uint32_t p, uint32_t a = 0,
              uint32_t periph = 0) noexcept;
```

The consteval constructor accepts GPIOx_BASE and rejects an unavailable GPIO port, a pin
number outside 0..15 or an AF outside 0..15 at compile time. The numeric arguments
are checked before a value can be accepted in the stored 8-bit fields, so values
such as 256 cannot silently wrap into valid definitions. No runtime checks,
BKPT or waiting loops are added.

Port, pin number and stored AF are private const fields. The public const
`periph_base` is read-only metadata for the existing USART/SPI/I2C binding checks;
it is not the GPIO port address. Copy/move construction is allowed, but assigning
another PIN is deleted: an existing object's physical binding cannot be changed.
Assignment of bool still sets the output level.

`SetUp(type, speed)` uses the stored AF. An explicit `af_override` selects an AF
at runtime without changing the stored field; the caller supplies a value in
0..15. These range checks do not verify every MCU-specific pin/function combination.

USART/SPI/I2C pin tables keep their existing `{GPIOx_BASE, pin, af, peripheral}`
format. Their checks that a pin belongs to the selected peripheral remain in
those drivers; no changes to those modules are included here.

The ordinary constructor is retained:

```cpp
PIN(GPIO_TypeDef* port, uint8_t p, uint8_t a = 0) noexcept;
PIN led(GPIOA, 5);
```

It binds the supplied GPIO pointer, pin and AF without runtime checks or traps;
the caller supplies a supported GPIO port and pin/AF in 0..15. This overload is
not consteval. ADC/TIM can keep their existing pointer-based construction.
Their migration to USART-style pin definitions/tables remains scheduled for
their own module reviews. DAC can also construct PIN from a GPIO pointer, but
its assignment of a whole PIN still needs migration because binding is immutable.

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
Array elements are copy-constructed directly; they are not rebound by assignment.
`SetUpAll()` configures every element and returns the first non-InitOK status
without converting it to NotInit. OUTPUT setup uses the default initial LOW.

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
strobe.TogglePin_BB();   // separate bit read/write; do not concurrently change this pin
```

### PinArray — a 4-Bit Bus for a Segment Display

```cpp
static constexpr PIN seg_pins_raw[] = {
    PIN(GPIOB_BASE, 0), PIN(GPIOB_BASE, 1), PIN(GPIOB_BASE, 4), PIN(GPIOB_BASE, 5)
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
// usage: SPI spi(SPI1, SPI::Direction::TXRX, MySpiPins::SCK::PA5, ...);
// the SPI constructor will compare .periph_base with (uint32_t)SPI1 and stop in DebugTrap on a mismatch.
```

---

## 9. Edge Cases

- **Two constructors** — GPIOx_BASE uses compile-time validation; GPIO_TypeDef*
  supports ordinary pointer-based construction without runtime validation.
  `PIN{}` represents an absent optional signal; do not call SetUp or level access on it.
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
PIN led(GPIOA, 5);                 // обычный объект GPIO
led.SetUp(PIN::TYPE::OUTPUT_PushPull);
led.SetLevel(true);

// compile-time — как в uart_defs.hpp / tim_defs.hpp:
static constexpr PIN PA9 = { GPIOA_BASE, 9, 7, USART1_BASE };  // AF7, "принадлежит" USART1
```

Конструктор с указателем используется для обычных объектов, например PIN(GPIOA, 5).
Для compile-time таблиц используется consteval-конструктор с GPIOx_BASE: он
проверяет порт, номер и сохранённый AF при сборке. Требуется C++20 или новее.

PORT() возвращает закреплённый порт, GetNumber() — закреплённый номер пина,
например для маршрутизации внешней EXTI. Менять привязку через эти методы нельзя.

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
SysInitStatus SetUp(TYPE type, OUTPUT_SPEED speed, uint8_t af_override,
                    LVL initial_level = LVL::LOW);
```

Порядок операций в `gpio.cpp` — **сначала распаковать `TYPE` в три поля**, потом:

1. **Тактирование** — `gpio_id` считается арифметикой указателей, а не таблицей:
   ```cpp
   gpio_id = (_port_base - GPIOA_BASE) / (GPIOB_BASE - GPIOA_BASE);  // 0=A, 1=B, 2=C, ...
   RCC_GPIO_EN_REG |= (RCC_GPIOA_EN << gpio_id);
   ```
   Порты F4/F7/G0 занимают адресные позиции с шагом 0x400; отсутствующие порты
   могут оставлять пропуски. Шаг берётся из адресов CMSIS. GCC 13.3.1 выдаёт тот же
   машинный код, что и явный сдвиг, для F429/F767/G031 с -O0/-Og/-Os;
   вызова программного деления нет. Дополнительный индекс порта не хранится.

2. **Одна операция чтение/изменение/запись на регистр** — новое поле заменяет
   старое одной записью. Раздельных очистки и последующего OR больше нет.

3. **ANALOG** обнуляет поля пина в OTYPER/OSPEEDR/PUPDR/AFR и выбирает
   MODER=0b11. Подтяжки для аналогового входа не включаются.

4. **OUTPUT и AF** получают заданные тип выхода и скорость. Для INPUT/ANALOG
   эти поля обнуляются, сохраняя прежний контракт настройки.

5. **AFR** получает заданный AF только в режиме AF, иначе ноль. pin >> 3
   выбирает AFR[0]/AFR[1], (pin & 7) * 4 — поле внутри регистра.

6. **Начальный уровень OUTPUT** записывается через BSRR до настройки.
   По умолчанию LOW, в том числе при повторном SetUp. Другие режимы не меняют
   выходную защёлку. MODER записывается последним и один раз: временного перехода
   во INPUT с отпусканием линии нет. Последовательные записи регистров не являются
   одной атомарной операцией; конфигурацию по-прежнему сериализует вызывающий код.

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

`SetLevel()` использует `BSRR` (Bit Set/Reset Register), а не read-modify-write через `ODR`:
запись `1` в биты `[15:0]` **устанавливает** соответствующий вывод, запись `1` в биты `[31:16]`
**сбрасывает** его. Один регистр, одна атомарная запись — `pin + 16*(!lvl)` выбирает нужную
половину без ветвления кода на два отдельных случая. Это устраняет гонку данных: если бы
`SetLevel()` делал `ODR |= (1<<pin)` / `ODR &= ~(1<<pin)`, прерывание между чтением и записью
`ODR` могло бы откатить чужое изменение того же регистра (другой вывод того же порта, изменённый
внутри ISR) — `BSRR` не читает `ODR` вообще, поэтому такой гонки нет.

`TogglePin()` сохраняет `ODR ^=`: читает и перезаписывает весь выходной регистр.
Нельзя менять выводы того же порта из IRQ между этим чтением и записью.
Toggle через чтение ODR и запись BSRR мог бы сохранять остальные биты, но
одновременные переключения одного пина всё равно не стали бы атомарными.
Сравнение этих вариантов по тактам не выполнялось.

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

`TogglePin_BB()` состоит из отдельного чтения и записи бита. Каждое обращение
затрагивает только выбранный бит, но переключение в целом не атомарно: между
чтением и записью другой код может изменить тот же пин. Запись всего ODR также
может затереть изменение, сделанное через bit-banding.

---

## 6. Constexpr-таблицы пинов и защита от опечаток

```cpp
consteval PIN(uintptr_t port_base, uint32_t p, uint32_t a = 0,
              uint32_t periph = 0) noexcept;
```

Consteval-конструктор принимает GPIOx_BASE и при компиляции отклоняет отсутствующий
GPIO-порт, номер вне 0..15 и AF вне 0..15. Аргументы проверяются до принятия
значений в 8-битные поля: например, 256 не может незаметно превратиться в ноль.
Runtime-проверки, BKPT и циклы ожидания не добавляются.

Порт, номер и сохранённый AF — private const. Публичный const `periph_base`
остаётся доступным только для чтения существующими проверками USART/SPI/I2C;
это адрес периферии, а не GPIO-порта. Копирующий/перемещающий конструкторы
сохранены, присваивание другого PIN запрещено: физическую привязку созданного
объекта менять нельзя. Присваивание bool по-прежнему меняет выходной уровень.

`SetUp(type, speed)` использует сохранённый AF. Явный `af_override` выбирает AF
во время выполнения, не меняя сохранённое поле; значение 0..15 обеспечивает
вызывающий код. Проверки диапазона не проверяют каждую конкретную комбинацию
вывода и функции выбранного МК.

Таблицы USART/SPI/I2C сохраняют формат `{GPIOx_BASE, pin, af, peripheral}`.
Проверка принадлежности пина выбранной периферии остаётся в этих драйверах;
их код в текущей работе не меняется.

Обычный конструктор сохранён:

```cpp
PIN(GPIO_TypeDef* port, uint8_t p, uint8_t a = 0) noexcept;
PIN led(GPIOA, 5);
```

Он закрепляет GPIO-указатель, номер и AF без runtime-проверок и остановов;
вызывающий код передаёт поддерживаемый GPIO-порт и номер/AF в диапазоне 0..15.
Эта перегрузка не consteval. Существующие вызовы ADC/TIM через указатель
сохраняются. Их переход на описания/таблицы пинов по образцу USART остаётся
задачей соответствующих модулей. Создание PIN через указатель в DAC также
поддерживается; его присваивание целого PIN ещё требует изменения, поскольку
привязка объекта неизменна.

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
Элементы массива сразу создаются копирующими конструкторами, без перепривязки
присваиванием. `SetUpAll()` настраивает все элементы и возвращает первый статус, отличный от
InitOK, без преобразования в NotInit. OUTPUT получает начальный LOW по умолчанию.

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
strobe.TogglePin_BB();   // отдельные чтение/запись; не менять этот пин параллельно
```

### PinArray — 4-битная шина сегментного индикатора

```cpp
static constexpr PIN seg_pins_raw[] = {
    PIN(GPIOB_BASE, 0), PIN(GPIOB_BASE, 1), PIN(GPIOB_BASE, 4), PIN(GPIOB_BASE, 5)
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
// использование: SPI spi(SPI1, SPI::Direction::TXRX, MySpiPins::SCK::PA5, ...);
// конструктор SPI сверит .periph_base с (uint32_t)SPI1 и остановится в DebugTrap при несовпадении.
```

---

## 9. Угловые случаи

- **Два конструктора** — GPIOx_BASE используется с проверкой при компиляции;
  GPIO_TypeDef* — для обычного создания через указатель без runtime-проверки.
  `PIN{}` обозначает отсутствующий сигнал; его нельзя настраивать или читать/менять уровень.
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
