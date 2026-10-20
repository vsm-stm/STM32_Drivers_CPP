#include "i2c.hpp"

#define I2C_DEFS_CPP
#include "i2c_defs.hpp"
#undef I2C_DEFS_CPP

#if defined(STM32G0) || defined(STM32F7)
static constexpr uint32_t CR1_IE_ALL = I2C_CR1_TXIE | I2C_CR1_RXIE | I2C_CR1_TCIE
    | I2C_CR1_STOPIE | I2C_CR1_NACKIE | I2C_CR1_ERRIE;
static constexpr uint32_t CR1_DMAEN = I2C_CR1_TXDMAEN | I2C_CR1_RXDMAEN;
static constexpr uint32_t NBYTES_MAX = 255;
#else
static constexpr uint32_t CR2_CTRL = I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN | I2C_CR2_ITERREN
    | I2C_CR2_DMAEN | I2C_CR2_LAST;
static constexpr uint32_t SR1_ERR = I2C_SR1_BERR | I2C_SR1_ARLO | I2C_SR1_AF;
#endif

#if defined(STM32G0)
uint32_t I2C::DmaRequest(I2C_TypeDef* i2c, bool tx)
{
    switch (reinterpret_cast<uintptr_t>(i2c)) {
#ifdef I2C1_BASE
    case I2C1_BASE: return (tx ? DMA_Sx::Req::I2c1::TX : DMA_Sx::Req::I2c1::RX).ch;
#endif
#ifdef I2C2_BASE
    case I2C2_BASE: return (tx ? DMA_Sx::Req::I2c2::TX : DMA_Sx::Req::I2c2::RX).ch;
#endif
#ifdef I2C3_BASE
    case I2C3_BASE: return (tx ? DMA_Sx::Req::I2c3::TX : DMA_Sx::Req::I2c3::RX).ch;
#endif
    default: return 0;
    }
}
#endif

// Initialization/ownership changes are main-only. The application serializes
// starts/Abort/DeInit with this object's I2C and DMA IRQs; its handlers do not nest.
SysInitStatus I2C::Apply(uint32_t timing, SPEED speed)
{
    if (_info) return SysInitStatus::InitError;
    const PeriphInfo* info = nullptr;
    for (const auto& e : i2c_table)
        if (e.periph == I2Cx) { info = &e; break; }
    if (!info) return SysInitStatus::InitError;

    _timing = timing;
#if defined(STM32F4)
    (void)speed; // F4 register configuration is entirely encoded in timing.
#endif

    I2C_SET_BIT(*info->clk_reg, info->clk_bit);
    (void)*info->clk_reg;
    I2C_SET_BIT(*info->rst_reg, info->rst_bit);
    I2C_CLEAR_BIT(*info->rst_reg, info->rst_bit);
    if (info->ksel_mask) I2C_CLEAR_BIT(*info->ksel_reg, info->ksel_mask);
    _scl.SetUp(PIN::TYPE::AF_OD_PulUp, PIN::OUTPUT_SPEED::High);
    _sda.SetUp(PIN::TYPE::AF_OD_PulUp, PIN::OUTPUT_SPEED::High);
#if defined(STM32G0) || defined(STM32F7)
#if defined(STM32G0)
    I2C_SET_BIT(RCC->APBENR2, RCC_APBENR2_SYSCFGEN);
    (void)RCC->APBENR2;
    auto& fmp = SYSCFG->CFGR1;
#else
    I2C_SET_BIT(RCC->APB2ENR, RCC_APB2ENR_SYSCFGEN);
    (void)RCC->APB2ENR;
    auto& fmp = SYSCFG->PMC;
#endif
    if (speed == SPEED::FAST_PLUS) I2C_SET_BIT(fmp, info->fmp_bit);
    else I2C_CLEAR_BIT(fmp, info->fmp_bit);
    I2Cx->CR1 = 0;
    I2Cx->TIMINGR = _timing;
    I2Cx->CR1 = I2C_CR1_PE;
#else
    ResetPeriph();
#endif

    _info = info;
    if (SetUpDMA() != SysInitStatus::InitOK) {
        I2Cx->CR1 = 0;
        _info = nullptr;
        return SysInitStatus::InitError;
    }

    const IRQn_Type none = static_cast<IRQn_Type>(-1);
    const IRQn_Type irqs[4] = {
        info->irq_ev, info->irq_er != info->irq_ev ? info->irq_er : none,
        _dma_tx.HasSelection() ? _dma_tx.GetIRQn() : none,
        _dma_rx.HasSelection() ? _dma_rx.GetIRQn() : none
    };
    IIRQHandler* const handlers[4] = { this, this, &_dma_tx_irq, &_dma_rx_irq };
    for (unsigned i = 0; i < 4; ++i) {
        if (irqs[i] == none) continue;
        const bool enabled = NVIC_GetEnableIRQ(irqs[i]) != 0;
        NVIC_DisableIRQ(irqs[i]);
        if (IRQ_Registry::Register(irqs[i], handlers[i]) != SysInitStatus::InitOK) {
            if (enabled) NVIC_EnableIRQ(irqs[i]);
            // Remove only entries acquired by this attempt; preserve shared users.
            for (unsigned j = 0; j < i; ++j) {
                if (irqs[j] == none) continue;
                const bool was_enabled = NVIC_GetEnableIRQ(irqs[j]) != 0;
                NVIC_DisableIRQ(irqs[j]);
                IRQ_Registry::Unregister(irqs[j], handlers[j]);
                if (was_enabled && IRQ_Registry::HasHandlers(irqs[j])) NVIC_EnableIRQ(irqs[j]);
                else if (!IRQ_Registry::HasHandlers(irqs[j])) NVIC_ClearPendingIRQ(irqs[j]);
            }
            if (_dma_tx.HasSelection()) _dma_tx.Release();
            if (_dma_rx.HasSelection()) _dma_rx.Release();
            I2Cx->CR1 = 0;
            _info = nullptr;
            return SysInitStatus::InitError;
        }
        NVIC_EnableIRQ(irqs[i]); // Sources remain off until a transfer starts.
    }
    _err = ERR::None;
    _state = State::Idle;
    return SysInitStatus::InitOK;
}

SysInitStatus I2C::SetUpDMA()
{
    DMA_Sx* const streams[2] = { &_dma_tx, &_dma_rx };
    for (unsigned i = 0; i < 2; ++i) {
        if (!streams[i]->HasSelection()) continue;
        DMA_Sx::StreamSettings cfg;
        cfg.direction = i ? DMA_Sx::DIR::From_Per : DMA_Sx::DIR::To_Per;
        cfg.data_size = DMA_Sx::SIZE::Byte;
        cfg.minc = true;
#if defined(STM32F4)
        cfg.per_address = reinterpret_cast<uint32_t>(&I2Cx->DR);
#else
        cfg.per_address = i ? reinterpret_cast<uint32_t>(&I2Cx->RXDR)
                            : reinterpret_cast<uint32_t>(&I2Cx->TXDR);
#endif
        if (streams[i]->SetUp(cfg) != SysInitStatus::InitOK) {
            if (i && _dma_tx.HasSelection()) _dma_tx.Release();
            return SysInitStatus::InitError;
        }
    }
    return SysInitStatus::InitOK;
}

// All public transfer variants use the same FSM.
SysStatus I2C::Send(uint8_t addr, const uint8_t* data, uint16_t len, uint32_t timeout)
{
	if (!len) return SysStatus::Error;
	return Run(addr, 0, 0, const_cast<uint8_t*>(data), len, false, timeout);
}

SysStatus I2C::Receive(uint8_t addr, uint8_t* data, uint16_t len, uint32_t timeout)
{
	return Run(addr, 0, 0, data, len, true, timeout);
}

SysStatus I2C::WriteReg(uint8_t addr, uint16_t reg, const uint8_t* data, uint16_t len,
						REG reg_size, uint32_t timeout)
{
	if (reg_size != REG::_8BIT && reg_size != REG::_16BIT) return SysStatus::Error;
	return Run(addr, reg, static_cast<uint8_t>(reg_size), const_cast<uint8_t*>(data), len, false, timeout);
}

SysStatus I2C::ReadReg(uint8_t addr, uint16_t reg, uint8_t* data, uint16_t len,
					   REG reg_size, uint32_t timeout)
{
	if (reg_size != REG::_8BIT && reg_size != REG::_16BIT) return SysStatus::Error;
	return Run(addr, reg, static_cast<uint8_t>(reg_size), data, len, true, timeout);
}

SysStatus I2C::IsReady(uint8_t addr, uint32_t timeout)
{
	return Run(addr, 0, 0, nullptr, 0, false, timeout);
}

SysStatus I2C::Send_IRQ(uint8_t addr, const uint8_t* data, uint16_t len)
{
	if (!len) return SysStatus::Error;
	return Begin(Mode::Irq, addr, 0, 0, const_cast<uint8_t*>(data), len, false);
}

SysStatus I2C::Receive_IRQ(uint8_t addr, uint8_t* data, uint16_t len)
{
	return Begin(Mode::Irq, addr, 0, 0, data, len, true);
}

SysStatus I2C::WriteReg_IRQ(uint8_t addr, uint16_t reg, const uint8_t* data, uint16_t len, REG reg_size)
{
	if (reg_size != REG::_8BIT && reg_size != REG::_16BIT) return SysStatus::Error;
	return Begin(Mode::Irq, addr, reg, static_cast<uint8_t>(reg_size), const_cast<uint8_t*>(data), len, false);
}

SysStatus I2C::ReadReg_IRQ(uint8_t addr, uint16_t reg, uint8_t* data, uint16_t len, REG reg_size)
{
	if (reg_size != REG::_8BIT && reg_size != REG::_16BIT) return SysStatus::Error;
	return Begin(Mode::Irq, addr, reg, static_cast<uint8_t>(reg_size), data, len, true);
}

SysStatus I2C::Send_DMA(uint8_t addr, const uint8_t* data, uint16_t len)
{
	if (!len) return SysStatus::Error;
	return Begin(Mode::Dma, addr, 0, 0, const_cast<uint8_t*>(data), len, false);
}

SysStatus I2C::Receive_DMA(uint8_t addr, uint8_t* data, uint16_t len)
{
	return Begin(Mode::Dma, addr, 0, 0, data, len, true);
}

SysStatus I2C::WriteReg_DMA(uint8_t addr, uint16_t reg, const uint8_t* data, uint16_t len, REG reg_size)
{
	if (reg_size != REG::_8BIT && reg_size != REG::_16BIT) return SysStatus::Error;
	return Begin(Mode::Dma, addr, reg, static_cast<uint8_t>(reg_size), const_cast<uint8_t*>(data), len, false);
}

SysStatus I2C::ReadReg_DMA(uint8_t addr, uint16_t reg, uint8_t* data, uint16_t len, REG reg_size)
{
	if (reg_size != REG::_8BIT && reg_size != REG::_16BIT) return SysStatus::Error;
	return Begin(Mode::Dma, addr, reg, static_cast<uint8_t>(reg_size), data, len, true);
}

SysStatus I2C::Begin(Mode mode, uint8_t addr, uint16_t reg, uint8_t reg_len,
					 uint8_t* buf, uint16_t len, bool read)
{
	if (!_info) return SysStatus::NotInit;
	if (addr > 0x7F || reg_len > 2 || (len && !buf) || (read && !len)) return SysStatus::Error;
	if (_state != State::Idle) return SysStatus::Busy;

	DMA_Sx& dma = read ? _dma_rx : _dma_tx;
	if (mode == Mode::Dma && (!dma.HasSelection() || !len)) return SysStatus::Error;

	// Bus held by another master/slave (or, F4, our STOP still pending)
#if defined(STM32G0) || defined(STM32F7)
	const bool bus_busy = (I2Cx->ISR & I2C_ISR_BUSY) != 0u;
#elif defined(STM32F4)
	const bool bus_busy = (I2Cx->SR2 & I2C_SR2_BUSY) || (I2Cx->CR1 & I2C_CR1_STOP);
#endif
	if (bus_busy) return SysStatus::Busy;

	_addr    = addr;
	_reg_len = reg_len;
	_reg_idx = 0;
	_reg[0]  = (reg_len == 2) ? static_cast<uint8_t>(reg >> 8) : static_cast<uint8_t>(reg);
	_reg[1]  = static_cast<uint8_t>(reg);
	_buf     = buf;
	_len     = len;
	_idx     = 0;
	_read    = read;
	_mode    = mode;
	_err     = ERR::None;

	if (mode == Mode::Dma) {
		// F7 D-cache: the DMA must see what the CPU wrote, and no dirty line
		// may be written over what the DMA will write (AN4839)
		if (read) System::CacheCleanInvalidate(buf, len);
		else      System::CacheClean(buf, len);
		if (dma.SetMemAddr(reinterpret_cast<uint32_t>(buf)) != SysStatus::OK
			|| dma.SetCount(len) != SysStatus::OK) return SysStatus::Error;
		dma.ClearFlags();
		dma.Enable_IRQ(DMA_Sx::IRQ::TE, false);
		dma.Enable_IRQ(DMA_Sx::IRQ::TC, false);
#if defined(STM32F4) || defined(STM32F7)
		dma.Enable_IRQ(DMA_Sx::IRQ::DME, false);
#endif
		dma.Stream_EN(ENABLE);
	}

#if defined(STM32G0) || defined(STM32F7)
	// START, address and byte count are one CR2 write: the first state is
	// the first byte to move. Register bytes always go through TXIS (CPU);
	// data through TXIS/RXNE (Poll, Irq) or the DMA request (Dma).
	const State first = reg_len ? State::TxReg  :
	                    !len    ? State::Stop   :
	                    read    ? State::RxData : State::TxData;

	I2Cx->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF | I2C_ICR_BERRCF | I2C_ICR_ARLOCF;
	uint32_t cr1 = 0;
	if (mode != Mode::Poll)
		cr1 |= I2C_CR1_ERRIE | I2C_CR1_NACKIE | I2C_CR1_STOPIE | I2C_CR1_TCIE;
	if (mode == Mode::Irq)
		cr1 |= (first == State::RxData) ? I2C_CR1_RXIE : I2C_CR1_TXIE;
	if (mode == Mode::Dma)
		cr1 |= reg_len ? I2C_CR1_TXIE : (read ? I2C_CR1_RXDMAEN : I2C_CR1_TXDMAEN);
	I2C_MODIFY_REG(I2Cx->CR1, CR1_IE_ALL | CR1_DMAEN, cr1);

	const uint32_t sadd = static_cast<uint32_t>(addr) << 1;
	_state = first;
	if (read && reg_len) {
		// Register write without AUTOEND: TC -> Restart
		I2Cx->CR2 = sadd | (static_cast<uint32_t>(reg_len) << I2C_CR2_NBYTES_Pos) | I2C_CR2_START;
	} else {
		_left = static_cast<uint32_t>(reg_len) + len;
		LoadNBytes(sadd | (read ? I2C_CR2_RD_WRN : 0u) | I2C_CR2_START);
	}
#elif defined(STM32F4)
	I2Cx->SR1 = 0;                                 // clear stale error flags (rc_w0)
	I2C_CLEAR_BIT(I2Cx->CR1, I2C_CR1_POS);

	// ITBUFEN: TXE/RXNE interrupts for bytes moved by the CPU (data in Irq
	// mode, register bytes in Dma mode). DMAEN: data by DMA, LAST: NACK on
	// the last DMA byte.
	uint32_t cr2 = 0;
	if (mode != Mode::Poll)                    cr2 |= I2C_CR2_ITEVTEN | I2C_CR2_ITERREN;
	if (mode == Mode::Irq || (mode == Mode::Dma && reg_len)) cr2 |= I2C_CR2_ITBUFEN;
	if (mode == Mode::Dma && !reg_len)         cr2 |= I2C_CR2_DMAEN | (read ? I2C_CR2_LAST : 0u);
	I2C_MODIFY_REG(I2Cx->CR2, CR2_CTRL, cr2);

	_state = State::Start;
	I2C_SET_BIT(I2Cx->CR1, I2C_CR1_ACK | I2C_CR1_START);
#endif
	return SysStatus::OK;
}

SysStatus I2C::Run(uint8_t addr, uint16_t reg, uint8_t reg_len,
				   uint8_t* buf, uint16_t len, bool read, uint32_t timeout)
{
	SysStatus s = Begin(Mode::Poll, addr, reg, reg_len, buf, len, read);
	if (s != SysStatus::OK) return s;

	// Poll mode is serviced only here; IRQ sources stay disabled.
	const uint32_t tick = System::GetTick();
	while (IsBusy()) {
		Process(StatusReg());
		if (IsBusy() && System::GetTick() - tick >= timeout) {
			_err = ERR::Timeout;
			ResetPeriph();
			Complete(false);
			return SysStatus::Timeout;
		}
	}
	return (_err == ERR::None) ? SysStatus::OK : SysStatus::Error;
}

void I2C::LoadNBytes(uint32_t cr2)
{
#if defined(STM32G0) || defined(STM32F7)
	const uint32_t n = (_left > NBYTES_MAX) ? NBYTES_MAX : _left;
	_left -= n;
	I2Cx->CR2 = cr2 | (n << I2C_CR2_NBYTES_Pos) | (_left ? I2C_CR2_RELOAD : I2C_CR2_AUTOEND);
#else
	(void)cr2;
#endif
}

void I2C::Process(uint32_t status)
{
#if defined(STM32G0) || defined(STM32F7)
	const uint32_t isr = status;

	// Errors. ARLO: the block has released the bus and is a slave again.
	// BERR: transfer state undefined -> peripheral reset. (OVR: slave with
	// NOSTRETCH only, not possible here.)
	if (isr & (I2C_ISR_ARLO | I2C_ISR_BERR)) {
		I2Cx->ICR = I2C_ICR_ARLOCF | I2C_ICR_BERRCF;
		_err = (isr & I2C_ISR_ARLO) ? ERR::ArbLost : ERR::BusError;
		if (isr & I2C_ISR_BERR) ResetPeriph();
		Finish();
		return;
	}
	if (isr & I2C_ISR_NACKF) {
		// With AUTOEND the block sends the STOP itself; only the register
		// phase of ReadReg (and a RELOAD chunk) needs ours
		I2Cx->ICR = I2C_ICR_NACKCF;
		_err      = ERR::Nack;
		I2Cx->ISR = I2C_ISR_TXE;                   // flush TXDR
		if (!(I2Cx->CR2 & I2C_CR2_AUTOEND)) I2C_SET_BIT(I2Cx->CR2, I2C_CR2_STOP);
		_state = State::Stop;
	}

	switch (_state) {
	case State::TxReg:
		if (!(isr & I2C_ISR_TXIS)) break;
		I2Cx->TXDR = _reg[_reg_idx++];
		if (_reg_idx < _reg_len) break;
		if (_read) {
			_state = State::Restart;
		} else {
			_state = State::TxData;
			if (_mode == Mode::Irq && !_len) I2C_CLEAR_BIT(I2Cx->CR1, I2C_CR1_TXIE);
			if (_mode == Mode::Dma)                // data by DMA from here on
				I2C_MODIFY_REG(I2Cx->CR1, I2C_CR1_TXIE, I2C_CR1_TXDMAEN);
		}
		break;

	case State::Restart:
		if (!(isr & I2C_ISR_TC)) break;
		_state = State::RxData;
		I2C_MODIFY_REG(I2Cx->CR1, I2C_CR1_TXIE, (_mode == Mode::Irq ? I2C_CR1_RXIE : _mode == Mode::Dma ? I2C_CR1_RXDMAEN : 0u));
		_left = _len;
		LoadNBytes((static_cast<uint32_t>(_addr) << 1) | I2C_CR2_RD_WRN | I2C_CR2_START);
		break;

	case State::TxData:
		if ((isr & I2C_ISR_TXIS) && _mode != Mode::Dma && _idx < _len)
			I2Cx->TXDR = _buf[_idx++];
		if (isr & I2C_ISR_TCR) LoadNBytes(static_cast<uint32_t>(_addr) << 1);
		break;

	case State::RxData:
		if ((isr & I2C_ISR_RXNE) && _mode != Mode::Dma && _idx < _len)
			_buf[_idx++] = static_cast<uint8_t>(I2Cx->RXDR);
		if (isr & I2C_ISR_TCR) LoadNBytes((static_cast<uint32_t>(_addr) << 1) | I2C_CR2_RD_WRN);
		break;

	default:                                       // Stop: only STOPF below
		break;
	}

	if (isr & I2C_ISR_STOPF) {
		I2Cx->ICR = I2C_ICR_STOPCF;
		if (_err == ERR::None && (_reg_idx != _reg_len
			|| (_mode != Mode::Dma && _idx != _len))) _err = ERR::BusError;
		Finish();
	}

#elif defined(STM32F4)
	// v1 IP has no STOPF in master mode: the transfer ends when we request
	// the STOP. Reception follows RM0090 "Master receiver" for N = 1, 2, >2.
	const uint32_t sr1 = status;

	if (sr1 & SR1_ERR) {
		I2Cx->SR1 = ~SR1_ERR;                      // rc_w0
		_err = (sr1 & I2C_SR1_AF)   ? ERR::Nack     :
			   (sr1 & I2C_SR1_ARLO) ? ERR::ArbLost  : ERR::BusError;
		if (!(sr1 & I2C_SR1_ARLO))                 // after ARLO the block is a slave again
			I2C_SET_BIT(I2Cx->CR1, I2C_CR1_STOP);
		Finish();
		return;
	}

	const bool rx = _read && _reg_idx == _reg_len; // register (if any) already sent

	switch (_state) {
	case State::Start:
		if (!(sr1 & I2C_SR1_SB)) break;
		I2Cx->DR = (static_cast<uint32_t>(_addr) << 1) | (rx ? 1u : 0u);
		_state = State::Addr;
		break;

	case State::Addr:
		if (!(sr1 & I2C_SR1_ADDR)) break;
		if (!rx) {
			(void)I2Cx->SR2;                       // SR1 then SR2 read: ADDR cleared
			if (_reg_len)    _state = State::TxReg;
			else if (_len)   _state = State::TxData;
			else {                                 // IsReady(): address acknowledged
				I2C_SET_BIT(I2Cx->CR1, I2C_CR1_STOP);
				Finish();
			}
			break;
		}
		if (_mode == Mode::Dma) {                  // LAST NACKs the last byte; N = 1: ACK off here
			if (_len == 1) I2C_CLEAR_BIT(I2Cx->CR1, I2C_CR1_ACK);
			(void)I2Cx->SR2;
			if (_len == 1) I2C_SET_BIT(I2Cx->CR1, I2C_CR1_STOP);
			_state = State::RxData;                // ends in the separate DMA RX handler
		} else if (_len == 1) {
			I2C_CLEAR_BIT(I2Cx->CR1, I2C_CR1_ACK);
			(void)I2Cx->SR2;
			I2C_SET_BIT(I2Cx->CR1, I2C_CR1_STOP);
			_state = State::RxTail;
		} else if (_len == 2) {
			I2C_MODIFY_REG(I2Cx->CR1, I2C_CR1_ACK, I2C_CR1_POS);   // NACK goes to the 2nd byte
			(void)I2Cx->SR2;
			_state = State::RxTail;
		} else {
			(void)I2Cx->SR2;
			_state = (_len == 3) ? State::RxTail : State::RxData;
		}
		if (_state == State::RxTail && _len > 1 && _mode == Mode::Irq)
			I2C_CLEAR_BIT(I2Cx->CR2, I2C_CR2_ITBUFEN);         // only BTF matters for the last bytes
		break;

	case State::TxReg:
		if (!(sr1 & I2C_SR1_TXE)) break;
		I2Cx->DR = _reg[_reg_idx++];
		if (_reg_idx < _reg_len) break;
		if (_read) {                               // wait for BTF: register fully out
			I2C_CLEAR_BIT(I2Cx->CR2, I2C_CR2_ITBUFEN);
			_state = State::Restart;
		} else {
			_state = State::TxData;
			if (_mode == Mode::Dma)                // data by DMA from here on
				I2C_MODIFY_REG(I2Cx->CR2, I2C_CR2_ITBUFEN, I2C_CR2_DMAEN);
			else if (!_len)
				I2C_CLEAR_BIT(I2Cx->CR2, I2C_CR2_ITBUFEN);
		}
		break;

	case State::Restart: {
		// The old BTF stays set until the START goes out; Start waits for SB only
		if (!(sr1 & I2C_SR1_BTF)) break;
		uint32_t cr2 = 0;
		if (_mode == Mode::Irq) cr2 |= I2C_CR2_ITBUFEN;
		if (_mode == Mode::Dma) cr2 |= I2C_CR2_DMAEN | I2C_CR2_LAST;
		I2C_MODIFY_REG(I2Cx->CR2, I2C_CR2_ITBUFEN | I2C_CR2_DMAEN | I2C_CR2_LAST, cr2);
		I2C_SET_BIT(I2Cx->CR1, I2C_CR1_ACK | I2C_CR1_START);
		_state = State::Start;
		break;
	}

	case State::TxData:
		if (!(sr1 & I2C_SR1_TXE)) break;
		if (_mode != Mode::Dma && _idx < _len) {
			I2Cx->DR = _buf[_idx++];
			if (_idx == _len) I2C_CLEAR_BIT(I2Cx->CR2, I2C_CR2_ITBUFEN);   // last byte queued: wait for BTF
		} else if ((sr1 & I2C_SR1_BTF) && (_mode != Mode::Dma || _dma_tx.GetCount() == 0)) {
			I2C_SET_BIT(I2Cx->CR1, I2C_CR1_STOP);             // everything shifted out
			Finish();
		}
		break;

	case State::RxData:                            // N > 3 by CPU; Dma: nothing to do here
		if (_mode == Mode::Dma || !(sr1 & I2C_SR1_RXNE)) break;
		_buf[_idx++] = static_cast<uint8_t>(I2Cx->DR);
		if (_len - _idx == 3) {
			_state = State::RxTail;
			if (_mode == Mode::Irq) I2C_CLEAR_BIT(I2Cx->CR2, I2C_CR2_ITBUFEN);
		}
		break;

	case State::RxTail:
		if (_len == 1) {                           // STOP already requested at ADDR
			if (!(sr1 & I2C_SR1_RXNE)) break;
			_buf[_idx++] = static_cast<uint8_t>(I2Cx->DR);
			Finish();
			break;
		}
		if (!(sr1 & I2C_SR1_BTF)) break;           // one byte in DR, one in the shift register
		if (_len - _idx == 3) {
			I2C_CLEAR_BIT(I2Cx->CR1, I2C_CR1_ACK);             // NACK the last byte
			_buf[_idx++] = static_cast<uint8_t>(I2Cx->DR);
			break;
		}
		I2C_SET_BIT(I2Cx->CR1, I2C_CR1_STOP);                 // 2 left
		_buf[_idx++] = static_cast<uint8_t>(I2Cx->DR);
		_buf[_idx++] = static_cast<uint8_t>(I2Cx->DR);
		Finish();
		break;

	default:
		break;
	}
#endif
}


// Peripheral completion and DMA completion are separate events. When DMA still
// owns a buffer, keep DmaStop until its own IRQ confirms TC/TE; never spin here.
void I2C::Finish()
{
#if defined(STM32F4)
    I2C_CLEAR_BIT(I2Cx->CR2, CR2_CTRL);
    I2C_CLEAR_BIT(I2Cx->CR1, I2C_CR1_POS);
#else
    I2C_CLEAR_BIT(I2Cx->CR1, CR1_IE_ALL | CR1_DMAEN);
    I2Cx->CR2 = 0;
#endif
    if (_mode != Mode::Dma) { Complete(true); return; }

    DMA_Sx& dma = _read ? _dma_rx : _dma_tx;
    _state = State::DmaStop;
#if defined(STM32F4) || defined(STM32F7)
    if (_err != ERR::None) dma.RequestStop();
#endif
    const uint32_t flags = dma.GetIRQFlags();
    if (flags & DMA_Sx::FLAG_ERRORS) {
        if (_err == ERR::None) _err = ERR::DmaError;
#if defined(STM32F4) || defined(STM32F7)
        dma.RequestStop();
#endif
    }
#if defined(STM32F4) || defined(STM32F7)
    // Normal mode clears EN at finite completion. A requested stop also needs
    // TC (or hardware TE), so an intermediate EN read cannot publish early.
    if (dma.IsEnabled() || !(flags & (DMA_Sx::FLAG_TC | DMA_LISR_TEIF0))) return;
#else
    // A failed G0 transfer is discarded: abort CNDTR is not used. A normal
    // STOP waits for the finite DMA's TC, not an assumed last-request delay.
    if (_err == ERR::None && !(flags & DMA_Sx::FLAG_TC)) return;
#endif
    Complete(true);
}

void I2C::Complete(bool notify)
{
    if (_mode == Mode::Dma) {
        DMA_Sx& dma = _read ? _dma_rx : _dma_tx;
        dma.Disable_IRQ(DMA_Sx::IRQ::TE);
        dma.Disable_IRQ(DMA_Sx::IRQ::TC);
#if defined(STM32F4) || defined(STM32F7)
        dma.Disable_IRQ(DMA_Sx::IRQ::DME);
#else
        dma.RequestStop(); // G0 finite transfers leave EN set.
#endif
        __DMB();
        if (_err == ERR::None && dma.GetCount() != 0) _err = ERR::DmaError;
        dma.ClearFlags();
        if (_read && _err == ERR::None) System::CacheInvalidate(_buf, _len);
    }
    const ERR error = _err;
    const bool callback = notify && _mode != Mode::Poll;
    _buf = nullptr;
    __DMB();
    _state = State::Idle;
    if (callback) {
        if (error == ERR::None) OnComplete();
        else OnError(error);
    }
}

void I2C::HandleDmaTxIRQ()
{
    if (!_info || _mode != Mode::Dma || _read || _state == State::Idle) return;
    const uint32_t flags = _dma_tx.GetIRQFlags();
    if ((flags & DMA_Sx::FLAG_ERRORS) && _err == ERR::None) {
        _err = ERR::DmaError;
        ResetPeriph();
        Finish();
        return;
    }
    if (_state == State::DmaStop) {
#if defined(STM32F4) || defined(STM32F7)
        if (_dma_tx.IsEnabled() || !(flags & (DMA_Sx::FLAG_TC | DMA_LISR_TEIF0))) return;
#else
        if (!(flags & DMA_Sx::FLAG_TC) && _err == ERR::None) return;
#endif
        Complete(true);
    } else if (flags & DMA_Sx::FLAG_TC) {
        // Data left memory; I2C still owes BTF/STOP. Retain TC for Finish().
        _dma_tx.Disable_IRQ(DMA_Sx::IRQ::TC);
    }
}

void I2C::HandleDmaRxIRQ()
{
    if (!_info || _mode != Mode::Dma || !_read || _state == State::Idle) return;
    const uint32_t flags = _dma_rx.GetIRQFlags();
    if ((flags & DMA_Sx::FLAG_ERRORS) && _err == ERR::None) {
        _err = ERR::DmaError;
        ResetPeriph();
        Finish();
        return;
    }
    if (_state == State::DmaStop) {
#if defined(STM32F4) || defined(STM32F7)
        if (_dma_rx.IsEnabled() || !(flags & (DMA_Sx::FLAG_TC | DMA_LISR_TEIF0))) return;
#else
        if (!(flags & DMA_Sx::FLAG_TC) && _err == ERR::None) return;
#endif
        Complete(true);
        return;
    }
    if (!(flags & DMA_Sx::FLAG_TC)) return;
    _dma_rx.Disable_IRQ(DMA_Sx::IRQ::TC);
#if defined(STM32F4)
    const uint32_t sr1 = I2Cx->SR1;
    if (sr1 & SR1_ERR) { Process(sr1); return; }
    // LAST NACKed the last byte. For N=1 STOP was already set at ADDR.
    if (_len > 1) I2C_SET_BIT(I2Cx->CR1, I2C_CR1_STOP);
    I2C_CLEAR_BIT(I2Cx->CR2, CR2_CTRL);
    I2C_CLEAR_BIT(I2Cx->CR1, I2C_CR1_POS);
    Complete(true); // TC confirms memory completion; STOP itself is not awaited.
#endif
}

void I2C::HandleIRQ()
{
    if (!_info || _state == State::Idle || _state == State::DmaStop || _mode == Mode::Poll) return;
    Process(StatusReg()); // One snapshot, one bounded FSM step.
}

void I2C::ResetPeriph()
{
#if defined(STM32G0) || defined(STM32F7)
    // Disable transfer sources too. Three APB reads meet the PE-low interval;
    // these are fixed accesses, not a polling loop.
    I2C_CLEAR_BIT(I2Cx->CR1, I2C_CR1_PE | CR1_IE_ALL | CR1_DMAEN);
    (void)I2Cx->CR1;
    (void)I2Cx->CR1;
    (void)I2Cx->CR1;
    I2C_SET_BIT(I2Cx->CR1, I2C_CR1_PE);
#else
    I2Cx->CR1 = I2C_CR1_SWRST;
    I2Cx->CR1 = 0;
    I2Cx->CR2 = _timing >> 24;
    I2Cx->CCR = _timing & 0xFFFFu;
    I2Cx->TRISE = (_timing >> 16) & 0xFFu;
    I2Cx->CR1 = I2C_CR1_PE;
#endif
}

void I2C::Abort()
{
    if (!_info || _state == State::Idle || _mode == Mode::Poll) return;
    _err = ERR::Aborted;
#if defined(STM32F4)
    I2C_CLEAR_BIT(I2Cx->CR2, CR2_CTRL);
#else
    I2C_CLEAR_BIT(I2Cx->CR1, CR1_IE_ALL | CR1_DMAEN);
#endif
    if (_mode == Mode::Dma) (_read ? _dma_rx : _dma_tx).Stream_EN(DISABLE);
    ResetPeriph();
    Complete(false);
}

void I2C::DeInit()
{
    if (!_info) return;
    Abort();
    I2Cx->CR1 = 0;
#if defined(STM32F4)
    I2Cx->CR2 = 0;
#endif
    if (_dma_tx.HasSelection()) _dma_tx.Stream_EN(DISABLE);
    if (_dma_rx.HasSelection()) _dma_rx.Stream_EN(DISABLE);
    const IRQn_Type none = static_cast<IRQn_Type>(-1);
    const IRQn_Type irqs[4] = {
        _info->irq_ev, _info->irq_er != _info->irq_ev ? _info->irq_er : none,
        _dma_tx.HasSelection() ? _dma_tx.GetIRQn() : none,
        _dma_rx.HasSelection() ? _dma_rx.GetIRQn() : none
    };
    IIRQHandler* const handlers[4] = { this, this, &_dma_tx_irq, &_dma_rx_irq };
    for (unsigned i = 0; i < 4; ++i) {
        if (irqs[i] == none) continue;
        const bool enabled = NVIC_GetEnableIRQ(irqs[i]) != 0;
        NVIC_DisableIRQ(irqs[i]);
        IRQ_Registry::Unregister(irqs[i], handlers[i]);
        if (IRQ_Registry::HasHandlers(irqs[i])) {
            if (enabled) NVIC_EnableIRQ(irqs[i]);
        } else NVIC_ClearPendingIRQ(irqs[i]);
    }
    if (_dma_tx.HasSelection()) _dma_tx.Release();
    if (_dma_rx.HasSelection()) _dma_rx.Release();
#if defined(STM32G0)
    I2C_CLEAR_BIT(SYSCFG->CFGR1, _info->fmp_bit);
#elif defined(STM32F7)
    I2C_CLEAR_BIT(SYSCFG->PMC, _info->fmp_bit);
#endif
    I2C_SET_BIT(*_info->rst_reg, _info->rst_bit);
    I2C_CLEAR_BIT(*_info->rst_reg, _info->rst_bit);
    I2C_CLEAR_BIT(*_info->clk_reg, _info->clk_bit);
    _scl.SetUp(PIN::TYPE::INPUT_NO_Pull);
    _sda.SetUp(PIN::TYPE::INPUT_NO_Pull);
    _buf = nullptr;
    _state = State::Idle;
    _err = ERR::None;
    _info = nullptr;
}
