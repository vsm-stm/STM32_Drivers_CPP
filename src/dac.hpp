#ifndef DAC_HPP_
#define DAC_HPP_

#include "system.hpp"
#include "rcc.hpp"
#include "gpio.hpp"

class DAC_Module
{
public:
	DAC_TypeDef *DACx;

	enum class CHANNEL
	{
		CH1 = 0,
		CH2
	};

	SYS_StatusTypeDef SetUp();

	DAC_Module(DAC_TypeDef *dacx, CHANNEL _ch) :
		DACx(dacx),
		ch(_ch)
	{
		if(ch == CHANNEL::CH1)
			out_pin = PIN(GPIOA,4);
		if(ch == CHANNEL::CH2)
			out_pin = PIN(GPIOA,5);
	};
	~DAC_Module(){};

	inline void SetValue(uint32_t data)
	{
		if(data > 0xFFF)
			return;
		*data_out = data;
	};
private:
	CHANNEL ch;
	PIN out_pin{};
	uint32_t* data_out;
};









#endif /* DAC_HPP_ */
