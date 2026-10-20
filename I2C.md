# I2C — Detailed Description

*[English](#english) · [Русский](#русский)*

---

## English

## Table of Contents

1. [Why This Is Needed](#1-why-this-is-needed)
2. [Constant Initialization and Constructor Diagnostics](#2-constant-initialization-and-constructor-diagnostics)
3. [Setup and Compile-Time Timing](#3-setup-and-compile-time-timing)
4. [Transfer API and Status](#4-transfer-api-and-status)
5. [IRQ and Completion](#5-irq-and-completion)
6. [Callbacks Through Inheritance](#6-callbacks-through-inheritance)
7. [Cache and Noncacheable DMA Memory](#7-cache-and-noncacheable-dma-memory)
8. [Atomic Register Updates](#8-atomic-register-updates)
9. [DeInit and Bus Recovery](#9-deinit-and-bus-recovery)
10. [Usage Examples](#10-usage-examples)
11. [Edge Cases](#11-edge-cases)
12. [See Also](#see-also)

---

## 1. Why This Is Needed

I2C implements a 7-bit master with blocking, IRQ and DMA operations. The operation model is shared while hardware branches account for F4 I2C v1 and F7/G0 I2C v2. Slave mode and F4 FMPI2C1 are not implemented here.

---

## 2. Constant Initialization and Constructor Diagnostics

Declare fixed peripheral objects with `constinit` at file scope or as `static constinit`. Use the provided _N::instance, named pins and optional _N::DMA routes. G0 takes numeric DMAx_Channely_BASE selections; the peripheral fixes the request source.

For peripheral binding checks, each failing condition calls non-constexpr `System::DebugTrap` with its cause. A `constinit` initializer rejects that call during compilation; without constinit, a failing runtime construction stops. The keyword is mandatory by the usage contract, not enforced by the constructor. SetUp still checks hardware state and occupied resources. See [Initialization.md](Initialization.md) for constructor variants and migration.

---

## 3. Setup and Compile-Time Timing

One object owns an I2C master. Pins and optional TX/RX DMA are fixed at
construction; blocking/IRQ operations remain usable when DMA is configured.
System/RCC must be initialized first. SetUp configures AF open-drain GPIO with
pull-up, PCLK kernel clock, timing and embedded DMA; real bus pull-ups must match
capacitance/rate. Repeated SetUp requires DeInit. Failure rolls back only newly
acquired DMA/registrations and preserves other shared handlers.

```cpp
template<ClockSystem::Config clocks, SPEED speed = SPEED::STANDARD>
SysInitStatus SetUp();
```

Use the same clocks as ClockSystem::Init<clocks>(). The driver obtains APB1 through
ClockSystem::GetAPB1Clock<clocks>(), sharing RCC Prepare and validation. Invalid
configurations/timing are compile errors. Apply compares the derived APB1 with
System::APB1BusClock before GPIO/MMIO/acquisition. This is nominal state, not an
oscillator measurement; always check RCC Init. Direct RCC changes and dynamic
timing recalculation are outside this contract.

F4 supports STANDARD 100 kHz and FAST 400 kHz. FAST_PLUS 1 MHz is for F7/G0 with
appropriate clock/electrical conditions. F7/G0 calculation in
src/tools/i2c_timing_utility.hpp runs only at compile time: the old 128-entry
permanent RAM array and runtime search are removed. Search retains ST ordering:
first suitable SCLDEL/SDADEL per PRESC, then minimum period error, keeping the
first tie. Invalid periods are skipped early. Analog filter is on, DNF=0;
rise/fall assumptions are 1000/300 ns STANDARD, 300/300 ns FAST, 100/100 ns FAST_PLUS.
This selects an allowed nearby timing, not necessarily exact Hz. The application
checks actual signal edges. Older SetUp(SPEED) and SetUp<pclk_hz, SPEED>() are removed.

---

## 4. Transfer API and Status

| Operation | Blocking | IRQ | DMA |
|---|---|---|---|
| Write | Send | Send_IRQ | Send_DMA |
| Read | Receive | Receive_IRQ | Receive_DMA |
| Register write | WriteReg | WriteReg_IRQ | WriteReg_DMA |
| Register read | ReadReg | ReadReg_IRQ | ReadReg_DMA |

addr is an unshifted 0..0x7F address. len counts bytes, up to 65535.
REG::_8BIT/_16BIT selects one/two register address bytes, high byte first.
ReadReg uses repeated START. F7/G0 split NBYTES into up to 255-byte RELOAD parts
without changing user length. Zero Send/Receive returns Error. CPU WriteReg may
send only the register address with len=0; DMA requires nonzero payload. IsReady
is a blocking address probe.

Blocking calls return OK/Busy/NotInit/Error/Timeout; GetError gives the reason.
GetTick must run. F4 ACK/POS/ADDR/STOP handling for the final 1–3 bytes is sensitive
to ISR latency; the driver does not add internal IRQ masking.

Asynchronous OK means started, not completed. The buffer belongs to the driver
until completion and must remain alive/unmodified. Poll IsBusy/GetError or use
callbacks. There is no internal IRQ/DMA timeout: main enforces a deadline and
calls Abort with this object's IRQs excluded. Abort confirms DMA stop, resets
I2C, publishes Aborted without a callback and retains bindings/registrations.

---

## 5. IRQ and Completion

SetUp registers I2C EV/ER (one shared vector on G0) and separate TX/RX DMA objects.
Each DMA handler reads its resource once through GetIRQFlags and the public
FLAG_TC/FLAG_ERRORS masks; HT is unused. Registry full/duplicate failures return
InitError. Shared G0 DMA TX/RX vectors require two registry slots.

Handlers for one object must not preempt each other. The application serializes
start/Abort/DeInit with these IRQs. Callbacks must not wait, call blocking I/O,
Abort/DeInit or change the registry. No internal blanket masking is introduced.

F4 TX completes after BTF and issuing STOP; RX DMA issues STOP after TC (one-byte
RX issues it after ADDR). There is no wait for STOP to clear: another start
returns Busy while STOP/BUSY remain set.

F7/G0 require both STOPF for the bus phase and DMA TC. If STOPF arrives first,
remain Busy until DMA completes; if TC arrives first, its source is disabled
while the flag remains until bus completion. Successful publication requires
zero DMA residual count. CPU STOPF also checks register/payload completion.

F4/F7 errors request DMA stop and finish on TC or TE with EN=0. G0 discards an
error packet after the stop command without interpreting CNDTR as partial data.
IRQ does not wait for EN/STOP or drain an unbounded FIFO. Missing events can
leave Busy: the external deadline and Abort cover that case too.

---

## 6. Callbacks Through Inheritance

```cpp
class SensorBus : public I2C {
public:
    using I2C::I2C;
    volatile bool ready = false;
    volatile ERR error = ERR::None;
protected:
    void OnComplete() override { ready = true; }
    void OnError(ERR reason) override { error = reason; }
};
```

OnComplete/OnError run from I2C or DMA IRQ after state/buffer handover. Error
data must not be read. Blocking methods do not invoke these callbacks. The old
SetDoneCallback/SetErrorCallback API is removed.

---

## 7. Cache and Noncacheable DMA Memory

TX uses System::CacheClean before DMA. RX uses CacheCleanInvalidate before
start and CacheInvalidate after successful completion, before CPU access.
On F7 the application owns all affected 32-byte lines including padding; len
need not be divisible by 32. Error buffers are not invalidated for partial reads.
System skips a range entirely inside a configured noncacheable DMA region;
F4/G0 helpers are empty. Project/MPU settings do not switch I2C implementations.

---

## 8. Atomic Register Updates

I2C_USE_ATOMIC in i2c.hpp defaults to 0 (SET_BIT/CLEAR_BIT/MODIFY_REG); 1 selects
ST ATOMIC_*. F4/F7 use LDREX/STREX; G0 briefly saves/masks/restores PRIMASK.
DMA_USE_ATOMIC independently controls DMA CR/CCR. Rebuild library/application;
a define confined to main.cpp is insufficient. One atomic RMW does not serialize
the FSM/transfer/buffer handover. ICR/SR, data registers and NBYTES/START commands
are written directly according to hardware semantics.

---

## 9. DeInit and Bus Recovery

Call DeInit from main before destroying the object. It stops the operation,
releases its DMA/IRQ entries, resets I2C and returns pins to input. Other handlers
on shared vectors remain. Repeated DeInit is allowed; the same bindings can
subsequently SetUp again. Recover is application code: DeInit first, drive GPIO
as appropriate, confirm released lines with device-specific delays/deadlines,
then SetUp. No hidden universal recovery service is provided.

---

## 10. Usage Examples

```cpp
constexpr ClockSystem::Config clocks{};
#if defined(STM32G0)
constexpr I2C::DMAReq tx = DMA1_Channel1_BASE;
constexpr I2C::DMAReq rx = DMA1_Channel2_BASE;
#else
constexpr I2C::DMAReq tx = I2C::_1::DMA::TX_DMA1_Stream6;
constexpr I2C::DMAReq rx = I2C::_1::DMA::RX_DMA1_Stream0;
#endif

constinit I2C bus(I2C::_1::instance, I2C::_1::SCL::PB8, I2C::_1::SDA::PB9, tx, rx);
if (System::Init() != SysInitStatus::InitOK) return;
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK) return;
auto init = bus.SetUp<clocks, I2C::SPEED::FAST>();
```

```cpp
uint8_t id = 0;
auto status = bus.ReadReg(0x68, 0x75, &id, 1, I2C::REG::_8BIT, 100);
```

```cpp
if (bus.Receive_DMA(0x68, buffer, sizeof(buffer)) == SysStatus::OK) {
    const uint32_t started = System::GetTick();
    while (bus.IsBusy() && System::GetTick() - started < 100u) {}
    if (bus.IsBusy()) bus.Abort();
}
```

Independent examples: check every returned status; the waiting/Abort example is main-only.

---

## 11. Edge Cases

No internal async timeout; use a main deadline and Abort. A failed RX buffer is invalid. A new F4 operation may remain Busy until STOP/BUSY clear. External pull-ups, electrical behavior and recovery belong to the application. Duplicate setup requires DeInit.

---

## See Also

- [RM0444, DMAMUX input assignment](https://www.st.com/resource/en/reference_manual/rm0444-stm32g0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf): I2C1 10/11, I2C2 12/13, I2C3 62/63.
- [RM0090, DMA1 request mapping](https://www.st.com/resource/en/reference_manual/rm0090-stm32f407-advanced-armbased-32bit-mcus-stmicroelectronics.pdf): I2C3 RX Stream2 CH3.
- [RM0431, DMA1 request mapping](https://www.st.com/resource/en/reference_manual/rm0431-stm32f72xxx-and-stm32f73xxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf): F72/F73 I2C1–3.
- [RM0385](https://www.st.com/resource/en/reference_manual/dm00124865.pdf), [RM0410](https://www.st.com/resource/en/reference_manual/rm0410-stm32f76xxx-and-stm32f77xxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf): I2C4 RX Stream2 CH2, TX Stream5 CH2.

Перенос выполнен по исходникам. Сборки, тесты и проверка на плате не запускались.

- [Initialization.md](Initialization.md)
- [RCC.md](RCC.md)
- [DMA.md](DMA.md)
- [GPIO.md](GPIO.md)
- [IRQ_Registry.md](IRQ_Registry.md)

---

## Русский

## Содержание

1. [Зачем это нужно](#1-зачем-это-нужно)
2. [Постоянная инициализация и диагностика конструктора](#2-постоянная-инициализация-и-диагностика-конструктора)
3. [Модель работы](#3-модель-работы)
4. [Передачи](#4-передачи)
5. [IRQ и завершение](#5-irq-и-завершение)
6. [Callbacks через наследование](#6-callbacks-через-наследование)
7. [Кеш и некешируемая DMA-память](#7-кеш-и-некешируемая-dma-память)
8. [Атомарные изменения регистров](#8-атомарные-изменения-регистров)
9. [DeInit и восстановление шины](#9-deinit-и-восстановление-шины)
10. [Примеры использования](#10-примеры-использования)
11. [Угловые случаи](#11-угловые-случаи)
12. [См. также](#см-также)

---

## 1. Зачем это нужно

I2C реализует master-обмен с адресами 7 бит, блокирующими, IRQ- и DMA-операциями. Общая модель операции сохраняется, аппаратные ветви F4 и F7/G0 учитывают разные поколения блока.

---

## 2. Постоянная инициализация и диагностика конструктора

Объявляйте объекты периферии глобально с `constinit` либо как `static constinit`. Используйте готовый _N::instance, именованные пины и необязательные маршруты _N::DMA. На G0 передаются числовые DMAx_Channely_BASE, источник запроса закрепляет периферия.

При проверке привязок периферии каждое неверное условие вызывает неконстантный `System::DebugTrap` с причиной. При `constinit` такой вызов отклоняется при сборке; без него неверное runtime-создание останавливает программу. Ключевое слово обязательно по контракту, конструктор не проверяет его наличие. Проверки состояния аппаратуры и занятых ресурсов остаются в SetUp. Варианты конструкторов и миграция: [Initialization.md](Initialization.md).

---

## 3. Модель работы

Один объект управляет одним I2C master. Поддерживаются F4 (I2C v1) и F7/G0
(I2C v2), адреса 7 бит без сдвига, блокирующие, IRQ- и DMA-операции.
Пины и DMA закрепляются конструктором. SetUp выполняется один раз до DeInit.
Блокирующие операции и IRQ доступны и при настроенном DMA.

```cpp
constexpr ClockSystem::Config clocks{}; // HSI, делители шин /1.
#if defined(STM32G0)
constexpr I2C::DMAReq tx = DMA1_Channel1_BASE;
constexpr I2C::DMAReq rx = DMA1_Channel2_BASE;
#else
constexpr I2C::DMAReq tx = I2C::_1::DMA::TX_DMA1_Stream6;
constexpr I2C::DMAReq rx = I2C::_1::DMA::RX_DMA1_Stream0;
#endif

constinit I2C bus(I2C::_1::instance, I2C::_1::SCL::PB8, I2C::_1::SDA::PB9, tx, rx);
// В main: одна и та же конфигурация для RCC и I2C.
if (System::Init() != SysInitStatus::InitOK) return;
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK) return;
auto init = bus.SetUp<clocks, I2C::SPEED::FAST>();
```

Без DMA последние два аргумента опускаются. На F4/F7 именованный DMAReq содержит
поток/CHSEL, периферию и направление. Несовместимая привязка при constinit вызывает ошибку компиляции. На G0 приложение выбирает физический канал; источник DMAMUX
драйвер берёт из I2Cx и направления. Один ресурс нельзя одновременно закреплять
за TX/RX этого объекта либо другим потребителем.

SetUp настраивает GPIO AF open-drain с pull-up, PCLK в качестве kernel clock,
тайминг и внутренние DMA. Для реальной шины внешние подтяжки выбираются
по её ёмкости и скорости. F4 поддерживает STANDARD (100 кГц) и FAST (400 кГц).
FAST_PLUS (1 МГц) доступен F7/G0 при подходящем тактировании и электрических
параметрах. FMPI2C1 на F4 не является поддерживаемым здесь I2C v1.

InitError возвращается при повторной настройке, несовпадении APB1 с конфигурацией,
отсутствующей периферии, занятом DMA или отказе регистрации IRQ.
При отказе регистрации удаляются только добавленные этим вызовом записи
и освобождаются приобретённые DMA. Проверяйте возвращённый статус.

### Расчёт во время компиляции

SetUp<clocks, SPEED>() принимает ClockSystem::Config, использованную в
ClockSystem::Init<clocks>(). Частота APB1 берётся из
ClockSystem::GetAPB1Clock<clocks>(): метод использует тот же Prepare и проверку
ограничений устройства, что и RCC Init. Копии формул PLL/делителей в I2C нет.

Расчёт consteval, неверная конфигурация тактирования/скорость/тайминги дают ошибку
компиляции. На F4 заранее рассчитываются CCR/TRISE/FREQ, на F7/G0 — TIMINGR.
Без второго параметра выбирается STANDARD: bus.SetUp<clocks>(). Отдельно задавать
частоту I2C kernel clock больше не требуется. Старые SetUp(SPEED) и
SetUp<pclk_hz, SPEED>() удалены.

При запуске Apply сравнивает рассчитанную APB1 с System::APB1BusClock до изменения
регистров, GPIO, DMA и регистрации IRQ. Несовпадение возвращает InitError без
занятия ресурсов. Это сравнение с частотой, опубликованной RCC, а не измерение
генератора; прямое изменение RCC мимо System/ClockSystem не поддерживается.
Проверяйте успешность System/RCC Init перед настройкой I2C. Частота фиксирована
на всё время работы I2C, динамический перерасчёт таймингов не добавлен.

Расчёт F7/G0 находится в src/tools/i2c_timing_utility.hpp. Прежние C utility,
статический массив на 128 записей (2560 байт) и общий счётчик удалены. Массив
из 16 записей на стеке MCU тоже не нужен: поиск выполняет только компилятор,
по одному PRESC. Невозможные периоды отсекаются досрочно.

Сохранены прежние ограничения/порядок выбора ST: первый подходящий SCLDEL/SDADEL
для PRESC, затем минимальная ошибка периода. Равные результаты сохраняют первый
вариант. Параметры не стали универсальной настройкой платы: analog filter включён,
DNF=0, rise/fall — 1000/300 нс для STANDARD, 300/300 нс для FAST, 100/100 нс для
FAST_PLUS. Выбирается ближайший допустимый вариант; точное равенство частоте
не гарантируется. Соответствие фронтов фактической шине остаётся приложению.

В CMake удалён только старый C-исходник utility и ненужный include path src/tools;
настройки частоты/скорости в CMake не добавлялись.

---

## 4. Передачи

| Операция | Блокирующая | Через IRQ | Через DMA |
|---|---|---|---|
| Запись | Send | Send_IRQ | Send_DMA |
| Чтение | Receive | Receive_IRQ | Receive_DMA |
| Запись регистра | WriteReg | WriteReg_IRQ | WriteReg_DMA |
| Чтение регистра | ReadReg | ReadReg_IRQ | ReadReg_DMA |

addr — 0…0x7F без сдвига; len — число байтов, до 65535.
REG::_8BIT/REG::_16BIT задаёт один/два байта адреса регистра, старший первым.
ReadReg использует повторный START. F7/G0 разбивают NBYTES на части до 255
с RELOAD; это не меняет длину пользовательского буфера.

Нулевой Send/Receive — Error. Сохранена запись одного адреса регистра без
payload через WriteReg/WriteReg_IRQ с len=0; DMA-операции требуют ненулевой len.
IsReady — блокирующая адресная проба без данных.

```cpp
uint8_t id = 0;
auto status = bus.ReadReg(0x68, 0x75, &id, 1, I2C::REG::_8BIT, 100);
```

Блокирующие вызовы возвращают OK, Busy, NotInit, Error или Timeout.
GetError уточняет причину. GetTick должен работать; передавать управление
этому I2C из другого контекста во время блокирующей операции нельзя.
На F4 последовательности ACK/POS/ADDR/STOP последних 1–3 байт чувствительны
к задержке обслуживания. Длинные посторонние ISR требуют отдельной проверки
на плате; внутреннее маскирование IRQ не вводится.

Асинхронный OK означает запуск. До окончания операции буфер принадлежит драйверу:
его нельзя менять, освобождать или читать как завершённый результат.
Успех/ошибка определяются через IsBusy/GetError либо callbacks.
Встроенного таймаута IRQ/DMA нет; срок ограничивает main:

```cpp
if (bus.Receive_DMA(0x68, buffer, sizeof(buffer)) == SysStatus::OK) {
    const uint32_t started = System::GetTick();
    while (bus.IsBusy() && System::GetTick() - started < 100u) {}
    if (bus.IsBusy()) bus.Abort();
    // При GetError()!=ERR::None данные не использовать.
}
```

Abort вызывается из main при исключённой работе IRQ данного объекта.
Он отключает запросы, подтверждает остановку DMA и сбрасывает I2C; callback
не вызывается, GetError возвращает Aborted. Привязки и регистрации сохраняются.

---

## 5. IRQ и завершение

SetUp регистрирует обработчик I2C на EV/ER (один общий вектор на G0) и отдельные
объекты DMA TX/RX. Каждый DMA-обработчик читает только свой канал/поток:
GetIRQFlags с публичными FLAG_TC/FLAG_ERRORS. DMA HT для I2C не используется.
Полный реестр или повторная регистрация возвращают InitError.

Общий вектор G0 для двух направлений требует двух мест IRQ_Registry.
Все обработчики одного I2C должны иметь одинаковый приоритет либо иным способом
не вытеснять друг друга. Приложение согласует старт/Abort/DeInit с этими IRQ.
Из callbacks запрещены ожидания, блокирующие передачи, Abort/DeInit
и изменение таблицы IRQ. Произвольное внутреннее маскирование не выполняется.

F4 TX завершается по BTF и команде STOP; F4 RX DMA формирует STOP после TC
(при одном байте STOP задаётся после ADDR). F4 не ждёт снятия STOP:
следующий запуск возвращает Busy, пока STOP/BUSY ещё установлены.

F7/G0 подтверждают автобусную фазу по STOPF, а DMA — по TC.
Если STOPF пришёл раньше последнего переноса DMA, объект остаётся Busy
до DMA IRQ. Если TC пришёл первым, источник TC выключается, а флаг сохраняется
до завершения I2C. При успешной публикации остаток счётчика DMA обязан быть нулём.
Для CPU-передачи STOPF также проверяет завершённость адреса регистра и данных.

При ошибке F4/F7 драйвер запрашивает остановку DMA и завершает передачу
после TC или аппаратного TE при EN=0. На G0 ошибочный пакет отбрасывается
после команды остановки без использования CNDTR как длины частичных данных.
Ожиданий EN/STOP и циклов обработки до опустошения FIFO в IRQ нет.

Неполная передача не публикуется как успешная. При исчезновении ожидаемых событий
она может остаться Busy; внешний срок и Abort нужны и для этого случая.

---

## 6. Callbacks через наследование

```cpp
class SensorBus : public I2C {
public:
    using I2C::I2C;
    volatile bool ready = false;
    volatile ERR error = ERR::None;
protected:
    void OnComplete() override { ready = true; }
    void OnError(ERR reason) override { error = reason; }
};
```

Callbacks вызываются из I2C или DMA IRQ после освобождения буфера и обновления
состояния. Ошибочные данные не используются. Блокирующие методы callbacks
не вызывают. Старые SetDoneCallback/SetErrorCallback удалены.

---

## 7. Кеш и некешируемая DMA-память

TX: System::CacheClean перед передачей. RX: CacheCleanInvalidate перед запуском,
CacheInvalidate после успешного завершения, до доступа CPU.
На F7 вызывающий код владеет всеми затронутыми 32-байтными линиями, включая
padding. len не обязана быть кратной 32. Ошибочный буфер не инвалидируется
для чтения частичных данных и не считается пригодным.

System автоматически пропускает обслуживание диапазона, полностью лежащего
в настроенной некешируемой DMA-области. На F4/G0 кеш-методы пустые.
Настройки project/MPU не переключают ветки самого I2C.

---

## 8. Атомарные изменения регистров

I2C_USE_ATOMIC в i2c.hpp: 0 по умолчанию — SET_BIT/CLEAR_BIT/MODIFY_REG;
1 — соответствующие ATOMIC_* ST. На F4/F7 используются LDREX/STREX,
на G0 — краткое маскирование с восстановлением PRIMASK.
DMA_USE_ATOMIC независимо задаёт операции CR/CCR самого DMA.
После изменения пересобираются драйверы и приложение; определения только
в main.cpp недостаточно.

Атомарность защищает одну операцию RMW. Она не заменяет согласование целой
передачи, состояния FSM и владения буфером. ICR/SR, DR/TXDR/RXDR и команды
NBYTES/START записываются напрямую согласно аппаратной семантике.

---

## 9. DeInit и восстановление шины

DeInit вызывается из main до уничтожения объекта. Останавливает обмен,
освобождает свои DMA и IRQ-записи, сбрасывает блок и возвращает пины в input.
Другие обработчики общего вектора сохраняются. Повторный DeInit допустим;
после него можно снова вызвать SetUp с теми же конструкторными привязками.

Recover удалён из I2C. Приложение само восстанавливает удерживаемую slave шину:
сначала завершает использование драйвера через DeInit, затем управляет GPIO,
подтверждает отпускание линий и выбирает задержки/таймауты по своему устройству.
После восстановления вновь вызывает SetUp. Универсальной реализации импульсов
и скрытого сервиса восстановления в драйвере нет.

---

## 10. Примеры использования

```cpp
constexpr ClockSystem::Config clocks{}; // HSI, делители шин /1.
#if defined(STM32G0)
constexpr I2C::DMAReq tx = DMA1_Channel1_BASE;
constexpr I2C::DMAReq rx = DMA1_Channel2_BASE;
#else
constexpr I2C::DMAReq tx = I2C::_1::DMA::TX_DMA1_Stream6;
constexpr I2C::DMAReq rx = I2C::_1::DMA::RX_DMA1_Stream0;
#endif

constinit I2C bus(I2C::_1::instance, I2C::_1::SCL::PB8, I2C::_1::SDA::PB9, tx, rx);
// В main: одна и та же конфигурация для RCC и I2C.
if (System::Init() != SysInitStatus::InitOK) return;
if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK) return;
auto init = bus.SetUp<clocks, I2C::SPEED::FAST>();
```

```cpp
uint8_t id = 0;
auto status = bus.ReadReg(0x68, 0x75, &id, 1, I2C::REG::_8BIT, 100);
```

```cpp
if (bus.Receive_DMA(0x68, buffer, sizeof(buffer)) == SysStatus::OK) {
    const uint32_t started = System::GetTick();
    while (bus.IsBusy() && System::GetTick() - started < 100u) {}
    if (bus.IsBusy()) bus.Abort();
    // При GetError()!=ERR::None данные не использовать.
}
```

Примеры независимы. Настройка RCC и I2C использует один clocks; проверьте каждый возвращаемый статус. Цикл ожидания/Abort допускается только из main.

---

## 11. Угловые случаи

Для IRQ/DMA нет встроенного таймаута: main задаёт срок и при необходимости вызывает Abort. Ошибочный RX-буфер не используется. F4 может вернуть Busy до снятия STOP/BUSY. Внешние подтяжки, помехи на линии и Recover остаются приложению; повторный SetUp требует DeInit.

---

## См. также

- [RM0444, DMAMUX input assignment](https://www.st.com/resource/en/reference_manual/rm0444-stm32g0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf): I2C1 10/11, I2C2 12/13, I2C3 62/63.
- [RM0090, DMA1 request mapping](https://www.st.com/resource/en/reference_manual/rm0090-stm32f407-advanced-armbased-32bit-mcus-stmicroelectronics.pdf): I2C3 RX Stream2 CH3.
- [RM0431, DMA1 request mapping](https://www.st.com/resource/en/reference_manual/rm0431-stm32f72xxx-and-stm32f73xxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf): F72/F73 I2C1–3.
- [RM0385](https://www.st.com/resource/en/reference_manual/dm00124865.pdf), [RM0410](https://www.st.com/resource/en/reference_manual/rm0410-stm32f76xxx-and-stm32f77xxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf): I2C4 RX Stream2 CH2, TX Stream5 CH2.

Перенос выполнен по исходникам. Сборки, тесты и проверка на плате не запускались.

- [Initialization.md](Initialization.md)
- [RCC.md](RCC.md)
- [DMA.md](DMA.md)
- [GPIO.md](GPIO.md)
- [IRQ_Registry.md](IRQ_Registry.md)
