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
	uint32_t irq_en;

	enum class IRQ
	{
		NO  = 0,
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
	}define;

	explicit USART(	USART_TypeDef *USARTx,
					uint32_t BaudRate,
					std::unique_ptr<PIN> TX,
					std::unique_ptr<PIN> RX) :
			USARTx(USARTx),
			BaudRate(BaudRate),
			_TX(std::move(TX)),
			_RX(std::move(RX))
	{
		HardwareSetup();
	}

	explicit USART(define def) :
			USARTx(def.USARTx),
			BaudRate(def.BaudRate),
			_TX(std::make_unique<PIN>(def.TX)),
			_RX(std::make_unique<PIN>(def.RX))
	{
		HardwareSetup();
	}

    USART()                         = delete;
    USART(USART const &)            = default;
    USART(USART &&)                 = default;
    USART &operator=(USART const &) = default;
    USART &operator=(USART &&)      = default;
	~USART()
	{
		USARTx->CR1 = 0;
	};

private:
	 std::unique_ptr<PIN> _TX{};
	 std::unique_ptr<PIN> _RX{};
	 PIN TXX{};
	 PIN RXX{};

	SYS_StatusTypeDef HardwareSetup();
	
};






#endif