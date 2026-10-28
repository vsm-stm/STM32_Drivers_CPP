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

// OVER8=0: 16-bit BRR, USARTDIV >= 16. G0 PRESC is /1.
static uint32_t BaudRegister(uint32_t clock, uint32_t baud)
{
	if (!baud || baud > clock / 16U) return 0;
	const uint32_t brr = (clock + baud / 2U) / baud;
	constexpr uint32_t max_brr = 0xFFFFU;
	return brr >= 16U && brr <= max_brr ? brr : 0;
}

uint32_t USART::KernelClock(const Instance& info) const
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
SysInitStatus USART::SetUp(FIFO fifo, FIFO_TH fifo_th_tx, FIFO_TH fifo_th_rx, FrameFormat format)
{
	if (USARTx) return SysInitStatus::InitError;
	if (format != FrameFormat::N8_1 && format != FrameFormat::E8_1
		&& format != FrameFormat::O8_1 && format != FrameFormat::N8_2)
		return SysInitStatus::InitError;
	const uint32_t brr = BaudRegister(KernelClock(_instance), BaudRate);
	if (!brr) return SysInitStatus::InitError;
#if defined(STM32G0)
	if ((fifo != FIFO::NO && fifo != FIFO::EN)
		|| static_cast<uint32_t>(fifo_th_tx) > static_cast<uint32_t>(FIFO_TH::all)
		|| static_cast<uint32_t>(fifo_th_rx) > static_cast<uint32_t>(FIFO_TH::all)
		|| (fifo == FIFO::EN && !IS_UART_FIFO_INSTANCE(reinterpret_cast<USART_TypeDef*>(_instance.periph)))
		|| (fifo == FIFO::NO && (fifo_th_tx != FIFO_TH::NO || fifo_th_rx != FIFO_TH::NO)))
		return SysInitStatus::InitError;
#endif

	USARTx = reinterpret_cast<USART_TypeDef*>(_instance.periph);
	UART_SET_BIT(*reinterpret_cast<volatile uint32_t*>(_instance.clk_reg), _instance.clk_bit);
	(void)*reinterpret_cast<volatile uint32_t*>(_instance.clk_reg);
	USARTx->CR1 = 0; // BRR and FIFO configuration require UE=0.
#if defined(STM32F7) || defined(STM32G0)
	if (_instance.clock_select) {
		// Locate this instance's two-bit USARTxSEL field; the mask is nonzero here.
		const uint32_t selection = static_cast<uint32_t>(_clock_source) << __builtin_ctz(_instance.clock_select);
#if defined(STM32F7)
		UART_MODIFY_REG(RCC->DCKCFGR2, _instance.clock_select, selection);
#else
		UART_MODIFY_REG(RCC->CCIPR, _instance.clock_select, selection);
#endif
	}
#endif
#if defined(STM32G0)
	USARTx->PRESC = 0;
#endif
	USARTx->BRR = brr;
	USARTx->CR2 = format == FrameFormat::N8_2 ? USART_CR2_STOP_1 : 0u;
	uint32_t cr1 = static_cast<uint32_t>(fifo);
	if (format == FrameFormat::E8_1 || format == FrameFormat::O8_1) {
#if defined(USART_CR1_M0)
		cr1 |= USART_CR1_M0 | USART_CR1_PCE;
#else
		cr1 |= USART_CR1_M | USART_CR1_PCE;
#endif
		if (format == FrameFormat::O8_1) cr1 |= USART_CR1_PS;
	}
	USARTx->CR1 = cr1;
	USARTx->CR3 = (static_cast<uint32_t>(fifo_th_tx) << FIFO_TH_TX_Pos)
		| (static_cast<uint32_t>(fifo_th_rx) << FIFO_TH_RX_Pos);
	if (_TX.IsValid()) {
		_TX.SetUp(PIN::TYPE::AF_PushPull);
		UART_SET_BIT(USARTx->CR1, USART_CR1_TE);
	}
	if (_RX.IsValid()) {
		_RX.SetUp(PIN::TYPE::AF_PushPull);
		UART_SET_BIT(USARTx->CR1, USART_CR1_RE);
	}
#if defined(STM32F4)
	(void)USARTx->SR;
	(void)USARTx->DR;
	USARTx->SR = ~USART_SR_TC;
#else
	USARTx->ICR = USART_ICR_IDLECF | USART_ICR_PECF | USART_ICR_FECF
		| static_cast<uint32_t>(ISR_FLAGS::NE) | USART_ICR_ORECF | USART_ICR_TCCF;
	USARTx->RQR = USART_RQR_RXFRQ;
#endif

	const bool irq_enabled = NVIC_GetEnableIRQ(_instance.irq) != 0;
	NVIC_DisableIRQ(_instance.irq);
	if (IRQ_Registry::Register(_instance.irq, this) != SysInitStatus::InitOK) {
		USARTx->CR1 = 0;
		USARTx = nullptr;
		if (irq_enabled) NVIC_EnableIRQ(_instance.irq);
		return SysInitStatus::InitError;
	}
	if (SetUpDMA() != SysInitStatus::InitOK) {
		IRQ_Registry::Unregister(_instance.irq, this);
		USARTx->CR1 = 0;
		USARTx = nullptr;
		if (irq_enabled) NVIC_EnableIRQ(_instance.irq);
		return SysInitStatus::InitError;
	}
	rx_status = SysStatus::OK;
	tx_status = SysStatus::OK;
	UART_SET_BIT(USARTx->CR1, USART_CR1_UE);
	NVIC_EnableIRQ(_instance.irq);
	return SysInitStatus::InitOK;
}

SysStatus USART::SetBaud(uint32_t baud)
{
	if (!USARTx) return SysStatus::NotInit;
	// Caller ensures physical completion via TC; these are software transfer states.
	if (tx_status == SysStatus::Busy || rx_status == SysStatus::Busy)
		return SysStatus::Busy;
	const uint32_t brr = BaudRegister(KernelClock(_instance), baud);
	if (!brr) return SysStatus::Error;
	const uint32_t ue = USARTx->CR1 & USART_CR1_UE;
	UART_CLEAR_BIT(USARTx->CR1, USART_CR1_UE);
	USARTx->BRR = brr;
	// Restore UE only, preserving other control bits changed by an IRQ.
	UART_MODIFY_REG(USARTx->CR1, USART_CR1_UE, ue);
	BaudRate = baud;
	return SysStatus::OK;
}

SysStatus USART::Send(uint8_t* data, uint32_t len, uint32_t timeout)
{
	const SysStatus state = tx_status;
	if (state == SysStatus::NotInit || state == SysStatus::Busy) return state;
	if (!data || !len) return SysStatus::Error;
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
	const SysStatus state = rx_status;
	if (state == SysStatus::NotInit || state == SysStatus::Busy) return state;
	if (!data || !len) return SysStatus::Error;
	ClearDataReceivedFlag();
	data_overflow_count = 0;
	ClearRxFlags();
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
	const SysStatus state = tx_status;
	if (state == SysStatus::NotInit || state == SysStatus::Busy) return state;
	if (!data || !len || len > UINT16_MAX) return SysStatus::Error;
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
	const SysStatus state = rx_status;
	if (state == SysStatus::NotInit || state == SysStatus::Busy) return state;
	if (!data || !len || len > UINT16_MAX) return SysStatus::Error;
	PrepareRx(data, len);
	rx_status = SysStatus::Busy;
	__DMB();
	uint32_t cr1_rx = USART_CR1_PEIE | USART_CR1_IDLEIE;
	uint32_t cr3_rx = USART_CR3_EIE;
#if defined(STM32G0)
	if (USARTx->CR1 & USART_CR1_FIFOEN) cr3_rx |= USART_CR3_RXFTIE;
	else
#endif
		cr1_rx |= CR1_RXNEIE;
	UART_SET_BIT(USARTx->CR3, cr3_rx);
	UART_SET_BIT(USARTx->CR1, cr1_rx);
	return SysStatus::OK;
}

SysStatus USART::Send_DMA(uint8_t* data, uint32_t len)
{
	const SysStatus state = tx_status;
	if (state == SysStatus::NotInit || state == SysStatus::Busy) return state;
	if (!_dma_tx.HasSelection() || !data || !len || len > UINT16_MAX) return SysStatus::Error;
	_dma_tx.PrepareTransfer(reinterpret_cast<uint32_t>(data), len, ENABLE, DMA_ERROR_IRQ_MASK);
#if defined(STM32F7)
	System::CacheClean(data, len);
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
	const SysStatus state = rx_status;
	if (state == SysStatus::NotInit || state == SysStatus::Busy) return state;
	if (!_dma_rx.HasSelection() || !data || !len || len > UINT16_MAX) return SysStatus::Error;
	_dma_rx.PrepareTransfer(reinterpret_cast<uint32_t>(data), len, ENABLE, DMA_ERROR_IRQ_MASK);
	PrepareRx(data, len);
#if defined(STM32F7)
	System::CacheCleanInvalidate(data, len);
#endif
	rx_status = SysStatus::Busy;
	_dma_rx.Stream_EN(ENABLE);
	UART_SET_BIT(USARTx->CR3, USART_CR3_EIE | USART_CR3_DMAR);
	UART_SET_BIT(USARTx->CR1, USART_CR1_PEIE | USART_CR1_IDLEIE);
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
		dma->Enable_IRQ(static_cast<DMA_Sx::IRQ>(static_cast<uint32_t>(DMA_Sx::IRQ::TC) | DMA_ERROR_IRQ_MASK));
	}
	return SysInitStatus::InitOK;
}

// USART, TX DMA and RX DMA keep separate entry points. No IRQ waits for hardware.
void USART::HandleIRQ()
{
	if (!USARTx) return;
	const uint32_t sr = Status_reg();
	const uint32_t cr1 = USARTx->CR1;
	const uint32_t cr3 = USARTx->CR3;
	if (rx_status == SysStatus::Busy) {
		const uint32_t enabled_errors = ((cr1 & USART_CR1_PEIE) ? static_cast<uint32_t>(Error::Parity) : 0U)
			| ((cr3 & USART_CR3_EIE) ? (UART_ERRORS & ~static_cast<uint32_t>(Error::Parity)) : 0U);
		const uint32_t errors = sr & enabled_errors;
		if (errors) {
			if ((cr3 & USART_CR3_DMAR) || _rx_dma_idle) {
				_rx_errors = _rx_errors | errors;
				if (!_rx_dma_idle) {
					StopRx();
					_rx_dma_idle = true;
					_dma_rx.RequestStop();
				}
#if defined(STM32G0)
				if (_dma_rx.GetIRQFlags() & DMA_Sx::FLAG_ERRORS)
					_rx_errors = _rx_errors | static_cast<uint32_t>(Error::DmaRx);
				FinishRxDMA(); // Error packets are discarded; never notify IDLE.
#endif
			} else {
				StopRx();
#if defined(STM32F4)
				(void)USARTx->DR; // Entry SR plus DR acknowledges the receive errors.
#else
				USARTx->ICR = errors;
#endif
				FinishRx(errors);
			}
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
	// IDLE has its own bounded tail read. CPU must not consume DMA-owned RDR.
	if (rx_irq && !idle_irq && rx_status == SysStatus::Busy && !(cr3 & USART_CR3_DMAR)) {
#if defined(STM32G0)
		uint32_t rx_sr = sr;
		for (uint32_t n = FifoDepth(); n && rx_data.size && (rx_sr & ISR_RXNE); --n) {
			OnRxByte(static_cast<uint8_t>(RXD()));
			if (n > 1U && rx_data.size) rx_sr = Status_reg();
		}
#else
		OnRxByte(static_cast<uint8_t>(RXD()));
#endif
		if (!rx_data.size) {
			StopRx();
			FinishRx(0);
		}
	}
	if (idle_irq) {
		if (rx_status == SysStatus::Busy) {
			StopRx();
			if (cr3 & USART_CR3_DMAR) {
				_rx_dma_idle = true;
				_dma_rx.RequestStop();
#if defined(STM32G0)
				// Last request must already be serviced; G0 has no software-stop TC.
				if (_dma_rx.GetIRQFlags() & DMA_Sx::FLAG_ERRORS)
					_rx_errors = static_cast<uint32_t>(Error::DmaRx);
				if (FinishRxDMA()) OnIdle();
#endif
			} else if (FinishRxIRQ()) OnIdle();
		} else {
#if defined(STM32F4)
			(void)USARTx->DR; // Entry SR plus DR clears an inactive IDLE.
#else
			ClearFlags(ISR_FLAGS::IDLE);
#endif
		}
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
	if (tx_status != SysStatus::Busy) return;
	const uint32_t flags = _dma_tx.GetIRQFlags();
	const uint32_t errors = flags & DMA_Sx::FLAG_ERRORS;
	if (errors) {
		DMA_en(DISABLE, DMA::TX);
		_dma_tx.RequestStop();
		_dma_tx.Disable_IRQ(static_cast<DMA_Sx::IRQ>(DMA_ERROR_IRQ_MASK));
#if defined(STM32F4) || defined(STM32F7)
		// TC confirms a software stop; TE can already have disabled the stream.
		if (!(flags & DMA_Sx::FLAG_TC)
			&& !((errors & DMA_LISR_TEIF0) && !_dma_tx.IsEnabled())) return;
#endif
		_dma_tx.ClearFlags();
		__DMB();
		tx_status = SysStatus::Error;
		OnError(static_cast<uint32_t>(Error::DmaTx));
		return;
	}
	if (!(flags & DMA_Sx::FLAG_TC)) return;
	DMA_en(DISABLE, DMA::TX);
#if defined(STM32G0)
	_dma_tx.RequestStop(); // G0 needs EN cleared after normal completion.
#endif
	_dma_tx.ClearFlags();
	__DMB();
	tx_status = SysStatus::OK;
	OnDmaTxComplete();
}

void USART::HandleDmaRxIRQ()
{
	if (rx_status != SysStatus::Busy) return;
	const uint32_t flags = _dma_rx.GetIRQFlags();
	const uint32_t errors = flags & DMA_Sx::FLAG_ERRORS;
	if (errors) {
		_rx_errors = _rx_errors | static_cast<uint32_t>(Error::DmaRx);
		_dma_rx.Disable_IRQ(static_cast<DMA_Sx::IRQ>(DMA_ERROR_IRQ_MASK));
		if (!_rx_dma_idle) {
			StopRx();
			_rx_dma_idle = true;
			_dma_rx.RequestStop();
		}
#if defined(STM32F4) || defined(STM32F7)
		if (!(flags & DMA_Sx::FLAG_TC)
			&& !((errors & DMA_LISR_TEIF0) && !_dma_rx.IsEnabled())) return;
#endif
	} else if (!(flags & DMA_Sx::FLAG_TC)) return;

	const bool idle = _rx_dma_idle;
	if (!idle) {
		StopRx(); // Capacity completion has not gone through USART IDLE.
#if defined(STM32G0)
		_dma_rx.RequestStop();
#endif
	}
	if (FinishRxDMA()) {
		if (idle) OnIdle();
		else OnDmaRxComplete();
	}
}

void USART::OnTxEmpty()
{
#if defined(STM32G0)
	for (uint32_t n = FifoDepth(); n && tx_data.size && (Status_reg() & ISR_TXE); --n) {
		TXD() = *tx_data.data_ptr++;
		--tx_data.size;
	}
#else
	// HandleIRQ already confirmed TXE; F4/F7 have one transmit data register.
	TXD() = *tx_data.data_ptr++;
	--tx_data.size;
#endif
	if (!tx_data.size) {
		UART_CLEAR_BIT(USARTx->CR1, CR1_TXEIE);
#if defined(STM32G0)
		UART_CLEAR_BIT(USARTx->CR3, CR3_TX_IRQ);
#endif
		tx_status = SysStatus::OK;
	}
}

void USART::OnRxByte(uint8_t byte)
{
	if (!rx_data.size) {
		data_overflow_count = data_overflow_count + 1;
		return;
	}
	*rx_data.data_ptr++ = byte;
	--rx_data.size;
	data_received_count = data_received_count + 1;
}

bool USART::FinishRx(uint32_t errors)
{
	if (errors) {
		data_received_count = 0;
		data_received = false;
		rx_status = SysStatus::Error;
		OnError(errors);
		return false;
	}
	rx_status = SysStatus::OK;
	__DMB();
	data_received = true;
	return true;
}

// Called after StopRx; IDLE completion owns the only RX tail budget in this IRQ.
bool USART::FinishRxIRQ()
{
	uint32_t sr = Status_reg();
	uint32_t errors = sr & UART_ERRORS;
#if defined(STM32F4)
	const uint8_t byte = static_cast<uint8_t>(USARTx->DR); // SR/DR clears IDLE/errors.
	if (!errors && (sr & ISR_RXNE)) OnRxByte(byte);
#else
	ClearFlags(ISR_FLAGS::IDLE);
	for (uint32_t n = FifoDepth(); n && rx_data.size && !errors && (sr & ISR_RXNE); --n) {
		OnRxByte(static_cast<uint8_t>(RXD()));
		sr = Status_reg(); // Acknowledge new errors after the actual tail read.
		errors |= sr & UART_ERRORS;
	}
	if (errors) USARTx->ICR = errors;
#endif
	return FinishRx(errors);
}

// Only called after DMA completion/confirmed F4/F7 stop, or the agreed G0 stop.
// No mode detection or repeat UART/DMA shutdown belongs to this handover.
bool USART::FinishRxDMA()
{
	__DMB();
	uint32_t errors = _rx_errors;
#if defined(STM32G0)
	uint32_t sr = Status_reg();
	errors |= sr & UART_ERRORS; // An error packet must not use an abort count.
#endif
	uint32_t count = 0;
	if (!errors) {
		count = _rx_dma_idle ? rx_data.size - _dma_rx.GetCount() : rx_data.size;
#if defined(STM32F7)
		System::CacheInvalidate(rx_data.data_ptr, count); // Before CPU tail stores.
#endif
	}
	_dma_rx.ClearFlags();
#if !defined(STM32G0)
	uint32_t sr = Status_reg(); // Fresh status after DMA/cache ownership handover.
	errors |= sr & UART_ERRORS;
#endif
#if defined(STM32F4)
	const uint8_t byte = static_cast<uint8_t>(USARTx->DR);
	if (!errors && (sr & ISR_RXNE)) {
		if (count < rx_data.size) rx_data.data_ptr[count++] = byte;
		else data_overflow_count = data_overflow_count + 1;
	}
#else
	ClearFlags(ISR_FLAGS::IDLE);
	for (uint32_t n = FifoDepth(); n && count < rx_data.size && !errors && (sr & ISR_RXNE); --n) {
		rx_data.data_ptr[count++] = static_cast<uint8_t>(RXD());
		sr = Status_reg();
		errors |= sr & UART_ERRORS;
	}
	if (errors) USARTx->ICR = errors;
#endif
	if (!errors) data_received_count = count;
	_rx_dma_idle = false;
	_rx_errors = 0;
	return FinishRx(errors);
}

// Completion callbacks only notify: state and buffer ownership are already final.
void USART::OnIdle() {}
void USART::OnTC() {}
void USART::OnDmaTxComplete() {}
void USART::OnDmaRxComplete() {}
void USART::OnError(uint32_t) {}

// Only an active DMA receive needs cache handover on cancellation. Invalidating
// an already completed buffer here could discard the application's later writes.
// Remove only this object from each unique vector before releasing DMA ownership.
void USART::DeInit()
{
	if (!USARTx) return;
#if defined(STM32F7)
	const bool receiving_dma = _dma_rx.HasSelection() && (_rx_dma_idle || (USARTx->CR3 & USART_CR3_DMAR));
#endif
	USARTx->CR1 = 0;
	USARTx->CR3 = 0; // Disable sources and UART requests before stopping DMA.
	if (_dma_tx.HasSelection()) { _dma_tx.Stream_EN(DISABLE); _dma_tx.ClearFlags(); }
	if (_dma_rx.HasSelection()) { _dma_rx.Stream_EN(DISABLE); _dma_rx.ClearFlags(); }

	const IRQn_Type no_irq = static_cast<IRQn_Type>(-1);
	const IRQn_Type irqs[3] = {
		_instance.irq, _dma_tx.HasSelection() ? _dma_tx.GetIRQn() : no_irq, _dma_rx.HasSelection() ? _dma_rx.GetIRQn() : no_irq
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
#if defined(STM32F7)
		if (receiving_dma)
			System::CacheInvalidate(rx_data.data_ptr, rx_data.size - _dma_rx.GetCount());
#endif
		_dma_rx.Release();
	}
	USARTx->CR2 = 0;
	tx_data = {};
	rx_data = {};
	ClearDataReceivedFlag();
	data_overflow_count = 0;
	_rx_dma_idle = false;
	_rx_errors = 0;
	tx_status = SysStatus::NotInit;
	rx_status = SysStatus::NotInit;
	USARTx = nullptr;
}
