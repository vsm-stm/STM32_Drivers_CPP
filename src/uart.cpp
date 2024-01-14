#include "uart.hpp"


SYS_StatusTypeDef USART::SetUp()
{
	uint32_t af, bus_clk;

	if((USARTx == USART1)
	|| (USARTx == USART2)
	|| (USARTx == USART3))
	{
		af = 7;
	}
	else
	if((USARTx == USART6)
	|| (USARTx == UART4)
	|| (USARTx == UART5))
	{
		af = 8;
	}
	else
	{
		return SYS_ERROR;
	}

	if(USARTx == USART1)
	{
		RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
		bus_clk = ClockSystem::APB2BusClock;
		IRQ_vector = USART1_IRQn;
	} else
	if(USARTx == USART2)
	{
		RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
		bus_clk = ClockSystem::APB1BusClock;
		IRQ_vector = USART2_IRQn;
	} else
	if(USARTx == USART3)
	{
		RCC->APB1ENR |= RCC_APB1ENR_USART3EN;
		bus_clk = ClockSystem::APB1BusClock;
		IRQ_vector = USART3_IRQn;
	} else
	if(USARTx == UART4)
	{
		RCC->APB1ENR |= RCC_APB1ENR_UART4EN;
		bus_clk = ClockSystem::APB1BusClock;
		IRQ_vector = UART4_IRQn;
	} else
	if(USARTx == UART5)
	{
		RCC->APB1ENR |= RCC_APB1ENR_UART5EN;
		bus_clk = ClockSystem::APB1BusClock;
		IRQ_vector = UART5_IRQn;
	} else
	if(USARTx == USART6)
	{
		RCC->APB2ENR |= RCC_APB2ENR_USART6EN;
		bus_clk = ClockSystem::APB2BusClock;
		IRQ_vector = USART6_IRQn;
	}

	USARTx->BRR = bus_clk/BaudRate;

	USARTx->CR1 = USART_CR1_UE;

	if(_TX.PORT != NULL)
	{
		_TX.SetUp(PIN::TYPE::AF_PushPull, af);
		USARTx->CR1 |= USART_CR1_TE;
	}
	if(_RX.PORT != NULL)
	{
		_RX.SetUp(PIN::TYPE::AF_PushPull, af);	
		USARTx->CR1 |= USART_CR1_RE;
	}

	USARTx->CR2 = 0;
	USARTx->CR3 = 0;

	USARTx->SR = 0;

	return SYS_OK;
}