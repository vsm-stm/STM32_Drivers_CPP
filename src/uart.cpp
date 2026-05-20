#include "uart.hpp"


#if defined(STM32F4) || defined(STM32F7)
	const USART::PeriphInfo USART::usart_table[] = {
		{ USART1, &RCC->APB2ENR, RCC_APB2ENR_USART1EN, &System::APB2BusClock, USART1_IRQn },
		{ USART2, &RCC->APB1ENR, RCC_APB1ENR_USART2EN, &System::APB1BusClock, USART2_IRQn },
	#ifndef STM32F411xE
		{ USART3, &RCC->APB1ENR, RCC_APB1ENR_USART3EN, &System::APB1BusClock, USART3_IRQn },
		{ UART4,  &RCC->APB1ENR, RCC_APB1ENR_UART4EN,  &System::APB1BusClock, UART4_IRQn  },
		{ UART5,  &RCC->APB1ENR, RCC_APB1ENR_UART5EN,  &System::APB1BusClock, UART5_IRQn  },
		{ USART6, &RCC->APB2ENR, RCC_APB2ENR_USART6EN, &System::APB2BusClock, USART6_IRQn },
	#endif
	};
#elif defined(STM32G0)
	const USART::PeriphInfo USART::usart_table[] = {
		{ USART1, &RCC->APBENR2, RCC_APBENR2_USART1EN, &System::APB1BusClock, USART1_IRQn },
		{ USART2, &RCC->APBENR1, RCC_APBENR1_USART2EN, &System::APB1BusClock, USART2_IRQn },
	};
#endif
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
	USARTx->CR3 = 0;
	
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
	uint32_t tick_start = System::GetTick();
	tx_status = SysStatus::Busy;

	for(uint32_t i = 0;i<len;i++)
	{
		while(!(Status_reg() & ISR_TXE))
		{
			if(System::GetTick() - tick_start > timeout)
				return SysStatus::Timeout;
		};

		TXD() = data[i];
		
	};
	tx_status = SysStatus::OK;
	return SysStatus::OK;
};

SysStatus USART::Receive(uint8_t *data, uint32_t len, uint32_t timeout)
{
	uint32_t tick_start = System::GetTick();
	rx_status = SysStatus::Busy;
	for(uint32_t i = 0;i<len;i++)
	{

		while(!(Status_reg() & ISR_RXNE))
		{
			if(System::GetTick() - tick_start > timeout)
				return SysStatus::Timeout;
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
		Enable_IRQ(IRQ::TXE);
	#elif defined(STM32G0)
		if(USARTx->CR1 & USART_CR1_FIFOEN)
			Enable_IRQ(IRQ::TXFIFO);
		else
			Enable_IRQ(IRQ::TXE);
	#endif
	return SysStatus::OK;
};

void USART::OnTxEmpty(){
	while (tx_data.size && (Status_reg() & ISR_TXE)) {  // ISR_TXE = TXFNF в FIFO-режиме
		TXD() = *tx_data.data_ptr++;
		tx_data.size--;
	}

	if (!tx_data.size) {
#if defined(STM32G0)
		if (USARTx->CR1 & USART_CR1_FIFOEN)
			Disable_IRQ(IRQ::TXFIFO);
		else
#endif
			Disable_IRQ(IRQ::TXE);
		tx_status = SysStatus::OK;
	}
};

SysStatus USART::Receive_IRQ(uint8_t* data, uint32_t len) {
	if(rx_status != SysStatus::OK)
		return rx_status;
	if(!data)
		return SysStatus::Error;

	rx_status = SysStatus::Busy;

	rx_data = { data, (uint16_t)len};

	#if defined(STM32F4) || defined(STM32F7)
		Enable_IRQ(IRQ::RXNE);
	#elif defined(STM32G0)
		if(USARTx->CR1 & USART_CR1_FIFOEN)
			Enable_IRQ(IRQ::RXFIFO);
		else
			Enable_IRQ(IRQ::RXNE);
	#endif
	Enable_IRQ(IRQ::IDLE); // Enable IDLE line detection to handle cases where data length is unknown or shorter than expected

	return SysStatus::OK;
}

void USART::OnRxByte(uint8_t byte) {
	*rx_data.data_ptr++ = byte;
	rx_data.size--;
	data_received_count++;
	if(rx_data.size == 0){
#if defined(STM32G0)
		if (USARTx->CR1 & USART_CR1_FIFOEN)
			Disable_IRQ(IRQ::RXFIFO);
		else
#endif
			Disable_IRQ(IRQ::RXNE);
		rx_status = SysStatus::OK;
		data_received = 1;
	}
};

void USART::OnIdle(){
	if(rx_status == SysStatus::Busy){
#if defined(STM32G0)
		if (USARTx->CR1 & USART_CR1_FIFOEN)
			Disable_IRQ(IRQ::RXFIFO);
		else
#endif
			Disable_IRQ(IRQ::RXNE);
		rx_status = SysStatus::OK;
		data_received = 1;
	}
	Disable_IRQ(IRQ::IDLE);
};

void USART::OnTC(){};
