#include <spi.hpp>

SysInitStatus SPI::SetHard()
{
	// Check the SPI pointer and configure corresponding parameters
	if(SPIx == SPI1)
	{
		RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
		RCC->APB2RSTR |= RCC_APB2RSTR_SPI1RST;
		RCC->APB2RSTR &= ~RCC_APB2RSTR_SPI1RST;
		bus_clk = System::APB2BusClock;
		IRQ_vector = SPI1_IRQn;
		af = 5;
	}else
	if (SPIx == SPI2)
	{
		RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
		RCC->APB1RSTR |= RCC_APB1RSTR_SPI2RST;
		RCC->APB1RSTR &= ~RCC_APB1RSTR_SPI2RST;
		bus_clk = System::APB1BusClock;
		IRQ_vector = SPI2_IRQn;
		af = 5;
	}else
	if (SPIx == SPI3)
	{
		RCC->APB1ENR |= RCC_APB1ENR_SPI3EN;
		RCC->APB1RSTR |= RCC_APB1RSTR_SPI3RST;
		RCC->APB1RSTR &= ~RCC_APB1RSTR_SPI3RST;
		bus_clk = System::APB1BusClock;
		IRQ_vector = SPI3_IRQn;
		af = 6;
	}
#if defined(STM32F446xx) || defined(STM32F429xx)
	else
	if (SPIx == SPI4)
	{
		RCC->APB2ENR |= RCC_APB2ENR_SPI4EN;
		RCC->APB2RSTR |= RCC_APB2RSTR_SPI4RST;
		RCC->APB2RSTR &= ~RCC_APB2RSTR_SPI4RST;
		bus_clk = System::APB2BusClock;
		IRQ_vector = SPI4_IRQn;
		af = 5;
	}
#endif
	else return SYS_ERROR;

	if (CLK.PORT != NULL)
	{
		CLK.SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High, af);
	}
	if (MOSI.PORT != NULL)
	{
		if(Master_slave == Master_sel::Master)
			MOSI.SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High, af);
		else
			MOSI.SetUp(PIN::TYPE::AF_OD_PulUp, PIN::OUTPUT_SPEED::High, af);
	}
	if (MISO.PORT != NULL)
	{
		if(Master_slave == Master_sel::Master)
			MISO.SetUp(PIN::TYPE::AF_OD_PulUp, PIN::OUTPUT_SPEED::High, af);
		else
			MISO.SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High, af);
			
	}

	if (SS.PORT != NULL)
	{
		if(nss_ctrl == NSS_ctrl::Hard)
		{
			SS.SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High, af);
		}	
		else
		{
			if(Master_slave == Master_sel::Master)
				SS.SetUp(PIN::TYPE::OUTPUT_PushPull, PIN::OUTPUT_SPEED::High);
			else
				SS.SetUp(PIN::TYPE::INPUT_NO_Pull);
		}
	}

	return SYS_OK;
}

SysInitStatus SPI::SetUp(Master_sel mstr, NSS_ctrl nss, TYPE type, Data_frame_format dff, Frame_Format ff, cPolPha cpolpha, uint8_t br)
{
	nss_ctrl = nss;
	Master_slave = mstr;
	SysInitStatus setup_status = SetHard();

	if(setup_status != SYS_OK)
		return setup_status;

	SPIx->CR1 =	static_cast<uint32_t>(mstr) |
	#if defined(STM32F4)
				static_cast<uint32_t>(dff) |
	#endif
				
				static_cast<uint32_t>(ff) |
				static_cast<uint32_t>(cpolpha) |
				br << SPI_CR1_BR_Pos;

	#if defined(STM32F7)
	SPIx->CR2 = static_cast<uint32_t>(dff);// |
				// SPI_CR2_FRXTH;
	#endif

	if((nss_ctrl == NSS_ctrl::Hard)
	&& (mstr == Master_sel::Master))
		SPIx->CR2 |= SPI_CR2_SSOE;
	
	if((nss_ctrl == NSS_ctrl::Software)
	&& (mstr == Master_sel::Slave))
	{
		SPIx->CR1 |= SPI_CR1_SSM;
		// SPIx->CR1 &= ~SPI_CR1_SSI;
	}

	if(type == TYPE::RX)
	{
		SPIx->CR1 |= SPI_CR1_RXONLY;
	}

	SS.SetLevel(1);

	return SYS_OK;
}

SysInitStatus SPI::Send_Receive(uint8_t* tx_data, uint8_t* rx_data, uint16_t data_len, uint32_t timeout)
{
	uint32_t tick_start = System::GetTick();

	SS.SetLevel(0);
	SPIx->CR1 |= SPI_CR1_SPE;
	for(uint32_t i = 0;i<data_len;i++)
	{
		SPIx->DR = tx_data[i];
		while(!(SPIx->SR & SPI_SR_RXNE))
		{
			if(System::GetTick() - tick_start > timeout)
				return SYS_ERROR;
		};

		rx_data[i] = SPIx->DR;
	}

	SPIx->CR1 &= ~SPI_CR1_SPE;

	while(SPIx->SR & SPI_SR_BSY)
	{
		if(System::GetTick() - tick_start > timeout)
			return SYS_ERROR;
	};
	
	SS.SetLevel(1);
	return SYS_OK;
}

SysInitStatus SPI::Send(uint8_t* tx_data, uint16_t data_len, uint32_t timeout)
{
	uint32_t tick_start = System::GetTick();

	SS.SetLevel(0);
	SPIx->CR1 |= SPI_CR1_SPE;
	for(uint32_t i = 0;i<data_len;i++)
	{
		while(!(SPIx->SR & SPI_SR_TXE))
		{
			if(System::GetTick() - tick_start > timeout)
				return SYS_ERROR;
		};

		// SPIx->DR = tx_data[i];


		// if((data_len - i) > 1)
		// {
		// 	SPIx->DR = *((uint16_t*)&tx_data[i]);
		// 	i++;
		// }
		// else
		// {
			*((uint8_t *)(&SPIx->DR)) = tx_data[i];
		// }


	}

	while(SPIx->SR & SPI_SR_BSY)
	{
		if(System::GetTick() - tick_start > timeout)
			return SYS_ERROR;
	};
	SPIx->CR1 &= ~SPI_CR1_SPE;

	SS.SetLevel(1);
	return SYS_OK;
}