#include "i2c.hpp"

#if defined(STM32G0) || defined(STM32F7)
extern "C" {
#include "i2c_timing_utility.h"
}
#endif

#define I2C_DEFS_CPP
#include "i2c_defs.hpp"
#undef  I2C_DEFS_CPP

#if defined(STM32G0) || defined(STM32F7)
static constexpr uint32_t CR1_IE_ALL = I2C_CR1_TXIE | I2C_CR1_RXIE | I2C_CR1_TCIE |
									   I2C_CR1_STOPIE | I2C_CR1_NACKIE | I2C_CR1_ERRIE;
static constexpr uint32_t CR1_DMAEN  = I2C_CR1_TXDMAEN | I2C_CR1_RXDMAEN;
static constexpr uint32_t NBYTES_MAX = 255;
#elif defined(STM32F4)
static constexpr uint32_t CR2_CTRL   = I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN | I2C_CR2_ITERREN |
									   I2C_CR2_DMAEN | I2C_CR2_LAST;
static constexpr uint32_t SR1_ERR    = I2C_SR1_BERR | I2C_SR1_ARLO | I2C_SR1_AF;
#endif

// ---------------------------------------------------------------------------
// Ownership model
//
// _state is changed by the public methods and by HandleIRQ (EV, ER and DMA
// lines, possibly at different priorities; callbacks may start a new
// transfer). Rules:
//  - every operation first reserves the driver: Idle -> Config atomically
//    (Reserve()); a second caller gets Busy / InitError;
//  - the transfer's first state and the START are set under one IrqLock;
//  - HandleIRQ runs the state machine under IrqLock; Finish() only queues
//    the callback, which runs after the lock is released;
//  - private helpers take no lock: the public method owns the operation.
// ---------------------------------------------------------------------------

bool I2C::Reserve()
{
	IrqLock lock;
	if (_state != State::Idle) return false;
	_state = State::Config;
	return true;
}

void I2C::Release()
{
	_state = State::Idle;   // single store: no lock needed
}

// ---------------------------------------------------------------------------
// SetUp
// ---------------------------------------------------------------------------

SysInitStatus I2C::SetUp(SPEED speed)
{
	const PeriphInfo* info = nullptr;
	for (const auto& e : i2c_table)
		if (e.periph == I2Cx) { info = &e; break; }
	if (!info)      return SysInitStatus::InitError;
	if (!Reserve()) return SysInitStatus::InitError;
	_info = info;

	*info->clk_reg |=  info->clk_bit;
	*info->rst_reg |=  info->rst_bit;
	*info->rst_reg &= ~info->rst_bit;

	_scl.SetUp(PIN::TYPE::AF_OD_PulUp, PIN::OUTPUT_SPEED::High);
	_sda.SetUp(PIN::TYPE::AF_OD_PulUp, PIN::OUTPUT_SPEED::High);

	SysInitStatus st = SysInitStatus::InitOK;
#if defined(STM32G0) || defined(STM32F7)
	*info->ksel_reg &= ~info->ksel_mask;           // kernel clock = PCLK

	// Fm+ drive strength on the pins of this I2C
#if defined(STM32G0)
	RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;
	volatile uint32_t& fmp = SYSCFG->CFGR1;
#else
	RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
	volatile uint32_t& fmp = SYSCFG->PMC;
#endif
	if (speed == SPEED::FAST_PLUS) fmp |=  info->fmp_bit;
	else                           fmp &= ~info->fmp_bit;

	_timing = I2C_GetTiming(System::APB1BusClock, static_cast<uint32_t>(speed));
	if (_timing) {
		I2Cx->CR1     = 0;
		I2Cx->TIMINGR = _timing;
		I2Cx->CR1     = I2C_CR1_PE;
	} else {
		st = SysInitStatus::InitError;
	}
#elif defined(STM32F4)
	// v1 IP: SCL = PCLK / (2 * CCR) in Sm, PCLK / (3 * CCR) in Fm (DUTY = 0);
	// CCR rounded up so the bus never runs faster than requested.
	const uint32_t pclk = System::APB1BusClock;
	const uint32_t freq = pclk / 1000000u;
	const uint32_t hz   = static_cast<uint32_t>(speed);
	if (speed == SPEED::FAST_PLUS || freq < 2u || freq > 50u || (speed == SPEED::FAST && freq < 4u)) {
		st = SysInitStatus::InitError;
	} else {
		uint32_t ccr, trise;
		if (speed == SPEED::STANDARD) {
			ccr   = (pclk + 2u * hz - 1u) / (2u * hz);
			if (ccr < 4u) ccr = 4u;
			trise = freq + 1u;                     // 1000 ns max rise time
		} else {
			ccr   = (pclk + 3u * hz - 1u) / (3u * hz);
			ccr  |= I2C_CCR_FS;
			trise = freq * 300u / 1000u + 1u;      // 300 ns max rise time
		}
		_timing = ccr | (trise << 16) | (freq << 24);
		ResetPeriph();                             // writes the timing, PE = 1
	}
#endif
	if (st != SysInitStatus::InitOK) _info = nullptr;   // transfers -> NotInit
	Release();
	return st;
}

// ---------------------------------------------------------------------------
// Public transfer functions — all go through Begin() / Run()
// ---------------------------------------------------------------------------

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
	return Run(addr, reg, static_cast<uint8_t>(reg_size), const_cast<uint8_t*>(data), len, false, timeout);
}

SysStatus I2C::ReadReg(uint8_t addr, uint16_t reg, uint8_t* data, uint16_t len,
					   REG reg_size, uint32_t timeout)
{
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
	return Begin(Mode::Irq, addr, reg, static_cast<uint8_t>(reg_size), const_cast<uint8_t*>(data), len, false);
}

SysStatus I2C::ReadReg_IRQ(uint8_t addr, uint16_t reg, uint8_t* data, uint16_t len, REG reg_size)
{
	return Begin(Mode::Irq, addr, reg, static_cast<uint8_t>(reg_size), data, len, true);
}

SysStatus I2C::Send_DMA(uint8_t addr, const uint8_t* data, uint16_t len)
{
	return Begin(Mode::Dma, addr, 0, 0, const_cast<uint8_t*>(data), len, false);
}

SysStatus I2C::Receive_DMA(uint8_t addr, uint8_t* data, uint16_t len)
{
	return Begin(Mode::Dma, addr, 0, 0, data, len, true);
}

SysStatus I2C::WriteReg_DMA(uint8_t addr, uint16_t reg, const uint8_t* data, uint16_t len, REG reg_size)
{
	return Begin(Mode::Dma, addr, reg, static_cast<uint8_t>(reg_size), const_cast<uint8_t*>(data), len, false);
}

SysStatus I2C::ReadReg_DMA(uint8_t addr, uint16_t reg, uint8_t* data, uint16_t len, REG reg_size)
{
	return Begin(Mode::Dma, addr, reg, static_cast<uint8_t>(reg_size), data, len, true);
}

// ---------------------------------------------------------------------------
// AttachDMA
// ---------------------------------------------------------------------------

SysInitStatus I2C::AttachDMA(DMA_Sx* tx, DMA_Sx* rx)
{
	if (!_info)     return SysInitStatus::InitError;
	if (!Reserve()) return SysInitStatus::InitError;

#if defined(STM32G0) || defined(STM32F7)
	const uint32_t txd = reinterpret_cast<uint32_t>(&I2Cx->TXDR);
	const uint32_t rxd = reinterpret_cast<uint32_t>(&I2Cx->RXDR);
#else
	const uint32_t txd = reinterpret_cast<uint32_t>(&I2Cx->DR);
	const uint32_t rxd = txd;
#endif

	// A channel kept from the previous call is already set up (SetUp() of a
	// DMA_Sx works once)
	const bool tx_new = tx && tx != _dma_tx;
	const bool rx_new = rx && rx != _dma_rx;
	bool ok = true;
	if (tx_new) {
		DMA_Sx::StreamSettings cfg;
		cfg.direction   = DMA_Sx::DIR::To_Per;
		cfg.data_size   = DMA_Sx::SIZE::Byte;
		cfg.minc        = true;
		cfg.per_address = txd;
		ok = tx->SetUp(cfg, _info->dma_tx) == SysInitStatus::InitOK;
	}
	if (ok && rx_new) {
		DMA_Sx::StreamSettings cfg;
		cfg.direction   = DMA_Sx::DIR::From_Per;
		cfg.data_size   = DMA_Sx::SIZE::Byte;
		cfg.minc        = true;
		cfg.per_address = rxd;
		ok = rx->SetUp(cfg, _info->dma_rx) == SysInitStatus::InitOK;
		if (!ok && tx_new) tx->Release();          // roll back the TX channel of this call
	}
	if (ok) {
		DMA_Sx* old_tx = _dma_tx;
		DMA_Sx* old_rx = _dma_rx;
		BindDmaIrqs(tx, rx);                       // also stores _dma_tx / _dma_rx
		if (old_tx && old_tx != tx && old_tx != rx) old_tx->Release();
		if (old_rx && old_rx != rx && old_rx != tx) old_rx->Release();
	}

	Release();
	return ok ? SysInitStatus::InitOK : SysInitStatus::InitError;
}

void I2C::BindDmaIrqs(DMA_Sx* tx, DMA_Sx* rx)
{
	// DMA lines: TE (all families) and, F4, the RX TC. tx and rx may share
	// a line (G0); Register() is idempotent.
	const IRQn_Type none = static_cast<IRQn_Type>(-1);
	const IRQn_Type o[2] = { _dma_tx ? _dma_tx->GetIRQn() : none, _dma_rx ? _dma_rx->GetIRQn() : none };
	const IRQn_Type n[2] = { tx ? tx->GetIRQn() : none, rx ? rx->GetIRQn() : none };

	for (IRQn_Type l : o) {
		if (l == none || l == n[0] || l == n[1]) continue;
		IRQ_Registry::Unregister(l, this);
		if (!IRQ_Registry::HasHandlers(l)) NVIC_DisableIRQ(l);
	}
	for (IRQn_Type l : n)
		if (l != none) IRQ_Registry::Register(l, this);   // NVIC: DMA_Sx::Enable_IRQ()

	_dma_tx = tx;
	_dma_rx = rx;
}

// ---------------------------------------------------------------------------
// Abort / Recover
// ---------------------------------------------------------------------------

void I2C::Abort()
{
	IrqLock lock;
	if (_state == State::Idle || _state == State::Config || _mode == Mode::Poll) return;
	_err = ERR::Aborted;
	ResetPeriph();
	Finish(false);               // no callback
}

SysStatus I2C::Recover()
{
	if (!_info)     return SysStatus::NotInit;
	if (!Reserve()) return SysStatus::Busy;

	I2Cx->CR1 &= ~I2C_CR1_PE;
	_scl.SetLevel(true);
	_sda.SetLevel(true);
	_scl.SetUp(PIN::TYPE::OUTPUT_OD_PulUp, PIN::OUTPUT_SPEED::High);
	_sda.SetUp(PIN::TYPE::OUTPUT_OD_PulUp, PIN::OUTPUT_SPEED::High);

	// Half period of a ~25 kHz software clock: slow enough for any slave
	const uint32_t half = System::SystemCoreClock / 200000u;
	auto wait = [half] { volatile uint32_t i = half; while (i) { i = i - 1u; } };

	for (uint8_t i = 0; i < 9 && !_sda.GetLevel(); i++) {
		_scl.SetLevel(false); wait();
		_scl.SetLevel(true);  wait();
	}
	// STOP: SDA low -> high while SCL is high
	_scl.SetLevel(false); wait();
	_sda.SetLevel(false); wait();
	_scl.SetLevel(true);  wait();
	_sda.SetLevel(true);  wait();
	const bool free = _sda.GetLevel() && _scl.GetLevel();

	_scl.SetUp(PIN::TYPE::AF_OD_PulUp, PIN::OUTPUT_SPEED::High);
	_sda.SetUp(PIN::TYPE::AF_OD_PulUp, PIN::OUTPUT_SPEED::High);
	ResetPeriph();               // F4: also clears a BUSY flag stuck by the glitches
	Release();
	return free ? SysStatus::OK : SysStatus::Error;
}

// ---------------------------------------------------------------------------
// Transfer engine
// ---------------------------------------------------------------------------

SysStatus I2C::Begin(Mode mode, uint8_t addr, uint16_t reg, uint8_t reg_len,
					 uint8_t* buf, uint16_t len, bool read)
{
	if (!_info) return SysStatus::NotInit;
	if (addr > 0x7F || (len && !buf) || (read && !len)) return SysStatus::Error;
	if (!Reserve()) return SysStatus::Busy;

	// Under the reservation: the DMA channels cannot change any more
	DMA_Sx* dma = read ? _dma_rx : _dma_tx;
	if (mode == Mode::Dma && (!dma || !len || (read && !DMA_Sx::CacheRxOk(buf, len)))) {
		Release();
		return SysStatus::Error;
	}

	// Bus held by another master/slave (or, F4, our STOP still pending)
#if defined(STM32G0) || defined(STM32F7)
	const bool bus_busy = (I2Cx->ISR & I2C_ISR_BUSY) != 0u;
#elif defined(STM32F4)
	const bool bus_busy = (I2Cx->SR2 & I2C_SR2_BUSY) || (I2Cx->CR1 & I2C_CR1_STOP);
#endif
	if (bus_busy) {
		Release();
		return SysStatus::Busy;
	}

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
		if (read) DMA_Sx::CacheInvalidate(buf, len);
		else      DMA_Sx::CacheClean(buf, len);
		dma->ClearFlags();
		dma->SetMemAddr(reinterpret_cast<uint32_t>(buf));
		dma->SetCount(len);
		dma->Enable_IRQ(DMA_Sx::IRQ::TE);
#if defined(STM32F4)
		if (read) dma->Enable_IRQ(DMA_Sx::IRQ::TC);   // STOP after the last byte
#endif
		dma->Stream_EN(ENABLE);
	}
	if (mode != Mode::Poll) EnableIRQ();

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
	I2Cx->CR1 = (I2Cx->CR1 & ~(CR1_IE_ALL | CR1_DMAEN)) | cr1;

	const uint32_t sadd = static_cast<uint32_t>(addr) << 1;
	IrqLock lock;                                  // first state and START together
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
	I2Cx->CR1 &= ~I2C_CR1_POS;

	// ITBUFEN: TXE/RXNE interrupts for bytes moved by the CPU (data in Irq
	// mode, register bytes in Dma mode). DMAEN: data by DMA, LAST: NACK on
	// the last DMA byte.
	uint32_t cr2 = I2Cx->CR2 & ~CR2_CTRL;
	if (mode != Mode::Poll)                    cr2 |= I2C_CR2_ITEVTEN | I2C_CR2_ITERREN;
	if (mode == Mode::Irq || (mode == Mode::Dma && reg_len)) cr2 |= I2C_CR2_ITBUFEN;
	if (mode == Mode::Dma && !reg_len)         cr2 |= I2C_CR2_DMAEN | (read ? I2C_CR2_LAST : 0u);
	I2Cx->CR2 = cr2;

	IrqLock lock;                                  // first state and START together
	_state = State::Start;
	I2Cx->CR1 |= I2C_CR1_ACK | I2C_CR1_START;
#endif
	return SysStatus::OK;
}

SysStatus I2C::Run(uint8_t addr, uint16_t reg, uint8_t reg_len,
				   uint8_t* buf, uint16_t len, bool read, uint32_t timeout)
{
	SysStatus s = Begin(Mode::Poll, addr, reg, reg_len, buf, len, read);
	if (s != SysStatus::OK) return s;

	// Poll mode: HandleIRQ leaves this transfer alone, no lock needed
	const uint32_t tick = System::GetTick();
	while (IsBusy()) {
		Process();
		if (IsBusy() && System::GetTick() - tick > timeout) {
			_err = ERR::Timeout;
			ResetPeriph();
			Finish(false);
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

void I2C::Process()
{
#if defined(STM32G0) || defined(STM32F7)
	const uint32_t isr = I2Cx->ISR;

	// Errors. ARLO: the block has released the bus and is a slave again.
	// BERR: transfer state undefined -> peripheral reset. (OVR: slave with
	// NOSTRETCH only, not possible here.)
	if (isr & (I2C_ISR_ARLO | I2C_ISR_BERR)) {
		I2Cx->ICR = I2C_ICR_ARLOCF | I2C_ICR_BERRCF;
		_err = (isr & I2C_ISR_ARLO) ? ERR::ArbLost : ERR::BusError;
		if (isr & I2C_ISR_BERR) ResetPeriph();
		Finish(true);
		return;
	}
	if (isr & I2C_ISR_NACKF) {
		// With AUTOEND the block sends the STOP itself; only the register
		// phase of ReadReg (and a RELOAD chunk) needs ours
		I2Cx->ICR = I2C_ICR_NACKCF;
		_err      = ERR::Nack;
		I2Cx->ISR = I2C_ISR_TXE;                   // flush TXDR
		if (!(I2Cx->CR2 & I2C_CR2_AUTOEND)) I2Cx->CR2 |= I2C_CR2_STOP;
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
			if (_mode == Mode::Dma)                // data by DMA from here on
				I2Cx->CR1 = (I2Cx->CR1 & ~I2C_CR1_TXIE) | I2C_CR1_TXDMAEN;
		}
		break;

	case State::Restart:
		if (!(isr & I2C_ISR_TC)) break;
		_state = State::RxData;
		I2Cx->CR1 = (I2Cx->CR1 & ~I2C_CR1_TXIE)
		          | (_mode == Mode::Irq ? I2C_CR1_RXIE : _mode == Mode::Dma ? I2C_CR1_RXDMAEN : 0u);
		_left = _len;
		LoadNBytes((static_cast<uint32_t>(_addr) << 1) | I2C_CR2_RD_WRN | I2C_CR2_START);
		break;

	case State::TxData:
		if ((isr & I2C_ISR_TXIS) && _mode != Mode::Dma && _idx < _len)
			I2Cx->TXDR = _buf[_idx++];
		if (isr & I2C_ISR_TCR) LoadNBytes(I2Cx->CR2 & (I2C_CR2_SADD | I2C_CR2_RD_WRN));
		break;

	case State::RxData:
		if ((isr & I2C_ISR_RXNE) && _mode != Mode::Dma && _idx < _len)
			_buf[_idx++] = static_cast<uint8_t>(I2Cx->RXDR);
		if (isr & I2C_ISR_TCR) LoadNBytes(I2Cx->CR2 & (I2C_CR2_SADD | I2C_CR2_RD_WRN));
		break;

	default:                                       // Stop: only STOPF below
		break;
	}

	if (isr & I2C_ISR_STOPF) {
		I2Cx->ICR = I2C_ICR_STOPCF;
		Finish(true);
	}

#elif defined(STM32F4)
	// v1 IP has no STOPF in master mode: the transfer ends when we request
	// the STOP. Reception follows RM0090 "Master receiver" for N = 1, 2, >2.
	const uint32_t sr1 = I2Cx->SR1;

	if (sr1 & SR1_ERR) {
		I2Cx->SR1 = ~SR1_ERR;                      // rc_w0
		_err = (sr1 & I2C_SR1_AF)   ? ERR::Nack     :
			   (sr1 & I2C_SR1_ARLO) ? ERR::ArbLost  : ERR::BusError;
		if (!(sr1 & I2C_SR1_ARLO))                 // after ARLO the block is a slave again
			I2Cx->CR1 |= I2C_CR1_STOP;
		Finish(true);
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
				I2Cx->CR1 |= I2C_CR1_STOP;
				Finish(true);
			}
			break;
		}
		if (_mode == Mode::Dma) {                  // LAST NACKs the last byte; N = 1: ACK off here
			if (_len == 1) I2Cx->CR1 &= ~I2C_CR1_ACK;
			(void)I2Cx->SR2;
			if (_len == 1) I2Cx->CR1 |= I2C_CR1_STOP;
			_state = State::RxData;                // ends on DMA TC (HandleIRQ)
		} else if (_len == 1) {
			I2Cx->CR1 &= ~I2C_CR1_ACK;
			(void)I2Cx->SR2;
			I2Cx->CR1 |= I2C_CR1_STOP;
			_state = State::RxTail;
		} else if (_len == 2) {
			I2Cx->CR1 = (I2Cx->CR1 & ~I2C_CR1_ACK) | I2C_CR1_POS;   // NACK goes to the 2nd byte
			(void)I2Cx->SR2;
			_state = State::RxTail;
		} else {
			(void)I2Cx->SR2;
			_state = (_len == 3) ? State::RxTail : State::RxData;
		}
		if (_state == State::RxTail && _len > 1 && _mode == Mode::Irq)
			I2Cx->CR2 &= ~I2C_CR2_ITBUFEN;         // only BTF matters for the last bytes
		break;

	case State::TxReg:
		if (!(sr1 & I2C_SR1_TXE)) break;
		I2Cx->DR = _reg[_reg_idx++];
		if (_reg_idx < _reg_len) break;
		if (_read) {                               // wait for BTF: register fully out
			I2Cx->CR2 &= ~I2C_CR2_ITBUFEN;
			_state = State::Restart;
		} else {
			_state = State::TxData;
			if (_mode == Mode::Dma)                // data by DMA from here on
				I2Cx->CR2 = (I2Cx->CR2 & ~I2C_CR2_ITBUFEN) | I2C_CR2_DMAEN;
			else if (!_len)
				I2Cx->CR2 &= ~I2C_CR2_ITBUFEN;
		}
		break;

	case State::Restart: {
		// The old BTF stays set until the START goes out; Start waits for SB only
		if (!(sr1 & I2C_SR1_BTF)) break;
		uint32_t cr2 = I2Cx->CR2 & ~(I2C_CR2_ITBUFEN | I2C_CR2_DMAEN | I2C_CR2_LAST);
		if (_mode == Mode::Irq) cr2 |= I2C_CR2_ITBUFEN;
		if (_mode == Mode::Dma) cr2 |= I2C_CR2_DMAEN | I2C_CR2_LAST;
		I2Cx->CR2  = cr2;
		I2Cx->CR1 |= I2C_CR1_ACK | I2C_CR1_START;
		_state = State::Start;
		break;
	}

	case State::TxData:
		if (!(sr1 & I2C_SR1_TXE)) break;
		if (_mode != Mode::Dma && _idx < _len) {
			I2Cx->DR = _buf[_idx++];
			if (_idx == _len) I2Cx->CR2 &= ~I2C_CR2_ITBUFEN;   // last byte queued: wait for BTF
		} else if ((sr1 & I2C_SR1_BTF) && (_mode != Mode::Dma || _dma_tx->GetCount() == 0)) {
			I2Cx->CR1 |= I2C_CR1_STOP;             // everything shifted out
			Finish(true);
		}
		break;

	case State::RxData:                            // N > 3 by CPU; Dma: nothing to do here
		if (_mode == Mode::Dma || !(sr1 & I2C_SR1_RXNE)) break;
		_buf[_idx++] = static_cast<uint8_t>(I2Cx->DR);
		if (_len - _idx == 3) {
			_state = State::RxTail;
			if (_mode == Mode::Irq) I2Cx->CR2 &= ~I2C_CR2_ITBUFEN;
		}
		break;

	case State::RxTail:
		if (_len == 1) {                           // STOP already requested at ADDR
			if (!(sr1 & I2C_SR1_RXNE)) break;
			_buf[_idx++] = static_cast<uint8_t>(I2Cx->DR);
			Finish(true);
			break;
		}
		if (!(sr1 & I2C_SR1_BTF)) break;           // one byte in DR, one in the shift register
		if (_len - _idx == 3) {
			I2Cx->CR1 &= ~I2C_CR1_ACK;             // NACK the last byte
			_buf[_idx++] = static_cast<uint8_t>(I2Cx->DR);
			break;
		}
		I2Cx->CR1 |= I2C_CR1_STOP;                 // 2 left
		_buf[_idx++] = static_cast<uint8_t>(I2Cx->DR);
		_buf[_idx++] = static_cast<uint8_t>(I2Cx->DR);
		Finish(true);
		break;

	default:
		break;
	}
#endif
}

void I2C::Finish(bool notify)
{
	const Mode mode = _mode;
#if defined(STM32G0) || defined(STM32F7)
	I2Cx->CR1 &= ~(CR1_IE_ALL | CR1_DMAEN);
	I2Cx->CR2  = 0;
#elif defined(STM32F4)
	I2Cx->CR2 &= ~CR2_CTRL;
	I2Cx->CR1 &= ~I2C_CR1_POS;
	// Let the requested STOP go out (cleared by hardware), so that a transfer
	// started right after this one (e.g. from the callback) finds the bus
	// free. Bounded (one byte time at 100 kHz is far below this); if the STOP
	// is still pending the bus is stuck: report it and reset the peripheral.
	uint32_t n = System::SystemCoreClock / 1000u;
	while ((I2Cx->CR1 & I2C_CR1_STOP) && n) n--;
	if (I2Cx->CR1 & I2C_CR1_STOP) {
		if (_err == ERR::None) _err = ERR::Timeout;
		ResetPeriph();
	}
#endif
	if (mode == Mode::Dma) {
		DMA_Sx* dma = _read ? _dma_rx : _dma_tx;
		dma->Disable_IRQ(DMA_Sx::IRQ::TE);
		dma->Disable_IRQ(DMA_Sx::IRQ::TC);
		dma->Stream_EN(DISABLE);
		if (_read) DMA_Sx::CacheInvalidate(_buf, _len);   // F7: CPU reads the DMA data
	}

	_state = State::Idle;
	if (notify && mode != Mode::Poll)
		_pending_cb = (_err == ERR::None) ? _done_cb : _err_cb;
}

void I2C::ResetPeriph()
{
#if defined(STM32G0) || defined(STM32F7)
	// PE low for at least 3 APB cycles: each read-back is one APB access
	I2Cx->CR1 &= ~I2C_CR1_PE;
	(void)I2Cx->CR1;
	(void)I2Cx->CR1;
	(void)I2Cx->CR1;
	I2Cx->CR1 |= I2C_CR1_PE;
#elif defined(STM32F4)
	// PE = 0 only takes effect at the end of a transfer on v1: use SWRST,
	// which also clears the timing registers -> write them again.
	I2Cx->CR1   = I2C_CR1_SWRST;
	I2Cx->CR1   = 0;
	I2Cx->CR2   = _timing >> 24;
	I2Cx->CCR   = _timing & 0xFFFFu;
	I2Cx->TRISE = (_timing >> 16) & 0xFFu;
	I2Cx->CR1   = I2C_CR1_PE;
#endif
}

void I2C::EnableIRQ()
{
	if (_irq_registered) return;
	IRQ_Registry::Register(_info->irq_ev, this);
	NVIC_EnableIRQ(_info->irq_ev);
	if (_info->irq_er != _info->irq_ev) {
		IRQ_Registry::Register(_info->irq_er, this);
		NVIC_EnableIRQ(_info->irq_er);
	}
	_irq_registered = true;
}

void I2C::HandleIRQ()
{
	void (*cb)(void) = nullptr;
	{
		// One owner of the transfer state: EV, ER and DMA lines may nest at
		// different priorities
		IrqLock lock;

		// The line may be shared (G0: I2C2/I2C3, DMA lines); a blocking
		// transfer polls Process() itself; Config: being set up
		if (_state == State::Idle || _state == State::Config || _mode == Mode::Poll) return;

		DMA_Sx* dma = (_mode == Mode::Dma) ? (_read ? _dma_rx : _dma_tx) : nullptr;
		if (dma && dma->GetTE_Flag()) {
			// DMA error: the stream has stopped, the transfer cannot complete
			dma->ClearFlags();
			_err = ERR::DmaError;
			ResetPeriph();
			Finish(true);
#if defined(STM32F4)
		} else if (dma && _read && _state == State::RxData && dma->GetTC_Flag()) {
			dma->ClearTC_Flag();
			if (I2Cx->SR1 & SR1_ERR) {
				Process();                         // an I2C error wins over the DMA TC
			} else {
				if (_len > 1) I2Cx->CR1 |= I2C_CR1_STOP;   // LAST NACKed the last byte; N = 1: STOP set at ADDR
				Finish(true);
			}
#endif
		} else {
			Process();
		}

		cb = _pending_cb;
		_pending_cb = nullptr;
	}
	if (cb) cb();   // after the lock: may start the next transfer
}
