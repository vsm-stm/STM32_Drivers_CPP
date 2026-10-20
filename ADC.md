# ADC / ADC_N — Detailed Description

*[English](#english) · [Русский](#русский)*

---

## English

## Table of Contents

1. [Why This Is Needed](#1-why-this-is-needed)
2. [Constant Initialization and Constructor Diagnostics](#2-constant-initialization-and-constructor-diagnostics)
3. [API and Conversion Modes](#3-api-and-conversion-modes)
4. [Setup Order and Lifetime](#4-setup-order-and-lifetime)
5. [Channels and Sampling](#5-channels-and-sampling)
6. [IRQs and Inherited Callbacks](#6-irqs-and-inherited-callbacks)
7. [Timer and External Trigger Binding](#7-timer-and-external-trigger-binding)
8. [HT/TC and Cache Ownership](#8-httc-and-cache-ownership)
9. [Errors, Stop and DeInit](#9-errors-stop-and-deinit)
10. [Migration](#10-migration)
11. [Usage Examples](#11-usage-examples)
12. [Edge Cases](#12-edge-cases)
13. [See Also](#see-also)

---

## 1. Why This Is Needed

ADC_N fixes the conversion policy, optional DMA and optional TIM trigger. SetUp and AddChannel prepare a sequence; one Start method runs the constructor-selected mode. ADC and DMA have separate registered handlers.

---

## 2. Constant Initialization and Constructor Diagnostics

Declare fixed peripheral objects with `constinit` at file scope or as `static constinit`. Use the provided _N::instance, named pins and optional _N::DMA routes. G0 takes numeric DMAx_Channely_BASE selections; the peripheral fixes the request source.

For peripheral binding checks, each failing condition uses `STM32_CONFIG_ERROR("reason")`. The reason is a template argument of the non-constexpr error function, so GCC includes it in the diagnostic itself. The macro uses `std::is_constant_evaluated()` to select that path; at runtime it calls `System::DebugTrap(text)` directly, without calling the template. A `constinit` initializer rejects that call during compilation; without constinit, a failing runtime construction stops. The keyword is mandatory by the usage contract, not enforced by the constructor. SetUp still checks hardware state and occupied resources. See [Initialization.md](Initialization.md) for constructor variants and migration.

---

## 3. API and Conversion Modes

```cpp
ADC_N(const Instance& instance, Conversion conversion,
      DMAReq dma = {}, TIM* timer = nullptr,
      Trigger source = Trigger::None);
SysStatus Start(uint16_t* buf = nullptr, uint32_t len = 0);
```

The sequence is construction → SetUp → AddChannel → optional IRQ_en → Start.

| Constructor policy | Start behavior |
|---|---|
| Single, no DMA/trigger | One sequence; DR/GetData supplies the result |
| Single with DMA, no trigger | One sequence, finite DMA buffer |
| Continuous, no DMA | Repeating conversions; DR retains the latest result only |
| Continuous with DMA | Repeating sequences, circular DMA |
| Single with trigger | One sequence per event; circular DMA when bound |
| Continuous with a trigger | Rejected during constinit construction |

When DMA is bound, Start always uses it and needs buffer/length arguments.
Without DMA, call Start(); nonempty buffer arguments return Error. There is no
AttachDMA/AttachTrig/AttachExtTrig, Start_DMA, StartCont or mutable binding API.
Copy/move is forbidden. After a finite capture call Start again; a busy capture
rejects another start or channel change. Stop retains bindings/channels; repeat
SetUp requires DeInit.

ADC_USE_ATOMIC and DMA_USE_ATOMIC independently select ordinary (0, default) or
ST ATOMIC_* control updates. Rebuild both driver and application after changes.
They do not serialize a whole main/IRQ operation.

---

## 4. Setup Order and Lifetime

Initialize System/RCC first. SetUp, AddChannel/ClearChannels, Start, Stop and
DeInit execute sequentially in main. G0 hardware handshakes use a two-tick
budget: with a regular 1 ms tick this is roughly 1–2 ms depending on start phase.
SysTick must run. IRQ handlers do not wait; Stop/DeInit in main may wait.

Serialize object control against its ADC/DMA IRQs and prevent mutual preemption.
There are no internal locks, thread checks or Process method. IsBusy/GetStatus
are pure queries. Callbacks notify main; they must not reconfigure/restart ADC
or change IRQ Registry. Disabling a peripheral source through IRQ_en is allowed.

The object, trigger TIM and active buffer remain alive until capture/registration
ends. SetUp acquires/configures embedded DMA for halfwords, Per→Mem, MINC and DR;
do not reconfigure or release that resource independently before ADC DeInit.

---

## 5. Channels and Sampling

ADC_PIN validates descriptor ranges during compilation. Use ADC_N::_1::PA0_IN0,
ADC_N::_3::PF6_IN4, or ADC_N::IN::PA0_IN0 on G0. Package bonding and the actual
device route remain application checks.

AddChannel order is result order. F4/F7 support up to 16 ranks, arbitrary order
and repeats. G0 supports eight ordered ranks for inputs 0..14; a ninth rank or
a higher input switches to bitmask mode, requiring strictly ascending unique
channels. Repeated physical inputs must use the same sampling setting.

F4/F7 use PCLK2/4 and per-channel CH_SMPL. ADCPRE is shared by ADC1/2/3: configure
all ADCs before starting them. No common/multimode manager is provided. G0 uses
PCLK/2; SetUp selects SMP1/SMP2 and AddChannel chooses one of these.

VREFINT/TEMP/VBAT are INTERNAL inputs on ADC1. Choose sampling from the device
datasheet and ADC clock. Delay_ms(1) starts an internal source; it does not extend
each acquisition. A shared TEMP/VBAT input cannot be added as both sources.

---

## 6. IRQs and Inherited Callbacks

SetUp registers ADC and its embedded DMA adapter separately; no manual duplicate
registration is needed. Each handler reads only its own hardware block. Failure
releases only acquisitions made by that setup, preserving other shared handlers.

IRQ_en changes sources without altering NVIC/registration:

| Source | Contract |
|---|---|
| EOC/EOS | F4/F7 aliases call OnEOS only; G0 has separate events |
| OVR | Reports overrun through OnError |
| DMA_HT | Initially off; enable while idle before Start for OnDmaHalf |
| DMA_TC | Initially on; cannot be disabled for a finite capture |

Changing HT while Busy is ignored. Circular TC notifications can be gated.
TE and F4/F7 DME are enabled during setup and rearmed after an error. DMA calls
with Enable_IRQ(..., false) preserve the caller's NVIC mask.

```cpp
class MyADC : public ADC_N {
public:
    using ADC_N::ADC_N;
    volatile bool ready = false;
    volatile uint32_t errors = 0;
protected:
    void OnDmaComplete() override { ready = true; }
    void OnDmaHalf() override {  }
    void OnError(uint32_t causes) override { errors = causes; }
    void OnEOS() override {  }
};
```

G0 additionally provides OnEOC. Base callbacks need not be called to complete
the operation. Do not read GetData while DMA owns DR. Without DMA, scans and
Continuous can overwrite an unread result.

---

## 7. Timer and External Trigger Binding

Configure the TIM first: SetUpTrigger for TRGO, or SetCompareIRQ(ch, compare,
nullptr) for a direct CCx event. ADC does not choose TIM frequency, compare,
TRGO or EXTI routing. The application ensures the TIM matches the selected
ADC Trigger enum; the constructor checks presence, not that hardware route.
An uninitialized TIM returns InitError during ADC SetUp.

Start arms DMA/ADC before starting TIM. Stop and errors stop that TIM; do not
restart it independently during capture. EXTI11 uses timer=nullptr and a rising
edge configured by the application. EXTSEL values are family-specific. TRGO2
configuration is outside the current TIM API; G0 TIM1 uses CC4.

---

## 8. HT/TC and Cache Ownership

Before RX, System::CacheCleanInvalidate prepares the buffer. Finite TC returns
the entire buffer. Circular HT invalidates the first half before OnDmaHalf;
TC invalidates the second before OnDmaComplete, or all of it when HT is disabled.
Finite capture may use HT, but final TC still returns all samples.

GetIRQFlags reads HT/TC/errors once; FLAG_HT/FLAG_TC/FLAG_ERRORS decode the result.
HT and TC are cleared separately. These events do not pause circular DMA.
Choose rate, sampling and buffer size so each part is processed before overwrite;
missed rotations are not recovered. Triggers also need adequate conversion time.

On F7, own all affected 32-byte lines including padding. With HT, each half of a
cacheable buffer must start on a line boundary and occupy complete lines:
alignas(32) uint16_t samples[32] gives two 32-byte halves. Finite transfers without
HT need not have a length divisible by 32. Buffers must be reachable by DMA.
System skips cache operations for a range fully inside a configured noncacheable
DMA region; those halves need no cache-line separation. F4/G0 helpers are empty.

---

## 9. Errors, Stop and DeInit

OnError receives Error::Overrun and/or Error::Dma. Overrun is handled when OVR
is enabled; Continuous without DMA may drop unread results.

- F4/F7 request DMA stop and remain Busy until DMA IRQ confirms stop by TC or
  hardware TE with EN=0; then publish Error and one OnError.
- G0 discards the packet without reading CNDTR as a partial length and publishes
  Error/OnError after the stop command. IRQ never waits.

Error data is neither invalidated for CPU access nor used. No success callback
follows a detected error. A new Start from main rearms sources/handshakes.
Stop confirms stopping from main, preserves configuration and does not notify
completion. A prior error remains in its result. DeInit removes only this ADC's
registrations, releases DMA, disables ADC and stops the bound TIM without
destroying/reconfiguring it. GPIO analog configuration and shared DMA/GPIO
clocks remain; destructor cleanup is not automatic.

---

## 10. Migration

| Previous API | Current API |
|---|---|
| ADC_N(ADCx), mode selected on start | ADC_N(_N::instance, Conversion, dma, timer, Trigger) |
| AttachDMA/AttachTrig/AttachExtTrig | Constructor bindings; DMA configured in SetUp |
| StartCont/StartCont_DMA/StartTrig_DMA | Fixed policy and one Start |
| Start_DMA(buf) | Start(buf, GetChannelCount()) |
| TIM_TriggerGenerator | Common TIM* plus ADC_N::Trigger |
| Shared ADC/DMA HandleIRQ | Separate registered handlers |
| Unused OnDmaHalf | Explicit IRQ_en(DMA_HT, ENABLE) |

---

## 11. Usage Examples

```cpp
constinit ADC_N input(ADC_N::_1::instance, ADC_N::Conversion::Single);
if (input.SetUp() == SysInitStatus::InitOK) {
#if defined(STM32G0)
    auto channel = input.AddChannel(ADC_N::IN::PA0_IN0);
#else
    auto channel = input.AddChannel(ADC_N::_1::PA0_IN0);
#endif
    if (channel == SysStatus::OK) {
        auto started = input.Start();
    }
}
```

```cpp
#if defined(STM32G0)
constexpr ADC_N::DMAReq dma = DMA1_Channel1_BASE;
#else
constexpr ADC_N::DMAReq dma = ADC_N::_1::DMA::RX_DMA2_Stream0;
#endif
constinit MyADC adc(ADC_N::_1::instance, ADC_N::Conversion::Single, dma);
alignas(32) static uint16_t samples[16];

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
        auto started = adc.Start(samples, 2);
    }
}
```

```cpp
#if defined(STM32G0)
constexpr ADC_N::DMAReq timed_dma = DMA1_Channel1_BASE;
#else
constexpr ADC_N::DMAReq timed_dma = ADC_N::_1::DMA::RX_DMA2_Stream0;
#endif
#if defined(STM32F7)
constinit TIM sampling(TIM::_2::instance);
constinit MyADC timed(ADC_N::_1::instance, ADC_N::Conversion::Single, timed_dma, &sampling,
            ADC_N::Trigger::TIM2_TRGO);
#else
constinit TIM sampling(TIM::_3::instance);
constinit MyADC timed(ADC_N::_1::instance, ADC_N::Conversion::Single, timed_dma, &sampling,
            ADC_N::Trigger::TIM3_TRGO);
#endif
alignas(32) static uint16_t circular[32];

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
        auto started = timed.Start(circular, 32);
    }
}
```

These are independent examples; do not assign the same DMA to multiple active
objects. Check every SetUp/AddChannel/Start result. Single/software DMA length
must equal channel count; other DMA lengths are 1..65535, aligned for uint16_t
and divisible by channel count. HT additionally requires an even length.
F4/G0 timer examples use TIM3; F7 uses TIM2. Choose a route actually available
on the selected part (for example F410 does not have TIM3).

---

## 12. Edge Cases

DR retains one result, not a scan buffer. Continuous and an external trigger are incompatible. Circular HT/TC are processing deadlines, not ownership locks. Treat all error data as invalid. G0 configuration timeout needs successful ClearChannels or DeInit/SetUp before retry. Package availability and precise sampling remain application obligations.

---

## See Also

- [Initialization.md](Initialization.md)
- [DMA.md](DMA.md)
- [TIM.md](TIM.md)
- [GPIO.md](GPIO.md)
- [System.md](System.md)

---

## Русский

## Содержание

1. [Зачем это нужно](#1-зачем-это-нужно)
2. [Постоянная инициализация и диагностика конструктора](#2-постоянная-инициализация-и-диагностика-конструктора)
3. [Текущий API](#3-текущий-api)
4. [Порядок работы и время жизни](#4-порядок-работы-и-время-жизни)
5. [Каналы и sampling](#5-каналы-и-sampling)
6. [IRQ и callbacks](#6-irq-и-callbacks)
7. [Таймер и внешние события](#7-таймер-и-внешние-события)
8. [HT/TC и кеш](#8-httc-и-кеш)
9. [Ошибки и освобождение](#9-ошибки-и-освобождение)
10. [Миграция](#10-миграция)
11. [Примеры использования](#11-примеры-использования)
12. [Угловые случаи](#12-угловые-случаи)
13. [См. также](#см-также)

---

## 1. Зачем это нужно

ADC_N задаёт неизменную модель преобразований и привязки DMA/TIM. Настройка последовательности выполняется через SetUp/AddChannel; один Start запускает выбранный конструктором режим.

---

## 2. Постоянная инициализация и диагностика конструктора

Объявляйте объекты периферии глобально с `constinit` либо как `static constinit`. Используйте готовый _N::instance, именованные пины и необязательные маршруты _N::DMA. На G0 передаются числовые DMAx_Channely_BASE, источник запроса закрепляет периферия.

При проверке привязок периферии каждое неверное условие использует `STM32_CONFIG_ERROR("причина")`. Причина передаётся как параметр шаблона неконстантной функции, поэтому GCC включает её в само сообщение ошибки. Макрос выбирает путь через `std::is_constant_evaluated()`; во время выполнения напрямую вызывает `System::DebugTrap(text)`, без вызова шаблонной функции. При `constinit` такой вызов отклоняется при сборке; без него неверное runtime-создание останавливает программу. Ключевое слово обязательно по контракту, конструктор не проверяет его наличие. Проверки состояния аппаратуры и занятых ресурсов остаются в SetUp. Варианты конструкторов и миграция: [Initialization.md](Initialization.md).

---

## 3. Текущий API

Обновлено 19.10.2026: режим преобразований, DMA и триггер закреплены конструктором.
Используется один Start; IRQ ADC и DMA разделены; HT/TC подключены к DMA API.
Сборка, тесты и проверка на плате этой версии не запускались. Старое сравнение
ассемблера прежней версии относится к прежнему общему обработчику.

```cpp
ADC_N(const Instance& instance, Conversion conversion,
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
| Continuous с любым триггером | Ошибка при constinit-конструировании |

Если DMA передан, Start всегда использует его: требуются адрес буфера и длина.
Без DMA вызывается Start(); ненулевые аргументы буфера/длины дают Error.
AttachDMA/AttachTrig/AttachExtTrig, Start_DMA/StartCont/StartCont_DMA/StartTrig_DMA
удалены. Методов смены привязок нет. Копирование и перемещение запрещены.

SetUp выполняется один раз, повторно — после DeInit. После конечного захвата
достаточно снова вызвать Start без повторных SetUp/AddChannel. Активный захват
возвращает Busy при новом запуске/изменении каналов. Stop сохраняет настройки.
DeInit освобождает ресурсы, сохраняя параметры конструктора для следующего SetUp.

---

## 4. Порядок работы и время жизни

System/RCC настраиваются заранее. SetUp, AddChannel/ClearChannels, Start,
Stop и DeInit вызываются последовательно из main. System waits требуют
работающего SysTick; предел аппаратных ожиданий G0 — 2 тика (2 мс номинально). При равномерном тике 1 мс фактическое окно составляет примерно 1–2 мс, в зависимости от фазы старта.
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

---

## 5. Каналы и sampling

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

---

## 6. IRQ и callbacks

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

---

## 7. Таймер и внешние события

Приложение заранее настраивает TIM через SetUpTrigger для TRGO либо SetCompareIRQ для прямого CCx. ADC принимает TIM* и источник Trigger; точное соответствие аппаратного маршрута проверяет приложение. Наличие указателя проверяется при constinit, готовность TIM — при ADC SetUp. EXTI11 использует nullptr вместо TIM. Start сначала вооружает ADC/DMA, затем запускает таймер; Stop и ошибки останавливают его. Подробный пример — в разделе примеров использования.

---

## 8. HT/TC и кеш

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

---

## 9. Ошибки и освобождение

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

---

## 10. Миграция

| Раньше | Сейчас |
|---|---|
| ADC_N(ADCx), режим при запуске | ADC_N(_N::instance, Conversion, dma, timer, Trigger) |
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

---

## 11. Примеры использования

### Чтение без DMA

```cpp
constinit ADC_N input(ADC_N::_1::instance, ADC_N::Conversion::Single);
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
constexpr ADC_N::DMAReq dma = DMA1_Channel1_BASE;
#else
constexpr ADC_N::DMAReq dma = ADC_N::_1::DMA::RX_DMA2_Stream0;
#endif
constinit MyADC adc(ADC_N::_1::instance, ADC_N::Conversion::Single, dma);
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
точное соответствие проверяет вызывающий код. Timer-source без TIM либо TIM вместе с None/EXTI11 отклоняются при
constinit-конструировании. Ненастроенный TIM даёт InitError при SetUp.

Пример ниже требует TIM3 на G0/F4 либо TIM2 на F7. Для F410, где TIM3 нет,
используйте, например, TIM1_CC1 и SetCompareIRQ(CH1, compare, nullptr).

```cpp
// Отдельный пример: не используйте один DMA одновременно с предыдущим adc.
#if defined(STM32G0)
constexpr ADC_N::DMAReq timed_dma = DMA1_Channel1_BASE;
#else
constexpr ADC_N::DMAReq timed_dma = ADC_N::_1::DMA::RX_DMA2_Stream0;
#endif
#if defined(STM32F7)
constinit TIM sampling(TIM::_2::instance);
constinit MyADC timed(ADC_N::_1::instance, ADC_N::Conversion::Single, timed_dma, &sampling,
            ADC_N::Trigger::TIM2_TRGO);
#else
constinit TIM sampling(TIM::_3::instance);
constinit MyADC timed(ADC_N::_1::instance, ADC_N::Conversion::Single, timed_dma, &sampling,
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

---

## 12. Угловые случаи

Continuous и внешний триггер несовместимы. DR без DMA хранит только последний результат. HT/TC не останавливают circular DMA: приложение обязано обработать данные до перезаписи. Буфер с ошибкой не используется; ожидания и DeInit допускаются только из main.

---

## См. также

- [Initialization.md](Initialization.md)
- [DMA.md](DMA.md)
- [TIM.md](TIM.md)
- [GPIO.md](GPIO.md)
- [System.md](System.md)
