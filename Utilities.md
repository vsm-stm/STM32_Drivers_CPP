# Software utilities — Examples and API

*[English](#english) · [Русский](#русский)*

---

## English

## Table of Contents

1. [Scope and integration](#1-scope-and-integration)
2. [Containers](#2-containers)
3. [PID](#3-pid)
4. [CRC32](#4-crc32)
5. [CMSIS-DSP subset](#5-cmsis-dsp-subset)
6. [WS2812B encoding](#6-ws2812b-encoding)
7. [Lifetime and limits](#7-lifetime-and-limits)
8. [See Also](#8-see-also)

---

## 1. Scope and integration

The examples are independent, complete C++ functions/classes. They use the public headers of this checkout. Enable the appropriate optional module in project.json; WS2812B is a TIM helper, not a separate module. These utilities perform no automatic sampling, IRQ registration or application integration.

```json
"drivers": ["CONTAINERS", "PID", "CRC", "DSP"]
```

## 2. Containers

| Type | Public operations |
| --- | --- |
| `RingQueue` | `push`, `pop`, `empty`, `full`, `size` |
| `StaticMap` | `insert`, `find`, `erase`, `clear`, `size`, `capacity`, `raw_entries` |
| `StaticSList` | `push_back`, `front`, `next`, `erase_first_if`, `empty`, `full`, `size` |
| `StaticDList` | `push_back`, `move_to_tail`, `erase`, `erase_and_next`, `head`, `tail`, `next`, `prev`, `ref`, `valid`, `empty`, `full`, `size`, `npos` |

### Queue and unique-key map

Enable CONTAINERS. RingQueue<T,N> stores N-1 items; check push/pop instead of assuming capacity. StaticMap is an unordered linear-search map with N usable entries; inserting an existing key replaces its value even when full. find returns a borrowed pointer, invalid after erase/clear. Raw entries can be inspected only when used is true; changing raw used flags does not repair the size counter. All containers are single-context fixed storage, not interrupt/main lock-free primitives.

```cpp
#include "RingQueue.hpp"
#include "StaticMap.hpp"
void Example() {
    RingQueue<int, 4> queue; // Three usable elements.
    bool stored = queue.push(10);
    int item = 0;
    bool received = queue.pop(item);
    (void)stored; (void)received;
    (void)queue.size(); (void)queue.empty(); (void)queue.full();

    StaticMap<int, float, 4> map;
    bool inserted = map.insert(1, 2.5f);
    bool replaced = map.insert(1, 3.5f);
    if (float* value = map.find(1)) *value = 4.0f;
    for (size_t i = 0; i < map.capacity(); ++i) {
        const auto& entry = map.raw_entries()[i];
        if (entry.used) { (void)entry.key; (void)entry.value; }
    }
    bool erased = map.erase(1);
    map.clear();
    (void)map.size(); (void)inserted; (void)replaced; (void)erased;
}
```

### Lists, stable indices and deletion during traversal

StaticSList requires a standard-layout element type. front/next yield live value pointers; do not change list structure during that traversal. erase_first_if removes only the first match. StaticDList indices remain stable when moving a live node, but erase invalidates its index and references even if the slot is later reused. Use erase_and_next when removing during traversal. No value destructor is called by removal, and there is no automatic resource release for stored pointers/handles. Both lists expose empty/full/size.

```cpp
#include "StaticSList.hpp"
#include "StaticDList.hpp"
void Example() {
    StaticSList<int, 4> singly;
    if (!singly.push_back(10) || !singly.push_back(20)) return;
    for (int* value = singly.front(); value; value = singly.next(value))
        *value += 1;
    bool erased = singly.erase_first_if([](int value) { return value == 11; });
    (void)erased; (void)singly.empty(); (void)singly.full(); (void)singly.size();

    StaticDList<int, 4> doubly;
    auto saved = decltype(doubly)::npos;
    if (!doubly.push_back(10, &saved) || !doubly.push_back(20)) return;
    doubly.move_to_tail(saved);
    for (auto i = doubly.head(); i != decltype(doubly)::npos; ) {
        if (doubly.ref(i) == 10) i = doubly.erase_and_next(i);
        else i = doubly.next(i);
    }
    auto tail = doubly.tail();
    if (doubly.valid(tail)) {
        (void)doubly.prev(tail);
        bool removed = doubly.erase(tail);
        (void)removed;
    }
    (void)doubly.empty(); (void)doubly.full(); (void)doubly.size();
}
```

---

## 3. PID

### Fixed-step PID, reset and term diagnostics

Enable `PID` and include `PID.hpp`. All six tunable values are in `PID::Parameters`; the controller keeps one private copy. Input pointers are fixed separately by the constructor, and calculation state remains private. There is no sample history, quality evaluation, heap allocation or internal locking.

**Constructors:**

```cpp
PID(const float* ptr_to_target, const float* ptr_to_data);
PID(float kp, float ki, float kd, float ki_saturation, float e_deadband,
    float dt, const float* ptr_to_target, const float* ptr_to_data);
```

The two-pointer overload uses the defaults below. The existing eight-argument constructor still initializes explicit settings directly. Both are `constexpr` and support `constinit` with static input variables. Input variables must outlive the controller.

| `PID::Parameters` field | Meaning | Default |
| --- | --- | --- |
| `kp` | Proportional gain | `1.0f` |
| `ki` | Integral gain | `0.0f` |
| `kd` | Derivative gain | `0.0f` |
| `ki_saturation` | Maximum absolute contribution `ki*I`, not total output | `0.0f` |
| `e_deadband` | Error magnitude at/below which I is held and D=0; P remains active | `0.0f` |
| `dt` | Configured sample interval in seconds | `1.0f` |

**One parameter getter/setter pair:**

```cpp
PID::Parameters get_parameters() const;
void set_parameters(const PID::Parameters& parameters);
```

The getter returns a copy suitable for application storage. Editing that copy has no effect until `set_parameters()` is called. The setter replaces all six settings first, then clamps the accumulated integral **once using the final new `ki` and `ki_saturation`**. It neither resets previous error/derivative nor runs a control step. With zero `ki`, raw I is held; a later nonzero `ki` applies the limit. No individual or grouped coefficient setters/getters remain. The caller serializes input/parameter changes, calculation, reset and diagnostic reads.

`u()` reads each input once, computes `target - data` and advances one sample. Call it once per configured `dt` seconds; the controller does not measure time or wait. If `dt` changes, the application also updates its actual calling cadence. Repeated calls advance the calculation even if inputs have not changed.

Inputs/settings must be finite, `dt > 0`, `ki_saturation`/`e_deadband >= 0`; there are no constructor/setter validity traps. Zero `dt` returns zero without reading inputs or changing state. Integration remains trapezoidal. The deadband holds I, sets D=0 and refreshes previous error; P remains active. No output clamp or derivative filter is added.

Only accumulated integral, previous error and current derivative are stored. Inline `p_term()`, `i_term()`, `d_term()` use that state and the current gains; updating parameters affects these getters immediately, without a new input read or control step. `Reset()` zeros calculation state/term getters while preserving parameters and inputs. Reset at nonzero error can cause a first-step derivative transient; parameter updates do not guarantee a continuous output.

The values below illustrate API usage, not tuning:

```cpp
#include "PID.hpp"

float target = 100.0f;
float measured = 0.0f;
constinit PID controller(&target, &measured); // kp=dt=1, all other settings=0.

void Configure() {
    PID::Parameters settings{
        .kp = 1.0f,
        .ki = 0.1f,
        .kd = 0.0f,
        .ki_saturation = 2.0f,
        .e_deadband = 0.01f,
        .dt = 0.001f,
    };
    controller.set_parameters(settings); // Also accepts settings read from storage.
}

// Call once per configured dt, after updating measured.
float Step() {
    return controller.u();
}

PID::Parameters SettingsForStorage() {
    return controller.get_parameters(); // Copy; contains no input pointers/state.
}
void Reset() {
    controller.Reset(); // Keeps parameters and pointed-to signals.
}
```

For a small edit, read the copy, change its fields and apply it once:

```cpp
void ChangeSampleRate(float sample_rate_hz) {
    auto settings = controller.get_parameters();
    settings.dt = 1.0f / sample_rate_hz; // Caller supplies a positive frequency.
    controller.set_parameters(settings);
    // The application also changes its timer/scheduler to this frequency.
}
```

**Migration:** use `PID.hpp`, `controller.u()` and `Reset()` instead of the old C API. The former per-parameter/grouped get/set methods are replaced by `get_parameters()` / `set_parameters()`. `max_ki_I` is now `Parameters::ki_saturation`; the deadband is `Parameters::e_deadband`. CMake compiles `math/PID.cpp`.

---

## 4. CRC32

### CRC32 of one buffer and a stream of chunks

Enable CRC, or build crc/crc32.c directly for portable use. This is software IEEE CRC32 (reflected polynomial 0xEDB88320), not the STM32 hardware CRC peripheral or Modbus CRC16. Start a stream with CRC32_INIT, update every chunk in order and finish once at the end. Feed the unfinished state into the next update, not the finalized checksum. Splitting input does not change the final CRC. The check value for ASCII 123456789 is 0xCBF43926; string terminators are excluded below.

```cpp
#include "crc32.h"
bool CheckCRC() {
    static const char bytes[] = "123456789";
    uint32_t one = crc32_compute(bytes, sizeof bytes - 1);
    uint32_t state = CRC32_INIT;
    state = crc32_update(state, bytes, 4);
    state = crc32_update(state, bytes + 4, 5);
    uint32_t streamed = crc32_finish(state);
    return one == 0xCBF43926u && streamed == one;
}
```

---

## 5. CMSIS-DSP subset

### Built CMSIS-DSP groups: math and controllers

Enable DSP: CMake fetches CMSIS-DSP v1.16.2 and builds FastMath, Controller and common tables only. Include the group headers instead of assuming all arm_math.h declarations are linked. sin/cos take radians; sin_cos_f32 takes degrees. Check arm_status for sqrt, atan2 and fixed-point divide; logs need positive inputs. CMSIS PID state is distinct from the library's fixed-step PID class. Initialize every CMSIS PID before use; arm_pid_reset clears history. q15/q31 variants use fixed-point scaling defined by their headers, not plain integer sensor units. Float16, FFT, FIR/IIR and statistics are not included by this module.

```cpp
#include "dsp/fast_math_functions.h"
#include "dsp/controller_functions.h"
bool MathExample() {
    float sine = arm_sin_f32(0.5f), cosine = arm_cos_f32(0.5f);
    float root = 0.0f, angle = 0.0f;
    if (arm_sqrt_f32(4.0f, &root) != ARM_MATH_SUCCESS ||
        arm_atan2_f32(1.0f, 1.0f, &angle) != ARM_MATH_SUCCESS) return false;
    float values[2] = {1.0f, 2.0f}, logs[2], exponentials[2];
    arm_vlog_f32(values, logs, 2);
    arm_vexp_f32(logs, exponentials, 2);
    arm_sin_cos_f32(30.0f, &sine, &cosine); // Degrees.
    float alpha, beta, direct, quadrature, phase_a, phase_b;
    arm_clarke_f32(1.0f, -0.5f, &alpha, &beta);
    arm_park_f32(alpha, beta, &direct, &quadrature, sine, cosine);
    arm_inv_park_f32(direct, quadrature, &alpha, &beta, sine, cosine);
    arm_inv_clarke_f32(alpha, beta, &phase_a, &phase_b);
    q31_t quotient;
    int16_t shift;
    if (arm_divide_q31(0x20000000, 0x40000000, &quotient, &shift)
            != ARM_MATH_SUCCESS) return false;
    return root > 0.0f;
}
float ControllerExample() {
    arm_pid_instance_f32 controller{};
    controller.Kp = 1.0f;
    controller.Ki = 0.1f;
    controller.Kd = 0.0f;
    arm_pid_init_f32(&controller, 1);
    float result = arm_pid_f32(&controller, 0.25f);
    arm_pid_reset_f32(&controller);
    return result;
}
```

Other included types: sin/cos and sqrt also expose q15/q31; atan2 exposes f32/q15/q31; vector log exposes f32/f64/q15/q31 and vector exp f32/f64; sin_cos and coordinate transforms expose f32/q31; PID exposes f32/q15/q31. Shared tables are linked once. See the pinned [FastMath header](https://github.com/ARM-software/CMSIS-DSP/blob/v1.16.2/Include/dsp/fast_math_functions.h) and [Controller header](https://github.com/ARM-software/CMSIS-DSP/blob/v1.16.2/Include/dsp/controller_functions.h) for domains/scaling.

---

## 6. WS2812B encoding

### WS2812B pixels, ranges, brightness and DMA data

ws2812b.hpp is a header helper used with TIM; there is no separate WS2812B CMake module. It encodes pixels into uint16_t CCR values, not GPIO writes. At 800 kHz, choose actual timer-period-dependent CCR values; 40 reset slots represent 50 us before accounting for transfer tails. SetCCR changes future encoding constants; re-encode pixels afterwards. Do not mutate the strip while TIM DMA reads its buffer. DMA TC releases the source but does not mean the last PWM period or LED reset has finished. F7 DMA placement/cache-line ownership is the same contract as TIM.md. N_LEDS must be positive and Len must fit the DMA count.

```cpp
#include "ws2812b.hpp"
WS2812B_Strip<4> strip(67, 33); // Example for a 100-tick PWM period.
void Encode() {
    strip.Clear();
    strip.SetAll(RGB::Red().dim(128));
    strip.SetPixel(0, 0, 255, 0);
    strip.SetPixel(1, RGB::Blue());
    strip.Fill(2, strip.Size(), FromHSV(85, 255, 64));
    strip.SetCCR(66, 33);
    strip.Rainbow(0, 128); // Re-encode with the new CCR values.
}
const uint16_t* Data() { return strip.Data(); }
size_t Count() { return strip.Len(); }
```

RGB also provides equality/inequality and named presets Black/White/Red/Green/Blue/Yellow/Cyan/Magenta/Orange/Purple. SetAll has RGB and r/g/b overloads. TIM setup and actual SendDMA/Start/completion are shown in [TIM.md](TIM.md#9-ws2812b-through-the-common-tim).

---

## 7. Lifetime and limits

- Container capacity is fixed; a full container reports failure instead of allocating.
- Erasing a container element does not release external resources; borrowed pointers/indices lose validity.
- Serialize access to each stateful utility, including main/IRQ sharing.
- PID gains, numerical ranges and actuator limits belong to the application.
- DMA buffers must remain alive and unchanged for the full ownership interval.
- CMSIS-DSP declarations outside the built subset do not imply linked implementations.
- The new examples were syntax-checked for F446/F722/G071 where the feature exists. This is not hardware validation or a full firmware link.

## 8. See Also

- [Examples.md](Examples.md)
- [Filters.md](Filters.md)
- [TIM.md](TIM.md)
- [ReadMe.md](ReadMe.md)
- [THIRD_PARTY.md](THIRD_PARTY.md)

---

## Русский

## Содержание

1. [Назначение и подключение](#1-назначение-и-подключение)
2. [Контейнеры](#2-контейнеры)
3. [PID-регулятор](#3-pid-регулятор)
4. [Контрольная сумма CRC32](#4-контрольная-сумма-crc32)
5. [Подмножество CMSIS-DSP](#5-подмножество-cmsis-dsp)
6. [Кодирование WS2812B](#6-кодирование-ws2812b)
7. [Время жизни и ограничения](#7-время-жизни-и-ограничения)
8. [См. также](#8-см-также)

---

## 1. Назначение и подключение

Примеры — независимые полные функции/классы C++ с публичными заголовками текущего checkout. В project.json включается соответствующий модуль; WS2812B — утилита TIM, а не отдельный модуль. Автоматической выборки, регистрации IRQ и интеграции с приложением здесь нет.

```json
"drivers": ["CONTAINERS", "PID", "CRC", "DSP"]
```

## 2. Контейнеры

| Тип | Публичные операции |
| --- | --- |
| `RingQueue` | `push`, `pop`, `empty`, `full`, `size` |
| `StaticMap` | `insert`, `find`, `erase`, `clear`, `size`, `capacity`, `raw_entries` |
| `StaticSList` | `push_back`, `front`, `next`, `erase_first_if`, `empty`, `full`, `size` |
| `StaticDList` | `push_back`, `move_to_tail`, `erase`, `erase_and_next`, `head`, `tail`, `next`, `prev`, `ref`, `valid`, `empty`, `full`, `size`, `npos` |

### Очередь и таблица уникальных ключей

Подключается CONTAINERS. RingQueue<T,N> хранит N-1 элементов; проверяйте push/pop, не предполагая наличие места. StaticMap — неупорядоченная таблица с линейным поиском и N полезными ячейками; существующий ключ заменяется даже при полном заполнении. find возвращает указатель без владения, недействительный после erase/clear. Raw entries читаются только при used=true; изменение raw-флагов used не исправляет счётчик размера. Все контейнеры имеют фиксированное хранилище для одного контекста, а не lock-free обмен IRQ/main.

```cpp
#include "RingQueue.hpp"
#include "StaticMap.hpp"
void Example() {
    RingQueue<int, 4> queue; // Three usable elements.
    bool stored = queue.push(10);
    int item = 0;
    bool received = queue.pop(item);
    (void)stored; (void)received;
    (void)queue.size(); (void)queue.empty(); (void)queue.full();

    StaticMap<int, float, 4> map;
    bool inserted = map.insert(1, 2.5f);
    bool replaced = map.insert(1, 3.5f);
    if (float* value = map.find(1)) *value = 4.0f;
    for (size_t i = 0; i < map.capacity(); ++i) {
        const auto& entry = map.raw_entries()[i];
        if (entry.used) { (void)entry.key; (void)entry.value; }
    }
    bool erased = map.erase(1);
    map.clear();
    (void)map.size(); (void)inserted; (void)replaced; (void)erased;
}
```

### Списки, стабильные индексы и удаление при обходе

StaticSList требует standard-layout тип элемента. front/next возвращают указатели на живые значения; при таком обходе структура не меняется. erase_first_if удаляет только первое совпадение. Индекс StaticDList сохраняется при перемещении живого узла, но erase делает индекс и ссылки недействительными, даже если ячейка затем занята снова. Для удаления при обходе используется erase_and_next. Удаление не вызывает деструктор значения и не освобождает ресурсы хранимых указателей/дескрипторов. Оба списка предоставляют empty/full/size.

```cpp
#include "StaticSList.hpp"
#include "StaticDList.hpp"
void Example() {
    StaticSList<int, 4> singly;
    if (!singly.push_back(10) || !singly.push_back(20)) return;
    for (int* value = singly.front(); value; value = singly.next(value))
        *value += 1;
    bool erased = singly.erase_first_if([](int value) { return value == 11; });
    (void)erased; (void)singly.empty(); (void)singly.full(); (void)singly.size();

    StaticDList<int, 4> doubly;
    auto saved = decltype(doubly)::npos;
    if (!doubly.push_back(10, &saved) || !doubly.push_back(20)) return;
    doubly.move_to_tail(saved);
    for (auto i = doubly.head(); i != decltype(doubly)::npos; ) {
        if (doubly.ref(i) == 10) i = doubly.erase_and_next(i);
        else i = doubly.next(i);
    }
    auto tail = doubly.tail();
    if (doubly.valid(tail)) {
        (void)doubly.prev(tail);
        bool removed = doubly.erase(tail);
        (void)removed;
    }
    (void)doubly.empty(); (void)doubly.full(); (void)doubly.size();
}
```

---

## 3. PID-регулятор

### PID с постоянным шагом, сброс и чтение составляющих

Подключите модуль `PID` и заголовок `PID.hpp`. Все шесть настраиваемых значений собраны в `PID::Parameters`; регулятор хранит одну приватную копию. Входные указатели закрепляются конструктором отдельно, состояние расчёта остаётся приватным. Журнала отсчётов, оценки качества, выделения памяти и внутренних блокировок нет.

**Конструкторы:**

```cpp
PID(const float* ptr_to_target, const float* ptr_to_data);
PID(float kp, float ki, float kd, float ki_saturation, float e_deadband,
    float dt, const float* ptr_to_target, const float* ptr_to_data);
```

Перегрузка с двумя указателями использует значения по умолчанию из таблицы. Прежний конструктор с восемью аргументами сохранён для явного задания настроек. Оба — `constexpr`, поддерживают `constinit` при статическом времени жизни входов. Входные переменные должны жить дольше регулятора.

| Поле `PID::Parameters` | Назначение | По умолчанию |
| --- | --- | --- |
| `kp` | Пропорциональный коэффициент | `1.0f` |
| `ki` | Интегральный коэффициент | `0.0f` |
| `kd` | Дифференциальный коэффициент | `0.0f` |
| `ki_saturation` | Максимальный модуль составляющей `ki*I`, не всего выхода | `0.0f` |
| `e_deadband` | Порог модуля ошибки: на границе/внутри зоны I удерживается и D=0, P действует | `0.0f` |
| `dt` | Настроенный интервал расчёта в секундах | `1.0f` |

**Одна пара чтения/записи параметров:**

```cpp
PID::Parameters get_parameters() const;
void set_parameters(const PID::Parameters& parameters);
```

Getter возвращает копию для чтения или сохранения в памяти приложения. Её изменение не влияет на регулятор до вызова `set_parameters()`. Setter сначала заменяет все шесть настроек, затем **один раз ограничивает накопленный интеграл по окончательным новым `ki` и `ki_saturation`**. Предыдущая ошибка и производная не обнуляются, шаг расчёта не выполняется. При нулевом `ki` сырой I сохраняется; последующая установка ненулевого `ki` применяет предел. Отдельные и групповые get/set коэффициентов удалены. Согласование изменения входов/параметров, расчёта, сброса и диагностического чтения обеспечивает приложение.

`u()` читает каждый вход один раз, вычисляет `target - data` и выполняет один шаг. Вызывайте его раз в настроенный `dt` секунд: регулятор не измеряет время и не ждёт. При изменении `dt` приложение меняет и фактический период вызова. Повторные вызовы продвигают расчёт даже при неизменных входах.

Входы/настройки конечные, `dt > 0`, `ki_saturation`/`e_deadband >= 0`; проверок с остановом в конструкторе/setter нет. Нулевой `dt` возвращает ноль без чтения входов и изменения состояния. Интеграл считается трапециями. В зоне нечувствительности I удерживается, D=0, предыдущая ошибка обновляется; P действует. Ограничения выхода и фильтра производной нет.

Хранятся только накопленный интеграл, предыдущая ошибка и текущая производная. Inline-методы `p_term()`, `i_term()`, `d_term()` используют это состояние и текущие коэффициенты; смена параметров сразу влияет на геттеры без нового чтения входов/шага расчёта. `Reset()` обнуляет состояние и геттеры составляющих, сохраняя параметры и входы. При ненулевой ошибке после сброса возможен первый скачок производной; непрерывность выхода при настройке не гарантируется.

Значения ниже показывают API, а не настройку регулятора:

```cpp
#include "PID.hpp"

float target = 100.0f;
float measured = 0.0f;
constinit PID controller(&target, &measured); // kp=dt=1, остальные настройки=0.

void Configure() {
    PID::Parameters settings{
        .kp = 1.0f,
        .ki = 0.1f,
        .kd = 0.0f,
        .ki_saturation = 2.0f,
        .e_deadband = 0.01f,
        .dt = 0.001f,
    };
    controller.set_parameters(settings); // Здесь же можно передать настройки, прочитанные из памяти.
}

// Вызывать раз в настроенный dt, после обновления measured.
float Step() {
    return controller.u();
}

PID::Parameters SettingsForStorage() {
    return controller.get_parameters(); // Копия без входных указателей и состояния расчёта.
}
void Reset() {
    controller.Reset(); // Параметры и значения входных переменных сохраняются.
}
```

Для изменения одного значения прочитайте копию, поправьте поле и примените структуру:

```cpp
void ChangeSampleRate(float sample_rate_hz) {
    auto settings = controller.get_parameters();
    settings.dt = 1.0f / sample_rate_hz; // Приложение передаёт положительную частоту.
    controller.set_parameters(settings);
    // Приложение также меняет таймер/планировщик на эту частоту.
}
```

**Миграция:** используйте `PID.hpp`, `controller.u()` и `Reset()` вместо старого C API. Прежние отдельные/групповые get/set заменены на `get_parameters()` / `set_parameters()`. `max_ki_I` теперь называется `Parameters::ki_saturation`, зона нечувствительности — `Parameters::e_deadband`. CMake собирает `math/PID.cpp`.

---

## 4. Контрольная сумма CRC32

### CRC32 одного буфера и потока фрагментов

Подключается CRC, либо crc/crc32.c собирается отдельно для переносимого кода. Это программный IEEE CRC32 (отражённый полином 0xEDB88320), не периферия CRC STM32 и не Modbus CRC16. Поток начинается с CRC32_INIT, фрагменты обновляются по порядку, finish вызывается один раз в конце. В следующий update передаётся незавершённое состояние, не финальная контрольная сумма. Разбиение входа не меняет CRC. Проверочное значение ASCII 123456789 — 0xCBF43926; завершающий ноль строки здесь не учитывается.

```cpp
#include "crc32.h"
bool CheckCRC() {
    static const char bytes[] = "123456789";
    uint32_t one = crc32_compute(bytes, sizeof bytes - 1);
    uint32_t state = CRC32_INIT;
    state = crc32_update(state, bytes, 4);
    state = crc32_update(state, bytes + 4, 5);
    uint32_t streamed = crc32_finish(state);
    return one == 0xCBF43926u && streamed == one;
}
```

---

## 5. Подмножество CMSIS-DSP

### Собираемые группы CMSIS-DSP: математика и регуляторы

Подключается DSP: CMake скачивает CMSIS-DSP v1.16.2 и собирает только FastMath, Controller и общие таблицы. Подключайте заголовки групп, не предполагая наличие всех реализаций из arm_math.h. sin/cos принимают радианы, sin_cos_f32 — градусы. Проверяйте arm_status у sqrt, atan2 и fixed-point divide; логарифму нужен положительный вход. Состояние CMSIS PID отличается от собственного класса PID с постоянным dt. Каждый CMSIS PID сначала инициализируется; arm_pid_reset очищает историю. Варианты q15/q31 используют масштаб фиксированной точки из заголовков, а не целые значения датчика без преобразования. Float16, FFT, FIR/IIR и статистика этим модулем не собираются.

```cpp
#include "dsp/fast_math_functions.h"
#include "dsp/controller_functions.h"
bool MathExample() {
    float sine = arm_sin_f32(0.5f), cosine = arm_cos_f32(0.5f);
    float root = 0.0f, angle = 0.0f;
    if (arm_sqrt_f32(4.0f, &root) != ARM_MATH_SUCCESS ||
        arm_atan2_f32(1.0f, 1.0f, &angle) != ARM_MATH_SUCCESS) return false;
    float values[2] = {1.0f, 2.0f}, logs[2], exponentials[2];
    arm_vlog_f32(values, logs, 2);
    arm_vexp_f32(logs, exponentials, 2);
    arm_sin_cos_f32(30.0f, &sine, &cosine); // Degrees.
    float alpha, beta, direct, quadrature, phase_a, phase_b;
    arm_clarke_f32(1.0f, -0.5f, &alpha, &beta);
    arm_park_f32(alpha, beta, &direct, &quadrature, sine, cosine);
    arm_inv_park_f32(direct, quadrature, &alpha, &beta, sine, cosine);
    arm_inv_clarke_f32(alpha, beta, &phase_a, &phase_b);
    q31_t quotient;
    int16_t shift;
    if (arm_divide_q31(0x20000000, 0x40000000, &quotient, &shift)
            != ARM_MATH_SUCCESS) return false;
    return root > 0.0f;
}
float ControllerExample() {
    arm_pid_instance_f32 controller{};
    controller.Kp = 1.0f;
    controller.Ki = 0.1f;
    controller.Kd = 0.0f;
    arm_pid_init_f32(&controller, 1);
    float result = arm_pid_f32(&controller, 0.25f);
    arm_pid_reset_f32(&controller);
    return result;
}
```

Другие включённые типы: sin/cos и sqrt — также q15/q31; atan2 — f32/q15/q31; векторный log — f32/f64/q15/q31, exp — f32/f64; sin_cos и преобразования координат — f32/q31; PID — f32/q15/q31. Общие таблицы линкуются один раз. Диапазоны и масштабы — в зафиксированных [FastMath](https://github.com/ARM-software/CMSIS-DSP/blob/v1.16.2/Include/dsp/fast_math_functions.h) и [Controller](https://github.com/ARM-software/CMSIS-DSP/blob/v1.16.2/Include/dsp/controller_functions.h).

---

## 6. Кодирование WS2812B

### WS2812B: пиксели, диапазоны, яркость и данные для DMA

ws2812b.hpp — заголовочная утилита для TIM; отдельного CMake-модуля WS2812B нет. Она кодирует пиксели в uint16_t значения CCR, а не пишет GPIO. Для 800 кГц выберите CCR по фактическому периоду таймера; 40 reset slots соответствуют 50 мкс без учёта хвоста передачи. SetCCR меняет константы для будущего кодирования — после него пиксели нужно перекодировать. Нельзя менять strip, пока TIM DMA читает буфер. DMA TC освобождает источник, но не означает завершения последнего PWM-периода или reset светодиодов. Для F7 действуют размещение и владение cache lines из TIM.md. N_LEDS должен быть положительным, Len — помещаться в DMA count.

```cpp
#include "ws2812b.hpp"
WS2812B_Strip<4> strip(67, 33); // Example for a 100-tick PWM period.
void Encode() {
    strip.Clear();
    strip.SetAll(RGB::Red().dim(128));
    strip.SetPixel(0, 0, 255, 0);
    strip.SetPixel(1, RGB::Blue());
    strip.Fill(2, strip.Size(), FromHSV(85, 255, 64));
    strip.SetCCR(66, 33);
    strip.Rainbow(0, 128); // Re-encode with the new CCR values.
}
const uint16_t* Data() { return strip.Data(); }
size_t Count() { return strip.Len(); }
```

RGB также предоставляет сравнение ==/!= и готовые цвета Black/White/Red/Green/Blue/Yellow/Cyan/Magenta/Orange/Purple. SetAll имеет перегрузки RGB и r/g/b. Настройка TIM, SendDMA/Start и завершение — в [TIM.md](TIM.md#9-ws2812b).

---

## 7. Время жизни и ограничения

- Вместимость контейнера фиксирована; при заполнении возвращается ошибка, новая память не выделяется.
- Удаление элемента не освобождает внешние ресурсы; выданные указатели/индексы теряют действительность.
- Доступ к состоянию согласуется приложением, в том числе между main и IRQ.
- Коэффициенты PID, численные диапазоны и пределы исполнительного устройства задаёт приложение.
- DMA-буфер должен жить и оставаться неизменным весь интервал владения.
- Объявление CMSIS-DSP вне собираемого подмножества не означает наличия реализации.
- Новые примеры проверены компилятором для F446/F722/G071 там, где есть соответствующая возможность. Это не проверка на железе и не полная линковка прошивки.

## 8. См. также

- [Examples.md](Examples.md)
- [Filters.md](Filters.md)
- [TIM.md](TIM.md)
- [ReadMe.md](ReadMe.md)
- [THIRD_PARTY.md](THIRD_PARTY.md)

---
