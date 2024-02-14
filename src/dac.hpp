#ifndef DAC_HPP_
#define DAC_HPP_

#include <system_f4.hpp>
#include <rcc.hpp>
#include <gpio.hpp>

class DAC_Module
{
private:
	PIN _CH1{};
	PIN _CH2{};
public:
	DAC_TypeDef *DACx;

	SYS_StatusTypeDef SetUp();

	DAC_Module(DAC_TypeDef *dacx, PIN CH1, PIN CH2) :
		DACx(dacx),
		_CH1(CH1),
		_CH2(CH2)
	{};
	~DAC_Module(){};

	void SetCh1Out(uint32_t data)
	{
		DAC->DHR12R1 = data;
	};

	void SetCh2Out(uint32_t data)
	{
		DAC->DHR12R2 = data;
	};
};









#endif /* DAC_HPP_ */
