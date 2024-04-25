#ifndef SPI_HPP_
#define SPI_HPP_

#include <system.hpp>
#include <rcc.hpp>
#include <gpio.hpp>
#include <memory>

class SPI
{
public:
	SPI_TypeDef *SPIx;

	enum class Master_sel
	{
		Slave = 0,
		Master = SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_MSTR
	};


	enum class Data_frame_format
	{
		Byte = 0,
		#if defined(STM32F4)
		Half_Word = SPI_CR1_DFF
		#elif defined(STM32F7)
		Half_Word = 0b111
		#endif
	};

	enum class Frame_Format
	{
		MSB = 0,
		LSB = SPI_CR1_LSBFIRST
	};

	enum class TYPE
	{
		RX = 0,
		TX = 0,
		TXRX
	};

	enum class cPolPha
	{
		None = 0,
		cPha = SPI_CR1_CPHA,
		cPol = SPI_CR1_CPOL,
		cPolPha = SPI_CR1_CPHA | SPI_CR1_CPOL
	};


	enum class IRQ
	{
		TXE = SPI_CR2_TXEIE,	///< Transmit Data Register Empty interrupt
		RXNE = SPI_CR2_RXNEIE,	///< Receive Data Register Not Empty interrupt
		ERR = SPI_CR2_ERRIE
	};

	explicit SPI(SPI_TypeDef *spix, PIN _CLK, PIN _MOSI, PIN _MISO, PIN _SS) :
		SPIx(spix),
		CLK(_CLK),
		MOSI(_MOSI),
		MISO(_MISO),
		SS(_SS)
	{
	}

	SPI() = delete;
	SPI(SPI const &) = default;
	SPI(SPI &&) = default;
	SPI &operator=(SPI const &) = default;
	SPI &operator=(SPI &&) = default;
	~SPI(){};

	SYS_StatusTypeDef SetUp(Master_sel mstr, TYPE type)
	{
		return SetUp(mstr, type, Data_frame_format::Byte, Frame_Format::MSB, cPolPha::None, 0);
	};

	SYS_StatusTypeDef SetUp(Master_sel mstr, TYPE type, Data_frame_format dff, Frame_Format ff, cPolPha cpolpha, uint8_t br);

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
	PIN CLK{};
	PIN MOSI{};
	PIN MISO{};
	PIN SS{};

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

	SPI_Slave_TX(SPI_TypeDef *spix, PIN CLK, PIN MISO) : SPI(spix, CLK, {}, MISO, {}){};
	~SPI_Slave_TX(){};
};

class SPI_Master : public SPI
{
private:

public:
	SPI_Master(SPI_TypeDef *spix, PIN CLK, PIN MOSI, PIN MISO) : SPI(spix, CLK, MOSI, MISO, {}){};
	~SPI_Master(){};
};





#endif /* SPI_HPP_ */
