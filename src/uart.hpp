#ifndef UART_H_
#define UART_H_

#include <memory>
#include <system_f4.hpp>
#include <rcc.hpp>
#include <gpio.hpp>

class USART
{

public:
	USART_TypeDef *USARTx;
	uint32_t BaudRate;

	enum class IRQ
	{
		TXE = USART_CR1_TXEIE,
		RXNE = USART_CR1_RXNEIE,
		TC = USART_CR1_TCIE,
		IDLE = USART_CR1_IDLEIE
	};

	typedef struct 
	{
		USART_TypeDef *USARTx;
		uint32_t BaudRate;
		PIN TX;
		PIN RX;
	}def;

	explicit USART(	USART_TypeDef *usartx,
					uint32_t baudrate,
					PIN TX,
					PIN RX) :
			USARTx(usartx),
			BaudRate(baudrate),
			_TX(TX),
			_RX(RX)
	{

	}

	explicit USART(def defs) :
			USARTx(defs.USARTx),
			BaudRate(defs.BaudRate),
			_TX(defs.TX),
			_RX(defs.RX)
	{

	}

	USART()							= delete;
	USART(USART const &)			= default;
	USART(USART &&)					= default;
	USART &operator=(USART const &)	= default;
	USART &operator=(USART &&)		= default;
	~USART()
	{
		USARTx->CR1 = 0;
		USARTx->CR2 = 0;
		USARTx->CR3 = 0;
	};

	SYS_StatusTypeDef SetUp();

	void Enable_IRQ(IRQ irq)
	{
		USARTx->CR1 |= static_cast<uint32_t>(irq);

		if(!(NVIC_GetEnableIRQ(IRQ_vector)))
		{
			NVIC_EnableIRQ(IRQ_vector);
		}
	}

	void Disable_IRQ(IRQ irq)
	{
		USARTx->CR1 &= ~(static_cast<uint32_t>(irq));
		if(!(USARTx->CR1 & (USART_CR1_TXEIE | USART_CR1_TCIE | USART_CR1_RXNEIE | USART_CR1_IDLEIE)))
		{
			NVIC_DisableIRQ(IRQ_vector);
		}
	}

	inline void Enable_DMA()
	{
		USARTx->CR3 |= USART_CR3_DMAR | USART_CR3_DMAT;
	}

	inline void Disable_DMA()
	{
		USARTx->CR3 &= ~(USART_CR3_DMAR | USART_CR3_DMAT);
	}

	inline void ClearFlags()
	{
		USARTx->SR = 0;
	}

private:
	PIN _TX{};
	PIN _RX{};

	uint32_t af, bus_clk;
	IRQn_Type IRQ_vector;
	
};



#endif
