#ifndef UART_H_
#define UART_H_

#include <system.hpp>
#include <rcc.hpp>
#include <gpio.hpp>

/**
 * @brief Class representing a USART communication interface.
 */
class USART
{
public:
	USART_TypeDef *USARTx;
	uint32_t BaudRate;

	/**
	 * @brief Enumeration for USART IRQs.
	 */
	enum class IRQ
	{
		TXE = USART_CR1_TXEIE,		///< Transmit Data Register Empty interrupt
		RXNE = USART_CR1_RXNEIE,	///< Receive Data Register Not Empty interrupt
		TC = USART_CR1_TCIE,		///< Transmission Complete interrupt
		IDLE = USART_CR1_IDLEIE		///< Idle Line Detected interrupt
	};

	/**
	 * @brief Structure defining USART configuration parameters.
	 */
	typedef struct
	{
		USART_TypeDef *USARTx;
		uint32_t BaudRate;
		PIN TX;
		PIN RX;
	} def;

	/**
	 * @brief Constructor for USART class.
	 * @param usartx Pointer to USART peripheral.
	 * @param baudrate Baud rate for communication.
	 * @param TX GPIO pin for TX.
	 * @param RX GPIO pin for RX.
	 */
	explicit USART(USART_TypeDef *usartx, uint32_t baudrate, PIN TX, PIN RX) :
		USARTx(usartx),
		BaudRate(baudrate),
		_TX(TX),
		_RX(RX)
	{
	}

	/**
	 * @brief Constructor for USART class using configuration structure.
	 * @param defs Structure containing USART configuration parameters.
	 */
	explicit USART(def defs) :
		USARTx(defs.USARTx),
		BaudRate(defs.BaudRate),
		_TX(defs.TX),
		_RX(defs.RX)
	{
	}

	USART() = delete;
	USART(USART const &) = default;
	USART(USART &&) = default;
	USART &operator=(USART const &) = default;
	USART &operator=(USART &&) = default;

	/**
	 * @brief Destructor for USART class.
	 */
	~USART()
	{
		USARTx->CR1 = 0;
		USARTx->CR2 = 0;
		USARTx->CR3 = 0;
	};

	/**
	 * @brief Set up the USART configuration.
	 * @return The status of the setup operation.
	 */
	SYS_StatusTypeDef SetUp();

	/**
	 * @brief Enable the specified USART IRQ.
	 * @param irq The IRQ to enable.
	 */
	void Enable_IRQ(IRQ irq)
	{
		USARTx->CR1 |= static_cast<uint32_t>(irq);

		if (!(NVIC_GetEnableIRQ(IRQ_vector)))
		{
			NVIC_EnableIRQ(IRQ_vector);
		}
	}

	/**
	 * @brief Disable the specified USART IRQ.
	 * @param irq The IRQ to disable.
	 */
	void Disable_IRQ(IRQ irq)
	{
		USARTx->CR1 &= ~(static_cast<uint32_t>(irq));
		if (!(USARTx->CR1 & (USART_CR1_TXEIE | USART_CR1_TCIE | USART_CR1_RXNEIE | USART_CR1_IDLEIE)))
		{
			NVIC_DisableIRQ(IRQ_vector);
		}
	}

	/**
	 * @brief Enable DMA for USART.
	 */
	inline void Enable_DMA()
	{
		USARTx->CR3 |= USART_CR3_DMAR | USART_CR3_DMAT;
	}

	/**
	 * @brief Disable DMA for USART.
	 */
	inline void Disable_DMA()
	{
		USARTx->CR3 &= ~(USART_CR3_DMAR | USART_CR3_DMAT);
	}

#if defined(STM32F4)
	/**
	 * @brief Clear all USART flags.
	 */
	inline void ClearFlags()
	{
		USARTx->SR = 0;
	}
#elif defined(STM32F7)
	/**
	 * @brief Clear all USART flags.
	 */
	inline void ClearFlags(uint32_t flag)
	{
		USARTx->ICR = flag;
	}
#endif

	/**
	 * @brief Set the baud rate for USART.
	 * @param baud The desired baud rate.
	 */
	inline void SetBaud(uint32_t baud)
	{
		if (bus_clk != 0)
		{
			USARTx->BRR = bus_clk / baud;
		}
	}

	/**
	 * @brief Set the parity for USART (Not implemented).
	 * @param parity The desired parity.
	 */
	inline void SetParity(uint32_t parity)
	{
		// Not implemented
	}

private:
	PIN _TX{};
	PIN _RX{};

	uint32_t af, bus_clk;
	IRQn_Type IRQ_vector;
};

#endif
