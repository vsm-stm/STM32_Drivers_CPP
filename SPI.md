# SPI — подробное описание

## Содержание

1. [Зачем это нужно](#1-зачем-это-нужно)
2. [Пины и роль Master/Slave в их настройке](#2-пины-и-роль-masterslave-в-их-настройке)
3. [Software vs Hardware NSS](#3-software-vs-hardware-nss)
4. [Три режима передачи](#4-три-режима-передачи)
5. [HandleIRQ() — почему проверяются биты разрешения, а не только флаги](#5-handleirq--почему-проверяются-биты-разрешения-а-не-только-флаги)
6. [Приём без TX-данных — RXONLY и "0xFF dummy"](#6-приём-без-tx-данных--rxonly-и-0xff-dummy)
7. [DMA-интеграция](#7-dma-интеграция)
8. [Виртуальные колбэки](#8-виртуальные-колбэки)
9. [Примеры использования](#9-примеры-использования)
10. [Угловые случаи](#10-угловые-случаи)

---

## 1. Зачем это нужно

`SPI` — один класс на один аппаратный SPI, работающий на STM32G0/F4/F7 без изменений в коде
приложения — архитектурно почти зеркало [`USART`](UART.md): наследует `IIRQHandler`, использует
таблицу `spi_table[]` (peripheral → clk_reg/clk_bit/bus_clk/irq, тот же паттерн, что
`usart_table`/`tim_table`), auto-register/unregister в `IRQ_Registry` через `IRQ_en()`, и три
режима передачи (блокирующий/IRQ/DMA).

```cpp
SPI spi(SPI1, SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7);
spi.SetUp(SPI::Master_sel::Master, SPI::NSS_ctrl::Software,
          SPI::TYPE::TX, SPI::Data_frame_format::Byte,
          SPI::Frame_Format::MSB, SPI::cPolPha::None, SPI::BaudRate::DIV4);
spi.Send(buf, sizeof(buf), 1000);
```

Отличие от `USART` в регистровой абстракции — минимальное: F4/F7/G0 используют один и тот же
`SPI_DR`/`SPI_SR` (не нужно разделять на `TDR`/`RDR`/`ISR`/`ICR`, как в UART), единственный
семейный разрыв — поле ширины кадра (`CR1::DFF` на F4 против `CR2::DS` на F7/G0, см. §2).

---

## 2. Пины и роль Master/Slave в их настройке

```cpp
explicit SPI(SPI_TypeDef* spix, PIN sck, PIN mosi, PIN miso = PIN{}, PIN ss = PIN{});
```

Как и `USART`/`TIM`, конструктор проверяет `periph_base` каждого переданного пина против `spix`
и вызывает `System::DebugTrap()` при несовпадении (см.
[GPIO.md §6](GPIO.md#6-constexpr-таблицы-пинов-и-защита-от-опечаток)).

Настройка пинов в `SetHard()` **зависит от роли** (Master/Slave), а не одинакова для всех:

| Пин | Master | Slave |
|-----|--------|-------|
| SCK | `AF_PushPull` | `AF_PushPull` |
| MOSI | `AF_PushPull` (ведущий гонит линию) | `AF_OD_PulUp` (ведомый только слушает свою же MOSI-линию, физически подтянутую) |
| MISO | `AF_OD_PulUp` (принимает от ведомого) | `AF_PushPull` (ведомый сам гонит ответ) |
| SS (Hard NSS) | `AF_PushPull` | `AF_PushPull` |
| SS (Software, Master) | `OUTPUT_PushPull` (сам управляет выбором ведомого) | — |
| SS (Software, Slave) | — | `INPUT_NO_Pull` (слушает, кто его выбрал) |

Ключевая деталь: MOSI/MISO настраиваются в открытый сток с подтяжкой (`AF_OD_PulUp`) для той
роли, в которой линия **не** является выходом текущего устройства — это защищает от конфликта
драйверов на шине, если по ошибке два устройства одновременно решат, что они ведущие.

---

## 3. Software vs Hardware NSS

```cpp
enum class NSS_ctrl { Hard, Software };
```

- **`Hard`** — аппаратный NSS: линия `SS` подключена к выводу, `SPI_CR2_SSOE` включает
  автоматическое управление им железом (только в режиме Master).
- **`Software`** — `SlaveSelect()` управляет пином (`_ss`) вручную:

```cpp
inline void SlaveSelect(FunctionalState en) {
    if (nss_ctrl != NSS_ctrl::Software) return;
    if (Master_slave == Master_sel::Master) _ss.SetLevel(!en);   // GPIO: LOW = выбран
    else /* slave */ { if (en) SPIx->CR1 &= ~SPI_CR1_SSI; else SPIx->CR1 |= SPI_CR1_SSI; }
}
```

На Master + Software эта функция дёргает обычный GPIO. На Slave + Software она вместо этого
трогает внутренний бит `SSI` (Slave Select Internal) — ведомое устройство не может физически
"выбрать само себя", поэтому программная эмуляция NSS для ведомого означает не выставление
вывода, а прямую подмену состояния, которое иначе читалось бы с реального пина `SS`.

Каждая блокирующая/IRQ/DMA-передача (`Send`, `Send_IRQ`, `SendDMA`, ...) сама вызывает
`SlaveSelect(ENABLE)` перед стартом и `SlaveSelect(DISABLE)` после завершения — независимо от
режима NSS, вызов безвреден (при `Hard` он просто ничего не делает, см. проверку
`nss_ctrl != Software` в начале).

---

## 4. Три режима передачи

Как и в [UART.md §4](UART.md#4-три-режима-передачи):

| Режим | Методы | Направление |
|-------|--------|--------------|
| **Блокирующий** | `Send()`, `Receive()`, `Send_Receive()` | TX, RX-only (через временный `RXONLY`), полный дуплекс |
| **IRQ** | `Send_IRQ()`, `Receive_IRQ()`, `SendReceive_IRQ()` | те же три варианта, неблокирующие |
| **DMA** | `SendDMA()`, `Receive_DMA()`, `ReceiveDMA()`, `SendReceive_DMA()` | TX-only, RX-only (без TX DMA), полный дуплекс с TX=dummy, полный дуплекс с реальными данными |

Особенность SPI (в отличие от UART) — это **синхронная** шина: приём без одновременной передачи
всё равно требует, чтобы кто-то генерировал тактовый сигнал. Отсюда:

- `Receive()`/`Receive_IRQ()`/`Receive_DMA()` — временно включают `RXONLY` (`CR1_RXONLY`), под
  которым сам SPI генерирует клок без реальной передачи по MOSI, и снимают его после завершения.
- `ReceiveDMA()` (обратите внимание на разницу в названии с `Receive_DMA()`!) — **не**
  использует `RXONLY`, а вместо этого гонит по MOSI фиктивные байты `0xFF` через отдельный
  DMA TX-канал, одновременно принимая настоящие данные по MISO через DMA RX-канал (см. §6). Both
  методов решают одну и ту же задачу "принять, не заботясь о содержимом TX", но разными
  средствами — `Receive_DMA()` не требует TX DMA-канала вообще, `ReceiveDMA()` требует оба.

---

## 5. HandleIRQ() — почему проверяются биты разрешения, а не только флаги

```cpp
void SPI::HandleIRQ() {
    // ... сначала DMA TC-флаги (см. §7) ...
    uint32_t cr2 = SPIx->CR2;
    uint32_t sr  = SPIx->SR;
    if ((cr2 & SPI_CR2_RXNEIE) && (sr & SR_RXNE)) while (SPIx->SR & SR_RXNE) OnRxByte(RXD());
    if ((cr2 & SPI_CR2_TXEIE)  && (sr & SR_TXE))  OnTxEmpty();
    if ((cr2 & SPI_CR2_ERRIE)  && (sr & SR_ERR))  OnError();
}
```

В отличие от [`TIM::HandleIRQ()`](TIM.md#5-timhandleirq--база-для-всех-подклассов) и
`USART::HandleIRQ()`, которые проверяют только сам флаг статуса, здесь **дополнительно**
проверяется соответствующий бит **разрешения** в `CR2` (`RXNEIE`/`TXEIE`/`ERRIE`). Причина —
`SR_TXE` физически почти всегда взведён (регистр данных на передачу пуст большую часть времени,
это нормальное состояние простаивающей шины), поэтому чисто по флагу `HandleIRQ()` вызывал бы
`OnTxEmpty()` при **любом** прерывании на этом SPI, включая то, что реально предназначено только
для RX (например, `Receive_IRQ()` включает только `RXNEIE`, не трогая `TXEIE`) — без проверки
`cr2 & SPI_CR2_TXEIE` TX-only колбэк срабатывал бы и на чисто RX-транзакциях.

---

## 6. Приём без TX-данных — RXONLY и "0xFF dummy"

Две разные техники для одной и той же практической задачи ("принять данные по MISO, не заботясь,
что уходит на MOSI"), выбор зависит от того, есть ли доступный TX DMA-канал:

```
RXONLY (Receive/Receive_IRQ/Receive_DMA):           TX+RX DMA (ReceiveDMA):
  CR1 |= RXONLY                                       DMA TX -> MOSI: 0xFF, 0xFF, 0xFF, ... (MINC=DISABLE!)
  CR1 |= SPE  (клок стартует немедленно)              DMA RX <- MISO: настоящие данные (MINC=ENABLE)
  # SPI сам генерирует клок без реальной TX-активности   # клок идёт от РЕАЛЬНОЙ TX-активности,
                                                          # просто данные фиктивные
```

`_dma_tx->MINC(DISABLE)` в `ReceiveDMA()` — ключевая деталь: адрес источника DMA TX **не**
увеличивается, один и тот же байт `spi_dummy_byte` (статическая константа) отправляется `len`
раз подряд — не нужен буфер из `len` копий `0xFF`, один байт с фиксированным адресом справляется.
После `ReceiveDMA()` `SendDMA()` восстанавливает `_dma_tx->MINC(ENABLE)` в `OnDmaRxComplete()` —
иначе следующий вызов обычного `SendDMA()` (с реальными данными, где инкремент нужен) унаследовал
бы `MINC = DISABLE` от предыдущего `ReceiveDMA()`.

---

## 7. DMA-интеграция

```cpp
void AttachDMA(DMA_Sx* tx = nullptr, DMA_Sx* rx = nullptr);
```

Структурно идентично [`USART::AttachDMA()`](UART.md#8-dma-интеграция) — таблица
`spi_dma_req_table` даёт `tx_req`/`rx_req` по указателю `SPIx`, `DMA_Sx::StreamSettings`
настраивается на `&SPIx->DR` с шириной `Byte`, регистрируется `this` как обработчик DMA TC
(и здесь тоже с проверкой общего вектора: `if (!tx || tx->GetIRQn() != rx->GetIRQn())` перед
повторной регистрацией для RX).

`HandleIRQ()` проверяет оба DMA-канала **до** SPI-флагов (см. §5) — так же, как в
[`USART::HandleIRQ()`](UART.md#6-handleirq--один-обработчик-на-uart-и-на-dma):

```cpp
if (_dma_tx && _dma_tx->GetTC_Flag()) { ...; OnDmaTxComplete(); }
if (_dma_rx && _dma_rx->GetTC_Flag()) { ...; OnDmaRxComplete(); }
```

**Порядок завершения в полном дуплексе важен**: `OnDmaTxComplete()` при активном RX (`_rx_active
== true`) **ничего не делает** (`return` сразу после `DMA_TX(DISABLE)`) — вся финальная очистка
(снятие `SPE`, `SlaveSelect(DISABLE)`, сброс статуса) отложена до `OnDmaRxComplete()`, потому что
TX может физически завершиться на несколько тактов раньше, чем последний принятый по MISO байт
дойдёт до памяти через RX DMA — закрывать шину нужно только когда закончены **оба** направления.

---

## 8. Виртуальные колбэки

```cpp
virtual void OnTxEmpty();          // IRQ-режим TX: закачивает следующий байт или завершает передачу
virtual void OnRxByte(uint8_t);    // IRQ-режим RX: принимает байт, следит за концом буфера
virtual void OnError();            // OVR/MODF/CRCERR — по умолчанию пустая заглушка
virtual void OnDmaTxComplete();    // DMA TX завершён (см. §7 про порядок в дуплексе)
virtual void OnDmaRxComplete();    // DMA RX завершён — финальная очистка шины
```

Тот же принцип, что в [UART.md §10](UART.md#10-виртуальные-колбэки--расширение-без-переписывания-handleirq):
`HandleIRQ()` — `final`, не переопределяется; вместо этого наследник переопределяет нужный
колбэк, не трогая логику диспетчеризации DMA/CR2/SR.

```cpp
struct MySPI : SPI {
    using SPI::SPI;
    void OnTxEmpty() override { /* своя логика подачи следующего байта */ }
};
```

**`OnError()` по умолчанию ничего не делает** — единственный из пяти колбэков без
содержательной реализации по умолчанию; `SR_ERR = OVR | MODF | CRCERR` объединяет три разные
аппаратные ошибки в один бит для проверки в `HandleIRQ()`, но не различает их за пользователя —
переопределение `OnError()` должно само прочитать `SPIx->SR`, если нужно узнать, какая именно
ошибка произошла.

---

## 9. Примеры использования

### Блокирующая передача (Master, Software NSS)

```cpp
SPI spi(SPI1, SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7, PIN{}, SPI::_1::SS::PA4);
spi.SetUp(SPI::Master_sel::Master, SPI::NSS_ctrl::Software, SPI::TYPE::TX,
          SPI::Data_frame_format::Byte, SPI::Frame_Format::MSB,
          SPI::cPolPha::None, SPI::BaudRate::DIV4);
spi.Send(buf, sizeof(buf), /*timeout_ms=*/100);
```

### Полный дуплекс блокирующий (обмен с датчиком по одному запросу-ответу)

```cpp
uint8_t tx[4] = {0x9F, 0, 0, 0};   // например, JEDEC ID запрос для SPI Flash
uint8_t rx[4];
spi.Send_Receive(tx, rx, sizeof(tx), 1000);
```

### IRQ-driven TX с переопределённым колбэком

```cpp
struct MySPI : SPI {
    using SPI::SPI;
    void OnTxEmpty() override { SPI::OnTxEmpty(); /* + своя телеметрия */ }
};
MySPI spi(SPI1, SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7);
spi.SetUp(...);
spi.Send_IRQ(buf, sizeof(buf));
```

### DMA TX (например, вывод на матрицу через SPI-мост)

```cpp
DMA_Sx dma_tx(DMA1_Channel3);   // G0: любой свободный канал
spi.SetUp(...);
spi.AttachDMA(&dma_tx, nullptr);
spi.SendDMA(framebuffer, sizeof(framebuffer));
```

### Полный дуплекс DMA — приём произвольных данных, чтение регистра ведомого

```cpp
DMA_Sx dma_tx(DMA1_Channel3), dma_rx(DMA1_Channel4);
spi.AttachDMA(&dma_tx, &dma_rx);
spi.ReceiveDMA(rx_buf, len);   // TX = 0xFF dummy автоматически, RX = rx_buf
```

---

## 10. Угловые случаи

- **`ReceiveDMA()` требует ОБА DMA-канала** (TX для генерации клока фиктивными байтами, RX для
  приёма) — `SysStatus::Error`, если хотя бы один не подключён через `AttachDMA()`. `Receive_DMA()`
  (с подчёркиванием, не путать!) требует только RX-канал — работает через `RXONLY`, без dummy TX.
- **`_dma_tx->MINC(DISABLE)` в `ReceiveDMA()` восстанавливается только в `OnDmaRxComplete()`** —
  если переопределить этот колбэк без вызова базовой реализации, следующий `SendDMA()` унаследует
  `MINC = DISABLE` и будет слать один и тот же байт вместо буфера.
- **`OnDmaTxComplete()` при `_rx_active == true` не завершает передачу** — вся очистка шины
  отложена до `OnDmaRxComplete()` (см. §7); переопределяя `OnDmaTxComplete()`, не стоит
  добавлять туда `SlaveSelect(DISABLE)` для дуплексного случая — это преждевременно снимет
  выбор ведомого до завершения приёма.
- **Software NSS на Slave не трогает реальный вывод** — использует `SPI_CR1_SSI`, программный
  флаг "как будто выбран", а не физический уровень на пине (см. §3) — ведомое устройство не
  может выбрать себя аппаратно.
- **`HandleIRQ()` проверяет бит разрешения (`CR2`) вместе с флагом (`SR`)** — в отличие от
  `TIM`/`USART`, где проверяется только сам флаг статуса (см. §5); при переносе паттерна на
  новый драйвер стоит помнить, что не все флаги статуса SPI редки в простое.
- **`OnError()` не различает OVR/MODF/CRCERR** — `SR_ERR` объединяет три бита в одну маску для
  единственной проверки в `HandleIRQ()`; конкретную причину нужно определять внутри
  переопределённого `OnError()` самостоятельно.

---

## См. также

- [IRQ_Registry.md](IRQ_Registry.md) — механизм, на котором построена регистрация `IRQ_en()`
  и диспетчеризация `HandleIRQ()` — тот же паттерн, что у `USART`/`TIM`/`RTC`.
- [DMA.md](DMA.md) — устройство `DMA_Sx`, общий с UART.md паттерн `AttachDMA()` (§7 здесь).
- [GPIO.md](GPIO.md) — `PIN`, compile-time таблицы `SPI::_N::SCK/MOSI/MISO/SS` (см. §2 здесь про
  их настройку по ролям Master/Slave).
- [System.md](System.md) — `SysStatus`, `System::GetTick()` (таймауты блокирующих передач).
- [UART.md](UART.md) — архитектурно почти зеркальный драйвер, полезно для сравнения регистровой
  абстракции и трёх режимов передачи.
