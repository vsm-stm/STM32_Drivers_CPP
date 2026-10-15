# UART / USART

[English](#english) · [Русский](#русский)

## English

### Initialization and ownership

One USART object controls one UART. Call System/RCC initialization first.
SetUp() selects PCLK, oversampling x16 and 8N1; G0 PRESC is /1.
Baud is rounded to the nearest representable divider:
BRR = (PCLK + baud/2) / baud, with 16 <= BRR <= 65535 and baud <= PCLK/16.
Zero baud and rates outside this range return InitError; no arbitrary 9600 minimum.

Named pins use USART::_N::TX/RX. PIN{} leaves a direction unconfigured.
Tables follow peripheral/GPIO availability in the CMSIS device header:
F4/F7 UART/USART 1..8 where present, G0 USART1..6 where present.
LPUART and F4 UART9/10 are outside these tables.
A GPIO port being present does not guarantee that its pin is bonded in the selected package.

SetUp() registers this object once on the UART IRQ and checks the result.
Repeated SetUp() is InitError until DeInit(). Initialization and ownership changes
run in main. Do not register this object manually.

IRQ_en() changes peripheral source bits only. Completion disables its sources
without unregistering anything. DeInit() removes this object's registrations and
preserves other handlers on shared vectors. The object must outlive registration;
its destructor does not call DeInit().

### Transfer contract

| Mode | Methods | Length and completion |
|---|---|---|
| Blocking | Send / Receive | Nonzero uint32_t length; timeout covers the entire call |
| IRQ | Send_IRQ / Receive_IRQ | 1..65535 bytes; RX ends at capacity or IDLE |
| DMA | Send_DMA / Receive_DMA | 1..65535 bytes; requires successful AttachDMA; RX ends at capacity or IDLE |

Before initialization: NotInit. Null pointer, zero length, unsupported asynchronous
length or an unconfigured direction: Error. An operation already active in the
same direction: Busy. TX and RX may run independently.
A new operation may be started after Error or Timeout.

Send() OK means all bytes were queued into UART; IRQ/DMA TX OK means the source
buffer can be reused. The last stop bit may still be on the wire.
Use the UART TC event for physical completion, for example before releasing RS-485 DE.
SetBaud() validates the new divider, writes BRR with UE=0 and stores BaudRate.
It returns NotInit, Error or Busy when the change cannot be performed.

Before SetBaud(), the caller must also ensure that the last stop bit has finished;
the Busy check covers the software operation only. Waiting for physical completion
is the caller's responsibility; the driver has no additional TX tracking field.
When the IRQ handler clears TC, use the OnTC() notification to observe completion.

Blocking timeouts require a running System tick; see [System.md](System.md).
Blocking Receive() reads exactly the requested length and does not publish the
asynchronous packet-ready flag.

### One packet at a time

Receive_IRQ/DMA starts a fresh packet and clears the previous ready/count/overflow state.
IDLE ends the packet. DMA RX then follows this order:

1. Disable UART RX interrupt sources and DMAR requests.
2. Stop DMA and wait for EN=0.
3. Read the remaining count and invalidate the bytes written by DMA.
4. Retain any pending UART/FIFO tail that fits the buffer.
5. Publish the byte count, OK and the ready flag.

On F4, the SR/DR sequence clearing IDLE occurs after DMA stops, and a pending
RXNE byte is retained when there is space. On G0, a FIFO tail below RXFT is drained.
DMA TC and IDLE cannot publish the same operation twice.

GetDataReceivedCount() is a pure read: zero while Busy or without a completed
packet, otherwise the completed count. ClearDataReceivedFlag() clears ready/count.
Consume the stopped buffer before clearing the flag and starting another receive.
GetOverflowCount() retains its read-and-clear contract; call it after reception stops.

DMAR=0 stops UART DMA requests; it does not disable the receiver. Bytes arriving
before the next Receive can fill RDR/FIFO or cause ORE. A new Receive discards stale
input/errors before arming the buffer. This is packet reception with explicit
rearming, not continuous reception without gaps.

### DMA and cache

Construct DMA resources with DMA_Sx::DMAReq constants, then call AttachDMA(tx, rx).
It configures Byte/MINC, direction and DR/TDR/RDR itself; do not call DMA SetUp first.
The UART validates the complete stream/CHSEL pair on F4/F7 or request ID on G0.
AttachDMA() returns SysInitStatus and rejects repeated attachment, wrong routes and
busy resources. Failure releases only resources acquired by that call.
A shared TX/RX DMA vector receives one registration.

Shared UART vectors also use IRQ_Registry capacity. If all four G0B0/B1
USART3..6 are used, configure at least four slots in irq_registry_settings.hpp.
An exhausted IRQ row returns InitError; this is not configured through CMake.

USART1 DMA2_Stream5 / CH4 is RX_alt, not TX_alt.
UART7/8 routes and G0 USART3..6 request IDs are provided where those peripherals exist.
DMA_Sx::CanUseRequest() inspects constructor selection without changing ownership.
GetErrorFlags() reads hardware error flags; UART enables TC/TE and F4/F7 DME interrupts.

Cache maintenance belongs to UART, using [System helpers](System.md):

| Handover | Operation |
|---|---|
| CPU -> TX DMA | CacheClean(buffer, length) |
| CPU -> RX DMA | CacheCleanInvalidate(buffer, capacity) |
| RX DMA -> CPU | Stop first, then CacheInvalidate(buffer, actual DMA count) |

On F4/G0 these functions are empty. On F7 System skips maintenance when D-cache is
disabled or the whole range lies inside one configured non-cacheable dma_regions area.
There is no second UART implementation or buffer-specific #ifdef.

A cacheable RX allocation must own every touched 32-byte line, including padding.
Do not let unrelated objects share those lines, or let the CPU access them while DMA
owns the buffer. Logical packet lengths need not be multiples of 32.
Aligned 64-byte storage can receive a packet of up to 37 bytes:

~~~cpp
alignas(32) static uint8_t packet[64];
uart.Receive_DMA(packet, 37);
~~~

Operation cost scales with touched cache lines and dirty writebacks, plus barriers.
Non-cacheable F7 buffers still incur the System range check. No cycle/size benchmark
was performed for these changes. See [ST AN4839](https://www.st.com/resource/en/application_note/an4839-level-1-cache-on-stm32f7-series-and-stm32h7-series-stmicroelectronics.pdf).

### Errors, FIFO and callbacks

UART PE/FE/NE/ORE are checked by a common enabled-error mask. DMA errors take
precedence over completion. The affected operation stops, becomes Error and invokes
OnError(uint32_t errors) once with the combined mask. An erroneous RX packet is not
published as ready. Error contains Parity/Framing/Noise/Overrun/DmaTx/DmaRx bits.
For blocking Receive, OnError runs in the calling context; asynchronous errors run in IRQ.

Callbacks are protected. Overrides of OnRxByte, OnTxEmpty, OnIdle,
OnDmaTxComplete and OnDmaRxComplete call the base implementation to retain standard
buffer/completion handling. OnTC and OnError are notifications with empty defaults.
No Register/Unregister, AttachDMA, SetUp or DeInit from callbacks.

G0 FIFO is accepted only for instances listed by IS_UART_FIFO_INSTANCE.
Threshold codes are 0..5; FIFO_TH::all is 5 (RX full, TX empty).
F4/F7 accept FIFO::NO only. Oversampling remains x16; no x8 selection was added.
Oversampling is performed by hardware and does not multiply CPU interrupts.
SetParity remains an unimplemented reserved API.

### Example

~~~cpp
USART uart(USART1, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7);
DMA_Sx tx(DMA1_Channel4, DMA_Sx::Req::Usart1::TX); // G0
DMA_Sx rx(DMA1_Channel5, DMA_Sx::Req::Usart1::RX);
alignas(32) static uint8_t packet[128];

if (uart.SetUp() == SysInitStatus::InitOK &&
    uart.AttachDMA(&tx, &rx) == SysInitStatus::InitOK)
    uart.Receive_DMA(packet, sizeof(packet));

// Main:
if (uart.GetDataReceivedFlag()) {
    uint32_t count = uart.GetDataReceivedCount();
    // Process packet[0..count) before rearming.
    uart.ClearDataReceivedFlag();
    uart.Receive_DMA(packet, sizeof(packet));
}
~~~

For F4/F7 construct DMA_Sx tx(DMA_Sx::Req::Usart1::TX) and the matching RX object.

Existing consumers that relied on GetDataReceivedCount() clearing the flag must
now call ClearDataReceivedFlag() explicitly. Modbus migration is deferred to its
module review; it was not changed here.

## Русский

### Настройка и владение

Один объект обслуживает один UART. Сначала настраиваются System и RCC.
SetUp() выбирает PCLK, oversampling x16 и 8N1; PRESC на G0 — /1.
BRR округляется до ближайшего целого: (PCLK + baud/2) / baud.
Допустимы 16..65535 и baud <= PCLK/16. Нулевой или непредставимый baud —
InitError. Условного нижнего предела 9600 больше нет.

Таблицы учитывают наличие периферии и GPIO в CMSIS: F4/F7 UART/USART 1..8,
G0 USART1..6 при их наличии. LPUART и F4 UART9/10 в эти таблицы не входят.
Наличие порта не означает доступность каждого пина в конкретном корпусе.
PIN{} оставляет направление ненастроенным.

SetUp() один раз регистрирует объект на UART IRQ и проверяет результат.
Повторная настройка — InitError до DeInit(). IRQ_en() меняет только биты источника.
Завершение обмена не меняет реестр. DeInit() останавливает DMA, освобождает свои
ресурсы и удаляет только свои обработчики, сохраняя общие IRQ других устройств.
Настройка/подключение/удаление выполняются в main, объект живёт до снятия регистрации.

### Обмен и готовность пакета

Нулевой указатель, нулевая длина, ненастроенное направление — Error.
До SetUp — NotInit, при занятом направлении — Busy.
IRQ/DMA принимают 1..65535 байт; блокирующие методы — ненулевую длину uint32_t.
После Error/Timeout разрешён новый запуск.

Таймаут блокирующего метода относится ко всему вызову, не к каждому байту.
Ему нужен работающий System::GetTick(). Блокирующий Receive читает ровно len байт
и не устанавливает флаг готовности асинхронного пакета.

TX OK означает передачу данных в UART и возможность повторного использования
исходного буфера. Последний стоповый бит может ещё передаваться.
Физическое завершение определяется UART TC; это нужно, например, для RS-485 DE.
SetBaud() проверяет divider, записывает BRR при UE=0, сохраняет новый BaudRate
и возвращает SysStatus.

Перед SetBaud вызывающий код должен дождаться последнего стопового бита;
проверка Busy относится только к программной операции. Ожидание физического TC —
ответственность вызывающего кода; дополнительного поля в драйвере нет.
Если обработчик IRQ очищает TC, завершение можно учитывать по уведомлению OnTC().

Receive_IRQ/DMA заканчивается по IDLE или заполнению буфера.
При IDLE отключаются источники RX и DMAR, DMA останавливается с ожиданием EN=0,
затем читается счётчик и обслуживается кеш. Оставшийся в UART/FIFO хвост
добавляется, если есть место. Только после этого публикуются длина и готовность.
На F4 чтение SR/DR для очистки IDLE выполняется после остановки DMA.
Повторного завершения одной операции по DMA TC и IDLE нет.

GetDataReceivedCount() ничего не сбрасывает: при Busy/отсутствии готового пакета
возвращает 0, после завершения — длину. После обработки вызвать
ClearDataReceivedFlag(), затем запустить следующий приём.
GetOverflowCount() остаётся чтением со сбросом и вызывается после остановки приёма.

DMAR отключает запросы DMA, но не приёмник UART. До повторного запуска новые байты
могут заполнить регистр/FIFO и вызвать ORE. Следующий Receive удаляет старый ввод
и ошибки. Непрерывный приём без пауз эта модель не обеспечивает.

### DMA, кеш и ошибки

AttachDMA() возвращает SysInitStatus, проверяет полный маршрут DMAReq и результаты
настройки/регистрации. Повторное подключение — ошибка до DeInit().
Отказ освобождает только ресурсы, полученные текущим вызовом.
TX/RX на общем DMA-векторе регистрируются один раз.

Для одновременного использования G0B0/B1 USART3..6 на общем UART-векторе
нужно не менее четырёх мест в irq_registry_settings.hpp. Настройка выполняется
через заголовок; при заполнении строки возвращается InitError.

UART вызывает System::CacheClean перед TX, CacheCleanInvalidate перед RX,
CacheInvalidate после остановки RX для записанных DMA байтов.
F4/G0: пустые функции. F7: System пропускает кешевые операции для диапазона,
полностью находящегося внутри одной некешируемой dma_regions области.
При этом остаётся проверка диапазона; отдельной реализации UART нет.

Кешируемый RX-буфер должен владеть всеми затронутыми 32-байтными строками.
Пока DMA работает, CPU не обращается к этим строкам. Длина пакета может быть
некратна 32, если выделенная память включает необходимый запас.
Например, alignas(32) uint8_t buffer[64] позволяет принять до 37 байт.
Стоимость зависит от числа строк, записи грязных данных и барьеров; замеров нет.

PE/FE/NE/ORE проверяются общей маской разрешённых ошибок.
Ошибки DMA имеют приоритет над успешным завершением.
Операция останавливается, получает Error и вызывает OnError(errors) с общей маской.
Повреждённый RX-пакет не получает ready. При блокирующем Receive callback вызывается
из вызывающего кода, при асинхронном обмене — из IRQ.

Callbacks доступны наследникам как protected. Для сохранения стандартной обработки
переопределения вызывают базовые OnRxByte/OnTxEmpty/OnIdle/OnDmaTxComplete/
OnDmaRxComplete. OnTC/OnError — уведомления с пустой реализацией по умолчанию.
Из callback нельзя менять реестр или вызывать SetUp/AttachDMA/DeInit.

FIFO G0 проверяется по возможностям конкретного USART. Код all исправлен на 5:
RX — полный FIFO, TX — пустой. Oversampling остаётся x16, режим x8 не добавлен.
SetParity остаётся заглушкой.

### Совместимость

USART1 DMA2_Stream5 / CH4 переименован из ошибочного TX_alt в RX_alt.
Добавлены UART7/8 и G0 USART3..6 DMAReq; исправлены G0 AF USART1/4 и общие IRQ.
Добавлены DMA_Sx::CanUseRequest() и GetErrorFlags(); прочие потребители DMA не менялись.

Потребители, ожидавшие сброс флага в GetDataReceivedCount(), должны теперь вызывать
ClearDataReceivedFlag() явно. Modbus оставлен для своего разбора.

### Источники для таблиц

- [ST AN4031 — таблицы DMA F4/F7](https://www.st.com/resource/en/application_note/dm00046011-using-the-stm32f2-stm32f4-and-stm32f7-series-dma-controller-stmicroelectronics.pdf).
- [ST G0 LL DMAMUX — идентификаторы запросов](https://raw.githubusercontent.com/STMicroelectronics/stm32g0xx-hal-driver/master/Inc/stm32g0xx_ll_dmamux.h).
- [ST G071 datasheet — AF USART1/4](https://www.st.com/resource/en/datasheet/stm32g071kb.pdf).
- [ST G0B1 datasheet — AF USART3..6](https://www.st.com/resource/en/datasheet/stm32g0b1me.pdf).
