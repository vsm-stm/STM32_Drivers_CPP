#ifndef I2C_HPP_
#define I2C_HPP_

#include <system.hpp>
#include <rcc.hpp>
#include <gpio.hpp>
extern "C"
{
	#include <i2c_timing_utility.h>
}

class I2C
{
public:
	I2C_TypeDef *I2Cx;

	enum class I2CMode
	{
		Master,
		Slave
	};

	enum class SpeedMode
	{
		Standart = 0,
		Fast,
		FastPlus
	};


	I2C(I2C_TypeDef *_i2c, SpeedMode _speed, PIN _scl, PIN _sda) :
		I2Cx(_i2c),
		SCL(_scl),
		SDA(_sda)
	{
		switch (_speed)
		{
		case SpeedMode::Standart:
			i2c_speed = 100000;
			break;
		case SpeedMode::Fast:
			i2c_speed = 400000;
			break;
		case SpeedMode::FastPlus:
			i2c_speed = 1000000;
			break;
		default:
			i2c_speed = 0;
			break;
		};
	};
	I2C(I2C_TypeDef *_i2c, uint32_t _speed, PIN _scl, PIN _sda) :
		I2Cx(_i2c),
		SCL(_scl),
		SDA(_sda)
	{
		if((_speed < 80000)
		|| (_speed > 1200000))
			i2c_speed = 0;
		else
			i2c_speed = _speed;
	};
	~I2C(){};

	SYS_StatusTypeDef SetUp();

	SYS_StatusTypeDef Send(uint8_t slave_addr, uint8_t *data, uint32_t len, uint32_t timeout);
	SYS_StatusTypeDef Receive(uint8_t slave_addr, uint8_t *data, uint32_t len, uint32_t timeout);
	SYS_StatusTypeDef ReceiveFromAddr(uint8_t slave_addr, uint8_t *addr, uint8_t addr_len, uint8_t *data, uint32_t len, uint32_t timeout);

private:
	PIN SCL{};
	PIN SDA{};

	uint32_t i2c_speed;

	uint32_t af, bus_clk;
	IRQn_Type IRQ_vector_EV, IRQ_vector_ER;

	SYS_StatusTypeDef SetHard();

	void error_stop();
	void normal_stop();

	const uint32_t MAX_NBYTE_SIZE = 255;
};


// class SPI
// {
// public:
// 	SPI_TypeDef *SPIx;

// 	enum class Master_sel
// 	{
// 		Slave = 0,
// 		Master = SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_MSTR
// 	};


// 	enum class Data_frame_format
// 	{
// 	#if defined(STM32F4)
// 		Byte = 0,
// 		Half_Word = SPI_CR1_DFF
// 	#elif defined(STM32F7)
// 		Byte = 0b111 << SPI_CR2_DS_Pos,
// 		Half_Word = 0b1111 << SPI_CR2_DS_Pos
// 	#endif
// 	};

// 	enum class Frame_Format
// 	{
// 		MSB = 0,
// 		LSB = SPI_CR1_LSBFIRST
// 	};

// 	enum class TYPE
// 	{
// 		RX = 0,
// 		TX = 0,
// 		TXRX
// 	};

// 	enum class cPolPha
// 	{
// 		None = 0,
// 		cPha = SPI_CR1_CPHA,
// 		cPol = SPI_CR1_CPOL,
// 		cPolPha = SPI_CR1_CPHA | SPI_CR1_CPOL
// 	};


// 	enum class IRQ
// 	{
// 		TXE = SPI_CR2_TXEIE,	///< Transmit Data Register Empty interrupt
// 		RXNE = SPI_CR2_RXNEIE,	///< Receive Data Register Not Empty interrupt
// 		ERR = SPI_CR2_ERRIE
// 	};

// 	explicit SPI(SPI_TypeDef *spix, PIN _CLK, PIN _MOSI, PIN _MISO, PIN _SS) :
// 		SPIx(spix),
// 		CLK(_CLK),
// 		MOSI(_MOSI),
// 		MISO(_MISO),
// 		SS(_SS)
// 	{
// 	}

// 	SPI() = delete;
// 	SPI(SPI const &) = default;
// 	SPI(SPI &&) = default;
// 	SPI &operator=(SPI const &) = default;
// 	SPI &operator=(SPI &&) = default;
// 	~SPI(){};

// 	typedef struct 
// 	{
// 		Master_sel mstr;
// 		TYPE type;
// 		Data_frame_format dff;
// 		Frame_Format ff;
// 		cPolPha cpolpha;
// 		uint8_t br;
// 	}Init_struct_Typedef;

// 	SYS_StatusTypeDef SetUp(Master_sel mstr, TYPE type)
// 	{
// 		return SetUp(mstr, type, Data_frame_format::Byte, Frame_Format::MSB, cPolPha::None, 0);
// 	};

// 	SYS_StatusTypeDef SetUp(Init_struct_Typedef Init_struct)
// 	{
// 		return SetUp(Init_struct.mstr, Init_struct.type, Init_struct.dff, Init_struct.ff, Init_struct.cpolpha, Init_struct.br);
// 	}

// 	SYS_StatusTypeDef SetUp(Master_sel mstr, TYPE type, Data_frame_format dff, Frame_Format ff, cPolPha cpolpha, uint8_t br);

// 	inline void Enable()
// 	{
// 		SPIx->CR1 |= SPI_CR1_SPE;
// 	};

// 	inline void Disable()
// 	{
// 		SPIx->CR1 &= ~SPI_CR1_SPE;
// 	};

// 	inline void SlaveSelect(FunctionalState en)
// 	{
// 		if(en)
// 			SS.SetLevel(0);
// 		else
// 			SS.SetLevel(1);
// 	}

// 	inline void DMA_TX(FunctionalState en)
// 	{
// 		if(en)
// 			SPIx->CR2 |= SPI_CR2_TXDMAEN;
// 		else
// 			SPIx->CR2 &= ~SPI_CR2_TXDMAEN;
// 	};

// 	void DMA_RX(FunctionalState en)
// 	{
// 		if(en)
// 			SPIx->CR2 |= SPI_CR2_RXDMAEN;
// 		else
// 			SPIx->CR2 &= ~SPI_CR2_RXDMAEN;
// 	};

// 	/**
// 	 * @brief Enable the specified USART IRQ.
// 	 * @param irq The IRQ to enable.
// 	 */
// 	void Enable_IRQ(IRQ irq)
// 	{
// 		SPIx->CR2 |= static_cast<uint32_t>(irq);

// 		if (!(NVIC_GetEnableIRQ(IRQ_vector)))
// 		{
// 			NVIC_EnableIRQ(IRQ_vector);
// 		}
// 	}

// 	/**
// 	 * @brief Disable the specified USART IRQ.
// 	 * @param irq The IRQ to disable.
// 	 */
// 	void Disable_IRQ(IRQ irq)
// 	{
// 		SPIx->CR2 &= ~(static_cast<uint32_t>(irq));
// 		if (!(SPIx->CR2 & (SPI_CR2_TXEIE | SPI_CR2_RXNEIE | SPI_CR2_ERRIE )))
// 		{
// 			NVIC_DisableIRQ(IRQ_vector);
// 		}
// 	}

// 	SYS_StatusTypeDef Send_Receive(uint8_t* tx_data, uint8_t* rx_data, uint16_t data_len, uint32_t timeout);

// protected:
// 	PIN CLK{};
// 	PIN MOSI{};
// 	PIN MISO{};
// 	PIN SS{};

// 	uint32_t af, bus_clk;
// 	IRQn_Type IRQ_vector;

// 	SYS_StatusTypeDef SetHard();
// };

#endif /* I2C_HPP_ */
