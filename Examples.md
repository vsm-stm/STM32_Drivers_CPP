# Examples — Capabilities and API map

*[English](#english) · [Русский](#русский)*

---

## English

## Table of Contents

1. [How to use the examples](#1-how-to-use-the-examples)
2. [Modules and public API](#2-modules-and-public-api)
3. [New complete scenarios](#3-new-complete-scenarios)
4. [Callbacks through inheritance](#4-callbacks-through-inheritance)
5. [Completion and ownership](#5-completion-and-ownership)
6. [Verification and limits](#6-verification-and-limits)

---

## 1. How to use the examples

Each scenario is an independent configuration. Include its optional modules in project.json, then initialize System and RCC before peripheral setup unless the example explicitly configures RCC. Check the selected model/package, named pin/DMA routes and every status. A constructor binds resources; hardware starts in SetUp, and TIM/ADC/SPI transfers follow their documented explicit start rules. Do not give one hardware instance or DMA resource to two active examples.

The index covers the exposed API, not every mode of the STM32 hardware. Low-level register gates are listed for completeness; they are not a substitute for a driver's owned transfer state machine. The actual declarations in src/*.hpp and module headers are authoritative.

## 2. Modules and public API

| Module | Scenarios | Public API / API families |
| --- | --- | --- |
| [System](System.md) | Startup, published clocks, polling without waits, timeouts, DWT, SWO and explicit cache handover | `Init`, `InitTicks`, `GetTick`, `TickIncrease`, `WaitBits`, `Delay_ms`, `Enable_CYCCNT`, `Delay_us`, `SWOTrace`, `MPU_Init`, `CacheClean`, `CacheInvalidate`, `CacheCleanInvalidate`, `DebugTrap` |
| [RCC](RCC.md) | Direct HSI/HSE, automatic/manual PLL, bus dividers and a shared I2C clock configuration | `ClockSystem::Config`, `CalcPLL`, `Init<config>`, `GetAPB1Clock<config>`, `GetHSEClock` |
| [GPIO / PinArray](GPIO.md) | Input/output/AF, initial HIGH/LOW, pulls, speed, F4 bit-banding, grouped buses | `PIN`, `SetUp overloads`, `PORT`, `GetNumber`, `IsValid`, `GetLevel`, `SetLevel`, `TogglePin`, `operator bool`, `operator=(bool)`, `*_BB; PinArray::SetUpAll`, `GetLevelAll`, `SetLevelAll`, `ToggleAll`, `operator[]`, `operator=` |
| [IRQ Registry](IRQ_Registry.md) | Object or function handlers, shared vectors, fixed capacity, selective teardown | `IIRQHandler`, `Register(object)`, `Register(function,ctx)`, `Unregister(vector/object/function)`, `HasHandlers`, `Dispatch` |
| [DMA](DMA.md) | Standalone G0 memory copy, application-owned IRQ, HT/TC/errors, peripheral-owned DMA and cache/stop order | `DMA_Sx`, `StreamSettings`, `SetUp`, `Release`, `SetMemAddr`, `SetPerAddr`, `SetCount`, `PrepareTransfer`, `GetCount`, `GetIRQn`, `GetIRQFlags`, `GetTC_Flag`, `GetHT_Flag`, `GetErrorFlags`, `ClearFlags`, `ClearHT_Flag`, `ClearTC_Flag`, `Enable_IRQ`, `Disable_IRQ`, `MINC`, `PINC`, `CIRC`, `RequestStop`, `IsEnabled`, `Stream_EN; ValidResource`, `RouteMatches`, `SameResource`, `CanUseRequest`, `HasSelection` |
| [USART](UART.md) | IRQ inheritance, IDLE/capacity packets, physical TC, format/FIFO/kernel clock, DMA and baud changes | `SetUp`, `Send/Receive`, `Send_IRQ/Receive_IRQ`, `Send_DMA/Receive_DMA`, `GetTxStatus`, `GetRxStatus`, `GetDataReceivedFlag`, `GetDataReceivedCount`, `ClearDataReceivedFlag`, `GetOverflow`, `GetOverflowCount`, `SetBaud`, `IRQ_en`, `DMA_en`, `ClearFlags`, `SetParity`, `DeInit; OnRxByte`, `OnTxEmpty`, `OnIdle`, `OnTC`, `OnDmaTxComplete`, `OnDmaRxComplete`, `OnError` |
| [SPI](SPI.md) | Byte/word and polling/IRQ/DMA, per-transfer CS, shared bus, slave NSS/EXTI, physical completion | `SetUp overloads`, `Send/Receive/Send_Receive`, `Send_IRQ/Receive_IRQ/SendReceive_IRQ`, `Send_DMA/Receive_DMA/SendReceive_DMA`, `Process`, `NssReleased`, `GetStatus`, `GetDataReceivedCount`, `IsTruncated`, `SlaveSelect`, `IRQ_en`, `DMA_TX`, `DMA_RX`, `Enable`, `Disable`, `DeInit; OnTxEmpty`, `OnRxByte`, `OnRxWord`, `OnDmaTxComplete`, `OnDmaRxComplete`, `OnComplete`, `OnError` |
| [TIM](TIM.md) | Periodic IRQ, pin-free compare, PWM/capture, TRGO/ADC, DMA/WS2812B, encoder/quadrature, burst/counter/slave/PWM input | `SetUp`, `SetUpPWM`, `SetDuty`, `SetCCR`, `SetUpCapture`, `GetCapture`, `SetCompareIRQ`, `SetUpTrigger`, `SetUpEncoder`, `SetUpQuadrature`, `GenPulse`, `SetUpPulseBurst`, `RunPulses`, `SetUpCounter`, `StopAfter`, `StopAt`, `SetUpPulseMeasure`, `SetUpSlave`, `SetUpOnePulse`, `SendDMA overloads`, `GetDMAStatus`, `IsDMABusy`, `IsAnyDMABusy`, `IRQ_en`, `Start`, `Stop`, `SetEnable`, `Reset`, `Count`, `GetTIMx`, `GetPeriod`, `GetCounterClock`, `DeInit` |
| [ADC](ADC.md) | One-shot/continuous, software/TIM/EXTI trigger, external/internal channels, IRQ, finite/circular DMA with HT/TC | `Conversion`, `SetUp`, `AddChannel(pin/internal)`, `ClearChannels`, `GetChannelCount`, `Start`, `Stop`, `IsBusy`, `GetStatus`, `GetData`, `IsEOC`, `IRQ_en`, `DeInit; OnEOC(G0)`, `OnEOS`, `OnDmaHalf`, `OnDmaComplete`, `OnError` |
| [DAC](DAC.md) | Independent CH1/CH2, raw 12-bit code, invalid-code handling; no DMA/IRQ API | `SetUp`, `SetValue`, `DeInit` |
| [I2C](I2C.md) | Probe, 7-bit address, 8/16-bit register address, repeated START, DMA, inherited notifications and main-side abort | `SetUp<clocks,speed>`, `Send/Receive/WriteReg/ReadReg (blocking`, `_IRQ`, `_DMA)`, `IsReady`, `IsBusy`, `GetError`, `Abort`, `DeInit`, `OnComplete`, `OnError` |
| [EXTI](EXTI.md) | Physical GPIO edges, input pulls, source masking, shared-vector capacity; no debounce/event-mode API | `EXTI_Line`, `Edge::Rising/Falling/Both`, `SetUp`, `IRQ_en`, `GetIRQn`, `DeInit`, `OnInterrupt` |
| [RTC](RTC.md) | Retained calendar, decimal/BCD, alarms, wakeup, timestamp polling/IRQ, calibration and clock output | `SetUp`, `SetTime`, `SetDate`, `SetTime_hex`, `SetDate_hex`, `GetDateTime`, `GetTime`, `GetDate`, `EnableWakeUpTimer`, `DisableWakeUpTimer`, `EnableAlarm_A/B`, `EnableTimestamp`, `GetTimestamp`, `IRQ_en`, `SetCalibration`, `EnableCOE`, `WriteProtection`, `to_bcd`, `DeInit; OnWakeup`, `OnAlarmA/B`, `OnTimestamp` |
| [Flash](Flash.md) | Geometry/banks, raw programming, reserved-sector journal, explicit load/save, incomplete-record recovery | `GetFlashSector`, `FLASH_SECTOR_COUNT`, `flash_base::init`, `enable_access`, `ready`, `erase_sector`, `mass_erase`, `clear_errors`, `read<T>`, `write<T>; flash_data::SetUp`, `read_data`, `write_data` |
| [Modbus transport](Modbus.md) | RS-485 DE, DMA, main parser, CRC validation/appending, application address/function/broadcast policy | `SetRxParse`, `SetUp`, `Rx_Process`, `GetTxBufferPtr`, `CancelTxBuffer`, `SendData`, `DeInit` |
| [Filters](Filters.md) | First-sample/zero startup, independent signals, float calculation, median→EMA and reset | `MovingAverage<N,Sample,Sum>`, `ExponentialMovingAverage<Sample,Value>`, `Median3<Sample>`, `add_new_sample`, `sum/value`, `constructor startup mode` |
| [Containers](Utilities.md) | Queue/map/list operations, full storage, borrowed pointers and deleting during traversal | `RingQueue`, `StaticMap`, `StaticSList`, `StaticDList` |
| [PID](Utilities.md) | Fixed dt, direct constructor arguments, private calculation state and P/I/D diagnostics | `PID`, `u`, `Reset`, `p_term`, `i_term`, `d_term` |
| [CRC32](Utilities.md) | One-shot and chunked IEEE CRC32; distinct from Modbus CRC16 | `CRC32_INIT`, `crc32_update`, `crc32_finish`, `crc32_compute` |
| [CMSIS-DSP](Utilities.md) | Built FastMath/Controller groups, float/fixed-point domains and excluded algorithms | `arm_sin_*`, `arm_cos_*`, `arm_sqrt_*`, `arm_atan2_*`, `arm_divide_*`, `arm_vlog_*`, `arm_vexp_*`, `arm_sin_cos_*`, `arm_pid_*`, `arm_clarke_*`, `arm_inv_clarke_*`, `arm_park_*`, `arm_inv_park_*` |
| [WS2812B / RGB](Utilities.md) | Pixel/range encoding and brightness; TIM/DMA transfer and physical reset limits | `WS2812B_Strip`, `SetPixel`, `SetAll`, `Fill`, `Clear`, `Rainbow`, `SetCCR`, `Data`, `Len`, `Size; RGB`, `dim`, `presets`, `==/!=`, `FromHSV` |

## 3. New complete scenarios

| Scenario | Document |
| --- | --- |
| Adding application work through inheritance | [UART.md](UART.md#adding-application-work-through-inheritance) |
| Consuming a packet, changing baud and polling | [UART.md](UART.md#consuming-a-packet-changing-baud-and-polling) |
| Several channel interrupts without GPIO pins | [TIM.md](TIM.md#several-channel-interrupts-without-gpio-pins) |
| Capture callback and polling the same hardware register | [TIM.md](TIM.md#capture-callback-and-polling-the-same-hardware-register) |
| One period and hardware-counted pulse burst | [TIM.md](TIM.md#one-period-and-hardware-counted-pulse-burst) |
| Inherited conversion callbacks without DMA | [ADC.md](ADC.md#inherited-conversion-callbacks-without-dma) |
| Circular DMA: two independent cache-line halves | [ADC.md](ADC.md#circular-dma-two-independent-cache-line-halves) |
| Inherited callbacks, IRQ/DMA completion and per-transfer CS | [SPI.md](SPI.md#inherited-callbacks-irqdma-completion-and-per-transfer-cs) |
| A complete inherited IRQ register transaction | [I2C.md](I2C.md#a-complete-inherited-irq-register-transaction) |
| Inherited edge notification and source masking | [EXTI.md](EXTI.md#inherited-edge-notification-and-source-masking) |
| Inherited WakeUp, alarm and timestamp notifications | [RTC.md](RTC.md#inherited-wakeup-alarm-and-timestamp-notifications) |
| Input buses, initial HIGH and read-only pin queries | [GPIO.md](GPIO.md#input-buses-initial-high-and-read-only-pin-queries) |
| Standalone G0 memory copy with a registered IRQ handler | [DMA.md](DMA.md#standalone-g0-memory-copy-with-a-registered-irq-handler) |
| Explicit journal load/save and raw programming | [Flash.md](Flash.md#explicit-journal-loadsave-and-raw-programming) |
| Function registration with context and selective removal | [IRQ_Registry.md](IRQ_Registry.md#function-registration-with-context-and-selective-removal) |
| Two independent outputs and rejected values | [DAC.md](DAC.md#two-independent-outputs-and-rejected-values) |
| Direct HSI, automatic PLL and passing clocks to consumers | [RCC.md](RCC.md#direct-hsi-automatic-pll-and-passing-clocks-to-consumers) |
| Nonblocking main-loop timing and explicit DMA cache handover | [System.md](System.md#nonblocking-main-loop-timing-and-explicit-dma-cache-handover) |
| A complete application parser and reply-buffer cancellation | [Modbus.md](Modbus.md#a-complete-application-parser-and-reply-buffer-cancellation) |
| Independent channels, float inputs and restarting history | [Filters.md](Filters.md#independent-channels-float-inputs-and-restarting-history) |
| Queue and unique-key map | [Utilities.md](Utilities.md#queue-and-unique-key-map) |
| Lists, stable indices and deletion during traversal | [Utilities.md](Utilities.md#lists-stable-indices-and-deletion-during-traversal) |
| Fixed-step PID, reset and term diagnostics | [Utilities.md](Utilities.md#fixed-step-pid-reset-and-term-diagnostics) |
| CRC32 of one buffer and a stream of chunks | [Utilities.md](Utilities.md#crc32-of-one-buffer-and-a-stream-of-chunks) |
| Built CMSIS-DSP groups: math and controllers | [Utilities.md](Utilities.md#built-cmsis-dsp-groups-math-and-controllers) |
| WS2812B pixels, ranges, brightness and DMA data | [Utilities.md](Utilities.md#ws2812b-pixels-ranges-brightness-and-dma-data) |

## 4. Callbacks through inheritance

| Driver | Override points | Base call / calling context |
| --- | --- | --- |
| USART | OnRxByte, OnTxEmpty | Call base to retain byte handling; USART IRQ, not per DMA byte. |
| USART | OnIdle, OnDmaTxComplete, OnDmaRxComplete, OnTC, OnError | Notifications after state handling; no base call. IRQ, except a blocking Receive error runs in its caller. DMA IDLE on F4/F7 completes from DMA IRQ. |
| SPI | OnTxEmpty, OnRxByte, OnRxWord, OnDmaTxComplete, OnDmaRxComplete | Call base for frame handling and slave DMA continuation; may run in IRQ or polling/drain context. |
| SPI | OnComplete, OnError | Notifications from Process in main, not at raw DMA TC. |
| I2C | OnComplete, OnError | Notifications from I2C/DMA IRQ; blocking calls do not notify. |
| ADC | OnEOC (G0), OnEOS, OnDmaHalf, OnDmaComplete, OnError | Notifications from ADC/DMA IRQ; do not read DR when DMA owns it. |
| EXTI_Line / RTC_cl | OnInterrupt / OnWakeup, OnAlarmA, OnAlarmB, OnTimestamp | Notifications after acknowledgement; no base call. |
| TIM | SetUp/SetCompareIRQ/SetUpCapture/SetUpPWM and whole-timer setup callbacks | void(*)() callbacks, not virtual HandleIRQ overrides. Use a free/static function or noncapturing lambda; no context argument. |

Never replace or manually call a driver's final HandleIRQ. Setup/teardown and IRQ_Registry modifications belong in main. Keep IRQ work bounded; move packet parsing, logging and blocking calls to main. volatile is not an atomic event queue: counters/flags still require an application ownership and consumption protocol.

## 5. Completion and ownership

| Operation | What completion means |
| --- | --- |
| USART IRQ/DMA TX | Source buffer reusable when TX leaves Busy; the last stop bit is a separate TC event. |
| USART RX | Packet ready/count are published after RX stop and successful buffer handover; consume before rearm. |
| SPI IRQ/DMA | Call Process until not Busy; only then consume RX/reuse buffers and begin the next packet. |
| I2C IRQ/DMA | OnComplete follows STOP/state/cache handling; OK at start is not completion. |
| ADC circular DMA | HT/TC hand back a half/window only until the writer returns; service it before overwrite. |
| TIM PWM DMA | TC releases source memory; the final CCR preload/PWM period may still be pending. |
| Flash Timeout | Does not cancel hardware; check readiness and follow recovery rules before further access. |

## 6. Verification and limits

The 26 newly added complete scenarios were syntax-checked as C++20 using arm-none-eabi-g++ for STM32F446xx, STM32F722xx and STM32G071xx. The standalone Mem_To_Mem example targets G0 only; the two-channel DAC example targets the F446/G071 devices with DAC, not F722. CMSIS-DSP declarations were checked with the exact v1.16.2 headers. This checks API names, overloads, inheritance and constant construction. It does not prove physical pin bonding, ITR wiring, oscillator frequency, protocol timing or behavior on hardware, and is not a full firmware link. Existing shorter fragments can intentionally depend on the enclosing section's declarations.

---

## Русский

## Содержание

1. [Как использовать примеры](#1-как-использовать-примеры)
2. [Модули и публичный API](#2-модули-и-публичный-api)
3. [Новые полные сценарии](#3-новые-полные-сценарии)
4. [Callbacks через наследование](#4-callbacks-через-наследование)
5. [Завершение и владение](#5-завершение-и-владение)
6. [Проверка и ограничения](#6-проверка-и-ограничения)

---

## 1. Как использовать примеры

Каждый сценарий — независимая конфигурация. Включите нужные модули в project.json, затем настройте System и RCC, если пример явно не настраивает RCC сам. Проверяйте модель/корпус, именованные пины/DMA-маршруты и каждый статус. Конструктор фиксирует ресурсы, аппаратура настраивается в SetUp; запуск TIM/ADC/SPI выполняется по правилам соответствующего драйвера. Один hardware instance или DMA-ресурс нельзя отдавать двум активным примерам.

Указатель охватывает открытый API, а не все аппаратные режимы STM32. Низкоуровневые биты включения перечислены для полноты, но не заменяют штатную машину состояний драйвера. Актуальные объявления находятся в src/*.hpp и заголовках модулей.

## 2. Модули и публичный API

| Модуль | Сценарии | Публичный API / группы API |
| --- | --- | --- |
| [System](System.md) | Старт, опубликованные частоты, polling без ожидания, таймауты, DWT, SWO и явная передача кеша | `Init`, `InitTicks`, `GetTick`, `TickIncrease`, `WaitBits`, `Delay_ms`, `Enable_CYCCNT`, `Delay_us`, `SWOTrace`, `MPU_Init`, `CacheClean`, `CacheInvalidate`, `CacheCleanInvalidate`, `DebugTrap` |
| [RCC](RCC.md) | Прямой HSI/HSE, автоматический/ручной PLL, делители шин и общая конфигурация частоты I2C | `ClockSystem::Config`, `CalcPLL`, `Init<config>`, `GetAPB1Clock<config>`, `GetHSEClock` |
| [GPIO / PinArray](GPIO.md) | Вход/выход/AF, начальные HIGH/LOW, подтяжки, скорость, bit-banding F4, группы пинов | `PIN`, `SetUp overloads`, `PORT`, `GetNumber`, `IsValid`, `GetLevel`, `SetLevel`, `TogglePin`, `operator bool`, `operator=(bool)`, `*_BB; PinArray::SetUpAll`, `GetLevelAll`, `SetLevelAll`, `ToggleAll`, `operator[]`, `operator=` |
| [IRQ Registry](IRQ_Registry.md) | Объектные и функциональные обработчики, общие векторы, фиксированная вместимость, выборочное освобождение | `IIRQHandler`, `Register(object)`, `Register(function,ctx)`, `Unregister(vector/object/function)`, `HasHandlers`, `Dispatch` |
| [DMA](DMA.md) | Самостоятельное копирование G0, собственный IRQ, HT/TC/ошибки, DMA драйвера периферии и порядок кеша/остановки | `DMA_Sx`, `StreamSettings`, `SetUp`, `Release`, `SetMemAddr`, `SetPerAddr`, `SetCount`, `PrepareTransfer`, `GetCount`, `GetIRQn`, `GetIRQFlags`, `GetTC_Flag`, `GetHT_Flag`, `GetErrorFlags`, `ClearFlags`, `ClearHT_Flag`, `ClearTC_Flag`, `Enable_IRQ`, `Disable_IRQ`, `MINC`, `PINC`, `CIRC`, `RequestStop`, `IsEnabled`, `Stream_EN; ValidResource`, `RouteMatches`, `SameResource`, `CanUseRequest`, `HasSelection` |
| [USART](UART.md) | Наследование IRQ, пакеты IDLE/вместимость, физический TC, формат/FIFO/kernel clock, DMA и смена baudrate | `SetUp`, `Send/Receive`, `Send_IRQ/Receive_IRQ`, `Send_DMA/Receive_DMA`, `GetTxStatus`, `GetRxStatus`, `GetDataReceivedFlag`, `GetDataReceivedCount`, `ClearDataReceivedFlag`, `GetOverflow`, `GetOverflowCount`, `SetBaud`, `IRQ_en`, `DMA_en`, `ClearFlags`, `SetParity`, `DeInit; OnRxByte`, `OnTxEmpty`, `OnIdle`, `OnTC`, `OnDmaTxComplete`, `OnDmaRxComplete`, `OnError` |
| [SPI](SPI.md) | Байт/слово и polling/IRQ/DMA, CS на транзакцию, общая шина, slave NSS/EXTI, физическое завершение | `SetUp overloads`, `Send/Receive/Send_Receive`, `Send_IRQ/Receive_IRQ/SendReceive_IRQ`, `Send_DMA/Receive_DMA/SendReceive_DMA`, `Process`, `NssReleased`, `GetStatus`, `GetDataReceivedCount`, `IsTruncated`, `SlaveSelect`, `IRQ_en`, `DMA_TX`, `DMA_RX`, `Enable`, `Disable`, `DeInit; OnTxEmpty`, `OnRxByte`, `OnRxWord`, `OnDmaTxComplete`, `OnDmaRxComplete`, `OnComplete`, `OnError` |
| [TIM](TIM.md) | Периодический IRQ, compare без пинов, PWM/capture, TRGO/ADC, DMA/WS2812B, encoder/quadrature, burst/counter/slave/PWM input | `SetUp`, `SetUpPWM`, `SetDuty`, `SetCCR`, `SetUpCapture`, `GetCapture`, `SetCompareIRQ`, `SetUpTrigger`, `SetUpEncoder`, `SetUpQuadrature`, `GenPulse`, `SetUpPulseBurst`, `RunPulses`, `SetUpCounter`, `StopAfter`, `StopAt`, `SetUpPulseMeasure`, `SetUpSlave`, `SetUpOnePulse`, `SendDMA overloads`, `GetDMAStatus`, `IsDMABusy`, `IsAnyDMABusy`, `IRQ_en`, `Start`, `Stop`, `SetEnable`, `Reset`, `Count`, `GetTIMx`, `GetPeriod`, `GetCounterClock`, `DeInit` |
| [ADC](ADC.md) | Однократный/непрерывный режим, software/TIM/EXTI trigger, внешние/внутренние каналы, IRQ, конечный/кольцевой DMA с HT/TC | `Conversion`, `SetUp`, `AddChannel(pin/internal)`, `ClearChannels`, `GetChannelCount`, `Start`, `Stop`, `IsBusy`, `GetStatus`, `GetData`, `IsEOC`, `IRQ_en`, `DeInit; OnEOC(G0)`, `OnEOS`, `OnDmaHalf`, `OnDmaComplete`, `OnError` |
| [DAC](DAC.md) | Независимые CH1/CH2, 12-битный код, отклонение неверного значения; API DMA/IRQ нет | `SetUp`, `SetValue`, `DeInit` |
| [I2C](I2C.md) | Probe, 7-битный адрес, регистр 8/16 бит, repeated START, DMA, наследуемые уведомления и Abort из main | `SetUp<clocks,speed>`, `Send/Receive/WriteReg/ReadReg (blocking`, `_IRQ`, `_DMA)`, `IsReady`, `IsBusy`, `GetError`, `Abort`, `DeInit`, `OnComplete`, `OnError` |
| [EXTI](EXTI.md) | Фронты GPIO, подтяжки входа, маскирование источника, вместимость общего вектора; API дребезга/event-режима нет | `EXTI_Line`, `Edge::Rising/Falling/Both`, `SetUp`, `IRQ_en`, `GetIRQn`, `DeInit`, `OnInterrupt` |
| [RTC](RTC.md) | Сохранённый календарь, десятичные/BCD данные, будильники, wakeup, timestamp polling/IRQ, калибровка и выход частоты | `SetUp`, `SetTime`, `SetDate`, `SetTime_hex`, `SetDate_hex`, `GetDateTime`, `GetTime`, `GetDate`, `EnableWakeUpTimer`, `DisableWakeUpTimer`, `EnableAlarm_A/B`, `EnableTimestamp`, `GetTimestamp`, `IRQ_en`, `SetCalibration`, `EnableCOE`, `WriteProtection`, `to_bcd`, `DeInit; OnWakeup`, `OnAlarmA/B`, `OnTimestamp` |
| [Flash](Flash.md) | Геометрия/банки, raw-программирование, журнал в выделенном секторе, явные load/save, неполная запись | `GetFlashSector`, `FLASH_SECTOR_COUNT`, `flash_base::init`, `enable_access`, `ready`, `erase_sector`, `mass_erase`, `clear_errors`, `read<T>`, `write<T>; flash_data::SetUp`, `read_data`, `write_data` |
| [Modbus transport](Modbus.md) | RS-485 DE, DMA, парсер main, проверка/добавление CRC, правила адреса/функции/broadcast приложения | `SetRxParse`, `SetUp`, `Rx_Process`, `GetTxBufferPtr`, `CancelTxBuffer`, `SendData`, `DeInit` |
| [Filters](Filters.md) | Старт с первого отсчёта/нуля, независимые сигналы, расчёт float, median→EMA и сброс | `MovingAverage<N,Sample,Sum>`, `ExponentialMovingAverage<Sample,Value>`, `Median3<Sample>`, `add_new_sample`, `sum/value`, `constructor startup mode` |
| [Containers](Utilities.md) | Очередь/таблица/списки, заполнение, выданные указатели и удаление при обходе | `RingQueue`, `StaticMap`, `StaticSList`, `StaticDList` |
| [PID](Utilities.md) | Постоянный dt, прямые аргументы конструктора, приватное состояние расчёта и диагностика P/I/D | `PID`, `u`, `Reset`, `p_term`, `i_term`, `d_term` |
| [CRC32](Utilities.md) | Одноразовый и потоковый IEEE CRC32; отдельно от Modbus CRC16 | `CRC32_INIT`, `crc32_update`, `crc32_finish`, `crc32_compute` |
| [CMSIS-DSP](Utilities.md) | Собираемые FastMath/Controller, float/fixed-point диапазоны и исключённые алгоритмы | `arm_sin_*`, `arm_cos_*`, `arm_sqrt_*`, `arm_atan2_*`, `arm_divide_*`, `arm_vlog_*`, `arm_vexp_*`, `arm_sin_cos_*`, `arm_pid_*`, `arm_clarke_*`, `arm_inv_clarke_*`, `arm_park_*`, `arm_inv_park_*` |
| [WS2812B / RGB](Utilities.md) | Кодирование пикселей/диапазонов и яркость; TIM/DMA и ограничения физического reset | `WS2812B_Strip`, `SetPixel`, `SetAll`, `Fill`, `Clear`, `Rainbow`, `SetCCR`, `Data`, `Len`, `Size; RGB`, `dim`, `presets`, `==/!=`, `FromHSV` |

## 3. Новые полные сценарии

| Сценарий | Документ |
| --- | --- |
| Дополнительные действия в IRQ через наследование | [UART.md](UART.md#дополнительные-действия-в-irq-через-наследование) |
| Разбор пакета, смена baudrate и блокирующий обмен | [UART.md](UART.md#разбор-пакета-смена-baudrate-и-блокирующий-обмен) |
| Несколько прерываний по каналам без GPIO-пинов | [TIM.md](TIM.md#несколько-прерываний-по-каналам-без-gpio-пинов) |
| Capture через callback и чтение аппаратного регистра | [TIM.md](TIM.md#capture-через-callback-и-чтение-аппаратного-регистра) |
| Один период и аппаратная серия импульсов | [TIM.md](TIM.md#один-период-и-аппаратная-серия-импульсов) |
| Callbacks преобразования через наследование без DMA | [ADC.md](ADC.md#callbacks-преобразования-через-наследование-без-dma) |
| Кольцевой DMA: две половины по отдельным cache lines | [ADC.md](ADC.md#кольцевой-dma-две-половины-по-отдельным-cache-lines) |
| Наследование, завершение IRQ/DMA и CS на одну транзакцию | [SPI.md](SPI.md#наследование-завершение-irqdma-и-cs-на-одну-транзакцию) |
| Полная транзакция с регистром через IRQ и наследование | [I2C.md](I2C.md#полная-транзакция-с-регистром-через-irq-и-наследование) |
| Наследование и маскирование источника | [EXTI.md](EXTI.md#наследование-и-маскирование-источника) |
| WakeUp, будильники и timestamp через наследование | [RTC.md](RTC.md#wakeup-будильники-и-timestamp-через-наследование) |
| Группа входов, начальный HIGH и запросы о пине | [GPIO.md](GPIO.md#группа-входов-начальный-high-и-запросы-о-пине) |
| Самостоятельное копирование на G0 с обработчиком IRQ | [DMA.md](DMA.md#самостоятельное-копирование-на-g0-с-обработчиком-irq) |
| Явная загрузка/запись журнала и низкоуровневое программирование | [Flash.md](Flash.md#явная-загрузказапись-журнала-и-низкоуровневое-программирование) |
| Регистрация функции с контекстом и выборочное удаление | [IRQ_Registry.md](IRQ_Registry.md#регистрация-функции-с-контекстом-и-выборочное-удаление) |
| Два независимых выхода и отклонение неверного значения | [DAC.md](DAC.md#два-независимых-выхода-и-отклонение-неверного-значения) |
| Прямой HSI, автоматический PLL и передача конфигурации потребителям | [RCC.md](RCC.md#прямой-hsi-автоматический-pll-и-передача-конфигурации-потребителям) |
| Время в main без блокировки и явная передача кешируемого DMA-буфера | [System.md](System.md#время-в-main-без-блокировки-и-явная-передача-кешируемого-dma-буфера) |
| Полный парсер приложения и отмена подготовленного ответа | [Modbus.md](Modbus.md#полный-парсер-приложения-и-отмена-подготовленного-ответа) |
| Независимые каналы, вход float и повторный запуск истории | [Filters.md](Filters.md#независимые-каналы-вход-float-и-повторный-запуск-истории) |
| Очередь и таблица уникальных ключей | [Utilities.md](Utilities.md#очередь-и-таблица-уникальных-ключей) |
| Списки, стабильные индексы и удаление при обходе | [Utilities.md](Utilities.md#списки-стабильные-индексы-и-удаление-при-обходе) |
| PID с постоянным шагом, сброс и чтение составляющих | [Utilities.md](Utilities.md#pid-с-постоянным-шагом-сброс-и-чтение-составляющих) |
| CRC32 одного буфера и потока фрагментов | [Utilities.md](Utilities.md#crc32-одного-буфера-и-потока-фрагментов) |
| Собираемые группы CMSIS-DSP: математика и регуляторы | [Utilities.md](Utilities.md#собираемые-группы-cmsis-dsp-математика-и-регуляторы) |
| WS2812B: пиксели, диапазоны, яркость и данные для DMA | [Utilities.md](Utilities.md#ws2812b-пиксели-диапазоны-яркость-и-данные-для-dma) |

## 4. Callbacks через наследование

| Драйвер | Точки расширения | Вызов базы / контекст |
| --- | --- | --- |
| USART | OnRxByte, OnTxEmpty | Вызов базы сохраняет обработку байтов; IRQ USART, не каждый DMA-байт. |
| USART | OnIdle, OnDmaTxComplete, OnDmaRxComplete, OnTC, OnError | Уведомления после обработки состояния; база не нужна. IRQ, кроме ошибки блокирующего Receive в вызывающем контексте. DMA IDLE F4/F7 завершается в DMA IRQ. |
| SPI | OnTxEmpty, OnRxByte, OnRxWord, OnDmaTxComplete, OnDmaRxComplete | База нужна для кадров и продолжения DMA slave; контекст IRQ либо polling/дочитывание. |
| SPI | OnComplete, OnError | Уведомления из Process в main, не по raw DMA TC. |
| I2C | OnComplete, OnError | Уведомления в I2C/DMA IRQ; блокирующие методы их не вызывают. |
| ADC | OnEOC (G0), OnEOS, OnDmaHalf, OnDmaComplete, OnError | Уведомления ADC/DMA IRQ; DR не читается при владении DMA. |
| EXTI_Line / RTC_cl | OnInterrupt / OnWakeup, OnAlarmA, OnAlarmB, OnTimestamp | Уведомления после подтверждения; вызов базы не нужен. |
| TIM | Callbacks SetUp/SetCompareIRQ/SetUpCapture/SetUpPWM и whole-timer setup | Callbacks void(*)(), не переопределение virtual HandleIRQ. Подходят свободная/static-функция и lambda без захвата; аргумента context нет. |

Не заменяйте и не вызывайте вручную final HandleIRQ драйвера. Setup/teardown и изменения IRQ_Registry выполняются в main. Работа IRQ должна быть ограниченной; разбор пакетов, логирование и блокирующие вызовы переносятся в main. volatile не превращает флаги/счётчики в атомарную очередь: приложение задаёт правила владения и чтения событий.

## 5. Завершение и владение

| Операция | Что означает завершение |
| --- | --- |
| USART IRQ/DMA TX | Выход TX из Busy разрешает переиспользовать источник; последний стоп-бит — отдельный TC. |
| USART RX | Готовность/длина публикуются после остановки RX и успешного возврата буфера; разбор до повторного приёма. |
| SPI IRQ/DMA | Вызывайте Process до выхода из Busy, затем используйте RX/буферы и начинайте следующий пакет. |
| I2C IRQ/DMA | OnComplete следует после STOP/состояния/кеша; OK при старте не означает окончание. |
| ADC circular DMA | HT/TC возвращают половину/окно до возврата пишущего DMA; обработайте до перезаписи. |
| TIM PWM DMA | TC освобождает источник, но последний CCR preload/PWM-период может ещё ожидать выполнения. |
| Flash Timeout | Не отменяет аппаратную операцию; сначала готовность и восстановление по правилам модуля. |

## 6. Проверка и ограничения

26 новых полных сценариев проверены как C++20 компилятором arm-none-eabi-g++ для STM32F446xx, STM32F722xx и STM32G071xx. Самостоятельный Mem_To_Mem предназначен только G0; пример двух DAC — F446/G071 с DAC, не F722. Объявления CMSIS-DSP проверены с заголовками именно v1.16.2. Это проверяет имена API, перегрузки, наследование и постоянную инициализацию. Корпусные пины, ITR-маршруты, частота генератора, протокольные интервалы и работа на железе этим не подтверждаются; полной линковки прошивки нет. Старые короткие фрагменты могут намеренно использовать объявления из своего раздела.

---
