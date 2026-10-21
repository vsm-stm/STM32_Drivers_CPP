# Driver construction / Создание объектов драйверов

## English

**Rule of thumb:** declare every peripheral object at file scope (or as a `static` local) with `constinit`, and touch hardware only later in `SetUp()`.

```cpp
#include "uart.hpp"
using Uart3 = USART::_3;

// F4/F7 example; pick pins/routes that exist on your MCU and package.
constinit USART pc_usart(
    Uart3::instance, 115200, Uart3::TX::PD8, Uart3::RX::PD9,
    Uart3::DMA::TX_DMA1_Stream3, Uart3::DMA::RX_DMA1_Stream1);
```

**What is `_N::instance`?** A constant description of one hardware block, defined in the module's `*_defs.hpp` next to that block's pins and DMA routes. It contains:

- the peripheral and RCC register addresses (as numbers);
- clock-enable and reset masks;
- IRQ numbers;
- block capabilities.

`SetUp()` uses this descriptor directly — there is no search through a table of all peripherals. The driver stores a reference to it, so always use the provided static definitions, never a temporary copy.

**What the constructor checks.** All driver constructors (and those of the embedded DMA and IRQ adapters) are `constexpr`. With `constinit` the compiler runs them during the build and rejects the declaration when:

- a pin belongs to a different peripheral;
- a DMA route belongs to a different peripheral, has the wrong direction or is not a valid resource;
- TX and RX use the same resource;
- a fixed mode constraint is violated;
- (TIM) a channel is foreign or used twice, or a channel's DMA route is wrong.

There is no separate `Config` object and no template driver API.

**How the error is reported.** Each check calls `STM32_CONFIG_ERROR("reason")`:

| Context | What happens |
| --- | --- |
| Compile time (`constinit`) | Calls the deliberately non-`constexpr` `stm32::CONFIGURATION_ERROR<reason>()`. The reason is a template argument, so GCC prints it **inside the error message**, not only in a source excerpt. The declaration is rejected. |
| Run time (no `constinit`) | Calls `System::DebugTrap(text)` directly and stops. |

The same mechanism is used by the `consteval` descriptor constructors `PIN`, `ADC_PIN` and `TIM_PIN`. The macro picks the path with `std::is_constant_evaluated()`.

**Why `constinit` is mandatory.** Without it the compiler is not required to evaluate the constructor at build time, and a constructor cannot tell whether the keyword was used. An invalid binding then stops in `DebugTrap` when the constructor runs at start-up. This is a well-defined stop — not undefined behaviour and not a recoverable error — but run-time construction is still outside the supported usage.

**`DebugTrap` behaviour:**

- **Debug:** saves the message, prints it over SWO (blocking, on supported cores when enabled), executes `BKPT` and stops.
- **Release:** inline endless loop; the message is ignored and unused strings are removed by the optimizer.

A successful `constinit` object runs **no** checks at start-up — there is no static-initialization code at all.

**G0 DMA arguments.** On G0 you pass the physical DMA channel address; the request ID comes from the peripheral and direction:

```cpp
#include "uart.hpp"
using Uart1 = USART::_1;
constinit USART port(Uart1::instance, 115200,
    Uart1::TX::PB6, Uart1::RX::PB7,
    DMA1_Channel4_BASE, DMA1_Channel5_BASE);
```

**Where DMA request names live now.** Named requests moved from `DMA_Sx::Req::...` to the owning peripheral's `_N::DMA`. To use them with a standalone `DMA_Sx`, include that peripheral's header. The common value type `DMA_Sx::DMAReq` stays in DMA. Numeric standalone constructors accept either a base address alone (request given at `SetUp`) or a base plus an explicit request.

**Hardware access starts in `SetUp()`**, after System/RCC initialization. Numeric addresses become CMSIS pointers there. All run-time checks remain: initialization state, resource ownership, hardware readiness, transfer arguments and errors. Fields shared between `main` and interrupts stay `volatile`; the constructor initializes them without reading them.

**Initialization state.** USART/SPI/I2C/TIM/ADC keep the instance reference plus one cached register pointer (no second metadata pointer):

- null pointer outside `SetUp()` → not initialized;
- a failed first `SetUp()` and `DeInit()` clear it;
- `SetUp()` on an already initialized object returns an error and leaves the object working.

**Inherited callbacks.** Use inherited constructors (or a `constexpr` derived constructor) and constant member initializers:

```cpp
class Link : public USART {
public:
    using USART::USART;
    volatile bool wire_complete = false;
protected:
    void OnTC() override { wire_complete = true; }
};
constinit Link link(Uart3::instance, 115200, Uart3::TX::PD8, Uart3::RX::PD9);
```

### Other modules and the TIM exception

| Module | Constructor arguments |
| --- | --- |
| SPI / I2C / ADC / TIM | Own `_N::instance` first, then mode/pins/DMA in the documented order |
| `flash_data` | `constexpr`; pass a static byte array directly for `constinit` |
| DAC | `DAC_Module::_1::instance`, then the channel |
| EXTI | No instance: a numeric-base `PIN` and an edge |
| RTC | No fixed arguments; settings are applied in `SetUp()` |

System/RCC/Flash transfer logic is not affected by this constructor model.

**The second `TIM*` (finite quadrature) is the one exception.** It is bound in the constructor, but a mutable object's hardware identity cannot be read during constant initialization. Therefore TIM checks "not itself", "different hardware" and the TRGO/ITR route in `Init()` and returns `InitError` before touching hardware. The second timer must stay alive and must not be used independently while the generator owns it. ADC still checks timer readiness in `SetUp()`.

### Deprecated USART constructor

The old USART constructor taking `USART_TypeDef*` is kept with `[[deprecated]]`:

- it maps the pointer to the same `_N::instance` through a run-time `switch` (no second metadata table);
- it keeps the old run-time pin/DMA checks and the `DebugTrap` behaviour;
- it **cannot** be used with `constinit`.

References to the old DMA request names and the old typed G0 `DMAReq` variables still have to be updated to the new API.

## Русский

**Главное правило:** объявляйте каждый объект периферии глобально (или как `static` внутри функции) с `constinit`, а к аппаратуре обращайтесь позже — в `SetUp()`.

Первый аргумент конструктора — готовый `_N::instance` из `*_defs.hpp`; рядом с ним определены выводы и маршруты `_N::DMA` этого блока. Для краткости используйте `using`, как в примерах ниже.

**Что такое `_N::instance`?** Постоянное описание одного аппаратного блока:

- адреса регистров периферии и RCC (числами);
- маски включения тактирования и сброса;
- номера IRQ;
- возможности блока.

`SetUp()` пользуется этим описанием напрямую — поиска по таблице всех периферий нет. Драйвер хранит ссылку на описание, поэтому используйте готовые статические определения, а не временные копии.

**Что проверяет конструктор.** Конструкторы драйверов, встроенного DMA и адаптеров IRQ — `constexpr`. С `constinit` компилятор выполняет их при сборке и отклоняет объявление, если:

- вывод принадлежит другой периферии;
- маршрут DMA чужой, не того направления или не является допустимым ресурсом;
- TX и RX используют один и тот же ресурс;
- нарушено постоянное ограничение режима;
- (TIM) канал чужой или повторяется, либо неверен маршрут DMA канала.

Отдельного `Config`, шаблонного API и поиска периферии по адресу во время работы нет. Конструкторы описаний `PIN`/`ADC_PIN`/`TIM_PIN` сохранены.

**Как сообщается ошибка.** Каждая проверка вызывает `STM32_CONFIG_ERROR("модуль: причина")`:

| Контекст | Что происходит |
| --- | --- |
| Компиляция (`constinit`) | Вызывается намеренно не-`constexpr` функция `stm32::CONFIGURATION_ERROR<reason>()`. Причина — параметр шаблона, поэтому GCC печатает её **в самом сообщении об ошибке**, а не только в показанном фрагменте кода. Объявление отклоняется. |
| Выполнение (без `constinit`) | Напрямую вызывается `System::DebugTrap(text)`, программа останавливается. |

Тот же механизм действует в `consteval`-конструкторах `PIN`/`ADC_PIN`/`TIM_PIN`. Путь выбирается через `std::is_constant_evaluated()`.

**Почему `constinit` обязателен.** Без него компилятор не обязан выполнять конструктор при сборке, а сам конструктор не может узнать, было ли слово в объявлении. Неверная привязка тогда остановит программу в `DebugTrap` при старте. Это определённая остановка (неопределённого поведения через `unreachable` больше нет), но не разрешение создавать драйверы динамически.

**Поведение `DebugTrap`:**

- **Debug:** сохраняет сообщение, при включённой трассировке на F4/F7 выводит его через SWO (блокирующе), выполняет `BKPT` и останавливается.
- **Release:** встроенный бесконечный цикл; сообщение игнорируется, неиспользуемые строки удаляет оптимизатор.

У успешно созданного через `constinit` объекта при старте **никаких** проверок не выполняется — кода статической инициализации нет вовсе.

**DMA на G0.** Пользователь задаёт физический канал числом `DMAx_Channely_BASE`, номер запроса (request ID) берётся из описания периферии. Именованные запросы перенесены из `DMA_Sx::Req` в `_N::DMA` соответствующего драйвера; при самостоятельной работе с DMA подключите заголовок той периферии, чьи запросы используете.

**Доступ к аппаратуре начинается в `SetUp()`** после System/RCC. Конструкторы хранят числовые адреса. Проверки текущего состояния, готовности оборудования, занятости ресурсов и аргументов обмена остаются во время работы. Поля, общие для `main` и прерываний, остаются `volatile`.

**Признак инициализации.** USART/SPI/I2C/TIM/ADC хранят ссылку на instance и один кешированный указатель регистров (без второго указателя метаданных):

- нулевой указатель вне `SetUp()` → не инициализирован;
- ошибка первой настройки и `DeInit()` обнуляют его;
- повторный `SetUp()` уже настроенного объекта возвращает ошибку, объект продолжает работать.

**Второй TIM — согласованное исключение.** Указатель закрепляется в конструкторе, но соответствие аппаратных блоков и маршрут TRGO/ITR проверяются в `Init()` с возвратом `InitError` до изменения оборудования. ADC проверяет готовность переданного таймера в `SetUp()`.

**Устаревший конструктор USART** с CMSIS-указателем сохранён как `[[deprecated]]`. Он сопоставляет указатель с тем же `_N::instance` через `switch` во время работы и сохраняет прежние проверки с `DebugTrap`. С `constinit` он **не работает**. Старые имена DMA и типизированные переменные G0 `DMAReq` нужно перенести на новый API отдельно.

Эта модель не добавляет поддержку G4/H7 и не меняет алгоритмы обмена или модель IRQ.

## Примеры и порядок использования

Ниже — **независимые** варианты объявления. Выберите подходящий; не объявляйте одновременно всех показанных владельцев одной периферии. Примеры F4/F7 используют периферию и выводы, доступные, в частности, у F429/F767. Для своего МК и корпуса берите определения из его `*_defs.hpp` и сверяйте выводы с платой. Один поток DMA не могут занимать два работающих объекта.

### Общие правила

1. Нужен C++20. Объявляйте драйверы глобально либо `static constinit` внутри функции. Обычный автоматический (стековый) объект правилам API не соответствует.
2. Выводы и маршруты задавайте именованными constexpr-определениями, а описание периферии — готовым `_N::instance`. Не создавайте локальные временные `Instance`: драйвер хранит ссылку на описание.
3. В `main` сначала успешно выполните System/RCC Init, затем `SetUp()` периферии. Конструкторы не включают тактирование и не занимают DMA/IRQ.
4. Проверяйте результат каждого `SetUp()` и запуска обмена. Проверка привязок при сборке не гарантирует готовность аппаратуры и свободу ресурсов во время работы.
5. Объект, второй TIM и активные буферы должны существовать всё время работы и регистрации IRQ. Повторный `SetUp()` требует `DeInit()`. Менять реестр прерываний из callback запрещено.
6. На F7 используйте некешируемую DMA-область либо соблюдайте выравнивание, границы кеш-линий и передачу владения буфером по документации модуля. `constinit` не выбирает область памяти и не заменяет обслуживание кеша.

Локальная область видимости допустима для объекта со статическим временем жизни:

```cpp
USART& Console()
{
    using U = USART::_3;
    static constinit USART port(U::instance, 115200, U::TX::PD8, U::RX::PD9);
    return port;
}
```

Это альтернативный владелец USART3 — не используйте его одновременно с другими объектами этой периферии. Инициализация объекта постоянная, но `SetUp()` всё равно вызывается явно после System/RCC. `constinit` не делает объект `const`: обмены и callback меняют его состояние во время работы.

### USART: без DMA, один DMA, оба DMA

```cpp
#include "uart.hpp"
using U = USART::_3;

// Выберите одно объявление для USART3.
constinit USART polling(U::instance, 115200, U::TX::PD8, U::RX::PD9);
constinit USART tx_dma(U::instance, 115200, U::TX::PD8, U::RX::PD9,
    U::DMA::TX_DMA1_Stream3);
constinit USART rx_dma(U::instance, 115200, U::TX::PD8, U::RX::PD9,
    {}, U::DMA::RX_DMA1_Stream1);
constinit USART both_dma(U::instance, 115200, U::TX::PD8, U::RX::PD9,
    U::DMA::TX_DMA1_Stream3, U::DMA::RX_DMA1_Stream1);
```

- Отсутствующая линия — `PIN{}`, отсутствующий DMA — `{}`.
- С DMA по-прежнему доступны блокирующие и IRQ-методы; DMA-методы — только для закреплённого направления.

На G0 вместо именованного маршрута передаётся физический канал:

```cpp
using U = USART::_1;
constinit USART port(U::instance, 115200, U::TX::PB6, U::RX::PB7,
    DMA1_Channel1_BASE, DMA1_Channel2_BASE);
```

На F7/G0 источник тактирования USART — последний аргумент, после обоих DMA:

```cpp
using U = USART::_1;
constinit USART port(U::instance, 115200, U::TX::PB6, U::RX::PB7,
    {}, {}, USART::ClockSource::HSI);
```

- По умолчанию — PCLK. SYSCLK/HSI/LSE выбираются так же, если их поддерживает данный экземпляр.
- Запуск генератора до `SetUp()` обеспечивает приложение. Конструктор проверяет допустимость выбора, `SetUp()` — готовность генератора.
- На F4 этого аргумента нет.

Наследование callback:

```cpp
class Link : public USART {
public:
    using USART::USART;
    volatile bool wire_complete = false;
protected:
    void OnTC() override { wire_complete = true; }
};
constinit Link link(U::instance, 115200, U::TX::PD8, U::RX::PD9);
```

Полный цикл обменов, IDLE, ошибки, TC и callback: [UART.md](UART.md).

### SPI: TX, RX, TXRX, CS, DMA и slave

```cpp
#include "spi.hpp"
using S = SPI::_1;

// Независимые варианты. Аргументы: instance, направление, SCK, MOSI, MISO, CS/NSS, TX DMA, RX DMA.
constinit SPI tx(S::instance, SPI::Direction::TX, S::SCK::PA5, S::MOSI::PA7);
constinit SPI rx(S::instance, SPI::Direction::RX, S::SCK::PA5,
    {}, S::MISO::PA6, {}, {}, S::DMA::RX_DMA2_Stream0);
constinit SPI duplex(S::instance, SPI::Direction::TXRX, S::SCK::PA5,
    S::MOSI::PA7, S::MISO::PA6, PIN(GPIOB_BASE, 0),
    S::DMA::TX_DMA2_Stream3, S::DMA::RX_DMA2_Stream0);
```

Режим работы задаётся в `SetUp()`:

```cpp
// В main, после System/RCC; здесь выбран duplex из предыдущего блока.
if (duplex.SetUp(SPI::Master_sel::Master, SPI::NSS_ctrl::Software,
        SPI::Data_frame_format::Byte) != SysInitStatus::InitOK)
    System::DebugTrap("SPI setup failed");
// Далее вызывайте duplex.Process() в основном цикле для завершения асинхронных обменов.
```

- Без программного CS передайте `PIN{}`. MOSI/MISO тоже могут отсутствовать, если физически не используются.
- В режиме RX master такты формируются передачей пустых байтов (dummy). При RX DMA второй DMA не нужен — dummy обслуживает TX-прерывание.
- Для TX нельзя задавать RX DMA, для RX — TX DMA.
- На G0 последние аргументы — `DMAx_Channely_BASE`, как у USART.

**Slave** создаётся тем же конструктором с аппаратным NSS (например, `S::SS::PA4`) вместо программного CS; в `SetUp()` выбираются `Slave` и `Hard`. Приложение сообщает о снятии NSS через `NssReleased`; граница пакета определяется по NSS, а не по DMA TC/BSY. Условие последнего DMA-запроса на G0, переполнение, 16-битные кадры, асинхронные вызовы и `Process`: [SPI.md](SPI.md).

### I2C: та же конфигурация частоты, что у RCC

```cpp
#include "i2c.hpp"
using I = I2C::_1;
constexpr ClockSystem::Config clocks{};
constinit I2C bus(I::instance, I::SCL::PB8, I::SDA::PB9);

void InitBus() // В main до обменов.
{
    if (System::Init() != SysInitStatus::InitOK)
        System::DebugTrap("System setup failed");
    if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
        System::DebugTrap("RCC setup failed");
    if (bus.SetUp<clocks, I2C::SPEED::STANDARD>() != SysInitStatus::InitOK)
        System::DebugTrap("I2C setup failed");
}
```

- F4/F7: после выводов можно добавить `I::DMA::TX_DMA1_Stream6` и `I::DMA::RX_DMA1_Stream0`; любой из них можно оставить `{}`.
- G0: передавайте физические каналы DMA.
- Конструктор проверяет привязки; `SetUp()` рассчитывает тайминги при компиляции, а при запуске сверяет APB1 с опубликованной частотой RCC.

FAST/FAST_PLUS, ограничения F4, Send/ReadReg, callback и восстановление шины: [I2C.md](I2C.md).

### ADC: Single, Continuous, DMA, TIM и EXTI

```cpp
#include "adc.hpp"
using A = ADC_N::_1;

constinit ADC_N single(A::instance, ADC_N::Conversion::Single);
constinit ADC_N continuous(A::instance, ADC_N::Conversion::Continuous);

#if defined(STM32G0)
constexpr ADC_N::DMAReq adc_dma = DMA1_Channel1_BASE;
#else
constexpr ADC_N::DMAReq adc_dma = A::DMA::RX_DMA2_Stream0;
#endif
constinit ADC_N capture(A::instance, ADC_N::Conversion::Single, adc_dma);
constinit ADC_N circular(A::instance, ADC_N::Conversion::Continuous, adc_dma);

// Настройка таймера — задача приложения. Источник выбирается из enum данного семейства.
#if defined(STM32F7)
constinit TIM sample_clock(TIM::_2::instance);
constinit ADC_N triggered(A::instance, ADC_N::Conversion::Single,
    adc_dma, &sample_clock, ADC_N::Trigger::TIM2_TRGO);
#else
constinit TIM sample_clock(TIM::_3::instance);
constinit ADC_N triggered(A::instance, ADC_N::Conversion::Single,
    adc_dma, &sample_clock, ADC_N::Trigger::TIM3_TRGO);
#endif
constinit ADC_N external(A::instance, ADC_N::Conversion::Single,
    adc_dma, nullptr, ADC_N::Trigger::EXTI11);
```

Выберите один объект ADC.

- **Запуск от таймера:** в `main` сначала `sample_clock.SetUp(freq)`, затем `sample_clock.SetUpTrigger()`, затем `SetUp()` ADC. Значение `freq` задаёт приложение.
- **CCx вместо TRGO:** настройте compare-событие таймера и выберите соответствующий `ADC_N::Trigger::TIMx_CCx`.
- **EXTI11:** источник фронтов настраивает приложение; указатель на TIM должен быть `nullptr`.
- Continuous с любым внешним триггером запрещён.

После успешного `SetUp()` ADC добавьте каналы и запустите преобразование:

```cpp
// Пример для capture: один канал, один отсчёт. Буфер живёт всё время обмена.
alignas(32) static uint16_t values[16]{}; // Отдельные 32 байта под кеш-линию F7.
void StartCapture()
{
    if (capture.SetUp() != SysInitStatus::InitOK)
        System::DebugTrap("ADC setup failed");
#if defined(STM32G0)
    const auto added = capture.AddChannel(ADC_N::IN::PA0_IN0);
#else
    const auto added = capture.AddChannel(A::PA0_IN0);
#endif
    if (added != SysStatus::OK || capture.Start(values, 1) != SysStatus::OK)
        System::DebugTrap("ADC capture failed");
}
```

- Без DMA — `Start()` без буфера. С DMA — один `Start(buf, len)`; однократный/кольцевой режим и триггер определяются конструктором.
- С триггером `Start()` готовит ADC/DMA и запускает переданный TIM.
- Для повторного конечного захвата снова вызывайте `Start()`, а не `SetUp()`.
- Для `DMA_HT` (половина буфера) вызовите `IRQ_en(IRQ::DMA_HT, ENABLE)` в простое до `Start()`; длина должна быть чётной. На F7 половины буфера не должны делить кеш-линию.

Схемы каналов, чтение без DMA, наследование callback, ошибки и сроки обработки кольцевого буфера: [ADC.md](ADC.md).

### TIM: база, каналы, режим всего таймера, второй TIM

```cpp
#include "tim.hpp"
using T = TIM::_3;
constinit TIM base(T::instance);
constinit TIM pwm(T::instance, {T::CH1::PA6}, {T::CH2::PA7});

// В main, для pwm:
// pwm.SetUp(1000, nullptr, 1000);     // Update 1 кГц; период 1000 тиков.
// pwm.SetUpPWM(TIM::Channel::CH1, 250);
// pwm.SetUpPWM(TIM::Channel::CH2, 750);
// pwm.Start();                        // Проверить результаты всех SetUp* до Start.
```

- Конструктор принимает до четырёх пар `{вывод, dma}`. Порядок не обязан совпадать с номером канала — номер содержится в `TIM_PIN`. Пустой слот — `{}`.
- DMA для PWM: маршрут `T::DMA::CHx_DMAy_Streamz` (если есть у МК); на G0 — физический канал. Capture в текущем API DMA не использует.
- Compare/TRGO могут работать без внешнего вывода — они настраиваются функциями общего TIM.
- Callback закрепляются только соответствующим `SetUp*`.

```cpp
// Независимые варианты, а не одновременные владельцы TIM3.
constinit TIM encoder(T::instance, {T::CH1::PA6}, {T::CH2::PA7});
// main: encoder.SetUpEncoder(65536); encoder.Start(); encoder.Count();

constinit TIM counter(TIM::_2::instance);
constinit TIM generator(T::instance,
    {T::CH1::PA6}, {T::CH2::PA7}, {}, {}, &counter);
// main: generator.SetUpQuadrature(TIM::Channel::CH1, TIM::Channel::CH2, 1000, 1000);
// Затем generator.GenPulse(100); проверять каждый возвращаемый статус.
```

- Второй TIM проверяется в `Init()`, а не чтением изменяемого объекта при `constinit`. Неподходящая связь TRGO/ITR → `InitError`.
- Для capture/counter период обязателен. Частота в `SetUp()` — частота события Update, а не счёта CNT.
- Encoder использует CH1/CH2 и занимает режим всего таймера.

PulseBurst, аппаратный счётчик, capture, измерение ширины, ITR/slave, OPM, PWM DMA uint16/uint32, ограничения остановки и внешнее маскирование IRQ: [TIM.md](TIM.md).

### GPIO, EXTI и DAC

```cpp
#include "exti.hpp"
constinit PIN led(GPIOA_BASE, 5);
constinit EXTI_Line edge(PIN(GPIOC_BASE, 13), EXTI_Line::Edge::Falling);

// Только если у выбранного МК есть поддерживаемый DAC:
#include "dac.hpp"
constinit DAC_Module analog(DAC_Module::_1::instance, DAC_Module::CHANNEL::CH1);
```

- **EXTI:** для собственного callback наследуйте `EXTI_Line`, используйте constexpr-конструктор (или `using EXTI_Line::EXTI_Line`) и объявляйте итоговый объект `constinit`. `SetUp()` настраивает GPIO; PAx и PBx с одним номером линии одновременно занять нельзя. Фронт: Rising/Falling/Both. См. [EXTI.md](EXTI.md).
- **DAC:** CH1 — PA4, CH2 — PA5 (если есть). После `SetUp()` проверяйте статус `SetValue()`. См. [DAC.md](DAC.md).
- **GPIO:** числовой конструктор проверяет порт/номер/AF через `consteval`. Перегрузка `PIN(GPIOA, 5)` с CMSIS-указателем для `constinit` не подходит и полагается на корректные аргументы. См. [GPIO.md](GPIO.md).

### flash_data: RAM-буфер с постоянным адресом

```cpp
#include "flash.hpp"
static uint8_t payload[16]{};
// Пример предполагает, что блок стирания 3 явно зарезервирован вне образа и других данных.
constexpr uint32_t settings_sector = 3;
constinit flash_data settings(settings_sector, payload, sizeof payload);
```

- Конструктор только запоминает адрес, размер и номер. `SetUp()` проверяет реальную область и журнал.
- Номер сектора выбирайте по своей карте памяти — не копируйте `3` без резервирования.
- `reinterpret_cast<uint8_t*>(&some_struct)` не является константным выражением C++20, поэтому так объявить `constinit`-объект нельзя. Используйте байтовый буфер, как в примере.

Формат данных, единственный сектор, промежуточный статус и потеря данных при стирании: [Flash.md](Flash.md).

### Modbus и самостоятельный DMA

```cpp
#include "Modbus-slave.hpp"
using U = USART::_3;
constinit ModbusRTU_Slave modbus(U::instance, 19200,
    U::TX::PD8, U::RX::PD9, PIN(GPIOD_BASE, 2),
    U::DMA::TX_DMA1_Stream3, U::DMA::RX_DMA1_Stream1);

// Отдельный альтернативный потребитель TX-потока, не одновременно с modbus:
constinit DMA_Sx standalone(U::DMA::TX_DMA1_Stream3);
```

- Modbus требует оба DMA; DE — обычный GPIO приложения. Формат, парсер, TX-буфер и граница по IDLE вместо строгих RTU t1.5/t3.5: [Modbus.md](Modbus.md).
- На G0 самостоятельный DMA: `constinit DMA_Sx channel(DMA1_Channel1_BASE, USART::_1::DMA::TX);`.
- Конструктор DMA только сохраняет описание; `SetUp()` проверяет ресурс и параметры. При работе через драйвер периферии отдельный объект DMA не создаётся: [DMA.md](DMA.md).
- RTC не принимает привязок в конструкторе, настройки применяются в `SetUp()`: [RTC.md](RTC.md).

### Как читать ошибку компиляции

```cpp
using U = USART::_3;
// Намеренно неверно: PA2 относится к USART2.
constinit USART bad_pin(U::instance, 115200, USART::_2::TX::PA2, U::RX::PD9);
// Отдельный неверный пример: на место RX передан TX-маршрут.
constinit USART bad_dma(U::instance, 115200, U::TX::PD8, U::RX::PD9,
    {}, U::DMA::TX_DMA1_Stream3);
```

GCC сообщает, что вызов `CONFIGURATION_ERROR` не является `constexpr`, и печатает параметр шаблона `reason`:

- в первом случае — `USART: TX pin belongs to another peripheral`;
- во втором — `USART: invalid RX DMA route`.

Существенный фрагмент сообщения (точное оформление зависит от версии GCC):

```text
call to non-'constexpr' function 'void stm32::CONFIGURATION_ERROR()
[with Text<...> reason = ...{"USART: TX pin belongs to another peripheral"}]'
```

**Ищите первую ошибку с `reason`** — остальные сообщения вторичны.

Общий механизм находится в `system.hpp`. Каждая проверка в конструкторе выглядит так:

```cpp
if (tx.IsValid() && tx.periph_base && tx.periph_base != instance.periph)
    STM32_CONFIG_ERROR("USART: TX pin belongs to another peripheral");
```

Шаблон используется только внутри диагностики; сигнатуры конструкторов от него не зависят.

| Модуль | Какие ошибки различает конструктор |
| --- | --- |
| GPIO/ADC_PIN/TIM_PIN | Номер вывода/AF/канала, отсутствующий порт или блок |
| USART | Чужой вывод TX/RX, неверный TX/RX DMA, общий ресурс, источник тактирования |
| SPI | SCK, принадлежность каждого вывода, TX/RX DMA, общий ресурс, направление |
| I2C | Отсутствующий/чужой SCL/SDA, TX/RX DMA, общий ресурс |
| ADC | DMA, режим преобразования, Continuous + триггер, наличие указателя TIM |
| TIM | Нет вывода для DMA, чужой/повторный канал, общий DMA, маршрут |
| EXTI | Внешний вывод GPIO, фронт |
| DAC | Номер/доступность канала |
| Modbus | Отсутствующий TX/RX DMA; остальные привязки проверяет USART |
