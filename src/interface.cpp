#include <interface.hpp>

void Interface_DMA::DMA_SetUp()
{
	dma_tx->SetUp();
	dma_tx->MINC(ENABLE);
	dma_rx->SetUp();
	dma_rx->MINC(ENABLE);
};

Interface_USART::Interface_USART(USART *_usart, DMA_Stream_TypeDef *_dma_stream_tx, DMA_Stream_TypeDef *_dma_stream_rx) :
	Interface_DMA(),
	usart(_usart)
{
	uint32_t ch = 4;
	if(usart->USARTx == USART6)
		ch = 5;
	if(_dma_stream_tx != NULL)
	{
		dma_tx = new DMA_Sx(_dma_stream_tx, 
					ch,
					reinterpret_cast<uint32_t>(&usart->USARTx->DR),
					DMA_Sx::Per_Type::usart,
					DMA_Sx::DIR::To_Per);

		status_tx = SYS_NO_Init;
	}

	if(_dma_stream_tx != NULL)
	{
		dma_rx = new DMA_Sx(_dma_stream_rx, 
					ch,
					reinterpret_cast<uint32_t>(&usart->USARTx->DR),
					DMA_Sx::Per_Type::usart,
					DMA_Sx::DIR::From_Per);

		status_rx = SYS_NO_Init;
	}
};

SYS_StatusTypeDef Interface_USART::Init()
{
	usart->SetUp();
	usart->DMA(ENABLE);
	DMA_SetUp();
	if(dma_tx->DMA_Stream_X != NULL)
		status_tx = SYS_OK;
	if(dma_rx->DMA_Stream_X != NULL)
		status_rx = SYS_OK;

	return SYS_OK;
};

void Interface_USART::Send(uint8_t* data, uint16_t data_len)
{
	if(status_tx == SYS_OK)
	{
		status_tx = SYS_BUSY;

		dma_tx->DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(data);
		dma_tx->DMA_Stream_X->NDTR = data_len;
		usart->ClearFlags();
		usart->Enable_IRQ(USART::IRQ::TC);
		dma_tx->ClearFlags();
		dma_tx->Enable_Stream();
	}
};

void Interface_USART::Receive(uint8_t* data, uint16_t data_len)
{
	Receive(data, data_len, false);
}

void Interface_USART::Receive(uint8_t* data, uint16_t data_len, bool cont)
{
	status_rx = SYS_BUSY;
	ContReceive = cont;

	dma_rx->DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(&data);
	dma_rx->DMA_Stream_X->NDTR = data_len;
	usart->Enable_IRQ(USART::IRQ::IDLE);
	dma_rx->ClearFlags();
	dma_rx->Enable_Stream();
};

void Interface_USART::IRQHandler()
{
	// Handle Transmit Complete (TC) interrupt
	if((usart->USARTx->SR & USART_SR_TC)
	&& (usart->USARTx->CR1 & USART_CR1_TCIE))
	{
		usart->USARTx->SR &= ~USART_SR_TC;
		usart->Disable_IRQ(USART::IRQ::TC);
		status_tx = SYS_OK;
	}

	// Handle Idle Line Detected interrupt
	if((usart->USARTx->SR & USART_SR_IDLE)
	&& (usart->USARTx->CR1 & USART_CR1_IDLEIE))
	{
		IsDataReceived = true;
		dma_rx->Disable_Stream();

		usart->USARTx->DR;
		if(ContReceive)
		{
			dma_rx->ClearFlags();
			dma_rx->Enable_Stream();
		}
		else
		{
			usart->Disable_IRQ(USART::IRQ::IDLE);
			status_rx = SYS_OK;
		}
	}
};

Interface_buffer_USART::Interface_buffer_USART(USART *_usart, DMA_Stream_TypeDef *_dma_tx, DMA_Stream_TypeDef *_dma_rx) :
	Interface_buffer(),
	Interface_USART(_usart, _dma_tx, _dma_rx)
{

};

/**
 * @brief Send data over the interface.
 * @param data Pointer to data to be sent.
 * @param len Length of the data.
 */
void Interface_buffer_USART::Send(uint8_t* data, uint16_t len)
{
	tx_data_typedef tmp = {.len = len};
	tx.push_back(tmp);

	tx.back().data_ptr = new uint8_t[len];
	uint8_t* t = tx.back().data_ptr;
	if(t == 0)
		return;

	memcpy(tx.back().data_ptr, data, len);
	tx.back().len = len;
	
	if(tx.size() == 1)
	{
		StartTranssmit();
	}
};

/**
 * @brief Enable continuous receive mode.
 */
void Interface_buffer_USART::Enable_Cont_Recieve(uint16_t len)
{
	cont_rx = true;

	// StartReceiver();
	Recieve(len);
}

/**
 * @brief Receive data from the interface.
 */
void Interface_buffer_USART::Recieve(uint16_t len)
{
	// cont_rx = false;

	rx_data_typedef tmp;
	tmp.data_ptr = new uint8_t[len];
	tmp.len = len;
	rx.push_back(tmp);

	StartReceiver();
}

/**
 * @brief IRQ handler for the interface.
 */
void Interface_buffer_USART::IRQHandler(void)
{
	// Handle Transmit Complete (TC) interrupt
	if((usart->USARTx->SR & USART_SR_TC)
	&& (usart->USARTx->CR1 & USART_CR1_TCIE))
	{
		usart->ClearFlags();

		if(tx.size() != 0)
		{
			delete tx.front().data_ptr;
			tx.erase(tx.begin());
		}

		if(tx.empty())
		{
			usart->Disable_IRQ(USART::IRQ::TC);
		}
		else
		{
			StartTranssmit();
		}
	}

	// Handle Idle Line Detected interrupt
	if((usart->USARTx->SR & USART_SR_IDLE)
	&& (usart->USARTx->CR1 & USART_CR1_IDLEIE))
	{
		dma_rx->Disable_Stream();
		usart->USARTx->DR;

		uint32_t tmp = tx.back().len;

		rx.back().len -= dma_rx->DMA_Stream_X->NDTR;

		if(cont_rx)
		{
			Recieve(tmp);
		}
		else
		{
			usart->Disable_IRQ(USART::IRQ::IDLE);
		}
	}
}

/**
 * @brief Start the transmit process.
 */
void Interface_buffer_USART::StartTranssmit()
{
	if(tx.size()>0)
	{
		dma_tx->DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(tx.front().data_ptr);
		dma_tx->DMA_Stream_X->NDTR = tx.front().len;
		usart->ClearFlags();
		usart->Enable_IRQ(USART::IRQ::TC);
		dma_tx->ClearFlags();
		dma_tx->Enable_Stream();
	}
}

/**
 * @brief Start the receive process.
 */
void Interface_buffer_USART::StartReceiver()
{
	dma_rx->DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(rx.back().data_ptr);
	dma_rx->DMA_Stream_X->NDTR = sizeof(rx.back().len);
	usart->Enable_IRQ(USART::IRQ::IDLE);
	dma_rx->ClearFlags();
	dma_rx->Enable_Stream();
}

Interface_SPI::Interface_SPI(SPI *_spi, SPI::Init_struct_Typedef _init_data, DMA_Stream_TypeDef *_dma_tx, DMA_Stream_TypeDef *_dma_rx) :
	Interface_DMA(),
	spi(_spi),
	spi_init_data(_init_data)
{
	uint32_t ch_tx = 0, ch_rx = 0;

	if(spi->SPIx == SPI1)
	{
		ch_tx = 3;
		ch_rx = 3;
	}
	else if(spi->SPIx == SPI4)
	{
		if(_dma_tx == DMA2_Stream1)
			ch_tx = 4;
		else
			ch_tx = 5;
		if(_dma_rx == DMA2_Stream0)
			ch_rx = 4;
		else
			ch_rx = 5;
	}

	dma_tx = new DMA_Sx(_dma_tx, 
				ch_tx,
				reinterpret_cast<uint32_t>(&spi->SPIx->DR),
				DMA_Sx::Per_Type::spi,
				DMA_Sx::DIR::To_Per);
	dma_rx = new DMA_Sx(_dma_rx, 
				ch_rx,
				reinterpret_cast<uint32_t>(&spi->SPIx->DR),
				DMA_Sx::Per_Type::spi,
				DMA_Sx::DIR::From_Per);
};

void Interface_SPI::Init()
{
	spi->SetUp(spi_init_data);
	spi->DMA_TX(ENABLE);
	spi->DMA_RX(ENABLE);

	DMA_SetUp();

	status = SYS_OK;
};

void Interface_SPI::Send_Receive(uint8_t* tx_data, uint8_t* rx_data, uint16_t data_len)
{
	status = SYS_BUSY;

	dma_tx->ClearFlags();
	dma_rx->ClearFlags();

	dma_tx->DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(tx_data);
	dma_tx->DMA_Stream_X->NDTR = data_len;
	dma_tx->MINC(ENABLE);

	dma_rx->DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(rx_data);
	dma_rx->DMA_Stream_X->NDTR = data_len;
	dma_rx->MINC(ENABLE);

	dma_rx->Enable_IRQ(DMA_Sx::IRQ::TC);

	spi->SlaveSelect(ENABLE);

	spi->DMA_TX(ENABLE);
	spi->DMA_RX(ENABLE);

	dma_tx->Enable_Stream();
	dma_rx->Enable_Stream();

	spi->Enable();
};

void Interface_SPI::Send(uint8_t* tx_data,uint16_t data_len)
{
		status = SYS_BUSY;

	dma_tx->ClearFlags();
	dma_rx->ClearFlags();

	dma_tx->DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(tx_data);
	dma_tx->DMA_Stream_X->NDTR = data_len;
	dma_tx->MINC(ENABLE);

	dma_rx->DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(tmp_data);
	dma_rx->DMA_Stream_X->NDTR = data_len;
	dma_rx->MINC(DISABLE);

	dma_rx->Enable_IRQ(DMA_Sx::IRQ::TC);

	spi->SlaveSelect(ENABLE);

	spi->DMA_TX(ENABLE);
	spi->DMA_RX(ENABLE);

	dma_tx->Enable_Stream();
	dma_rx->Enable_Stream();

	spi->Enable();
};

void Interface_SPI::Receive(uint8_t* rx_data, uint16_t data_len)
{
	status = SYS_BUSY;

	dma_tx->ClearFlags();
	dma_rx->ClearFlags();

	dma_tx->DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(tmp_data);
	dma_tx->DMA_Stream_X->NDTR = data_len;
	dma_tx->MINC(DISABLE);

	dma_rx->DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(rx_data);
	dma_rx->DMA_Stream_X->NDTR = data_len;
	dma_rx->MINC(ENABLE);

	dma_rx->Enable_IRQ(DMA_Sx::IRQ::TC);

	spi->SlaveSelect(ENABLE);

	spi->DMA_TX(ENABLE);
	spi->DMA_RX(ENABLE);

	dma_tx->Enable_Stream();
	dma_rx->Enable_Stream();

	spi->Enable();
};

void Interface_SPI::IRQHandler()
{
	spi->Disable();
	spi->DMA_TX(DISABLE);
	spi->DMA_RX(DISABLE);

	spi->SlaveSelect(DISABLE);

	dma_rx->Disable_IRQ(DMA_Sx::IRQ::TC);

	IsDataReceived = true;

	status = SYS_OK;
};

Interface_buffer_SPI::Interface_buffer_SPI(SPI *_spi, SPI::Init_struct_Typedef _init_data, DMA_Stream_TypeDef *_dma_tx, DMA_Stream_TypeDef *_dma_rx) :
	Interface_buffer(),
	Interface_SPI(_spi, _init_data, _dma_tx, _dma_rx)
{

};

void Interface_buffer_SPI::Send_Receive(uint8_t *tx_data, uint16_t len)
{
	rxtx_data_typedef tmp_data;
	tmp_data.len = len;
	tmp_data.type = RXTX_Type::RXTX;
	
	tx.push_back(tmp_data);

	tx.back().tx_data_ptr = new uint8_t[len];
	tx.back().rx_data_ptr = new uint8_t[len];
	memcpy(tx.back().tx_data_ptr, tx_data, len);

	if(tx.size() == 1)
	{
		StartTranssmit();
	}
}

void Interface_buffer_SPI::Send(uint8_t *tx_data, uint16_t len)
{
	rxtx_data_typedef tmp_data;
	tmp_data.len = len;
	tmp_data.type = RXTX_Type::TX;

	tx.push_back(tmp_data);

	tx.back().tx_data_ptr = new uint8_t[len];
	tx.back().rx_data_ptr = new uint8_t[1];

	memcpy(tx.back().tx_data_ptr, tx_data, len);

	if(tx.size() == 1)
	{
		StartTranssmit();
	}
}

void Interface_buffer_SPI::Receive(uint16_t len)
{
	rxtx_data_typedef tmp_data;
	tmp_data.len = len;
	tmp_data.type = RXTX_Type::RX;

	tx.push_back(tmp_data);

	tx.back().tx_data_ptr = new uint8_t[1];
	tx.back().rx_data_ptr = new uint8_t[len];
	tx.back().tx_data_ptr[0] = 0;

	if(tx.size() == 1)
	{
		StartTranssmit();
	}
}

void Interface_buffer_SPI::StartTranssmit()
{
	if(tx.size() == 0)
		return;

	dma_tx->ClearFlags();
	dma_rx->ClearFlags();

	dma_tx->DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(tx.front().tx_data_ptr);
	dma_tx->DMA_Stream_X->NDTR = tx.front().len;
	dma_tx->MINC(ENABLE);

	dma_rx->DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(tx.front().rx_data_ptr);
	dma_rx->DMA_Stream_X->NDTR = tx.front().len;
	dma_rx->MINC(ENABLE);

	if(tx.front().type == RXTX_Type::RX)
		dma_tx->MINC(DISABLE);
	if(tx.front().type == RXTX_Type::TX)
		dma_rx->MINC(DISABLE);

	dma_rx->Enable_IRQ(DMA_Sx::IRQ::TC);

	spi->SlaveSelect(ENABLE);

	spi->DMA_TX(ENABLE);
	spi->DMA_RX(ENABLE);

	dma_tx->Enable_Stream();
	dma_rx->Enable_Stream();

	spi->Enable();
}

void Interface_buffer_SPI::IRQHandler(void)
{
	spi->Disable();
	spi->DMA_TX(DISABLE);
	spi->DMA_RX(DISABLE);

	spi->SlaveSelect(DISABLE);

	dma_rx->Disable_IRQ(DMA_Sx::IRQ::TC);

	if(tx.front().type != RXTX_Type::TX)
		rx.push_back(tx.front());

	delete [] tx.front().tx_data_ptr;
	delete [] tx.front().rx_data_ptr;
	tx.erase(tx.begin());

	if(tx.size() != 0)
	{
		StartTranssmit();
	}

}