#include <spi.hpp>

SYS_StatusTypeDef SPI::SetHard()
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
		CLK.SetUp(PIN::TYPE::AF_PushPull, af);
	}
	if (MOSI.PORT != NULL)
	{
		MOSI.SetUp(PIN::TYPE::AF_PushPull, af);
	}
	if (MISO.PORT != NULL)
	{
		MISO.SetUp(PIN::TYPE::AF_OD, af);
	}

	if (SS.PORT != NULL)
	{
		if(nss_ctrl == NSS_ctrl::Hard)
			SS.SetUp(PIN::TYPE::AF_PushPull, af);
		else
			SS.SetUp(PIN::TYPE::OUTPUT_PushPull);
	}

	return SYS_OK;
}

SYS_StatusTypeDef SPI::SetUp(Master_sel mstr, NSS_ctrl nss, TYPE type, Data_frame_format dff, Frame_Format ff, cPolPha cpolpha, uint8_t br)
{
	nss_ctrl = nss;
	SYS_StatusTypeDef setup_status = SetHard();

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

	if(nss_ctrl == NSS_ctrl::Hard)
		SPIx->CR2 |= SPI_CR2_SSOE;

	if(type == TYPE::RX)
	{
		SPIx->CR1 |= SPI_CR1_RXONLY;
	}

	SS.SetLevel(1);

	return SYS_OK;
}

 SYS_StatusTypeDef SPI::Send_Receive(uint8_t* tx_data, uint8_t* rx_data, uint16_t data_len, uint32_t timeout)
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
