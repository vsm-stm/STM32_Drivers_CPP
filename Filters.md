# Filters — Detailed Description

*[English](#english) · [Русский](#русский)*

---

## English

## Table of Contents

1. [Why This Is Needed](#1-why-this-is-needed)
2. [Integration](#2-integration)
3. [Moving Average](#3-moving-average)
4. [Exponential Moving Average](#4-exponential-moving-average)
5. [Median of Three](#5-median-of-three)
6. [Usage Examples](#6-usage-examples)
7. [Edge Cases](#7-edge-cases)
8. [See Also](#8-see-also)

---

## 1. Why This Is Needed

`Filters` contains sample-by-sample utilities for measurements. The filters do
not acquire ADC data, run timers or perform DMA/cache handover. The application
supplies each new sample after it owns the input buffer.

| Filter | Use | History | Work per sample |
| --- | --- | --- | --- |
| `MovingAverage<N, Sample, Sum>` | Smooth noise over a fixed window | N samples and a sum | O(1); optional first fill O(N) |
| `ExponentialMovingAverage<Sample, Value>` | Smooth noise with an adjustable coefficient | Previous output | O(1) |
| `Median3<Sample>` | Reject isolated high/low spikes | Three samples | O(1) |

Each instance owns its state. There is no heap allocation, hardware setup,
callback or background processing. Do not update one instance concurrently from
an interrupt and `main`; serialize access in the application.

---

## 2. Integration

Enable the header-only module in the consuming project's `project.json`:

```json
"drivers": ["ADC", "FILTERS"]
```

The `FILTERS` module adds the `Filters/` include directory. It has no dependency
on `DSP` or any peripheral module; `ADC` in this example is optional.

Include only the required filter header. Portable code can also use these
headers directly, with `Filters/` on its include path, without the STM32 library.

All three filters accept `from_first_sample`: **true** initializes from the
first input; **false** starts processing with a zero-filled state.

| Filter | Constructor | Default startup |
| --- | --- | --- |
| Moving average | `MovingAverage<N>(from_first_sample)` | First sample (`true`) |
| EMA | `ExponentialMovingAverage<>(alpha, from_first_sample)` | First sample (`true`) |
| Median of three | `Median3<>(from_first_sample)` | First sample (`true`) |

The choice only sets `initialized`; no separate mode field is stored. This flag
is false while first-sample initialization is pending, and true immediately in
zero-start mode. It is not a "sample received" indicator. Before any input,
the sum/output and sample buffers are zero in either mode.

---

## 3. Moving Average

```cpp
#include <moving_average.hpp>

Filters::MovingAverage<4> filter;
filter.add_new_sample(100);
auto average = filter.sum / 4; // 100: all four slots contain the first sample.
```

The existing sum, ring-buffer fields and update method are preserved:

| Member | Meaning |
| --- | --- |
| `array[N]` | Ring buffer of input samples; initially zero |
| `sum` | Sum of all N buffer elements; initially zero |
| `index` | Oldest slot, replaced by the next sample |
| `initialized` | False while first-sample filling is pending; true for zero startup or after filling |
| `add_new_sample(sample)` | Replace one slot, subtract the old value and add the new one |

`N` must be positive. `Sample` defaults to `std::int32_t`; `Sum` defaults to
`std::int64_t`. Choose an accumulator that represents every sample and all
intermediate sums. No saturation or overflow detection is performed.

The filter keeps a **sum**, not a separately stored average. Divide by N at the
point of use. By default (`true`), the first call fills all N slots with that
sample and calculates their sum once in O(N) work; later updates are O(1).
Pass `false` to start with zero-filled slots; the divisor is still N:

```cpp
Filters::MovingAverage<4> from_zero{false};
from_zero.add_new_sample(100);
auto average = from_zero.sum / 4; // 25: the other three slots are still zero.
```

Integer division truncates towards zero. To obtain a fractional
average from integer samples, convert before dividing:

```cpp
float average = static_cast<float>(filter.sum) / 4.0f;
```

Preserve `sum == sum(array)` and `index < N` when accessing the public state.
Reset the complete history with `filter = {};` for default first-sample startup.
For zero startup, reconstruct with the explicit choice:
`filter = Filters::MovingAverage<4>{false};`.

---

## 4. Exponential Moving Average

```cpp
#include <exponential_moving_average.hpp>

Filters::ExponentialMovingAverage<> filter{0.25f}; // int32_t input, float output.
filter.add_new_sample(100); // value = 100.0f.
filter.add_new_sample(200); // value = 125.0f.
```

An EMA updates its output using the previous output and one new sample:

```text
value += alpha * (sample - value)
```

By default, the first sample sets `value` directly, without a rise from zero.
Pass `false` as the second constructor argument to smooth from zero starting
with the first call:

```cpp
Filters::ExponentialMovingAverage<> from_zero{0.25f, false};
from_zero.add_new_sample(100); // value = 25.0f.
```

The constructor requires a coefficient; there is no implicit default smoothing
strength. The startup argument is optional and defaults to `true`.

| Member | Meaning |
| --- | --- |
| `alpha` | Coefficient supplied to the constructor; may be changed between samples |
| `value` | Current filtered output |
| `initialized` | False while first-sample initialization is pending; true for zero startup or after initialization |
| `add_new_sample(sample)` | Convert the input and update `value` |

`Sample` defaults to `std::int32_t`; `Value` defaults to **float**. `Value` must
be a floating-point type, checked at compile time. For fractional inputs use
`ExponentialMovingAverage<float>`; choose another calculation type explicitly
with the second template parameter, e.g. `ExponentialMovingAverage<float, double>`.
The default implementation performs float arithmetic, with no implicit double
constants in the update.

Input conversion happens **before subtraction**, avoiding integer difference
overflow. It can still lose precision: float represents all integers through
2^24 in magnitude exactly, but not every larger integer. Subtraction, multiplication
and accumulation also round. A very small correction may not change `value`.

Use `0 < alpha <= 1`; the range is a caller contract, not clamped or checked at
run time. Smaller coefficients provide stronger smoothing and a slower response.
With `alpha == 1` there is no intended smoothing; conversion and arithmetic
rounding still apply. A fixed
coefficient describes smoothing per sample; changing the sample interval also
changes the response in seconds.

Inputs and state must be finite, and the intermediate arithmetic must remain
representable. The coefficient is supplied by the application; it is not
automatically calculated from a cutoff frequency.

Reset with a fresh instance, choosing the coefficient and startup again:

```cpp
filter = Filters::ExponentialMovingAverage<>{filter.alpha};
// The next sample becomes the new initial output.
filter = Filters::ExponentialMovingAverage<>{filter.alpha, false};
// The next sample is smoothed from zero instead.
```

---

## 5. Median of Three

```cpp
#include <median3.hpp>

Filters::Median3<> filter; // int32_t samples and output.
filter.add_new_sample(100); // value = 100.
filter.add_new_sample(900); // value = 100.
filter.add_new_sample(102); // value = 102.
```

A median filter selects the middle value of the three most recent samples.
For example, `100, 900, 102` produces `102`, rejecting the isolated high spike.
The stored history stays in arrival order; comparisons operate on local copies.

By default, the first sample fills all three slots, so there is no initial pull
towards zero. The second sample replaces one of the first sample's copies;
from the third call the window consists of three actual most recent samples.

With `false`, the history starts at zero: the first output is zero, the second
still depends on one zero slot, and the third uses a fully populated window:

```cpp
Filters::Median3<> from_zero{false};
from_zero.add_new_sample(100); // value = 0.
from_zero.add_new_sample(200); // value = 100.
from_zero.add_new_sample(300); // value = 200.
```

| Member | Meaning |
| --- | --- |
| `array[3]` | Ring buffer; zero startup or first-sample filling is selected at construction |
| `value` | Current median |
| `index` | Oldest slot, replaced by the next sample |
| `initialized` | False while first-sample filling is pending; true for zero startup or after filling |
| `add_new_sample(sample)` | Store one sample and update `value` |

`Sample` defaults to `std::int32_t`; `Median3<float>` and `Median3<double>` are
also supported. Comparisons do not subtract sample values, so full-range
integer inputs do not overflow. Floating-point samples must be finite.
Reset all state with `filter = {};` to initialize from a new first sample.
Use `filter = Filters::Median3<>{false};` to reset to zero startup.

This is not arithmetic averaging. With two corrupted samples in the same
three-sample window, the output may also be corrupted. Real short pulses can
be removed, and real transitions can be delayed.

---

## 6. Usage Examples

For an integer measurement with a 16-sample rolling average:

```cpp
#include <moving_average.hpp>

Filters::MovingAverage<16> measurement;

void OnMeasurement(std::int32_t sample)
{
    measurement.add_new_sample(sample);
    const auto average = measurement.sum / 16;
    (void)average; // Pass the result to the application.
}
```

For isolated spikes followed by continuous noise, the application can feed a
median output into an EMA:

```cpp
#include <median3.hpp>
#include <exponential_moving_average.hpp>

Filters::Median3<> spikes;
Filters::ExponentialMovingAverage<> smooth{0.25f};

void OnSmoothedMeasurement(std::int32_t sample)
{
    spikes.add_new_sample(sample);
    smooth.add_new_sample(spikes.value);
    const float filtered = smooth.value;
    (void)filtered; // Pass the result to the application.
}
```

Supply each ADC/DMA sample once, after the driver has completed buffer handover.
Keep sampling reasonably uniform when using filters to suppress time-domain
noise. Filter construction itself does not read hardware.

---

## 7. Edge Cases

- **One state per signal.** Interleaving channels through one filter mixes their history.
- **Restarting a measurement.** Reset the history explicitly when old samples must not contribute.
- **Startup.** Select it in the constructor: true initializes from the first sample, false starts from zero. All three default to first-sample startup. Reconstructing with no startup argument restores this default.
- **Numeric limits.** Moving sums can overflow; floating-point calculations have finite precision. There is no saturation or NaN handling.
- **Latency.** Smoothing changes the response to real signals as well as noise.
- **Execution context.** Update and read one instance sequentially; public state is not atomic.

---

## 8. See Also

- [ReadMe.md](ReadMe.md) — module selection and library integration.
- [ADC.md](ADC.md) — conversion modes and DMA buffer ownership.
- [System.md](System.md) — cache maintenance for DMA buffers.

---

## Русский

## Содержание

1. [Назначение](#1-назначение)
2. [Подключение](#2-подключение)
3. [Скользящее среднее](#3-скользящее-среднее)
4. [Экспоненциальное среднее](#4-экспоненциальное-среднее)
5. [Медиана трёх отсчётов](#5-медиана-трёх-отсчётов)
6. [Примеры использования](#6-примеры-использования)
7. [Угловые случаи](#7-угловые-случаи)
8. [См. также](#8-см-также)

---

## 1. Назначение

`Filters` содержит утилиты для обработки измерений по одному отсчёту. Фильтры
не запускают АЦП, таймеры и DMA, не обслуживают кеш. Приложение передаёт новый
отсчёт после завершения передачи буфера процессору.

| Фильтр | Назначение | История | Работа на отсчёт |
| --- | --- | --- | --- |
| `MovingAverage<N, Sample, Sum>` | Сглаживание шума в фиксированном окне | N отсчётов и сумма | O(1); первое заполнение при необходимости O(N) |
| `ExponentialMovingAverage<Sample, Value>` | Сглаживание с регулируемым коэффициентом | Предыдущий выход | O(1) |
| `Median3<Sample>` | Подавление одиночных выбросов вверх/вниз | Три отсчёта | O(1) |

Каждый экземпляр хранит своё состояние. Динамической памяти, настройки
аппаратуры, callback и фоновой обработки нет. Обращения к одному экземпляру
из прерывания и `main` приложение должно выполнять последовательно.

---

## 2. Подключение

Включите модуль заголовков в `project.json` использующего проекта:

```json
"drivers": ["ADC", "FILTERS"]
```

Модуль `FILTERS` добавляет каталог `Filters/` в пути поиска заголовков. Он не
зависит от `DSP` или периферии; `ADC` в этом примере необязателен.

Подключайте только нужный заголовок. Для переносимого кода можно использовать
заголовки отдельно от библиотеки STM32, добавив `Filters/` в пути поиска.

Все три фильтра принимают `from_first_sample`: **true** задаёт начальное
состояние первым отсчётом, **false** запускает обработку с нулевого состояния.

| Фильтр | Конструктор | Старт по умолчанию |
| --- | --- | --- |
| Скользящее среднее | `MovingAverage<N>(from_first_sample)` | С первого отсчёта (`true`) |
| EMA | `ExponentialMovingAverage<>(alpha, from_first_sample)` | С первого отсчёта (`true`) |
| Медиана трёх | `Median3<>(from_first_sample)` | С первого отсчёта (`true`) |

Выбор задаёт только `initialized`; отдельного поля режима нет. Флаг равен false,
пока ожидается инициализация первым отсчётом, и сразу true при старте с нуля.
Это не признак получения данных. До первого входа сумма/выход и буферы нулевые
в обоих режимах.

---

## 3. Скользящее среднее

```cpp
#include <moving_average.hpp>

Filters::MovingAverage<4> filter;
filter.add_new_sample(100);
auto average = filter.sum / 4; // 100: все четыре ячейки содержат первый отсчёт.
```

Существующие поля суммы/буфера и метод обновления сохранены:

| Поле или метод | Назначение |
| --- | --- |
| `array[N]` | Кольцевой буфер отсчётов; изначально нулевой |
| `sum` | Сумма всех N ячеек; изначально ноль |
| `index` | Самая старая ячейка, заменяемая следующим отсчётом |
| `initialized` | False до заполнения первым отсчётом; true при старте с нуля или после заполнения |
| `add_new_sample(sample)` | Заменить одну ячейку, вычесть старое и прибавить новое значение |

`N` должно быть положительным. По умолчанию `Sample` — `std::int32_t`, `Sum` —
`std::int64_t`. Накопитель должен вмещать каждый отсчёт и все промежуточные суммы.
Насыщения и проверки переполнения нет.

Фильтр хранит **сумму**, а не отдельное среднее. Делите её на N в месте
использования. По умолчанию (`true`) первый вызов заполняет все N ячеек первым
отсчётом и один раз вычисляет их сумму за O(N); последующие обновления — O(1).
Передайте `false` для старта с нулевыми ячейками; делитель всё равно N:

```cpp
Filters::MovingAverage<4> from_zero{false};
from_zero.add_new_sample(100);
auto average = from_zero.sum / 4; // 25: остальные три ячейки пока нулевые.
```

Целочисленное деление отбрасывает дробную часть в сторону нуля.
Для дробного результата преобразуйте тип перед делением:

```cpp
float average = static_cast<float>(filter.sum) / 4.0f;
```

При прямом доступе к полям сохраняйте `sum == sum(array)` и `index < N`.
Для сброса всей истории со стартом с первого отсчёта используйте `filter = {};`.
Для старта с нуля создайте экземпляр с явным выбором:
`filter = Filters::MovingAverage<4>{false};`.

---

## 4. Экспоненциальное среднее

```cpp
#include <exponential_moving_average.hpp>

Filters::ExponentialMovingAverage<> filter{0.25f}; // Вход int32_t, выход float.
filter.add_new_sample(100); // value = 100.0f.
filter.add_new_sample(200); // value = 125.0f.
```

EMA обновляет выход по предыдущему выходу и одному новому отсчёту:

```text
value += alpha * (sample - value)
```

По умолчанию первый отсчёт напрямую задаёт `value`, без начального подъёма от
нуля. Передайте `false` вторым аргументом конструктора, чтобы сглаживать от нуля
уже с первого вызова:

```cpp
Filters::ExponentialMovingAverage<> from_zero{0.25f, false};
from_zero.add_new_sample(100); // value = 25.0f.
```

Коэффициент обязателен в конструкторе; неявного значения силы сглаживания нет.
Аргумент старта необязателен и по умолчанию равен `true`.

| Поле или метод | Назначение |
| --- | --- |
| `alpha` | Коэффициент из конструктора; можно менять между отсчётами |
| `value` | Текущий отфильтрованный выход |
| `initialized` | False до инициализации первым отсчётом; true при старте с нуля или после инициализации |
| `add_new_sample(sample)` | Преобразовать вход и обновить `value` |

По умолчанию `Sample` — `std::int32_t`, `Value` — **float**. Тип `Value` должен
быть с плавающей точкой; это проверяется при компиляции. Для дробного входа
используйте `ExponentialMovingAverage<float>`. Другой тип расчёта задаётся явно
вторым параметром, например `ExponentialMovingAverage<float, double>`.
По умолчанию расчёт выполняется в float, без неявных констант double в формуле.

Вход преобразуется **до вычитания**, поэтому разность не переполняет
целочисленный тип. Точность при преобразовании всё же может теряться: float
точно представляет все целые до 2^24 по модулю, но не каждое большее целое.
Вычитание, умножение и накопление также округляются. Очень малая поправка может
не изменить `value`.

Используйте `0 < alpha <= 1`; приложение обеспечивает диапазон, автоматической
проверки и ограничения нет. Чем меньше коэффициент, тем сильнее сглаживание
и медленнее реакция. При `alpha == 1` сглаживание не предполагается, но округление
при преобразовании и вычислениях сохраняется. Постоянный
коэффициент задаёт сглаживание на отсчёт: изменение периода выборки меняет
реакцию в секундах.

Вход и состояние должны быть конечными, промежуточные вычисления — представимыми
в выбранном типе. Коэффициент задаёт приложение; автоматического расчёта по
частоте среза нет.

Сбрасывайте фильтр созданием нового экземпляра, заново выбрав коэффициент и старт:

```cpp
filter = Filters::ExponentialMovingAverage<>{filter.alpha};
// Следующий отсчёт станет новым начальным выходом.
filter = Filters::ExponentialMovingAverage<>{filter.alpha, false};
// Следующий отсчёт будет сглаживаться от нуля.
```

---

## 5. Медиана трёх отсчётов

```cpp
#include <median3.hpp>

Filters::Median3<> filter; // Отсчёты и выход int32_t.
filter.add_new_sample(100); // value = 100.
filter.add_new_sample(900); // value = 100.
filter.add_new_sample(102); // value = 102.
```

Фильтр выбирает среднее по порядку значение из трёх последних отсчётов.
Например, `100, 900, 102` даёт `102`, подавляя одиночный выброс вверх.
История хранится в порядке поступления; сравнения работают с локальными копиями.

По умолчанию первый отсчёт заполняет все три ячейки, поэтому начального смещения
к нулю нет. Второй отсчёт заменяет одну копию первого; начиная с третьего вызова
окно содержит три реальных последних отсчёта.

При `false` история изначально нулевая: первый выход равен нулю, второй ещё
зависит от одной нулевой ячейки, а третий использует полностью заполненное окно:

```cpp
Filters::Median3<> from_zero{false};
from_zero.add_new_sample(100); // value = 0.
from_zero.add_new_sample(200); // value = 100.
from_zero.add_new_sample(300); // value = 200.
```

| Поле или метод | Назначение |
| --- | --- |
| `array[3]` | Кольцевой буфер; старт с нуля или первого отсчёта выбирается в конструкторе |
| `value` | Текущая медиана |
| `index` | Самая старая ячейка, заменяемая следующим отсчётом |
| `initialized` | False до заполнения первым отсчётом; true при старте с нуля или после заполнения |
| `add_new_sample(sample)` | Записать отсчёт и обновить `value` |

По умолчанию `Sample` — `std::int32_t`; также поддерживаются `Median3<float>`
и `Median3<double>`. Сравнения не вычитают значения отсчётов, поэтому целые
значения полного диапазона не переполняются. Отсчёты с плавающей точкой должны
быть конечными. Сброс `filter = {};` заново запускает фильтр с первого отсчёта.
Для сброса со стартом с нуля используйте `filter = Filters::Median3<>{false};`.

Это не арифметическое усреднение. Если испорчены два отсчёта в одном окне,
результат также может быть неверным. Реальные короткие импульсы могут исчезать,
а переходы сигнала — задерживаться.

---

## 6. Примеры использования

Для целочисленного измерения со скользящим средним по 16 отсчётам:

```cpp
#include <moving_average.hpp>

Filters::MovingAverage<16> measurement;

void OnMeasurement(std::int32_t sample)
{
    measurement.add_new_sample(sample);
    const auto average = measurement.sum / 16;
    (void)average; // Передать результат приложению.
}
```

При одиночных выбросах и постоянном шуме приложение может передать выход
медианного фильтра в EMA:

```cpp
#include <median3.hpp>
#include <exponential_moving_average.hpp>

Filters::Median3<> spikes;
Filters::ExponentialMovingAverage<> smooth{0.25f};

void OnSmoothedMeasurement(std::int32_t sample)
{
    spikes.add_new_sample(sample);
    smooth.add_new_sample(spikes.value);
    const float filtered = smooth.value;
    (void)filtered; // Передать результат приложению.
}
```

Передавайте каждый отсчёт АЦП/DMA один раз после завершения передачи буфера
процессору. Для подавления шума во времени поддерживайте равномерный период
выборки. Конструирование фильтра само по себе к аппаратуре не обращается.

---

## 7. Угловые случаи

- **Отдельное состояние для каждого сигнала.** Чередование каналов через один фильтр смешивает их историю.
- **Повторное измерение.** Сбрасывайте историю явно, если старые отсчёты больше не должны влиять на результат.
- **Начальное состояние.** Выбор задаётся в конструкторе: true — первый отсчёт, false — нули. Во всех трёх фильтрах по умолчанию используется первый отсчёт. Создание экземпляра без аргумента старта восстанавливает этот режим.
- **Численные пределы.** Суммы могут переполниться, вычисления с плавающей точкой имеют конечную точность. Насыщения и обработки NaN нет.
- **Задержка.** Сглаживание изменяет реакцию и на полезный сигнал, и на шум.
- **Контекст вызова.** Обновляйте и читайте экземпляр последовательно; открытые поля не атомарны.

---

## 8. См. также

- [ReadMe.md](ReadMe.md) — выбор модулей и подключение библиотеки.
- [ADC.md](ADC.md) — режимы преобразования и владение буфером DMA.
- [System.md](System.md) — обслуживание кеша для буферов DMA.
