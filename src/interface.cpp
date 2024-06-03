#include <interface.hpp>
void Interface::SetUpDMA()
{
	dma_tx->SetUp();
	dma_tx->MINC(ENABLE);
	dma_rx->SetUp();
	dma_rx->MINC(ENABLE);
}

Interface_USART::Interface_USART(USART *_usart, DMA_Stream_TypeDef *_dma_tx, DMA_Stream_TypeDef *_dma_rx) :
		Interface(),
		usart(_usart)
{
		uint32_t ch = 4;
		if(usart->USARTx == USART6)
			ch = 5;
		dma_tx = new DMA_Sx(_dma_tx, 
					ch,
					reinterpret_cast<uint32_t>(&usart->USARTx->DR),
					DMA_Sx::Per_Type::usart,
					DMA_Sx::DIR::To_Per);
		dma_rx = new DMA_Sx(_dma_rx, 
					ch,
					reinterpret_cast<uint32_t>(&usart->USARTx->DR),
					DMA_Sx::Per_Type::usart,
					DMA_Sx::DIR::From_Per);
};

/**
 * @brief Send data over the interface.
 * @param data Pointer to data to be sent.
 * @param len Length of the data.
 */
void Interface_USART::Send(uint8_t* data, uint16_t len)
{
	tx_data_typedef tmp;
	tmp.data_ptr = new uint8_t[len];
	memcpy(tmp.data_ptr, data, len);
	tmp.len = len;
	tx.push_back(tmp);

	if(tx.size() == 1)
	{
		StartTranssmit();
	}
};

/**
 * @brief Enable continuous receive mode.
 */
void Interface_USART::Enable_Cont_Recieve(uint16_t len)
{
	cont_rx = true;

	// StartReceiver();
	Recieve(len);
}

/**
 * @brief Receive data from the interface.
 */
void Interface_USART::Recieve(uint16_t len)
{
	// cont_rx = false;

	rx_data_typedef tmp;
	tmp.data_ptr = new uint8_t[len];
	tmp.len = len;
	rx.push_back(tmp);

	StartReceiver();
}


/**
 * @brief Initialize the Interface.
 */
void Interface_USART::Init()
{
	usart->SetUp();
	usart->DMA(ENABLE);
	SetUpDMA();
};

/**
 * @brief IRQ handler for the interface.
 */
void Interface_USART::IRQHandler(void)
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
void Interface_USART::StartTranssmit()
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
void Interface_USART::StartReceiver()
{
	dma_rx->DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(rx.back().data_ptr);
	dma_rx->DMA_Stream_X->NDTR = sizeof(rx.back().len);
	usart->Enable_IRQ(USART::IRQ::IDLE);
	dma_rx->ClearFlags();
	dma_rx->Enable_Stream();
}


Interface_SPI::Interface_SPI(SPI *_spi, SPI::Init_struct_Typedef _init_data, DMA_Stream_TypeDef *_dma_tx, DMA_Stream_TypeDef *_dma_rx) :
		Interface(),
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

	SetUpDMA();
};

void Interface_SPI::RXTX(uint8_t *tx_data, uint16_t len)
{
	tx_data_typedef tx_tmp;
	tx_tmp.data_ptr = new uint8_t[len];
	memcpy(tx_tmp.data_ptr, tx_data, len);
	tx_tmp.len = len;
	tx.push_back(tx_tmp);

	rx_data_typedef rx_tmp;
	rx_tmp.data_ptr = new uint8_t[len];
	rx_tmp.len = len;
	rx.push_back(rx_tmp);

	if(tx.size() == 1)
	{
		StartTranssmit();
	}
}

void Interface_SPI::StartTranssmit()
{
	if(tx.size()>0)
	{
		dma_tx->DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(tx.front().data_ptr);
		dma_tx->DMA_Stream_X->NDTR = tx.front().len;
		dma_rx->DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(rx.back().data_ptr);
		dma_rx->DMA_Stream_X->NDTR = rx.back().len;
		dma_tx->Enable_IRQ(DMA_Sx::IRQ::TC);

		dma_tx->ClearFlags();
		dma_rx->ClearFlags();

		spi->DMA_TX(ENABLE);
		spi->DMA_RX(ENABLE);

		dma_tx->Enable_Stream();
		dma_rx->Enable_Stream();

		spi->Enable();
	}
}

void Interface_SPI::IRQHandler(void)
{
	spi->Disable();
	spi->DMA_TX(DISABLE);
	spi->DMA_RX(DISABLE);

	if(tx.size() != 0)
	{
		delete tx.front().data_ptr;
		tx.erase(tx.begin());
	}

	dma_rx->Disable_IRQ(DMA_Sx::IRQ::TC);
}