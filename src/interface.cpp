#include <interface.hpp>

SYS_StatusTypeDef Interface_DMA::DMA_SetUp()
{
	SYS_StatusTypeDef status;
	status = dma_tx.SetUp(tx_settings);
	if(status == SYS_OK)
	{
		dma_tx.MINC(ENABLE);
		status = dma_rx.SetUp(rx_settings);
		dma_rx.MINC(ENABLE);
	}
	return status;
};

Interface_USART::Interface_USART(	
	USART *_usart,
	DMA_Stream_TypeDef *_dma_tx,
	DMA_Stream_TypeDef *_dma_rx,
	uint32_t tx_buffer_size,
	uint32_t rx_buffer_size) :
									Interface_DMA(_dma_tx, _dma_rx),
									usart(_usart),
									buffer(tx_buffer_size, rx_buffer_size)
{
	tx_settings.channel = 4;
	rx_settings.channel = 4;
	tx_settings.peripheral_type = DMA_Sx::Per_Type::usart;
	rx_settings.peripheral_type = DMA_Sx::Per_Type::usart;
	tx_settings.direction = DMA_Sx::DIR::To_Per;
	rx_settings.direction = DMA_Sx::DIR::From_Per;

#if defined(STM32F4)
	tx_settings.peripheral_address = reinterpret_cast<uint32_t>(&usart->USARTx->DR);
	rx_settings.peripheral_address = reinterpret_cast<uint32_t>(&usart->USARTx->DR);
#elif defined(STM32F7)
	tx_settings.peripheral_address = reinterpret_cast<uint32_t>(&usart->USARTx->TDR);
	rx_settings.peripheral_address = reinterpret_cast<uint32_t>(&usart->USARTx->RDR);
#endif

	if(usart->USARTx == USART6)
	{
		tx_settings.channel = 5;
		rx_settings.channel = 5;
	}

	status_tx = SYS_NO_Init;
	status_rx = SYS_NO_Init;

};

SYS_StatusTypeDef Interface_USART::Init()
{
	usart->SetUp();
	usart->DMA(ENABLE);
	DMA_SetUp();
	if(dma_tx.DMA_Stream_X != NULL)
		status_tx = SYS_OK;
	if(dma_rx.DMA_Stream_X != NULL)
		status_rx = SYS_OK;

	return SYS_OK;
};

SYS_StatusTypeDef Interface_USART::TX(uint8_t* data, uint16_t data_size)
{
	if(status_tx == SYS_OK)
	{
		status_tx = SYS_BUSY;

		dma_tx.SetMemAddr(reinterpret_cast<uint32_t>(data), data_size);
#if defined(STM32F4)
		usart->ClearFlags();
#elif defined(STM32F7)
		usart->ClearFlags(USART::ISR_FLAGS::TC);
#endif
		usart->Enable_IRQ(USART::IRQ::TC);
		dma_tx.ClearFlags();
		dma_tx.Stream(ENABLE);
	}

	return status_tx;
};

SYS_StatusTypeDef Interface_USART::Send(uint8_t* data, uint16_t data_size)
{
	if((data == NULL)
	|| (data_size == 0))
		return SYS_ERROR;
	if(buffer.tx_buffer_size_max == 0)
	{
		return TX(data, data_size);
	}
	else
	{
		if(buffer.tx.size() >= buffer.tx_buffer_size_max)
			return SYS_ERROR;

		buffer.tx.emplace_back();
		_data_typedef *tmp = &buffer.tx.back();
		tmp->size = data_size;
		tmp->data_ptr = new uint8_t[data_size];
		if(tmp->data_ptr == 0)
			return SYS_ERROR;

		memcpy(tmp->data_ptr, data, data_size);
		
		if((buffer.tx.size() == 1)
		&& (status_tx == SYS_OK))
		{
			return TX(buffer.tx.front().data_ptr, buffer.tx.front().size);
		}
	}

	return SYS_OK;
};

SYS_StatusTypeDef Interface_USART::RX(uint8_t* data, uint16_t data_size)
{
	if(status_rx == SYS_OK)
	{
		status_rx = SYS_BUSY;

		dma_rx.SetMemAddr(reinterpret_cast<uint32_t>(&data), data_size);
		usart->Enable_IRQ(USART::IRQ::IDLE);
		dma_rx.ClearFlags();
		dma_rx.Stream(ENABLE);
	}
	return status_rx;
};

SYS_StatusTypeDef Interface_USART::Receive(uint8_t* data, uint16_t data_size, bool cont)
{
	ContReceive = cont;

	if(buffer.rx_buffer_size_max == 0)
	{
		if(data == NULL)
			return SYS_ERROR;
		return RX(data, data_size);
	}
	else
	{
		if(buffer.rx_buffer_size_max == buffer.rx.size())
			return SYS_ERROR;

		buffer.rx.emplace_back();
		buffer.rx.back().size = data_size;
		buffer.rx.back().data_ptr = new uint8_t[data_size];
		uint8_t* t = buffer.rx.back().data_ptr;
		if(t == 0)
			return SYS_ERROR;

		if(status_rx == SYS_OK)
		{
			return RX(buffer.rx.front().data_ptr, buffer.rx.front().size);
		}
	}
	return SYS_OK;
};

SYS_StatusTypeDef Interface_USART::Receive(uint8_t* data, uint16_t data_size)
{
	return Receive(data, data_size, false);
};

SYS_StatusTypeDef Interface_USART::Receive(uint16_t data_size, bool cont)
{
	if(buffer.rx_buffer_size_max == 0)
		return SYS_ERROR;
	else
		return Receive(NULL, data_size, cont);
};

SYS_StatusTypeDef Interface_USART::Receive(uint16_t data_size)
{
	if(buffer.rx_buffer_size_max == 0)
		return SYS_ERROR;
	else
		return Receive(NULL, data_size, false);
};

void Interface_USART::IRQHandler()
{
	// Handle Transmit Complete (TC) interrupt
	if((usart->USARTx->CR1 & USART_CR1_TCIE) &&
#if defined(STM32F4)
		(usart->USARTx->SR & USART_SR_TC))
	{
		usart->USARTx->SR &= ~USART_SR_TC;

#elif defined(STM32F7)
		(usart->USARTx->ISR & USART_ISR_TC))
	{
		usart->USARTx->ICR = USART_ICR_TCCF;
#endif
	
		usart->Disable_IRQ(USART::IRQ::TC);
		status_tx = SYS_OK;

		if(buffer.tx_buffer_size_max != 0)
		{
			
			_data_typedef *tmp = &buffer.tx.front();
			if(buffer.tx.size() != 0)
			{
				
				delete [] tmp->data_ptr;
				buffer.tx.pop_front();
			}

			if(!buffer.tx.empty())
			{
				tmp = &buffer.tx.front();
				status_tx = TX(tmp->data_ptr, tmp->size);
			}
		}
	}

	// Handle Idle Line Detected interrupt
	if((usart->USARTx->CR1 & USART_CR1_IDLEIE) &&
#if defined(STM32F4)
	  (usart->USARTx->SR & USART_SR_IDLE))
#elif defined(STM32F7)
	  (usart->USARTx->ISR & USART_ISR_IDLE))
#endif
	{
		IsDataReceived = true;
		dma_rx.Stream(DISABLE);

#if defined(STM32F4)
		usart->USARTx->DR;
#elif defined(STM32F7)
		usart->USARTx->ICR = USART_ICR_IDLECF;
#endif

		status_rx = SYS_OK;

		if(buffer.rx_buffer_size_max != 0)
		{
			buffer.rx.back().size -= dma_rx.DMA_Stream_X->NDTR;
		}

		if(ContReceive)
		{
			if(buffer.rx_buffer_size_max == 0)
			{
				dma_rx.ClearFlags();
				dma_rx.Stream(ENABLE);
				status_rx = SYS_BUSY;
			}
			else
			{
				status_rx = Receive(buffer.rx.back().size, ContReceive);
			}
		}
		else
		{
			usart->Disable_IRQ(USART::IRQ::IDLE);
		}
	}

};



Interface_SPI::Interface_SPI(
	SPI *_spi, 
	SPI::Init_struct_Typedef _init_data,
	DMA_Stream_TypeDef *_dma_tx,
	DMA_Stream_TypeDef *_dma_rx,
	uint32_t buffer_size) :
								Interface_DMA(_dma_tx, _dma_rx),
								spi(_spi),
								spi_init_data(_init_data),
								buffer(buffer_size, buffer_size)
{
	tx_settings.peripheral_type = DMA_Sx::Per_Type::spi;
	rx_settings.peripheral_type = DMA_Sx::Per_Type::spi;
	tx_settings.direction = DMA_Sx::DIR::To_Per;
	rx_settings.direction = DMA_Sx::DIR::From_Per;
	tx_settings.peripheral_address = reinterpret_cast<uint32_t>(&spi->SPIx->DR);
	rx_settings.peripheral_address = reinterpret_cast<uint32_t>(&spi->SPIx->DR);


	if(spi->SPIx == SPI1)
	{
		tx_settings.channel = 3;
		rx_settings.channel = 3;
	}
	else if(spi->SPIx == SPI4)
	{
		if(_dma_tx == DMA2_Stream1)
			tx_settings.channel = 4;
		else
			tx_settings.channel = 5;
		if(_dma_rx == DMA2_Stream0)
			rx_settings.channel = 4;
		else
			rx_settings.channel = 5;
	}
};

SYS_StatusTypeDef Interface_SPI::Init()
{
	status = spi->SetUp(spi_init_data);
#if defined(STM32F7)
	spi->SPIx->CR2 |= SPI_CR2_LDMARX | SPI_CR2_LDMATX;
#endif
	if(status == SYS_OK)
	{
		status = DMA_SetUp();
	}

	return status;
};

SYS_StatusTypeDef Interface_SPI::TXRX(uint8_t* tx_data, uint8_t* rx_data, uint16_t size, TXRX_Type type)
{
	if(status == SYS_OK)
	{
		status = SYS_BUSY;

		dma_tx.ClearFlags();
		dma_rx.ClearFlags();

		if(type == TXRX_Type::RX)
		{
			dma_tx.SetMemAddr(reinterpret_cast<uint32_t>(&tmp_data),size);
			dma_tx.MINC(DISABLE);
		}
		else
		{
			dma_tx.SetMemAddr(reinterpret_cast<uint32_t>(tx_data),size);
			dma_tx.MINC(ENABLE);
		}

		if(type == TXRX_Type::TX)
		{
			tmp_data[0] = 0;
			dma_rx.SetMemAddr(reinterpret_cast<uint32_t>(&tmp_data),size);
			dma_rx.MINC(DISABLE);
		}
		else
		{
			dma_rx.SetMemAddr(reinterpret_cast<uint32_t>(rx_data),size);
			dma_rx.MINC(ENABLE);
		}

		dma_rx.Enable_IRQ(DMA_Sx::IRQ::TC);

		spi->SlaveSelect(ENABLE);

		spi->DMA_TX(ENABLE);
		spi->DMA_RX(ENABLE);

		dma_tx.Stream(ENABLE);
		dma_rx.Stream(ENABLE);

		spi->Enable();
	}
	return status;
}

SYS_StatusTypeDef Interface_SPI::Send_Receive(uint8_t* tx_data, uint8_t* rx_data, uint16_t data_size)
{
	if(((tx_data == NULL)
	 && (rx_data == NULL))
	|| (data_size == 0))
		return SYS_ERROR;

	TXRX_Type txrx_type = TXRX_Type::TXRX;

	if((tx_data != NULL)
	&& (rx_data == NULL))
		txrx_type = TXRX_Type::TX;
	else
	if((tx_data == NULL)
	&& (rx_data != NULL))
		txrx_type = TXRX_Type::RX;

	if(buffer.tx_buffer_size_max == 0)
	{
		return TXRX(tx_data, rx_data, data_size, txrx_type);
	}
	else
	{
		if(buffer.tx_buffer_size_max == buffer.tx.size())
			return SYS_ERROR;

		buffer.tx.emplace_back();
		rxtx_data_typedef *tmp = &buffer.tx.back();
		tmp->size = data_size;
		tmp->type = txrx_type;
		if(txrx_type != TXRX_Type::RX)
		{
			tmp->tx_data_ptr = new uint8_t[data_size];
			if(tmp->tx_data_ptr == 0)
				return SYS_ERROR;

			memcpy(tmp->tx_data_ptr, tx_data, data_size);
		}
		if(txrx_type != TXRX_Type::TX)
		{
			tmp->data_ptr = new uint8_t[data_size];
			if(tmp->data_ptr == 0)
				return SYS_ERROR;
		}

		if((buffer.tx.size() == 1)
		&& (status == SYS_OK))
		{
			tmp = &buffer.tx.front();
			return TXRX(tmp->tx_data_ptr, tmp->data_ptr, tmp->size, tmp->type);
		}
	}
	return SYS_OK;
};

SYS_StatusTypeDef Interface_SPI::Send(uint8_t* tx_data,uint16_t data_size)
{
	return Send_Receive(tx_data, NULL, data_size);
};

SYS_StatusTypeDef Interface_SPI::Receive(uint8_t* rx_data, uint16_t data_size)
{
	return Send_Receive(NULL, rx_data, data_size);
};

void Interface_SPI::IRQHandler()
{
	spi->Disable();
	spi->DMA_TX(DISABLE);
	spi->DMA_RX(DISABLE);

	spi->SlaveSelect(DISABLE);

	dma_rx.Disable_IRQ(DMA_Sx::IRQ::TC);

	IsDataReceived = true;

	status = SYS_OK;

	if(buffer.tx_buffer_size_max != 0)
	{
		if(buffer.tx.front().type != TXRX_Type::TX)
		{
			buffer.rx.push_back(buffer.tx.front());
			delete [] buffer.tx.front().data_ptr;
		}

		if(buffer.tx.front().type != TXRX_Type::RX)
		{
			delete [] buffer.tx.front().tx_data_ptr;
		}

		buffer.tx.pop_front();

		if(!buffer.tx.empty())
		{
			std::list<rxtx_data_typedef>::iterator iter = buffer.tx.begin();
			status = TXRX(iter->tx_data_ptr, iter->data_ptr, iter->size, iter->type);
		}
	}
};

#if defined(STM32F7)
Interface_I2C::Interface_I2C(I2C *_i2c, DMA_Stream_TypeDef *_dma_tx, DMA_Stream_TypeDef *_dma_rx) :
	Interface_DMA(),
	i2c(_i2c)	
{
	uint32_t ch;

	if(i2c->I2Cx == I2C1)
	{
		ch = 1;
	}
	else if(i2c->I2Cx == I2C2)
	{
		ch = 7;
	}
	else if(i2c->I2Cx == I2C3)
	{
		ch = 3;
		if(dma_rx.DMA_Stream_X == DMA1_Stream1)
			ch = 1;
	}

	dma_tx = new DMA_Sx(_dma_tx, 
				ch,
				reinterpret_cast<uint32_t>(&i2c->I2Cx->TXDR),
				DMA_Sx::Per_Type::i2c,
				DMA_Sx::DIR::To_Per);
	dma_rx = new DMA_Sx(_dma_rx, 
				ch,
				reinterpret_cast<uint32_t>(&i2c->I2Cx->RXDR),
				DMA_Sx::Per_Type::i2c,
				DMA_Sx::DIR::From_Per);
};

SYS_StatusTypeDef Interface_I2C::Init()
{
	status = i2c->SetUp();
	
	i2c->I2Cx->CR1 |= I2C_CR1_TXDMAEN | I2C_CR1_RXDMAEN;
	i2c->Enable_IRQ(I2C::IRQ::STOP);
	i2c->Enable_IRQ(I2C::IRQ::TC);

	DMA_SetUp();
	dma_tx.MINC(ENABLE);
	dma_rx.MINC(ENABLE);

	return status;
};

void Interface_I2C::IRQHandler()
{
	if(i2c->I2Cx->ISR & I2C_ISR_STOPF)
	{
		while(!(i2c->I2Cx->ISR & I2C_ISR_STOPF)){};
		i2c->I2Cx->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF;
		i2c->I2Cx->CR2 = 0;

		transfer_count = 0;
		_slave_addr = 0;
		data_addr = 0;
		need_reload_dma = 0;
		txrx = TXRX_Type::none;
		status = SYS_OK;
	}

	if((i2c->I2Cx->ISR & I2C_ISR_TCR)
	|| (i2c->I2Cx->ISR & I2C_ISR_TC))
	{
		uint32_t mode = _slave_addr;

		if(need_reload_dma)
		{
			need_reload_dma = false;

			if(txrx == TXRX_Type::TX)
			{
				dma_tx.ClearFlags();
				dma_tx.DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(data_addr);
				dma_tx.DMA_Stream_X->NDTR = transfer_count;
				dma_tx.Stream(ENABLE);
			}
			else
			{
				dma_rx.ClearFlags();
				dma_rx.DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(data_addr);
				dma_rx.DMA_Stream_X->NDTR = transfer_count;
				dma_rx.Stream(ENABLE);

				mode |= I2C_CR2_START |
						I2C_CR2_RD_WRN;
			}
		}
		else
		{
			transfer_count -= I2C::MAX_NBYTE_SIZE;
		}

		if(transfer_count > I2C::MAX_NBYTE_SIZE)
		{

			mode |= I2C_CR2_RELOAD |
					I2C::MAX_NBYTE_SIZE << I2C_CR2_NBYTES_Pos;
		}
		else
		{
			mode |= I2C_CR2_AUTOEND |
					transfer_count << I2C_CR2_NBYTES_Pos;
		}
		i2c->I2Cx->CR2 = mode;
	}
};

void Interface_I2C::Send(uint8_t slave_addr, uint8_t* data, uint16_t data_len)
{
	if(status != SYS_OK)
		return;
	status = SYS_BUSY;
	transfer_count = data_len;
	_slave_addr = slave_addr;
	need_reload_dma = false;

	uint32_t mode = _slave_addr | I2C_CR2_START;
	txrx = TXRX_Type::TX;

	if(transfer_count > I2C::MAX_NBYTE_SIZE)
	{
		mode |= I2C_CR2_RELOAD |
			 	I2C::MAX_NBYTE_SIZE << I2C_CR2_NBYTES_Pos;
	}
	else
	{
		mode |= I2C_CR2_AUTOEND |
			 	transfer_count << I2C_CR2_NBYTES_Pos;
	}

	dma_tx.ClearFlags();
	dma_tx.DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(data);
	dma_tx.DMA_Stream_X->NDTR = data_len;
	dma_tx.Stream(ENABLE);

	i2c->I2Cx->CR2 = mode;
}

void Interface_I2C::SendToAddr(uint8_t slave_addr, uint8_t* addr, uint8_t addr_size, uint8_t* data, uint16_t data_len)
{
	if((status != SYS_OK)
	|| (addr_size > 4)
	|| (addr_size < 0))
		return;
	status = SYS_BUSY;
	transfer_count = data_len;
	_slave_addr = slave_addr;
	data_addr = data;
	need_reload_dma = true;

	uint32_t mode = _slave_addr | I2C_CR2_START;
	txrx = TXRX_Type::TX;

	mode |= I2C_CR2_RELOAD |
			addr_size << I2C_CR2_NBYTES_Pos;

	dma_tx.ClearFlags();
	dma_tx.DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(addr);
	dma_tx.DMA_Stream_X->NDTR = addr_size;
	dma_tx.Stream(ENABLE);

	i2c->I2Cx->CR2 = mode;
}

void Interface_I2C::Receive(uint8_t slave_addr, uint8_t* data, uint16_t data_len)
{
	if(status != SYS_OK)
		return;
	status = SYS_BUSY;
	transfer_count = data_len;
	_slave_addr = slave_addr;
	need_reload_dma = false;

	uint32_t mode = _slave_addr | I2C_CR2_START | I2C_CR2_RD_WRN;
	txrx = TXRX_Type::RX;

	if(transfer_count > I2C::MAX_NBYTE_SIZE)
	{
		mode |= I2C_CR2_RELOAD |
			 	I2C::MAX_NBYTE_SIZE << I2C_CR2_NBYTES_Pos;
	}
	else
	{
		mode |= I2C_CR2_AUTOEND |
			 	transfer_count << I2C_CR2_NBYTES_Pos;
	}

	dma_rx.ClearFlags();
	dma_rx.DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(data);
	dma_rx.DMA_Stream_X->NDTR = data_len;
	dma_rx.Stream(ENABLE);

	i2c->I2Cx->CR2 = mode;
}

void Interface_I2C::ReceiveFromAddr(uint8_t slave_addr, uint8_t* addr, uint8_t addr_size, uint8_t* data, uint16_t data_len)
{
	if((status != SYS_OK)
	|| (addr_size > 4)
	|| (addr_size < 0))
		return;
	status = SYS_BUSY;
	transfer_count = data_len;
	_slave_addr = slave_addr;
	data_addr = data;
	need_reload_dma = true;

	uint32_t mode = _slave_addr | I2C_CR2_START;
	txrx = TXRX_Type::RX;

	mode |= addr_size << I2C_CR2_NBYTES_Pos;

	dma_tx.ClearFlags();
	dma_tx.DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(addr);
	dma_tx.DMA_Stream_X->NDTR = addr_size;
	dma_tx.Stream(ENABLE);

	i2c->I2Cx->CR2 = mode;
}

Interface_buffer_I2C::Interface_buffer_I2C(I2C *_i2c, DMA_Stream_TypeDef *_dma_tx, DMA_Stream_TypeDef *_dma_rx) :
	Interface_buffer(),
	Interface_I2C(_i2c, _dma_tx, _dma_rx)
{

};

void Interface_buffer_I2C::IRQHandler(void)
{
	Interface_I2C::IRQHandler();
	if(status != SYS_BUSY)
	{
		if((tx.front().type == TXRX_Type::TX)
		|| (tx.front().type == TXRX_Type::TX_reg))
		{
			delete [] tx.front().data_ptr;
			delete [] tx.front().reg_addr_ptr;
			tx.erase(tx.begin());
		} else
		if((tx.front().type == TXRX_Type::RX)
		|| (tx.front().type == TXRX_Type::RX_reg))
		{
			rx.push_back(tx.front());

			delete [] tx.front().data_ptr;
			delete [] tx.front().reg_addr_ptr;
			tx.erase(tx.begin());
		}
	}
	if(tx.size() > 0)
		StartTranssmit();
};

void Interface_buffer_I2C::StartTranssmit()
{
	if(tx.size() == 0)
		return;

	if(tx.front().type == TXRX_Type::TX)
	{
		Interface_I2C::Send(tx.front().slave_addr, tx.front().data_ptr, tx.front().data_len);
	} else
	if(tx.front().type == TXRX_Type::TX_reg)
	{
		Interface_I2C::SendToAddr(tx.front().slave_addr, tx.front().reg_addr_ptr, tx.front().reg_addr_len, tx.front().data_ptr, tx.front().data_len);
	} else
	if(tx.front().type == TXRX_Type::RX)
	{
		Interface_I2C::Receive(tx.front().slave_addr, tx.front().data_ptr, tx.front().data_len);
	} else
	if(tx.front().type == TXRX_Type::RX_reg)
	{
		Interface_I2C::ReceiveFromAddr(tx.front().slave_addr, tx.front().reg_addr_ptr, tx.front().reg_addr_len, tx.front().data_ptr, tx.front().data_len);
	}
};

void Interface_buffer_I2C::Send(uint8_t slave_addr, uint8_t* data, uint16_t data_len)
{
	Data_Typedef tmp_data = {
			.slave_addr = slave_addr,
			.use_reg_addr = false,
			.data_len = data_len,
			.type = TXRX_Type::TX
	};

	tx.push_back(tmp_data);

	tx.back().data_ptr = new uint8_t[data_len];

	memcpy(tx.back().data_ptr, data, data_len);

	if(tx.size() == 1)
	{
		StartTranssmit();
	}
};

void Interface_buffer_I2C::SendToAddr(uint8_t slave_addr, uint8_t* addr, uint8_t addr_size, uint8_t* data, uint16_t data_len)
{
	Data_Typedef tmp_data = {
			.slave_addr = slave_addr,
			.use_reg_addr = true,
			.reg_addr_len = addr_size,
			.data_len = data_len,
			.type = TXRX_Type::TX_reg
	};

	tx.push_back(tmp_data);

	tx.back().data_ptr 		= new uint8_t[data_len];
	tx.back().reg_addr_ptr	= new uint8_t[addr_size];

	memcpy(tx.back().data_ptr, data, data_len);
	memcpy(tx.back().reg_addr_ptr, addr, addr_size);

	if(tx.size() == 1)
	{
		StartTranssmit();
	}
};

void Interface_buffer_I2C::Receive(uint8_t slave_addr, uint8_t* data, uint16_t data_len)
{
	Data_Typedef tmp_data = {
			.slave_addr = slave_addr,
			.use_reg_addr = false,
			.data_len = data_len,
			.type = TXRX_Type::RX
	};

	tx.push_back(tmp_data);

	tx.back().data_ptr = new uint8_t[data_len];

	// memcpy(tx.back().data_ptr, data, data_len);

	if(tx.size() == 1)
	{
		StartTranssmit();
	}
};

void Interface_buffer_I2C::ReceiveFromAddr(uint8_t slave_addr, uint8_t* addr, uint8_t addr_size, uint8_t* data, uint16_t data_len)
{
	Data_Typedef tmp_data = {
			.slave_addr = slave_addr,
			.use_reg_addr = true,
			.reg_addr_len = addr_size,
			.data_len = data_len,
			.type = TXRX_Type::RX_reg
	};

	tx.push_back(tmp_data);

	tx.back().data_ptr = new uint8_t[data_len];

	// memcpy(tx.back().data_ptr, data, data_len);

	if(tx.size() == 1)
	{
		StartTranssmit();
	}
};

#endif
