#include "uart.hpp"


#if defined(STM32F4) || defined(STM32F7)
	const USART::PeriphInfo USART::usart_table[] = {
		{ USART1, &RCC->APB2ENR, RCC_APB2ENR_USART1EN, &System::APB2BusClock, USART1_IRQn, 7 },
		{ USART2, &RCC->APB1ENR, RCC_APB1ENR_USART2EN, &System::APB1BusClock, USART2_IRQn, 7 },
	#ifndef STM32F411xE
		{ USART3, &RCC->APB1ENR, RCC_APB1ENR_USART3EN, &System::APB1BusClock, USART3_IRQn, 7 },
		{ UART4,  &RCC->APB1ENR, RCC_APB1ENR_UART4EN,  &System::APB1BusClock, UART4_IRQn,  8 },
		{ UART5,  &RCC->APB1ENR, RCC_APB1ENR_UART5EN,  &System::APB1BusClock, UART5_IRQn,  8 },
		{ USART6, &RCC->APB2ENR, RCC_APB2ENR_USART6EN, &System::APB2BusClock, USART6_IRQn, 8 },
	#endif
	};
#elif defined(STM32G0)
	const USART::PeriphInfo USART::usart_table[] = {
		{ USART1, &RCC->APBENR2, RCC_APBENR2_USART1EN, &System::APB1BusClock, USART1_IRQn, 0 },
		{ USART2, &RCC->APBENR1, RCC_APBENR1_USART2EN, &System::APB1BusClock, USART2_IRQn, 0 },
	};
#endif
/**
 * @brief Initialize the USART configuration.
 * @return The status of the initialization operation.
 */
SysInitStatus USART::SetUp()
{
	if((BaudRate < 9600)
	|| (BaudRate > 115200*16))
		return SysInitStatus::InitError;

	const PeriphInfo* info = nullptr;
	for (const auto& e : usart_table)
		if (e.periph == USARTx) { info = &e; break; }
	if (!info)
		return SysInitStatus::InitError;

	_info = info;
	*info->clk_reg |= info->clk_bit;

	// Set the Baud Rate
	USARTx->BRR = *_info->bus_clk / BaudRate;

	// Enable USART and configure TX and RX pins if available
	USARTx->CR1 = USART_CR1_UE;

	if (_TX.PORT != NULL)
	{
		_TX.SetUp(PIN::TYPE::AF_PushPull, _info->af);
		USARTx->CR1 |= USART_CR1_TE;
	}
	if (_RX.PORT != NULL)
	{
		_RX.SetUp(PIN::TYPE::AF_PushPull, _info->af);
		USARTx->CR1 |= USART_CR1_RE;
	}

	// Clear and configure CR2 and CR3 registers
	USARTx->CR2 = 0;
	USARTx->CR3 = 0;

#if defined(STM32F4)
	ClearFlags();
#elif defined(STM32F7) || defined(STM32G0)
	ClearFlags(ISR_FLAGS::TC);
#endif

	return SysInitStatus::InitOK;
}

SysStatus USART::Send(uint8_t *data, uint32_t len, uint32_t timeout)
{
	uint32_t tick_start = System::GetTick();

	for(uint32_t i = 0;i<len;i++)
	{
		while(!(Status_reg() & ISR_TXE))
		{
			if(System::GetTick() - tick_start > timeout)
				return SysStatus::Timeout;
		};

		TXD() = data[i];
		
	};
	return SysStatus::OK;
};

SysStatus USART::Receive(uint8_t *data, uint32_t len, uint32_t timeout)
{
	uint32_t tick_start = System::GetTick();

	for(uint32_t i = 0;i<len;i++)
	{

		while(!(Status_reg() & ISR_RXNE))
		{
			if(System::GetTick() - tick_start > timeout)
				return SysStatus::Timeout;
		};

		data[i] = RXD();
		
	};
	return SysStatus::OK;
};