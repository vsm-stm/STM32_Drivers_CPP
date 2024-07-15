#ifndef I2C_HPP_
#define I2C_HPP_

#include <system.hpp>
#include <rcc.hpp>
#include <gpio.hpp>

#if defined(STM32F7)
extern "C"
{
	#include <i2c_timing_utility.h>
}

class I2C
{
public:
	I2C_TypeDef *I2Cx;
	const static uint32_t MAX_NBYTE_SIZE = 255;

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

	enum class IRQ
	{
		TXE =  I2C_CR1_TXIE,
		RXNE = I2C_CR1_RXIE,
		ADDR = I2C_CR1_ADDRIE,
		NACK = I2C_CR1_NACKIE,
		STOP = I2C_CR1_STOPIE,
		TC =   I2C_CR1_TCIE,
		ERR =  I2C_CR1_ERRIE
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

	void Enable_IRQ(IRQ irq)
	{
		IRQn_Type irq_vec;
		if(irq == IRQ::ERR)
			irq_vec = IRQ_vector_ER;
		else
			irq_vec = IRQ_vector_EV;

		I2Cx->CR1 |= static_cast<uint32_t>(irq);

		if (!(NVIC_GetEnableIRQ(irq_vec)))
		{
			NVIC_EnableIRQ(irq_vec);
		}
	}

	void Disable_IRQ(IRQ irq)
	{
		I2Cx->CR1 &= ~(static_cast<uint32_t>(irq));
		if(irq == IRQ::ERR)
		{
			NVIC_DisableIRQ(IRQ_vector_ER);
		}
		else
		{
			if (!(I2Cx->CR1 & (I2C_CR1_RXIE | I2C_CR1_TXIE | I2C_CR1_STOPIE | I2C_CR1_TCIE | I2C_CR1_ADDRIE | I2C_CR1_NACKIE)))
			{
				NVIC_DisableIRQ(IRQ_vector_EV);
			}
		}
	}

protected:
	PIN SCL{};
	PIN SDA{};

	uint32_t i2c_speed;

	uint32_t af, bus_clk;
	IRQn_Type IRQ_vector_EV, IRQ_vector_ER;

	SYS_StatusTypeDef SetHard();

	void error_stop();
	void normal_stop();
};

#endif

#endif /* I2C_HPP_ */
