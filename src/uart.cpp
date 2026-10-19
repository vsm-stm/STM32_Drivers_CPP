/**
 * @file uart.cpp
 * @brief UART setup and blocking/IRQ/DMA transfer implementation.
 *
 * uart.hpp contains the public contracts and complete usage examples.
 * IRQ dispatch tests flag AND source-enable/state; a bound DMA resource alone
 * does not mean the current operation uses DMA. Request gates identify that path.
 * Packet handover stops DMA before count/cache/CPU access. Registry mutations
 * belong only to main-loop setup/deinitialization, never completion.
 */
#include "uart.hpp"

#define UART_DEFS_CPP
#include "uart_defs.hpp"
#undef UART_DEFS_CPP

// OVER8=0: 16-bit BRR, USARTDIV >= 16. G0 PRESC is /1.
static uint32_t BaudRegister(uint32_t clock, uint32_t baud)
{
	if (!baud || baud > clock / 16U) return 0;
	const uint32_t brr = (clock + baud / 2U) / baud;
	constexpr uint32_t max_brr = 0xFFFFU;
	return brr >= 16U && brr <= max_brr ? brr : 0;
}

uint32_t USART::KernelClock(const PeriphInfo& info) const
{
#if defined(STM32F7) || defined(STM32G0)
	if (!info.clock_select && _clock_source != ClockSource::PCLK) return 0;
	switch (_clock_source) {
	case ClockSource::PCLK: return *info.bus_clk;
	case ClockSource::SYSCLK: {
		// HPRE encodes /1, /2, /4, /8, /16, /64, /128, /256, /512.
		// SystemCoreClock is HCLK: undo the AHB divider, not the APB divider.
		static constexpr uint8_t ahb_shift[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9};
		return System::SystemCoreClock << ahb_shift[(RCC->CFGR & RCC_CFGR_HPRE) >> RCC_CFGR_HPRE_Pos];
	}
	case ClockSource::HSI: {
		const uint32_t cr = RCC->CR;
#if defined(STM32G0)
		const uint32_t enabled = RCC_CR_HSION | RCC_CR_HSIKERON;
#else
		const uint32_t enabled = RCC_CR_HSION;
#endif
		return (cr & enabled) && (cr & RCC_CR_HSIRDY) ? HSI_Clock : 0;
	}
	case ClockSource::LSE:
		return (RCC->BDCR & (RCC_BDCR_LSEON | RCC_BDCR_LSERDY))
			== (RCC_BDCR_LSEON | RCC_BDCR_LSERDY) ? RCCDefs::LSE_HZ : 0;
	default: return 0;
	}
#else
	return *info.bus_clk;
#endif
}

// Select the kernel clock and program BRR with UE=0. Register the object once even if another
// peripheral already enabled the shared NVIC line. FIFO support is per instance.
SysInitStatus USART::SetUp(FIFO fifo, FIFO_TH fifo_th_tx, FIFO_TH fifo_th_rx)
{
	if (_info) return SysInitStatus::InitError;
	const PeriphInfo* info = nullptr;
	for (const auto& e : usart_table)
		if (e.periph == USARTx) { info = &e; break; }
	if (!info) return SysInitStatus::InitError;
	const uint32_t brr = BaudRegister(KernelClock(*info), BaudRate);
	if (!brr) return SysInitStatus::InitError;
#if defined(STM32G0)
	if ((fifo != FIFO::NO && fifo != FIFO::EN)
		|| static_cast<uint32_t>(fifo_th_tx) > static_cast<uint32_t>(FIFO_TH::all)
		|| static_cast<uint32_t>(fifo_th_rx) > static_cast<uint32_t>(FIFO_TH::all)
		|| (fifo == FIFO::EN && !IS_UART_FIFO_INSTANCE(USARTx))
		|| (fifo == FIFO::NO && (fifo_th_tx != FIFO_TH::NO || fifo_th_rx != FIFO_TH::NO)))
		return SysInitStatus::InitError;
#endif

	ATOMIC_SET_BIT(*info->clk_reg, info->clk_bit);
	(void)*info->clk_reg;
	USARTx->CR1 = 0; // BRR and FIFO configuration require UE=0.
#if defined(STM32F7) || defined(STM32G0)
	if (info->clock_select) {
		// Locate this instance's two-bit USARTxSEL field; the mask is nonzero here.
		const uint32_t selection = static_cast<uint32_t>(_clock_source) << __builtin_ctz(info->clock_select);
#if defined(STM32F7)
		ATOMIC_MODIFY_REG(RCC->DCKCFGR2, info->clock_select, selection);
#else
		ATOMIC_MODIFY_REG(RCC->CCIPR, info->clock_select, selection);
#endif
	}
#endif
#if defined(STM32G0)
	USARTx->PRESC = 0;
#endif
	USARTx->BRR = brr;
	USARTx->CR2 = 0;
	USARTx->CR1 = static_cast<uint32_t>(fifo);
	USARTx->CR3 = (static_cast<uint32_t>(fifo_th_tx) << FIFO_TH_TX_Pos)
		| (static_cast<uint32_t>(fifo_th_rx) << FIFO_TH_RX_Pos);
	if (_TX.IsValid()) {
		_TX.SetUp(PIN::TYPE::AF_PushPull);
		ATOMIC_SET_BIT(USARTx->CR1, USART_CR1_TE);
	}
	if (_RX.IsValid()) {
		_RX.SetUp(PIN::TYPE::AF_PushPull);
		ATOMIC_SET_BIT(USARTx->CR1, USART_CR1_RE);
	}
#if defined(STM32F4)
	(void)USARTx->SR;
	(void)USARTx->DR;
	USARTx->SR = ~USART_SR_TC;
#else
	USARTx->ICR = USART_ICR_IDLECF | USART_ICR_PECF | USART_ICR_FECF
		| USART_ICR_NCF | USART_ICR_ORECF | USART_ICR_TCCF;
	USARTx->RQR = USART_RQR_RXFRQ;
#endif

	const bool irq_enabled = NVIC_GetEnableIRQ(info->irq) != 0;
	NVIC_DisableIRQ(info->irq);
	if (IRQ_Registry::Register(info->irq, this) != SysInitStatus::InitOK) {
		USARTx->CR1 = 0;
		if (irq_enabled) NVIC_EnableIRQ(info->irq);
		return SysInitStatus::InitError;
	}
	_info = info;
	if (SetUpDMA() != SysInitStatus::InitOK) {
		IRQ_Registry::Unregister(info->irq, this);
		USARTx->CR1 = 0;
		_info = nullptr;
		if (irq_enabled) NVIC_EnableIRQ(info->irq);
		return SysInitStatus::InitError;
	}
	rx_status = SysStatus::OK;
	tx_status = SysStatus::OK;
	ATOMIC_SET_BIT(USARTx->CR1, USART_CR1_UE);
	NVIC_EnableIRQ(info->irq);
	return SysInitStatus::InitOK;
}

SysStatus USART::SetBaud(uint32_t baud)
{
	if (!_info) return SysStatus::NotInit;
	const uint32_t brr = BaudRegister(KernelClock(*_info), baud);
	if (!brr) return SysStatus::Error;
	// Caller ensures physical completion via TC; these are software transfer states.
	if (tx_status == SysStatus::Busy || rx_status == SysStatus::Busy)
		return SysStatus::Busy;
	const uint32_t ue = USARTx->CR1 & USART_CR1_UE;
	ATOMIC_CLEAR_BIT(USARTx->CR1, USART_CR1_UE);
	USARTx->BRR = brr;
	// Restore UE only, preserving other control bits changed by an IRQ.
	ATOMIC_MODIFY_REG(USARTx->CR1, USART_CR1_UE, ue);
	BaudRate = baud;
	return SysStatus::OK;
}

SysStatus USART::Send(uint8_t* data, uint32_t len, uint32_t timeout)
{
	if (!_info) return SysStatus::NotInit;
	if (!data || !len || !(USARTx->CR1 & USART_CR1_TE)) return SysStatus::Error;
	if (tx_status == SysStatus::Busy) return SysStatus::Busy;
	const uint32_t start = System::GetTick();
	tx_status = SysStatus::Busy;
	for (uint32_t i = 0; i < len; ++i) {
		while (!(Status_reg() & ISR_TXE)) {
			if (System::GetTick() - start >= timeout) {
				tx_status = SysStatus::Timeout;
				return SysStatus::Timeout;
			}
		}
		TXD() = data[i];
	}
	tx_status = SysStatus::OK; // Queued in UART; the last stop bit may still be on the wire.
	return SysStatus::OK;
}

// Blocking reception deliberately ignores packet-IDLE framing: collect exactly
// len bytes or return an error/timeout. It does not publish asynchronous ready.
SysStatus USART::Receive(uint8_t* data, uint32_t len, uint32_t timeout)
{
	if (!_info) return SysStatus::NotInit;
	if (!data || !len || !(USARTx->CR1 & USART_CR1_RE)) return SysStatus::Error;
	if (rx_status == SysStatus::Busy) return SysStatus::Busy;
	ClearDataReceivedFlag();
	data_overflow = false;
	data_overflow_count = 0;
#if defined(STM32F4)
	(void)USARTx->SR;
	(void)USARTx->DR;
#else
	USARTx->ICR = USART_ICR_IDLECF | USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NCF | USART_ICR_ORECF;
	USARTx->RQR = USART_RQR_RXFRQ;
#endif
	const uint32_t start = System::GetTick();
	rx_status = SysStatus::Busy;
	for (uint32_t i = 0; i < len; ++i) {
		for (;;) {
			const uint32_t sr = Status_reg();
			if (sr & UART_ERRORS) {
#if defined(STM32F4)
				(void)USARTx->DR;
#else
				USARTx->ICR = sr & UART_ERRORS;
#endif
				rx_status = SysStatus::Error;
				OnError(sr & UART_ERRORS);
				return SysStatus::Error;
			}
			if (sr & ISR_RXNE) { data[i] = static_cast<uint8_t>(RXD()); break; }
			if (System::GetTick() - start >= timeout) {
				rx_status = SysStatus::Timeout;
				return SysStatus::Timeout;
			}
		}
	}
	rx_status = SysStatus::OK;
	return SysStatus::OK;
}

SysStatus USART::Send_IRQ(uint8_t* data, uint32_t len)
{
	if (!_info) return SysStatus::NotInit;
	if (!data || !len || len > UINT16_MAX || !(USARTx->CR1 & USART_CR1_TE)) return SysStatus::Error;
	if (tx_status == SysStatus::Busy) return SysStatus::Busy;
	tx_data = { data, static_cast<uint16_t>(len) };
#if defined(STM32F4)
	USARTx->SR = ~USART_SR_TC;
#else
	ClearFlags(ISR_FLAGS::TC);
#endif
	tx_status = SysStatus::Busy;
	__DMB(); // Publish the descriptor before enabling its interrupt source.
#if defined(STM32G0)
	if (USARTx->CR1 & USART_CR1_FIFOEN) IRQ_en(IRQ::TXFIFO, ENABLE);
	else
#endif
		IRQ_en(IRQ::TXE, ENABLE);
	return SysStatus::OK;
}

SysStatus USART::Receive_IRQ(uint8_t* data, uint32_t len)
{
	if (!_info) return SysStatus::NotInit;
	if (!data || !len || len > UINT16_MAX || !(USARTx->CR1 & USART_CR1_RE)) return SysStatus::Error;
	if (rx_status == SysStatus::Busy) return SysStatus::Busy;
	ClearDataReceivedFlag();
	data_overflow = false;
	data_overflow_count = 0;
	_rx_dma_idle = false;
	_rx_errors = 0;
	rx_data = { data, static_cast<uint16_t>(len) };
#if defined(STM32F4)
	(void)USARTx->SR;
	(void)USARTx->DR;
#else
	USARTx->ICR = USART_ICR_IDLECF | USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NCF | USART_ICR_ORECF;
	USARTx->RQR = USART_RQR_RXFRQ;
#endif
	rx_status = SysStatus::Busy;
	__DMB();
	ATOMIC_SET_BIT(USARTx->CR3, USART_CR3_EIE);
	ATOMIC_SET_BIT(USARTx->CR1, USART_CR1_PEIE);
#if defined(STM32G0)
	if (USARTx->CR1 & USART_CR1_FIFOEN) IRQ_en(IRQ::RXFIFO, ENABLE);
	else
#endif
		IRQ_en(IRQ::RXNE, ENABLE);
	IRQ_en(IRQ::IDLE, ENABLE);
	return SysStatus::OK;
}

SysStatus USART::Send_DMA(uint8_t* data, uint32_t len)
{
	if (!_info) return SysStatus::NotInit;
	if (!_dma_tx.HasSelection() || !data || !len || len > UINT16_MAX || !(USARTx->CR1 & USART_CR1_TE)) return SysStatus::Error;
	if (tx_status == SysStatus::Busy) return SysStatus::Busy;
	SysStatus status = _dma_tx.SetMemAddr(reinterpret_cast<uint32_t>(data));
	if (status != SysStatus::OK) return status;
	status = _dma_tx.SetCount(len);
	if (status != SysStatus::OK) return status;
	System::CacheClean(data, len);
	_dma_tx.ClearFlags();
	_dma_tx.Enable_IRQ(DMA_Sx::IRQ::TE);
#if defined(STM32F4) || defined(STM32F7)
	_dma_tx.Enable_IRQ(DMA_Sx::IRQ::DME);
#endif
#if defined(STM32F4)
	USARTx->SR = ~USART_SR_TC;
#else
	ClearFlags(ISR_FLAGS::TC);
#endif
	tx_status = SysStatus::Busy;
	_dma_tx.Stream_EN(ENABLE);
	DMA_en(ENABLE, DMA::TX);
	return SysStatus::OK;
}

SysStatus USART::Receive_DMA(uint8_t* data, uint32_t len)
{
	if (!_info) return SysStatus::NotInit;
	if (!_dma_rx.HasSelection() || !data || !len || len > UINT16_MAX || !(USARTx->CR1 & USART_CR1_RE)) return SysStatus::Error;
	if (rx_status == SysStatus::Busy) return SysStatus::Busy;
	SysStatus status = _dma_rx.SetMemAddr(reinterpret_cast<uint32_t>(data));
	if (status != SysStatus::OK) return status;
	status = _dma_rx.SetCount(len);
	if (status != SysStatus::OK) return status;
	ClearDataReceivedFlag();
	data_overflow = false;
	data_overflow_count = 0;
	_rx_dma_idle = false;
	_rx_errors = 0;
	rx_data = { data, static_cast<uint16_t>(len) }; // Retain original address and capacity for cache/count.
#if defined(STM32F4)
	(void)USARTx->SR;
	(void)USARTx->DR;
#else
	USARTx->ICR = USART_ICR_IDLECF | USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NCF | USART_ICR_ORECF;
	USARTx->RQR = USART_RQR_RXFRQ;
#endif
	System::CacheCleanInvalidate(data, len);
	_dma_rx.ClearFlags();
	_dma_rx.Enable_IRQ(DMA_Sx::IRQ::TE);
#if defined(STM32F4) || defined(STM32F7)
	_dma_rx.Enable_IRQ(DMA_Sx::IRQ::DME);
#endif
	rx_status = SysStatus::Busy;
	ATOMIC_SET_BIT(USARTx->CR1, USART_CR1_PEIE);
	ATOMIC_SET_BIT(USARTx->CR3, USART_CR3_EIE);
	_dma_rx.Stream_EN(ENABLE);
	DMA_en(ENABLE, DMA::RX);
	IRQ_en(IRQ::IDLE, ENABLE);
	return SysStatus::OK;
}

// Acquire internally stored DMA and register each direction once.
// Rollback releases only resources successfully acquired by this SetUp().
SysInitStatus USART::SetUpDMA()
{
	DMA_Sx* const tx = _dma_tx.HasSelection() ? &_dma_tx : nullptr;
	DMA_Sx* const rx = _dma_rx.HasSelection() ? &_dma_rx : nullptr;
	if (!tx && !rx) return SysInitStatus::InitOK;
	// Route ownership/direction was checked by the constructor.

	DMA_Sx::StreamSettings cfg;
	cfg.data_size = DMA_Sx::SIZE::Byte;
	cfg.minc = true;
	if (tx) {
		cfg.direction = DMA_Sx::DIR::To_Per;
		cfg.per_address = reinterpret_cast<uint32_t>(&TXD());
		if (tx->SetUp(cfg) != SysInitStatus::InitOK) return SysInitStatus::InitError;
	}
	if (rx) {
		cfg.direction = DMA_Sx::DIR::From_Per;
		cfg.per_address = reinterpret_cast<uint32_t>(&RXD());
		if (rx->SetUp(cfg) != SysInitStatus::InitOK) {
			if (tx) tx->Release(); // Only the resource successfully acquired above.
			return SysInitStatus::InitError;
		}
	}

	const IRQn_Type no_irq = static_cast<IRQn_Type>(-1);
	const IRQn_Type irqs[2] = { tx ? tx->GetIRQn() : no_irq, rx ? rx->GetIRQn() : no_irq };
	IIRQHandler* const handlers[2] = { &_dma_tx_irq, &_dma_rx_irq };
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
	DMA_Sx* channels[2] = { tx, rx };
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

// Only USART sources are examined here. F4/F7 DMA RX stop completes in its own
// TC interrupt, including an IDLE/error stop before the configured capacity.
void USART::HandleIRQ()
{
	if (!_info) return;
	const uint32_t sr = Status_reg();
	const uint32_t cr1 = USARTx->CR1;
	const uint32_t cr3 = USARTx->CR3;
	if (rx_status == SysStatus::Busy) {
		const uint32_t enabled_errors = ((cr1 & USART_CR1_PEIE) ? static_cast<uint32_t>(Error::Parity) : 0U)
			| ((cr3 & USART_CR3_EIE) ? (UART_ERRORS & ~static_cast<uint32_t>(Error::Parity)) : 0U);
		const uint32_t errors = sr & enabled_errors;
		if (errors) {
			ATOMIC_CLEAR_BIT(USARTx->CR1, CR1_RX_IRQ);
			ATOMIC_CLEAR_BIT(USARTx->CR3, CR3_RX_IRQ);
			if (_dma_rx.HasSelection() && ((cr3 & USART_CR3_DMAR) || _rx_dma_idle)) {
				_rx_errors = _rx_errors | errors;
				_rx_dma_idle = true;
				DMA_en(DISABLE, DMA::RX);
#if defined(STM32G0)
				USART::OnIdle();
#else
				_dma_rx.RequestStop();
#endif
				return;
			}
#if defined(STM32F4)
			(void)USARTx->SR;
			(void)USARTx->DR;
#else
			USARTx->ICR = errors & UART_ERRORS;
#endif
			data_received = false;
			data_received_count = 0;
			rx_status = SysStatus::Error;
			OnError(errors);
			return;
		}
	}

	bool rx_irq = (cr1 & CR1_RXNEIE) && (sr & ISR_RXNE);
	bool tx_irq = (cr1 & CR1_TXEIE) && (sr & ISR_TXE);
	const bool idle_irq = (sr & ISR_IDLE) && (cr1 & USART_CR1_IDLEIE);
#if defined(STM32G0)
	rx_irq = rx_irq || ((cr3 & USART_CR3_RXFTIE) && (sr & ISR_RXFT));
	tx_irq = tx_irq || ((cr3 & USART_CR3_TXFTIE) && (sr & ISR_TXFT));
#endif
	// Never read RDR in CPU while DMA owns the RX path.
	// IDLE owns its FIFO tail; do not spend a second RX budget in the same entry.
	if (rx_irq && !idle_irq && rx_status == SysStatus::Busy && !(cr3 & USART_CR3_DMAR)) {
#if defined(STM32G0)
		for (uint32_t n = FifoDepth(); n && (Status_reg() & ISR_RXNE)
			&& rx_status == SysStatus::Busy; --n)
			OnRxByte(static_cast<uint8_t>(RXD()));
#else
		// RXNE is already present in sr; these UARTs have one receive register.
		OnRxByte(static_cast<uint8_t>(RXD()));
#endif
	}
	if (idle_irq) {
#if defined(STM32F4) || defined(STM32F7)
		// The ordinary RX callback was skipped for IDLE, so cr3 is still usable.
		if (rx_status == SysStatus::Busy && (cr3 & USART_CR3_DMAR)) {
			_rx_dma_idle = true;
			ATOMIC_CLEAR_BIT(USARTx->CR1, CR1_RX_IRQ);
			ATOMIC_CLEAR_BIT(USARTx->CR3, CR3_RX_IRQ);
			DMA_en(DISABLE, DMA::RX);
			_dma_rx.RequestStop();
		} else
#endif
			OnIdle();
	}
	if (tx_irq && tx_status == SysStatus::Busy && !(cr3 & USART_CR3_DMAT)) OnTxEmpty();
	// Callbacks may enable TCIE, and sending a byte clears TC. Re-read both.
	if ((USARTx->CR1 & USART_CR1_TCIE) && (Status_reg() & ISR_TC)) {
#if defined(STM32F4)
		USARTx->SR = ~USART_SR_TC;
#else
		ClearFlags(ISR_FLAGS::TC);
#endif
		OnTC();
	}
}

void USART::HandleDmaTxIRQ()
{
	if (!_info || tx_status != SysStatus::Busy) return;
	const uint32_t errors = _dma_tx.GetErrorFlags();
	if (errors) {
		DMA_en(DISABLE, DMA::TX);
		_dma_tx.RequestStop();
		_dma_tx.Disable_IRQ(DMA_Sx::IRQ::TE);
#if defined(STM32F4) || defined(STM32F7)
		_dma_tx.Disable_IRQ(DMA_Sx::IRQ::DME);
		// An enabled stream finishes its stop with TC. TE may already have
		// disabled it in hardware. Do not spin on EN from the error interrupt.
		if (_dma_tx.IsEnabled()) return;
#endif
		_dma_tx.ClearFlags();
		__DMB();
		tx_status = SysStatus::Error;
		OnError(static_cast<uint32_t>(Error::DmaTx));
		return;
	}
	if (_dma_tx.GetTC_Flag()) OnDmaTxComplete();
}

void USART::HandleDmaRxIRQ()
{
	if (!_info || rx_status != SysStatus::Busy) return;
	const uint32_t errors = _dma_rx.GetErrorFlags();
	if (errors) {
		_rx_errors = _rx_errors | static_cast<uint32_t>(Error::DmaRx);
		_rx_dma_idle = true;
		ATOMIC_CLEAR_BIT(USARTx->CR1, CR1_RX_IRQ);
		ATOMIC_CLEAR_BIT(USARTx->CR3, CR3_RX_IRQ);
		DMA_en(DISABLE, DMA::RX);
		_dma_rx.Disable_IRQ(DMA_Sx::IRQ::TE);
#if defined(STM32F4) || defined(STM32F7)
		_dma_rx.RequestStop();
		_dma_rx.Disable_IRQ(DMA_Sx::IRQ::DME);
		if (_dma_rx.IsEnabled()) return;
#endif
		USART::OnIdle(); // Error cleanup only; do not notify a successful IDLE.
		return;
	}
	if (!_dma_rx.GetTC_Flag()) return;
	if (_rx_errors) USART::OnIdle();
	else if (_rx_dma_idle) OnIdle();
	else OnDmaRxComplete();
}

void USART::OnTxEmpty()
{
	for (uint32_t n = FifoDepth(); n && tx_data.size && (Status_reg() & ISR_TXE); --n) {
		TXD() = *tx_data.data_ptr++;
		--tx_data.size;
	}
	if (!tx_data.size) {
		ATOMIC_CLEAR_BIT(USARTx->CR1, CR1_TXEIE);
		ATOMIC_CLEAR_BIT(USARTx->CR3, CR3_TX_IRQ);
		tx_status = SysStatus::OK;
	}
}

void USART::OnRxByte(uint8_t byte)
{
	if (!rx_data.size) {
		data_overflow = true;
		data_overflow_count = data_overflow_count + 1;
		return;
	}
	*rx_data.data_ptr++ = byte;
	--rx_data.size;
	data_received_count = data_received_count + 1;
	if (!rx_data.size) {
		ATOMIC_CLEAR_BIT(USARTx->CR1, CR1_RX_IRQ);
		ATOMIC_CLEAR_BIT(USARTx->CR3, CR3_RX_IRQ);
		rx_status = SysStatus::OK;
		__DMB();
		data_received = true;
	}
}

void USART::OnIdle()
{
	const bool receiving = rx_status == SysStatus::Busy;
	const bool dma = receiving && _dma_rx.HasSelection() && (_rx_dma_idle || (USARTx->CR3 & USART_CR3_DMAR));
	uint32_t errors = _rx_errors;
	ATOMIC_CLEAR_BIT(USARTx->CR1, CR1_RX_IRQ);
	ATOMIC_CLEAR_BIT(USARTx->CR3, CR3_RX_IRQ);
	if (dma) {
		DMA_en(DISABLE, DMA::RX);
#if defined(STM32G0)
		// IDLE assumes that the last DMA request has already been serviced.
		// G0 abort does not produce the F4/F7 software-stop TC event.
		_dma_rx.RequestStop();
		__DMB();
		if (_dma_rx.GetErrorFlags()) errors |= static_cast<uint32_t>(Error::DmaRx);
		errors |= Status_reg() & UART_ERRORS;
#else
		// Called only after DMA TC (normal completion or software stop), or TE
		// with the stream already disabled. No wait or second stop request.
		__DMB();
#endif
		_dma_rx.ClearFlags();
#if defined(STM32G0)
		// An abort during an active transfer may leave CNDTR inaccurate. Failed
		// packets are discarded without deriving a length from that counter.
		if (!errors) data_received_count = rx_data.size - _dma_rx.GetCount();
#else
		data_received_count = rx_data.size - _dma_rx.GetCount();
		System::CacheInvalidate(rx_data.data_ptr, data_received_count);
#endif
	}
#if defined(STM32F4)
	// Stop DMA before the SR/DR clear sequence; retain a byte not yet fetched by DMA.
	const uint32_t sr = USARTx->SR;
	const uint8_t byte = static_cast<uint8_t>(USARTx->DR);
	errors |= sr & UART_ERRORS;
	if (receiving && (sr & ISR_RXNE)) {
		if (dma && data_received_count < rx_data.size) {
			rx_data.data_ptr[data_received_count] = byte;
			data_received_count = data_received_count + 1;
		} else if (!dma) OnRxByte(byte);
		else { data_overflow = true; data_overflow_count = data_overflow_count + 1; }
	}
#else
	ClearFlags(ISR_FLAGS::IDLE);
	// FIFO may contain a short tail below RXFT. DMA is stopped before CPU access.
	for (uint32_t n = FifoDepth(); n && receiving && !errors && (Status_reg() & ISR_RXNE); --n) {
		if (dma) {
			if (data_received_count == rx_data.size) break;
			rx_data.data_ptr[data_received_count] = static_cast<uint8_t>(RXD());
			data_received_count = data_received_count + 1;
		} else {
			if (!rx_data.size) break;
			OnRxByte(static_cast<uint8_t>(RXD()));
		}
	}
	errors |= Status_reg() & UART_ERRORS;
	USARTx->ICR = errors & UART_ERRORS;
#endif
	if (receiving) {
		_rx_dma_idle = false;
		_rx_errors = 0;
		if (errors) {
			data_received = false;
			data_received_count = 0;
			rx_status = SysStatus::Error;
			OnError(errors);
			return;
		}
		rx_status = SysStatus::OK;
		__DMB();
		data_received = true; // Publish only after DMA stop, cache and the final byte.
	}
}

void USART::OnTC() {}

void USART::OnDmaTxComplete()
{
	DMA_en(DISABLE, DMA::TX);
	_dma_tx.RequestStop(); // TC already confirms completion; G0 still needs EN cleared.
	_dma_tx.ClearFlags();
	__DMB();
	tx_status = SysStatus::OK;
}

void USART::OnDmaRxComplete()
{
	// Full capacity and IDLE use the same stop/count/cache/buffer handover.
	USART::OnIdle(); // Qualified call: do not invoke an overridden IDLE callback.
}

void USART::OnError(uint32_t) {}

// Only an active DMA receive needs cache handover on cancellation. Invalidating
// an already completed buffer here could discard the application's later writes.
// Remove only this object from each unique vector before releasing DMA ownership.
void USART::DeInit()
{
	if (!_info) return;
	const bool receiving_dma = _dma_rx.HasSelection() && (_rx_dma_idle || (USARTx->CR3 & USART_CR3_DMAR));
	USARTx->CR1 = 0;
	USARTx->CR3 = 0; // Disable sources and UART requests before stopping DMA.
	if (_dma_tx.HasSelection()) { _dma_tx.Stream_EN(DISABLE); _dma_tx.ClearFlags(); }
	if (_dma_rx.HasSelection()) { _dma_rx.Stream_EN(DISABLE); _dma_rx.ClearFlags(); }

	const IRQn_Type no_irq = static_cast<IRQn_Type>(-1);
	const IRQn_Type irqs[3] = {
		_info->irq, _dma_tx.HasSelection() ? _dma_tx.GetIRQn() : no_irq, _dma_rx.HasSelection() ? _dma_rx.GetIRQn() : no_irq
	};
	IIRQHandler* const handlers[3] = { this, &_dma_tx_irq, &_dma_rx_irq };
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
	if (_dma_rx.HasSelection()) {
		if (receiving_dma)
			System::CacheInvalidate(rx_data.data_ptr, rx_data.size - _dma_rx.GetCount());
		_dma_rx.Release();
	}
	USARTx->CR2 = 0;
	tx_data = {};
	rx_data = {};
	ClearDataReceivedFlag();
	data_overflow = false;
	data_overflow_count = 0;
	_rx_dma_idle = false;
	_rx_errors = 0;
	tx_status = SysStatus::NotInit;
	rx_status = SysStatus::NotInit;
	_info = nullptr;
}
