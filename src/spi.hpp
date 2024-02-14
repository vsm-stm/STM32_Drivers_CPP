#ifndef SPI_HPP_
#define SPI_HPP_

#include <memory>
#include <system_f4.hpp>
#include <rcc.hpp>
#include <gpio.hpp>

class SPI
{
public:
	SPI_TypeDef *SPIx;

	enum class IRQ
	{
		TXE = SPI_CR2_TXEIE,	///< Transmit Data Register Empty interrupt
		RXNE = SPI_CR2_RXNEIE,	///< Receive Data Register Not Empty interrupt
		ERR = SPI_CR2_ERRIE
	};

	explicit SPI(SPI_TypeDef *spix, PIN CLK, PIN MOSI, PIN MISO) :
		SPIx(spix),
		_CLK(CLK),
		_MOSI(MOSI),
		_MISO(MISO)
	{
	}

	SPI() = delete;
	SPI(SPI const &) = default;
	SPI(SPI &&) = default;
	SPI &operator=(SPI const &) = default;
	SPI &operator=(SPI &&) = default;
	~SPI(){};

	void Enable()
	{
		SPIx->CR1 |= SPI_CR1_SPE;
	};

	void Disable()
	{
		SPIx->CR1 &= ~SPI_CR1_SPE;
	};

	void Enable_DMA_TX()
	{
		SPIx->CR2 |= SPI_CR2_TXDMAEN;
	};

	void Disable_DMA_TX()
	{
		SPIx->CR2 &= ~SPI_CR2_TXDMAEN;
	};

	void Enable_DMA_RX()
	{
		SPIx->CR2 |= SPI_CR2_RXDMAEN;
	};

	void Disable_DMA_RX()
	{
		SPIx->CR2 &= ~SPI_CR2_RXDMAEN;
	};

		/**
	 * @brief Enable the specified USART IRQ.
	 * @param irq The IRQ to enable.
	 */
	void Enable_IRQ(IRQ irq)
	{
		SPIx->CR2 |= static_cast<uint32_t>(irq);

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
		SPIx->CR2 &= ~(static_cast<uint32_t>(irq));
		if (!(SPIx->CR2 & (SPI_CR2_TXEIE | SPI_CR2_RXNEIE | SPI_CR2_ERRIE )))
		{
			NVIC_DisableIRQ(IRQ_vector);
		}
	}

protected:
	PIN _CLK{};
	PIN _MOSI{};
	PIN _MISO{};

	uint32_t af, bus_clk;
	IRQn_Type IRQ_vector;

	SYS_StatusTypeDef SetHard();
};

class SPI_Slave_TX : public SPI
{
private:
	/* data */
public:

	SYS_StatusTypeDef SetUp();

	SPI_Slave_TX(SPI_TypeDef *spix, PIN CLK, PIN MISO) : SPI(spix, CLK, PIN(0,0), MISO){};
	~SPI_Slave_TX(){};
};





#endif /* SPI_HPP_ */
