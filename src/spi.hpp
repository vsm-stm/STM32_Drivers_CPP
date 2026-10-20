/**
 * @file spi.hpp
 * @brief SPI master/slave driver for STM32F4/F7/G0.
 *
 * One object owns one hardware SPI and its IRQ/DMA registrations. Initialize
 * System/RCC first. SetUp() fixes clock polarity/phase, divider and 8/16-bit frame
 * width. Direction and optional DMA selections are fixed by the constructor.
 * TX uses transmit-only BIDIMODE/BIDIOE and needs no RX DMA or RX IRQ.
 * RX uses full duplex without RXONLY: master TX IRQ supplies 0xFF/0xFFFF dummy
 * frames; slave RX needs no dummy writer. TXRX permits Send, Receive and simultaneous
 * Send/Receive; Send consumes/discards RX and Receive supplies dummy frames.
 *
 * Pins and chip select
 * --------------------
 * Named SPI::_N pins carry their AF and peripheral base. The constructor checks
 * peripheral ownership at run time; PIN performs its own compile-time numeric
 * checks for GPIOx_BASE declarations. SCK is required; MOSI/MISO may be PIN{} when
 * the application's operation does not use that direction externally.
 * SS remains optional in the constructor for software-NSS master: a supplied pin
 * is driven low/high around one operation. Without it, the application selects
 * devices and may keep its own CS active across several operations. Hard NSS
 * requires the matching SS pin. Slave uses hardware NSS only.
 *
 * Completion and callbacks
 * ------------------------
 * Blocking methods poll with one GetTick timeout covering the complete call.
 * IRQ/DMA methods return after starting. Call Process() regularly from main:
 * it never waits for BSY and is responsible for physical completion, cache return,
 * automatic software-CS release, final status and OnComplete()/OnError().
 * IRQ callbacks feed/drain data and handle DMA TC; they do not finish the wire
 * transaction or mutate the IRQ registry. Busy lasts until Process() finishes.
 * SPI IRQ shares entry CR2/SR snapshots; FIFO reads refresh SR only for the next
 * frame. DMA handlers read TC and supported errors together in one ISR access.
 * Hardware CRC and TI mode are disabled; only OVR/MODF are SPI error reasons.
 * No asynchronous timeout is imposed; the application owns its deadline and
 * can cancel/release through DeInit(). GetStatus() is a pure read.
 *
 * Slave packets
 * -------------
 * Arm the buffer before the master selects NSS. len is capacity in frames;
 * the packet ends when the application calls NssReleased() from its rising-edge
 * EXTI handler on the hardware NSS pin. Configure/acknowledge EXTI yourself.
 * NSS EXTI, SPI and DMA handlers for this object must not preempt one another.
 * They must run before another packet begins. Rearm only after Process() returns
 * the completed packet; this driver does not promise gapless slave reception.
 * The first len received frames are retained. Further frames set IsTruncated();
 * normal truncation does not turn the packet into SysStatus::Error. OVR before
 * the buffer is full is a hardware error: the missing data cannot be recovered.
 * After the TX payload, slave supplies 0xFF/0xFFFF until NSS is released.
 * SCK must allow the selected DMA/IRQ servicing latency; IRQ dummy feeding is
 * not a guarantee of uninterrupted output at the peripheral's maximum SCK.
 *
 * DMA and F7 cache
 * ----------------
 * Pass DMAReq after SS: named requests on F4/F7, physical channels on G0.
 * SPI owns both DMA objects. SetUp() acquires/configures/registers them;
 * DeInit releases resources and preserves constructor selections. F4/F7 requests
 * are checked in the constructor for SPI ownership, direction and distinct streams.
 * G0 derives the request source from SPI and TX/RX; only channels are user-selected.
 * SPI_USE_ATOMIC selects ordinary (0, default) or ST atomic (1) control updates.
 * Unattached directions use SPI IRQ servicing where the operation permits it.
 * F7 RX buffers own all touched 32-byte cache lines, including padding; CPU
 * must not access them while Busy. System cache helpers skip project-defined
 * non-cacheable regions and are empty on F4/G0. DMA-accessible RAM is required.
 * The SPI IRQ and DMA TX/RX IRQs have separate registered handler objects; a
 * shared DMA vector therefore needs two registry slots. No IRQ waits for hardware.
 * F4/F7 collect forced-stop completion and counts in the DMA IRQ. On G0 a slave
 * NSS stop requires the last DMA request already serviced; Process() reads the
 * stopped count. Arbitrary aborts discard the packet, without using CNDTR.
 *
 * Blocking master, application-owned CS
 * @code
 *   constinit SPI bus(SPI::_1::instance, SPI::Direction::TXRX, SPI::_1::SCK::PA5,
 *           SPI::_1::MOSI::PA7, SPI::_1::MISO::PA6);
 *   if (bus.SetUp(SPI::Master_sel::Master, SPI::NSS_ctrl::Software) ==
 *       SysInitStatus::InitOK) {
 *       PIN cs(GPIOB, 0);
 *       cs.SetUp(PIN::TYPE::OUTPUT_PushPull, PIN::OUTPUT_SPEED::Low, PIN::LVL::HIGH);
 *       uint8_t command[] = {0x9F};
 *       uint8_t reply[3];
 *       cs.SetLevel(0);
 *       SysStatus sent = bus.Send(command, 1);
 *       if (sent == SysStatus::OK) bus.Receive(reply, 3); // Check this result too.
 *       cs.SetLevel(1); // Also release on an error.
 *   }
 * @endcode
 *
 * Async master with automatic software CS
 * @code
 *   constexpr SPI::DMAReq tx = SPI::_1::DMA::TX_DMA2_Stream3; // F4/F7
 *   constexpr SPI::DMAReq rx = SPI::_1::DMA::RX_DMA2_Stream0;
 *   constinit SPI bus(SPI::_1::instance, SPI::Direction::TXRX, SPI::_1::SCK::PA5,
 *           SPI::_1::MOSI::PA7, SPI::_1::MISO::PA6, PIN(GPIOB_BASE, 0), tx, rx);
 *   // After successful bus.SetUp(Master, Software):
 *   // Start SendReceive_DMA(tx_buffer, rx_buffer, frame_count); check the result.
 *   // In main, call bus.Process() until it returns OK/Error; only then use RX.
 * @endcode
 *
 * Receive-only master with one DMA (G0 example)
 * @code
 *   constexpr SPI::DMAReq rx = DMA1_Channel3_BASE;
 *   constinit SPI bus(SPI::_1::instance, SPI::Direction::RX, SPI::_1::SCK::PA5,
 *           PIN{}, SPI::_1::MISO::PA6, PIN{}, {}, rx);
 *   // Check bus.SetUp(Master, Software), then Receive_DMA(buffer, frame_count).
 *   // TX IRQ provides dummy clocks; no MOSI pin or TX DMA is required.
 *   // Call Process() from main and check OK before using the buffer.
 * @endcode
 *
 * Slave and 16-bit examples are also documented at the corresponding methods
 * below and in SPI.md. Keep detailed callback contracts when overriding them.
 */
#ifndef SPI_HPP_
#define SPI_HPP_

#include "system.hpp"
#include "rcc.hpp"
#include "gpio.hpp"
#include "dma.hpp"
#include "irq_registry.hpp"

// Change here and rebuild this driver and its consumers.
#ifndef SPI_USE_ATOMIC
#define SPI_USE_ATOMIC 0
#endif

#if SPI_USE_ATOMIC
#define SPI_SET_BIT ATOMIC_SET_BIT
#define SPI_CLEAR_BIT ATOMIC_CLEAR_BIT
#define SPI_MODIFY_REG ATOMIC_MODIFY_REG
#else
#define SPI_SET_BIT SET_BIT
#define SPI_CLEAR_BIT CLEAR_BIT
#define SPI_MODIFY_REG MODIFY_REG
#endif

class SPI : public IIRQHandler
{
public:
    /// Fixed physical direction; transfer methods still select polling/IRQ/DMA.
    enum class Direction : uint8_t { TX, RX, TXRX };
    /// Only the role bit; NSS policy is configured independently by SetUp().
    enum class Master_sel { Slave = 0, Master = SPI_CR1_MSTR };
    enum class NSS_ctrl { Hard = 0, Software };
    enum class Data_frame_format {
#if defined(STM32F4)
        Byte = 0, Half_Word = SPI_CR1_DFF
#else
        Byte = 7U << SPI_CR2_DS_Pos, Half_Word = 15U << SPI_CR2_DS_Pos
#endif
    };
    enum class Frame_Format { MSB = 0, LSB = SPI_CR1_LSBFIRST };
    enum class cPolPha {
        None = 0, cPha = SPI_CR1_CPHA, cPol = SPI_CR1_CPOL,
        cPolPha = SPI_CR1_CPHA | SPI_CR1_CPOL
    };
    enum class BaudRate { DIV2 = 0, DIV4, DIV8, DIV16, DIV32, DIV64, DIV128, DIV256 };
    /// Peripheral source bits only. IRQ_en() does not change registry or NVIC.
    enum class IRQ { TXE = SPI_CR2_TXEIE, RXNE = SPI_CR2_RXNEIE, ERR = SPI_CR2_ERRIE };
    /// Combined reasons reported to OnError(); DMA errors use separate high bits.
    enum class Error : uint32_t {
        Overrun = SPI_SR_OVR, ModeFault = SPI_SR_MODF,
        DmaTx = 1U << 16, DmaRx = 1U << 17
    };

#if defined(STM32G0)
    using DMAReq = uintptr_t; ///< Source is implied by SPI and direction.
#else
    using DMAReq = DMA_Sx::DMAReq;
#endif

    /** Fixed device metadata from spi_defs.hpp; no runtime lookup. */
    struct Instance {
        uintptr_t periph;
        uintptr_t clk_reg;
        uint32_t clk_bit;
        uintptr_t rst_reg;
        uint32_t rst_bit;
        const uint32_t* bus_clk;
        IRQn_Type irq;
#if defined(STM32G0)
        uint32_t dma_tx_request, dma_rx_request;
#endif
    };
#include "spi_defs.hpp"

    /**
     * @brief Store pins and hardware; no register access until SetUp().
     * @param instance SPI::_N::instance for the selected hardware block.
     * @param direction Fixed TX, RX or TXRX; incompatible transfer methods return Error.
     * @param sck Named SPI::_N::SCK pin.
     * @param mosi Optional named MOSI; defaults to PIN{} if externally unused.
     * @param miso Named MISO, or PIN{} if externally unused.
     * @param ss Optional software CS GPIO for master; hardware NSS pin for Hard.
     * @param dma_tx Optional TX DMA, unused/forbidden for Direction::RX.
     * @param dma_rx Optional RX DMA, unused/forbidden for Direction::TX.
     * Use constinit: incompatible named pins or DMA routes are compilation errors.
     * Objects may not be copied/moved; call DeInit() before destruction.
     */
    constexpr explicit SPI(const Instance& instance, Direction direction, PIN sck, PIN mosi = PIN{},
                 PIN miso = PIN{}, PIN ss = PIN{},
                 DMAReq dma_tx = {}, DMAReq dma_rx = {})
        : _direction(direction),
#if defined(STM32G0)
          _dma_tx(dma_tx, instance.dma_tx_request), _dma_rx(dma_rx, instance.dma_rx_request),
#else
          _dma_tx(dma_tx), _dma_rx(dma_rx),
#endif
          _dma_tx_irq(*this), _dma_rx_irq(*this),
          _instance(instance), _clk(sck), _mosi(mosi), _miso(miso), _ss(ss)
    {
        if (!sck.IsValid())
            System::DebugTrap("SPI: SCK pin is required");
        if (sck.periph_base && sck.periph_base != instance.periph)
            System::DebugTrap("SPI: SCK pin belongs to another peripheral");
        if (mosi.IsValid() && mosi.periph_base && mosi.periph_base != instance.periph)
            System::DebugTrap("SPI: MOSI pin belongs to another peripheral");
        if (miso.IsValid() && miso.periph_base && miso.periph_base != instance.periph)
            System::DebugTrap("SPI: MISO pin belongs to another peripheral");
        if (ss.IsValid() && ss.periph_base && ss.periph_base != instance.periph)
            System::DebugTrap("SPI: NSS pin belongs to another peripheral");
        if (!DMA_Sx::RouteMatches(dma_tx, instance.periph, DMA_Sx::DIR::To_Per))
            System::DebugTrap("SPI: invalid TX DMA route");
        if (!DMA_Sx::RouteMatches(dma_rx, instance.periph, DMA_Sx::DIR::From_Per))
            System::DebugTrap("SPI: invalid RX DMA route");
        if (DMA_Sx::SameResource(dma_tx, dma_rx))
            System::DebugTrap("SPI: TX and RX require different DMA resources");
        if (direction != Direction::TX && direction != Direction::RX && direction != Direction::TXRX)
            System::DebugTrap("SPI: invalid direction");
        if (direction == Direction::TX && _dma_rx.HasSelection())
            System::DebugTrap("SPI: TX direction cannot use RX DMA");
        if (direction == Direction::RX && _dma_tx.HasSelection())
            System::DebugTrap("SPI: RX direction cannot use TX DMA");
    }
    SPI() = delete;
    SPI(const SPI&) = delete;
    SPI(SPI&&) = delete;
    SPI& operator=(const SPI&) = delete;
    SPI& operator=(SPI&&) = delete;
    ~SPI() = default;

    /// Setup arguments remain in SetUp; physical direction is constructor-bound.
    struct Init_struct_Typedef {
        Master_sel mstr;
        NSS_ctrl nss_ctrl;
        Data_frame_format dff;
        Frame_Format ff;
        cPolPha cpolpha;
        BaudRate br;
    };
    SysInitStatus SetUp(Init_struct_Typedef s)
    { return SetUp(s.mstr, s.nss_ctrl, s.dff, s.ff, s.cpolpha, s.br); }

    /**
     * @brief Configure one SPI, pins, source-free IRQ registration and NVIC.
     * @return InitError for an unsupported peripheral, invalid enums/missing SCK,
     *         Hard without SS, software-NSS slave, repeated setup or IRQ failure.
     * Slave requires hardware NSS; configure EXTI on the same pin in the application.
     * Byte uses 8-bit DR access and the 8-bit RX threshold on F7/G0; Half_Word
     * uses 16-bit DR access, the half-word threshold and 16-bit DMA transfers.
     * TX sets BIDIMODE/BIDIOE; RX/TXRX leave RXONLY clear. Default NSS is Hard;
     * Owned DMA objects are configured here, after constructor DMAReq validation, with separate
     * registration per direction. On failure only newly acquired resources are
     * released. No DMA is required for blocking/IRQ calls. Pass Software explicitly
     * for a master without a hardware NSS pin.
     *
     * @code
     *   bus.SetUp(SPI::Master_sel::Master, SPI::NSS_ctrl::Software,
     *             SPI::Data_frame_format::Half_Word, SPI::Frame_Format::MSB,
     *             SPI::cPolPha::None, SPI::BaudRate::DIV8);
     *   uint16_t frames[] = {0x1234, 0xABCD};
     *   bus.Send(frames, 2); // Two 16-bit frames, not four byte frames.
     * @endcode
     */
    SysInitStatus SetUp(Master_sel mstr, NSS_ctrl nss = NSS_ctrl::Hard,
                       Data_frame_format dff = Data_frame_format::Byte,
                       Frame_Format ff = Frame_Format::MSB,
                       cPolPha cpolpha = cPolPha::None, BaudRate br = BaudRate::DIV2);

    /// Low-level SPE controls; do not use to interrupt an owned active operation.
    void Enable() { if (_info) SPI_SET_BIT(SPIx->CR1, SPI_CR1_SPE); }
    void Disable() { if (_info) SPI_CLEAR_BIT(SPIx->CR1, SPI_CR1_SPE); }
    /// Optional active-low software master CS. Hard NSS and absent CS are untouched.
    void SlaveSelect(FunctionalState en) {
        if (_info && Master_slave == Master_sel::Master
            && nss_ctrl == NSS_ctrl::Software && _ss.IsValid()) _ss.SetLevel(!en);
    }
    /// Raw request gates; normal transfers manage them in start/stop order.
    void DMA_TX(FunctionalState en) {
        if (en) SPI_SET_BIT(SPIx->CR2, SPI_CR2_TXDMAEN); else SPI_CLEAR_BIT(SPIx->CR2, SPI_CR2_TXDMAEN);
    }
    void DMA_RX(FunctionalState en) {
        if (en) SPI_SET_BIT(SPIx->CR2, SPI_CR2_RXDMAEN); else SPI_CLEAR_BIT(SPIx->CR2, SPI_CR2_RXDMAEN);
    }
    /// Changes CR2 only; safe for a callback to disable its own source.
    /// Enable sources only for an active transfer; SetUp() owns NVIC registration.
    void IRQ_en(IRQ irq, FunctionalState en) {
        if (!_info) return;
        if (en) SPI_SET_BIT(SPIx->CR2, static_cast<uint32_t>(irq));
        else SPI_CLEAR_BIT(SPIx->CR2, static_cast<uint32_t>(irq));
    }

    /**
     * @name Blocking transfers
     * @param len Frames (1..65535), not bytes for uint16_t buffers.
     * @param timeout One GetTick budget in ms for the entire operation.
     * @return NotInit before setup, Busy during another operation, Error for
     * null/zero arguments, width/alignment mismatch, missing data pins or hardware
     * errors; Timeout after cleanup; OK after physical master completion or slave NSS.
     * TXRX Send discards RX; TX-only has no RX. Master Receive sends dummy frames.
     * A slave buffer is capacity; NSS ends a shorter packet and excess RX is
     * discarded with IsTruncated() set. The user's EXTI must still call NssReleased().
     * No OnComplete notification for blocking calls. Hardware errors call OnError().
     * RX data on Error/Timeout must be discarded.
     */
    SysStatus Send(uint8_t* data, uint32_t len, uint32_t timeout = 100)
    { return Poll(data, nullptr, len, Data_frame_format::Byte, timeout); }
    SysStatus Receive(uint8_t* data, uint32_t len, uint32_t timeout = 100)
    { return Poll(nullptr, data, len, Data_frame_format::Byte, timeout); }
    SysStatus Send_Receive(uint8_t* tx, uint8_t* rx, uint32_t len, uint32_t timeout = 100)
    { return tx && rx ? Poll(tx, rx, len, Data_frame_format::Byte, timeout) : (_info ? SysStatus::Error : SysStatus::NotInit); }
    SysStatus Send(uint16_t* data, uint32_t len, uint32_t timeout = 100)
    { return Poll(data, nullptr, len, Data_frame_format::Half_Word, timeout); }
    SysStatus Receive(uint16_t* data, uint32_t len, uint32_t timeout = 100)
    { return Poll(nullptr, data, len, Data_frame_format::Half_Word, timeout); }
    SysStatus Send_Receive(uint16_t* tx, uint16_t* rx, uint32_t len, uint32_t timeout = 100)
    { return tx && rx ? Poll(tx, rx, len, Data_frame_format::Half_Word, timeout) : (_info ? SysStatus::Error : SysStatus::NotInit); }

    /**
     * @name IRQ transfers
     * Same buffer/width/count rules as blocking calls. OK means started.
     * Keep buffers alive and TX unchanged until Process() finishes.
     * RX is serviced in RX/TXRX, including discard for TXRX Send and slave truncation.
     * Software CS stays asserted until Process(), never released by a byte IRQ.
     */
    SysStatus Send_IRQ(uint8_t* data, uint32_t len)
    { return Start(data, nullptr, len, Data_frame_format::Byte, Mode::Irq); }
    SysStatus Receive_IRQ(uint8_t* data, uint32_t len)
    { return Start(nullptr, data, len, Data_frame_format::Byte, Mode::Irq); }
    SysStatus SendReceive_IRQ(uint8_t* tx, uint8_t* rx, uint32_t len)
    { return tx && rx ? Start(tx, rx, len, Data_frame_format::Byte, Mode::Irq) : (_info ? SysStatus::Error : SysStatus::NotInit); }
    SysStatus Send_IRQ(uint16_t* data, uint32_t len)
    { return Start(data, nullptr, len, Data_frame_format::Half_Word, Mode::Irq); }
    SysStatus Receive_IRQ(uint16_t* data, uint32_t len)
    { return Start(nullptr, data, len, Data_frame_format::Half_Word, Mode::Irq); }
    SysStatus SendReceive_IRQ(uint16_t* tx, uint16_t* rx, uint32_t len)
    { return tx && rx ? Start(tx, rx, len, Data_frame_format::Half_Word, Mode::Irq) : (_info ? SysStatus::Error : SysStatus::NotInit); }

    /**
     * @name DMA transfers
     * OK means started. Count is 1..65535 frames; cache byte count is len*frame size.
     * Send_DMA requires TX DMA; Receive_DMA requires RX DMA; SendReceive_DMA both.
     * Direction::TX uses only TX DMA. Direction::RX master uses RX DMA plus TX IRQ
     * for dummy clocks; slave RX uses only RX DMA. It never needs a second DMA.
     * In TXRX with one DMA direction, the other is serviced by SPI IRQ (dummy TX
     * or discarded RX). Both DMA directions reduce TXRX per-frame IRQ overhead.
     * minc=DISABLE repeats the first source frame and cleans only that source frame.
     * F7 cacheable RX allocations own full 32-byte lines. CPU does not access RX
     * or change TX before Process() finishes. TX/RX overlapping buffer ownership
     * and DMA reachability are the caller's responsibility.
     */
    SysStatus Send_DMA(uint8_t* data, uint32_t len, FunctionalState minc = ENABLE)
    { return Start(data, nullptr, len, Data_frame_format::Byte, Mode::Dma, minc); }
    SysStatus Receive_DMA(uint8_t* data, uint32_t len)
    { return Start(nullptr, data, len, Data_frame_format::Byte, Mode::Dma); }
    SysStatus SendReceive_DMA(uint8_t* tx, uint8_t* rx, uint32_t len)
    { return tx && rx ? Start(tx, rx, len, Data_frame_format::Byte, Mode::Dma) : (_info ? SysStatus::Error : SysStatus::NotInit); }
    SysStatus Send_DMA(uint16_t* data, uint32_t len, FunctionalState minc = ENABLE)
    { return Start(data, nullptr, len, Data_frame_format::Half_Word, Mode::Dma, minc); }
    SysStatus Receive_DMA(uint16_t* data, uint32_t len)
    { return Start(nullptr, data, len, Data_frame_format::Half_Word, Mode::Dma); }
    SysStatus SendReceive_DMA(uint16_t* tx, uint16_t* rx, uint32_t len)
    { return tx && rx ? Start(tx, rx, len, Data_frame_format::Half_Word, Mode::Dma) : (_info ? SysStatus::Error : SysStatus::NotInit); }

    /**
     * @brief Complete an async operation from main without waiting for hardware.
     * @return Busy until master buffers and the SPI transmitter are empty/idle,
     *         or until slave NssReleased(); final OK/Error afterwards.
     * Calls OnComplete()/OnError() after stopping sources, DMA/cache return and CS.
     * Poll regularly. GetStatus() alone does not drive completion.
     */
    SysStatus Process();

    /**
     * @brief Notify the slave's hardware NSS rising edge from user EXTI.
     * The user configures/acknowledges EXTI. This method stops SPI sources and DMA
     * immediately, retaining data/FIFO for Process(); it does not call completion
     * callbacks or maintain cache. Ignored unless an active slave operation exists.
     * No next packet may begin before Process() and rearm. Keep this EXTI and the
     * SPI/DMA handlers at priorities preventing mutual preemption.
     * This call contains no hardware wait. F4/F7 await stop TC in DMA IRQ before
     * Process() publishes data. G0 requires the last request serviced before this
     * call; Process() reads CNDTR after stop. Error packets must be discarded.
     * @code
     *   // In the user's handler, after acknowledging the actual NSS EXTI flag:
     *   slave.NssReleased();
     *   // In main:
     *   if (slave.Process() == SysStatus::OK) {
     *       auto frames = slave.GetDataReceivedCount();
     *       bool truncated = slave.IsTruncated();
     *       // Consume the first frames; then Receive_IRQ/Receive_DMA to rearm.
     *   }
     * @endcode
     */
    void NssReleased();
    /// Pure read. A completed buffer is reusable only after Process() leaves Busy.
    SysStatus GetStatus() const { return _status; }
    /// Successful packet length, saturated at capacity; 0 before success/while Busy.
    uint32_t GetDataReceivedCount() const {
        return _status == SysStatus::OK && _direction != Direction::TX ? _capacity - _rx_remaining : 0U;
    }
    /// Excess slave RX after capacity; cleared by the next start or DeInit().
    bool IsTruncated() const { return _truncated; }

    /**
     * @brief Cancel, stop SPI/DMA, return RX cache ownership and release registrations.
     * Removes this SPI only from shared vectors; shared NVIC/controller clocks
     * are retained. Releases attached DMA. Does not invoke completion callbacks.
     * Call from main, never from a callback. Repeated setup requires DeInit().
     * The application owns EXTI and must disable/serialize its NSS callback first.
     */
    void DeInit();

private:
    enum class Mode : uint8_t { Polling, Irq, Dma };
    using PeriphInfo = Instance;
    static constexpr uint32_t IRQ_MASK = SPI_CR2_TXEIE | SPI_CR2_RXNEIE | SPI_CR2_ERRIE;
    static constexpr uint32_t DMA_MASK = SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN;
    // Motorola SPI without hardware CRC; TI frame errors are not applicable.
    static constexpr uint32_t SPI_ERRORS = SPI_SR_OVR | SPI_SR_MODF;

    const uint8_t* _tx_data = nullptr;
    uint8_t* _rx_data = nullptr;
    uint16_t _capacity = 0;
    volatile uint16_t _tx_remaining = 0;
    volatile uint16_t _rx_remaining = 0;
    Data_frame_format _frame_format = Data_frame_format::Byte;
    Mode _mode = Mode::Polling;
    volatile SysStatus _status{SysStatus::NotInit};
    volatile uint32_t _errors = 0;
    volatile bool _truncated = false;
    volatile bool _nss_ended = false;
    const Direction _direction;
    DMA_Sx _dma_tx;
    DMA_Sx _dma_rx;
    // Active directions awaiting a DMA completion/stop event; SPI CR2 bit values.
    volatile uint8_t _dma_pending = 0;

    class DmaTxIRQ final : public IIRQHandler {
    public:
        constexpr explicit DmaTxIRQ(SPI& spi) : _spi(spi) {}
        void HandleIRQ() override { _spi.HandleDmaTxIRQ(); }
    private:
        SPI& _spi;
    };
    class DmaRxIRQ final : public IIRQHandler {
    public:
        constexpr explicit DmaRxIRQ(SPI& spi) : _spi(spi) {}
        void HandleIRQ() override { _spi.HandleDmaRxIRQ(); }
    private:
        SPI& _spi;
    };
    DmaTxIRQ _dma_tx_irq;
    DmaRxIRQ _dma_rx_irq;
    void HandleDmaTxIRQ();
    void HandleDmaRxIRQ();

    uint16_t ReadFrame() const;
    void WriteFrame(uint16_t frame);
    void StoreFrame(uint16_t frame);
    // RX/error handling share the SR snapshot and budget, including OVR discard.
    uint32_t FifoFrames() const {
#if defined(STM32F4)
        return 1U;
#else
        return _frame_format == Data_frame_format::Byte ? 4U : 2U;
#endif
    }
    void DrainRx(uint32_t& sr, uint32_t& remaining);
    uint32_t ReadErrors(uint32_t& sr, uint32_t& remaining);
    void StopTransfer();
    SysInitStatus SetUpDMA();
    void Finish(SysStatus result, uint32_t errors = 0);
    SysStatus Start(const void* tx, void* rx, uint32_t len, Data_frame_format frame_format,
                    Mode mode, FunctionalState minc = ENABLE);
    SysStatus Poll(const void* tx, void* rx, uint32_t len,
                   Data_frame_format frame_format, uint32_t timeout);
    void HandleIRQ() override final;

protected:
    const Instance& _instance;
	SPI_TypeDef* SPIx = nullptr;
    PIN _clk, _mosi, _miso, _ss;
    NSS_ctrl nss_ctrl{};
    Master_sel Master_slave{};
    const PeriphInfo* _info = nullptr;

    /**
     * @name Operational callbacks
     * IRQ callbacks run in SPI/DMA IRQ. The default implementations move data or
     * record DMA completion. Overrides call the base to retain that processing.
     * OnRxByte/OnRxWord can also run in blocking calls or final FIFO drain.
     * Never change registry/attachment from callbacks; no internal locks exist.
     */
    virtual void OnTxEmpty();
    virtual void OnRxByte(uint8_t frame);
    virtual void OnRxWord(uint16_t frame);
    virtual void OnDmaTxComplete();
    virtual void OnDmaRxComplete();
    /// Notification after physical completion; Process()/main context, not a TXE IRQ.
    virtual void OnComplete() {}
    /// Combined Error bits; main/Process or blocking calling context, after cleanup.
    virtual void OnError(uint32_t errors) { (void)errors; }
};

#endif // SPI_HPP_
