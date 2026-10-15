/**
 * @file uart.cpp
 * @brief UART setup and blocking/IRQ/DMA transfer implementation.
 *
 * uart.hpp contains the public contracts and complete usage examples.
 * IRQ dispatch tests flag AND source-enable/state; an attached DMA pointer alone
 * does not mean the current operation uses DMA. Request gates identify that path.
 * Packet handover stops DMA before count/cache/CPU access. Registry mutations
 * belong only to main-loop setup/attachment/deinitialization, never completion.
 */
#include "uart.hpp"

#define UART_DEFS_CPP
#include "uart_defs.hpp"
#undef UART_DEFS_CPP

// OVER8=0: 16-bit BRR, USARTDIV >= 16. Kernel clock is PCLK, PRESC is /1.
static uint32_t BaudRegister(uint32_t clock, uint32_t baud)
{
	if (!baud || baud > clock / 16U) return 0;
	const uint32_t brr = (clock + baud / 2U) / baud;
	constexpr uint32_t max_brr = 0xFFFFU;
	return brr >= 16U && brr <= max_brr ? brr : 0;
}

// Select PCLK and program BRR with UE=0. Register the object once even if another
// peripheral already enabled the shared NVIC line. FIFO support is per instance.
SysInitStatus USART::SetUp(FIFO fifo, FIFO_TH fifo_th_tx, FIFO_TH fifo_th_rx)
{
	if (_info) return SysInitStatus::InitError;
	const PeriphInfo* info = nullptr;
	for (const auto& e : usart_table)
		if (e.periph == USARTx) { info = &e; break; }
	if (!info) return SysInitStatus::InitError;
	const uint32_t brr = BaudRegister(*info->bus_clk, BaudRate);
	if (!brr) return SysInitStatus::InitError;
#if defined(STM32G0)
	if ((fifo != FIFO::NO && fifo != FIFO::EN)
		|| static_cast<uint32_t>(fifo_th_tx) > static_cast<uint32_t>(FIFO_TH::all)
		|| static_cast<uint32_t>(fifo_th_rx) > static_cast<uint32_t>(FIFO_TH::all)
		|| (fifo == FIFO::EN && !IS_UART_FIFO_INSTANCE(USARTx))
		|| (fifo == FIFO::NO && (fifo_th_tx != FIFO_TH::NO || fifo_th_rx != FIFO_TH::NO)))
		return SysInitStatus::InitError;
#endif

	*info->clk_reg |= info->clk_bit;
	(void)*info->clk_reg;
	USARTx->CR1 = 0; // BRR and FIFO configuration require UE=0.
#if defined(STM32F7)
	RCC->DCKCFGR2 &= ~info->clock_select;
#elif defined(STM32G0)
	RCC->CCIPR &= ~info->clock_select;
	USARTx->PRESC = 0;
#endif
	USARTx->BRR = brr;
	USARTx->CR2 = 0;
	USARTx->CR1 = static_cast<uint32_t>(fifo);
	USARTx->CR3 = (static_cast<uint32_t>(fifo_th_tx) << FIFO_TH_TX_Pos)
		| (static_cast<uint32_t>(fifo_th_rx) << FIFO_TH_RX_Pos);
	if (_TX.IsValid()) {
		_TX.SetUp(PIN::TYPE::AF_PushPull);
		USARTx->CR1 |= USART_CR1_TE;
	}
	if (_RX.IsValid()) {
		_RX.SetUp(PIN::TYPE::AF_PushPull);
		USARTx->CR1 |= USART_CR1_RE;
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
	rx_status = SysStatus::OK;
	tx_status = SysStatus::OK;
	USARTx->CR1 |= USART_CR1_UE;
	NVIC_EnableIRQ(info->irq);
	return SysInitStatus::InitOK;
}

SysStatus USART::SetBaud(uint32_t baud)
{
	if (!_info) return SysStatus::NotInit;
	const uint32_t brr = BaudRegister(*_info->bus_clk, baud);
	if (!brr) return SysStatus::Error;
	// Caller ensures physical completion via TC; these are software transfer states.
	if (tx_status == SysStatus::Busy || rx_status == SysStatus::Busy)
		return SysStatus::Busy;
	const uint32_t cr1 = USARTx->CR1;
	USARTx->CR1 = cr1 & ~USART_CR1_UE;
	USARTx->BRR = brr;
	USARTx->CR1 = cr1;
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
	USARTx->CR3 |= USART_CR3_EIE;
	USARTx->CR1 |= USART_CR1_PEIE;
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
	if (!_dma_tx || !data || !len || len > UINT16_MAX || !(USARTx->CR1 & USART_CR1_TE)) return SysStatus::Error;
	if (tx_status == SysStatus::Busy) return SysStatus::Busy;
	SysStatus status = _dma_tx->SetMemAddr(reinterpret_cast<uint32_t>(data));
	if (status != SysStatus::OK) return status;
	status = _dma_tx->SetCount(len);
	if (status != SysStatus::OK) return status;
	System::CacheClean(data, len);
	_dma_tx->ClearFlags();
#if defined(STM32F4)
	USARTx->SR = ~USART_SR_TC;
#else
	ClearFlags(ISR_FLAGS::TC);
#endif
	tx_status = SysStatus::Busy;
	_dma_tx->Stream_EN(ENABLE);
	DMA_en(ENABLE, DMA::TX);
	return SysStatus::OK;
}

SysStatus USART::Receive_DMA(uint8_t* data, uint32_t len)
{
	if (!_info) return SysStatus::NotInit;
	if (!_dma_rx || !data || !len || len > UINT16_MAX || !(USARTx->CR1 & USART_CR1_RE)) return SysStatus::Error;
	if (rx_status == SysStatus::Busy) return SysStatus::Busy;
	SysStatus status = _dma_rx->SetMemAddr(reinterpret_cast<uint32_t>(data));
	if (status != SysStatus::OK) return status;
	status = _dma_rx->SetCount(len);
	if (status != SysStatus::OK) return status;
	ClearDataReceivedFlag();
	data_overflow = false;
	data_overflow_count = 0;
	rx_data = { data, static_cast<uint16_t>(len) }; // Retain original address and capacity for cache/count.
#if defined(STM32F4)
	(void)USARTx->SR;
	(void)USARTx->DR;
#else
	USARTx->ICR = USART_ICR_IDLECF | USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NCF | USART_ICR_ORECF;
	USARTx->RQR = USART_RQR_RXFRQ;
#endif
	System::CacheCleanInvalidate(data, len);
	_dma_rx->ClearFlags();
	rx_status = SysStatus::Busy;
	USARTx->CR1 |= USART_CR1_PEIE;
	USARTx->CR3 |= USART_CR3_EIE;
	_dma_rx->Stream_EN(ENABLE);
	DMA_en(ENABLE, DMA::RX);
	IRQ_en(IRQ::IDLE, ENABLE);
	return SysStatus::OK;
}

// Validate routes first, acquire resources second, register each distinct IRQ once.
// Publish attachment pointers only after every step succeeds; rollback must never
// release a resource whose SetUp failed because another owner already held it.
SysInitStatus USART::AttachDMA(DMA_Sx* tx, DMA_Sx* rx)
{
	if (!_info || (!tx && !rx) || (tx && tx == rx) || _dma_tx || _dma_rx
		|| tx_status == SysStatus::Busy || rx_status == SysStatus::Busy)
		return SysInitStatus::InitError;
	DMA_Sx::DMAReq tx_req{}, rx_req{};
	for (const auto& e : uart_dma_req_table) {
		if (e.periph != USARTx) continue;
		for (unsigned i = 0; i < UART_DMA_ROUTES; ++i) {
			if (tx && tx->CanUseRequest(e.tx[i])) tx_req = e.tx[i];
			if (rx && rx->CanUseRequest(e.rx[i])) rx_req = e.rx[i];
		}
		break;
	}
	if ((tx && !tx_req.IsValid()) || (rx && !rx_req.IsValid())) return SysInitStatus::InitError;

	DMA_Sx::StreamSettings cfg;
	cfg.data_size = DMA_Sx::SIZE::Byte;
	cfg.minc = true;
	if (tx) {
		cfg.channel = tx_req.ch;
		cfg.direction = DMA_Sx::DIR::To_Per;
		cfg.per_address = reinterpret_cast<uint32_t>(&TXD());
		if (tx->SetUp(cfg) != SysInitStatus::InitOK) return SysInitStatus::InitError;
	}
	if (rx) {
		cfg.channel = rx_req.ch;
		cfg.direction = DMA_Sx::DIR::From_Per;
		cfg.per_address = reinterpret_cast<uint32_t>(&RXD());
		if (rx->SetUp(cfg) != SysInitStatus::InitOK) {
			if (tx) tx->Release(); // Only the resource successfully acquired above.
			return SysInitStatus::InitError;
		}
	}

	const IRQn_Type no_irq = static_cast<IRQn_Type>(-1);
	const IRQn_Type irqs[2] = { tx ? tx->GetIRQn() : no_irq, rx ? rx->GetIRQn() : no_irq };
	bool enabled[2]{};
	for (unsigned i = 0; i < 2; ++i) {
		if (irqs[i] == no_irq || (i && irqs[i] == irqs[0])) continue;
		enabled[i] = NVIC_GetEnableIRQ(irqs[i]) != 0;
		NVIC_DisableIRQ(irqs[i]);
		if (IRQ_Registry::Register(irqs[i], this) != SysInitStatus::InitOK) {
			if (enabled[i]) NVIC_EnableIRQ(irqs[i]);
			for (unsigned j = 0; j < i; ++j) {
				if (irqs[j] == no_irq) continue;
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

// One handler serves UART and DMA vectors. Aggregate enabled receive errors and
// DMA direction errors before considering TC, so failure cannot publish ready.
// After a completion callback, refresh UART flags rather than reusing stale IDLE.
void USART::HandleIRQ()
{
	if (!_info) return;
	uint32_t sr = Status_reg();
	const uint32_t cr1 = USARTx->CR1;
	const uint32_t cr3 = USARTx->CR3;
	uint32_t errors = 0;
	if (rx_status == SysStatus::Busy) {
		const uint32_t enabled_errors = ((cr1 & USART_CR1_PEIE) ? static_cast<uint32_t>(Error::Parity) : 0U)
			| ((cr3 & USART_CR3_EIE) ? (UART_ERRORS & ~static_cast<uint32_t>(Error::Parity)) : 0U);
		errors = sr & enabled_errors;
		if (_dma_rx && (cr3 & USART_CR3_DMAR) && _dma_rx->GetErrorFlags())
			errors |= static_cast<uint32_t>(Error::DmaRx);
	}
	if (_dma_tx && tx_status == SysStatus::Busy && (cr3 & USART_CR3_DMAT) && _dma_tx->GetErrorFlags())
		errors |= static_cast<uint32_t>(Error::DmaTx);
	if (errors) {
		if (errors & (UART_ERRORS | static_cast<uint32_t>(Error::DmaRx))) {
			USARTx->CR1 &= ~CR1_RX_IRQ;
			USARTx->CR3 &= ~CR3_RX_IRQ;
			if (_dma_rx && (cr3 & USART_CR3_DMAR)) {
				DMA_en(DISABLE, DMA::RX);
				_dma_rx->Stream_EN(DISABLE);
				_dma_rx->ClearFlags();
				System::CacheInvalidate(rx_data.data_ptr, rx_data.size - _dma_rx->GetCount());
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
		}
		if (errors & static_cast<uint32_t>(Error::DmaTx)) {
			DMA_en(DISABLE, DMA::TX);
			_dma_tx->Stream_EN(DISABLE);
			_dma_tx->ClearFlags();
			USARTx->CR1 &= ~CR1_TXEIE;
			USARTx->CR3 &= ~CR3_TX_IRQ;
			tx_status = SysStatus::Error;
		}
		OnError(errors); // One callback includes all simultaneous causes.
		return;
	}

	if (_dma_tx && tx_status == SysStatus::Busy
		&& (USARTx->CR3 & USART_CR3_DMAT) && _dma_tx->GetTC_Flag()) {
		OnDmaTxComplete();
		sr = Status_reg();
	}
	if (_dma_rx && rx_status == SysStatus::Busy
		&& (USARTx->CR3 & USART_CR3_DMAR) && _dma_rx->GetTC_Flag()) {
		OnDmaRxComplete();
		sr = Status_reg();
	}

	bool rx_irq = (USARTx->CR1 & CR1_RXNEIE) && (sr & ISR_RXNE);
	bool tx_irq = (USARTx->CR1 & CR1_TXEIE) && (sr & ISR_TXE);
#if defined(STM32G0)
	rx_irq = rx_irq || ((USARTx->CR3 & USART_CR3_RXFTIE) && (sr & ISR_RXFT));
	tx_irq = tx_irq || ((USARTx->CR3 & USART_CR3_TXFTIE) && (sr & ISR_TXFT));
#endif
	// Never read RDR in CPU while DMA owns the RX path.
	if (rx_irq && rx_status == SysStatus::Busy && !(USARTx->CR3 & USART_CR3_DMAR))
		while ((Status_reg() & ISR_RXNE) && rx_status == SysStatus::Busy)
			OnRxByte(static_cast<uint8_t>(RXD()));
	if ((sr & ISR_IDLE) && (USARTx->CR1 & USART_CR1_IDLEIE)) OnIdle();
	if (tx_irq && tx_status == SysStatus::Busy && !(USARTx->CR3 & USART_CR3_DMAT)) OnTxEmpty();
	// Sending the next byte clears TC. Re-read after TXE to avoid an old TC event.
	if ((USARTx->CR1 & USART_CR1_TCIE) && (Status_reg() & ISR_TC)) {
#if defined(STM32F4)
		USARTx->SR = ~USART_SR_TC;
#else
		ClearFlags(ISR_FLAGS::TC);
#endif
		OnTC();
	}
}

void USART::OnTxEmpty()
{
	while (tx_data.size && (Status_reg() & ISR_TXE)) {
		TXD() = *tx_data.data_ptr++;
		--tx_data.size;
	}
	if (!tx_data.size) {
		USARTx->CR1 &= ~CR1_TXEIE;
		USARTx->CR3 &= ~CR3_TX_IRQ;
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
		USARTx->CR1 &= ~CR1_RX_IRQ;
		USARTx->CR3 &= ~CR3_RX_IRQ;
		rx_status = SysStatus::OK;
		__DMB();
		data_received = true;
	}
}

void USART::OnIdle()
{
	const bool receiving = rx_status == SysStatus::Busy;
	const bool dma = receiving && _dma_rx && (USARTx->CR3 & USART_CR3_DMAR);
	uint32_t errors = 0;
	USARTx->CR1 &= ~CR1_RX_IRQ;
	USARTx->CR3 &= ~CR3_RX_IRQ;
	if (dma) {
		DMA_en(DISABLE, DMA::RX);
		_dma_rx->Stream_EN(DISABLE); // Includes waiting for EN=0.
		if (_dma_rx->GetErrorFlags()) errors = static_cast<uint32_t>(Error::DmaRx);
		data_received_count = rx_data.size - _dma_rx->GetCount();
		_dma_rx->ClearFlags();
		System::CacheInvalidate(rx_data.data_ptr, data_received_count);
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
	while (receiving && (Status_reg() & ISR_RXNE)) {
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
	_dma_tx->Stream_EN(DISABLE);
	_dma_tx->ClearFlags();
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
	const bool receiving_dma = _dma_rx && (USARTx->CR3 & USART_CR3_DMAR);
	USARTx->CR1 = 0;
	USARTx->CR3 = 0; // Disable sources and UART requests before stopping DMA.
	if (_dma_tx) { _dma_tx->Stream_EN(DISABLE); _dma_tx->ClearFlags(); }
	if (_dma_rx) { _dma_rx->Stream_EN(DISABLE); _dma_rx->ClearFlags(); }

	const IRQn_Type no_irq = static_cast<IRQn_Type>(-1);
	const IRQn_Type irqs[3] = {
		_info->irq, _dma_tx ? _dma_tx->GetIRQn() : no_irq, _dma_rx ? _dma_rx->GetIRQn() : no_irq
	};
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
	if (_dma_rx) {
		if (receiving_dma)
			System::CacheInvalidate(rx_data.data_ptr, rx_data.size - _dma_rx->GetCount());
		_dma_rx->Release();
	}
	_dma_tx = nullptr;
	_dma_rx = nullptr;
	USARTx->CR2 = 0;
	tx_data = {};
	rx_data = {};
	ClearDataReceivedFlag();
	data_overflow = false;
	data_overflow_count = 0;
	tx_status = SysStatus::NotInit;
	rx_status = SysStatus::NotInit;
	_info = nullptr;
}
