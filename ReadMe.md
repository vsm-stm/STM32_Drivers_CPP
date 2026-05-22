# Drivers — библиотека периферии STM32

Универсальная C++ библиотека поверх CMSIS для семейств **STM32G0**, **STM32F4**, **STM32F7**.  
Поддерживаемые периферийные модули: System, RCC, GPIO, USART, SPI, DMA, TIM, RTC, Flash, IRQ_Registry.

---

## Содержание

1. [Архитектурные принципы](#1-архитектурные-принципы)
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

---

## 1. Архитектурные принципы

### Таблицы периферии (PeriphInfo)

Вместо длинных if-else цепочек вида `if (USARTx == USART1) { RCC->APB2ENR |= ...; }` каждый драйвер хранит **статическую таблицу** с описанием всех экземпляров периферии:

```cpp
struct PeriphInfo {
    USART_TypeDef*      periph;   // указатель на периферию (USART1, USART2...)
    volatile uint32_t*  clk_reg;  // регистр включения тактирования (APBxENR)
    uint32_t            clk_bit;  // бит в clk_reg
    uint32_t const*     bus_clk;  // указатель на переменную с частотой шины
    IRQn_Type           irq;      // номер вектора прерывания
};
static const PeriphInfo usart_table[];
```

При вызове `SetUp()` драйвер проходит по таблице, находит нужную запись по указателю на периферию, и автоматически:
- включает тактирование
- читает актуальную частоту шины
- знает вектор прерывания

Это единственное место, которое нужно обновлять при добавлении нового МК.

### Валидация пинов при компиляции

Пины для USART, SPI выбираются из вложенных структур:

```cpp
USART debug(USART1, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7);
```

Каждый пин из `USART::_1::TX::PB6` содержит базовый адрес периферии. В конструкторе проверяется соответствие — если передать пин от другого USART, сработает `BKPT` и бесконечный цикл, что мгновенно обнаруживается при отладке.

### IRQ_Registry — единый диспетчер прерываний

Все прерывания проходят через один диспетчер. Стаб-обработчики в `irq_registry_config.h` вызывают `IRQ_Registry::Dispatch(IRQn)`, который перенаправляет вызов зарегистрированному объекту `IIRQHandler`.

Драйверы регистрируются и отменяют регистрацию автоматически при включении/выключении прерывания через `IRQ_en()`.

---

## 2. System

**Файлы:** `system.hpp`, `system.cpp`

Базовый модуль: FPU, Flash-акселератор, SysTick, задержки.

### Инициализация

```cpp
System::Init();  // включает FPU, настраивает Flash latency, запускает SysTick (1 кГц)
```

Должен вызываться **первым** в `main()`, до `ClockSystem::Init()`.

### Работа со временем

```cpp
uint32_t t = System::GetTick();   // мс с момента старта
System::Delay_ms(500);            // блокирующая задержка 500 мс

// Неблокирующий таймер:
uint32_t start = System::GetTick();
if ((System::GetTick() - start) > 1000) { /* прошла секунда */ }
```

### Микросекундные задержки (F4/F7)

```cpp
System::Enable_CYCCNT();   // один раз при старте
System::Delay_us(10);      // задержка 10 мкс через DWT
```

На **STM32G0** DWT недоступен — `Delay_us` не существует.

### Переменные частот шин

После `ClockSystem::Init()` доступны:

```cpp
System::SystemCoreClock  // частота ядра (AHB)
System::APB1BusClock     // частота APB1
System::APB2BusClock     // частота APB2
System::TIMxAPB1Clock    // частота для таймеров на APB1
System::TIMxAPB2Clock    // частота для таймеров на APB2
```

---

## 3. RCC / ClockSystem

**Файлы:** `rcc.hpp`, `rcc.cpp`

Конфигурирует систему тактирования: HSI/HSE, PLL, делители шин AHB/APB.

### Простой запуск (HSI без PLL)

```cpp
ClockSystem::Init();  // HSI 16 МГц, делители DIV1
```

### Запуск с PLL (рекомендуется)

Пример: STM32G0, кварц 16 МГц на входе, SYSCLK = 64 МГц:

```cpp
ClockSystem::Init(
    ClockSystem::SystemClockSource::PLL,
    16000000,  // частота HSE кварца
    ClockSystem::BusDividers{
        ClockSystem::AHB_Divider::DIV1,
        ClockSystem::APB_Divider::DIV1,
        ClockSystem::APB_Divider::DIV1
    },
    ClockSystem::PLL_CFGR{
        ClockSystem::PLL_ClockSource::HSE,
        1,   // M
        16,  // N  → VCO = 16 МГц / 1 * 16 = 256 МГц
        2,   // P
        2,   // Q
        4    // R  → SYSCLK = 256 / 4 = 64 МГц
    }
);
```

Пример: STM32F407, HSE 8 МГц, SYSCLK = 168 МГц:

```cpp
ClockSystem::Init(
    ClockSystem::SystemClockSource::PLL,
    8000000,
    ClockSystem::BusDividers{
        ClockSystem::AHB_Divider::DIV1,
        ClockSystem::APB_Divider::DIV4,   // APB1 ≤ 42 МГц
        ClockSystem::APB_Divider::DIV2    // APB2 ≤ 84 МГц
    },
    ClockSystem::PLL_CFGR{
        ClockSystem::PLL_ClockSource::HSE,
        4,    // M → VCO_in = 2 МГц
        168,  // N → VCO = 336 МГц
        2,    // P → SYSCLK = 168 МГц
        7,    // Q → USB/SDIO = 48 МГц
        2
    }
);
```

### Автоматический расчёт PLL

```cpp
// Хочу 64 МГц, источник — HSE 16 МГц
ClockSystem::InitCalcPLL(64*MHz, ClockSystem::PLL_ClockSource::HSE, 16000000);
```

`InitCalcPLL` сам подберёт M/N/R/P.

### Ограничения

`Init()` проверяет все частотные лимиты для конкретного МК и возвращает `SysInitStatus::InitError` при выходе за допустимые значения. Flash latency настраивается автоматически.

---

## 4. GPIO / PIN

**Файлы:** `gpio.hpp`, `gpio.cpp`

### Создание и настройка пина

```cpp
PIN led(GPIOB, 8);                        // порт + номер пина
PIN btn(GPIOA, 0);

led.SetUp(PIN::TYPE::OUTPUT_PushPull);    // выход push-pull
btn.SetUp(PIN::TYPE::INPUT_PullUp);       // вход с подтяжкой

led.SetLevel(true);      // HIGH
led.SetLevel(false);     // LOW
led.TogglePin();         // инверсия

bool state = btn.GetLevel();   // читаем состояние
```

### Все типы пинов

| Константа | Описание |
|---|---|
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

### Альтернативная функция

```cpp
PIN sck(GPIOA, 5);
sck.SetUp(PIN::TYPE::AF_PushPull, 5);   // AF5 = SPI1 на большинстве F4/G0
```

Драйверы (USART, SPI, TIM) вызывают `SetUp(AF_PushPull, af)` автоматически.

### Оператор присваивания и преобразования

```cpp
led = true;       // SetLevel(true)
led = false;      // SetLevel(false)
bool b = (bool)led;   // GetLevel()
```

### IsValid()

Пин, созданный через `PIN{}` (конструктор по умолчанию), возвращает `IsValid() == false`. Используется в драйверах как признак "канал не подключён":

```cpp
SPI spi(SPI1, sck, mosi, PIN{}, PIN{});   // MISO и SS не используются
```

### PinArray — массив пинов

```cpp
PIN segments[] = {
    PIN(GPIOA, 0), PIN(GPIOA, 1), PIN(GPIOA, 2), PIN(GPIOA, 3)
};
PinArray<4> seg_pins(segments);

seg_pins.SetUpAll(PIN::TYPE::OUTPUT_PushPull);
seg_pins.SetLevelAll(0b1010);    // установить по битам
uint32_t state = seg_pins.GetLevelAll();  // прочитать все сразу
```

---

## 5. IRQ_Registry

**Файлы:** `irq_registry.hpp`, `irq_registry.cpp`, `irq_registry_config.h`

### Идея

Вместо `extern "C" void USART1_IRQHandler() { my_usart.HandleIRQ(); }` для каждой периферии — единый механизм регистрации объектов. Стабы в `irq_registry_config.h` всегда зовут `IRQ_Registry::Dispatch(IRQn)`, который делегирует зарегистрированному объекту.

### IIRQHandler

Любой класс, который хочет обрабатывать прерывания, наследует `IIRQHandler`:

```cpp
class MyHandler : public IIRQHandler {
    void HandleIRQ() override {
        // вызывается из ISR
    }
};
```

### Регистрация вручную

```cpp
MyHandler handler;
IRQ_Registry::Register(TIM3_IRQn, &handler);
NVIC_EnableIRQ(TIM3_IRQn);
```

Большинство драйверов делают это **автоматически** при вызове `IRQ_en()`.

### Отмена регистрации

```cpp
IRQ_Registry::Unregister(TIM3_IRQn);
NVIC_DisableIRQ(TIM3_IRQn);
```

### Разделяемые IRQ-линии (общий вектор для нескольких каналов)

На STM32G0 каналы DMA1_Channel2 и DMA1_Channel3 делят один вектор `DMA1_Channel2_3_IRQn`.  
`IRQ_Registry` поддерживает **связный список обработчиков** на одной линии:

```cpp
// Первая регистрация — создаёт голову списка
IRQ_Registry::Register(DMA1_Channel2_3_IRQn, &handler_ch2);
// Вторая — добавляется в конец цепочки, BKPT не срабатывает
IRQ_Registry::Register(DMA1_Channel2_3_IRQn, &handler_ch3);
```

`Dispatch` вызывает **оба** обработчика по очереди. Каждый сам проверяет свой TC-флаг:

```cpp
void HandleIRQ() override {
    if (_dma->GetTC_Flag()) {   // только если именно мой канал сработал
        // ...
    }
}
```

### Добавление нового вектора

В `irq_registry_config.h` добавить строку:

```cpp
extern "C" void NEW_IRQHandler() { IRQ_Registry::Dispatch(NEW_IRQn); }
```

И увеличить `IRQ_TABLE_SIZE` если `NEW_IRQn` больше текущего максимума.

---

## 6. DMA

**Файлы:** `dma.hpp`, `dma.cpp`, `dma_requests.hpp`

Поддерживает F4/F7 (потоки, `DMA_Stream_TypeDef`) и G0 (каналы, `DMA_Channel_TypeDef`). API идентичен для обоих семейств.

### Конструктор (STM32G0)

На G0 DMAMUX1 позволяет подключить любой канал к любой периферии. Канал выбирает пользователь, запрос — из таблицы `Req::`:

```cpp
// Вариант 1: канал + запрос из таблицы (рекомендуется)
DMA_Sx dma_tx(DMA1_Channel4, DMA_Sx::Req::Usart1::TX);
DMA_Sx dma_rx(DMA1_Channel5, DMA_Sx::Req::Usart1::RX);

// Вариант 2: канал + сырой ID запроса DMAMUX1
DMA_Sx dma(DMA1_Channel3, 48u);

// Вариант 3: канал без запроса (ID передаётся позже через StreamSettings)
DMA_Sx dma(DMA1_Channel3);
```

### Конструктор (STM32F4/F7)

На F4 поток и CHSEL жёстко привязаны к периферии:

```cpp
DMA_Sx dma_tx(DMA_Sx::Req::Usart1::TX);       // DMA2, Stream7, CH4
DMA_Sx dma_tx(DMA_Sx::Req::Usart1::TX_alt);   // альтернативный поток
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

dma.SetUp(cfg);
```

### Ручной запуск передачи

```cpp
dma.ClearFlags();
dma.SetMemAddr(reinterpret_cast<uint32_t>(buf));
dma.SetCount(len);
dma.Stream_EN(ENABLE);
```

### Прерывание по TC

```cpp
IRQ_Registry::Register(dma.GetIRQn(), &my_handler);
NVIC_EnableIRQ(dma.GetIRQn());
dma.Enable_IRQ(DMA_Sx::IRQ::TC);
```

В обработчике:

```cpp
void HandleIRQ() override {
    if (_dma.GetTC_Flag()) {
        _dma.Stream_EN(DISABLE);
        _dma.ClearFlags();
        // передача завершена
    }
}
```

### Направления

| `DIR` | Значение |
|---|---|
| `From_Per` | Периферия → Память |
| `To_Per` | Память → Периферия |
| `Mem_To_Mem` | Память → Память |

### Ширина элемента

| `SIZE` | Размер |
|---|---|
| `Byte` | 8 бит |
| `Half_Word` | 16 бит |
| `Word` | 32 бита |

### Важно о разделяемых IRQ на G0

| Каналы | Вектор |
|---|---|
| CH1 | `DMA1_Channel1_IRQn` (эксклюзивный) |
| CH2, CH3 | `DMA1_Channel2_3_IRQn` (общий) |
| CH4, CH5 | `DMA1_Ch4_5_DMAMUX1_OVR_IRQn` (общий) |

Два канала на одном векторе — не проблема: `IRQ_Registry` поддерживает цепочку обработчиков.

---

## 7. USART

**Файлы:** `uart.hpp`, `uart.cpp`, `uart_defs.hpp`

### Базовое использование

```cpp
USART debug(USART1, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7);

debug.SetUp();
debug.Send((uint8_t*)"Hello\r\n", 7);

uint8_t buf[16];
debug.Receive(buf, sizeof(buf), 500);  // таймаут 500 мс
```

### Только TX (без RX)

```cpp
USART log(USART2, 115200, USART::_2::TX::PA2, PIN{});
log.SetUp();
log.Send((uint8_t*)"OK\r\n", 4);
```

### DMA-передача

```cpp
USART  uart(USART1, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7);
DMA_Sx dma_tx(DMA1_Channel4, DMA_Sx::Req::Usart1::TX);
DMA_Sx dma_rx(DMA1_Channel5, DMA_Sx::Req::Usart1::RX);

uart.SetUp();
uart.AttachDMA(&dma_tx, &dma_rx);   // SetUp DMA вызывать отдельно НЕ нужно

uart.Send_DMA(tx_buf, sizeof(tx_buf));
uart.Receive_DMA(rx_buf, sizeof(rx_buf));  // завершается по IDLE или по заполнению
```

### Прерывания

```cpp
uart.IRQ_en(USART::IRQ::RXNE, ENABLE);   // включить RXNE-прерывание
uart.IRQ_en(USART::IRQ::TXE, DISABLE);   // выключить TXE-прерывание
```

При первом `IRQ_en(..., ENABLE)` — объект сам регистрируется в `IRQ_Registry` и включает NVIC.  
При последнем `IRQ_en(..., DISABLE)` — отменяет регистрацию и выключает NVIC.

### Собственная обработка прерываний (наследование)

```cpp
class MyUART : public USART {
    using USART::USART;  // наследуем конструктор
    void OnRxByte(uint8_t b) override { ring_buf.push(b); }
    void OnIdle()            override { process_packet();  }
};

MyUART uart(USART1, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7);
```

### Изменение baudrate на лету

```cpp
uart.SetBaud(9600);
```

---

## 8. SPI

**Файлы:** `spi.hpp`, `spi.cpp`, `spi_defs.hpp`

### Базовое использование (блокирующий режим)

```cpp
SPI spi(SPI1, SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7,
                SPI::_1::MISO::PA6, SPI::_1::SS::PA4);

spi.SetUp(SPI::Master_sel::Master,
          SPI::NSS_ctrl::Software,
          SPI::TYPE::TXRX,
          SPI::Data_frame_format::Byte,
          SPI::Frame_Format::MSB,
          SPI::cPolPha::None,
          SPI::BaudRate::DIV4);

spi.SlaveSelect(ENABLE);
spi.Send(tx_buf, sizeof(tx_buf));
spi.SlaveSelect(DISABLE);
```

### Полудуплекс (только TX)

```cpp
SPI spi(SPI2, SPI::_2::SCK::PB10, SPI::_2::MOSI::PB11, PIN{}, PIN(GPIOB, 12));

spi.SetUp(SPI::Master_sel::Master, SPI::NSS_ctrl::Software,
          SPI::TYPE::TX, SPI::Data_frame_format::Byte,
          SPI::Frame_Format::MSB, SPI::cPolPha::None, SPI::BaudRate::DIV4);
```

### DMA-передача

```cpp
SPI    spi(SPI1, SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7);
DMA_Sx dma_tx(DMA1_Channel3);

spi.SetUp(...);
spi.AttachDMA(&dma_tx, nullptr);  // nullptr = без RX DMA
spi.SendDMA(buf, len);            // неблокирующая передача

// Колбэк по завершению — переопределить в наследнике:
// void OnDmaTxComplete() override { ... }
```

### Делитель частоты (BaudRate)

| Константа | Делитель |
|---|---|
| `DIV2` | PCLK / 2 |
| `DIV4` | PCLK / 4 |
| `DIV8` | PCLK / 8 |
| ... | ... |
| `DIV256` | PCLK / 256 |

### Полярность/фаза (cPolPha)

| Константа | CPOL | CPHA | Режим SPI |
|---|---|---|---|
| `None` | 0 | 0 | Mode 0 |
| `cPha` | 0 | 1 | Mode 1 |
| `cPol` | 1 | 0 | Mode 2 |
| `cPolPha` | 1 | 1 | Mode 3 |

---

## 9. TIM

**Файлы:** `tim.hpp`, `tim.cpp`

Иерархия классов:

```
TIM : IIRQHandler
    TIM_PeriodicIRQ      — периодические прерывания
    TIM_PWM              — ШИМ-выходы (1–4 канала), поддержка DMA
    TIM_InputCapture     — захват входного сигнала
    TIM_PulseMeasure     — измерение периода и длительности импульса (PWM-input)
    TIM_EncoderGenerator — генератор квадратурных импульсов для шаговых двигателей
```

### TIM_PeriodicIRQ — периодические прерывания

```cpp
TIM_PeriodicIRQ timer(TIM3);

// Настройка: 1000 Гц (1 кГц), колбэк
timer.SetUp(1000, []{ led.TogglePin(); });
timer.Start();
```

`SetUp()` автоматически вычисляет PSC и ARR, включает UE-прерывание и регистрирует объект в `IRQ_Registry`. `Start()` нужно вызвать явно.

### TIM_PWM — ШИМ

```cpp
// Одноканальный ШИМ на PA8 (TIM1 CH1)
TIM_PWM pwm(TIM1, PIN{GPIOA, 8});

pwm.SetUp(10000);          // 10 кГц ШИМ, ARR=999 (0.1% разрешение)
pwm.SetDuty(TIM::TIM_Channel::CH1, 75);   // 75% скважность
```

Четырёхканальный ШИМ:

```cpp
TIM_PWM pwm(TIM3,
    PIN{GPIOA, 6},  // CH1
    PIN{GPIOA, 7},  // CH2
    PIN{GPIOB, 0},  // CH3
    PIN{GPIOB, 1}   // CH4
);
pwm.SetUp(50);   // 50 Гц (сервоприводы)
pwm.SetDuty(TIM::TIM_Channel::CH1, 5);   // 5% = ~1 мс (мин угол)
pwm.SetDuty(TIM::TIM_Channel::CH1, 10);  // 10% = ~2 мс (макс угол)
```

Прямая запись CCR (для протоколов типа WS2812B):

```cpp
TIM_PWM ws(TIM1, PIN{GPIOA, 8});
ws.SetUp(800000, 79);    // 800 кГц, ARR=79 (явный ARR)
ws.SetCCR(TIM::TIM_Channel::CH1, 52);   // ~0.8 мкс (бит "1")
ws.SetCCR(TIM::TIM_Channel::CH1, 26);   // ~0.4 мкс (бит "0")
```

### TIM_PWM + DMA — генерация WS2812B

Самый мощный режим: DMA записывает CCR-значения на каждое событие Update, без участия CPU.

```cpp
constexpr size_t N_LEDS = 8;
constexpr uint16_t ONE  = 52;
constexpr uint16_t ZERO = 26;

static uint16_t ws_buf[N_LEDS * 24 + 40];  // +40 нулей = 50 мкс reset
// ... заполнить ws_buf значениями ONE/ZERO

DMA_Sx dma_ws(DMA1_Channel3);
TIM_PWM ws_tim(TIM1, PIN{GPIOA, 8});

ws_tim.SetUp(800000, 79);                         // 800 кГц, ARR=79
ws_tim.AttachDMA(&dma_ws, TIM::TIM_Channel::CH1); // привязать DMA к CH1
ws_tim.SetDMACallback([]{ /* передача завершена */ });

ws_tim.SendDMA(ws_buf, sizeof(ws_buf)/sizeof(ws_buf[0]));

// Проверка готовности:
while (ws_tim.IsDMABusy()) {}
```

`AttachDMA` настраивает DMAMUX1, периферийный адрес (CCR1), ширину Half_Word и регистрирует обработчик DMA TC в `IRQ_Registry` автоматически.

### TIM_InputCapture — захват

```cpp
TIM_InputCapture cap(TIM3, PIN{GPIOA, 6}, TIM::TIM_Channel::CH1);

cap.SetUp(100000, []{ uint32_t v = cap.GetCapture(); /* ... */ });
cap.Start();
```

### TIM_PulseMeasure — измерение PWM-сигнала

Режим PWM-input: один канал измеряет период, второй — ширину импульса.

```cpp
TIM_PulseMeasure pm(TIM3, TIM::Line{PIN{GPIOA, 6}, TIM::TIM_Channel::CH1});
pm.SetUp(100000);   // максимальная ожидаемая частота 100 кГц
pm.Start();

// После прихода сигнала:
uint32_t period = pm.GetPeriod();   // период в тактах таймера
uint32_t width  = pm.GetWidth();    // ширина импульса
pm.Clear();
```

### TIM_EncoderGenerator — квадратурный генератор

```cpp
TIM_EncoderGenerator enc(TIM3,
    TIM::Line{PIN{GPIOA, 6}, TIM::TIM_Channel::CH1},  // A
    TIM::Line{PIN{GPIOA, 7}, TIM::TIM_Channel::CH2}   // B (90° сдвиг)
);

enc.SetUp(1000, 99, 50, 50);  // 1000 Гц, период ARR=99, CCR=50 для обоих каналов
enc.SetCompleteCallback([]{ /* завершено */ });
enc.GenPulse(200);   // 200 шагов вперёд
enc.GenPulse(-50);   // 50 шагов назад
```

### Алгоритм вычисления PSC/ARR

```
ratio = bus_clk / freq         // полное произведение (PSC+1)*(ARR+1)
psc   = ratio / 65536          // минимальный делитель, чтобы ARR влез в 16 бит
arr   = ratio / (psc+1) - 1    // максимальный ARR для наилучшего разрешения
```

Для `TIM_PWM` с явным ARR:

```
psc = bus_clk / ((arr+1) * freq) - 1
```

### Поддерживаемые таймеры (STM32G0)

| Периферия | Шина | irq_up | irq_cc | BDTR | AF |
|---|---|---|---|---|---|
| TIM1 | APB2 | TIM1_BRK_UP_TRG_COM | TIM1_CC | да | 2 |
| TIM3 | APB1 | TIM3 | TIM3 | нет | 1 |
| TIM14 | APB2 | TIM14 | TIM14 | нет | 4 |
| TIM16 | APB2 | TIM16 | TIM16 | да | 2 |
| TIM17 | APB2 | TIM17 | TIM17 | да | 2 |

`has_bdtr = true` означает расширенный/полурасширенный таймер, требующий установки `TIM_BDTR_MOE` для активации выходов.

---

## 10. RTC

**Файлы:** `rtc.hpp`, `rtc.cpp`

Полностью статический класс — объектов не создаётся. Все методы вызываются как `RTC_cl::SetUp(...)`.

### Инициализация

```cpp
// LSE — кварц 32.768 кГц (рекомендуется для точности)
RTC_cl::SetUp(RTC_cl::CLK_Source::LSE);

// LSI — внутренний RC ~32 кГц (без внешних компонентов, менее точный)
RTC_cl::SetUp(RTC_cl::CLK_Source::LSI);
```

По умолчанию: `prediv_a = 127`, `prediv_s = 255` → 32768 / (128 × 256) = 1 Гц.

Если `ICSR::RSF == 0` (RTC уже синхронизирован, например после выхода из Stop), инициализация пропускается — время не сбрасывается.

### Установка времени и даты

```cpp
RTC_cl::SetTime({14, 25, 00});                           // 14:25:00
RTC_cl::SetDate({22, 05, 26}, RTC_cl::WeekDay::Friday);  // 22.05.2026, Пт
```

### WakeUp Timer — периодические прерывания

```cpp
// Каждую секунду (counter=0, clock_div=0b100 = ck_spre=1Гц → период = counter+1 сек)
RTC_cl::EnableWakeUpTimer(0, 0b100, []{ read_sensors(); });

// Каждые 5 секунд
RTC_cl::EnableWakeUpTimer(4, 0b100, []{ ... });
```

### Alarm A / Alarm B

Срабатывание в конкретное время:

```cpp
RTC_cl::AlarmConfig cfg {
    .hour = {14, RTC_cl::Alarm_Masks::Care},  // матчить час 14
    .min  = {25, RTC_cl::Alarm_Masks::Care},  // матчить минуту 25
    .sec  = { 0, RTC_cl::Alarm_Masks::Care},  // матчить секунду 0
    // day — не указан → mask = Ignore → любой день
};
RTC_cl::EnableAlarm_A(ENABLE, cfg, []{ buzz(); });
```

Поля с `mask = Alarm_Masks::Ignore` (умолчание) не сравниваются.

Отключение будильника:

```cpp
RTC_cl::EnableAlarm_A(DISABLE);
```

### Чтение текущего времени

```cpp
// Прямое чтение регистров (внутри WUT-колбэка):
uint8_t hours   = ((RTC->TR & RTC_TR_HT)  >> RTC_TR_HT_Pos)  * 10
                 + (RTC->TR & RTC_TR_HU);
uint8_t minutes = ((RTC->TR & RTC_TR_MNT) >> RTC_TR_MNT_Pos) * 10
                 + ((RTC->TR & RTC_TR_MNU) >> RTC_TR_MNU_Pos);
uint8_t seconds = ((RTC->TR & RTC_TR_ST)  >> RTC_TR_ST_Pos)  * 10
                 + (RTC->TR & RTC_TR_SU);
```

### Субсекунды

`RTC->SSR` убывает от `prediv_s` (255) до 0 за одну секунду. Разрешение:
```
t_ss = 1 / (prediv_s + 1) = 1/256 ≈ 3.9 мс
```

### Калибровочный выход (512 Гц / 1 Гц)

```cpp
RTC_cl::EnableCOE(true);   // выводит сигнал на вывод RTC_CALIB
```

Полезно для измерения точности кристалла осциллографом. Ожидаемая частота при prediv_a=127, prediv_s=255: **512 Гц** (RTCCLK / 64).

### Защита от записи

Методы управляют защитой автоматически. Последовательность снятия: запись `0xCA`, затем `0x53` в `RTC->WPR`. Установка: запись любого неверного ключа (например `0xFF`).

---

## 11. Flash

**Файлы:** `flash.hpp`, `flash.cpp`

### flash_base — низкоуровневый доступ

```cpp
flash_base::init();
flash_base::enable_access();  // разблокировать запись (ключи KEYR)

// Чтение любого типа
uint32_t val = flash_base::read<uint32_t>(0x08010000);

// Запись
uint32_t data[] = {0x12345678, 0xDEADBEEF};
flash_base::write(0x08010000, data, 2);

// Стирание сектора
flash_base::erase_sector(3);
```

На **STM32G0** запись всегда 64-битная (две 32-битных операции подряд). На **F4** поддерживается 8/16/32/64-битная запись с автонастройкой `PSIZE`.

### flash_data — хранение структур во Flash

```cpp
uint8_t settings_buf[32];
flash_data settings(7,            // номер сектора
                    settings_buf, // буфер данных в RAM
                    sizeof(settings_buf));

settings.SetUp();

// Чтение последних сохранённых данных
auto status = settings.read_data();
if (status == flash_data::Data_Status::DATA_OK) {
    // settings_buf содержит актуальные данные
}

// Запись новых данных (append-журнал внутри сектора)
memcpy(settings_buf, &my_config, sizeof(my_config));
settings.write_data();
```

`flash_data` использует wear-leveling внутри одного сектора: каждая запись добавляется в конец, `find_offset()` находит последнюю валидную запись при инициализации.

---

## Порядок инициализации в main()

```cpp
int main() {
    System::Init();      // 1. FPU, Flash latency, SysTick

    ClockSystem::Init(   // 2. Тактирование
        ClockSystem::SystemClockSource::PLL, ...);

    // 3. Периферия — в любом порядке
    uart.SetUp();
    spi.SetUp(...);
    rtc.SetUp(...);

    // 4. DMA — после SetUp() соответствующей периферии
    uart.AttachDMA(&dma_tx, &dma_rx);
    spi.AttachDMA(&dma_spi_tx, nullptr);
    tim_pwm.AttachDMA(&dma_tim, TIM::TIM_Channel::CH1);

    // 5. Запуск таймеров, которые требуют явного Start()
    periodic_timer.Start();

    for (;;) { /* main loop */ }
}
```

---

## Зависимости между модулями

```
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

`flash.hpp` зависит только от `system.hpp` и `flash_config.h`.

---

## Добавление нового МК

1. Добавить `#include "stm32xxxx.h"` в `system.hpp`.
2. Добавить записи в `PeriphInfo`-таблицы в `uart.cpp`, `spi.cpp`, `tim.cpp`, `dma.cpp`.
3. Добавить стабы в `irq_registry_config.h`.
4. Добавить частотные лимиты в `rcc.hpp` (`SYS_CLK_LIMIT`, `APB1_CLK_LIMIT` и т.д.).
5. При необходимости добавить макросы регистровых абстракций (например `RCC_GPIO_EN_REG` в `gpio.hpp`).
