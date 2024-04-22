#include <spi.hpp>

SYS_StatusTypeDef SPI::SetHard()
{
	// Check the SPI pointer and configure corresponding parameters
	if(SPIx == SPI1)
	{
		RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
		RCC->APB2RSTR |= RCC_APB2RSTR_SPI1RST;
		RCC->APB2RSTR &= ~RCC_APB2RSTR_SPI1RST;
		bus_clk = ClockSystem::APB2BusClock;
		IRQ_vector = SPI1_IRQn;
		af = 5;
	}else
	if (SPIx == SPI2)
	{
		RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
		RCC->APB1RSTR |= RCC_APB1RSTR_SPI2RST;
		RCC->APB1RSTR &= ~RCC_APB1RSTR_SPI2RST;
		bus_clk = ClockSystem::APB1BusClock;
		IRQ_vector = SPI2_IRQn;
		af = 5;
	}else
	if (SPIx == SPI3)
	{
		RCC->APB1ENR |= RCC_APB1ENR_SPI3EN;
		RCC->APB1RSTR |= RCC_APB1RSTR_SPI3RST;
		RCC->APB1RSTR &= ~RCC_APB1RSTR_SPI3RST;
		bus_clk = ClockSystem::APB1BusClock;
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
		bus_clk = ClockSystem::APB2BusClock;
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
		SS.SetUp(PIN::TYPE::OUTPUT_PushPull);
	}

	return SYS_OK;
}

SYS_StatusTypeDef SPI::SetUp(Master_sel mstr, TYPE type, Data_frame_format dff, Frame_Format ff, cPolPha cpolpha, uint8_t br)
{
	SYS_StatusTypeDef setup_status = SetHard();

	if(setup_status != SYS_OK)
		return setup_status;

	SPIx->CR1 =	static_cast<uint32_t>(mstr) |
				static_cast<uint32_t>(dff) |
				static_cast<uint32_t>(ff) |
				static_cast<uint32_t>(cpolpha) |
				br << SPI_CR1_BR_Pos;

	if(type == TYPE::RX)
	{
		SPIx->CR1 |= SPI_CR1_RXONLY;
	}

	return SYS_OK;
}

SYS_StatusTypeDef SPI_Slave_TX::SetUp()
{
	SYS_StatusTypeDef setup_status = SetHard();

	if(setup_status != SYS_OK)
		return setup_status;

	SPIx->CR1 = //SPI_CR1_CPHA;// |
				SPI_CR1_CPOL;
	// SPIx->CR2 = SPI_CR2_TXDMAEN;
	
	return SYS_OK;
}