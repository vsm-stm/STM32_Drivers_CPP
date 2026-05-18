#ifndef UART_H_
#define UART_H_

#include "system.hpp"
#include "rcc.hpp"
#include "gpio.hpp"

class USART
{
public:
	USART_TypeDef *USARTx;
	uint32_t BaudRate;

	// -----------------------------------------------------------------------
	// Compile-time pin tables — PIN carries port, pin number and AF.
	// Zero memory footprint: used only in constant expressions.
	// -----------------------------------------------------------------------

#if defined(STM32G0)
	struct TX {
		static constexpr PIN PA2  = { GPIOA_BASE,  2, 1, USART2_BASE };
		static constexpr PIN PA9  = { GPIOA_BASE,  9, 0, USART1_BASE };
		static constexpr PIN PA14 = { GPIOA_BASE, 14, 1, USART2_BASE };
		static constexpr PIN PB6  = { GPIOB_BASE,  6, 0, USART1_BASE };
	};
	struct RX {
		static constexpr PIN PA3  = { GPIOA_BASE,  3, 1, USART2_BASE };
		static constexpr PIN PA10 = { GPIOA_BASE, 10, 0, USART1_BASE };
		static constexpr PIN PA15 = { GPIOA_BASE, 15, 1, USART2_BASE };
		static constexpr PIN PB7  = { GPIOB_BASE,  7, 0, USART1_BASE };
	};

#elif defined(STM32F4) || defined(STM32F7)
	struct TX {
		static constexpr PIN PA9  = { GPIOA_BASE,  9, 7, USART1_BASE };
		static constexpr PIN PB6  = { GPIOB_BASE,  6, 7, USART1_BASE };
		static constexpr PIN PA2  = { GPIOA_BASE,  2, 7, USART2_BASE };
		static constexpr PIN PD5  = { GPIOD_BASE,  5, 7, USART2_BASE };
		static constexpr PIN PB10 = { GPIOB_BASE, 10, 7, USART3_BASE };
		static constexpr PIN PC10 = { GPIOC_BASE, 10, 7, USART3_BASE };
	};
	struct RX {
		static constexpr PIN PA10 = { GPIOA_BASE, 10, 7, USART1_BASE };
		static constexpr PIN PB7  = { GPIOB_BASE,  7, 7, USART1_BASE };
		static constexpr PIN PA3  = { GPIOA_BASE,  3, 7, USART2_BASE };
		static constexpr PIN PD6  = { GPIOD_BASE,  6, 7, USART2_BASE };
		static constexpr PIN PB11 = { GPIOB_BASE, 11, 7, USART3_BASE };
		static constexpr PIN PC11 = { GPIOC_BASE, 11, 7, USART3_BASE };
	};
#endif

private:
	#if defined(STM32F4)
		static constexpr uint32_t CR1_TXEIE  = USART_CR1_TXEIE;
		static constexpr uint32_t CR1_RXNEIE = USART_CR1_RXNEIE;

		volatile uint32_t& TXD()        const { return USARTx->DR; }
		volatile uint32_t& RXD()        const { return USARTx->DR; }
		volatile uint32_t& Status_reg() const { return USARTx->SR; }
		volatile uint32_t& Clear_reg()  const { return USARTx->SR; }

		static constexpr uint32_t ISR_TXE  = USART_SR_TXE;
		static constexpr uint32_t ISR_RXNE = USART_SR_RXNE;

	#elif defined(STM32F7)
		static constexpr uint32_t CR1_TXEIE  = USART_CR1_TXEIE;
		static constexpr uint32_t CR1_RXNEIE = USART_CR1_RXNEIE;

		volatile uint32_t& TXD()        const { return USARTx->TDR; }
		volatile uint32_t& RXD()        const { return USARTx->RDR; }
		volatile uint32_t& Status_reg() const { return USARTx->ISR; }
		volatile uint32_t& Clear_reg()  const { return USARTx->ICR; }

		static constexpr uint32_t ISR_TXE  = USART_ISR_TXE;
		static constexpr uint32_t ISR_RXNE = USART_ISR_RXNE;

	#elif defined(STM32G0)
		static constexpr uint32_t CR1_TXEIE  = USART_CR1_TXEIE_TXFNFIE;
		static constexpr uint32_t CR1_RXNEIE = USART_CR1_RXNEIE_RXFNEIE;

		volatile uint32_t& TXD()        const { return USARTx->TDR; }
		volatile uint32_t& RXD()        const { return USARTx->RDR; }
		volatile uint32_t& Status_reg() const { return USARTx->ISR; }
		volatile uint32_t& Clear_reg()  const { return USARTx->ICR; }

		static constexpr uint32_t ISR_TXE  = USART_ISR_TXE_TXFNF;
		static constexpr uint32_t ISR_RXNE = USART_ISR_RXNE_RXFNE;

	#endif

public:
	enum class IRQ
	{
		TXE  = CR1_TXEIE,
		RXNE = CR1_RXNEIE,
		TC   = USART_CR1_TCIE,
		IDLE = USART_CR1_IDLEIE
	};

	explicit USART(USART_TypeDef *usartx, uint32_t baudrate,
				   PIN tx = PIN{}, PIN rx = PIN{}) :
		USARTx(usartx),
		BaudRate(baudrate),
		_TX(tx),
		_RX(rx)
	{
#ifndef NDEBUG
		auto chk = [usartx](const PIN& p) {
			if (p.IsValid() && p.periph_base && p.periph_base != (uint32_t)usartx)
				{ __BKPT(0); while(1); }
		};
		chk(tx); chk(rx);
#endif
	}

	USART() = delete;
	USART(const USART&) = delete;
	USART& operator=(const USART&) = delete;
	USART(USART&&) = delete;
	USART& operator=(USART&&) = delete;

	~USART(){};

	SysInitStatus SetUp();

	inline void Enable_IRQ(IRQ irq)
	{
		USARTx->CR1 |= static_cast<uint32_t>(irq);
	}

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
	inline void ClearFlags()
	{
		USARTx->SR = 0;
	}
#elif defined(STM32F7) || defined(STM32G0)
	enum class ISR_FLAGS
	{
		PE   = USART_ICR_PECF,
		FE   = USART_ICR_FECF,
		ORE  = USART_ICR_ORECF,
		IDLE = USART_ICR_IDLECF,
		TC   = USART_ICR_TCCF
	};

	inline void ClearFlags(ISR_FLAGS flag)
	{
		USARTx->ICR = static_cast<uint32_t>(flag);
	}
#endif

	SysStatus Send(uint8_t *data, uint32_t len, uint32_t timeout);
	SysStatus Receive(uint8_t *data, uint32_t len, uint32_t timeout);

	inline void SetBaud(uint32_t baud)
	{
		if (_info != nullptr)
			USARTx->BRR = *_info->bus_clk / baud;
	}

	inline void SetParity(uint32_t parity) { (void)parity; }

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
	};
	static const PeriphInfo usart_table[];

	PIN _TX;
	PIN _RX;

	const PeriphInfo* _info = nullptr;
};

#endif
