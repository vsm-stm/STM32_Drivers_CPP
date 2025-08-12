#include <interface.hpp>

SysInitStatus Interface_DMA::DMA_SetUp()
{
	SysInitStatus status;
	status = dma_tx.SetUp(tx_settings);
	if(status == SysInitStatus::InitOK)
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
									usart(_usart)//,
									// buffer(tx_buffer_size, rx_buffer_size)
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

	status_tx = SysStatus::NotInit;
	status_rx = SysStatus::NotInit;

};

SysInitStatus Interface_USART::Init()
{
	usart->SetUp();
	usart->DMA(ENABLE);
	DMA_SetUp();
	if(dma_tx.DMA_Stream_X != NULL)
		status_tx = SysStatus::OK;
	if(dma_rx.DMA_Stream_X != NULL)
		status_rx = SysStatus::OK;

	usart->EnableNVIC_IRQ();

	return SysInitStatus::InitOK;
};

SysStatus Interface_USART::Send(uint8_t* data, uint16_t data_size)
{
	if((data == NULL)
	|| (data_size == 0))
		return SysStatus::Error;

	if(status_tx == SysStatus::OK)
	{
		status_tx = SysStatus::Busy;

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

SysStatus Interface_USART::Receive(uint8_t* data, uint16_t data_size, bool cont)
{
	ContReceive = cont;
	if(data == NULL)
		return SysStatus::Error;
	return Receive(data, data_size);
};

SysStatus Interface_USART::Receive(uint8_t* data, uint16_t data_size)
{
	if(status_rx == SysStatus::OK)
	{
		status_rx = SysStatus::Busy;
		Count_To_Receive = data_size;

		dma_rx.SetMemAddr(reinterpret_cast<uint32_t>(data), data_size);
		usart->Enable_IRQ(USART::IRQ::IDLE);
		dma_rx.ClearFlags();
		dma_rx.Stream(ENABLE);
	}
	return status_rx;
};

// SYS_StatusTypeDef Interface_USART::Receive(uint16_t data_size, bool cont)
// {
// 	if(buffer.rx_buffer_size_max == 0)
// 		return SYS_ERROR;
// 	else
// 		return Receive(NULL, data_size, cont);
// };

// SYS_StatusTypeDef Interface_USART::Receive(uint16_t data_size)
// {
// 	if(buffer.rx_buffer_size_max == 0)
// 		return SYS_ERROR;
// 	else
// 		return Receive(NULL, data_size, false);
// };

void Interface_USART::Stop_Receive()
{
	dma_rx.Stream(DISABLE);
	usart->Disable_IRQ(USART::IRQ::IDLE);
	status_rx = SysStatus::OK;
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
		status_tx = SysStatus::OK;

		// if(buffer.tx_buffer_size_max != 0)
		// {
			
		// 	data_typedef *tmp = &buffer.tx.front();
		// 	if(buffer.tx.size() != 0)
		// 	{
				
		// 		delete [] tmp->data_ptr;
		// 		buffer.tx.pop_front();
		// 	}

		// 	if(!buffer.tx.empty())
		// 	{
		// 		tmp = &buffer.tx.front();
		// 		status_tx = TX(tmp->data_ptr, tmp->size);
		// 	}
		// }
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
		Receive_Count = Count_To_Receive - dma_rx.DMA_Stream_X->NDTR;
		dma_rx.Stream(DISABLE);

#if defined(STM32F4)
		usart->USARTx->DR;
#elif defined(STM32F7)
		usart->USARTx->ICR = USART_ICR_IDLECF;
#endif

		status_rx = SysStatus::OK;

		// if(buffer.rx_buffer_size_max != 0)
		// {
		// 	buffer.rx.back().size -= dma_rx.DMA_Stream_X->NDTR;
		// }

		// if(ContReceive)
		// {
		// 	if(buffer.rx_buffer_size_max == 0)
		// 	{
		// 		dma_rx.ClearFlags();
		// 		dma_rx.Stream(ENABLE);
		// 		status_rx = SYS_BUSY;
		// 	}
		// 	else
		// 	{
		// 		status_rx = Receive(buffer.rx.back().size, ContReceive);
		// 	}
		// }
		// else
		if(!ContReceive)
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
	uint32_t tx_buffer_size,
	uint32_t rx_buffer_size) :
								Interface_DMA(_dma_tx, _dma_rx),
								spi(_spi),
								spi_init_data(_init_data)//,
								// buffer(tx_buffer_size, rx_buffer_size)
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

SysInitStatus Interface_SPI::Init()
{
	SysInitStatus status = spi->SetUp(spi_init_data);
#if defined(STM32F7)
	spi->SPIx->CR2 |= SPI_CR2_LDMARX | SPI_CR2_LDMATX;
#endif
	if(status == SysInitStatus::InitOK)
	{
		status = DMA_SetUp();
	}

	return status;
};

SysStatus Interface_SPI::Send_Receive(uint8_t* tx_data, uint8_t* rx_data, uint16_t data_size)
{
	if(((tx_data == NULL)
	 && (rx_data == NULL))
	|| (data_size == 0))
		return SysStatus::Error;

	TXRX_Type txrx_type = TXRX_Type::TXRX;

	if((tx_data != NULL)
	&& (rx_data == NULL))
		txrx_type = TXRX_Type::TX;
	else
	if((tx_data == NULL)
	&& (rx_data != NULL))
		txrx_type = TXRX_Type::RX;

	// if(buffer.tx_buffer_size_max == 0)
	// {
		return TXRX(tx_data, rx_data, data_size, txrx_type);
	// }
	// else
	// {
	// 	if(buffer.tx.size() >= buffer.tx_buffer_size_max)
	// 		return SYS_ERROR;

	// 	buffer.tx.emplace_back();
	// 	rxtx_data_typedef *tmp = &buffer.tx.back();
	// 	tmp->size = data_size;
	// 	tmp->type = txrx_type;
	// 	if(txrx_type != TXRX_Type::RX)
	// 	{
	// 		tmp->tx_data_ptr = new uint8_t[data_size];
	// 		if(tmp->tx_data_ptr == 0)
	// 			return SYS_ERROR;

	// 		memcpy(tmp->tx_data_ptr, tx_data, data_size);
	// 	}
	// 	if(txrx_type != TXRX_Type::TX)
	// 	{
	// 		if(buffer.rx_buffer_size_max == 0)
	// 		{
	// 			tmp->rx_data_ptr = new uint8_t[data_size];
	// 			if(tmp->rx_data_ptr == 0)
	// 				return SYS_ERROR;
	// 		}
	// 		else
	// 		{
	// 			tmp->rx_data_ptr = rx_data;
	// 		}
	// 	}

	// 	if((buffer.tx.size() == 1)
	// 	&& (status == SYS_OK))
	// 	{
	// 		tmp = &buffer.tx.front();
	// 		return TXRX(tmp->tx_data_ptr, tmp->rx_data_ptr, tmp->size, tmp->type);
	// 	}
	// }
	return SysStatus::OK;
};

SysStatus Interface_SPI::Send(uint8_t* tx_data,uint16_t data_size)
{
	return Send_Receive(tx_data, NULL, data_size);
};

SysStatus Interface_SPI::Receive(uint8_t* rx_data, uint16_t data_size)
{
	return Send_Receive(NULL, rx_data, data_size);
};

SysStatus Interface_SPI::TXRX(uint8_t* tx_data, uint8_t* rx_data, uint16_t size, TXRX_Type type)
{
	if(status == SysStatus::OK)
	{
		status = SysStatus::Busy;

		dma_tx.ClearFlags();
		dma_rx.ClearFlags();

		if(type == TXRX_Type::RX)
		{
			tmp_data[0] = 0;
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

		dma_rx.Stream(ENABLE);

		spi->DMA_RX(ENABLE);

		dma_tx.Stream(ENABLE);

		spi->Enable();

		spi->DMA_TX(ENABLE);
	}
	return status;
}

void Interface_SPI::IRQHandler()
{
	spi->Disable();
	spi->DMA_TX(DISABLE);
	spi->DMA_RX(DISABLE);

	spi->SlaveSelect(DISABLE);

	dma_rx.Disable_IRQ(DMA_Sx::IRQ::TC);

	IsDataReceived = true;

	status = SysStatus::OK;

	// if(buffer.tx_buffer_size_max != 0)
	// {
	// 	rxtx_data_typedef *tmp = &buffer.tx.front();
	// 	if(((tmp->type == TXRX_Type::RX)
	// 	 || (tmp->type == TXRX_Type::TXRX))
	// 	&& (buffer.rx.size() < buffer.rx_buffer_size_max))
	// 	{
	// 		buffer.rx.push_back(*tmp);
	// 		if(buffer.rx.size() == buffer.rx_buffer_size_max)
	// 			buffer.rx_buffer_full = true;
	// 	}
	// 	else
	// 	{
	// 		if((tmp->type == TXRX_Type::TXRX)
	// 		|| (tmp->type == TXRX_Type::TX))
	// 			delete [] buffer.tx.front().tx_data_ptr;
	// 		if((tmp->type == TXRX_Type::TXRX)
	// 		|| (tmp->type == TXRX_Type::RX))
	// 			delete [] buffer.tx.front().rx_data_ptr;
	// 	}

	// 	buffer.tx.pop_front();

	// 	if(!buffer.tx.empty())
	// 	{
	// 		rxtx_data_typedef *tmp = &buffer.tx.front();
	// 		status = TXRX(tmp->tx_data_ptr, tmp->rx_data_ptr, tmp->size, tmp->type);
	// 	}
	// }
};


//#if defined(STM32F7) 
#if defined(STM32F8) // todo F7 fix
Interface_I2C::Interface_I2C(
		I2C *_i2c,
		DMA_Stream_TypeDef *_dma_tx,
		DMA_Stream_TypeDef *_dma_rx,
		uint32_t buffer_size) :
									Interface_DMA(_dma_tx, _dma_rx),
									i2c(_i2c),
									buffer(buffer_size, buffer_size)
{
	tx_settings.peripheral_type = DMA_Sx::Per_Type::i2c;
	rx_settings.peripheral_type = DMA_Sx::Per_Type::i2c;
	tx_settings.direction = DMA_Sx::DIR::To_Per;
	rx_settings.direction = DMA_Sx::DIR::From_Per;

#if defined(STM32F4)
	tx_settings.peripheral_address = reinterpret_cast<uint32_t>(&i2c->I2Cx->DR);
	rx_settings.peripheral_address = reinterpret_cast<uint32_t>(&i2c->I2Cx->DR);
#elif defined(STM32F7)
	tx_settings.peripheral_address = reinterpret_cast<uint32_t>(&i2c->I2Cx->TXDR);
	rx_settings.peripheral_address = reinterpret_cast<uint32_t>(&i2c->I2Cx->RXDR);
#endif

	if(i2c->I2Cx == I2C1)
	{
		tx_settings.channel = 1;
		rx_settings.channel = 1;
	}
	else if(i2c->I2Cx == I2C2)
	{
		tx_settings.channel = 7;
		rx_settings.channel = 7;
	}
	else if(i2c->I2Cx == I2C3)
	{
		tx_settings.channel = 3;
		rx_settings.channel = 3;
		if(dma_rx.DMA_Stream_X == DMA1_Stream1)
		{
			tx_settings.channel = 1;
			rx_settings.channel = 1;
		}
	}
};

SYS_StatusTypeDef Interface_I2C::Init()
{
	status = i2c->SetUp();
	if(status != SYS_OK)
		return status;
	
	i2c->I2Cx->CR1 |= I2C_CR1_TXDMAEN | I2C_CR1_RXDMAEN;
	i2c->Enable_IRQ(I2C::IRQ::STOP);
	i2c->Enable_IRQ(I2C::IRQ::TC);
	i2c->Enable_IRQ(I2C::IRQ::NACK);

	status = DMA_SetUp();
	dma_tx.MINC(ENABLE);
	dma_rx.MINC(ENABLE);

	return status;
};

SYS_StatusTypeDef Interface_I2C::Send(uint8_t slave_addr, uint8_t* data, uint16_t data_size)
{
	return Send_Receive(slave_addr, NULL, 0, data, data_size, TXRX_Type::TX);
};

SYS_StatusTypeDef Interface_I2C::Send(uint8_t slave_addr, uint8_t* reg_addr, uint8_t reg_addr_size, uint8_t* data, uint16_t data_size)
{
	return Send_Receive(slave_addr, reg_addr, reg_addr_size, data, data_size, TXRX_Type::TX);
};

SYS_StatusTypeDef Interface_I2C::Receive(uint8_t slave_addr, uint8_t* data, uint16_t data_size)
{
	return Send_Receive(slave_addr, NULL, 0, data, data_size, TXRX_Type::RX);
};

SYS_StatusTypeDef Interface_I2C::Receive(uint8_t slave_addr, uint8_t* reg_addr, uint8_t reg_addr_size, uint8_t* data, uint16_t data_size)
{
	return Send_Receive(slave_addr, reg_addr, reg_addr_size, data, data_size, TXRX_Type::RX);
};

SYS_StatusTypeDef Interface_I2C::Send_Receive(uint8_t slave_addr, uint8_t* reg_addr, uint8_t reg_addr_size, uint8_t* data, uint16_t data_size, TXRX_Type type)
{
	if((reg_addr_size > 4)
	|| (data == NULL)
	|| (data_size == 0)
	|| (slave_addr == 0)
	|| ((reg_addr != NULL)
		&& (reg_addr_size == 0)))
		return SYS_ERROR;

	if(buffer.tx_buffer_size_max == 0)
	{
		return TXRX(slave_addr, reg_addr, reg_addr_size, data, data_size, type);
	}
	else
	{
		if(buffer.tx.size() >= buffer.tx_buffer_size_max)
			return SYS_ERROR;

		buffer.tx.emplace_back();
		data_typedef *tmp = &buffer.tx.back();
		tmp->slave_addr = slave_addr;
		tmp->reg_addr_ptr = reg_addr;
		tmp->reg_addr_size = reg_addr_size;
		tmp->data_ptr = data;
		tmp->size = data_size;
		tmp->type = type;

		if(reg_addr != NULL)
		{
			tmp->reg_addr_ptr = new uint8_t[reg_addr_size];
			if(tmp->reg_addr_ptr == 0)
				return SYS_ERROR;
			memcpy(tmp->reg_addr_ptr, reg_addr, data_size);
		}

		tmp->data_ptr = new uint8_t[data_size];
		if(tmp->data_ptr == 0)
				return SYS_ERROR;
		if(type == TXRX_Type::TX)
			memcpy(tmp->data_ptr, data, data_size);


		if((buffer.tx.size() == 1)
		&& (status == SYS_OK))
		{
			tmp = &buffer.tx.front();
			return TXRX(tmp->slave_addr, tmp->reg_addr_ptr, tmp->reg_addr_size, tmp->data_ptr, tmp->size, tmp->type);
		}
	}
	
	return status;
};

SYS_StatusTypeDef Interface_I2C::TXRX(uint8_t slave_addr, uint8_t* reg_addr, uint8_t reg_addr_size, uint8_t* data, uint16_t data_size, TXRX_Type type)
{
	if(status == SYS_OK)
	{
		status = SYS_BUSY;

		transfer_count = data_size;
		_slave_addr = slave_addr;
		data_addr = data;

		uint32_t mode = _slave_addr | I2C_CR2_START;
		txrx = type;
		DMA_Sx *req_dma;
		if((txrx == TXRX_Type::RX)
		&& (reg_addr == NULL))
			req_dma = &dma_rx;
		else
			req_dma = &dma_tx;

		if(reg_addr == NULL)
		{
			need_reload_dma = false;

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

			if(txrx == TXRX_Type::RX)
				mode |= I2C_CR2_RD_WRN;

			req_dma->SetMemAddr(reinterpret_cast<uint32_t>(data), data_size);
			
		}
		else
		{
			need_reload_dma = true;

			mode |= reg_addr_size << I2C_CR2_NBYTES_Pos;

			if(txrx == TXRX_Type::TX)
				mode |= I2C_CR2_RELOAD;

			req_dma->SetMemAddr(reinterpret_cast<uint32_t>(reg_addr), reg_addr_size);
		}

		req_dma->ClearFlags();
		req_dma->Stream(ENABLE);

		i2c->I2Cx->CR2 = mode;
	}

	return status;
}

void Interface_I2C::IRQHandler()
{
	if(i2c->I2Cx->ISR & I2C_ISR_STOPF)
	{
		while(!(i2c->I2Cx->ISR & I2C_ISR_STOPF)){};
		i2c->I2Cx->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF | I2C_ICR_ARLOCF;
		i2c->I2Cx->CR2 = 0;

		dma_tx.Stream(DISABLE);
		dma_rx.Stream(DISABLE);

		transfer_count = 0;
		_slave_addr = 0;
		data_addr = 0;
		need_reload_dma = 0;
		txrx = TXRX_Type::none;
		status = SYS_OK;

		if(buffer.tx_buffer_size_max != 0)
		{
			if((buffer.tx.front().type == TXRX_Type::RX)
			&& (buffer.rx.size() < buffer.rx_buffer_size_max))
			{
				buffer.rx.push_back(buffer.tx.front());
				if(buffer.rx.size() == buffer.rx_buffer_size_max)
					buffer.rx_buffer_full = true; 
			}
			else
			{
				if(buffer.tx.front().reg_addr_ptr != NULL)
					delete [] buffer.tx.front().reg_addr_ptr;
				delete [] buffer.tx.front().data_ptr;
			}

			buffer.tx.pop_front();

			if(!buffer.tx.empty())
			{
				data_typedef *tmp = &buffer.tx.front();
				status = TXRX(tmp->slave_addr, tmp->reg_addr_ptr, tmp->reg_addr_size, tmp->data_ptr, tmp->size, tmp->type);
			}

		}
	}
	else
	if(i2c->I2Cx->ISR & I2C_ISR_NACKF)
	{
		i2c->I2Cx->ICR = I2C_ICR_NACKCF;
		i2c->I2Cx->CR2 = 0;
		i2c->I2Cx->CR1 &= ~I2C_CR1_PE;
		i2c->I2Cx->CR1 |= I2C_CR1_PE;

		dma_tx.Stream(DISABLE);
		dma_rx.Stream(DISABLE);
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
				dma_tx.SetMemAddr(reinterpret_cast<uint32_t>(data_addr), transfer_count);
				dma_tx.Stream(ENABLE);
			}
			else
			{
				dma_rx.ClearFlags();
				dma_rx.SetMemAddr(reinterpret_cast<uint32_t>(data_addr), transfer_count);
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

#endif
