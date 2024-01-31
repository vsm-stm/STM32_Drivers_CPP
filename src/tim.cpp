#include <tim.hpp>
// #include <gpio.hpp>


SYS_StatusTypeDef TIM::SetUp()
{
	if(TIMx == TIM1)
	{
		RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
		bus_clk = ClockSystem::TIMxAPB2Clock;
		IRQ_vector = TIM1_UP_TIM10_IRQn; //todo
		af = 1;
	}else
	if(TIMx == TIM8)
	{
		RCC->APB2ENR |= RCC_APB2ENR_TIM8EN;
		bus_clk = ClockSystem::TIMxAPB2Clock;
		IRQ_vector = TIM8_UP_TIM13_IRQn; //todo
		af = 3;
	}else
	if(TIMx == TIM9)
	{
		RCC->APB2ENR |= RCC_APB2ENR_TIM9EN;
		bus_clk = ClockSystem::TIMxAPB2Clock;
		IRQ_vector = TIM1_BRK_TIM9_IRQn;
		af = 3;
	}else
	if(TIMx == TIM10)
	{
		RCC->APB2ENR |= RCC_APB2ENR_TIM10EN;
		bus_clk = ClockSystem::TIMxAPB2Clock;
		IRQ_vector = TIM1_UP_TIM10_IRQn;
		af = 3;
	}else
	if(TIMx == TIM11)
	{
		RCC->APB2ENR |= RCC_APB2ENR_TIM11EN;
		bus_clk = ClockSystem::TIMxAPB2Clock;
		IRQ_vector = TIM1_TRG_COM_TIM11_IRQn;
		af = 3;
	}else
	if(TIMx == TIM2)
	{
		RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
		bus_clk = ClockSystem::TIMxAPB1Clock;
		IRQ_vector = TIM2_IRQn; //todo
		af = 1;
	}else
	if(TIMx == TIM3)
	{
		RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
		bus_clk = ClockSystem::TIMxAPB1Clock;
		IRQ_vector = TIM3_IRQn; //todo
		af = 2;
	}else
	if(TIMx == TIM4)
	{
		RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;
		bus_clk = ClockSystem::TIMxAPB1Clock;
		IRQ_vector = TIM4_IRQn; //todo
		af = 2;
	}else
	if(TIMx == TIM5)
	{
		RCC->APB1ENR |= RCC_APB1ENR_TIM5EN;
		bus_clk = ClockSystem::TIMxAPB1Clock;
		IRQ_vector = TIM5_IRQn; //todo
		af = 2;
	}else
	if(TIMx == TIM6)
	{
		RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;
		bus_clk = ClockSystem::TIMxAPB1Clock;
		IRQ_vector = TIM6_DAC_IRQn; //todo
		af = -1;
	}else
	if(TIMx == TIM7)
	{
		RCC->APB1ENR |= RCC_APB1ENR_TIM7EN;
		bus_clk = ClockSystem::TIMxAPB1Clock;
		IRQ_vector = TIM7_IRQn; //todo
		af = -1;
	}else
	if(TIMx == TIM12)
	{
		RCC->APB1ENR |= RCC_APB1ENR_TIM12EN;
		bus_clk = ClockSystem::TIMxAPB1Clock;
		IRQ_vector = TIM8_BRK_TIM12_IRQn; //todo
		af = 9;
	}else
	if(TIMx == TIM13)
	{
		RCC->APB1ENR |= RCC_APB1ENR_TIM13EN;
		bus_clk = ClockSystem::TIMxAPB1Clock;
		IRQ_vector = TIM8_UP_TIM13_IRQn; //todo
		af = 9;
	}else
	if(TIMx == TIM14)
	{
		RCC->APB1ENR |= RCC_APB1ENR_TIM14EN;
		bus_clk = ClockSystem::TIMxAPB1Clock;
		IRQ_vector = TIM8_TRG_COM_TIM14_IRQn; //todo
		af = 9;
	}else
		return SYS_ERROR;

	if (CH1_pin.PORT != nullptr)
	{
		CH1_pin.SetUp(PIN::TYPE::AF_PushPull, af);
	}
	if (CH2_pin.PORT != nullptr)
	{
		CH2_pin.SetUp(PIN::TYPE::AF_PushPull, af);
	}
	if (CH3_pin.PORT != nullptr)
	{
		CH3_pin.SetUp(PIN::TYPE::AF_PushPull, af);
	}
	if (CH4_pin.PORT != nullptr)
	{
		CH4_pin.SetUp(PIN::TYPE::AF_PushPull, af);
	}

	return SYS_OK;
}

/* freq in Hz*/
SYS_StatusTypeDef TIM::StartPeriodicIRQ(uint32_t freq)
{
	if((freq == 0)
	|| (freq > 1000)) //todo for high freq
		return SYS_ERROR;

	SYS_StatusTypeDef setup_status = SetUp();

	if(setup_status != SYS_OK)
		return setup_status;
	
	TIMx->PSC = bus_clk/10000 - 1;
	TIMx->ARR = 10000/freq - 1;
	TIMx->DIER = TIM_DIER_UIE;
	TIMx->SR = 0;

	NVIC_EnableIRQ(IRQ_vector);

	TIMx->CR1 = TIM_CR1_CEN;
	return SYS_OK;
}

SYS_StatusTypeDef TIM::SetupGenPulses(uint32_t freq, TIM_Channel ch1, uint32_t ch1_width, TIM_Channel ch2, uint32_t ch2_width)
{
	if((freq == 0)
	|| (freq > 1000)
	|| (CH1_pin.PORT == nullptr)
	|| (CH2_pin.PORT == nullptr)) //todo for high freq
		return SYS_ERROR;

	SYS_StatusTypeDef setup_status = SetUp();

	if(setup_status != SYS_OK)
		return setup_status;

	TIMx->ARR = 99;
	TIMx->PSC = bus_clk/((TIMx->ARR+1) * freq * 2) - 1;
	TIMx->CCR1 = ch1_width;
	TIMx->CCR2 = ch2_width;
	TIMx->BDTR = TIM_BDTR_MOE;
	TIMx->DIER = TIM_DIER_UIE;
	TIMx->CCER = TIM_CCER_CC1E |
				 TIM_CCER_CC1NE |
				 TIM_CCER_CC2E  |
				 TIM_CCER_CC2NE;
	TIMx->CCMR1 = TIM_CCMR1_OC1M_0 |
				  TIM_CCMR1_OC1M_1 | // toggle mode
				  TIM_CCMR1_OC2M_0 |
				  TIM_CCMR1_OC2M_1 ; // toggle mode

	NVIC_EnableIRQ(IRQ_vector);
	// TIMx->CR1 |= TIM_CR1_CEN;

	return SYS_OK;
}

void TIM::GenPulses_IRQ()
{
	ClearFlags();
	if(--_pulses == 0)
		TIMx->CR1 &= ~TIM_CR1_CEN;

	// TIMx->CNT = 0;
}


