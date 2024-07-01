#include <i2c.hpp>

#if defined(STM32F7)

SYS_StatusTypeDef I2C::SetHard()
{
	if((i2c_speed == 0)
	|| (SCL.PORT == 0)
	|| (SDA.PORT == 0))
		return SYS_ERROR;

	if(I2Cx == I2C1)
	{
		RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
		RCC->APB1RSTR |= RCC_APB1RSTR_I2C1RST;
		RCC->APB1RSTR &= ~RCC_APB1RSTR_I2C1RST;
		bus_clk = System::APB1BusClock;
		IRQ_vector_EV = I2C1_EV_IRQn;
		IRQ_vector_ER = I2C1_ER_IRQn;
	} else
	if(I2Cx == I2C2)
	{
		RCC->APB1ENR |= RCC_APB1ENR_I2C2EN;
		RCC->APB1RSTR |= RCC_APB1RSTR_I2C2RST;
		RCC->APB1RSTR &= ~RCC_APB1RSTR_I2C2RST;
		bus_clk = System::APB1BusClock;
		IRQ_vector_EV = I2C2_EV_IRQn;
		IRQ_vector_ER = I2C2_ER_IRQn;
	} else
	if(I2Cx == I2C3)
	{
		RCC->APB1ENR |= RCC_APB1ENR_I2C3EN;
		RCC->APB1RSTR |= RCC_APB1RSTR_I2C3RST;
		RCC->APB1RSTR &= ~RCC_APB1RSTR_I2C3RST;
		bus_clk = System::APB1BusClock;
		IRQ_vector_EV = I2C3_EV_IRQn;
		IRQ_vector_ER = I2C3_ER_IRQn;
	} else
	{
		return SYS_ERROR;
	}

	af = 4;

	SCL.SetUp(PIN::TYPE::AF_OD_PulUp, PIN::OUTPUT_SPEED::High, af);
	SDA.SetUp(PIN::TYPE::AF_OD_PulUp, PIN::OUTPUT_SPEED::High, af);

	I2Cx->CR1 = 0;
	uint32_t timing = I2C_GetTiming(bus_clk, i2c_speed);
	I2Cx->TIMINGR = timing;
	I2Cx->CR1 = I2C_CR1_PE;

	return SYS_OK;
};

SYS_StatusTypeDef I2C::SetUp()
{
	return SetHard();
};

void I2C::error_stop()
{
	I2Cx->CR2 |= I2C_CR2_STOP;
	normal_stop();
};

void I2C::normal_stop()
{
	while(!(I2Cx->ISR & I2C_ISR_STOPF)){};

	I2Cx->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF;
	I2Cx->CR2 = 0;
};

SYS_StatusTypeDef I2C::Send(uint8_t slave_addr, uint8_t *data, uint32_t len, uint32_t timeout)
{
	uint32_t transfer_count = len / MAX_NBYTE_SIZE;
	uint32_t current_transfer_count = 0;
	uint32_t start = I2C_CR2_START;
	uint32_t mode;
	uint32_t tick_start = System::GetTick();

	for(int32_t transfer = transfer_count; transfer >= 0; transfer--)
	{
		mode = slave_addr | start;
		if(transfer == 0)
		{
			current_transfer_count = len - transfer_count*MAX_NBYTE_SIZE;
			mode |= I2C_CR2_AUTOEND;
		}
		else
		{
			current_transfer_count = MAX_NBYTE_SIZE;
			mode |= I2C_CR2_RELOAD;
		}
		mode |= current_transfer_count << I2C_CR2_NBYTES_Pos;
		if(!start)
			while(!(I2Cx->ISR & I2C_ISR_TCR))
			{
				if(System::GetTick() - tick_start > timeout)
				{
					error_stop();
					return SYS_ERROR;
				}
			};
		I2Cx->CR2 = mode;
		
		start = 0;
		
		for(uint8_t current_transfer = 0; current_transfer < current_transfer_count; current_transfer++)
		{
			while(!(I2Cx->ISR & I2C_ISR_TXIS))
			{
				if((I2Cx->ISR & I2C_ISR_NACKF)
				|| (System::GetTick() - tick_start > timeout))
				{
					error_stop();
					return SYS_ERROR;
				}
			};
			I2Cx->TXDR = data[current_transfer + (transfer_count-transfer)*MAX_NBYTE_SIZE];
		}
	}

	while(!(I2Cx->ISR & I2C_ISR_STOPF))
	{
		if(System::GetTick() - tick_start > timeout)
		{
			error_stop();
			return SYS_ERROR;
		}
	};

	I2Cx->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF;
	I2Cx->CR2 = 0;
	return SYS_OK;
};


SYS_StatusTypeDef I2C::Receive(uint8_t slave_addr, uint8_t *data, uint32_t len, uint32_t timeout)
{
	uint32_t transfer_count = len / MAX_NBYTE_SIZE;
	uint32_t current_transfer_count = 0;
	uint32_t start = I2C_CR2_START;
	uint32_t mode;
	uint32_t tick_start = System::GetTick();

	for(int32_t transfer = transfer_count; transfer >= 0; transfer--)
	{
		mode = slave_addr | start | I2C_CR2_RD_WRN;
		if(transfer == 0)
		{
			current_transfer_count = len - transfer_count*MAX_NBYTE_SIZE;
			mode |= I2C_CR2_AUTOEND;
		}
		else
		{
			current_transfer_count = MAX_NBYTE_SIZE;
			mode |= I2C_CR2_RELOAD;
		}
		mode |= current_transfer_count << I2C_CR2_NBYTES_Pos;
		I2Cx->CR2 = mode;
		start = 0;
		
		for(uint8_t current_transfer = 0; current_transfer < current_transfer_count; current_transfer++)
		{
			while(!(I2Cx->ISR & I2C_ISR_RXNE))
			{
				if((I2Cx->ISR & I2C_ISR_NACKF)
				|| (System::GetTick() - tick_start > timeout))
				{
					error_stop();
					return SYS_ERROR;
				}
			};
			data[current_transfer + (transfer_count-transfer)*MAX_NBYTE_SIZE] = I2Cx->RXDR;
		}
	}

	while(!(I2Cx->ISR & I2C_ISR_STOPF))
	{
		if(System::GetTick() - tick_start > timeout)
		{
			error_stop();
			return SYS_ERROR;
		}
	};

	I2Cx->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF;
	I2Cx->CR2 = 0;
	return SYS_OK;
}

SYS_StatusTypeDef I2C::ReceiveFromAddr(uint8_t slave_addr, uint8_t *addr, uint8_t addr_len, uint8_t *data, uint32_t len, uint32_t timeout)
{
	if(addr_len > 4)
		return SYS_ERROR;

	uint32_t tick_start = System::GetTick();

	I2Cx->CR2 = slave_addr |
				addr_len << I2C_CR2_NBYTES_Pos |
				I2C_CR2_START;

	for(uint8_t transfer = 0; transfer<addr_len; transfer++)
	{
		while(!(I2C1->ISR & I2C_ISR_TXIS))
		{
			if((I2Cx->ISR & I2C_ISR_NACKF)
			|| (System::GetTick() - tick_start > timeout))
			{
				error_stop();
				return SYS_ERROR;
			}

		};
		I2Cx->TXDR = addr[transfer];
	}

	while(!(I2Cx->ISR & I2C_ISR_TC))
	{
		if(System::GetTick() - tick_start > timeout)
		{
			error_stop();
			return SYS_ERROR;
		}
	};

	return Receive(slave_addr, data, len, timeout);
}

#endif