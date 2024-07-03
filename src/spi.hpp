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

	enum class NSS_ctrl
	{
		Hard = 0,
		Software
	};

	enum class Data_frame_format
	{
	#if defined(STM32F4)
		Byte = 0,
		Half_Word = SPI_CR1_DFF
	#elif defined(STM32F7)
		Byte = 0b111 << SPI_CR2_DS_Pos,
		Half_Word = 0b1111 << SPI_CR2_DS_Pos
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

	typedef struct 
	{
		Master_sel mstr;
		NSS_ctrl nss_ctrl;
		TYPE type;
		Data_frame_format dff;
		Frame_Format ff;
		cPolPha cpolpha;
		uint8_t br;
	}Init_struct_Typedef;

	SYS_StatusTypeDef SetUp(Master_sel mstr, TYPE type)
	{
		return SetUp(mstr, NSS_ctrl::Hard, type, Data_frame_format::Byte, Frame_Format::MSB, cPolPha::None, 0);
	};

	SYS_StatusTypeDef SetUp(Init_struct_Typedef Init_struct)
	{
		return SetUp(Init_struct.mstr, Init_struct.nss_ctrl, Init_struct.type, Init_struct.dff, Init_struct.ff, Init_struct.cpolpha, Init_struct.br);
	}

	SYS_StatusTypeDef SetUp(Master_sel mstr, NSS_ctrl nss, TYPE type, Data_frame_format dff, Frame_Format ff, cPolPha cpolpha, uint8_t br);

	inline void Enable()
	{
		SPIx->CR1 |= SPI_CR1_SPE;
	};

	inline void Disable()
	{
		SPIx->CR1 &= ~SPI_CR1_SPE;
	};

	inline void SlaveSelect(FunctionalState en)
	{
		if(nss_ctrl == NSS_ctrl::Software)
		{
			if(en)
				SS.SetLevel(0);
			else
				SS.SetLevel(1);
		}
	}

	inline void DMA_TX(FunctionalState en)
	{
		if(en)
			SPIx->CR2 |= SPI_CR2_TXDMAEN;
		else
			SPIx->CR2 &= ~SPI_CR2_TXDMAEN;
	};

	void DMA_RX(FunctionalState en)
	{
		if(en)
			SPIx->CR2 |= SPI_CR2_RXDMAEN;
		else
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

	SYS_StatusTypeDef Send_Receive(uint8_t* tx_data, uint8_t* rx_data, uint16_t data_len, uint32_t timeout);

protected:
	PIN CLK{};
	PIN MOSI{};
	PIN MISO{};
	PIN SS{};

	uint32_t af, bus_clk;
	NSS_ctrl nss_ctrl;
	IRQn_Type IRQ_vector;

	SYS_StatusTypeDef SetHard();
};

#endif /* SPI_HPP_ */
