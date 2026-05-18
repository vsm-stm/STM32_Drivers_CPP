#include "spi.hpp"

#if defined(STM32F4) || defined(STM32F7)
const SPI::PeriphInfo SPI::spi_table[] = {
	{ SPI1, &RCC->APB2ENR, RCC_APB2ENR_SPI1EN, &RCC->APB2RSTR, RCC_APB2RSTR_SPI1RST, &System::APB2BusClock, SPI1_IRQn },
	{ SPI2, &RCC->APB1ENR, RCC_APB1ENR_SPI2EN, &RCC->APB1RSTR, RCC_APB1RSTR_SPI2RST, &System::APB1BusClock, SPI2_IRQn },
	{ SPI3, &RCC->APB1ENR, RCC_APB1ENR_SPI3EN, &RCC->APB1RSTR, RCC_APB1RSTR_SPI3RST, &System::APB1BusClock, SPI3_IRQn },
#if defined(STM32F446xx) || defined(STM32F429xx)
	{ SPI4, &RCC->APB2ENR, RCC_APB2ENR_SPI4EN, &RCC->APB2RSTR, RCC_APB2RSTR_SPI4RST, &System::APB2BusClock, SPI4_IRQn },
#endif
};
#elif defined(STM32G0)
const SPI::PeriphInfo SPI::spi_table[] = {
	{ SPI1, &RCC->APBENR2, RCC_APBENR2_SPI1EN, &RCC->APBRSTR2, RCC_APBRSTR2_SPI1RST, &System::APB1BusClock, SPI1_IRQn },
	{ SPI2, &RCC->APBENR1, RCC_APBENR1_SPI2EN, &RCC->APBRSTR1, RCC_APBRSTR1_SPI2RST, &System::APB1BusClock, SPI2_IRQn },
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

	_clk.SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High);

	if (Master_slave == Master_sel::Master)
		_mosi.SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High);
	else
		_mosi.SetUp(PIN::TYPE::AF_OD_PulUp, PIN::OUTPUT_SPEED::High);

	if (_miso.IsValid())
	{
		if (Master_slave == Master_sel::Master)
			_miso.SetUp(PIN::TYPE::AF_OD_PulUp);
		else
			_miso.SetUp(PIN::TYPE::AF_PushPull);
	}

	if (_ss.IsValid())
	{
		if (nss_ctrl == NSS_ctrl::Hard)
			_ss.SetUp(PIN::TYPE::AF_PushPull);
		else if (Master_slave == Master_sel::Master)
			_ss.SetUp(PIN::TYPE::OUTPUT_PushPull);
		else
			_ss.SetUp(PIN::TYPE::INPUT_NO_Pull);
	}

	return SysInitStatus::InitOK;
}

SysInitStatus SPI::SetUp(Master_sel mstr, NSS_ctrl nss, TYPE type,
						  Data_frame_format dff, Frame_Format ff,
						  cPolPha cpolpha, uint8_t br)
{
	nss_ctrl     = nss;
	Master_slave = mstr;

	SysInitStatus status = SetHard();
	if (status != SysInitStatus::InitOK) return status;

	SPIx->CR1 = static_cast<uint32_t>(mstr) |
#if defined(STM32F4)
				static_cast<uint32_t>(dff) |
#endif
				static_cast<uint32_t>(ff)      |
				static_cast<uint32_t>(cpolpha) |
				br << SPI_CR1_BR_Pos;

#if defined(STM32F7) || defined(STM32G0)
	SPIx->CR2 = static_cast<uint32_t>(dff);
#endif

	if (nss_ctrl == NSS_ctrl::Hard && mstr == Master_sel::Master)
		SPIx->CR2 |= SPI_CR2_SSOE;

	if (nss_ctrl == NSS_ctrl::Software && mstr == Master_sel::Slave)
		SPIx->CR1 |= SPI_CR1_SSM;

	if (type == TYPE::RX)
		SPIx->CR1 |= SPI_CR1_RXONLY;

	_ss.SetLevel(1);
	return SysInitStatus::InitOK;
}

SysStatus SPI::Send_Receive(uint8_t* tx_data, uint8_t* rx_data,
							uint16_t data_len, uint32_t timeout)
{
	uint32_t tick_start = System::GetTick();

	_ss.SetLevel(0);
	SPIx->CR1 |= SPI_CR1_SPE;

	for (uint32_t i = 0; i < data_len; i++)
	{
		SPIx->DR = tx_data[i];
		while (!(SPIx->SR & SPI_SR_RXNE))
		{
			if (System::GetTick() - tick_start > timeout)
				return SysStatus::Timeout;
		}
		rx_data[i] = SPIx->DR;
	}

	SPIx->CR1 &= ~SPI_CR1_SPE;
	while (SPIx->SR & SPI_SR_BSY)
	{
		if (System::GetTick() - tick_start > timeout)
			return SysStatus::Timeout;
	}

	_ss.SetLevel(1);
	return SysStatus::OK;
}

SysStatus SPI::Send(uint8_t* tx_data, uint16_t data_len, uint32_t timeout)
{
	uint32_t tick_start = System::GetTick();

	_ss.SetLevel(0);
	SPIx->CR1 |= SPI_CR1_SPE;

	for (uint32_t i = 0; i < data_len; i++)
	{
		while (!(SPIx->SR & SPI_SR_TXE))
		{
			if (System::GetTick() - tick_start > timeout)
				return SysStatus::Timeout;
		}
		*((uint8_t*)(&SPIx->DR)) = tx_data[i];
	}

	while (SPIx->SR & SPI_SR_BSY)
	{
		if (System::GetTick() - tick_start > timeout)
			return SysStatus::Timeout;
	}

	SPIx->CR1 &= ~SPI_CR1_SPE;
	_ss.SetLevel(1);
	return SysStatus::OK;
}
