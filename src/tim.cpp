#include <tim.hpp>
// #include <gpio.hpp>


SysInitStatus TIM::SetHard()
{
	if(TIMx == TIM1)
	{
		RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
		bus_clk = System::TIMxAPB2Clock;
		IRQ_vector = TIM1_UP_TIM10_IRQn; //todo
		af = 1;
	}else
	if(TIMx == TIM8)
	{
		RCC->APB2ENR |= RCC_APB2ENR_TIM8EN;
		bus_clk = System::TIMxAPB2Clock;
		IRQ_vector = TIM8_UP_TIM13_IRQn; //todo
		af = 3;
	}else
	if(TIMx == TIM9)
	{
		RCC->APB2ENR |= RCC_APB2ENR_TIM9EN;
		bus_clk = System::TIMxAPB2Clock;
		IRQ_vector = TIM1_BRK_TIM9_IRQn;
		af = 3;
	}else
	if(TIMx == TIM10)
	{
		RCC->APB2ENR |= RCC_APB2ENR_TIM10EN;
		bus_clk = System::TIMxAPB2Clock;
		IRQ_vector = TIM1_UP_TIM10_IRQn;
		af = 3;
	}else
	if(TIMx == TIM11)
	{
		RCC->APB2ENR |= RCC_APB2ENR_TIM11EN;
		bus_clk = System::TIMxAPB2Clock;
		IRQ_vector = TIM1_TRG_COM_TIM11_IRQn;
		af = 3;
	}else
	if(TIMx == TIM2)
	{
		RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
		bus_clk = System::TIMxAPB1Clock;
		IRQ_vector = TIM2_IRQn; //todo
		af = 1;
	}else
	if(TIMx == TIM3)
	{
		RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
		bus_clk = System::TIMxAPB1Clock;
		IRQ_vector = TIM3_IRQn; //todo
		af = 2;
	}else
	if(TIMx == TIM4)
	{
		RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;
		bus_clk = System::TIMxAPB1Clock;
		IRQ_vector = TIM4_IRQn; //todo
		af = 2;
	}else
	if(TIMx == TIM5)
	{
		RCC->APB1ENR |= RCC_APB1ENR_TIM5EN;
		bus_clk = System::TIMxAPB1Clock;
		IRQ_vector = TIM5_IRQn; //todo
		af = 2;
	}else
	if(TIMx == TIM6)
	{
		RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;
		bus_clk = System::TIMxAPB1Clock;
		IRQ_vector = TIM6_DAC_IRQn; //todo
		af = -1;
	}else
	if(TIMx == TIM7)
	{
		RCC->APB1ENR |= RCC_APB1ENR_TIM7EN;
		bus_clk = System::TIMxAPB1Clock;
		IRQ_vector = TIM7_IRQn; //todo
		af = -1;
	}else
	if(TIMx == TIM12)
	{
		RCC->APB1ENR |= RCC_APB1ENR_TIM12EN;
		bus_clk = System::TIMxAPB1Clock;
		IRQ_vector = TIM8_BRK_TIM12_IRQn; //todo
		af = 9;
	}else
	if(TIMx == TIM13)
	{
		RCC->APB1ENR |= RCC_APB1ENR_TIM13EN;
		bus_clk = System::TIMxAPB1Clock;
		IRQ_vector = TIM8_UP_TIM13_IRQn; //todo
		af = 9;
	}else
	if(TIMx == TIM14)
	{
		RCC->APB1ENR |= RCC_APB1ENR_TIM14EN;
		bus_clk = System::TIMxAPB1Clock;
		IRQ_vector = TIM8_TRG_COM_TIM14_IRQn; //todo
		af = 9;
	}else
		return SysInitStatus::InitError;

	return SysInitStatus::InitOK;
}

SysInitStatus TIM::SetFreq(uint32_t freq)
{
	uint32_t pcs = 0, arr = 0;

	do
	{
		arr += 10;

		if(arr > 0xFFFF)
			return SysInitStatus::InitError;

		pcs = bus_clk/((arr+1) * freq) - 1;

	} while (pcs > 0xFFFF);	
	
	TIMx->ARR = arr;
	TIMx->PSC = pcs;

	return SysInitStatus::InitOK;
}

/* freq in Hz*/
SysInitStatus TIM_PeriodicIRQ::SetUp(uint32_t freq)
{
	SysInitStatus setup_status = SetHard();

	if(setup_status != SysInitStatus::InitOK)
		return setup_status;
	
	setup_status = SetFreq(freq/2);
	if(setup_status != SysInitStatus::InitOK)
		return setup_status;

	TIMx->DIER = TIM_DIER_UIE;
	TIMx->SR = 0;

	NVIC_EnableIRQ(IRQ_vector);

	// TIMx->CR1 = TIM_CR1_CEN;
	return SysInitStatus::InitOK;
}


SysInitStatus TIM_EncoderGenerator::SetUp(uint32_t freq, uint32_t period, uint32_t ch1_width, uint32_t ch2_width)
{
	if((freq == 0)
	|| (freq > 1000)
	|| (A.pin.PORT == nullptr)
	|| (B.pin.PORT == nullptr))
		return SysInitStatus::InitError;

	SysInitStatus setup_status = SetHard();

	if(setup_status != SysInitStatus::InitOK)
		return setup_status;

	A.pin.SetUp(PIN::TYPE::AF_PushPull, af);
	B.pin.SetUp(PIN::TYPE::AF_PushPull, af);

	uint32_t ccr_offset = &TIMx->CCR2 - &TIMx->CCR1;

	uint32_t* ccr_A = const_cast<uint32_t*>(&TIMx->CCR1 + ccr_offset * line_A_offset);
	uint32_t* ccr_B = const_cast<uint32_t*>(&TIMx->CCR1 + ccr_offset * line_B_offset);

	uint32_t* ccrmr_A = const_cast<uint32_t*>(&TIMx->CCMR1 + 32 * ((line_A_offset & 2) >> 1));
	uint32_t* ccrmr_B = const_cast<uint32_t*>(&TIMx->CCMR1 + 32 * ((line_B_offset & 2) >> 1));


	TIMx->ARR = period;
	TIMx->PSC = bus_clk/((TIMx->ARR+1) * freq * 2) - 1;
	*ccr_A = ch1_width;
	*ccr_B = ch2_width;
	TIMx->BDTR = TIM_BDTR_MOE;
	TIMx->DIER = TIM_DIER_UIE;
	TIMx->CCER = TIM_CCER_CC1E << ((TIM_CCER_CC2E_Pos - TIM_CCER_CC1E_Pos) * line_A_offset) |
				 TIM_CCER_CC1E << ((TIM_CCER_CC2E_Pos - TIM_CCER_CC1E_Pos) * line_B_offset);
	
	TIMx->CCMR1 = 0;
	TIMx->CCMR2 = 0;
	*ccrmr_A |= 3 << (TIM_CCMR1_OC1M_Pos + 8*(line_A_offset & 1)); // toggle mode
	*ccrmr_B |= 3 << (TIM_CCMR1_OC1M_Pos + 8*(line_B_offset & 1)); // toggle mode

	NVIC_EnableIRQ(IRQ_vector);

	return SysInitStatus::InitOK;
}

void TIM_EncoderGenerator::GenPulses_IRQ()
{
	ClearFlags();
	if(--_pulses == 0)
		TIMx->CR1 &= ~TIM_CR1_CEN;
}

SysInitStatus TIM_PulseMeasure::SetUp(uint32_t max_freq)
{
	if((max_freq > 1000000)
	|| (line_offset > 1)
	|| (Input.pin.PORT == nullptr))
		return SysInitStatus::InitError;

	SysInitStatus setup_status = SetHard();
	if(setup_status != SysInitStatus::InitOK)
		return setup_status;

	Input.pin.SetUp(PIN::TYPE::AF_PushPull, af);

	TIMx->PSC = bus_clk/max_freq - 1;
	TIMx->ARR = 0xFFFF;
	TIMx->CCMR1 = 0b01 << line_offset*TIM_CCMR1_CC2S_Pos |
				  0b10 << !line_offset*TIM_CCMR1_CC2S_Pos;

	TIMx->SMCR = (0b101 + line_offset)  << TIM_SMCR_TS_Pos | // Filtered Timer Input 1/2
				  0b100 << TIM_SMCR_SMS_Pos; // Reset Mode - Rising edge of the selected trigger input (TRGI) reinitializes the counter and generates an update of the registers
	TIMx->CCER  = TIM_CCER_CC1P |
				  TIM_CCER_CC1E |
				  TIM_CCER_CC2E;
	
	TIMx->CR1 = TIM_CR1_CEN;

	return SysInitStatus::InitOK;
}

SysInitStatus TIM_PWM::SetUp(uint32_t freq)
{
	if((freq == 0)
	|| (freq > 10000)
	|| ((CH1.PORT == nullptr)
	 && (CH2.PORT == nullptr)
	 && (CH3.PORT == nullptr)
	 && (CH4.PORT == nullptr)))
		return SysInitStatus::InitError;

	SysInitStatus setup_status = SetHard();

	if(setup_status != SysInitStatus::InitOK)
		return setup_status;

	uint32_t pcs = 0, arr = 0;
	arr_off = -9;

	do
	{
		arr_off+=10;
		arr = 99*arr_off;

		if(arr > 0xFFFF)
			return SysInitStatus::InitError;

		pcs = bus_clk/((arr+1) * freq) - 1;

	} while (pcs > 0xFFFF);	
	
	TIMx->ARR = arr;
	TIMx->PSC = pcs;
	
	TIMx->BDTR = TIM_BDTR_MOE;
	TIMx->CCER = 0;
	TIMx->CCMR1 = 0;
	TIMx->CCMR2 = 0;

	if(CH1.PORT != nullptr)
	{
		CH1.SetUp(PIN::TYPE::AF_PushPull, af);
		TIMx->CCER |= TIM_CCER_CC1E;
		TIMx->CCMR1 |= 6 << TIM_CCMR1_OC1M_Pos;
	}
	if(CH2.PORT != nullptr)
	{
		CH2.SetUp(PIN::TYPE::AF_PushPull, af);
		TIMx->CCER |= TIM_CCER_CC2E;
		TIMx->CCMR1 |= 6 << TIM_CCMR1_OC2M_Pos;
	}
	if(CH3.PORT != nullptr)
	{
		CH3.SetUp(PIN::TYPE::AF_PushPull, af);
		TIMx->CCER |= TIM_CCER_CC3E;
		TIMx->CCMR2 |= 6 << TIM_CCMR2_OC3M_Pos;
	}
	if(CH4.PORT != nullptr)
	{
		CH4.SetUp(PIN::TYPE::AF_PushPull, af);
		TIMx->CCER |= TIM_CCER_CC4E;
		TIMx->CCMR2 |= 6 << TIM_CCMR2_OC4M_Pos;
	}


	TIMx->CR1 |= TIM_CR1_CEN;
	return SysInitStatus::InitOK;
}

void TIM_PWM::SetCCR(TIM_Channel ch, uint32_t width)
{
	if(width > 100)
		return;

	uint32_t ccr_off = &TIMx->CCR2 - &TIMx->CCR1;
	uint32_t *ccr = const_cast<uint32_t*>(&TIMx->CCR1 + ccr_off * static_cast<uint32_t>(ch));
	*ccr = width * arr_off;
}