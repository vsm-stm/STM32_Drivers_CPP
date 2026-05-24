# IRQ Registry — подробное описание

## Содержание

1. [Зачем это нужно](#1-зачем-это-нужно)
2. [Архитектура](#2-архитектура)
3. [Файлы и их роли](#3-файлы-и-их-роли)
4. [Таблица _table — устройство](#4-таблица-_table--устройство)
5. [Порядок include и IRQ_MAX_SHARED](#5-порядок-include-и-irq_max_shared)
6. [Полная цепочка при срабатывании прерывания](#6-полная-цепочка-при-срабатывании-прерывания)
7. [Оценка задержки в тактах](#7-оценка-задержки-в-тактах)
8. [Сравнение с прямым подходом](#8-сравнение-с-прямым-подходом)
9. [Угловые случаи](#9-угловые-случаи)
10. [Добавление нового вектора](#10-добавление-нового-вектора)

---

## 1. Зачем это нужно

### Стандартный подход CMSIS

```c
// Жёстко привязано к конкретному объекту
extern "C" void USART1_IRQHandler(void) {
    my_usart.HandleIRQ();        // только этот объект, навсегда
}
extern "C" void SPI1_IRQHandler(void) {
    my_spi.HandleIRQ();
}
// ... десятки таких функций
```

**Проблемы:**
- Каждый обработчик — отдельная глобальная функция, привязанная к конкретному объекту.
- Два объекта на одном IRQ (общий DMA-вектор) — нет встроенного решения.
- Не позволяет менять обработчик в рантайме (например, при реконфигурации периферии).
- Весь ISR-код разбросан по разным файлам.

### Подход IRQ_Registry

```c
// Все стабы одинаковые — просто вызывают диспетчер
extern "C" void USART1_IRQHandler(void) { IRQ_Registry::Dispatch(USART1_IRQn); }
extern "C" void SPI1_IRQHandler(void)   { IRQ_Registry::Dispatch(SPI1_IRQn);   }
extern "C" void DMA1_Channel2_3_IRQHandler(void) { IRQ_Registry::Dispatch(DMA1_Channel2_3_IRQn); }
```

Кто будет обрабатывать — определяется при инициализации через `Register()`.
Обработчик можно переключить в рантайме. Два объекта на одном векторе — поддерживается нативно.

---

## 2. Архитектура

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           Cortex-M NVIC                                     │
│                                                                             │
│  Таблица векторов (адреса в Flash):                                         │
│  [0]  Reset_Handler                                                         │
│  ...  (15 системных исключений Cortex-M)                                   │
│  [16] WWDG_IRQHandler          ←─ периферийные IRQ начинаются здесь        │
│  [17] PVD_IRQHandler                                                        │
│  [18] RTC_TAMP_IRQHandler                                                   │
│  ...                                                                        │
│  [45] USART1_IRQHandler                                                     │
└──────────────────────┬──────────────────────────────────────────────────────┘
                       │ Прерывание → процессор прыгает на адрес из таблицы
                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                    ISR-стаб  (irq_registry_config.h)                        │
│                                                                             │
│  extern "C" void USART1_IRQHandler() {                                      │
│      IRQ_Registry::Dispatch(USART1_IRQn);   // USART1_IRQn = 27 (G0)       │
│  }                                                                          │
│                                                                             │
│  — Генерируется CMake из таблицы векторов МК                                │
│  — Все стабы одинаковые по структуре                                        │
└──────────────────────┬──────────────────────────────────────────────────────┘
                       │ Dispatch(27)
                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│           IRQ_Registry::_table[IRQ_TABLE_SIZE][IRQ_MAX_SHARED]              │
│                                                                             │
│  idx │ [0]                   │ [1]                                          │
│  ────┼───────────────────────┼───────────────────                           │
│   0  │ nullptr               │ nullptr    WWDG                              │
│   2  │ &_rtc_handler         │ nullptr    RTC_TAMP                          │
│   9  │ &NX_Top_spi *         │ nullptr    DMA1_Ch1     (эксклюзив)          │
│  10  │ &NX_Front_spi *       │ &ws_tim *  DMA1_Ch2_3  (ОБЩИЙ вектор)       │
│  25  │ &NX_Front_spi *       │ nullptr    SPI1                              │
│  26  │ &NX_Top_spi *         │ nullptr    SPI2                              │
│  27  │ &debug_usart          │ nullptr    USART1                            │
│  28  │ &USB_usart            │ nullptr    USART2                            │
│  ────┴───────────────────────┴───────────────────                           │
│  * — заполняется при вызове AttachDMA() / IRQ_en()                          │
└──────────────────────┬──────────────────────────────────────────────────────┘
                       │ вызов HandleIRQ() для каждого непустого слота
                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│              Объект-наследник IIRQHandler                                   │
│                                                                             │
│  class USART : public IIRQHandler {                                         │
│      void HandleIRQ() override final { /* чтение RDR, запись TDR, ... */ } │
│  };                                                                         │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Файлы и их роли

```
cmake/functions.cmake
  └─ stm32_generate_irq_handlers()
       ├─ считает IRQ_TABLE_SIZE из таблицы векторов МК
       ├─ выбирает IRQ_MAX_SHARED по семейству (G0→3, F4/F7→2)
       └─ генерирует irq_registry_config.h через configure_file()

cmsis-core/.../cmake/irq_registry_config.h.in    ← шаблон
  #define IRQ_TABLE_SIZE @IRQ_TABLE_SIZE@
  #define IRQ_MAX_SHARED @IRQ_MAX_SHARED@
  #include "irq_registry.hpp"
  IIRQHandler* IRQ_Registry::_table[IRQ_TABLE_SIZE][IRQ_MAX_SHARED] = {};
  extern "C" void XXX_IRQHandler() { Dispatch(XXX_IRQn); }
  ...

cmsis-core/.../Device/Include/irq_registry_config.h    ← сгенерированный файл
  #define IRQ_TABLE_SIZE 30   ← точное число векторов для STM32G030
  #define IRQ_MAX_SHARED 3    ← максимум для G0
  (подставлены все стабы)

Drivers/src/irq_registry.hpp    ← драйвер, не трогается CMake
  #ifndef IRQ_MAX_SHARED
  #  define IRQ_MAX_SHARED 3   ← дефолт для direct-include
  #endif
  class IRQ_Registry {
      static constexpr int MAX_PER_IRQ = IRQ_MAX_SHARED;
      static IIRQHandler* _table[][MAX_PER_IRQ];   ← незакрытый первый индекс
  };

Drivers/src/irq_registry.cpp    ← реализация Register/Unregister/Dispatch
```

### Почему первый индекс _table не задан в заголовке

В C++ допустимо объявить статический массив с незакрытым первым измерением:

```cpp
// hpp — объявление
static IIRQHandler* _table[][MAX_PER_IRQ];   // [] — неполный тип, OK

// cpp (через irq_registry_config.h) — определение
IIRQHandler* IRQ_Registry::_table[IRQ_TABLE_SIZE][MAX_PER_IRQ] = {};  // задаём размер
```

Это позволяет `IRQ_TABLE_SIZE` жить только в сгенерированном файле, а не в заголовке драйвера.

---

## 4. Таблица _table — устройство

```
_table[IRQ_TABLE_SIZE][IRQ_MAX_SHARED]

  IRQn_Type (enum) кастуется в int → это индекс строки
  Каждая строка — до IRQ_MAX_SHARED указателей на обработчики

  Пример для STM32G030 (IRQ_TABLE_SIZE=30, IRQ_MAX_SHARED=3):

  _table[30][3]  →  30 * 3 * 4 байта = 360 байт в .bss (нули при старте)

  Столбцы заполняются в порядке регистрации:
  Register(IRQn, A)  →  _table[IRQn][0] = A
  Register(IRQn, B)  →  _table[IRQn][1] = B
  Register(IRQn, C)  →  _table[IRQn][2] = C
  Register(IRQn, D)  →  BKPT! (все слоты заняты)
```

### IRQ_MAX_SHARED по семействам

| Семейство | Значение | Причина |
|-----------|----------|---------|
| STM32G0 | **3** | `DMA1_Ch4_5_DMAMUX1_OVR_IRQn` = ch4 + ch5 + DMAMUX overrun |
| STM32F4 | **2** | Пары таймеров: `TIM1_UP_TIM10`, `TIM8_UP_TIM13`, ... DMA имеет отдельные векторы |
| STM32F7 | **2** | Аналогично F4 |
| Default | **2** | Безопасное значение |

Общий максимум по всем семействам = **3** (используется как дефолт в `irq_registry.hpp`).

---

## 5. Порядок include и IRQ_MAX_SHARED

Проблема: `irq_registry.hpp` нужно знать `IRQ_MAX_SHARED`, но он не может включать `irq_registry_config.h` (это создаст циклическую зависимость).

Решение: `IRQ_MAX_SHARED` определяется **до** `#include "irq_registry.hpp"` в config-файле.

```
Путь 1: tim.cpp  →  tim.hpp  →  irq_registry.hpp
                                   ├─ IRQ_MAX_SHARED не определён
                                   ├─ #ifndef IRQ_MAX_SHARED
                                   ├─ #  define IRQ_MAX_SHARED 3   ← дефолт
                                   └─ MAX_PER_IRQ = 3

Путь 2: irq_registry.cpp  →  irq_registry_config.h
                                   ├─ #define IRQ_TABLE_SIZE 30
                                   ├─ #define IRQ_MAX_SHARED 3    ← CMake
                                   └─ #include "irq_registry.hpp"
                                             ├─ IRQ_MAX_SHARED уже = 3
                                             ├─ #ifndef → пропускается
                                             └─ MAX_PER_IRQ = 3   ← из config
```

В обоих путях `MAX_PER_IRQ = 3` — объявление и определение `_table` совпадают.

---

## 6. Полная цепочка при срабатывании прерывания

### Сценарий: SPI1 DMA-передача завершена (канал 2)

```
Аппаратная часть:
  ┌──────────────────────────────────────────────────────────┐
  │  DMA1_Channel2 завершил передачу                         │
  │  → устанавливает TCIF2 в DMA1->ISR (бит 5)              │
  │  → сигнализирует NVIC о DMA1_Channel2_3_IRQn (idx=10)   │
  └────────────────────────┬─────────────────────────────────┘
                           │
  Cortex-M аппаратный стек (12 циклов):
  ┌──────────────────────────────────────────────────────────┐
  │  Сохраняет в стек: xPSR, PC, LR, r12, r3, r2, r1, r0   │
  │  Загружает PC из таблицы векторов → DMA1_Channel2_3_IRQHandler │
  └────────────────────────┬─────────────────────────────────┘
                           │
  ISR-стаб:
  ┌──────────────────────────────────────────────────────────┐
  │  extern "C" void DMA1_Channel2_3_IRQHandler() {          │
  │      IRQ_Registry::Dispatch(DMA1_Channel2_3_IRQn);       │
  │  }                                                       │
  │  ≈ 2 инструкции: MOVS r0, #10  / B Dispatch             │
  └────────────────────────┬─────────────────────────────────┘
                           │
  Dispatch(idx=10):
  ┌──────────────────────────────────────────────────────────┐
  │  if (idx < 0 || idx >= IRQ_TABLE_SIZE) return;           │
  │      // 10 < 30 → OK                                     │
  │                                                          │
  │  s=0: h = _table[10][0] = &NX_Front_spi  (не null)      │
  │       → NX_Front_spi.HandleIRQ()                        │
  │                                                          │
  │  s=1: h = _table[10][1] = &ws_tim        (не null)      │
  │       → ws_tim.HandleIRQ()                              │
  │                                                          │
  │  s=2: h = _table[10][2] = nullptr → break               │
  └────────┬───────────────────────────┬─────────────────────┘
           │                           │
           ▼                           ▼
  NX_Front_spi.HandleIRQ()        ws_tim.HandleIRQ()
  ┌─────────────────────────┐     ┌─────────────────────────┐
  │ GetTC_Flag():           │     │ GetTC_Flag():           │
  │ DMA1->ISR & TCIF2       │     │ DMA1->ISR & TCIF3       │
  │ = 1  → ЭТО МОЙ КАНАЛ   │     │ = 0  → не мой → return  │
  │                         │     └─────────────────────────┘
  │ Stream_EN(DISABLE)      │
  │ ClearFlags()  (TCIF2=0) │
  │ _dma_busy = false       │
  │ OnDmaTxComplete()       │
  └─────────────────────────┘
                           │
  Возврат из прерывания:
  ┌──────────────────────────────────────────────────────────┐
  │  Cortex-M восстанавливает из стека: xPSR, PC, LR, ...   │
  │  Продолжение выполнения основного кода                   │
  └──────────────────────────────────────────────────────────┘
```

### Сценарий: WS2812B передача завершена (канал 3)

```
DMA1_Channel3 → TCIF3=1 → NVIC → ISR-стаб → Dispatch(10)

  s=0: NX_Front_spi.HandleIRQ()
       GetTC_Flag() → TCIF2 = 0 → не мой → return   (1 чтение DMA1->ISR)

  s=1: ws_tim.HandleIRQ()
       GetTC_Flag() → TCIF3 = 1 → МОЙ КАНАЛ
       Stream_EN(DISABLE)
       ClearFlags()
       TIMx->DIER &= ~TIM_DIER_UDE   // отключаем DMA-запрос таймера
       _dma_busy = false
       _dma_cb()                      // on_transfer_done()
```

---

## 7. Оценка задержки в тактах

### Что происходит с момента сигнала до первой инструкции HandleIRQ

#### Фиксированная аппаратная часть (одинакова для ВСЕХ подходов)

| Фаза | Cortex-M0/M0+ (G0) | Cortex-M4 (F4) | Cortex-M7 (F7) |
|------|--------------------|-----------------|--------------------|
| Сохранение контекста (8 регистров в стек) | **12 тактов** | **12 тактов** | **12 тактов** |
| Загрузка PC из таблицы векторов | включено в 12 | включено в 12 | включено в 12 |
| **Итого аппаратная задержка** | **12** | **12** | **12** |

> На M4/M7 с FPU и lazy stacking — те же 12 тактов, FPU-регистры сохраняются лениво только при использовании.

#### Программная часть: IRQ_Registry overhead

Стаб — это просто точка входа из вектора. Он не добавляет "фазу сам по себе": в любом
подходе NVIC прыгает на какую-то функцию. Overhead стаба — только вызов Dispatch
(`MOVS r0 + B.W` = 3 такта на M0, 1-2 такта на M4/M7). Именно поэтому он входит в строку
Dispatch, а не выделяется отдельно.

```
Стаб (точка входа из вектора — одинакова в любом подходе):
  MOVS r0, #<IRQn>             1 такт
  B.W  IRQ_Registry::Dispatch  2 такта (M0) / 1 такт (M4/M7)
  ↑ это и есть ВЕСЬ overhead стаба

Dispatch():
  Bounds check (CMP + BLT)  2 такта
  Вычисление адреса _table[idx][0]:
    idx * IRQ_MAX_SHARED * 4   3-5 тактов (M0: нет одноциклового MUL)
                                1-2 такта  (M4/M7: MUL за 1 такт)
  LDR h = _table[idx][0]      2 такта (SRAM, 0 wait states)
  CBZ h, skip                  1 такт
  Виртуальный вызов h->HandleIRQ():
    LDR r1, [h]                2 такта (загрузка vtable pointer)
    LDR r2, [r1, #offset]      2 такта (указатель на функцию из vtable)
    BLX r2                     2-3 такта
  Если slot[1] не null — ещё один такой цикл
```

| Фаза | M0/M0+ (G0, 64 МГц) | M4 (F4, 168 МГц) | M7 (F7, 216 МГц) |
|------|---------------------|------------------|------------------|
| Вызов Dispatch из стаба (BL) | 3 / **47 нс** | 2 / **12 нс** | 1 / **5 нс** |
| Bounds check | 2 / **31 нс** | 2 / **12 нс** | 1 / **5 нс** |
| Адресация _table[idx][s] | 5 / **78 нс** | 2 / **12 нс** | 2 / **9 нс** |
| Загрузка указателя из SRAM | 2 / **31 нс** | 2 / **12 нс** | 1 / **5 нс** |
| Null-check + branch | 2 / **31 нс** | 1 / **6 нс** | 1 / **5 нс** |
| Виртуальный вызов (vtable) | 6 / **94 нс** | 4 / **24 нс** | 3 / **14 нс** |
| **Итого Dispatch overhead** | **~20 тактов / ~310 нс** | **~13 тактов / ~77 нс** | **~9 тактов / ~42 нс** |
| **+ аппаратная задержка** | **+12 / +188 нс** | **+12 / +71 нс** | **+12 / +56 нс** |
| **Полная задержка до HandleIRQ** | **~32 такта / ~500 нс** | **~25 тактов / ~148 нс** | **~21 такт / ~97 нс** |

> Цифры приближённые — зависят от выравнивания кода, состояния кэша (M7), конвейера.
> M0 не имеет конвейера ветвлений и одноцикловых умножений — все числа пессимистичны.

#### Дополнительный overhead для второго обработчика (shared IRQ)

Если первый обработчик **не сработал** (его флаг не установлен), `HandleIRQ()` выполняет одно чтение регистра ISR и `return`. Это примерно:

| МК | Цена "холостого" HandleIRQ |
|----|---------------------------|
| M0 (G0) | ~8-12 тактов (~125-188 нс) |
| M4 (F4) | ~4-6 тактов (~24-36 нс) |
| M7 (F7) | ~3-4 такта (~14-19 нс) |

---

## 8. Сравнение с прямым подходом

### Вариант A: прямой вызов без виртуальности (C-стиль)

```c
extern "C" void USART1_IRQHandler(void) {
    // прямой код обработки
    if (USART1->ISR & USART_ISR_RXNE) { buf[idx++] = USART1->RDR; }
}
```

| Фаза | M0 (G0) | M4 (F4) | M7 (F7) |
|------|---------|---------|---------|
| Аппаратная задержка | 12 | 12 | 12 |
| Программный overhead | **0** | **0** | **0** |
| **Итого** | **12** | **12** | **12** |

Минимально возможная задержка. Но: код неудобен, не переиспользуем.

### Вариант B: прямой вызов метода (без реестра)

```cpp
extern "C" void USART1_IRQHandler(void) {
    my_usart.HandleIRQ();   // прямой вызов, HandleIRQ() — виртуальный
}
```

| Фаза | M0 (G0) | M4 (F4) | M7 (F7) |
|------|---------|---------|---------|
| Аппаратная задержка | 12 | 12 | 12 |
| Вызов HandleIRQ (виртуальный) | ~6 | ~4 | ~3 |
| **Итого** | **~18** | **~16** | **~15** |

### Вариант C: IRQ_Registry (текущий подход)

| Фаза | M0 (G0) | M4 (F4) | M7 (F7) |
|------|---------|---------|---------|
| Аппаратная задержка | 12 | 12 | 12 |
| Dispatch + vtable (через стаб) | ~20 | ~13 | ~9 |
| **Итого** | **~32** | **~25** | **~21** |

### Итоговая таблица: дополнительные такты IRQ_Registry vs прямой виртуальный вызов

```
                 M0/G0 (64 МГц)    M4/F4 (168 МГц)    M7/F7 (216 МГц)
                 ──────────────    ───────────────    ───────────────
  Overhead        ~14 тактов         ~9 тактов          ~6 тактов
  Время           ~219 нс            ~54 нс             ~28 нс
  % от 1 мс       0.022%             0.005%             0.003%
```

**Вывод:** накладные расходы IRQ_Registry составляют менее 0.025% от типичного периода задачи (1 мс).
Это значимо только для прерываний с частотой выше 100 кГц при работе на M0 — например, прямой DMA-less UART на 4 Мбод.
В остальных случаях overhead пренебрежимо мал.

### Когда overhead важен

| Сценарий | Частота IRQ | G0 overhead | Критично? |
|----------|-------------|-------------|-----------|
| UART 115200 | 11520 Гц | 2.5 мкс/с | Нет |
| SPI 10 МГц (байт IRQ) | 10 МГц | 2190 мкс/с (0.2% CPU) | Нет |
| WS2812B DMA TC | 1 Гц | 219 нс/с | Нет |
| Encoder 1 МГц | 1 МГц | 219 мкс/с (0.02% CPU) | Нет |
| ADC непрерывный 1 MSPS | 1 МГц | 219 мкс/с | На грани |

---

## 9. Угловые случаи

### 9.1 Один объект в нескольких слотах (TIM1)

TIM1 на G0 имеет ДВА отдельных вектора: `TIM1_BRK_UP_TRG_COM` и `TIM1_CC`.
`IRQ_en()` регистрирует один и тот же объект в оба слота:

```cpp
Register(TIM1_BRK_UP_TRG_COM_IRQn, this);  // _table[13][0] = this
Register(TIM1_CC_IRQn,              this);  // _table[14][0] = this
```

С **двумерным массивом** это безопасно: у каждого IRQn — своя независимая строка.
Один объект может стоять в любом количестве строк одновременно.

Почему **intrusive linked list (`_irq_next`)** ломается в этом случае:

```
Register(TIM1_BRK_UP, ws_tim):  _table[13] = ws_tim, ws_tim._next = null
Register(TIM1_CC,     ws_tim):  _table[14] = ws_tim, ws_tim._next = null  ← OK пока

Register(DMA_ch23,    spi):     _table[10] = spi, spi._next = null
Register(DMA_ch23,    ws_tim):  spi._next = ws_tim, ws_tim._next = null   ← OK

// Кто-то регистрирует ещё одного для TIM1_BRK_UP:
Register(TIM1_BRK_UP, third):  ws_tim._next = third  ← ИСПОРЧЕНО!

// Теперь DMA dispatch:
Dispatch(DMA_ch23):
  slot[0] = spi    → spi.HandleIRQ()    ← корректно
  slot[1] = ws_tim → ws_tim.HandleIRQ() ← корректно
  сле адующий по _next = third          ← НЕТ! third зарегистрирован для TIM1, не DMA!
```

**Двумерный массив** этой проблемы лишён полностью: каждая строка _table независима.

### 9.2 Shared DMA вектор (DMA1_Channel2_3)

```
Регистрация:
  NX_Front_spi.AttachDMA(&dma_nx_front_tx) → Register(DMA1_Ch2_3_IRQn, &NX_Front_spi)
  ws_tim.AttachDMA(&dma_ws)                → Register(DMA1_Ch2_3_IRQn, &ws_tim)

  _table[10][0] = &NX_Front_spi
  _table[10][1] = &ws_tim

Dispatch(10):
  Вызываем [0] и [1] всегда.
  Каждый проверяет СВОЙ бит в DMA1->ISR — лишний вызов стоит ~10 тактов.
```

### 9.3 DMAMUX1 Overrun (G0 специфика)

Вектор `DMA1_Ch4_5_DMAMUX1_OVR_IRQn` объединяет три источника.
`IRQ_MAX_SHARED = 3` позволяет зарегистрировать все три:

```cpp
Register(DMA1_Ch4_5_DMAMUX1_OVR_IRQn, &spi_handler);    // slot[0]
Register(DMA1_Ch4_5_DMAMUX1_OVR_IRQn, &i2c_handler);    // slot[1]
Register(DMA1_Ch4_5_DMAMUX1_OVR_IRQn, &dmamux_handler); // slot[2]
```

---

## 10. Добавление нового вектора

### Если CMake-генерация работает

Просто запустить CMake — он автоматически добавит стаб для нового вектора
если он появится в таблице векторов МК.

### Ручное добавление (без cmake)

**Шаг 1** — добавить стаб в `irq_registry_config.h`:
```cpp
extern "C" void NEW_PERIPH_IRQHandler() {
    IRQ_Registry::Dispatch(NEW_PERIPH_IRQn);
}
```

**Шаг 2** — проверить `IRQ_TABLE_SIZE`:
```
NEW_PERIPH_IRQn должен быть < IRQ_TABLE_SIZE
Если нет — увеличить #define IRQ_TABLE_SIZE в config-файле
```

**Шаг 3** — регистрация в драйвере (делается автоматически через `IRQ_en()`):
```cpp
IRQ_Registry::Register(NEW_PERIPH_IRQn, this);
NVIC_EnableIRQ(NEW_PERIPH_IRQn);
```

### Если нужен третий обработчик на общем векторе

`IRQ_MAX_SHARED = 3` уже поддерживает это. Просто вызвать `Register` ещё раз — слот [2] заполнится.
Если все три уже заняты — при попытке зарегистрировать четвёртый сработает `BKPT`.

---

## Жизненный цикл обработчика

```
Инициализация                        Прерывание                    Деинициализация
─────────────                        ──────────                    ───────────────

uart.SetUp()                         NVIC fires                    uart.IRQ_en(RXNE, DISABLE)
  └─ lookup в usart_table               │                            ├─ CR1 &= ~RXNEIE
       │                               ▼                            ├─ все CR1 IRQ bits == 0
       └─ _info → irq = USART1_IRQn  ISR-стаб                      ├─ Unregister(USART1_IRQn)
                                        │                            │   _table[27] = {0,0,0}
uart.IRQ_en(RXNE, ENABLE)             Dispatch(27)                  └─ NVIC_DisableIRQ(USART1_IRQn)
  ├─ CR1 |= RXNEIE                      │
  ├─ NVIC_GetEnableIRQ() == 0         _table[27][0] = &uart
  │   (первый раз)                      │
  ├─ Register(USART1_IRQn, this)       uart.HandleIRQ()
  │   _table[27][0] = &uart              ├─ ISR & RXNE → читаем RDR
  └─ NVIC_EnableIRQ(USART1_IRQn)       └─ return из ISR
```
