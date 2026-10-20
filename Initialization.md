# Driver construction / Создание объектов драйверов

## English

Declare peripheral objects at file scope or as `static` objects with `constinit`:

```cpp
#include "uart.hpp"
using Uart3 = USART::_3;

// F4/F7 example; select pins/routes available on the target MCU and package.
constinit USART pc_usart(
    Uart3::instance, 115200, Uart3::TX::PD8, Uart3::RX::PD9,
    Uart3::DMA::TX_DMA1_Stream3, Uart3::DMA::RX_DMA1_Stream1);
```

`_N::instance` contains immutable hardware metadata: numeric peripheral and RCC
register addresses, clock/reset masks, IRQ numbers and block capabilities. It is
defined beside that instance's pins and DMA routes in the corresponding
`*_defs.hpp`. `SetUp()` uses this descriptor directly; it does not search a table
of all peripherals. The descriptor must outlive the driver; use the provided
static definitions.

The driver, embedded DMA and IRQ adapters have `constexpr` constructors. During
constant initialization, invalid pin ownership, DMA ownership/direction/resource,
duplicate TX/RX resources, and fixed mode constraints reject the declaration.
TIM also checks channel ownership, duplicate channels and channel DMA routes.
There is no separate Config object or template driver API. Each failed check uses
`STM32_CONFIG_ERROR("reason")`, which uses `std::is_constant_evaluated()` to select
the diagnostic path. During constant evaluation it calls non-constexpr
`stm32::CONFIGURATION_ERROR<reason>()`. The reason is a string template argument,
so GCC includes it in the diagnostic itself, not only in a source-code excerpt.
Under `constinit`, the call rejects constant initialization. Existing consteval
PIN/ADC_PIN/TIM_PIN descriptor constructors use the same mechanism. At runtime
the macro calls `System::DebugTrap(text)` directly, without a call to the template.

`constinit` is required by this usage contract. Omitting it does not require the
compiler to evaluate the constructor during translation. A constructor cannot
inspect whether the declaration used the `constinit` keyword. Without it, an
invalid binding stops in `DebugTrap` if construction executes at runtime; it is
not undefined behavior or a recoverable error. This fallback does not make runtime
construction part of the supported usage contract.

In DEBUG, DebugTrap saves the message, emits blocking SWO on supported cores when
enabled, executes BKPT and stops. In Release, its inline body ignores the message
and stops; optimisation can discard unused strings. Successful constant
initialization does not execute constructor checks at startup. Binary size/string
elimination has not been measured for this revision.

G0 peripheral constructors take numeric physical DMA channel addresses; the
request source is fixed by the peripheral and direction:

```cpp
#include "uart.hpp"
using Uart1 = USART::_1;
constinit USART port(Uart1::instance, 115200,
    Uart1::TX::PB6, Uart1::RX::PB7,
    DMA1_Channel4_BASE, DMA1_Channel5_BASE);
```

Named request definitions moved from `DMA_Sx::Req::...` to the corresponding
peripheral's `_N::DMA`. Include that peripheral's header when using its named
requests with standalone DMA. The common `DMA_Sx::DMAReq` value type stays in
DMA. Numeric standalone constructors accept a base alone (request supplied at
SetUp), or a base plus an explicit request.

Hardware access starts in `SetUp()`, after System/RCC initialization. Numeric
addresses become CMSIS pointers there or in DMA register accessors. Existing
runtime checks for initialization state, resource ownership, hardware readiness,
transfer arguments and errors remain. Shared main/IRQ fields remain `volatile`;
the constructor initializes them without reading their volatile values.

For inherited callbacks, use inherited constructors or a `constexpr` derived
constructor and constant member initializers:

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

SPI/I2C/ADC/TIM take their own `_N::instance` as the first argument; their fixed
mode/pin/DMA arguments retain the agreed order. `flash_data` also has a constexpr
constructor; pass a static byte array directly for constinit construction. DAC takes
`DAC_Module::_1::instance` followed by its channel. EXTI has no peripheral-instance
parameter: use `constinit` with a numeric-base PIN and an edge. RTC has no fixed
constructor arguments requiring migration. System/RCC/Flash transfer logic is
unchanged by this constructor migration.

The second `TIM*` remains a constructor binding for finite quadrature. Its
hardware identity cannot be read through an ordinary mutable object during
constant initialization. TIM checks self-use, equal hardware and the TRGO/ITR
route in `Init()` and returns `InitError` before touching hardware on failure.
The second timer must stay alive and must not be used independently while owned
by the generator. ADC still checks timer readiness during SetUp.

### Deprecated USART constructor

The USART constructor whose first parameter is `USART_TypeDef*` is retained with
`[[deprecated]]`. It uses a runtime switch to the same `_N::instance` definitions
and retains the previous runtime pin/DMA checks and DebugTrap behavior. No second
metadata table is maintained. This compatibility path is not `constinit`-capable.
The relocation of DMA names and the new numeric G0 `DMAReq` alias still require
updating references to the old request API.

## Русский

Объекты периферии объявляются глобально или как `static` с `constinit`. Первый
аргумент — готовый `_N::instance` из `*_defs.hpp`; рядом с ним находятся пины и
маршруты `_N::DMA`. Для короткой записи используется `using`, как в примерах выше.

Конструкторы драйверов, встроенного DMA и адаптеров IRQ — `constexpr`. При
постоянной инициализации проверяются привязки пинов, принадлежность/направление DMA,
совпадение ресурсов и постоянные ограничения режима. TIM также проверяет каналы
и их маршруты DMA. Отдельного Config, шаблонного API и runtime-поиска периферии по
адресу нет. Имеющиеся конструкторы описаний PIN/ADC_PIN/TIM_PIN сохраняются.

`constinit` обязателен по контракту использования нового API. Конструктор не может
проверить наличие этого слова в объявлении. Каждая проверка использует
`STM32_CONFIG_ERROR("модуль: причина")`. Макрос выбирает путь через
`std::is_constant_evaluated()`. При постоянном вычислении вызывает неконстантную функцию
`stm32::CONFIGURATION_ERROR<reason>()`: строка — параметр шаблона, поэтому GCC
включает причину в само сообщение ошибки, а не только в показанный исходник.
То же действует в consteval-конструкторах PIN/ADC_PIN/TIM_PIN. Во время выполнения
макрос напрямую вызывает `System::DebugTrap(text)`, без вызова шаблонной функции.

Без `constinit` неверная привязка при runtime-создании останавливает программу в
DebugTrap; неопределённого поведения через unreachable больше нет. Это страховка,
а не разрешение создавать драйверы динамически. В DEBUG сохраняется сообщение,
при включённой трассировке F4/F7 выполняется блокирующий вывод SWO, затем BKPT и
останов. В Release тело inline, сообщение игнорируется, программа останавливается;
оптимизатор может удалить неиспользуемые строки. У успешно созданного через
constinit объекта проверки не выполняются при старте. Размер прошивки в этой
редакции не измерялся.

В G0 пользователь задаёт физический канал числовым `DMAx_Channely_BASE`, request
ID выбирается из описания периферии. Именованные запросы перенесены из
`DMA_Sx::Req` в `_N::DMA` соответствующего драйвера; при самостоятельной работе с
DMA нужно подключить заголовок периферии, чьи запросы используются.

Конструкторы сохраняют числовые аппаратные адреса. Доступ к оборудованию
начинается в SetUp после System/RCC. Проверки текущего состояния, готовности
оборудования, занятости ресурсов и аргументов обмена остаются в runtime.
`volatile` у состояния main/IRQ сохраняется.

Для второго TIM согласовано исключение: соответствие аппаратных блоков и маршрут
TRGO/ITR проверяются в Init с возвратом InitError до изменения оборудования.
Указатель закрепляется в конструкторе. ADC проверяет готовность переданного
таймера при SetUp.

Старый конструктор USART с CMSIS-указателем сохранён как deprecated. Он использует
те же instance через runtime-switch и сохраняет прежние проверки с DebugTrap.
Для него constinit недоступен. Старые имена DMA и типизированные G0 DMAReq-переменные
нужно перенести на новый API отдельно.

Миграция не добавляет поддержку G4/H7 и не меняет алгоритмы обмена или модель IRQ.

## Примеры и порядок использования

Ниже — независимые варианты объявления. Выберите подходящий вариант; не объявляйте
сразу всех показанных владельцев одной периферии. Примеры F4/F7 используют
периферию и пины, доступные, в частности, у F429/F767; для конкретного МК и корпуса
выбирайте существующие определения из его `*_defs.hpp` и сверяйте выводы с платой.
Одинаковый DMA stream нельзя занимать двумя работающими объектами.

### Общие правила

1. Требуется C++20. Объявляйте драйверы глобально либо `static constinit` внутри
   функции. Обычный автоматический объект не подходит под контракт API.
2. Пины и маршруты задавайте именованными constexpr-определениями, а описание
   периферии — готовым `_N::instance`. Не создавайте локальные временные Instance:
   драйвер хранит ссылку на описание.
3. В `main` сначала успешно выполните System/RCC Init, затем SetUp периферии.
   Конструкторы не включают тактирование и не занимают DMA/IRQ.
4. Проверяйте результат каждого SetUp/запуска обмена. Проверка привязок при сборке
   не подтверждает готовность аппаратуры и доступность ресурсов при выполнении.
5. Объект, второй TIM и активные буферы должны жить всё время работы и регистрации
   IRQ. Повторный SetUp требует DeInit. Изменять реестр из callback запрещено.
6. На F7 используйте некешируемую DMA-область либо соблюдайте выравнивание,
   границы кеш-линий и передачу владения буфером из документации модуля.
   `constinit` не выбирает область памяти и не заменяет обслуживание кеша.

Локальная область видимости допустима для объекта со статическим временем жизни:

```cpp
USART& Console()
{
    using U = USART::_3;
    static constinit USART port(U::instance, 115200, U::TX::PD8, U::RX::PD9);
    return port;
}
```

Это альтернативный владелец USART3; не используйте его одновременно с другими
объектами этой периферии. Инициализация объекта постоянная, а SetUp всё равно
явно выполняется после System/RCC. `constinit` не делает объект `const`: обмены
и callbacks меняют его состояние во время выполнения.

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

Отсутствующая линия — `PIN{}`. Наличие DMA сохраняет возможность использовать
блокирующие и IRQ-методы; методы DMA доступны для закреплённого направления.
На G0 вместо именованного маршрута передаётся физический канал:

```cpp
using U = USART::_1;
constinit USART port(U::instance, 115200, U::TX::PB6, U::RX::PB7,
    DMA1_Channel1_BASE, DMA1_Channel2_BASE);
```

Источник тактирования F7/G0 — последний аргумент, после обоих DMA:

```cpp
using U = USART::_1;
constinit USART port(U::instance, 115200, U::TX::PB6, U::RX::PB7,
    {}, {}, USART::ClockSource::HSI);
```

По умолчанию выбран PCLK. SYSCLK/HSI/LSE выбираются так же, если поддерживаются
данным экземпляром. Запуск/готовность генератора обеспечивает приложение до SetUp.
Конструктор проверяет выбор источника; SetUp проверяет его аппаратную готовность.
В F4 этого аргумента нет. Полный цикл обменов, IDLE, ошибки, TC и callbacks:
[UART.md](UART.md). Пример наследования с `constinit Link` приведён выше.

### SPI: TX, RX, TXRX, CS, DMA и slave

```cpp
#include "spi.hpp"
using S = SPI::_1;

// Независимые варианты. Последние два аргумента — TX DMA и RX DMA.
constinit SPI tx(S::instance, SPI::Direction::TX, S::SCK::PA5, S::MOSI::PA7);
constinit SPI rx(S::instance, SPI::Direction::RX, S::SCK::PA5,
    {}, S::MISO::PA6, {}, {}, S::DMA::RX_DMA2_Stream0);
constinit SPI duplex(S::instance, SPI::Direction::TXRX, S::SCK::PA5,
    S::MOSI::PA7, S::MISO::PA6, PIN(GPIOB_BASE, 0),
    S::DMA::TX_DMA2_Stream3, S::DMA::RX_DMA2_Stream0);
```

Вызов SetUp сохраняет параметры аппаратной настройки:

```cpp
// В main, после System/RCC; здесь выбран duplex из предыдущего блока.
if (duplex.SetUp(SPI::Master_sel::Master, SPI::NSS_ctrl::Software,
        SPI::Data_frame_format::Byte) != SysInitStatus::InitOK)
    System::DebugTrap("SPI setup failed");
// Далее вызывайте duplex.Process() в main loop для завершения асинхронных обменов.
```

Без программного CS передайте `PIN{}`. MOSI/MISO также могут отсутствовать, когда
физически не используются. Для RX master такты формируются через TX dummy;
при RX DMA второй DMA не требуется, dummy обслуживает TX IRQ. Для TX нельзя
задавать RX DMA, для RX нельзя задавать TX DMA. На G0 последние аргументы —
`DMAx_Channely_BASE`, как у USART.

Slave создаётся тем же конструктором с аппаратным NSS, например `S::SS::PA4`
вместо программного CS. При SetUp выбираются `Slave` и `Hard`. Приложение сообщает
снятие NSS через NssReleased; граница пакета определяется NSS, не DMA TC/BSY.
Условие завершённого последнего DMA-запроса G0, переполнение, 16-битные кадры,
асинхронные вызовы и Process: [SPI.md](SPI.md).

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

Для F4/F7 добавьте после пинов `I::DMA::TX_DMA1_Stream6` и
`I::DMA::RX_DMA1_Stream0`; любой из аргументов можно оставить `{}`. Для G0
передавайте физические DMA-каналы. Конструктор проверяет привязки; SetUp считает
тайминг при компиляции и при запуске сверяет APB1 с опубликованной частотой RCC.
FAST/FAST_PLUS, ограничения F4, Send/ReadReg, callbacks и восстановление шины:
[I2C.md](I2C.md).

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

// Настройка таймера — приложению. Источник выбирается из enum данного семейства.
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

Выберите один ADC-объект. Для запуска от таймера в main сначала выполните
`sample_clock.SetUp(freq)`, затем `sample_clock.SetUpTrigger()` и ADC SetUp.
Значение `freq` задаёт приложение. Для CCx вместо TRGO настройте compare-событие
таймера и выберите соответствующий доступный `ADC_N::Trigger::TIMx_CCx`.
Для EXTI11 источник фронтов настраивает приложение; TIM pointer должен быть nullptr.
Continuous с любым внешним триггером запрещён.

После успешного ADC SetUp добавляйте каналы и запускайте преобразование:

```cpp
// Пример для capture: один канал, один отсчёт. Буфер живёт всё время обмена.
alignas(32) static uint16_t values[16]{}; // Отдельные 32 байта для кеш-линии F7.
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

Без DMA используется `Start()` без буфера. С DMA — один `Start(buf, len)`;
параметры конструктора определяют one-shot/circular/trigger. При триггере Start
вооружает ADC/DMA и запускает переданный TIM. Для повторного конечного захвата
снова вызывайте Start, не SetUp. Для DMA_HT вызовите `IRQ_en(IRQ::DMA_HT, ENABLE)`
при простое до Start; длина должна быть чётной. Cache-линии половин F7 не должны
пересекаться. Схемы каналов, чтение без DMA, наследование callbacks, ошибки и сроки
обработки circular-буфера: [ADC.md](ADC.md).

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
// pwm.Start();                      // Проверить результаты всех SetUp* до Start.
```

Конструктор принимает до четырёх `{pin, dma}`; порядок аргументов не обязан
совпадать с номером канала — номер содержится в TIM_PIN. Незанятый слот — `{}`.
DMA для PWM задавайте маршрутом `T::DMA::CHx_DMAy_Streamz`, имеющимся у устройства;
на G0 — физическим каналом. Capture не использует DMA в текущем API.
Compare/TRGO могут работать без внешнего пина: их настройка выполняется функциями
общего TIM. Callbacks закрепляются только при соответствующем SetUp*.

```cpp
// Независимые варианты, а не одновременные владельцы TIM3.
constinit TIM encoder(T::instance, {T::CH1::PA6}, {T::CH2::PA7});
// main: encoder.SetUpEncoder(65536); encoder.Start(); encoder.Count();

constinit TIM counter(TIM::_2::instance);
constinit TIM generator(T::instance,
    {T::CH1::PA6}, {T::CH2::PA7}, {}, {}, &counter);
// main: generator.SetUpQuadrature(TIM::Channel::CH1, TIM::Channel::CH2, 1000, 1000);
// Затем generator.GenPulse(100); проверить каждый возвращаемый статус.
```

Второй TIM проверяется при Init, а не через чтение изменяемого объекта при
constinit. Неподходящая TRGO/ITR-связь возвращает InitError. Период обязателен для
capture/counter; частота SetUp — частота Update, не частота CNT. Encoder использует
CH1/CH2 и занимает режим всего таймера. PulseBurst, аппаратный counter, capture,
измерение ширины, ITR/slave, OPM, PWM DMA uint16/uint32, ограничения остановки и
внешнее маскирование IRQ: [TIM.md](TIM.md).

### GPIO, EXTI и DAC

```cpp
#include "exti.hpp"
constinit PIN led(GPIOA_BASE, 5);
constinit EXTI_Line edge(PIN(GPIOC_BASE, 13), EXTI_Line::Edge::Falling);

// Только если выбранный МК имеет поддерживаемый DAC:
#include "dac.hpp"
constinit DAC_Module analog(DAC_Module::_1::instance, DAC_Module::CHANNEL::CH1);
```

Для собственного EXTI callback наследуйте EXTI_Line, используйте constexpr
конструктор (либо `using EXTI_Line::EXTI_Line`) и объявляйте итоговый объект
constinit. SetUp EXTI настраивает GPIO; одновременно PAx/PBx на одном номере
линии занять нельзя. Edge может быть Rising/Falling/Both. См. [EXTI.md](EXTI.md).
DAC CH1 — PA4, CH2 — PA5, если доступен. После SetUp проверяйте статус SetValue;
описание: [DAC.md](DAC.md). Числовой конструктор GPIO проверяет port/pin/AF через
consteval. Сохранённый `PIN(GPIOA, 5)` с CMSIS-указателем не подходит для constinit
и полагается на корректные аргументы приложения: [GPIO.md](GPIO.md).

### flash_data: RAM-буфер с постоянным адресом

```cpp
#include "flash.hpp"
static uint8_t payload[16]{};
// Пример предполагает, что erase-unit 3 явно зарезервирован вне образа/других данных.
constexpr uint32_t settings_sector = 3;
constinit flash_data settings(settings_sector, payload, sizeof payload);
```

Конструктор только сохраняет адрес/размер/индекс. SetUp проверяет реальную область
и журнал. Номер сектора выбирайте по своей карте; нельзя копировать 3 без
резервирования. `reinterpret_cast<uint8_t*>(&some_struct)` не является постоянным
выражением C++20: для такого способа хранения constinit этим вызовом не получится.
Здесь показан byte-буфер с прямой передачей адреса. Формат данных, единственный
сектор, промежуточный статус и потеря данных при стирании: [Flash.md](Flash.md).

### Modbus и самостоятельный DMA

```cpp
#include "Modbus-slave.hpp"
using U = USART::_3;
constinit ModbusRTU_Slave modbus(U::instance, 19200,
    U::TX::PD8, U::RX::PD9, PIN(GPIOD_BASE, 2),
    U::DMA::TX_DMA1_Stream3, U::DMA::RX_DMA1_Stream1);

// Отдельный альтернативный потребитель TX stream, не одновременно с modbus:
constinit DMA_Sx standalone(U::DMA::TX_DMA1_Stream3);
```

Modbus требует оба DMA; DE — GPIO приложения. Выбор формата, parser, TX-буфер и
ограничение IDLE вместо строгого RTU t1.5/t3.5: [Modbus.md](Modbus.md).
На G0 самостоятельный DMA можно объявить как
`constinit DMA_Sx channel(DMA1_Channel1_BASE, USART::_1::DMA::TX);`.
Чистый DMA-конструктор сохраняет описание; собственный SetUp по-прежнему проверяет
ресурс/параметры. При работе через периферийный драйвер отдельный объект DMA не
создаётся: [DMA.md](DMA.md). RTC не принимает фиксированных привязок в
конструкторе, его настройки применяются в SetUp: [RTC.md](RTC.md).

### Как читать ошибку компиляции

```cpp
using U = USART::_3;
// Намеренно неверный пример: PA2 относится к USART2.
constinit USART bad_pin(U::instance, 115200, USART::_2::TX::PA2, U::RX::PD9);
// Независимый неверный пример: RX получил TX-маршрут.
constinit USART bad_dma(U::instance, 115200, U::TX::PD8, U::RX::PD9,
    {}, U::DMA::TX_DMA1_Stream3);
```

GCC сообщает, что вызов CONFIGURATION_ERROR не является constexpr, и печатает
параметр шаблона `reason`. В первом случае он содержит
`USART: TX pin belongs to another peripheral`, во втором —
`USART: invalid RX DMA route`. Точное оформление зависит от версии GCC; ниже
показан существенный фрагмент сообщения, а не результат сборки этой редакции:

```text
call to non-'constexpr' function 'void stm32::CONFIGURATION_ERROR()
[with Text<...> reason = ...{"USART: TX pin belongs to another peripheral"}]'
```

Общий механизм находится в system.hpp. В конструкторе остаётся отдельная
проверка с её конкретной причиной:

```cpp
if (tx.IsValid() && tx.periph_base && tx.periph_base != instance.periph)
    STM32_CONFIG_ERROR("USART: TX pin belongs to another peripheral");
```

Шаблон используется внутри диагностики, сигнатуры конструкторов не меняются.
При исправлении смотрите первую ошибку с reason, а не вторичные сообщения.

| Модуль | Различаемые причины в конструкторе |
|---|---|
| GPIO/ADC_PIN/TIM_PIN | Номер pin/AF/channel, отсутствующий порт или блок |
| USART | Чужой TX/RX pin, неверный TX/RX DMA, общий ресурс, источник частоты |
| SPI | SCK, принадлежность каждого pin, TX/RX DMA, общий ресурс, направление |
| I2C | Отсутствующий/чужой SCL/SDA, TX/RX DMA, общий ресурс |
| ADC | DMA, conversion, Continuous+trigger, наличие TIM pointer |
| TIM | Отсутствующий pin для DMA, чужой/повторный канал, общий DMA, маршрут |
| EXTI | Внешний GPIO pin, фронт |
| DAC | Номер/доступность канала |
| Modbus | Отсутствующий TX/RX DMA; остальные привязки проверяет USART |
