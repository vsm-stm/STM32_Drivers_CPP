#ifndef SPI_HPP_
#define SPI_HPP_

#include "system.hpp"
#include "rcc.hpp"
#include "gpio.hpp"
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
	#elif defined(STM32F7) || defined(STM32G0)
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
		TXRX = 0,
		TX   = 1,
		RX   = 2,
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

	SysInitStatus SetUp(Master_sel mstr, TYPE type)
	{
		return SetUp(mstr, NSS_ctrl::Hard, type, Data_frame_format::Byte, Frame_Format::MSB, cPolPha::None, 0);
	};

	SysInitStatus SetUp(Init_struct_Typedef Init_struct)
	{
		return SetUp(Init_struct.mstr, Init_struct.nss_ctrl, Init_struct.type, Init_struct.dff, Init_struct.ff, Init_struct.cpolpha, Init_struct.br);
	}

	SysInitStatus SetUp(Master_sel mstr, NSS_ctrl nss, TYPE type, Data_frame_format dff, Frame_Format ff, cPolPha cpolpha, uint8_t br);

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
			{
				if(Master_slave == Master_sel::Master)
					SS.SetLevel(0);
				else
					SPIx->CR1 &= ~SPI_CR1_SSI;
			}
			else
			{
				if(Master_slave == Master_sel::Master)
					SS.SetLevel(1);
				else
					SPIx->CR1 |= SPI_CR1_SSI;
			}
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

		if (_info != nullptr && !(NVIC_GetEnableIRQ(_info->irq)))
		{
			NVIC_EnableIRQ(_info->irq);
		}
	}

	/**
	 * @brief Disable the specified SPI IRQ.
	 * @param irq The IRQ to disable.
	 */
	void Disable_IRQ(IRQ irq)
	{
		SPIx->CR2 &= ~(static_cast<uint32_t>(irq));
		if (_info != nullptr && !(SPIx->CR2 & (SPI_CR2_TXEIE | SPI_CR2_RXNEIE | SPI_CR2_ERRIE)))
		{
			NVIC_DisableIRQ(_info->irq);
		}
	}
	SysStatus Send(uint8_t* tx_data, uint16_t data_len, uint32_t timeout);
	SysStatus Send_Receive(uint8_t* tx_data, uint8_t* rx_data, uint16_t data_len, uint32_t timeout);

private:
	struct PeriphInfo {
		SPI_TypeDef*        periph;
		volatile uint32_t*  clk_reg;
		uint32_t            clk_bit;
		volatile uint32_t*  rst_reg;
		uint32_t            rst_bit;
		uint32_t const*     bus_clk;
		IRQn_Type           irq;
		uint8_t             af;
	};
	static const PeriphInfo spi_table[];

protected:
	PIN CLK{};
	PIN MOSI{};
	PIN MISO{};
	PIN SS{};

	NSS_ctrl nss_ctrl;
	Master_sel Master_slave;
	const PeriphInfo* _info = nullptr;

	SysInitStatus SetHard();
};

#endif /* SPI_HPP_ */
