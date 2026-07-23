# TIM — подробное описание

## Содержание

1. [Зачем базовый класс защищён](#1-зачем-базовый-класс-защищён)
2. [PeriphInfo — таблица фактов о каждом таймере](#2-periphinfo--таблица-фактов-о-каждом-таймере)
3. [SetFrequency() — расчёт PSC/ARR](#3-setfrequency--расчёт-pscarr)
4. [IRQ_en() и общий/раздельный вектор](#4-irq_en-и-общийраздельный-вектор)
5. [TIM::HandleIRQ() — база для всех подклассов](#5-timhandleirq--база-для-всех-подклассов)
6. [Подклассы](#6-подклассы)
   - [6.1 TIM_PeriodicIRQ](#61-tim_periodicirq)
   - [6.2 TIM_PWM](#62-tim_pwm)
   - [6.3 TIM_InputCapture](#63-tim_inputcapture)
   - [6.4 TIM_PulseMeasure](#64-tim_pulsemeasure)
   - [6.5 TIM_EncoderGenerator](#65-tim_encodergenerator)
   - [6.6 TIM_StepGenerator](#66-tim_stepgenerator)
   - [6.7 TIM_HWCounter](#67-tim_hwcounter)
7. [ITR / TRGO — каскадирование таймеров в железе](#7-itr--trgo--каскадирование-таймеров-в-железе)
8. [Примеры использования](#8-примеры-использования)
9. [Угловые случаи](#9-угловые-случаи)

---

## 1. Зачем базовый класс защищён

`TIM` — общая часть для семи разных драйверов (`TIM_PeriodicIRQ`, `TIM_PWM`, `TIM_InputCapture`,
`TIM_PulseMeasure`, `TIM_EncoderGenerator`, `TIM_StepGenerator`, `TIM_HWCounter`), но сам по себе
не является рабочим драйвером — его конструктор `protected`, создать `TIM` напрямую нельзя.

Причина — не только организационная. CMSIS даёт каждому `TIM_TypeDef*` одинаковый набор полей
(`CCR1-4`, `CCMR1-2`, `CCER`, `BDTR`, `RCR`, `OR`), даже если конкретный таймер физически не
реализует часть из них: `TIM6`/`TIM7` (basic timers, F4) вообще не имеют каналов сравнения, только
Update-событие; `TIM9-TIM14` — 1-2 канала; полные 4 канала есть только у `TIM1/2/3/4/5/8` (F4) или
`TIM1/TIM3` (G0). Запись в регистр, которого физически нет у данного экземпляра, аппаратно
игнорируется — не ошибка шины, а тихий no-op. То есть `TIM t(TIM6); t.SetCCCallback(...)`
скомпилировался бы и просто никогда не сработал. Каждый подкласс требует конкретный `Line`/`TIM_PIN`
из таблиц `tim_defs.hpp`, которые **объявляют только существующие каналы** для этого таймера
(например, для `TIM14` нет `TIM::_14::CH2` — второго канала там просто не существует в таблице), так
что ошибка "канал, которого не существует" ловится на этапе компиляции, а не тишиной в рантайме.

---

## 2. PeriphInfo — таблица фактов о каждом таймере

```cpp
struct PeriphInfo {
    TIM_TypeDef*        periph;        // TIM3, ...
    volatile uint32_t*  clk_reg;       // &RCC->APB1ENR
    uint32_t            clk_bit;       // RCC_APB1ENR_TIM3EN
    uint32_t const*     bus_clk;       // &System::TIMxAPB1Clock (уже с учётом правила "x2", см. RCC.md)
    IRQn_Type           irq_up;        // вектор Update-события
    IRQn_Type           irq_cc;        // вектор CC-события; == irq_up, если вектор общий
    bool                has_bdtr;      // advanced/semi-advanced (BDTR/MOE/RCR доступны)
    uint8_t             channel_count; // 0, 1, 2 или 4
    uint32_t            arr_max;       // 0xFFFF (16 бит) или 0xFFFFFFFF (TIM2/TIM5, 32 бита)
    uint32_t            dma_up_req;    // DMAMUX request ID для Update-события (0 — нет/не нужен)
};
static const PeriphInfo tim_table[];   // одна строка на таймер, per-family — tim_defs.hpp Section B
```

`Init()` ищет строку линейным поиском по `TIMx` и запоминает указатель в `_info` — дальше **весь**
остальной код (`SetFrequency`, `IRQ_en`, все `SetUp()` подклассов) читает факты через `_info`,
вместо `switch(TIMx)`/`#if` в каждом методе. Пример реальной строки (F4, `TIM3` — 16-битный
general-purpose, 4 канала, общий вектор Update+CC):

```cpp
{ TIM3, &RCC->APB1ENR, RCC_APB1ENR_TIM3EN, &System::TIMxAPB1Clock,
  TIM3_IRQn, TIM3_IRQn, /*has_bdtr=*/false, /*channels=*/4, /*arr_max=*/0xFFFF, /*dma_up_req=*/0 }
```

Сравните с `TIM1` на том же F4 (advanced timer — `has_bdtr = true`, отдельные вектора Update и CC):

```cpp
{ TIM1, &RCC->APB2ENR, RCC_APB2ENR_TIM1EN, &System::TIMxAPB2Clock,
  TIM1_UP_TIM10_IRQn, TIM1_CC_IRQn, /*has_bdtr=*/true, 4, 0xFFFF, 0 }
```

и с `TIM6` (basic timer — 0 каналов, никакой подкласс с CC-каналами на нём не заведётся):

```cpp
{ TIM6, &RCC->APB1ENR, RCC_APB1ENR_TIM6EN, &System::TIMxAPB1Clock,
  TIM6_DAC_IRQn, TIM6_DAC_IRQn, false, /*channels=*/0, 0xFFFF, 0 }
```

---

## 3. SetFrequency() — расчёт PSC/ARR

```cpp
SysInitStatus TIM::SetFrequency(uint32_t freq)
{
    uint32_t ratio = *_info->bus_clk / freq;                 // (PSC+1)*(ARR+1)
    uint32_t psc   = ratio / (uint64_t(_info->arr_max) + 1);  // минимальный PSC, при котором ARR влезает
    uint32_t arr   = ratio / (psc + 1) - 1;                   // максимальный ARR при этом PSC
    if (psc > 0xFFFF || arr > _info->arr_max) return InitError;
    TIMx->PSC = psc; TIMx->ARR = arr;
    return InitOK;
}
```

Идея: частота прерывания/события определяется произведением `(PSC+1)*(ARR+1)`, но разрешение
таймера (шаг `SetCCR()`/`SetDuty()`) растёт с `ARR`. Формула **максимизирует `ARR`** при заданной
`freq` — `psc` берётся как раз настолько большим, чтобы оставшийся `ARR` ещё помещался в разрядность
таймера (`arr_max` — 0xFFFF на 16-битных, 0xFFFFFFFF на `TIM2`/`TIM5`), а не наоборот. Приведение
`arr_max` к `uint64_t` перед `+1` — иначе `0xFFFFFFFF + 1` переполнило бы `uint32_t` в ноль для
32-битных таймеров.

`SetFrequency()` используется в `TIM_PeriodicIRQ` и косвенно в `TIM_StepGenerator` (для 2x-частоты
toggle-режима, см. §6.6) — но не в `TIM_PWM`, у которого своя формула с фиксированным `ARR` (см.
§6.2), потому что там разрешение задаётся явно вызывающим кодом (аргумент `arr`), а не выводится
автоматически.

---

## 4. IRQ_en() и общий/раздельный вектор

```cpp
void TIM::IRQ_en(IRQ irq, FunctionalState en)
```

Тот же auto-register/auto-unregister идиом, что и у `USART`/`RTC` (см. [IRQ_Registry.md](IRQ_Registry.md)),
но с одним отличием: у продвинутых таймеров Update и Compare-события могут висеть на **двух разных**
векторах NVIC (`irq_up` != `irq_cc`, например `TIM1_UP_TIM10_IRQn` и `TIM1_CC_IRQn` на F4). При
первом включении любого источника `IRQ_en()` регистрирует **оба** вектора (если они разные) на один
и тот же `this`:

```cpp
if (en && !NVIC_GetEnableIRQ(_info->irq_up)) {
    IRQ_Registry::Register(_info->irq_up, this);
    NVIC_EnableIRQ(_info->irq_up);
    if (_info->irq_cc != _info->irq_up) {
        IRQ_Registry::Register(_info->irq_cc, this);
        NVIC_EnableIRQ(_info->irq_cc);
    }
}
```

и симметрично отменяет регистрацию на обоих при выключении последнего источника (`!(TIMx->DIER & all)`,
где `all` — маска всех 5 IE-битов). На таймерах с общим вектором (`irq_up == irq_cc`, большинство
general-purpose таймеров) второй `Register()` просто не выполняется — не нужен.

---

## 5. TIM::HandleIRQ() — база для всех подклассов

```cpp
void TIM::HandleIRQ()
{
    uint32_t sr = TIMx->SR;
    TIMx->SR = ~sr;   // см. ниже — не "= 0"
    if ((sr & TIM_SR_UIF)   && _update_cb) _update_cb();
    if ((sr & TIM_SR_CC1IF) && _cc_cb[0])  _cc_cb[0]();
    if ((sr & TIM_SR_CC2IF) && _cc_cb[1])  _cc_cb[1]();
    if ((sr & TIM_SR_CC3IF) && _cc_cb[2])  _cc_cb[2]();
    if ((sr & TIM_SR_CC4IF) && _cc_cb[3])  _cc_cb[3]();
}
```

Два неочевидных решения, оба закомментированы прямо в коде и стоит знать при чтении/правке:

- **`TIMx->SR = ~sr`, а не `TIMx->SR = 0`.** `SR` — "write-0-to-clear, write-1-is-no-op": запись
  инвертированной маски снимает ровно те флаги, что были прочитаны, и не трогает остальные.
  Блок `SR = 0` снял бы **все** флаги разом — включая тот, что аппаратура могла успеть выставить
  в промежутке между чтением `sr` и записью (например, `CC2IF` взводится, пока ещё выполняется
  колбэк `CC1`) — такое событие было бы потеряно молча вместо того, чтобы остаться выставленным
  для следующего входа в `HandleIRQ()` (tail-chaining NVIC подхватит его сразу же).
- **Все 4 CC-флага проверяются безусловно**, а не циклом до `_info->channel_count`. На таймерах с
  меньшим числом каналов недостающие биты `SR` аппаратно зашиты в 0 согласно Reference Manual —
  проверка "лишних" битов и корректна, и дёшева (одно `AND`+переход на канал, пренебрежимо на фоне
  входа/выхода из прерывания), так что не стоит усложнять код ради этого случая.

Подклассы, которым нужна другая логика диспетчеризации (DMA + таймерные флаги вместе, единственный
канал вместо всех четырёх, и т.д.), переопределяют `HandleIRQ()` целиком — см. таблицу подклассов
ниже.

---

## 6. Подклассы

| Класс | Назначение | Свой `HandleIRQ()`? |
|-------|------------|----------------------|
| `TIM_PeriodicIRQ` | Периодическое прерывание + доп. события на CC-каналах без пина | Нет — использует базовый |
| `TIM_PWM` | ШИМ на 1-4 каналах, опционально с DMA на CCR | Да — DMA TC + база |
| `TIM_InputCapture` | Захват CCRx по фронту на одном канале | Да — один канал вместо всех четырёх |
| `TIM_PulseMeasure` | Период+ширина импульса (PWM input, reset mode) | Нет — работа только на чтение CCR, без колбэка |
| `TIM_EncoderGenerator` | Генератор квадратурных импульсов (шаговый привод) | Да — счётчик оставшихся импульсов |
| `TIM_StepGenerator` | STEP-пульсация 50% duty (STEP/DIR привод) | Да — инкремент счётчика шагов |
| `TIM_HWCounter` | Аппаратный счётчик чужих импульсов через ITR, без CPU | Да — авто-стоп по достижении цели |

### 6.1 TIM_PeriodicIRQ

```cpp
SysInitStatus SetUp(uint32_t freq, void (*cb)(void) = nullptr);
SysInitStatus SetCompareIRQ(TIM_Channel ch, uint32_t compare, void (*cb)(void));
```

`SetUp()` — `SetFrequency(freq)` + колбэк на Update. `SetCompareIRQ()` добавляет **дополнительное**
событие на CC-канале, не занимая GPIO: канал остаётся в режиме Frozen (`OCxM` = 0, сброс по
умолчанию) — `CCxIF` взводится чисто от совпадения `CNT == CCRx`, независимо от `OCxM`/`CCxE`, так
что пин не требуется вообще. Несколько каналов = несколько независимых моментов внутри одного
периода на одном таймере, без необходимости заводить отдельный таймер под каждое расписание.

```cpp
TIM_PeriodicIRQ tick(TIM3);
tick.SetUp(1000, &Tick);                                  // 1 кГц Update
tick.SetCompareIRQ(TIM::TIM_Channel::CH1, 500, &HalfTick); // + событие в середине периода
tick.Start();                                              // SetUp() таймер НЕ запускает
```

### 6.2 TIM_PWM

```cpp
SysInitStatus SetUp(uint32_t freq, uint32_t arr = 999);
void SetDuty(TIM_Channel ch, uint32_t percent);   // 0-100%, масштабируется по фактическому ARR
void SetCCR(TIM_Channel ch, uint32_t val);        // прямая запись CCRx
void AttachDMA(DMA_Sx* dma, TIM_Channel ch);
SysStatus SendDMA(TIM_Channel ch, const uint16_t* data, uint32_t len);
```

Каждый канал настраивается в PWM mode 1 (`OCxM = 6`) **с обязательным preload** (`OCxPE = 1`):

> Preload обязателен для DMA-управляемого вывода: без него DMA-запись приходит примерно через
> 3 такта после события UPDATE (задержка AHB) и попадает в CCR уже посреди периода — это обрезает
> передний фронт первого бита при каждой смене значения CCR. С preload включённым DMA пишет в
> теневой регистр, и значение атомарно применяется на следующем UPDATE.

`AttachDMA()`/`SendDMA()` позволяют менять `CCRx` каждый период через DMA без участия CPU — типичный
случай: генерация протокола WS2812B, где каждый бит кодируется своей длительностью high-фазы.
Несколько каналов подключаются независимо, каждый получает свой DMA-стрим; общий `TIM_DIER_UDE`
включается на первом активном канале и не трогается, пока другие ещё работают.

```cpp
TIM_PWM ws_tim(TIM1, TIM::_1::CH1::PA8, TIM::_1::CH2::PA9, TIM::_1::CH3::PA10);
ws_tim.SetUp(800'000, 79);           // 800 кГц, ARR=79 (под тактовую конкретного МК)
ws_tim.AttachDMA(&dma_ch1, TIM::TIM_Channel::CH1);
ws_tim.SendDMA(TIM::TIM_Channel::CH1, bit_pattern, n_bits);
```

### 6.3 TIM_InputCapture

```cpp
TIM_InputCapture cap(TIM3, TIM::_3::CH1::PA6);
cap.SetUp(1'000'000, &OnEdgeCaptured);  // или без колбэка — опрос через GetCapture()
```

`CCxS` маппится на **собственный** TI-вход канала (`0b01`) — прямой захват, без перекрёстной
маршрутизации между каналами (это делает `TIM_PulseMeasure`, см. §6.4). Свой `HandleIRQ()`
проверяет только один бит `SR` (канал, на котором реально сконфигурирован захват), а не все
четыре — в отличие от базового класса, здесь заранее известно, что остальные три не имеют смысла
для этого объекта.

### 6.4 TIM_PulseMeasure

```cpp
TIM_PulseMeasure pm(TIM3, TIM::_3::CH1::PA6);   // канал должен быть CH1 или CH2
pm.SetUp(1'000'000);
uint32_t width  = pm.GetWidth();   // CCR1
uint32_t period = pm.GetPeriod();  // CCR2
```

Использует аппаратный **Reset mode** (`SMS = 0b100`) с перекрёстной маршрутизацией CCMR: на
выбранном канале — прямой захват (`CC1S = 01`), на втором — обратный (`CC2S = 10`, тот же физический
пин, но инвертированный вход). Каждый фронт на "своём" полярности (`CC1P`) сбрасывает счётчик через
`TS`/`SMS`, поэтому `CCR1` фиксирует ширину импульса, `CCR2` — полный период, оба обновляются
аппаратно каждый цикл сигнала без единого прерывания. Никакого пользовательского колбэка нет —
чисто аппаратный измеритель, читаемый через `GetWidth()`/`GetPeriod()` в любой момент.

### 6.5 TIM_EncoderGenerator

```cpp
TIM_EncoderGenerator step(TIM3, TIM::_3::CH1::PA6, TIM::_3::CH2::PA7);   // A, B (90°)
step.SetUp(/*freq=*/1000, /*period=*/999, /*ch1_width=*/500, /*ch2_width=*/250);
step.SetCompleteCallback(&OnDone);
step.GenPulse(200);    // 200 импульсов вперёд; отрицательное значение — назад
```

Оба канала в toggle-режиме (`OCxM = 0b011`) — выход инвертируется на каждое совпадение,
independent от `CCRx`, что даёт фазовый сдвиг между A и B, задаваемый разностью `ch1_width`/
`ch2_width`. Направление — не отдельный регистр, а инверсия полярности `CC1P` перед стартом
(`GenPulse(pulses > 0)` снимает `CC1P`, `< 0` — ставит и инвертирует знак). `HandleIRQ()` здесь
переопределён на простой декремент программного счётчика `_pulses` в Update-обработчике — когда
он достигает 0, таймер останавливается и вызывается `_on_complete`. Это программный (не RCR-based)
способ ограничить число импульсов — в отличие от `TIM_StepGenerator::RunSteps()` (§6.6), тут нет
аппаратного авто-стопа, ISR обязателен на каждый период.

### 6.6 TIM_StepGenerator

```cpp
TIM_StepGenerator step(TIM3, TIM::_3::CH1::PA6);
step.SetUp(1000, /*count_in_isr=*/true);   // 1 кГц, считать шаги в ISR
step.Start();
uint32_t done = step.GetStepCount();
```

Toggle-режим (`OCxM = 0b011`) даёт ровно 50% duty независимо от `CCRx` — выход переключается на
каждое совпадение, то есть **два** переполнения таймера = **один** выходной импульс. Отсюда
`SetStepFrequency(freq)` вызывает `SetFrequency(freq * 2)` — не опечатка, а прямое следствие
toggle-режима.

Два способа считать прошедшие шаги, выбираются в `SetUp()`:

- **`count_in_isr = true`** — Update IRQ включён, `HandleIRQ()` просто инкрементирует
  `_step_count` (в тактах toggle, `GetStepCount()` возвращает `>> 1`, т.е. в реальных шагах).
  Простое решение, но одно прерывание на каждые 2 фронта — ощутимо на десятках кГц.
- **`count_in_isr = false`** — прерывание вообще не включается; вместо него используется
  [`TIM_HWCounter`](#67-tim_hwcounter), считающий переполнения этого таймера через ITR/TRGO в
  железе, без единого ISR — для высоких частот шагов.

**`RunSteps(steps)`** — аппаратный одноразовый запуск на точное число импульсов без единого ISR
для отслеживания завершения:

```cpp
uint32_t events = steps * 2u - 1u;   // toggle: 2 события на шаг, последнее событие уже финальное
TIMx->RCR  = events;                  // Repetition Counter
TIMx->CR1 |= TIM_CR1_OPM;             // One Pulse Mode — CEN сбрасывается сам, когда RCR исчерпан
TIMx->EGR  = TIM_EGR_UG;              // форсировать перезагрузку RCR из preload перед первым периодом
```

Требует `has_bdtr == true` (Repetition Counter физически есть только у advanced/semi-advanced
таймеров — `TIM1`/`TIM8` на F4, `TIM1`/`TIM16`/`TIM17` на G0) — иначе `InitError`. Важное
свойство: `RCR` считает **прошедшие периоды**, а не время, поэтому смена скорости через
`SetStepFrequency()` между вызовами `RunSteps()` не портит итоговое число сгенерированных
импульсов даже при разгоне/торможении посреди движения.

> Арифметика RCR (`2*steps-1`) не проверена на реальном железе — перед тем как полагаться на неё
> для точного позиционирования, стоит сверить реальное число импульсов осциллографом при первом
> запуске на новой плате.

### 6.7 TIM_HWCounter

```cpp
TIM_StepGenerator step(TIM1, TIM::_1::CH1::PA8);
step.SetUp(200'000, /*count_in_isr=*/false);   // 200 кГц, без единого ISR на самом STEP-таймере

TIM_HWCounter counter(TIM3);
counter.SetUp(step);          // TIM3 считает переполнения TIM1 через ITR — свободный выбор пары см. §7
step.Start();
uint32_t steps_done = counter.Count() / 2;    // /2: toggle = 2 фронта на шаг
```

`SetUp(master)` включает `master.EnableTriggerOutput()` (`MMS = 010`, TRGO на Update) и переводит
**этот** таймер в External Clock Mode 1 (`SMS = 0b111`) с источником `TS` = найденный ITR-индекс —
после этого `CNT` инкрементируется аппаратно на каждое Update-событие master'а, без единого цикла
CPU и без ISR, на любой частоте, которую способен генерировать master. `SetUp()` — чисто
свободный счёт (`Count()` растёт, ничего не останавливается автоматически).

`RunUntil(master, steps, cb)` добавляет авто-стоп: `ARR` этого счётчика выставляется в
`steps*2-1`, при переполнении срабатывает **один** IRQ (на этом счётчике, не на каждый шаг),
который останавливает и себя, и `master`, и вызывает `cb`. Это единственный способ остановить
STEP-генератор на точное число импульсов **без RCR** на самом STEP-таймере — актуально, если
STEP физически генерируется на таймере без Repetition Counter (например, `TIM3`), где
`TIM_StepGenerator::RunSteps()` недоступен (`has_bdtr == false`).

---

## 7. ITR / TRGO — каскадирование таймеров в железе

```cpp
struct ITR_Route { TIM_TypeDef* master; TIM_TypeDef* slave; uint8_t itr; };
static const ITR_Route itr_table[];
static uint8_t FindITR(TIM_TypeDef* master, TIM_TypeDef* slave);  // 0xFF = маршрут не найден/не подтверждён
```

Внутренняя шина Internal Trigger (`ITR0`-`ITR3`) соединяет TRGO одного таймера со Slave Mode
Controller (`SMCR`/`TS`) другого — это то, на чём строится `TIM_HWCounter` (§6.7). Таблица
`itr_table` перечисляет **подтверждённые по Reference Manual** пары `(master, slave, itr)`;
`FindITR()` — линейный поиск, `0xFF` означает "для этой пары нет верифицированной записи, считать
неподдерживаемой" — так `TIM_HWCounter::SetUp()`/`RunUntil()` отказываются работать, а не молча
подключаются к неправильному ITR-индексу.

На F4 таблица заполнена для `TIM1/2/3/4/5/8` (единственные таймеры с полноценным SMCR по RM0090).
На STM32G0 обе записи (`TIM3→TIM1`, `TIM1→TIM3`) стоят с `itr = 0xFF` намеренно — они ещё не
сверены с RM0444, поэтому `TIM_HWCounter` на G0 сейчас всегда возвращает `InitError`, пока кто-то
не подтвердит и не проставит реальный индекс.

```
TIM_StepGenerator (master, STEP на своём CH)  ──TRGO (Update)──▶  ITRx  ──▶  TIM_HWCounter (slave)
        EnableTriggerOutput(): MMS=010                 SMCR: TS=itr, SMS=111 (External Clock Mode 1)
```

---

## 8. Примеры использования

Развёрнутые примеры для каждого подкласса приведены прямо в исходнике — см. doc-комментарии над
каждым классом в [tim.hpp](src/tim.hpp) (секции `TIM_PeriodicIRQ`, `TIM_PWM`, `TIM_InputCapture`,
`TIM_PulseMeasure`, `TIM_EncoderGenerator`, `TIM_StepGenerator`, `TIM_HWCounter`) — они не
дублируются здесь полностью, только ключевые фрагменты в §6. Сводка "что выбрать":

| Задача | Класс |
|--------|-------|
| Периодический тик + доп. события без пина | `TIM_PeriodicIRQ` |
| ШИМ (моторы, LED, WS2812B) | `TIM_PWM` (+ `AttachDMA` для протоколов вроде WS2812B) |
| Измерить частоту/период входного сигнала по фронтам | `TIM_InputCapture` |
| Измерить период И скважность одного PWM-сигнала | `TIM_PulseMeasure` |
| Квадратурные импульсы на шаговый привод (A/B, фиксированное число) | `TIM_EncoderGenerator` |
| STEP/DIR импульсы, малая частота, программный счёт | `TIM_StepGenerator(count_in_isr=true)` |
| STEP/DIR импульсы, высокая частота, без ISR на STEP | `TIM_StepGenerator(false)` + `TIM_HWCounter` |
| Точное число шагов без ISR на каждый шаг | `RunSteps()` (нужен RCR) или `TIM_HWCounter::RunUntil()` (не нужен) |

---

## 9. Угловые случаи

- **`TIM6`/`TIM7` (F4) — 0 каналов** (`channel_count = 0`): годятся только для
  `TIM_PeriodicIRQ` (Update-событие) — все остальные подклассы требуют хотя бы один CC-канал и
  либо не скомпилируются (нет нужной таблицы пинов), либо `SetUp()` вернёт `InitError`.
- **16-бит vs 32-бит ARR** — только `TIM2`/`TIM5` на F4/F7 имеют `arr_max = 0xFFFFFFFF`; все
  остальные ограничены `0xFFFF`. `SetDuty()` предупреждает в своём doc-комментарии: для ARR
  вблизи 32-битного предела использовать `SetCCR()` напрямую, не проценты.
- **`has_bdtr`** — не просто "продвинутый таймер": это единственный признак наличия `BDTR`/`MOE`
  (нужен для `TIM_PWM` на некоторых таймерах — без `MOE` выходы физически не активируются) и `RCR`
  (нужен для `TIM_StepGenerator::RunSteps()`).
- **`TIM_HWCounter` на STM32G0 всегда `InitError`** — таблица `itr_table` не подтверждена (см.
  §7); это не баг конкретного вызова, а сознательно неполная таблица.
- **`TIM_StepGenerator::SetStepFrequency()` сбрасывает режим `RunSteps()`** — явно снимает
  `TIM_CR1_OPM`, возвращая таймер в свободный (continuous) режим; вызов после `RunSteps()`
  отменяет ожидаемый авто-стоп.
- **Общий вектор Update+CC у большинства general-purpose таймеров** — `IRQ_en()` не регистрирует
  второй раз один и тот же вектор (сравнение `irq_cc != irq_up`), но это значит, что `HandleIRQ()`
  для таких таймеров вызывается на **любое** из событий Update/CC1-4 — код внутри всегда сам
  разбирает, какие именно биты `SR` реально стоят (см. §5).

---

## См. также

- [IRQ_Registry.md](IRQ_Registry.md) — `IRQ_en()` (§4) регистрирует `this` тем же способом, что
  `USART`/`SPI`/`RTC`; см. там же про общий/раздельный вектор на одну строку `_table`.
- [DMA.md](DMA.md) — `TIM_PWM::AttachDMA()` (§6.2) настраивает `DMA_Sx` так же, как `USART`/`SPI`.
- [RCC.md](RCC.md) — `_info->bus_clk` (§2-3) указывает на `System::TIMxAPB1Clock`/
  `TIMxAPB2Clock`, которые пишет именно этот модуль (включая правило "таймеры x2").
- [GPIO.md](GPIO.md) — `TIM_PIN`/`Line` (tim_defs.hpp) построены поверх того же `PIN`, что и
  остальные периферийные таблицы пинов.
- [System.md](System.md) — общая инфраструктура (`SysStatus`/`SysInitStatus`, `DebugTrap`),
  используемая всеми методами `SetUp()` этого файла.
