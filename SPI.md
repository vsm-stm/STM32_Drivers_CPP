# SPI

[English](#english) · [Русский](#русский)

## English

### 1. Ownership and setup

One SPI object controls one hardware peripheral. Initialize System/RCC first.
Call SetUp() once; use DeInit() before reconfiguration or destruction. DMA objects,
registered SPI objects and active buffers must remain alive.

The constructor retains its optional SS pin:

~~~cpp
SPI bus(SPI1, SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7,
        SPI::_1::MISO::PA6, PIN(GPIOB, 0)); // Optional software master CS
~~~

SCK is required. A direction's data pin may be omitted when it is not used
externally. Named AF pins are checked for peripheral ownership at construction
time; this check is a runtime DebugTrap, not a compile-time assertion.

TYPE is removed from SetUp(). Operations choose direction; hardware remains in
two-line full duplex. RXONLY and single-wire BIDIMODE are not used.

~~~cpp
SysInitStatus initialized = bus.SetUp(
    SPI::Master_sel::Master, SPI::NSS_ctrl::Software,
    SPI::Data_frame_format::Byte, SPI::Frame_Format::MSB,
    SPI::cPolPha::None, SPI::BaudRate::DIV8);
~~~

Check InitOK before starting. Invalid enums, missing SCK, Hard NSS without its
pin, software-NSS slave, unsupported instance, repeat setup or IRQ registration
failure return InitError. Tables cover CMSIS-present SPI1–SPI6 on F4/F7 and SPI1–SPI3 on G0.
Named pins depend on the device AF map; check that the chosen pin is bonded in
its package. SPI5 on F410/F411/F412/F413/F423 uses AF6; larger F4/F7 use AF5.
G0 SPI2/SPI3 share SPI2_3_IRQn where SPI3 exists.

### 2. Software CS, hardware NSS and shared buses

- Software-NSS master with SS: the driver initializes it high, asserts it at
  start and deasserts it only at final completion/error/timeout.
- Software-NSS master without SS: the application owns all chip-select GPIOs.
- Hardware-NSS master: SSM is clear and SSOE is set. NSS follows SPI enable.
- Slave: hardware NSS is required; SSM and SSOE are clear. Configure EXTI on
  that same pin in the application to notify packet termination.

On a shared bus use one SPI object and application-owned CS pins. This keeps CS
active across multiple operations. Always release it on an error as well.

~~~cpp
SPI bus(SPI1, SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7, SPI::_1::MISO::PA6);
PIN flash_cs(GPIOB, 0), adc_cs(GPIOB, 1);
flash_cs.SetUp(PIN::TYPE::OUTPUT_PushPull, PIN::OUTPUT_SPEED::Low, PIN::LVL::HIGH);
adc_cs.SetUp(PIN::TYPE::OUTPUT_PushPull, PIN::OUTPUT_SPEED::Low, PIN::LVL::HIGH);
// After successful bus.SetUp(Master, Software):
uint8_t command[] = {0x9F};
uint8_t response[3];
flash_cs.SetLevel(0);
SysStatus result = bus.Send(command, 1);
if (result == SysStatus::OK) result = bus.Receive(response, 3);
flash_cs.SetLevel(1);
// Inspect result; the application chooses the device and protocol.
~~~

RCC/SPI configuration is fixed while a transfer is active. Devices with different
CPOL/CPHA/divider/frame settings require explicit idle reconfiguration through
DeInit()/SetUp(), including DMA reattachment if used. No device scheduler is added.

### 3. Frames, directions and status

Every transfer has uint8_t* and uint16_t* overloads. The selected overload must
match SetUp(Byte/Half_Word). len is 1..65535 frames for every method:

- uint8_t buffer: len bytes, 8-bit DR/DMA accesses.
- uint16_t buffer: len half-words, 16-bit DR/DMA accesses; two-byte alignment.
- F7/G0 RX FIFO threshold matches 8/16-bit frames, without implicit packing.

Send transmits the supplied frames and discards RX. Receive transmits all-one
dummy frames and stores RX. Send/Receive uses both supplied buffers.

| Mode | Methods | Meaning of returned OK |
|---|---|---|
| Blocking | Send, Receive, Send_Receive | Operation has completed |
| IRQ | Send_IRQ, Receive_IRQ, SendReceive_IRQ | Operation has started |
| DMA | Send_DMA, Receive_DMA, SendReceive_DMA | Operation has started |

Receive_DMA uses dummy-clocked two-line reception. RXONLY is not used.

NotInit means setup has not succeeded. Busy rejects a simultaneous operation.
Null/zero arguments, >65535 count, frame-width/alignment mismatch, missing data
pin or required DMA return Error. Error/Timeout permit another start after cleanup.
One blocking timeout budget covers the entire call and needs a working System tick.
Asynchronous calls impose no internal deadline; the application owns its deadline
and can cancel through DeInit().

~~~cpp
// After successful setup with Half_Word:
uint16_t tx[] = {0x1234, 0xABCD}, rx[2];
SysStatus result = bus.Send_Receive(tx, rx, 2); // Two 16-bit frames.
~~~

### 4. Physical completion through Process()

IRQ moves frames. DMA TC ends buffer feeding/capture. Neither TXE nor TX DMA TC
alone means that the last wire frame has finished.

Call Process() regularly from main after starting an IRQ/DMA operation:

~~~cpp
if (bus.SendReceive_IRQ(tx, rx, 2) == SysStatus::OK) {
    // Return to the main loop.
}
// In main, while this operation is active:
SysStatus state = bus.Process();
if (state == SysStatus::OK) {
    uint32_t received_frames = bus.GetDataReceivedCount();
    // RX is available; its count is read without clearing it.
}
~~~

For master, Process first requires completion of all planned TX/RX frame handling.
It then checks SPI itself: F4 TXE=1 and BSY=0; F7/G0 TX FIFO empty and BSY=0.
A single BSY=0 check during an unfinished operation is insufficient.

Process does not spin or wait for BSY. It performs stop/cache/CS handover before
publishing OK and calling OnComplete(). Hardware errors call OnError(mask) after
cleanup. These notifications run in main/Process context, not in DMA IRQ.
GetStatus() is a pure read and does not perform this work.

### 5. Slave: packet terminated by hardware NSS

Slave packets may be shorter or longer than the prepared buffer. len is capacity,
not a required packet length. The first len complete frames are retained.
GetDataReceivedCount() reports stored frames after successful Process().
Further frames set IsTruncated(), but the stored prefix is still a successful
packet. At exactly len frames, truncation remains false. After the TX payload, slave feeds 0xFF/0xFFFF dummy frames until NSS rises.
This tail is fed by TXE IRQ, including after TX DMA TC; choose SCK and interrupt
latency accordingly. Continuous dummy output at maximum SCK is not guaranteed.

Prepare TX/RX before the master selects NSS. The application configures and
acknowledges rising-edge EXTI on the hardware NSS pin, then calls NssReleased():

~~~cpp
SPI slave(SPI1, SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7,
          SPI::_1::MISO::PA6, SPI::_1::SS::PA4);
static uint8_t packet[64];
// Check SetUp(Slave, Hard), then Receive_IRQ(packet, sizeof(packet)).

// In the application's EXTI handler, after checking/acknowledging its NSS flag:
slave.NssReleased();

// In main, after the operation has started:
if (slave.Process() == SysStatus::OK) {
    uint32_t frames = slave.GetDataReceivedCount();
    bool truncated = slave.IsTruncated();
    // Process packet[0..frames); then rearm and check the start result.
}
~~~

NssReleased stops SPI sources/DMA without cache work or completion callbacks.
Process returns DMA-written bytes to CPU, drains the final RX FIFO tail and
publishes the packet. Slave completion also resets this SPI to flush any unused
prefetched TX frames before the next packet. It does not use slave BSY as a packet boundary: affected
devices have a documented slave-BSY erratum.

The NSS EXTI, SPI and DMA handlers for the object must not preempt one another.
EXTI must be serviced before a subsequent transaction, and the master must wait
until Process() and rearm before selecting another packet. Software polling of NSS
cannot replace edge capture if the master can produce short high pulses.

Only complete frames are reported. An incomplete last frame or a packet's protocol
validity must be handled by the application. OVR before capacity is exhausted means
data were lost and results in Error. OVR after capacity is exhausted is truncation.

### 6. DMA attachment, requests and fallback directions

~~~cpp
// F4/F7: the chosen resource is visible in its name.
DMA_Sx tx(DMA_Sx::Req::Spi1::TX_DMA2_Stream3);
DMA_Sx rx(DMA_Sx::Req::Spi1::RX_DMA2_Stream0);
// G0 alternatives:
// DMA_Sx tx(DMA1_Channel2, DMA_Sx::Req::Spi1::TX);
// DMA_Sx rx(DMA1_Channel3, DMA_Sx::Req::Spi1::RX);

if (bus.AttachDMA(&tx, &rx) == SysInitStatus::InitOK) {
    // Send_DMA/Receive_DMA/SendReceive_DMA can now be started.
}
~~~

AttachDMA configures ownership, width, direction and DR address. Do not configure
these DMA objects separately first. At least one direction is required. Repeated
attachment, wrong complete DMAReq, same object for TX/RX, owned resources, setup or
registry failure give InitError. Only resources acquired by the failing call are
released. One shared DMA vector registers the SPI object once.

Send_DMA requires TX DMA; Receive_DMA requires RX DMA; SendReceive_DMA needs both.
If the other direction is not attached, SPI IRQ feeds dummy TX or discards RX.
For high SCK rates use both DMA directions to avoid per-frame CPU work.
Send_DMA(minc=DISABLE) repeats the first frame. DMA buffers must be accessible by DMA.

### 7. F7 cache and buffer handover

SPI uses System::CacheClean for TX, CacheCleanInvalidate before RX and
CacheInvalidate for the DMA-written prefix after DMA EN=0. Cache byte lengths are
frame_count * frame_width. For minc=DISABLE only the repeated TX frame is cleaned.

F7 cacheable RX allocations own complete 32-byte cache lines, including padding.
Do not read/write them while Busy. System skips ranges wholly within a configured
project non-cacheable DMA region; F4/G0 helpers are empty. No separate SPI build
configuration is needed for non-cacheable buffers.

The all-one dummy source is immutable Flash. A separate discard word is never
read or modified by CPU: its DMA writes do not need a CPU cache handover.
DMA reachability remains a caller responsibility. For simultaneous TX/RX use
distinct buffers; writing into a TX buffer that DMA has not yet consumed races
with transmission.

~~~cpp
alignas(32) static uint16_t rx[64]; // Owns all affected cache lines.
static uint16_t tx[64];
// After Half_Word setup and successful DMA attachment:
bus.SendReceive_DMA(tx, rx, 64); // 64 frames, 128 cache-maintained RX bytes.
// Do not access rx before Process() completes.
~~~

### 8. IRQ, errors, callbacks and cleanup

SetUp registers the SPI object and enables its NVIC line once. IRQ_en changes only
CR2 source bits; enable transfer sources only for an active operation.
AttachDMA separately registers distinct DMA vectors. DeInit removes only the
SPI object's entries and retains other handlers/shared NVIC state. Registry changes
belong to main, never to callbacks.

Errors are combined into one OnError(uint32_t) mask: Overrun, ModeFault, Crc, Frame,
DmaTx and DmaRx. DMA errors take precedence over TC. Sources stop in IRQ, final
cache/status/error notification happens in Process(). A blocking timeout releases
automatic CS and resets the aborted SPI to discard queued/latched state; unrelated
peripherals and shared clocks are untouched.

Operational callbacks are protected: OnTxEmpty, OnRxByte, OnRxWord,
OnDmaTxComplete, OnDmaRxComplete. Overrides call their base to preserve processing.
OnComplete and OnError are empty notifications. Byte/word callbacks also run during
blocking polling or final FIFO drain in the calling/main context.

~~~cpp
class Link : public SPI {
public:
    using SPI::SPI;
protected:
    void OnRxWord(uint16_t frame) override {
        SPI::OnRxWord(frame); // Preserve storage/count/truncation.
    }
    void OnComplete() override { /* Main/Process: packet is available. */ }
    void OnError(uint32_t errors) override { /* Main: record failure mask. */ }
};
~~~

DeInit cancels without completion callbacks, stops DMA, returns an active RX buffer,
unregisters this object and releases DMA resources. Disable/serialize the user's
NSS EXTI callback first. The destructor does not call DeInit.

## Русский

### 1. Настройка и владение

Один объект SPI обслуживает одну аппаратную периферию. Сначала настраиваются
System/RCC. Повторный SetUp — ошибка до DeInit. Объекты SPI/DMA и буферы должны
жить до завершения использования. Деструктор автоматически DeInit не вызывает.

Конструктор сохраняет необязательный SS. В master с программным NSS переданный
SS автоматически управляется драйвером; без него устройствами управляет приложение.
Аппаратный NSS требует соответствующий пин. Slave использует аппаратный NSS.

TYPE удалён из SetUp. Все операции работают в двухлинейном режиме: Send передаёт
и отбрасывает RX, Receive передаёт dummy и сохраняет RX, Send_Receive использует
оба буфера. RXONLY/BIDIMODE не включаются. Таблицы охватывают присутствующие в CMSIS SPI1–SPI6 для F4/F7 и SPI1–SPI3
для G0. Пины зависят от карты AF; доступность вывода в корпусе проверяет
пользователь. SPI5 у F410/F411/F412/F413/F423 использует AF6, у старших F4/F7 —
AF5. У G0 с SPI3 блоки SPI2/SPI3 используют общий SPI2_3_IRQn.

~~~cpp
SPI bus(SPI1, SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7, SPI::_1::MISO::PA6);
SysInitStatus initialized = bus.SetUp(
    SPI::Master_sel::Master, SPI::NSS_ctrl::Software,
    SPI::Data_frame_format::Half_Word, SPI::Frame_Format::MSB,
    SPI::cPolPha::None, SPI::BaudRate::DIV8);
uint16_t tx[] = {0x1234, 0xABCD}, rx[2];
// Только после initialized == InitOK:
SysStatus result = bus.Send_Receive(tx, rx, 2); // Два кадра, не два байта.
~~~

### 2. Общая шина и SS

Для нескольких устройств используется один SPI и отдельные CS приложения.
Так CS может оставаться активным между отправкой команды и чтением ответа.
При ошибке приложение тоже снимает свой CS. При IRQ/DMA CS снимается после
физического завершения через Process, а не сразу после успешного старта.

~~~cpp
PIN cs(GPIOB, 0);
cs.SetUp(PIN::TYPE::OUTPUT_PushPull, PIN::OUTPUT_SPEED::Low, PIN::LVL::HIGH);
// bus настроен на Master/Software, SS в конструкторе не передан.
cs.SetLevel(0);
SysStatus result = bus.Send(command, command_frames);
if (result == SysStatus::OK) result = bus.Receive(reply, reply_capacity);
cs.SetLevel(1);
~~~

В master Hard сбрасывает SSM и включает SSOE; NSS управляется через SPE.
В slave Hard сбрасывает SSM/SSOE. Программный NSS в slave не поддерживается.
Для устройств с разными настройками требуется явный DeInit/SetUp в простое,
а при DMA — повторное подключение освобождённых DMA. Планировщик устройств
или автоматическое переключение их конфигураций не добавлены.

### 3. Размер кадра и методы

У методов есть перегрузки uint8_t*/uint16_t*. Они должны соответствовать
Byte/Half_Word из SetUp. len — 1..65535 кадров. Для 16 бит размер буфера в байтах
равен len*2; нужны 16-битные обращения к DR, выравнивание и Half_Word у DMA.
На F7/G0 порог RX FIFO соответствует размеру кадра.

| Режим | Методы | Значение OK при возврате |
|---|---|---|
| Блокирующий | Send, Receive, Send_Receive | Обмен завершён |
| IRQ | Send_IRQ, Receive_IRQ, SendReceive_IRQ | Обмен запущен |
| DMA | Send_DMA, Receive_DMA, SendReceive_DMA | Обмен запущен |

Receive_DMA использует full duplex с dummy, без RXONLY.
Dummy равен 0xFF для 8 бит и 0xFFFF для 16 бит.

До SetUp возвращается NotInit, при занятом SPI — Busy. Нулевой/некорректный буфер,
длина 0 или >65535, неверная ширина/выравнивание, отсутствующий пин данных или
необходимый DMA — Error. Блокирующий timeout один на весь вызов, требует GetTick;
после Timeout/Error можно начать новый обмен. У асинхронных методов внутреннего
дедлайна нет: приложение контролирует время и может отменить обмен через DeInit.

### 4. Process и завершение

IRQ обслуживает кадры, DMA TC завершает работу с буфером. Физическое завершение
проверяет Process в mainloop. Для master сначала должны быть обработаны все
запланированные TX/RX кадры, затем F4 проверяет TXE=1/BSY=0, F7/G0 — пустой
TX FIFO и BSY=0. Process не содержит ожидания BSY и возвращает Busy, пока рано.

~~~cpp
// После успешного SendReceive_IRQ или SendReceive_DMA:
SysStatus state = bus.Process();
if (state == SysStatus::OK) {
    uint32_t frames = bus.GetDataReceivedCount();
    // Теперь RX доступен CPU; чтение счётчика его не сбрасывает.
}
~~~

Process останавливает источники/DMA, возвращает кеш, снимает необязательный
программный SS, публикует статус и вызывает OnComplete/OnError. Эти уведомления
выполняются из main/Process, не из DMA IRQ. GetStatus только читает статус.
Для блокирующих вызовов OnComplete не вызывается; OnError сообщает аппаратные ошибки.

### 5. Slave до снятия аппаратного NSS

len — вместимость буфера. Пакет может быть короче; сохраняются первые len полных
кадров. GetDataReceivedCount возвращает число сохранённых кадров после успешного
Process. Дополнительные кадры отбрасываются с IsTruncated=true. Ровно len кадров
не считаются усечением. После TX-буфера slave выдаёт dummy 0xFF/0xFFFF до снятия NSS.
Хвост обслуживает TXE IRQ, в том числе после TX DMA TC: частота SCK должна
учитывать задержку обслуживания. Непрерывная выдача на максимальной SCK не гарантируется.

Приложение настраивает EXTI по нарастающему фронту на пине аппаратного NSS,
само проверяет/сбрасывает флаг и вызывает NssReleased:

~~~cpp
SPI slave(SPI1, SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7,
          SPI::_1::MISO::PA6, SPI::_1::SS::PA4);
static uint8_t packet[64];
// Проверить SetUp(Slave, Hard), затем Receive_IRQ(packet, sizeof(packet)).

// В пользовательском EXTI после подтверждения флага снятия NSS:
slave.NssReleased();

// В main после старта:
if (slave.Process() == SysStatus::OK) {
    uint32_t frames = slave.GetDataReceivedCount();
    bool truncated = slave.IsTruncated();
    // Обработать packet[0..frames), затем снова взвести приём.
}
~~~

NssReleased сразу останавливает источники и DMA, но не обслуживает кеш и не
вызывает завершающие callbacks. Process инвалидирует записанный DMA префикс,
дочитывает хвост RX FIFO и публикует результат. После slave-пакета SPI также
сбрасывается для удаления заранее загруженных, но не переданных TX кадров. Slave BSY не используется как
граница пакета из-за аппаратных ограничений.

Обработчики NSS EXTI, SPI и DMA этого объекта не должны прерывать друг друга.
Фронт должен быть обслужен до следующего пакета, а master должен дождаться
Process и повторного взведения slave. Приём без пауз между пакетами не обещается.
Только опрос NSS в mainloop может пропустить короткую паузу.

Неполный последний кадр и корректность протокола проверяет приложение.
OVR до заполнения буфера означает потерю данных и Error. После заполнения —
усечение с сохранением префикса.

### 6. DMA и кеш

AttachDMA возвращает SysInitStatus, проверяет полный DMAReq, результаты SetUp
и регистрации. Повторное подключение или занятый DMA — ошибка. При отказе
освобождаются только ресурсы, полученные этим вызовом. Общий DMA IRQ регистрирует
объект SPI один раз. Ширина DMA соответствует настроенному кадру.

~~~cpp
// F4/F7:
DMA_Sx tx(DMA_Sx::Req::Spi1::TX_DMA2_Stream3);
DMA_Sx rx(DMA_Sx::Req::Spi1::RX_DMA2_Stream0);
// G0:
// DMA_Sx tx(DMA1_Channel2, DMA_Sx::Req::Spi1::TX);
// DMA_Sx rx(DMA1_Channel3, DMA_Sx::Req::Spi1::RX);
// Проверить bus.AttachDMA(&tx, &rx), затем запускать обмен.
~~~

Send_DMA требует TX DMA, Receive_DMA — RX DMA, SendReceive_DMA — оба.
Если другая сторона DMA не подключена, её обслуживает SPI IRQ: выдаёт dummy
или отбрасывает RX. Для высокой частоты SCK нужны оба DMA, иначе остаётся
покадровая нагрузка на CPU. minc=DISABLE повторяет первый кадр TX.

Кеш обслуживается через System: clean TX, clean+invalidate перед RX, invalidate
DMA-записанного префикса после подтверждения DMA EN=0. Размер — кадры*ширина.
При minc=DISABLE чистится только повторяемый кадр. CPU не обращается к активным
RX cache lines и не меняет TX. Некешируемые области проекта пропускаются
в System; на F4/G0 helpers пустые. Буферы должны быть доступны DMA. Для одновременного TX/RX используйте отдельные
буферы: приём в ещё не прочитанные DMA исходные данные нарушает передачу.
Dummy неизменяем и находится во Flash; discard word CPU не читает/не меняет.

### 7. Ошибки, callbacks и освобождение

IRQ_en меняет только CR2, не реестр/NVIC. SetUp регистрирует SPI и включает его
NVIC; AttachDMA регистрирует отдельные DMA-векторы. Источники разрешаются только
для активного обмена. DeInit удаляет лишь свои записи, сохраняя общие обработчики.

OnError(uint32_t) получает маску Overrun/ModeFault/Crc/Frame/DmaTx/DmaRx.
При ошибке источники останавливаются в IRQ; кеш и уведомление — в Process.
При timeout/error сбрасывается только прерванный SPI для удаления старой очереди
и аппаратного состояния. Программный SS снимается.

OnTxEmpty, OnRxByte, OnRxWord, OnDmaTxComplete, OnDmaRxComplete защищённые и имеют
стандартную обработку. Наследник вызывает базовый callback, если она нужна.
OnComplete/OnError — уведомления. Покадровые callbacks могут выполняться также
из блокирующего вызова или при дочитывании FIFO в main. Менять реестр/подключение
в callbacks запрещено.

DeInit отменяет обмен без callbacks, останавливает DMA, возвращает активный RX
буфер CPU, удаляет свои IRQ-записи и освобождает DMA. Перед этим пользователь
отключает/согласовывает свой NSS EXTI. Счётчик/усечение сбрасываются при новом
старте либо DeInit.

## References / Источники

- [ST AN5543 — data flow, DMA and physical completion, NSS](https://www.st.com/resource/en/application_note/an5543-guidelines-for-enhanced-spi-communication-on-stm32-mcus-and-mpus-stmicroelectronics.pdf).
- [ST F7 SPI driver — FIFO threshold and transaction-end checks](https://raw.githubusercontent.com/STMicroelectronics/stm32f7xx-hal-driver/master/Src/stm32f7xx_hal_spi.c).
- [ST F429 errata §2.12.5 — slave BSY](https://www.st.com/resource/en/errata_sheet/dm00068628-stm32f427437-and-stm32f429439-line-limitations-stmicroelectronics.pdf).
- [ST F411 datasheet — AF mapping](https://www.st.com/resource/en/datasheet/stm32f411ce.pdf).
- [ST F410 datasheet — SPI5 AF6 pins](https://www.st.com/resource/en/datasheet/stm32f410rb.pdf).
- [ST F412 datasheet — AF mapping](https://www.st.com/resource/en/datasheet/stm32f412ve.pdf).
- [ST F413 datasheet — AF mapping](https://www.st.com/resource/en/datasheet/stm32f413cg.pdf).
- [ST F429 datasheet — SPI4–SPI6 pins](https://www.st.com/resource/en/datasheet/stm32f429zi.pdf).
- [ST F767 datasheet — SPI4–SPI6 pins](https://www.st.com/resource/en/datasheet/stm32f767vi.pdf).
- [ST RM0385 — DMA request mapping](https://www.st.com/resource/en/reference_manual/dm00124865.pdf).
- [ST RM0383 — F411 DMA request mapping](https://www.st.com/resource/en/reference_manual/dm00119316-stm32f4-series-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).
- [ST RM0401 — F410 DMA request mapping](https://www.st.com/resource/en/reference_manual/dm00180366-stm32f410-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).
- [ST G0B1 datasheet — SPI3 pins](https://www.st.com/resource/en/datasheet/stm32g0b1kc.pdf).
- [ST G0 LL DMAMUX — SPI3 RX=66, TX=67](https://raw.githubusercontent.com/STMicroelectronics/stm32g0xx-hal-driver/master/Inc/stm32g0xx_ll_dmamux.h).
- [DMA.md](DMA.md), [IRQ_Registry.md](IRQ_Registry.md), [System.md](System.md).
