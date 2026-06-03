#include "uart.hpp"

#define UART_DEFS_CPP
#include "uart_defs.hpp"
#undef  UART_DEFS_CPP

/**
 * @brief Initialize the USART configuration.
 * @return The status of the initialization operation.
 */
SysInitStatus USART::SetUp(FIFO fifo, FIFO_TH fifo_th_tx, FIFO_TH fifo_th_rx)
{
	if((BaudRate < 9600)
	|| (BaudRate > 115200*16)) // todo - max baud from datasheets
		return SysInitStatus::InitError;

	const PeriphInfo* info = nullptr;
	for (const auto& e : usart_table)
		if (e.periph == USARTx) { info = &e; break; }
	if (!info)
		return SysInitStatus::InitError;

	_info = info;
	*info->clk_reg |= info->clk_bit;

	// Set the Baud Rate
	USARTx->BRR = *_info->bus_clk / BaudRate;
	USARTx->CR2 = 0;
	
	USARTx->CR1 = static_cast<uint32_t>(fifo);  // NO=0, EN=FIFOEN
	USARTx->CR3 = (static_cast<uint32_t>(fifo_th_tx) << FIFO_TH_TX_Pos) | (static_cast<uint32_t>(fifo_th_rx) << FIFO_TH_RX_Pos);

	// Enable USART and configure TX and RX pins if available
	USARTx->CR1 |= USART_CR1_UE;

	if (_TX.IsValid())
	{
		_TX.SetUp(PIN::TYPE::AF_PushPull);
		USARTx->CR1 |= USART_CR1_TE;
	}
	if (_RX.IsValid())
	{
		_RX.SetUp(PIN::TYPE::AF_PushPull);
		USARTx->CR1 |= USART_CR1_RE;
	}


#if defined(STM32F4)
	ClearFlags();
#elif defined(STM32F7) || defined(STM32G0)
	ClearFlags(ISR_FLAGS::TC);
#endif

	rx_status = SysStatus::OK;
	tx_status = SysStatus::OK;

	return SysInitStatus::InitOK;
}

SysStatus USART::Send(uint8_t *data, uint32_t len, uint32_t timeout)
{
	if(tx_status == SysStatus::Busy)
		return tx_status;
	uint32_t tick_start = System::GetTick();
	tx_status = SysStatus::Busy;

	for(uint32_t i = 0;i<len;i++)
	{
		while(!(Status_reg() & ISR_TXE))
		{
			if(System::GetTick() - tick_start > timeout) {
				tx_status = SysStatus::OK;
				return SysStatus::Timeout;
			}
		};

		TXD() = data[i];
		
	};
	tx_status = SysStatus::OK;
	return SysStatus::OK;
};

SysStatus USART::Receive(uint8_t *data, uint32_t len, uint32_t timeout)
{
	if(rx_status == SysStatus::Busy)
		return rx_status;
	uint32_t tick_start = System::GetTick();
	rx_status = SysStatus::Busy;
	for(uint32_t i = 0;i<len;i++)
	{

		while(!(Status_reg() & ISR_RXNE))
		{
			if(System::GetTick() - tick_start > timeout) {
				rx_status = SysStatus::OK;
				return SysStatus::Timeout;
			}
		};

		data[i] = RXD();

	};
	rx_status = SysStatus::OK;
	return SysStatus::OK;
};

SysStatus USART::Send_IRQ(uint8_t *data, uint32_t len){
	if(tx_status != SysStatus::OK)
		return tx_status;
	if(!data)
		return SysStatus::Error;
	
	tx_status = SysStatus::Busy;

	tx_data.data_ptr = data;
	tx_data.size = len;

	#if defined(STM32F4) || defined(STM32F7)
		IRQ_en(IRQ::TXE, ENABLE);
	#elif defined(STM32G0)
		if(USARTx->CR1 & USART_CR1_FIFOEN)
			IRQ_en(IRQ::TXFIFO, ENABLE);
		else
			IRQ_en(IRQ::TXE, ENABLE);
	#endif
	return SysStatus::OK;
};

SysStatus USART::Receive_IRQ(uint8_t* data, uint32_t len) {
	if(rx_status != SysStatus::OK)
		return rx_status;
	if(!data)
		return SysStatus::Error;

	rx_status = SysStatus::Busy;
	data_overflow = false;
	data_overflow_count = 0;

	rx_data = { data, (uint16_t)len};

	#if defined(STM32F4) || defined(STM32F7)
		IRQ_en(IRQ::RXNE, ENABLE);
	#elif defined(STM32G0)
		if(USARTx->CR1 & USART_CR1_FIFOEN)
			IRQ_en(IRQ::RXFIFO, ENABLE);
		else
			IRQ_en(IRQ::RXNE, ENABLE);
	#endif
	IRQ_en(IRQ::IDLE, ENABLE);

	return SysStatus::OK;
}

SysStatus USART::Send_DMA(uint8_t* data, uint32_t len) {
	if (!_dma_tx || !data || !len) return SysStatus::Error;
	if (tx_status == SysStatus::Busy)  return SysStatus::Busy;

	tx_status = SysStatus::Busy;
#if defined(STM32F7)
	SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(data),
	                         static_cast<int32_t>((len + 31u) & ~31u));
#endif
	_dma_tx->ClearFlags();
	_dma_tx->SetMemAddr(reinterpret_cast<uint32_t>(data));
	_dma_tx->SetCount(len);
	DMA_en(ENABLE, DMA::TX);
	_dma_tx->Stream_EN(ENABLE);
	return SysStatus::OK;
}

SysStatus USART::Receive_DMA(uint8_t* data, uint32_t len) {
	if (!_dma_rx || !data || !len) return SysStatus::Error;
	if (rx_status == SysStatus::Busy)  return SysStatus::Busy;

	rx_status = SysStatus::Busy;
	data_overflow = false;
	data_overflow_count = 0;
	rx_data = { data, static_cast<uint16_t>(len) };
	_dma_rx->ClearFlags();
	_dma_rx->SetMemAddr(reinterpret_cast<uint32_t>(data));
	_dma_rx->SetCount(len);
	DMA_en(ENABLE, DMA::RX);
	_dma_rx->Stream_EN(ENABLE);
	IRQ_en(IRQ::IDLE, ENABLE);
	return SysStatus::OK;
}

// ---------------------------------------------------------------------------
// DMA attachment and transfer methods
// ---------------------------------------------------------------------------

void USART::AttachDMA(DMA_Sx* tx, DMA_Sx* rx) {
	_dma_tx = tx;
	_dma_rx = rx;
	if (!_info) return;

	// Look up req ID (G0) / CHSEL (F4/F7) for this peripheral.
	// Only used when DMA_Sx was not constructed with an explicit DMAReq (_has_preset=false).
	uint32_t tx_req = 0, rx_req = 0;
	for (const auto& e : uart_dma_req_table)
		if (e.periph == USARTx) { tx_req = e.tx_req; rx_req = e.rx_req; break; }

	if (tx) {
		DMA_Sx::StreamSettings cfg;
		cfg.channel     = tx_req;
		cfg.direction   = DMA_Sx::DIR::To_Per;
		cfg.data_size   = DMA_Sx::SIZE::Byte;
		cfg.minc        = true;
#if defined(STM32F4)
		cfg.per_address = reinterpret_cast<uint32_t>(&USARTx->DR);
#else
		cfg.per_address = reinterpret_cast<uint32_t>(&USARTx->TDR);
#endif
		tx->SetUp(cfg);
	}

	if (rx) {
		DMA_Sx::StreamSettings cfg;
		cfg.channel     = rx_req;
		cfg.direction   = DMA_Sx::DIR::From_Per;
		cfg.data_size   = DMA_Sx::SIZE::Byte;
		cfg.minc        = true;
#if defined(STM32F4)
		cfg.per_address = reinterpret_cast<uint32_t>(&USARTx->DR);
#else
		cfg.per_address = reinterpret_cast<uint32_t>(&USARTx->RDR);
#endif
		rx->SetUp(cfg);
	}

	IRQn_Type tx_irqn = static_cast<IRQn_Type>(-1);
	if (tx) {
		tx_irqn = tx->GetIRQn();
		if (tx_irqn != static_cast<IRQn_Type>(-1)) {
			IRQ_Registry::Register(tx_irqn, this);
			NVIC_EnableIRQ(tx_irqn);
			tx->Enable_IRQ(DMA_Sx::IRQ::TC);
		}
	}

	if (rx) {
		IRQn_Type rx_irqn = rx->GetIRQn();
		if (rx_irqn != static_cast<IRQn_Type>(-1) && rx_irqn != tx_irqn) {
			IRQ_Registry::Register(rx_irqn, this);
			NVIC_EnableIRQ(rx_irqn);
		}
		rx->Enable_IRQ(DMA_Sx::IRQ::TC);
	}
}

// ---------------------------------------------------------------------------
// HandleIRQ — handles both UART and DMA interrupt lines
// ---------------------------------------------------------------------------

void USART::HandleIRQ() {
	// DMA TC checks (invoked when the DMA IRQ line fires)
	if (_dma_tx && _dma_tx->GetTC_Flag()) {
		_dma_tx->Stream_EN(DISABLE);
		_dma_tx->ClearFlags();
		OnDmaTxComplete();
	}
	if (_dma_rx && _dma_rx->GetTC_Flag()) {
		_dma_rx->Stream_EN(DISABLE);
		_dma_rx->ClearFlags();
		OnDmaRxComplete();
	}

	// UART status flags
	uint32_t sr = Status_reg();
#if defined(STM32G0)
	while (Status_reg() & (ISR_RXNE | ISR_RXFT)) OnRxByte(RXD());
#else
	while (Status_reg() & ISR_RXNE) OnRxByte(RXD());
#endif
#if defined(STM32F4)
	if (sr & ISR_IDLE) { (void)USARTx->DR; OnIdle(); }  // F4: clear IDLE by reading DR after SR
	if (sr & ISR_TC)   { USARTx->SR &= ~USART_SR_TC; OnTC(); }
#else
	if (sr & ISR_IDLE) { ClearFlags(ISR_FLAGS::IDLE); OnIdle(); }
	if (sr & ISR_TC)   { ClearFlags(ISR_FLAGS::TC);   OnTC();   }
#endif
#if defined(STM32G0)
	if (sr & (ISR_TXE | ISR_TXFT)) OnTxEmpty();
#else
	if (sr & ISR_TXE)  OnTxEmpty();
#endif
}

// ---------------------------------------------------------------------------
// Default virtual callbacks
// ---------------------------------------------------------------------------

void USART::OnTxEmpty(){
	while (tx_data.size && (Status_reg() & ISR_TXE)) {  // ISR_TXE = TXFNF в FIFO-режиме
		TXD() = *tx_data.data_ptr++;
		tx_data.size--;
	}

	if (!tx_data.size) {
#if defined(STM32G0)
		if (USARTx->CR1 & USART_CR1_FIFOEN)
			IRQ_en(IRQ::TXFIFO, DISABLE);
		else
#endif
			IRQ_en(IRQ::TXE, DISABLE);
		tx_status = SysStatus::OK;
	}
};

void USART::OnRxByte(uint8_t byte) {
	if (!rx_data.size) {
		data_overflow = true;
		data_overflow_count++;
		return;
	}
	*rx_data.data_ptr++ = byte;
	rx_data.size--;
	data_received_count++;
	if(rx_data.size == 0){
#if defined(STM32G0)
		if (USARTx->CR1 & USART_CR1_FIFOEN)
			IRQ_en(IRQ::RXFIFO, DISABLE);
		else
#endif
			IRQ_en(IRQ::RXNE, DISABLE);
		rx_status = SysStatus::OK;
		data_received = true;
	}
};

void USART::OnIdle() {
	if (rx_status == SysStatus::Busy) {
		if (_dma_rx) {
			// DMA receive: stop channel, count actual bytes received
			uint32_t remaining = _dma_rx->GetCount();
			_dma_rx->Stream_EN(DISABLE);
			_dma_rx->ClearFlags();
			DMA_en(DISABLE, DMA::RX);
			data_received_count = rx_data.size - remaining;
			data_received = true;
			rx_status = SysStatus::OK;
		} else {
			// IRQ receive
#if defined(STM32G0)
			if (USARTx->CR1 & USART_CR1_FIFOEN)
				IRQ_en(IRQ::RXFIFO, DISABLE);
			else
#endif
				IRQ_en(IRQ::RXNE, DISABLE);
			rx_status = SysStatus::OK;
			data_received = true;
		}
	}
	IRQ_en(IRQ::IDLE, DISABLE);
}

void USART::OnTC() {}

void USART::OnDmaTxComplete() {
	DMA_en(DISABLE, DMA::TX);
	tx_status = SysStatus::OK;
}

void USART::OnDmaRxComplete() {
	DMA_en(DISABLE, DMA::RX);
	IRQ_en(IRQ::IDLE, DISABLE);
	data_received_count = rx_data.size;
	data_received = 1;
	rx_status = SysStatus::OK;
}
