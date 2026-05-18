#ifndef SPI_HPP_
#define SPI_HPP_

#include "system.hpp"
#include "rcc.hpp"
#include "gpio.hpp"

class SPI
{
public:
	SPI_TypeDef* SPIx;

	// -----------------------------------------------------------------------
	// Configuration enumerations
	// -----------------------------------------------------------------------

	enum class Master_sel
	{
		Slave  = 0,
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
		Byte      = 0,
		Half_Word = SPI_CR1_DFF
	#elif defined(STM32F7) || defined(STM32G0)
		Byte      = 0b0111 << SPI_CR2_DS_Pos,
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
		None    = 0,
		cPha    = SPI_CR1_CPHA,
		cPol    = SPI_CR1_CPOL,
		cPolPha = SPI_CR1_CPHA | SPI_CR1_CPOL
	};

	enum class BaudRate {
		DIV2 = 0,
		DIV4,
		DIV8,
		DIV16,
		DIV32,
		DIV64,
		DIV128,
		DIV256
	};

	enum class IRQ
	{
		TXE  = SPI_CR2_TXEIE,
		RXNE = SPI_CR2_RXNEIE,
		ERR  = SPI_CR2_ERRIE
	};

	// -----------------------------------------------------------------------
	// Compile-time pin tables — PIN carries port, pin number and AF.
	// Zero memory footprint: used only in constant expressions.
	// -----------------------------------------------------------------------

#if defined(STM32G0)

	struct SCK {
		static constexpr PIN PA5  = { GPIOA_BASE,  5,  0, SPI1_BASE };
		static constexpr PIN PB3  = { GPIOB_BASE,  3,  0, SPI1_BASE };
		static constexpr PIN PB8  = { GPIOB_BASE,  8,  1, SPI2_BASE };
		static constexpr PIN PB10 = { GPIOB_BASE, 10,  5, SPI2_BASE };
		static constexpr PIN PB13 = { GPIOB_BASE, 13,  0, SPI2_BASE };
	};
	struct MOSI {
		static constexpr PIN PA7  = { GPIOA_BASE,  7,  0, SPI1_BASE };
		static constexpr PIN PB5  = { GPIOB_BASE,  5,  0, SPI1_BASE };
		static constexpr PIN PB7  = { GPIOB_BASE,  7,  1, SPI2_BASE };
		static constexpr PIN PB11 = { GPIOB_BASE, 11,  0, SPI2_BASE };
		static constexpr PIN PB15 = { GPIOB_BASE, 15,  0, SPI2_BASE };
	};
	struct MISO {
		static constexpr PIN PA6  = { GPIOA_BASE,  6,  0, SPI1_BASE };
		static constexpr PIN PB4  = { GPIOB_BASE,  4,  0, SPI1_BASE };
		static constexpr PIN PB6  = { GPIOB_BASE,  6,  4, SPI2_BASE };
		static constexpr PIN PB14 = { GPIOB_BASE, 14,  0, SPI2_BASE };
	};
	struct SS {
		static constexpr PIN PA4  = { GPIOA_BASE,  4,  0, SPI1_BASE };
		static constexpr PIN PA15 = { GPIOA_BASE, 15,  0, SPI1_BASE };
		static constexpr PIN PB9  = { GPIOB_BASE,  9,  5, SPI2_BASE };
		static constexpr PIN PB12 = { GPIOB_BASE, 12,  0, SPI2_BASE };
	};

#elif defined(STM32F4) || defined(STM32F7)

	struct SCK {
		static constexpr PIN PA5      = { GPIOA_BASE,  5, 5, SPI1_BASE };
		static constexpr PIN SPI1_PB3 = { GPIOB_BASE,  3, 5, SPI1_BASE };
		static constexpr PIN PB10     = { GPIOB_BASE, 10, 5, SPI2_BASE };
		static constexpr PIN PB13     = { GPIOB_BASE, 13, 5, SPI2_BASE };
		static constexpr PIN SPI3_PB3 = { GPIOB_BASE,  3, 6, SPI3_BASE };
		static constexpr PIN PC10     = { GPIOC_BASE, 10, 6, SPI3_BASE };
	};
	struct MOSI {
		static constexpr PIN PA7      = { GPIOA_BASE,  7, 5, SPI1_BASE };
		static constexpr PIN SPI1_PB5 = { GPIOB_BASE,  5, 5, SPI1_BASE };
		static constexpr PIN PB15     = { GPIOB_BASE, 15, 5, SPI2_BASE };
		static constexpr PIN PC3      = { GPIOC_BASE,  3, 5, SPI2_BASE };
		static constexpr PIN SPI3_PB5 = { GPIOB_BASE,  5, 6, SPI3_BASE };
		static constexpr PIN PC12     = { GPIOC_BASE, 12, 6, SPI3_BASE };
	};
	struct MISO {
		static constexpr PIN PA6      = { GPIOA_BASE,  6, 5, SPI1_BASE };
		static constexpr PIN SPI1_PB4 = { GPIOB_BASE,  4, 5, SPI1_BASE };
		static constexpr PIN PB14     = { GPIOB_BASE, 14, 5, SPI2_BASE };
		static constexpr PIN PC2      = { GPIOC_BASE,  2, 5, SPI2_BASE };
		static constexpr PIN SPI3_PB4 = { GPIOB_BASE,  4, 6, SPI3_BASE };
		static constexpr PIN PC11     = { GPIOC_BASE, 11, 6, SPI3_BASE };
	};
	struct SS {
		static constexpr PIN PA4       = { GPIOA_BASE,  4, 5, SPI1_BASE };
		static constexpr PIN PA15_SPI1 = { GPIOA_BASE, 15, 5, SPI1_BASE };
		static constexpr PIN PB9       = { GPIOB_BASE,  9, 5, SPI2_BASE };
		static constexpr PIN PB12      = { GPIOB_BASE, 12, 5, SPI2_BASE };
		static constexpr PIN PA15_SPI3 = { GPIOA_BASE, 15, 6, SPI3_BASE };
	};

#endif

	// -----------------------------------------------------------------------
	// Constructor
	// -----------------------------------------------------------------------

	explicit SPI(SPI_TypeDef* spix,
				 PIN sck,
				 PIN mosi,
				 PIN miso = PIN{},
				 PIN ss   = PIN{})
		: SPIx(spix), _clk(sck), _mosi(mosi), _miso(miso), _ss(ss)
	{
#ifndef NDEBUG
		auto chk = [spix](const PIN& p) {
			if (p.IsValid() && p.periph_base && p.periph_base != (uint32_t)spix)
				{ __BKPT(0); while(1); }
		};
		chk(sck); chk(mosi); chk(miso); chk(ss);
#endif
	}

	SPI() = delete;
	SPI(SPI const&)            = default;
	SPI(SPI&&)                 = default;
	SPI& operator=(SPI const&) = default;
	SPI& operator=(SPI&&)      = default;
	~SPI() = default;

	// -----------------------------------------------------------------------
	// Init helpers
	// -----------------------------------------------------------------------

	typedef struct
	{
		Master_sel        mstr;
		NSS_ctrl          nss_ctrl;
		TYPE              type;
		Data_frame_format dff;
		Frame_Format      ff;
		cPolPha           cpolpha;
		BaudRate          br;
	} Init_struct_Typedef;

	SysInitStatus SetUp(Master_sel mstr, TYPE type)
	{
		return SetUp(mstr, NSS_ctrl::Hard, type,
					 Data_frame_format::Byte, Frame_Format::MSB, cPolPha::None, BaudRate::DIV2);
	}

	SysInitStatus SetUp(Init_struct_Typedef s)
	{
		return SetUp(s.mstr, s.nss_ctrl, s.type, s.dff, s.ff, s.cpolpha, s.br);
	}

	SysInitStatus SetUp(Master_sel mstr, NSS_ctrl nss, TYPE type,
						Data_frame_format dff, Frame_Format ff,
						cPolPha cpolpha, BaudRate br);

	// -----------------------------------------------------------------------
	// Runtime control
	// -----------------------------------------------------------------------

	inline void Enable()  { SPIx->CR1 |=  SPI_CR1_SPE; }
	inline void Disable() { SPIx->CR1 &= ~SPI_CR1_SPE; }

	inline void SlaveSelect(FunctionalState en)
	{
		if (nss_ctrl == NSS_ctrl::Software)
		{
			if (en)
			{
				if (Master_slave == Master_sel::Master) _ss.SetLevel(0);
				else SPIx->CR1 &= ~SPI_CR1_SSI;
			}
			else
			{
				if (Master_slave == Master_sel::Master) _ss.SetLevel(1);
				else SPIx->CR1 |= SPI_CR1_SSI;
			}
		}
	}

	inline void DMA_TX(FunctionalState en)
	{
		if (en) SPIx->CR2 |=  SPI_CR2_TXDMAEN;
		else    SPIx->CR2 &= ~SPI_CR2_TXDMAEN;
	}

	inline void DMA_RX(FunctionalState en)
	{
		if (en) SPIx->CR2 |=  SPI_CR2_RXDMAEN;
		else    SPIx->CR2 &= ~SPI_CR2_RXDMAEN;
	}

	void Enable_IRQ(IRQ irq)
	{
		SPIx->CR2 |= static_cast<uint32_t>(irq);
		if (_info != nullptr && !NVIC_GetEnableIRQ(_info->irq))
			NVIC_EnableIRQ(_info->irq);
	}

	void Disable_IRQ(IRQ irq)
	{
		SPIx->CR2 &= ~static_cast<uint32_t>(irq);
		if (_info != nullptr &&
			!(SPIx->CR2 & (SPI_CR2_TXEIE | SPI_CR2_RXNEIE | SPI_CR2_ERRIE)))
			NVIC_DisableIRQ(_info->irq);
	}

	SysStatus Send(uint8_t* tx_data, uint16_t data_len, uint32_t timeout);
	SysStatus Send_Receive(uint8_t* tx_data, uint8_t* rx_data,
						   uint16_t data_len, uint32_t timeout);

private:
	struct PeriphInfo {
		SPI_TypeDef*        periph;
		volatile uint32_t*  clk_reg;
		uint32_t            clk_bit;
		volatile uint32_t*  rst_reg;
		uint32_t            rst_bit;
		uint32_t const*     bus_clk;
		IRQn_Type           irq;
	};
	static const PeriphInfo spi_table[];

protected:
	PIN    _clk{};
	PIN    _mosi{};
	PIN    _miso{};
	PIN    _ss{};

	NSS_ctrl   nss_ctrl{};
	Master_sel Master_slave{};

	const PeriphInfo* _info = nullptr;

	SysInitStatus SetHard();
};

#endif /* SPI_HPP_ */
