/**
 * @file spi.cpp
 * @brief Shared 8/16-bit SPI transfer engine and main-loop physical completion.
 *
 * SPI/DMA IRQ and the user's NSS EXTI must not preempt one another for one object.
 * No locks are introduced. DMA TC ends buffer feeding, never the wire transaction.
 * Process() owns final status/cache/CS handover. Slave packet boundaries come only
 * from the application's NssReleased() notification, not from BSY or DMA TC.
 */
#include "spi.hpp"

// DMA-only immutable dummy and discard storage.
static const uint16_t spi_dummy_word = 0xFFFFU;
static uint16_t spi_discard_word;

// DR requires halfword accesses for 16-bit frames. CMSIS declares DR as uint32_t;
// may_alias permits the required narrower MMIO access under GCC strict aliasing.
using SpiHalfwordRegister = uint16_t __attribute__((__may_alias__));

uint16_t SPI::ReadFrame() const
{
    if (_frame_format == Data_frame_format::Byte) return *reinterpret_cast<volatile uint8_t*>(&SPIx->DR);
    return *reinterpret_cast<volatile SpiHalfwordRegister*>(&SPIx->DR);
}

void SPI::WriteFrame(uint16_t frame)
{
    if (_frame_format == Data_frame_format::Byte) *reinterpret_cast<volatile uint8_t*>(&SPIx->DR) = static_cast<uint8_t>(frame);
    else *reinterpret_cast<volatile SpiHalfwordRegister*>(&SPIx->DR) = frame;
}

SysInitStatus SPI::SetUp(Master_sel mstr, NSS_ctrl nss, Data_frame_format dff,
                        Frame_Format ff, cPolPha cpolpha, BaudRate br)
{
    if (SPIx
        || (mstr != Master_sel::Master && mstr != Master_sel::Slave)
        || (nss != NSS_ctrl::Hard && nss != NSS_ctrl::Software)
        || (dff != Data_frame_format::Byte && dff != Data_frame_format::Half_Word)
        || (ff != Frame_Format::MSB && ff != Frame_Format::LSB)
        || (static_cast<uint32_t>(cpolpha) & ~(SPI_CR1_CPOL | SPI_CR1_CPHA))
        || static_cast<uint32_t>(br) > static_cast<uint32_t>(BaudRate::DIV256)
        || (nss == NSS_ctrl::Hard && !_ss.IsValid())
        || (mstr == Master_sel::Slave && nss != NSS_ctrl::Hard))
        return SysInitStatus::InitError;
    SPIx = reinterpret_cast<SPI_TypeDef*>(_instance.periph);

    SPI_SET_BIT(*reinterpret_cast<volatile uint32_t*>(_instance.clk_reg), _instance.clk_bit);
    (void)*reinterpret_cast<volatile uint32_t*>(_instance.clk_reg);
    SPI_SET_BIT(*reinterpret_cast<volatile uint32_t*>(_instance.rst_reg), _instance.rst_bit);
    SPI_CLEAR_BIT(*reinterpret_cast<volatile uint32_t*>(_instance.rst_reg), _instance.rst_bit);

    // AF selects the peripheral's input/output direction. Open-drain is not a
    // bus-conflict protection mechanism for an SPI input; use push-pull AF.
    _clk.SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High);
    if (_mosi.IsValid()) _mosi.SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High);
    if (_miso.IsValid()) _miso.SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High);
    if (_ss.IsValid()) {
        if (nss == NSS_ctrl::Hard) _ss.SetUp(PIN::TYPE::AF_PushPull);
        else _ss.SetUp(PIN::TYPE::OUTPUT_PushPull, PIN::OUTPUT_SPEED::Low, PIN::LVL::HIGH);
    }

    uint32_t cr1 = static_cast<uint32_t>(mstr) | static_cast<uint32_t>(ff)
        | static_cast<uint32_t>(cpolpha) | (static_cast<uint32_t>(br) << SPI_CR1_BR_Pos);
    if (nss == NSS_ctrl::Software) cr1 |= SPI_CR1_SSM | SPI_CR1_SSI;
    if (_direction == Direction::TX) cr1 |= SPI_CR1_BIDIMODE | SPI_CR1_BIDIOE;
#if defined(STM32F4)
    cr1 |= static_cast<uint32_t>(dff);
    SPIx->CR2 = nss == NSS_ctrl::Hard && mstr == Master_sel::Master ? SPI_CR2_SSOE : 0U;
#else
    SPIx->CR2 = static_cast<uint32_t>(dff)
        | (dff == Data_frame_format::Byte ? SPI_CR2_FRXTH : 0U)
        | (nss == NSS_ctrl::Hard && mstr == Master_sel::Master ? SPI_CR2_SSOE : 0U);
#endif
    SPIx->CR1 = cr1;

    const bool enabled = NVIC_GetEnableIRQ(_instance.irq) != 0;
    NVIC_DisableIRQ(_instance.irq);
    if (IRQ_Registry::Register(_instance.irq, this) != SysInitStatus::InitOK) {
        SPIx = nullptr;
        if (enabled) NVIC_EnableIRQ(_instance.irq);
        return SysInitStatus::InitError;
    }
    Master_slave = mstr;
    nss_ctrl = nss;
    _frame_format = dff;
    if (SetUpDMA() != SysInitStatus::InitOK) {
        IRQ_Registry::Unregister(_instance.irq, this);
        SPIx = nullptr;
        if (enabled) NVIC_EnableIRQ(_instance.irq);
        return SysInitStatus::InitError;
    }
    _status = SysStatus::OK;
    NVIC_EnableIRQ(_instance.irq);
    return SysInitStatus::InitOK;
}

// Each public transfer family has its own start path. Buffer presence is checked
// by the public wrappers; fixed configuration and buffer layout are caller contracts.
SysStatus SPI::StartIRQ(const void* tx, void* rx, uint32_t len)
{
    const SysStatus status = _status;
    if (status != SysStatus::OK) return status;
    PrepareTransfer(tx, rx, len, Mode::Irq);
    uint32_t sources = SPI_CR2_ERRIE;
    if (_rx_remaining) sources |= SPI_CR2_RXNEIE;
    if (_tx_remaining) sources |= SPI_CR2_TXEIE;
    EnableTransfer(sources);
    return SysStatus::OK;
}

SysStatus SPI::StartDMA(const void* tx, void* rx, uint32_t len, FunctionalState minc)
{
    const SysStatus status = _status;
    if (status != SysStatus::OK) return status;
    const bool tx_dma = _dma_tx.HasSelection();
    const bool rx_dma = _dma_rx.HasSelection();
    if ((tx && !tx_dma) || (rx && !rx_dma)) return SysStatus::Error;

    PrepareTransfer(tx, rx, len, Mode::Dma);
    constexpr uint32_t dma_irqs = static_cast<uint32_t>(DMA_Sx::IRQ::TC)
        | static_cast<uint32_t>(DMA_Sx::IRQ::TE)
#if defined(STM32F4) || defined(STM32F7)
        | static_cast<uint32_t>(DMA_Sx::IRQ::DME)
#endif
        ;
    if (tx_dma)
        _dma_tx.PrepareTransfer(reinterpret_cast<uint32_t>(tx ? tx : &spi_dummy_word),
            len, tx ? minc : DISABLE, dma_irqs);
    if (rx_dma)
        _dma_rx.PrepareTransfer(reinterpret_cast<uint32_t>(rx ? rx : &spi_discard_word),
            len, rx ? ENABLE : DISABLE, dma_irqs);

    const uint32_t frame_bytes = _frame_format == Data_frame_format::Byte ? 1U : 2U;
    if (tx) System::CacheClean(tx, minc == ENABLE ? len * frame_bytes : frame_bytes);
    if (rx) System::CacheCleanInvalidate(rx, len * frame_bytes);
    // Flash dummy and CPU-unread discard storage need no cache handover.

    uint32_t sources = SPI_CR2_ERRIE;
    if (rx_dma) sources |= SPI_CR2_RXDMAEN;
    else if (_rx_remaining) sources |= SPI_CR2_RXNEIE;
    if (tx_dma) sources |= SPI_CR2_TXDMAEN;
    else if (_tx_remaining) sources |= SPI_CR2_TXEIE;
    _dma_pending = sources & DMA_MASK;
    // Both DMA engines are prepared before SPI requests can produce any traffic.
    // RX is enabled first; missing directions retain the agreed SPI IRQ fallback.
    if (rx_dma) _dma_rx.Stream_EN(ENABLE);
    if (tx_dma) _dma_tx.Stream_EN(ENABLE);
    EnableTransfer(sources);
    return SysStatus::OK;
}

SysStatus SPI::StartPolling(const void* tx, void* rx, uint32_t len, uint32_t timeout)
{
    const SysStatus status = _status;
    if (status != SysStatus::OK) return status;
    PrepareTransfer(tx, rx, len, Mode::Polling);
    EnableTransfer(0U);
    const uint32_t tick = System::GetTick();
    while (_status == SysStatus::Busy) {
        if (!_nss_ended && !_errors) {
            uint32_t remaining = FifoFrames();
            uint32_t sr = SPIx->SR;
            const uint32_t errors = ReadErrors(sr, remaining);
            if (errors) Finish(SysStatus::Error, errors);
            else {
                if (_direction != Direction::TX) DrainRx(sr, remaining);
                if ((_direction != Direction::RX || Master_slave == Master_sel::Master)
                    && (sr & SPI_SR_TXE)) OnTxEmpty();
            }
        }
        Process();
        if (_status == SysStatus::Busy && System::GetTick() - tick >= timeout)
            Finish(SysStatus::Timeout);
    }
    return _status;
}

void SPI::StoreFrame(uint16_t frame)
{
    if (_rx_remaining) {
        if (_rx_data) {
            const uint32_t offset = _capacity - _rx_remaining;
            if (_frame_format == Data_frame_format::Byte) _rx_data[offset] = static_cast<uint8_t>(frame);
            else reinterpret_cast<uint16_t*>(_rx_data)[offset] = frame;
        }
        _rx_remaining = _rx_remaining - 1U;
        if (!_rx_remaining && Master_slave == Master_sel::Master)
            IRQ_en(IRQ::RXNE, DISABLE);
    } else if (Master_slave == Master_sel::Slave) {
        _truncated = true;
    }
}

void SPI::OnRxByte(uint8_t frame) { StoreFrame(frame); }
void SPI::OnRxWord(uint16_t frame) { StoreFrame(frame); }

void SPI::DrainRx(uint32_t& sr, uint32_t& remaining)
{
    for (; remaining && (sr & SPI_SR_RXNE);) {
        --remaining;
        const uint16_t frame = ReadFrame();
        if (_frame_format == Data_frame_format::Byte) OnRxByte(static_cast<uint8_t>(frame));
        else OnRxWord(frame);
#if !defined(STM32F4)
        if (remaining) sr = SPIx->SR; // Check the next FIFO frame, not the first again.
#endif
    }
}

void SPI::OnTxEmpty()
{
    if (_tx_remaining) {
        uint16_t frame = 0xFFFFU;
        if (_tx_data) {
            frame = _frame_format == Data_frame_format::Byte ? *_tx_data : *reinterpret_cast<const uint16_t*>(_tx_data);
            _tx_data += _frame_format == Data_frame_format::Byte ? 1U : 2U;
        }
        WriteFrame(frame);
        _tx_remaining = _tx_remaining - 1U;
    }
    else if (Master_slave == Master_sel::Slave) WriteFrame(0xFFFFU);
    if (!_tx_remaining && Master_slave == Master_sel::Master)
        IRQ_en(IRQ::TXE, DISABLE);
}

uint32_t SPI::ReadErrors(uint32_t& sr, uint32_t& remaining)
{
    uint32_t errors = sr & SPI_ERRORS;
    if ((errors & SPI_SR_OVR) && Master_slave == Master_sel::Slave
        && (!_rx_remaining || (_mode == Mode::Dma && _dma_rx.HasSelection() && !_dma_rx.GetCount()))) {
        // Capacity was already filled: lost excess frames are normal truncation.
        _truncated = true;
        for (; remaining && (sr & SPI_SR_RXNE); --remaining) {
            (void)ReadFrame();
            sr = SPIx->SR; // DR then SR clears OVR, including the final discarded frame.
        }
        errors &= ~SPI_SR_OVR;
    }
    return errors;
}

// No waits: F4/F7 stop completion is collected by the separate DMA IRQs.
// G0 has no stop TC; its NSS contract requires the last request already serviced.
void SPI::StopTransfer()
{
    SPI_CLEAR_BIT(SPIx->CR2, (IRQ_MASK | DMA_MASK));
    if (_dma_pending & SPI_CR2_TXDMAEN) _dma_tx.RequestStop();
    if (_dma_pending & SPI_CR2_RXDMAEN) _dma_rx.RequestStop();
    SPI_CLEAR_BIT(SPIx->CR1, SPI_CR1_SPE);
}

void SPI::NssReleased()
{
    if (!SPIx || Master_slave != Master_sel::Slave || _status != SysStatus::Busy || _nss_ended)
        return;
    uint32_t remaining = FifoFrames();
    uint32_t sr = SPIx->SR;
    _errors = _errors | ReadErrors(sr, remaining);
    _nss_ended = true;
    StopTransfer();
    __DMB();
}

SysStatus SPI::Process()
{
    if (_status != SysStatus::Busy) return _status;
    uint32_t remaining = FifoFrames();
    uint32_t sr = SPIx->SR;
    const uint32_t errors = ReadErrors(sr, remaining);
    if (errors) _errors = _errors | errors;
    if (_errors || _nss_ended) StopTransfer();
#if defined(STM32G0)
    // No stop TC on G0. At NSS the last request must already be serviced.
    // Error aborts discard the packet and never use a potentially invalid CNDTR.
    if (_errors || _nss_ended) {
        if (_dma_pending & SPI_CR2_TXDMAEN) {
            if (_dma_tx.IsEnabled()) return SysStatus::Busy;
            if (_dma_tx.GetErrorFlags()) _errors = _errors | static_cast<uint32_t>(Error::DmaTx);
            if (!_errors) _tx_remaining = static_cast<uint16_t>(_dma_tx.GetCount());
            _dma_tx.ClearFlags();
            _dma_pending = _dma_pending & ~SPI_CR2_TXDMAEN;
        }
        if (_dma_pending & SPI_CR2_RXDMAEN) {
            if (_dma_rx.IsEnabled()) return SysStatus::Busy;
            if (_dma_rx.GetErrorFlags()) _errors = _errors | static_cast<uint32_t>(Error::DmaRx);
            if (!_errors) _rx_remaining = static_cast<uint16_t>(_dma_rx.GetCount());
            _dma_rx.ClearFlags();
            _dma_pending = _dma_pending & ~SPI_CR2_RXDMAEN;
        }
    }
#endif
    if (_errors) {
        if (_dma_pending) return SysStatus::Busy;
        Finish(SysStatus::Error);
        return _status;
    }
    if (Master_slave == Master_sel::Slave) {
        if (!_nss_ended || _dma_pending) return SysStatus::Busy;
    } else {
        if (_tx_remaining || _rx_remaining || _dma_pending) return SysStatus::Busy;
        // DMA IRQ may have finished after the initial snapshot; sample the wire now.
        sr = SPIx->SR;
#if defined(STM32F4)
        if (!(sr & SPI_SR_TXE) || (sr & SPI_SR_BSY)) return SysStatus::Busy;
#else
        if (sr & (SPI_SR_FTLVL | SPI_SR_BSY)) return SysStatus::Busy;
#endif
    }
    Finish(SysStatus::OK);
    return _status;
}

// Cache return precedes CPU FIFO-tail stores. Slave NSS may end before its TX
// buffer: flush prefetched TX on completion as well as on error/timeout.
void SPI::Finish(SysStatus result, uint32_t errors)
{
    StopTransfer();
    errors |= _errors;
    if (_mode == Mode::Dma && _dma_rx.HasSelection() && _rx_data)
        System::CacheInvalidate(_rx_data, (result == SysStatus::OK && !errors ? _capacity - _rx_remaining : _capacity)
                * (_frame_format == Data_frame_format::Byte ? 1U : 2U));
    if (result == SysStatus::OK) {
        uint32_t remaining = FifoFrames();
        uint32_t sr = SPIx->SR;
        if (_direction != Direction::TX) {
            DrainRx(sr, remaining);
            sr = SPIx->SR; // Check for errors after the tail reads and their callbacks.
        }
        errors |= ReadErrors(sr, remaining);
    }
    if (errors && result == SysStatus::OK) result = SysStatus::Error;
    if (result != SysStatus::OK || Master_slave == Master_sel::Slave) {
        uint32_t cr1 = SPIx->CR1 & ~SPI_CR1_SPE;
        if (Master_slave == Master_sel::Master) cr1 |= SPI_CR1_MSTR;
        const uint32_t cr2 = SPIx->CR2 & ~(IRQ_MASK | DMA_MASK);
        SPI_SET_BIT(*reinterpret_cast<volatile uint32_t*>(_instance.rst_reg), _instance.rst_bit);
        SPI_CLEAR_BIT(*reinterpret_cast<volatile uint32_t*>(_instance.rst_reg), _instance.rst_bit);
        SPIx->CR1 = cr1;
        SPIx->CR2 = cr2;
    }
    SlaveSelect(DISABLE);
    const bool notify = _mode != Mode::Polling;
    __DMB();
    _status = result;
    if (result == SysStatus::Error) OnError(errors);
    else if (result == SysStatus::OK && notify) OnComplete();
}

void SPI::OnDmaTxComplete()
{
    _tx_remaining = 0;
    if (Master_slave == Master_sel::Slave) IRQ_en(IRQ::TXE, ENABLE);
}

void SPI::OnDmaRxComplete()
{
    _rx_remaining = 0;
    // Slave keeps counting/discarding extra frames until NSS, without DMA writes
    // beyond the user allocation. Master has no more planned receive frames.
    if (Master_slave == Master_sel::Slave) IRQ_en(IRQ::RXNE, ENABLE);
}

void SPI::HandleIRQ()
{
    if (!SPIx || _status != SysStatus::Busy || _nss_ended) return;
    const uint32_t cr2 = SPIx->CR2;
    uint32_t sr = SPIx->SR;
    uint32_t remaining = FifoFrames();
    uint32_t errors = 0;
    if (cr2 & SPI_CR2_ERRIE) errors = ReadErrors(sr, remaining);
    if (errors) {
        _errors = _errors | errors;
#if SPI_USE_ATOMIC
        SPI_CLEAR_BIT(SPIx->CR2, IRQ_MASK | DMA_MASK);
#else
        SPIx->CR2 = cr2 & ~(IRQ_MASK | DMA_MASK);
#endif
        return;
    }
    if ((cr2 & SPI_CR2_RXNEIE) && !(cr2 & SPI_CR2_RXDMAEN)) DrainRx(sr, remaining);
    if ((cr2 & SPI_CR2_TXEIE) && (sr & SPI_SR_TXE)) OnTxEmpty();
}

void SPI::HandleDmaTxIRQ()
{
    if (!(_dma_pending & SPI_CR2_TXDMAEN)) return;
    const uint32_t flags = _dma_tx.GetIRQFlags();
    const uint32_t errors = flags & DMA_Sx::FLAG_ERRORS;
    if (errors) {
        _errors = _errors | static_cast<uint32_t>(Error::DmaTx);
        _dma_tx.Disable_IRQ(DMA_Sx::IRQ::TE);
#if defined(STM32F4) || defined(STM32F7)
        _dma_tx.Disable_IRQ(DMA_Sx::IRQ::DME);
#endif
        StopTransfer();
    }
#if defined(STM32F4) || defined(STM32F7)
    if (!(flags & DMA_Sx::FLAG_TC)
        && !((errors & DMA_LISR_TEIF0) && !_dma_tx.IsEnabled())) return;
#else
    if (!(flags & DMA_Sx::FLAG_TC) && !errors) return;
#endif
    DMA_TX(DISABLE);
    _dma_tx.RequestStop();
    if (!_errors) _tx_remaining = static_cast<uint16_t>(_dma_tx.GetCount());
    _dma_tx.ClearFlags();
    __DMB();
    _dma_pending = _dma_pending & ~SPI_CR2_TXDMAEN;
    if (!_errors && !_nss_ended) OnDmaTxComplete();
}

void SPI::HandleDmaRxIRQ()
{
    if (!(_dma_pending & SPI_CR2_RXDMAEN)) return;
    const uint32_t flags = _dma_rx.GetIRQFlags();
    const uint32_t errors = flags & DMA_Sx::FLAG_ERRORS;
    if (errors) {
        _errors = _errors | static_cast<uint32_t>(Error::DmaRx);
        _dma_rx.Disable_IRQ(DMA_Sx::IRQ::TE);
#if defined(STM32F4) || defined(STM32F7)
        _dma_rx.Disable_IRQ(DMA_Sx::IRQ::DME);
#endif
        StopTransfer();
    }
#if defined(STM32F4) || defined(STM32F7)
    if (!(flags & DMA_Sx::FLAG_TC)
        && !((errors & DMA_LISR_TEIF0) && !_dma_rx.IsEnabled())) return;
#else
    if (!(flags & DMA_Sx::FLAG_TC) && !errors) return;
#endif
    DMA_RX(DISABLE);
    _dma_rx.RequestStop();
    if (!_errors) _rx_remaining = static_cast<uint16_t>(_dma_rx.GetCount());
    _dma_rx.ClearFlags();
    __DMB();
    _dma_pending = _dma_pending & ~SPI_CR2_RXDMAEN;
    if (!_errors && !_nss_ended) OnDmaRxComplete();
}

SysInitStatus SPI::SetUpDMA()
{
    DMA_Sx* const tx = _dma_tx.HasSelection() ? &_dma_tx : nullptr;
    DMA_Sx* const rx = _dma_rx.HasSelection() ? &_dma_rx : nullptr;
    if (!tx && !rx) return SysInitStatus::InitOK;
    DMA_Sx::StreamSettings cfg;
    cfg.data_size = _frame_format == Data_frame_format::Byte ? DMA_Sx::SIZE::Byte : DMA_Sx::SIZE::Half_Word;
    cfg.minc = true;
    cfg.per_address = reinterpret_cast<uint32_t>(&SPIx->DR);
    if (tx) {
        cfg.direction = DMA_Sx::DIR::To_Per;
        if (tx->SetUp(cfg) != SysInitStatus::InitOK) return SysInitStatus::InitError;
    }
    if (rx) {
        cfg.direction = DMA_Sx::DIR::From_Per;
        if (rx->SetUp(cfg) != SysInitStatus::InitOK) {
            if (tx) tx->Release();
            return SysInitStatus::InitError;
        }
    }
    const IRQn_Type no_irq = static_cast<IRQn_Type>(-1);
    const IRQn_Type irqs[2] = {tx ? tx->GetIRQn() : no_irq, rx ? rx->GetIRQn() : no_irq};
    IIRQHandler* const handlers[2] = {&_dma_tx_irq, &_dma_rx_irq};
    bool enabled[2]{};
    for (unsigned i = 0; i < 2; ++i) {
        if (irqs[i] == no_irq) continue;
        enabled[i] = NVIC_GetEnableIRQ(irqs[i]) != 0;
        NVIC_DisableIRQ(irqs[i]);
        if (IRQ_Registry::Register(irqs[i], handlers[i]) != SysInitStatus::InitOK) {
            if (enabled[i]) NVIC_EnableIRQ(irqs[i]);
            for (unsigned j = 0; j < i; ++j) {
                if (irqs[j] == no_irq) continue;
                IRQ_Registry::Unregister(irqs[j], handlers[j]);
                if (enabled[j]) NVIC_EnableIRQ(irqs[j]);
            }
            if (tx) tx->Release();
            if (rx) rx->Release();
            return SysInitStatus::InitError;
        }
    }
    DMA_Sx* channels[2] = {tx, rx};
    for (DMA_Sx* dma : channels) {
        if (!dma) continue;
        dma->Enable_IRQ(DMA_Sx::IRQ::TC);
        dma->Enable_IRQ(DMA_Sx::IRQ::TE);
#if defined(STM32F4) || defined(STM32F7)
        dma->Enable_IRQ(DMA_Sx::IRQ::DME);
#endif
    }
    return SysInitStatus::InitOK;
}

void SPI::DeInit()
{
    if (!SPIx) return;
    if (_status == SysStatus::Busy) {
        _nss_ended = true; // A late DMA completion must not re-enable SPI sources.
        StopTransfer();
        // DeInit is main-only: Release/Stream_EN may wait here, never in IRQ/Process.
        if (_dma_tx.HasSelection()) _dma_tx.Stream_EN(DISABLE);
        if (_dma_rx.HasSelection()) _dma_rx.Stream_EN(DISABLE);
#if defined(STM32F4) || defined(STM32F7)
        // EN=0 alone does not confirm a FIFO flush. An IRQ may already have
        // collected TC (pending bit cleared); TE ends the stream on hardware error.
        while ((_dma_pending & SPI_CR2_TXDMAEN)
            && !(_dma_tx.GetIRQFlags() & (DMA_Sx::FLAG_TC | DMA_LISR_TEIF0))) {}
        while ((_dma_pending & SPI_CR2_RXDMAEN)
            && !(_dma_rx.GetIRQFlags() & (DMA_Sx::FLAG_TC | DMA_LISR_TEIF0))) {}
#endif
        if (_mode == Mode::Dma && _dma_rx.HasSelection() && _rx_data)
            System::CacheInvalidate(_rx_data, _capacity
                * (_frame_format == Data_frame_format::Byte ? 1U : 2U));
    }
    SPI_CLEAR_BIT(SPIx->CR2, (IRQ_MASK | DMA_MASK));
    SPI_CLEAR_BIT(SPIx->CR1, SPI_CR1_SPE);
    SlaveSelect(DISABLE);
    const IRQn_Type no_irq = static_cast<IRQn_Type>(-1);
    const IRQn_Type irqs[3] = {_instance.irq, _dma_tx.HasSelection() ? _dma_tx.GetIRQn() : no_irq,
                            _dma_rx.HasSelection() ? _dma_rx.GetIRQn() : no_irq};
    IIRQHandler* const handlers[3] = {this, &_dma_tx_irq, &_dma_rx_irq};
    for (unsigned i = 0; i < 3; ++i) {
        if (irqs[i] == no_irq) continue;
        const bool enabled = NVIC_GetEnableIRQ(irqs[i]) != 0;
        NVIC_DisableIRQ(irqs[i]);
        IRQ_Registry::Unregister(irqs[i], handlers[i]);
        if (IRQ_Registry::HasHandlers(irqs[i])) {
            if (enabled) NVIC_EnableIRQ(irqs[i]);
        } else NVIC_ClearPendingIRQ(irqs[i]);
    }
    if (_dma_tx.HasSelection()) _dma_tx.Release();
    if (_dma_rx.HasSelection()) _dma_rx.Release();
    _dma_pending = 0;
    SPI_SET_BIT(*reinterpret_cast<volatile uint32_t*>(_instance.rst_reg), _instance.rst_bit);
    SPI_CLEAR_BIT(*reinterpret_cast<volatile uint32_t*>(_instance.rst_reg), _instance.rst_bit);
    SPIx = nullptr;
    _tx_data = nullptr;
    _rx_data = nullptr;
    _capacity = 0;
    _tx_remaining = 0;
    _rx_remaining = 0;
    _errors = 0;
    _truncated = false;
    _nss_ended = false;
    _status = SysStatus::NotInit;
}
