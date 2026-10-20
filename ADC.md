# ADC

## Текущий API

Обновлено 19.10.2026: режим преобразований, DMA и триггер закреплены конструктором.
Используется один Start; IRQ ADC и DMA разделены; HT/TC подключены к DMA API.
Сборка, тесты и проверка на плате этой версии не запускались. Старое сравнение
[ассемблера](Review/ADC_IRQ_Timing.md) относится к прежнему общему обработчику.

```cpp
ADC_N(ADC_TypeDef* ADCx, Conversion conversion,
      DMAReq dma = {}, TIM* timer = nullptr,
      Trigger source = Trigger::None);

SysStatus Start(uint16_t* buf = nullptr, uint32_t len = 0);
```

Модель: конструктор → SetUp → AddChannel → IRQ_en при необходимости → Start.

ADC содержит собственный DMA_Sx. На F4/F7 передаётся именованный DMAReq,
на G0 — только физический DMA-канал; источник ADC выбирается автоматически.
ADC_USE_ATOMIC в adc.hpp: 0 по умолчанию — обычные изменения управляющих
регистров, 1 — ATOMIC_* ST. Пересоберите драйвер и приложение после изменения.
DMA_USE_ATOMIC независимо выбирает способ изменения регистров самого DMA.
Эти настройки не заменяют согласование операций main и IRQ.

| Параметры конструктора | Поведение Start |
|---|---|
| Single без DMA/триггера | Одна последовательность, результат через DR/GetData |
| Single с DMA без триггера | Одна последовательность в буфер, конечный DMA |
| Continuous без DMA | Непрерывное повторение; DR хранит только последний результат |
| Continuous с DMA | Непрерывные последовательности, circular DMA |
| Single с триггером | Последовательность по каждому событию; с DMA — circular |
| Continuous с любым триггером | InitError в SetUp |

Если DMA передан, Start всегда использует его: требуются адрес буфера и длина.
Без DMA вызывается Start(); ненулевые аргументы буфера/длины дают Error.
AttachDMA/AttachTrig/AttachExtTrig, Start_DMA/StartCont/StartCont_DMA/StartTrig_DMA
удалены. Методов смены привязок нет. Копирование и перемещение запрещены.

SetUp выполняется один раз, повторно — после DeInit. После конечного захвата
достаточно снова вызвать Start без повторных SetUp/AddChannel. Активный захват
возвращает Busy при новом запуске/изменении каналов. Stop сохраняет настройки.
DeInit освобождает ресурсы, сохраняя параметры конструктора для следующего SetUp.

## Порядок работы и время жизни

System/RCC настраиваются заранее. SetUp, AddChannel/ClearChannels, Start,
Stop и DeInit вызываются последовательно из main. System waits требуют
работающего SysTick; аппаратные ожидания G0 ограничены 1 мс.
В IRQ аппаратных ожиданий нет; Stop/DeInit из main могут ждать остановку.

Приложение сериализует управление объектом и его ADC/DMA IRQ. Обработчики одного
объекта не должны прерывать друг друга. Внутренних блокировок, PRIMASK/IPSR
проверок, потоков или Process нет. IsBusy/GetStatus — чистые запросы.
Callbacks уведомляют приложение и не перенастраивают/перезапускают ADC или
IRQ Registry. Отключение периферийного источника через IRQ_en допустимо.

Объекты ADC/DMA/TIM и активный буфер живут до завершения/отмены захвата и снятия
регистраций. DMA приобретает/настраивает ADC в SetUp: половинные слова, Per→Mem,
MINC, адрес DR, circular по фиксированному режиму. Нельзя отдельно перенастраивать,
освобождать или передавать этот DMA другому потребителю до DeInit ADC.

## Каналы и sampling

Пины неизменны и проверяются consteval-конструктором. Примеры:
ADC_N::_1::PA0_IN0, ADC_N::_3::PF6_IN4; на G0 — ADC_N::IN::PA0_IN0.
Наличие вывода в корпусе и точный аппаратный маршрут проверяет приложение.

Порядок AddChannel определяет порядок результатов. F4/F7 допускают до 16
позиций с произвольным порядком/повторами. G0 использует до восьми позиций
входов 0..14; девятая позиция либо вход выше 14 переводит в битовую маску,
требующую строго возрастающих входов без повторов. Один физический вход
не может иметь разные sampling в разных позициях.

На F4/F7 используется PCLK2/4 и sampling каждого канала через CH_SMPL.
ADCPRE общий для ADC1/2/3 даже в независимом режиме: сначала настройте все
ADC, затем запускайте преобразования. Менеджер common/multimode не добавлен.
G0 использует PCLK/2: SetUp задаёт SMP1/SMP2, AddChannel выбирает один из них.

VREFINT/TEMP/VBAT добавляются через INTERNAL на ADC1. Sampling выбирается
по datasheet и частоте ADC: короткий default внешних входов может не подходить.
Delay_ms(1) при включении внутреннего источника обеспечивает запуск, а не
длительность выборки каждого отсчёта. Автоподбора sampling нет.
Где TEMP/VBAT используют один вход, одновременно добавлять их нельзя.

## IRQ и callbacks

SetUp отдельно регистрирует ADC и адаптер его DMA. Реестр IRQ сохранён;
ручная повторная регистрация не нужна. ADC IRQ читает только флаги ADC,
DMA IRQ — только своего ресурса. При ошибке настройки освобождаются только
приобретённые ресурсы/регистрации, сохраняются чужие shared handlers.

IRQ_en меняет только источники, сохраняя NVIC и регистрации:

| Источник | Действие |
|---|---|
| EOC/EOS | F4/F7: алиасы одного источника, только OnEOS; G0: отдельные события |
| OVR | Обработка overrun через OnError |
| DMA_HT | Изначально выключен, включается явно до Start, вызывает OnDmaHalf |
| DMA_TC | Изначально включён, отключение для конечного DMA игнорируется |

Изменение DMA_HT во время Busy игнорируется: формат половин задаётся до Start.
В circular DMA_TC управляет уведомлениями. TE и F4/F7 DME включаются при SetUp
и восстанавливаются при новом Start после ошибки. Runtime Enable_IRQ(..., false)
не снимает внешнюю маску NVIC.

```cpp
class MyADC : public ADC_N {
public:
    using ADC_N::ADC_N;
    volatile bool ready = false;
    volatile uint32_t errors = 0;
protected:
    void OnDmaComplete() override { ready = true; }
    void OnDmaHalf() override { /* первая половина готова до её перезаписи */ }
    void OnError(uint32_t causes) override { errors = causes; }
    void OnEOS() override { /* конец последовательности, если включён IRQ */ }
};
```

На G0 также есть OnEOC. Вызывать базовые callbacks для завершения операции
не требуется. Без DMA DR хранит только последний результат, а не scan-буфер.
GetData во время DMA запрещён контрактом: чтение DR может забрать его отсчёт.
Continuous без DMA допускает перезапись непрочитанного результата.

### Чтение без DMA

```cpp
ADC_N input(ADC1, ADC_N::Conversion::Single);
if (input.SetUp() == SysInitStatus::InitOK) {
#if defined(STM32G0)
    auto channel = input.AddChannel(ADC_N::IN::PA0_IN0);
#else
    auto channel = input.AddChannel(ADC_N::_1::PA0_IN0);
#endif
    if (channel == SysStatus::OK) {
        auto started = input.Start(); // Проверить OK.
        // Позже из main: если !input.IsBusy(), прочитать input.GetData().
    }
}
```

### Однократный DMA

```cpp
#if defined(STM32G0)
const ADC_N::DMAReq dma = DMA1_Channel1;
#else
const ADC_N::DMAReq dma = DMA_Sx::Req::Adc1::RX_DMA2_Stream0;
#endif
MyADC adc(ADC1, ADC_N::Conversion::Single, dma);
alignas(32) static uint16_t samples[16]; // Целая кеш-линия; используем 2 отсчёта.

if (adc.SetUp() == SysInitStatus::InitOK) {
#if defined(STM32G0)
    auto a = adc.AddChannel(ADC_N::IN::PA0_IN0);
    auto b = adc.AddChannel(ADC_N::IN::PB0_IN8);
#else
    auto a = adc.AddChannel(ADC_N::_1::PA0_IN0);
    auto b = adc.AddChannel(ADC_N::_1::PB0_IN8);
#endif
    if (a == SysStatus::OK && b == SysStatus::OK) {
        adc.ready = false;
        auto started = adc.Start(samples, 2); // Проверить OK перед ожиданием.
    }
}
// Main: после ready обработать samples[0..1], сбросить ready и снова Start(samples, 2).
// Ошибку обрабатывать через errors/GetStatus, а не ждать только ready.
```

Start проверяет адрес/выравнивание uint16_t, длину 1..65535, её кратность
числу каналов и подготовку DMA. Для Single/software len равна GetChannelCount().
При включённом HT требуется чётная длина, иначе Error. Конечный TC подтверждает
завершение DMA; затем обслуживается кеш, публикуется OK и OnDmaComplete.

### TIM и внешние события

TIM предварительно настраивает приложение: SetUpTrigger для TRGO либо
SetCompareIRQ(channel, compare, nullptr) для прямого CCx. ADC не задаёт
частоту TIM, compare, TRGO или EXTI. TIM обязан соответствовать выбранному Trigger:
точное соответствие проверяет вызывающий код. Timer-source без TIM,
ненастроенный TIM либо TIM вместе с None/EXTI11 дают InitError.

Пример ниже требует TIM3 на G0/F4 либо TIM2 на F7. Для F410, где TIM3 нет,
используйте, например, TIM1_CC1 и SetCompareIRQ(CH1, compare, nullptr).

```cpp
// Отдельный пример: не используйте один DMA одновременно с предыдущим adc.
#if defined(STM32G0)
const ADC_N::DMAReq timed_dma = DMA1_Channel1;
#else
const ADC_N::DMAReq timed_dma = DMA_Sx::Req::Adc1::RX_DMA2_Stream0;
#endif
#if defined(STM32F7)
TIM sampling(TIM2);
MyADC timed(ADC1, ADC_N::Conversion::Single, timed_dma, &sampling,
            ADC_N::Trigger::TIM2_TRGO);
#else
TIM sampling(TIM3);
MyADC timed(ADC1, ADC_N::Conversion::Single, timed_dma, &sampling,
            ADC_N::Trigger::TIM3_TRGO);
#endif
alignas(32) static uint16_t circular[32]; // Две половины по 32 байта.

if (sampling.SetUp(1000) == SysInitStatus::InitOK
    && sampling.SetUpTrigger() == SysInitStatus::InitOK
    && timed.SetUp() == SysInitStatus::InitOK) {
#if defined(STM32G0)
    auto channel = timed.AddChannel(ADC_N::IN::PA0_IN0);
#else
    auto channel = timed.AddChannel(ADC_N::_1::PA0_IN0);
#endif
    if (channel == SysStatus::OK) {
        timed.IRQ_en(ADC_N::IRQ::DMA_HT, ENABLE);
        auto started = timed.Start(circular, 32); // Проверить OK.
    }
}
```

Start сначала подготавливает DMA и вооружает ADC, затем запускает TIM.
Stop и ошибка останавливают этот TIM: приложение не перезапускает его независимо
во время захвата. EXTI11 использует timer=nullptr; приложение настраивает
маршрутизацию и события. Поддержан нарастающий фронт. Значения EXTSEL по
семействам находятся в enum Trigger. TRGO2 не добавлен: текущий общий TIM
не предоставляет его настройку; G0 TIM1 используется через CC4.

## HT/TC и кеш

Перед RX — System::CacheCleanInvalidate. При конечном TC возвращается весь
буфер. В circular с HT первая половина инвалидируется перед OnDmaHalf,
вторая — перед OnDmaComplete. При выключенном HT TC инвалидирует весь буфер.
GetIRQFlags читает HT, TC и ошибки за одно обращение к ISR; обработчик использует
DMA_Sx::FLAG_HT, FLAG_TC и FLAG_ERRORS. HT/TC очищаются раздельно через
ClearHT_Flag/ClearTC_Flag. Конечный захват
тоже допускает HT, но TC возвращает весь завершённый буфер.

HT/TC не приостанавливают circular DMA. Приложение выбирает частоту/sampling,
длину буфера и успевает обработать часть до перезаписи. Пропущенные обороты
не восстанавливаются; копирования/автоматической защиты от отставания нет.
При триггере также требуется достаточное время между преобразованиями.

На F7 приложение владеет всеми затронутыми 32-байтными линиями, включая padding.
При HT каждая половина кешируемого буфера занимает отдельные линии: начало
выровнено на 32 байта, размер половины кратен 32 байтам. Например,
alignas(32) uint16_t samples[32] — две половины по 32 байта.
Это обязанность вызывающего кода; драйвер буферы не выделяет/не копирует.
При конечном захвате без HT длина передачи не обязана быть кратна 32 байтам.

Память должна быть доступна DMA. System пропускает обслуживание диапазона,
целиком находящегося в проектной некешируемой DMA-области: для неё разделение
половин по кеш-линиям не требуется. На F4/G0 cache helpers пустые.

## Ошибки и освобождение

OnError получает маску Error::Overrun / Error::Dma. Overrun обрабатывается
при включённом OVR; Continuous без DMA допускает пропуск результатов.
При ошибке прекращаются преобразования/таймер и запрашивается остановка DMA:

- F4/F7: Busy сохраняется до DMA IRQ, подтвердившего остановку по TC либо
  аппаратному TE при EN=0. Затем Error и один OnError.
- G0: пакет отбрасывается, CNDTR не читается; Error/OnError публикуются после
  команды остановки. В IRQ нет аппаратных ожиданий.

Ошибочные данные не инвалидируются и не читаются приложением. Успешные
callbacks после обнаруженной ошибки не вызываются. Следующий Start из main
восстанавливает источники DMA и выполняет необходимые handshakes.

Stop/DeInit вызываются из main: останавливают TIM/ADC и подтверждают остановку DMA.
Stop не генерирует callbacks и сохраняет каналы/привязки; уже зафиксированная ошибка
сохраняется в его возвращаемом статусе. DeInit удаляет собственные ADC/DMA
регистрации, освобождает приобретённый DMA и отключает свой ADC. Чужие shared IRQ,
GPIO/DMA clocks и аналоговые GPIO остаются. TIM не уничтожается/не перенастраивается.
Конструкторные параметры сохраняются; автоматического освобождения в деструкторе нет.

## Миграция

| Раньше | Сейчас |
|---|---|
| ADC_N(ADCx), режим при запуске | ADC_N(ADCx, Conversion, dma, timer, Trigger) |
| AttachDMA/AttachTrig/AttachExtTrig | Конструктор; DMA настраивается в SetUp |
| StartCont/StartCont_DMA/StartTrig_DMA | Фиксированные параметры и один Start |
| Start_DMA(buf) | Start(buf, GetChannelCount()) |
| TIM_TriggerGenerator | Общий TIM* и ADC_N::Trigger |
| Общий ADC/DMA HandleIRQ | Два отдельных зарегистрированных обработчика |
| Неиспользуемый OnDmaHalf | IRQ_en(DMA_HT, ENABLE), уведомления половин |

Маршруты сверены по официальным ST LL ADC:
[F4](https://github.com/STMicroelectronics/stm32f4xx-hal-driver/blob/master/Inc/stm32f4xx_ll_adc.h),
[F7](https://github.com/STMicroelectronics/stm32f7xx-hal-driver/blob/master/Inc/stm32f7xx_ll_adc.h),
[G0](https://github.com/STMicroelectronics/stm32g0xx-hal-driver/blob/master/Inc/stm32g0xx_ll_adc.h).
