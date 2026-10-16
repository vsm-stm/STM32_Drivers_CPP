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

#define SPI_DEFS_CPP
#include "spi_defs.hpp"
#undef SPI_DEFS_CPP

uint16_t SPI::ReadFrame() const
{
    if (_frame_format == Data_frame_format::Byte) return *reinterpret_cast<volatile uint8_t*>(&SPIx->DR);
    return *reinterpret_cast<volatile uint16_t*>(&SPIx->DR);
}

void SPI::WriteFrame(uint16_t frame)
{
    if (_frame_format == Data_frame_format::Byte) *reinterpret_cast<volatile uint8_t*>(&SPIx->DR) = static_cast<uint8_t>(frame);
    else *reinterpret_cast<volatile uint16_t*>(&SPIx->DR) = frame;
}

SysInitStatus SPI::SetUp(Master_sel mstr, NSS_ctrl nss, Data_frame_format dff,
                        Frame_Format ff, cPolPha cpolpha, BaudRate br)
{
    if (_info || !_clk.IsValid()
        || (mstr != Master_sel::Master && mstr != Master_sel::Slave)
        || (nss != NSS_ctrl::Hard && nss != NSS_ctrl::Software)
        || (dff != Data_frame_format::Byte && dff != Data_frame_format::Half_Word)
        || (ff != Frame_Format::MSB && ff != Frame_Format::LSB)
        || (static_cast<uint32_t>(cpolpha) & ~(SPI_CR1_CPOL | SPI_CR1_CPHA))
        || static_cast<uint32_t>(br) > static_cast<uint32_t>(BaudRate::DIV256)
        || (nss == NSS_ctrl::Hard && !_ss.IsValid())
        || (mstr == Master_sel::Slave && nss != NSS_ctrl::Hard))
        return SysInitStatus::InitError;
    const PeriphInfo* info = nullptr;
    for (const auto& entry : spi_table)
        if (entry.periph == SPIx) { info = &entry; break; }
    if (!info) return SysInitStatus::InitError;

    *info->clk_reg |= info->clk_bit;
    (void)*info->clk_reg;
    *info->rst_reg |= info->rst_bit;
    *info->rst_reg &= ~info->rst_bit;

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
#if defined(STM32F4)
    cr1 |= static_cast<uint32_t>(dff);
    SPIx->CR2 = nss == NSS_ctrl::Hard && mstr == Master_sel::Master ? SPI_CR2_SSOE : 0U;
#else
    SPIx->CR2 = static_cast<uint32_t>(dff)
        | (dff == Data_frame_format::Byte ? SPI_CR2_FRXTH : 0U)
        | (nss == NSS_ctrl::Hard && mstr == Master_sel::Master ? SPI_CR2_SSOE : 0U);
#endif
    SPIx->CR1 = cr1;

    const bool enabled = NVIC_GetEnableIRQ(info->irq) != 0;
    NVIC_DisableIRQ(info->irq);
    if (IRQ_Registry::Register(info->irq, this) != SysInitStatus::InitOK) {
        if (enabled) NVIC_EnableIRQ(info->irq);
        return SysInitStatus::InitError;
    }
    _info = info;
    Master_slave = mstr;
    nss_ctrl = nss;
    _frame_format = dff;
    _status = SysStatus::OK;
    NVIC_EnableIRQ(info->irq);
    return SysInitStatus::InitOK;
}

// Shared setup, with no SPI traffic until descriptors, cache and DMA are ready.
SysStatus SPI::Start(const void* tx, void* rx, uint32_t len, Data_frame_format frame_format,
                     Mode mode, FunctionalState minc)
{
    if (!_info) return SysStatus::NotInit;
    if ((!tx && !rx) || !len || len > 65535U || frame_format != _frame_format
        || (frame_format == Data_frame_format::Half_Word
            && ((reinterpret_cast<uintptr_t>(tx) | reinterpret_cast<uintptr_t>(rx)) & 1U))
        || (minc != ENABLE && minc != DISABLE))
        return SysStatus::Error;
    if (_status == SysStatus::Busy) return SysStatus::Busy;
    if ((tx && !(Master_slave == Master_sel::Master ? _mosi.IsValid() : _miso.IsValid()))
        || (rx && !(Master_slave == Master_sel::Master ? _miso.IsValid() : _mosi.IsValid())))
        return SysStatus::Error;
    if (mode == Mode::Dma && ((tx && !_dma_tx) || (rx && !_dma_rx)))
        return SysStatus::Error;

    SPIx->CR2 &= ~(IRQ_MASK | DMA_MASK);
    SPIx->CR1 &= ~SPI_CR1_SPE;
    while (SPIx->SR & SPI_SR_RXNE) (void)ReadFrame();
    (void)SPIx->SR; // DR then SR clears old OVR.

    if (mode == Mode::Dma) {
        const uint32_t frame_bytes = frame_format == Data_frame_format::Byte ? 1U : 2U;
        if (_dma_tx) {
            _dma_tx->MINC(tx ? minc : DISABLE);
            if (_dma_tx->SetMemAddr(reinterpret_cast<uint32_t>(tx ? tx : &spi_dummy_word)) != SysStatus::OK
                || _dma_tx->SetCount(len) != SysStatus::OK) return SysStatus::Error;
            _dma_tx->ClearFlags();
        }
        if (_dma_rx) {
            _dma_rx->MINC(rx ? ENABLE : DISABLE);
            if (_dma_rx->SetMemAddr(reinterpret_cast<uint32_t>(rx ? rx : &spi_discard_word)) != SysStatus::OK
                || _dma_rx->SetCount(len) != SysStatus::OK) return SysStatus::Error;
            _dma_rx->ClearFlags();
        }
        if (tx) System::CacheClean(tx, minc == ENABLE ? len * frame_bytes : frame_bytes);
        if (rx) System::CacheCleanInvalidate(rx, len * frame_bytes);
        // spi_dummy_word is immutable Flash. spi_discard_word is never read by CPU:
        // neither needs CPU/DMA cache ownership handover.
    }
    _tx_data = static_cast<const uint8_t*>(tx);
    _rx_data = static_cast<uint8_t*>(rx);
    _capacity = static_cast<uint16_t>(len);
    _tx_remaining = _capacity;
    _rx_remaining = _capacity;
    _errors = 0;
    _truncated = false;
    _nss_ended = false;
    _mode = mode;
    _status = SysStatus::Busy;

    // RX is armed before any master-generated clock. Directions without DMA
    // use SPI IRQ; Send still drains RX and Receive still supplies dummy TX.
    if (mode == Mode::Dma) {
        if (_dma_rx) { _dma_rx->Stream_EN(ENABLE); DMA_RX(ENABLE); }
        else IRQ_en(IRQ::RXNE, ENABLE);
        if (_dma_tx) { _dma_tx->Stream_EN(ENABLE); DMA_TX(ENABLE); }
        else IRQ_en(IRQ::TXE, ENABLE);
    } else if (mode == Mode::Irq) {
        IRQ_en(IRQ::RXNE, ENABLE);
        IRQ_en(IRQ::TXE, ENABLE);
    }
    if (mode != Mode::Polling) IRQ_en(IRQ::ERR, ENABLE);
    SlaveSelect(ENABLE);
    SPIx->CR1 |= SPI_CR1_SPE;
    return SysStatus::OK;
}

SysStatus SPI::Poll(const void* tx, void* rx, uint32_t len,
                    Data_frame_format frame_format, uint32_t timeout)
{
    const SysStatus started = Start(tx, rx, len, frame_format, Mode::Polling);
    if (started != SysStatus::OK) return started;
    const uint32_t tick = System::GetTick();
    while (_status == SysStatus::Busy) {
        if (!_nss_ended && !_errors) {
            const uint32_t errors = ReadErrors();
            if (errors) Finish(SysStatus::Error, errors);
            else {
                DrainRx();
                if (SPIx->SR & SPI_SR_TXE) OnTxEmpty();
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

void SPI::DrainRx()
{
    while (SPIx->SR & SPI_SR_RXNE) {
        const uint16_t frame = ReadFrame();
        if (_frame_format == Data_frame_format::Byte) OnRxByte(static_cast<uint8_t>(frame));
        else OnRxWord(frame);
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

uint32_t SPI::ReadErrors()
{
    uint32_t errors = SPIx->SR & SPI_ERRORS;
    if ((errors & SPI_SR_OVR) && Master_slave == Master_sel::Slave
        && (!_rx_remaining || (_mode == Mode::Dma && _dma_rx && !_dma_rx->GetCount()))) {
        // Capacity was already filled: lost excess frames are normal truncation.
        _truncated = true;
        while (SPIx->SR & SPI_SR_RXNE) (void)ReadFrame();
        (void)SPIx->SR;
        errors &= ~SPI_SR_OVR;
    }
    if (_mode == Mode::Dma) {
        if (_dma_tx && _dma_tx->GetErrorFlags()) errors |= static_cast<uint32_t>(Error::DmaTx);
        if (_dma_rx && _dma_rx->GetErrorFlags()) errors |= static_cast<uint32_t>(Error::DmaRx);
    }
    return errors;
}

// Stop sources first, then DMA; pending shared IRQ sees disabled request/source
// bits and cannot touch a buffer while main later returns its cache ownership.
uint32_t SPI::StopTransfer()
{
    uint32_t errors = 0;
    SPIx->CR2 &= ~(IRQ_MASK | DMA_MASK);
    if (_mode == Mode::Dma) {
        if (_dma_tx) {
            _dma_tx->Stream_EN(DISABLE);
            if (_dma_tx->GetErrorFlags()) errors |= static_cast<uint32_t>(Error::DmaTx);
            _tx_remaining = static_cast<uint16_t>(_dma_tx->GetCount());
            _dma_tx->ClearFlags();
        }
        if (_dma_rx) {
            _dma_rx->Stream_EN(DISABLE);
            if (_dma_rx->GetErrorFlags()) errors |= static_cast<uint32_t>(Error::DmaRx);
            _rx_remaining = static_cast<uint16_t>(_dma_rx->GetCount());
            _dma_rx->ClearFlags();
        }
    }
    SPIx->CR1 &= ~SPI_CR1_SPE;
    return errors;
}

void SPI::NssReleased()
{
    if (!_info || Master_slave != Master_sel::Slave || _status != SysStatus::Busy || _nss_ended)
        return;
    const uint32_t errors = StopTransfer();
    _errors = _errors | errors | ReadErrors();
    __DMB();
    _nss_ended = true;
}

SysStatus SPI::Process()
{
    if (_status != SysStatus::Busy) return _status;
    const uint32_t errors = ReadErrors();
    if (_errors || errors) {
        Finish(SysStatus::Error, errors);
        return _status;
    }
    if (Master_slave == Master_sel::Slave) {
        if (!_nss_ended) return SysStatus::Busy;
    } else {
        if (_tx_remaining || _rx_remaining) return SysStatus::Busy;
        const uint32_t sr = SPIx->SR;
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
    errors |= StopTransfer();
    errors |= _errors;
    if (_mode == Mode::Dma && _dma_rx && _rx_data)
        System::CacheInvalidate(_rx_data, (_capacity - _rx_remaining)
                * (_frame_format == Data_frame_format::Byte ? 1U : 2U));
    if (result == SysStatus::OK) {
        DrainRx();
        errors |= ReadErrors();
    }
    if (errors && result == SysStatus::OK) result = SysStatus::Error;
    if (result != SysStatus::OK || Master_slave == Master_sel::Slave) {
        uint32_t cr1 = SPIx->CR1 & ~SPI_CR1_SPE;
        if (Master_slave == Master_sel::Master) cr1 |= SPI_CR1_MSTR;
        const uint32_t cr2 = SPIx->CR2 & ~(IRQ_MASK | DMA_MASK);
        *_info->rst_reg |= _info->rst_bit;
        *_info->rst_reg &= ~_info->rst_bit;
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
    if (!_info || _status != SysStatus::Busy || _nss_ended) return;
    const uint32_t cr2 = SPIx->CR2;
    uint32_t errors = 0;
    if (cr2 & SPI_CR2_ERRIE) errors = ReadErrors();
    if (errors) {
        _errors = _errors | errors | StopTransfer();
        return;
    }
    if (_mode == Mode::Dma) {
        if (_dma_rx && (cr2 & SPI_CR2_RXDMAEN) && _dma_rx->GetTC_Flag()) {
            DMA_RX(DISABLE);
            _dma_rx->Stream_EN(DISABLE);
            if (_dma_rx->GetErrorFlags()) {
                _errors = _errors | static_cast<uint32_t>(Error::DmaRx) | StopTransfer();
                return;
            }
            _dma_rx->ClearFlags();
            OnDmaRxComplete();
        }
        if (_dma_tx && (cr2 & SPI_CR2_TXDMAEN) && _dma_tx->GetTC_Flag()) {
            DMA_TX(DISABLE);
            _dma_tx->Stream_EN(DISABLE);
            if (_dma_tx->GetErrorFlags()) {
                _errors = _errors | static_cast<uint32_t>(Error::DmaTx) | StopTransfer();
                return;
            }
            _dma_tx->ClearFlags();
            OnDmaTxComplete();
        }
    }
    // Refresh source bits after DMA TC switched a direction to IRQ handling.
    if ((SPIx->CR2 & SPI_CR2_RXNEIE) && !(SPIx->CR2 & SPI_CR2_RXDMAEN)) DrainRx();
    if ((SPIx->CR2 & SPI_CR2_TXEIE) && (SPIx->SR & SPI_SR_TXE)) OnTxEmpty();
}

SysInitStatus SPI::AttachDMA(DMA_Sx* tx, DMA_Sx* rx)
{
    if (!_info || (!tx && !rx) || (tx && tx == rx) || _dma_tx || _dma_rx || _status == SysStatus::Busy)
        return SysInitStatus::InitError;
    DMA_Sx::DMAReq tx_req{}, rx_req{};
    for (const auto& entry : spi_dma_req_table) {
        if (entry.periph != SPIx) continue;
        for (unsigned i = 0; i < SPI_DMA_ROUTES; ++i) {
            if (tx && tx->CanUseRequest(entry.tx[i])) tx_req = entry.tx[i];
            if (rx && rx->CanUseRequest(entry.rx[i])) rx_req = entry.rx[i];
        }
        break;
    }
    if ((tx && !tx_req.IsValid()) || (rx && !rx_req.IsValid())) return SysInitStatus::InitError;
    DMA_Sx::StreamSettings cfg;
    cfg.data_size = _frame_format == Data_frame_format::Byte ? DMA_Sx::SIZE::Byte : DMA_Sx::SIZE::Half_Word;
    cfg.minc = true;
    cfg.per_address = reinterpret_cast<uint32_t>(&SPIx->DR);
    if (tx) {
        cfg.channel = tx_req.ch;
        cfg.direction = DMA_Sx::DIR::To_Per;
        if (tx->SetUp(cfg) != SysInitStatus::InitOK) return SysInitStatus::InitError;
    }
    if (rx) {
        cfg.channel = rx_req.ch;
        cfg.direction = DMA_Sx::DIR::From_Per;
        if (rx->SetUp(cfg) != SysInitStatus::InitOK) {
            if (tx) tx->Release();
            return SysInitStatus::InitError;
        }
    }
    const IRQn_Type no_irq = static_cast<IRQn_Type>(-1);
    const IRQn_Type irqs[2] = {tx ? tx->GetIRQn() : no_irq, rx ? rx->GetIRQn() : no_irq};
    bool enabled[2]{};
    for (unsigned i = 0; i < 2; ++i) {
        if (irqs[i] == no_irq || (i && irqs[i] == irqs[0]) || irqs[i] == _info->irq) continue;
        enabled[i] = NVIC_GetEnableIRQ(irqs[i]) != 0;
        NVIC_DisableIRQ(irqs[i]);
        if (IRQ_Registry::Register(irqs[i], this) != SysInitStatus::InitOK) {
            if (enabled[i]) NVIC_EnableIRQ(irqs[i]);
            for (unsigned j = 0; j < i; ++j) {
                if (irqs[j] == no_irq || irqs[j] == _info->irq) continue;
                IRQ_Registry::Unregister(irqs[j], this);
                if (enabled[j]) NVIC_EnableIRQ(irqs[j]);
            }
            if (tx) tx->Release();
            if (rx) rx->Release();
            return SysInitStatus::InitError;
        }
    }
    _dma_tx = tx;
    _dma_rx = rx;
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
    if (!_info) return;
    if (_status == SysStatus::Busy) {
        StopTransfer();
        if (_mode == Mode::Dma && _dma_rx && _rx_data)
            System::CacheInvalidate(_rx_data, (_capacity - _rx_remaining)
                * (_frame_format == Data_frame_format::Byte ? 1U : 2U));
    }
    SPIx->CR2 &= ~(IRQ_MASK | DMA_MASK);
    SPIx->CR1 &= ~SPI_CR1_SPE;
    SlaveSelect(DISABLE);
    const IRQn_Type no_irq = static_cast<IRQn_Type>(-1);
    const IRQn_Type irqs[3] = {_info->irq, _dma_tx ? _dma_tx->GetIRQn() : no_irq,
                            _dma_rx ? _dma_rx->GetIRQn() : no_irq};
    for (unsigned i = 0; i < 3; ++i) {
        if (irqs[i] == no_irq || (i && irqs[i] == irqs[0]) || (i == 2 && irqs[i] == irqs[1])) continue;
        const bool enabled = NVIC_GetEnableIRQ(irqs[i]) != 0;
        NVIC_DisableIRQ(irqs[i]);
        IRQ_Registry::Unregister(irqs[i], this);
        if (IRQ_Registry::HasHandlers(irqs[i])) {
            if (enabled) NVIC_EnableIRQ(irqs[i]);
        } else NVIC_ClearPendingIRQ(irqs[i]);
    }
    if (_dma_tx) _dma_tx->Release();
    if (_dma_rx) _dma_rx->Release();
    _dma_tx = nullptr;
    _dma_rx = nullptr;
    *_info->rst_reg |= _info->rst_bit;
    *_info->rst_reg &= ~_info->rst_bit;
    _info = nullptr;
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
