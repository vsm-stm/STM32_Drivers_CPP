#include "uart.hpp"
#include <stdio.h>
/**
 * @brief Set up the USART configuration.
 * @return The status of the setup operation.
 */
SysInitStatus USART::SetUp()
{
	if((BaudRate < 9600)
	|| (BaudRate > 115200*16))
		return SysInitStatus::InitError;

	// Check the USARTx pointer and configure corresponding parameters
	if (USARTx == USART1)
	{
		RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
		bus_clk = System::APB2BusClock;
		IRQ_vector = USART1_IRQn;
		af = 7;
	}else
	if (USARTx == USART2)
	{
		RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
		bus_clk = System::APB1BusClock;
		IRQ_vector = USART2_IRQn;
		af = 7;
	}
#ifndef STM32F411xE

	else if (USARTx == USART3)
	{
		RCC->APB1ENR |= RCC_APB1ENR_USART3EN;
		bus_clk = System::APB1BusClock;
		IRQ_vector = USART3_IRQn;
		af = 7;
	}
	else if (USARTx == UART4)
	{
		RCC->APB1ENR |= RCC_APB1ENR_UART4EN;
		bus_clk = System::APB1BusClock;
		IRQ_vector = UART4_IRQn;
		af = 8;
	}
	else if (USARTx == UART5)
	{
		RCC->APB1ENR |= RCC_APB1ENR_UART5EN;
		bus_clk = System::APB1BusClock;
		IRQ_vector = UART5_IRQn;
		af = 8;
	}
	else if (USARTx == USART6)
	{
		RCC->APB2ENR |= RCC_APB2ENR_USART6EN;
		bus_clk = System::APB2BusClock;
		IRQ_vector = USART6_IRQn;
		af = 8;
	}
#endif
	else
		return SysInitStatus::InitError;

	// Set the Baud Rate
	USARTx->BRR = bus_clk / BaudRate;

	// Enable USART and configure TX and RX pins if available
	USARTx->CR1 = USART_CR1_UE;

	if (_TX.PORT != NULL)
	{
		_TX.SetUp(PIN::TYPE::AF_PushPull, af);
		USARTx->CR1 |= USART_CR1_TE;
	}
	if (_RX.PORT != NULL)
	{
		_RX.SetUp(PIN::TYPE::AF_PushPull, af);
		USARTx->CR1 |= USART_CR1_RE;
	}

	// Clear and configure CR2 and CR3 registers
	USARTx->CR2 = 0;
	USARTx->CR3 = 0;

#if defined(STM32F4)
	ClearFlags();
#elif defined(STM32F7)
	ClearFlags(ISR_FLAGS::TC);
#endif

	return SysInitStatus::InitOK;
}

SysStatus USART::Send(uint8_t *data, uint32_t len, uint32_t timeout)
{
	uint32_t tick_start = System::GetTick();

	for(uint32_t i = 0;i<len;i++)
	{
#if defined(STM32F4)
		while(!(USARTx->SR & USART_SR_TXE))
#elif defined(STM32F7)
		while(!(USARTx->ISR & USART_ISR_TXE))
#endif
		{
			if(System::GetTick() - tick_start > timeout)
				return SysStatus::Timeout;
		};
#if defined(STM32F4)
		USARTx->DR = data[i];
#elif defined(STM32F7)
		USARTx->TDR = data[i];
#endif
		
	};
	return SysStatus::OK;
};

SysStatus USART::Receive(uint8_t *data, uint32_t len, uint32_t timeout)
{
	uint32_t tick_start = System::GetTick();

	for(uint32_t i = 0;i<len;i++)
	{
#if defined(STM32F4)
		while(!(USARTx->SR & USART_SR_RXNE))
#elif defined(STM32F7)
		while(!(USARTx->ISR & USART_ISR_RXNE))
#endif
		{
			if(System::GetTick() - tick_start > timeout)
				return SysStatus::Timeout;
		};
		#if defined(STM32F4)
			data[i] = USARTx->DR;
		#elif defined(STM32F7)
			data[i] = USARTx->RDR;
		#endif
		
	};
	return SysStatus::OK;
};