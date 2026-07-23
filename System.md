# System — подробное описание

## Содержание

- [System — подробное описание](#system--подробное-описание)
	- [Содержание](#содержание)
	- [1. Зачем это нужно — связующая часть между всеми драйверами](#1-зачем-это-нужно--связующая-часть-между-всеми-драйверами)
	- [2. System::Init() — что настраивается при старте](#2-systeminit--что-настраивается-при-старте)
	- [3. Тактовые переменные — общая память между RCC и всеми драйверами](#3-тактовые-переменные--общая-память-между-rcc-и-всеми-драйверами)
	- [4. SysTick и миллисекундный счётчик](#4-systick-и-миллисекундный-счётчик)
	- [5. Микросекундная задержка через DWT CYCCNT](#5-микросекундная-задержка-через-dwt-cyccnt)
	- [6. DebugTrap — единая точка отказа для всех драйверов](#6-debugtrap--единая-точка-отказа-для-всех-драйверов)
	- [7. Bit-banding и SysStatus/SysInitStatus](#7-bit-banding-и-sysstatussysinitstatus)
	- [8. Примеры использования](#8-примеры-использования)
		- [Стандартная последовательность старта (как в main.cpp)](#стандартная-последовательность-старта-как-в-maincpp)
		- [Микросекундная задержка](#микросекундная-задержка)
		- [DebugTrap с выводом в UART (типично для отладочной сборки)](#debugtrap-с-выводом-в-uart-типично-для-отладочной-сборки)
	- [9. Угловые случаи](#9-угловые-случаи)
	- [См. также](#см-также)

---

## 1. Зачем это нужно — связующая часть между всеми драйверами

`system.hpp`/`system.cpp` — не отдельная периферия, а **общая инфраструктура**, от которой
зависят буквально все остальные драйверы этого проекта:

```
                              ┌────────────────────┐
                              │   system.hpp/cpp   │
                              │  System, SysStatus,│
                              │  SysInitStatus,    │
                              │  kHz/MHz, BIT_BB   │
                              └──────────┬─────────┘
                                         │  #include "system.hpp"
        ┌──────────────┬──────────────┬──┴───────────┬──────────────┬──────────────┐
        ▼              ▼              ▼              ▼              ▼              ▼
   [RCC.md]        [GPIO.md]      [DMA.md]        [UART.md]       [TIM.md]      [RTC.md] / [SPI.md] / [Flash.md]
   ClockSystem      PIN            DMA_Sx           USART           TIM          ...
   пишет           System::       System::         System::        System::
   System::        BB_RD/WR       GetTick()         GetTick()       GetTick()
   SystemCoreClock                (таймауты)        (таймауты)      (Delay, если нужно)
   и APBx/TIMx...
```

Три вещи, которые `System` даёт всем остальным драйверам:

1. **`SysStatus`/`SysInitStatus`** — два общих enum'а результата операции/инициализации,
   которые использует буквально каждый `.hpp` в `Drivers/src` (см. §7) — единый словарь
   "успех/ошибка/занято/таймаут" вместо своего набора кодов в каждом драйвере.
2. **Тактовые переменные** (`SystemCoreClock`, `APB1BusClock`, ...) — записываются
   [`ClockSystem::Init()`](RCC.md), читаются `UART`/`TIM`/`SPI` для расчёта делителей/BRR
   (см. §3).
3. **`System::GetTick()`/`Delay_ms()`/`Delay_us()`** — таймауты блокирующих операций
   (`USART::Send()`, `SPI::Send()`, `ClockSystem::Init()`'s HSE/PLL ожидания) все построены
   на одном и том же миллисекундном счётчике (§4).

---

## 2. System::Init() — что настраивается при старте

```cpp
static SysInitStatus Init();
```

Вызывается один раз в начале `main()`, до любого другого драйвера (см. `src/main.cpp`).
Настраивает низкоуровневые вещи, которые не относятся ни к одной конкретной периферии:

1. **FPU** (только Cortex-M4/M7 с `__FPU_PRESENT`/`__FPU_USED`) — `SCB->CPACR` даёт полный
   доступ к сопроцессорам CP10/CP11 (иначе первая же операция с `float`/`double` уйдёт в
   `UsageFault`).
2. **Flash-акселератор/кэш** — семейно-специфично:
   - F4: `PRFTEN` (prefetch) + `ICEN`/`DCEN` (инструкционный/данных кэш).
   - F7: `PRFTEN` + `ARTEN` (ART Accelerator) + `SCB_EnableICache()`/`SCB_EnableDCache()`
     (кэши самого ядра Cortex-M7, не Flash-контроллера) + включение тактирования `PWR`
     (нужно для voltage scaling, которым управляет [`ClockSystem`](RCC.md)).
   - G0: `PRFTEN` + `ICEN`.
   - L0: ничего — нет ART/кэша на этом ядре, wait states настраиваются иначе.
3. **Сброс тактовых переменных к HSI** — `SystemCoreClock`/`APB1BusClock`/... = `HSI_Clock`
   (16 МГц) до вызова [`ClockSystem::Init()`](RCC.md) — то есть сразу после `System::Init()`,
   но до `ClockSystem::Init()`, весь код видит корректные (хоть и не окончательные) значения
   частот, а не мусор.
4. **`InitTicks()`** — настраивает и запускает SysTick на `SystemCoreClock` (на данный момент
   ещё HSI) — см. §4. Если `ClockSystem::Init()` вызывается позже и меняет частоту, он сам
   вызывает `InitTicks()` повторно (см. [RCC.md §6](RCC.md#6-init--пошагово)), чтобы SysTick
   пересчитался под новую частоту.

```cpp
int main() {
    System::Init();                                    // FPU, Flash/cache, SysTick @ HSI
    ClockSystem::InitCalcPLL(180'000'000, ..., 8'000'000); // меняет частоту, сам вызывает InitTicks()
    System::Enable_CYCCNT();                            // для Delay_us(), см. §5
    ...
}
```

---

## 3. Тактовые переменные — общая память между RCC и всеми драйверами

```cpp
static uint32_t SystemCoreClock; // AHB/ядро/GPIO/DMA/USB
static uint32_t APB1BusClock;    // WWDG, SPI2/3, USART2/3, UART4/5, I2C1-3, CAN1/2, DAC
static uint32_t APB2BusClock;    // USART1/6, ADC1-3, SPI1/4, SYSCFG, SAI1/2
static uint32_t TIMxAPB1Clock;   // TIM2-7, TIM12-14 (с учётом правила "x2", см. RCC.md §2)
static uint32_t TIMxAPB2Clock;   // TIM1, TIM8-11
```

Это не константы, а обычные статические переменные — **`ClockSystem::Init()`/`InitCalcPLL()`
единственные места, которые их пишут** (см. [RCC.md](RCC.md)); все остальные драйверы только
читают:

- `USART::SetUp()` — `USARTx->BRR = *_info->bus_clk / BaudRate;` (где `bus_clk` указывает на
  `APB1BusClock` или `APB2BusClock` в зависимости от того, на какой шине сидит конкретный
  USART — см. [UART.md](UART.md)).
- `TIM::SetFrequency()` — то же самое через `_info->bus_clk`, только на `TIMxAPB1Clock`/
  `TIMxAPB2Clock` (см. [TIM.md](TIM.md)).
- `SPI` — расчёт `BaudRate` через делитель тактовой БД аналогично.

Так что порядок вызовов в `main()` важен: `ClockSystem::Init()` должен отработать **до**
`SetUp()` любого периферийного драйвера — иначе `USART::SetUp()` посчитает `BRR` от 16 МГц
(HSI, значение по умолчанию из `System::Init()`), а не от реальной частоты после PLL.

---

## 4. SysTick и миллисекундный счётчик

```cpp
static SysInitStatus InitTicks();   // SysTick_Config(SystemCoreClock / TICK_BASE)
static void          TickIncrease(); // вызывается из SysTick_Handler
static uint32_t      GetTick();
static void          Delay_ms(uint32_t delay);
```

`TICK_BASE = 1 kHz` — SysTick настроен на прерывание раз в 1 мс. Обработчик:

```cpp
extern "C" void SysTick_Handler(void) { System::TickIncrease(); }
```

инкрементирует единственный `static volatile uint32_t Tick` (модульная переменная, не поле
класса) — `GetTick()` просто читает её. На 32-битном ARM с выровненным адресом это чтение
атомарно само по себе, поэтому `GetTick()` не требует ни `__disable_irq()`, ни атомиков.

`Delay_ms()` — не `while(GetTick() < target)`, а через разность:

```cpp
uint32_t wait = (delay < 0xFFFFFFFFUL) ? delay + 1UL : delay;  // +1 компенсирует "неполный" первый тик
while ((GetTick() - tick_start) < wait) {}
```

Вычитание вместо прямого сравнения — это то же самое "wrap-around safe" рассуждение, что и в
[`TIM_HWCounter`](TIM.md#67-tim_hwcounter)/`Delay_us()` ниже: беззнаковое вычитание корректно
работает даже когда `Tick` успевает переполниться (после ~49.7 суток непрерывной работы) прямо
посреди ожидания — `target - tick_start` останется верным расстоянием в тиках, а `GetTick() <
target` в этот момент дало бы неверный результат. Все таймауты в проекте (`USART::Send`,
`ClockSystem::Init`'s HSE/PLL/CLOCKSWITCH таймауты, ...) используют этот же паттерн
`GetTick() - tick_start > timeout`, а не хранят абсолютный "дедлайн".

---

## 5. Микросекундная задержка через DWT CYCCNT

```cpp
#if not defined(STM32L0) && not defined(STM32G0)
static void     Enable_CYCCNT();
static void     Delay_us(uint32_t delay);
static uint32_t SWOTrace(const uint8_t* ptr, uint32_t len);
#endif
```

Доступно только там, где есть блок DWT (Cortex-M4/M7 — **не** на Cortex-M0/M0+, откуда и
условие `!L0 && !G0`). `Enable_CYCCNT()` включает трассировку (`DEMCR::TRCENA`) и сам счётчик
тактов (`DWT->CTRL::CYCCNTENA`) — обязателен до первого `Delay_us()`.

```cpp
void System::Delay_us(uint32_t delay) {
    uint32_t start = DWT->CYCCNT;
    uint32_t wait  = delay * (SystemCoreClock / 1'000'000UL);
    while ((DWT->CYCCNT - start) < wait) {}
}
```

Тот же приём "разность вместо сравнения с целью" из §4 — `DWT->CYCCNT` физически 32-битный и
переполняется каждые несколько десятков секунд на частотах в сотни МГц, `CYCCNT - start`
остаётся корректным сквозь это переполнение, прямое сравнение — нет.

`SWOTrace()` — побайтовая отправка в ITM/SWO (для трассировки через SWD, не связано с обычным
UART) — тоже требует включённого DWT/ITM, отсюда общий `#if` с `Delay_us`/`Enable_CYCCNT`.

---

## 6. DebugTrap — единая точка отказа для всех драйверов

```cpp
[[noreturn]] static void DebugTrap(const char* msg);
#ifdef DEBUG
static void SetDebugOutput(void (*fn)(const char*));
#endif
```

Единственный по всему проекту способ сообщить "это программная ошибка конфигурации, а не
рантайм-сбой периферии" — используется в конструкторах `USART`/`SPI`/`TIM_PWM`/... для проверки
"пин принадлежит не тому периферийному модулю" (см. [GPIO.md §6](GPIO.md#6-constexpr-таблицы-пинов-и-защита-от-опечаток)).

Поведение зависит от сборки:

- **Debug** (`DEBUG` определён CMake'ом): `msg` сохраняется в `volatile _debug_trap_msg`
  (видно в Watch/Memory окне отладчика даже без стека вызовов — полезно, если оптимизация
  инлайнит/уничтожает стек), затем вызывается зарегистрированный колбэк (если есть,
  `SetDebugOutput()`), затем `__BKPT(0)` + `while(1)`.
- **Release**: тело схлопывается в чистый `while(1)` — ни строковых литералов в Flash, ни
  брейкпоинта, ни вызова колбэка; `SetDebugOutput()` в этой сборке линкер вообще выкидывает.

```cpp
System::SetDebugOutput([](const char* s) {
    debug_usart.Send((uint8_t*)s, strlen(s));   // сообщение об ошибке уходит на терминал
});
```

Это единственный колбэк такого рода на весь проект — не нужно писать свой обработчик ошибок
конфигурации в каждом драйвере, они все зовут `System::DebugTrap()`.

---

## 7. Bit-banding и SysStatus/SysInitStatus

```cpp
enum class SysStatus     : uint8_t { OK, Error, Busy, NotInit, Timeout };
enum class SysInitStatus : uint8_t { NotInit, InitOK, InitError = 0xFF };
```

Общий словарь результатов для всех драйверов — `SysStatus` для рантайм-операций (`Send()`,
`Receive()`, ...), `SysInitStatus` для однократных настроек (`SetUp()`, `Init()`). Не самая
детализированная схема (нет отдельного кода "неверный аргумент" vs "устройство не найдено" —
оба варианта чаще всего `Error`/`InitError`), но единая на весь проект, так что вызывающему коду
не нужно помнить разные enum'ы для разных периферий.

`BIT_BB`/`BB_RD`/`BB_WR` — низкоуровневые функции адресной арифметики bit-banding
(`(addr - PERIPH_BASE) * 32 + bit * 4 + PERIPH_BB_BASE`), на которых построены
`PIN::SetLevel_BB()`/`GetLevel_BB()`/`TogglePin_BB()` — подробное объяснение самой техники
bit-banding и почему она доступна только на F4 — в [GPIO.md §5](GPIO.md#5-bit-banding-только-f4);
здесь эти функции просто определены (F4-only, `#if defined(STM32F4)`), так как исторически это
общесистемная, а не GPIO-специфичная возможность ядра.

---

## 8. Примеры использования

### Стандартная последовательность старта (как в main.cpp)

```cpp
int main() {
    System::Init();
    ClockSystem::InitCalcPLL(180'000'000, ClockSystem::PLL_ClockSource::HSE, 8'000'000);
    System::Enable_CYCCNT();   // требуется перед первым Delay_us()

    uint32_t tick = System::GetTick();
    for (;;) {
        if (System::GetTick() - tick > 500) {
            tick = System::GetTick();
            // раз в 500 мс...
        }
    }
}

extern "C" void NMI_Handler(void)       { while(1){} }
extern "C" void HardFault_Handler(void) { while(1){} }
```

### Микросекундная задержка

```cpp
System::Enable_CYCCNT();     // один раз
System::Delay_us(50);        // 50 мкс, блокирующая
```

### DebugTrap с выводом в UART (типично для отладочной сборки)

```cpp
#ifdef DEBUG
System::SetDebugOutput([](const char* s) {
    debug_uart.Send(reinterpret_cast<uint8_t*>(const_cast<char*>(s)), strlen(s));
});
#endif
```

---

## 9. Угловые случаи

- **Порядок вызовов важен**: `System::Init()` → (опционально) `System::SetDebugOutput()` →
  `ClockSystem::Init()`/`InitCalcPLL()` → `System::Enable_CYCCNT()` → `SetUp()` любых
  периферийных драйверов. Периферийный `SetUp()` до `ClockSystem::Init()` посчитает делители
  от HSI (16 МГц), а не от реальной частоты (см. §3).
- **`Enable_CYCCNT()`/`Delay_us()`/`SWOTrace()` недоступны на STM32L0/G0** — оба используют
  Cortex-M0/M0+, где нет блока DWT с `CYCCNT`. Вызов на этих целях — ошибка компиляции
  (функции просто не существуют в этой сборке), а не рантайм-сбой.
- **`Delay_ms(0xFFFFFFFF)`** — единственный случай, когда компенсирующий `+1` не добавляется
  (иначе `wait` переполнился бы в 0, и `Delay_ms` вернулся бы немедленно вместо максимально
  длинной задержки).
- **`DebugTrap()` в release-сборке не сохраняет `msg` нигде** — строка может быть даже не
  включена в бинарник компилятором (если `msg` не используется больше нигде), в отличие от
  debug-сборки, где `_debug_trap_msg` всегда указывает на неё.
- **`TickIncrease()` вызывается только из `SysTick_Handler`** — вызов из любого другого места
  не является ошибкой (это просто инкремент переменной), но нарушит соответствие между
  `GetTick()` и реальным временем.

---

## См. также

- [RCC.md](RCC.md) — единственный писатель тактовых переменных, описанных в §3.
- [GPIO.md](GPIO.md) — `BIT_BB`/`BB_RD`/`BB_WR` (§7) используются `PIN::*_BB()`-методами.
- [IRQ_Registry.md](IRQ_Registry.md) — независимый механизм диспетчеризации периферийных IRQ;
  `SysTick_Handler`/`NMI_Handler`/`HardFault_Handler` и подобные исключения Cortex-M сюда не
  входят и настраиваются отдельно (см. `src/main.cpp`).
- [UART.md](UART.md), [TIM.md](TIM.md), [SPI.md](SPI.md), [RTC.md](RTC.md), [DMA.md](DMA.md),
  [Flash.md](Flash.md) — все читают `SysStatus`/`SysInitStatus` (§7) и `System::GetTick()` (§4)
  из этого модуля.
