#ifndef UART_H_
#define UART_H_

#include <memory>
#include <system_f4.hpp>
#include <rcc.hpp>
#include <gpio.hpp>

struct USART_define
{
	USART_TypeDef *USARTx;
	uint32_t BaudRate;
	PIN TX;
	PIN RX;
};

struct USART_DMA_define
{
	USART_TypeDef *USARTx;
	uint32_t BaudRate;
	PIN TX;
	PIN RX;
	DMA_Stream_TypeDef *DMA_TX;
	DMA_Stream_TypeDef *DMA_RX;
};

class USART
{

public:
	USART_TypeDef *USARTx;
	uint32_t BaudRate;
	DMA_Stream_TypeDef *DMA_Sx_TX;
	DMA_Stream_TypeDef *DMA_Sx_RX;

	explicit USART(	USART_TypeDef *USARTx,
					uint32_t BaudRate,
					std::unique_ptr<PIN> TX,
					std::unique_ptr<PIN> RX,
					DMA_Stream_TypeDef *DMA_TX,
					DMA_Stream_TypeDef *DMA_RX ) :
			USARTx(USARTx),
			BaudRate(BaudRate),
			DMA_Sx_TX(DMA_TX),
			DMA_Sx_RX(DMA_RX),
			_TX(std::move(TX)),
			_RX(std::move(RX))
	{
		HardwareSetup();
	}

	explicit USART(	USART_TypeDef *USARTx,
					uint32_t BaudRate,
					std::unique_ptr<PIN> TX,
					std::unique_ptr<PIN> RX ) :
			USARTx(USARTx),
			BaudRate(BaudRate),
			_TX(std::move(TX)),
			_RX(std::move(RX))
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

	 uint32_t dma_tx_id, dma_tx_sx_id, dma_rx_id, dma_rx_sx_id;





	SYS_StatusTypeDef HardwareSetup();
	
};




#endif