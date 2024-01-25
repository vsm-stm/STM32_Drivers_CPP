#ifndef TIM_HPP_
#define TIM_HPP_

#include <system_f4.hpp>
#include <rcc.hpp>
#include <gpio.hpp>

class TIM
{
private:
	IRQn_Type IRQ_vector;
	uint32_t bus_clk;
	uint32_t af;

	SYS_StatusTypeDef SetUp();
public:
	TIM_TypeDef *TIMx;
	uint32_t Freq;

	TIM(TIM_TypeDef *timx):
			TIMx(timx)
	{
		SetUp();
	};

	~TIM(){};

	SYS_StatusTypeDef StartPeriodicIRQ(uint32_t freq);
	inline void StopPeriodicIRQ()
	{
		TIMx->CR1 &= ~TIM_CR1_CEN;
	};

	inline void ClearFlags()
	{
		TIMx->SR &= ~TIM_SR_UIF;
	};
};




#endif
