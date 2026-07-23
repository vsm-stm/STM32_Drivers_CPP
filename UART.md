# UART / USART — подробное описание

## Содержание

1. [Зачем это нужно](#1-зачем-это-нужно)
2. [Абстракция регистров между семействами](#2-абстракция-регистров-между-семействами)
3. [Файлы и роли](#3-файлы-и-роли)
4. [Три режима передачи](#4-три-режима-передачи)
5. [IRQ_en() — регистрация по требованию](#5-irq_en--регистрация-по-требованию)
6. [HandleIRQ() — один обработчик на UART и на DMA](#6-handleirq--один-обработчик-на-uart-и-на-dma)
7. [IDLE-line — приём переменной длины](#7-idle-line--приём-переменной-длины)
8. [DMA-интеграция](#8-dma-интеграция)
9. [FIFO (только STM32G0)](#9-fifo-только-stm32g0)
10. [Виртуальные колбэки — расширение без переписывания HandleIRQ](#10-виртуальные-колбэки--расширение-без-переписывания-handleirq)
11. [Примеры использования](#11-примеры-использования)
12. [Угловые случаи](#12-угловые-случаи)

---

## 1. Зачем это нужно

`USART` — один класс на один аппаратный USART/UART, работающий на STM32G0/F4/F7 без изменений в
коде приложения. Как и `TIM`/`RCC`, использует паттерн "таблица фактов о периферии, найти строку
по указателю на регистры" (`usart_table[]`, см. раздел 3) вместо `switch(USARTx)` в каждом методе.

Класс наследует [`IIRQHandler`](IRQ_Registry.md) — регистрируется в `IRQ_Registry` автоматически,
как только приложение включает первый источник прерывания (`IRQ_en()`, см. раздел 5); ни один метод
`IRQ_Registry::Register()` вручную вызывать не нужно.

```cpp
USART debug(USART1, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7);
debug.SetUp();
debug.Send(buf, len, /*timeout_ms=*/1000);
```

---

## 2. Абстракция регистров между семействами

STM32F4 использует один `DR` (data register) и на чтение, и на запись, статус — в `SR`, флаги
сбрасываются записью в тот же `SR`. STM32F7 и STM32G0 разделяют `DR` на `TDR`/`RDR`, статус — в
`ISR` (read-only), а сброс флагов — через отдельный `ICR`. Чтобы весь остальной код driver'а
(`Send`, `Receive`, `HandleIRQ`, ...) не разветвлялся на `#if` в каждом методе, разница целиком
спрятана в пяти приватных инлайн-акцессорах (`uart_defs.hpp`, "Section A"):

```cpp
volatile uint32_t& TXD();        // USARTx->DR   (F4)  или USARTx->TDR (F7/G0)
volatile uint32_t& RXD();        // USARTx->DR   (F4)  или USARTx->RDR (F7/G0)
volatile uint32_t& Status_reg(); // USARTx->SR   (F4)  или USARTx->ISR (F7/G0)
volatile uint32_t& Clear_reg();  // USARTx->SR   (F4)  или USARTx->ICR (F7/G0)
```

Плюс набор `constexpr` масок `ISR_TXE`/`ISR_RXNE`/`ISR_IDLE`/`ISR_TC`, которые указывают на
семейно-специфичное имя бита (`USART_SR_TXE` на F4, `USART_ISR_TXE` на F7,
`USART_ISR_TXE_TXFNF` на G0 — на G0 бит "TXE" и "TX FIFO not full" физически один и тот же бит,
так что макрос переиспользуется под обоими смыслами). Всё, что выше этих пяти строк
(`Send()`, `Receive()`, `HandleIRQ()`, ...) написано один раз и компилируется одинаково на всех
трёх семействах.

---

## 3. Файлы и роли

```
uart.hpp        — класс USART: публичный API, приватные поля состояния
uart_defs.hpp   — все таблицы, подключается дважды с разными #define (двухсекционный файл):
  Section A (внутри тела класса USART, guard: UART_H_ && !UART_DEFS_CPP)
    ├─ USART::_1::TX/RX ... _8::TX/RX  — constexpr PIN-таблицы (TX/RX по номеру периферии)
    ├─ FIFO / FIFO_TH                   — enum'ы (реальные значения только на G0, на F4/F7 = NO-заглушки)
    └─ TXD()/RXD()/Status_reg()/...     — приватные акцессоры из раздела 2
  Section B (файловая область видимости внутри uart.cpp, guard: UART_DEFS_CPP)
    ├─ USART::usart_table[]             — periph → {clk_reg, clk_bit, bus_clk, irq}
    └─ uart_dma_req_table[]             — periph → {tx_req, rx_req} для DMAMUX/CHSEL
uart.cpp        — реализация: SetUp/Send/Receive/*_IRQ/*_DMA/AttachDMA/HandleIRQ/On*
```

`usart_table` — как `tim_table` в TIM: одна строка на периферию, `SetUp()` ищет её линейным
поиском по указателю `USARTx` и сохраняет результат в `_info`. Все остальные методы читают факты
через `_info` (`*_info->bus_clk`, `_info->irq`), а не хардкодят их.

---

## 4. Три режима передачи

| Режим | Методы | Как определяется завершение |
|-------|--------|------------------------------|
| **Блокирующий** | `Send()`, `Receive()` | Опрос `TXE`/`RXNE` в цикле, с таймаутом по `System::GetTick()` на каждый байт |
| **По прерываниям (IRQ)** | `Send_IRQ()`, `Receive_IRQ()` | `OnTxEmpty()`/`OnRxByte()` вызываются из `HandleIRQ()`, сами гасят своё прерывание, когда буфер исчерпан |
| **DMA** | `Send_DMA()`, `Receive_DMA()` (после `AttachDMA()`) | `OnDmaTxComplete()`/`OnDmaRxComplete()` по флагу TC на DMA-линии |

Блокирующий режим не использует `IRQ_Registry` вообще — чистый polling. IRQ- и DMA-режимы
используют один и тот же механизм регистрации (`IRQ_en()`/`AttachDMA()`), поэтому их можно
свободно сочетать (например, TX через DMA, RX через IRQ) на одном и том же объекте `USART`.

---

## 5. IRQ_en() — регистрация по требованию

```cpp
inline void IRQ_en(IRQ irq, FunctionalState en)
```

Тот же паттерн "auto-register на первом включении / auto-unregister на последнем выключении",
что и в `TIM::IRQ_en()` (см. [TIM.md](TIM.md)):

1. Установить/снять один бит в `CR1` (F4/F7) или в `CR1`/`CR3` в зависимости от источника (G0 —
   `TXFIFO`/`RXFIFO` живут в `CR3`, остальные в `CR1`).
2. Проверить, остались ли **вообще** включённые источники прерывания на этом USART
   (`all_cleared`).
3. Если это **первое** включение (`NVIC_GetEnableIRQ() == 0`) — `IRQ_Registry::Register(irq, this)`
   + `NVIC_EnableIRQ()`.
4. Если это **последнее** выключение (`all_cleared`) — `IRQ_Registry::Unregister()` +
   `NVIC_DisableIRQ()`.

Так что `Send_IRQ()`/`Receive_IRQ()`/`Receive_DMA()` (для IDLE) могут свободно вызывать
`IRQ_en(..., ENABLE)`/`IRQ_en(..., DISABLE)` в любом порядке — регистрация в `IRQ_Registry`
происходит ровно один раз за "сессию" активности прерываний, а не при каждом вызове.

---

## 6. HandleIRQ() — один обработчик на UART и на DMA

`USART::HandleIRQ()` вызывается из `Dispatch()` [IRQ_Registry](IRQ_Registry.md) на **любой** из
линий, на которых объект зарегистрирован — сам UART-вектор, и (если `AttachDMA()` вызывался)
DMA TX/RX вектора тоже указывают на этот же `this`. Порядок проверок внутри одной функции:

```
HandleIRQ():
  1. _dma_tx: если TC-флаг стоит — остановить стрим, очистить флаги, OnDmaTxComplete()
  2. _dma_rx: аналогично — OnDmaRxComplete()
  3. цикл "пока RXNE (или RXFT на G0)": OnRxByte(RXD())  — вычитывает все накопленные байты разом
  4. IDLE:  очистить флаг, OnIdle()
  5. TC:    очистить флаг, OnTC()
  6. TXE (или TXFT на G0): OnTxEmpty()
```

Каждый пункт независим — если сработала только DMA TC, шаги 3-6 просто не находят своих флагов
и ничего не делают (дешёвая проверка бита, не полноценный обработчик). Это тот же принцип, что и
"проверять все 4 CC-флага безусловно" в `TIM::HandleIRQ()` — дешевле, чем городить отдельные
пути по источнику вектора.

**Почему F4 обрабатывает IDLE иначе**: на F4 флаг `IDLE` в `SR` сбрасывается характерной
последовательностью "прочитать `SR`, затем прочитать `DR`" (аппаратная особенность регистра, а
не выбор этого драйвера) — поэтому в ветке F4 явно стоит `(void)USARTx->DR;` перед `OnIdle()`.
На F7/G0 сброс идёт через `ICR` как обычно.

---

## 7. IDLE-line — приём переменной длины

Когда длина входящих данных заранее не известна (например, NMEA-строка переменной длины),
`Receive_DMA()`/`Receive_IRQ()` включают ещё и `IRQ::IDLE` в дополнение к TC/RXNE:

```cpp
SysStatus USART::Receive_DMA(uint8_t* data, uint32_t len) {
    ...
    _dma_rx->SetCount(len);        // "не более len" — максимум, не точная длина
    DMA_en(ENABLE, DMA::RX);
    _dma_rx->Stream_EN(ENABLE);
    IRQ_en(IRQ::IDLE, ENABLE);     // остановить приём раньше, если линия замолчала
    return SysStatus::OK;
}
```

Когда линия простаивает дольше одного символьного времени, аппаратно устанавливается флаг
`IDLE`, вызывается `OnIdle()`:

```cpp
void USART::OnIdle() {
    if (rx_status == SysStatus::Busy) {
        if (_dma_rx) {
            uint32_t remaining = _dma_rx->GetCount();      // сколько НЕ пришло из запрошенных len
            _dma_rx->Stream_EN(DISABLE);
            ...
            data_received_count = rx_data.size - remaining; // фактически принято
            data_received = true;
            rx_status = SysStatus::OK;
        } else { /* IRQ-режим — просто завершить приём как есть */ }
    }
    IRQ_en(IRQ::IDLE, DISABLE);
}
```

Так фактическая длина сообщения (`GetDataReceivedCount()`) вычисляется как `len - remaining`, а
не требует заранее знать точный размер — приём останавливается либо по достижении `len`
(`OnDmaRxComplete()`), либо по паузе на линии (`OnIdle()`), что бы не наступило раньше.

---

## 8. DMA-интеграция

```cpp
void AttachDMA(DMA_Sx* tx = nullptr, DMA_Sx* rx = nullptr);
```

1. Ищет `tx_req`/`rx_req` (DMAMUX ID на G0, CHSEL на F4/F7) в `uart_dma_req_table` по указателю
   `USARTx` — так вызывающему коду не нужно самому подбирать номер запроса.
2. Для каждого переданного канала настраивает `DMA_Sx::StreamSettings` (направление,
   периферийный адрес `TDR`/`RDR` или `DR` на F4, ширина `Byte`, `minc = true`) и вызывает
   `dma->SetUp(cfg)`.
3. Регистрирует **этот же `USART`-объект** (`this`) как обработчик DMA TC-прерывания через
   `IRQ_Registry::Register(dma->GetIRQn(), this)` — отдельного класса-адаптера для DMA не
   заводится, `USART::HandleIRQ()` сам знает, как отличить DMA TC от UART-флагов (раздел 6).
4. Если TX и RX используют один и тот же вектор (`tx_irqn == rx_irqn` — возможно на МК с малым
   числом DMA-векторов), регистрация для RX **пропускается** — иначе один и тот же `this` попал
   бы в оба слота `IRQ_MAX_SHARED` одной строки `_table` без необходимости (см.
   [IRQ_Registry.md, §9.1](IRQ_Registry.md#91-один-объект-на-нескольких-векторах)).

`AttachDMA()` обязателен только для `Send_DMA()`/`Receive_DMA()` — блокирующий и IRQ-режимы его
не требуют вообще.

---

## 9. FIFO (только STM32G0)

STM32G0's USART имеет аппаратный FIFO на TX и RX (STM32F4/F7 в этом проекте — нет, `FIFO`/
`FIFO_TH` там вырождаются в единственное значение `NO`). Когда `SetUp(FIFO::EN, ...)` включает
FIFO, биты прерываний меняют смысл — вместо "регистр пуст/полон на один байт" используются пороги
заполненности (`FIFO_TH`: `1/8`, `1/4`, `1/2`, `3/4`, `7/8`, `all`), и `IRQ_en()`/`HandleIRQ()`
переключаются на `TXFIFO`/`RXFIFO`-биты вместо `TXE`/`RXNE`:

```cpp
#if defined(STM32G0)
    if (USARTx->CR1 & USART_CR1_FIFOEN) IRQ_en(IRQ::TXFIFO, ENABLE);
    else                                 IRQ_en(IRQ::TXE, ENABLE);
#endif
```

Физически `TXE`/`TXFT` (и `RXNE`/`RXFT`) занимают один и тот же бит регистра на G0 — маски
`ISR_TXE` (= `USART_ISR_TXE_TXFNF`) и `ISR_TXFT` разные имена для разных случаев, не два
независимых флага.

---

## 10. Виртуальные колбэки — расширение без переписывания HandleIRQ

```cpp
virtual void OnRxByte(uint8_t);
virtual void OnTxEmpty();
virtual void OnIdle();
virtual void OnTC();
virtual void OnDmaTxComplete();
virtual void OnDmaRxComplete();
```

Все шесть — приватные виртуальные методы с реализацией по умолчанию (буферизация в `tx_data`/
`rx_data`, подсчёт `data_received_count`/`data_overflow_count`). `HandleIRQ()` в базовом классе
финальный (`override final`) и переопределять его не нужно — вместо этого наследник `USART`
переопределяет только нужный колбэк, чтобы, например, парсить протокол побайтово прямо в
`OnRxByte()`, не трогая логику определения TC/IDLE/DMA.

---

## 11. Примеры использования

### Блокирующая отправка/приём

```cpp
USART dbg(USART3, 115200, USART::_3::TX::PD8, USART::_3::RX::PD9);
dbg.SetUp();
uint8_t msg[] = "hello\r\n";
dbg.Send(msg, sizeof(msg) - 1, /*timeout_ms=*/100);

uint8_t rx[4];
if (dbg.Receive(rx, sizeof(rx), 1000) == SysStatus::Timeout) { /* ... */ }
```

### Приём фиксированной длины по прерываниям

```cpp
USART dbg(USART1, 115200, USART::_1::TX::PA9, USART::_1::RX::PA10);
dbg.SetUp();
dbg.Receive_IRQ(buf, sizeof(buf));   // возвращается сразу, IRQ_Registry регистрируется внутри
NVIC_EnableIRQ(...);                 // не нужен явно — IRQ_en() уже сделал это

// в основном цикле:
if (dbg.GetDataReceivedFlag()) {
    uint32_t n = dbg.GetDataReceivedCount();
    // ... обработать buf[0..n) ...
}
```

### DMA TX + DMA RX с остановкой по IDLE (переменная длина, как в main.cpp)

```cpp
USART a(USART3, 115200, USART::_3::TX::PD8, USART::_3::RX::PD9);
DMA_Sx dma_tx(DMA_Sx::Req::Usart3::TX);
DMA_Sx dma_rx(DMA_Sx::Req::Usart3::RX);

a.SetUp();
a.AttachDMA(&dma_tx, &dma_rx);

uint8_t hello[] = "Hello, world!\r\n";
a.Send_DMA(hello, sizeof(hello) - 1);

uint8_t rx_buf[128];
a.Receive_DMA(rx_buf, sizeof(rx_buf));   // "до 128 байт или до паузы на линии"
// позже:
if (a.GetDataReceivedFlag()) {
    uint32_t n = a.GetDataReceivedCount();  // фактическая длина, может быть < 128
}
```

### Переопределение колбэка для побайтового парсинга протокола

```cpp
class ProtoUsart : public USART {
    using USART::USART;
    void OnRxByte(uint8_t b) override {
        USART::OnRxByte(b);   // сохранить стандартное поведение (буфер/счётчик), затем свою логику
        parser.Feed(b);
    }
};
```

---

## 12. Угловые случаи

- **Диапазон `BaudRate` проверяется в `SetUp()`**: `[9600, 115200*16]`; вне диапазона —
  `SysInitStatus::InitError` без единой записи в регистры (помечено `// todo` — верхняя граница
  условна, не взята из даташита конкретного чипа).
- **`AttachDMA()` вызывается ПОСЛЕ `SetUp()`** — иначе `_info` ещё `nullptr` и функция выходит
  рано, ничего не настроив.
- **Переполнение приёма** (`data_overflow`/`data_overflow_count`) — если `OnRxByte()` вызывается,
  когда `rx_data.size == 0` (никто не ждёт данные), байт **отбрасывается**, счётчик overflow
  увеличивается; это не ошибка шины, а сигнал "данные пришли, когда приём не был запущен".
- **`SetParity()` — заглушка** (`(void)parity;`), зарезервирована на будущее, реально ничего не
  настраивает.
- **`DeInit()` не выключает тактирование периферии** — только сбрасывает `CR1`/`CR2`/`CR3` и
  снимает регистрацию в `IRQ_Registry`; повторный `SetUp()` полностью восстанавливает работу.
- **`tx_irqn == rx_irqn` в `AttachDMA()`** — специально проверяется, чтобы не регистрировать один
  и тот же `this` дважды в одну строку `_table` (см. раздел 8, пункт 4).

---

## См. также

- [IRQ_Registry.md](IRQ_Registry.md) — механизм, на котором построена вся регистрация обработчиков
  (`IRQ_en()`, §5) и диспетчеризация `HandleIRQ()` (§6).
- [DMA.md](DMA.md) — устройство `DMA_Sx`, `AttachDMA()` со стороны DMA (общий паттерн с SPI.md).
- [GPIO.md](GPIO.md) — `PIN`, compile-time таблицы `USART::_N::TX/RX` и защита от "пин не той
  периферии" (см. §6 там).
- [System.md](System.md) — `System::GetTick()`, на котором построены таймауты `Send()`/`Receive()`.
- [SPI.md](SPI.md) — архитектурно почти зеркальный драйвер (тот же паттерн IRQ/DMA/виртуальных
  колбэков), полезно для сравнения.
