#include "spi.hpp"


#if defined(STM32F4) || defined(STM32F7)
	const SPI::PeriphInfo SPI::spi_table[] = {
		{ SPI1, &RCC->APB2ENR, RCC_APB2ENR_SPI1EN, &RCC->APB2RSTR, RCC_APB2RSTR_SPI1RST, &System::APB2BusClock, SPI1_IRQn, 5 },
		{ SPI2, &RCC->APB1ENR, RCC_APB1ENR_SPI2EN, &RCC->APB1RSTR, RCC_APB1RSTR_SPI2RST, &System::APB1BusClock, SPI2_IRQn, 5 },
		{ SPI3, &RCC->APB1ENR, RCC_APB1ENR_SPI3EN, &RCC->APB1RSTR, RCC_APB1RSTR_SPI3RST, &System::APB1BusClock, SPI3_IRQn, 6 },
	#if defined(STM32F446xx) || defined(STM32F429xx)
		{ SPI4, &RCC->APB2ENR, RCC_APB2ENR_SPI4EN, &RCC->APB2RSTR, RCC_APB2RSTR_SPI4RST, &System::APB2BusClock, SPI4_IRQn, 5 },
	#endif
	};
#elif defined(STM32G0)
	const SPI::PeriphInfo SPI::spi_table[] = {
		{ SPI1, &RCC->APBENR2, RCC_APBENR2_SPI1EN, &RCC->APBRSTR2, RCC_APBRSTR2_SPI1RST, &System::APB1BusClock, SPI1_IRQn, 0 },
		{ SPI2, &RCC->APBENR1, RCC_APBENR1_SPI2EN, &RCC->APBRSTR1, RCC_APBRSTR1_SPI2RST, &System::APB1BusClock, SPI2_IRQn, 0 },
	};
#endif

SysInitStatus SPI::SetHard()
{
	const PeriphInfo* info = nullptr;
	for (const auto& e : spi_table)
		if (e.periph == SPIx) { info = &e; break; }
	if (!info) return SysInitStatus::InitError;

	_info = info;
	*info->clk_reg |= info->clk_bit;
	*info->rst_reg |= info->rst_bit;
	*info->rst_reg &= ~info->rst_bit;

	if (CLK.PORT != NULL)
	{
	#if defined(STM32G0)
			if(CLK.PORT == GPIOB &&  CLK.pin == 10)
				CLK.SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High, 5);
			else
				CLK.SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High, _info->af);
	#else
			CLK.SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High, _info->af);
	#endif

		// CLK.SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High, _info->af);
	}
	if (MOSI.PORT != NULL)
	{
		if(Master_slave == Master_sel::Master)
			MOSI.SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High, _info->af);
		else
			MOSI.SetUp(PIN::TYPE::AF_OD_PulUp, PIN::OUTPUT_SPEED::High, _info->af);
	}
	if (MISO.PORT != NULL)
	{
		if(Master_slave == Master_sel::Master)
			MISO.SetUp(PIN::TYPE::AF_OD_PulUp, PIN::OUTPUT_SPEED::High, _info->af);
		else
			MISO.SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High, _info->af);

	}

	if (SS.PORT != NULL)
	{
		if(nss_ctrl == NSS_ctrl::Hard)
		{
			SS.SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High, _info->af);
		}	
		else
		{
			if(Master_slave == Master_sel::Master)
				SS.SetUp(PIN::TYPE::OUTPUT_PushPull, PIN::OUTPUT_SPEED::High);
			else
				SS.SetUp(PIN::TYPE::INPUT_NO_Pull);
		}
	}

	return SysInitStatus::InitOK;
}

SysInitStatus SPI::SetUp(Master_sel mstr, NSS_ctrl nss, TYPE type, Data_frame_format dff, Frame_Format ff, cPolPha cpolpha, uint8_t br)
{
	nss_ctrl = nss;
	Master_slave = mstr;
	SysInitStatus setup_status = SetHard();

	if(setup_status != SysInitStatus::InitOK)
		return setup_status;

	SPIx->CR1 =	static_cast<uint32_t>(mstr) |
	#if defined(STM32F4)
				static_cast<uint32_t>(dff) |
	#endif
				
				static_cast<uint32_t>(ff) |
				static_cast<uint32_t>(cpolpha) |
				br << SPI_CR1_BR_Pos;

	#if defined(STM32F7) || defined(STM32G0)
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

	return SysInitStatus::InitOK;
}

SysStatus SPI::Send_Receive(uint8_t* tx_data, uint8_t* rx_data, uint16_t data_len, uint32_t timeout)
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
				return SysStatus::Timeout;
		};

		rx_data[i] = SPIx->DR;
	}

	SPIx->CR1 &= ~SPI_CR1_SPE;

	while(SPIx->SR & SPI_SR_BSY)
	{
		if(System::GetTick() - tick_start > timeout)
			return SysStatus::Timeout;
	};
	
	SS.SetLevel(1);
	return SysStatus::OK;
}

SysStatus SPI::Send(uint8_t* tx_data, uint16_t data_len, uint32_t timeout)
{
	uint32_t tick_start = System::GetTick();

	SS.SetLevel(0);
	SPIx->CR1 |= SPI_CR1_SPE;
	for(uint32_t i = 0;i<data_len;i++)
	{
		while(!(SPIx->SR & SPI_SR_TXE))
		{
			if(System::GetTick() - tick_start > timeout)
				return SysStatus::Timeout;
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
			return SysStatus::Timeout;
	};

	SPIx->CR1 &= ~SPI_CR1_SPE;

	SS.SetLevel(1);
	return SysStatus::OK;
}