#ifndef UART_H_
#define UART_H_

#include "system.hpp"
#include "rcc.hpp"
#include "gpio.hpp"

/**
 * @brief Class representing a USART communication interface.
 */
class USART
{
public:
	USART_TypeDef *USARTx;
	uint32_t BaudRate;

private:
	#if defined(STM32F4) 
		static constexpr uint32_t CR1_TXEIE = USART_CR1_TXEIE;
		static constexpr uint32_t CR1_RXNEIE = USART_CR1_RXNEIE;

		volatile uint32_t& TXD() const { return USARTx->DR; }
		volatile uint32_t& RXD() const { return USARTx->DR; }

		volatile uint32_t& Status_reg() const { return USARTx->SR; }
		volatile uint32_t& Clear_reg() const { return USARTx->SR; }

		static constexpr uint32_t ISR_TXE = USART_SR_TXE;
		static constexpr uint32_t ISR_RXNE = USART_SR_RXNE;

	#elif defined(STM32F7)
		static constexpr uint32_t CR1_TXEIE = USART_CR1_TXEIE;
		static constexpr uint32_t CR1_RXNEIE = USART_CR1_RXNEIE;

		volatile uint32_t& TXD() const { return USARTx->TDR; }
		volatile uint32_t& RXD() const { return USARTx->RDR; }

		volatile uint32_t& Status_reg() const { return USARTx->ISR; }
		volatile uint32_t& Clear_reg() const { return USARTx->ICR; }

		static constexpr uint32_t ISR_TXE = USART_ISR_TXE;
		static constexpr uint32_t ISR_RXNE = USART_ISR_RXNE;

	#elif defined(STM32G0)
		static constexpr uint32_t CR1_TXEIE = USART_CR1_TXEIE_TXFNFIE;
		static constexpr uint32_t CR1_RXNEIE = USART_CR1_RXNEIE_RXFNEIE;

		volatile uint32_t& TXD() const { return USARTx->TDR; }
		volatile uint32_t& RXD() const { return USARTx->RDR; }

		volatile uint32_t& Status_reg() const { return USARTx->ISR; }
		volatile uint32_t& Clear_reg() const { return USARTx->ICR; }

		static constexpr uint32_t ISR_TXE = USART_ISR_TXE_TXFNF;
		static constexpr uint32_t ISR_RXNE = USART_ISR_RXNE_RXFNE;

	#endif

public:

	/**
	 * @brief Enumeration for USART IRQs.
	 */
	enum class IRQ
	{
		TXE = CR1_TXEIE,		///< Transmit Data Register Empty interrupt
		RXNE = CR1_RXNEIE,	///< Receive Data Register Not Empty interrupt
		TC = USART_CR1_TCIE,		///< Transmission Complete interrupt
		IDLE = USART_CR1_IDLEIE		///< Idle Line Detected interrupt
	};

	/**
	 * @brief Structure defining USART configuration parameters.
	 */
	typedef struct
	{
		USART_TypeDef *USARTx;
		uint32_t baudrate;
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
		BaudRate(defs.baudrate),
		_TX(defs.TX),
		_RX(defs.RX)
	{
	}

	USART() = delete;
	USART(const USART&) = delete;
	USART& operator=(const USART&) = delete;
	USART(USART&&) = delete;
	USART& operator=(USART&&) = delete;

	/**
	 * @brief Destructor for USART class.
	 */
	~USART(){};

	/**
	 * @brief Initialize the USART configuration.
	 * @return The status of the initialization operation.
	 */
	SysInitStatus SetUp();

	/**
	 * @brief Enable the specified USART IRQ.
	 * @param irq The IRQ to enable.
	 */
	inline void Enable_IRQ(IRQ irq)
	{
		USARTx->CR1 |= static_cast<uint32_t>(irq);
	}

	/**
	 * @brief Disable the specified USART IRQ.
	 * @param irq The IRQ to disable.
	 */
	inline void Disable_IRQ(IRQ irq)
	{
		USARTx->CR1 &= ~(static_cast<uint32_t>(irq));
	}

	inline void DMA(FunctionalState en)
	{
		if(en)
			USARTx->CR3 |=   USART_CR3_DMAR | USART_CR3_DMAT;
		else
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
#elif defined(STM32F7) or defined(STM32G0)
	enum class ISR_FLAGS
	{
		PE = USART_ICR_PECF,
		FE = USART_ICR_FECF,
		// Noise = USART_ICR_NCF, // todo
		ORE  = USART_ICR_ORECF,
		IDLE  = USART_ICR_IDLECF,
		TC  = USART_ICR_TCCF
	};

	inline void ClearFlags(ISR_FLAGS flag)
	{
		USARTx->ICR = static_cast<uint32_t>(flag);
	}
#endif

	SysStatus Send(uint8_t *data, uint32_t len, uint32_t timeout);
	SysStatus Receive(uint8_t *data, uint32_t len, uint32_t timeout);

	/**
	 * @brief Set the baud rate for USART.
	 * @param baud The desired baud rate.
	 */
	inline void SetBaud(uint32_t baud)
	{
		if (_info != nullptr)
		{
			USARTx->BRR = *_info->bus_clk / baud;
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

	inline void EnableNVIC_IRQ()
	{
		if (_info != nullptr) NVIC_EnableIRQ(_info->irq);
	}

	inline void DisableNVIC_IRQ()
	{
		if (_info != nullptr) NVIC_DisableIRQ(_info->irq);
	}

	inline void DeInit()
	{	
		USARTx->CR1 = 0;
		USARTx->CR2 = 0;
		USARTx->CR3 = 0;
		DisableNVIC_IRQ();
	}


private:
	struct PeriphInfo {
		USART_TypeDef*      periph;
		volatile uint32_t*  clk_reg;
		uint32_t            clk_bit;
		uint32_t const*     bus_clk;
		IRQn_Type           irq;
		uint8_t             af;
	};
	static const PeriphInfo usart_table[];

	PIN _TX{};
	PIN _RX{};

	const PeriphInfo* _info = nullptr;
};

#endif
