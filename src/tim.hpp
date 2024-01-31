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

	PIN CH1_pin{};
	PIN CH2_pin{};
	PIN CH3_pin{};
	PIN CH4_pin{};

	SYS_StatusTypeDef SetUp();

	uint32_t _pulses; // @todo create subclass
public:
	TIM_TypeDef *TIMx;
	uint32_t Freq;

	TIM(TIM_TypeDef *timx):
			TIMx(timx)
	{};

	TIM(TIM_TypeDef *timx, PIN _ch1, PIN _ch2):
			TIMx(timx),
			CH1_pin(_ch1),
			CH2_pin(_ch2)
	{};

	~TIM(){};

	enum class TIM_Channel
	{
		CHANNEL_1 = 1,
		CHANNEL_2,
		CHANNEL_3,
		CHANNEL_4
	};

	SYS_StatusTypeDef StartPeriodicIRQ(uint32_t freq);

	SYS_StatusTypeDef SetupGenPulses(uint32_t freq, TIM_Channel ch1, uint32_t ch1_width, TIM_Channel ch2, uint32_t ch2_width); // @todo flex params count, maybe create subclass
	void GenPulse(int32_t pulses)
	{
		pulses = pulses;

		if(pulses > 0)
		{
			TIMx->CCER &= ~TIM_CCER_CC1P;
			_pulses += pulses;
		}
		else
		{
			TIMx->CCER |= TIM_CCER_CC1P;
			_pulses += -pulses;
		}
		TIMx->CR1 |= TIM_CR1_CEN;
	}
	
	void GenPulses_IRQ();


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
