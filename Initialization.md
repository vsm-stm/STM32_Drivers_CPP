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
There is no separate Config object, template driver API or runtime DebugTrap in
the new constructor path. The existing PIN/ADC_PIN/TIM_PIN compile-time descriptor
constructors remain supported.

`constinit` is required by this usage contract. Omitting it does not require the
compiler to evaluate the constructor during translation. The rejection branches
use `__builtin_unreachable()`; they are not runtime validation or a recoverable
error path. Do not call the new constructors with unchecked dynamic arguments.

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
mode/pin/DMA arguments retain the agreed order. DAC takes
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

`constinit` обязателен по контракту использования нового API. Без него компилятор
не обязан вычислять конструктор при сборке. Ветки `__builtin_unreachable()` служат
для отклонения неверного постоянного выражения; передавать в новый конструктор
непроверенные динамические аргументы нельзя.

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
