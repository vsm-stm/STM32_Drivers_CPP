#include <rcc.hpp>


uint32_t ClockSystem::HSESrcClk{0};


SYS_StatusTypeDef ClockSystem::Init(SystemClockSource ClkSrc, uint32_t HSE_Clk, BusDividers BusDiv, PLL_CFGR PLLCfgr)
{
	uint32_t sys_clk = 0;
	uint32_t tickStart;

	if((ClkSrc == SystemClockSource::HSE)
	|| (PLLCfgr.PLL_ClkSrc == PLL_ClockSource::HSE))
	{
		if((HSE_Clk == 0)
		|| (HSE_Clk > 26000000))
			return SYS_ERROR;
		
		RCC->CR |= RCC_CR_HSEON;// HSE Enable
		tickStart = System::GetTick();
		while (!(RCC->CR & RCC_CR_HSERDY))
		{
			if((System::GetTick() - tickStart) > HSE_TIMEOUT_VALUE)
				return SYS_TIMEOUT;
		};
		HSESrcClk = HSE_Clk;
	}

	if(PLLCfgr.PLL_ClkSrc == PLL_ClockSource::NO)
	{
		if(HSE_Clk == 0)
			sys_clk = HSI_Clock;
		else
			sys_clk = HSE_Clk;
	}
	else
	{
		if((PLLCfgr.PLL_M < 2)  || (PLLCfgr.PLL_M > 63)
		|| (PLLCfgr.PLL_N < 50) || (PLLCfgr.PLL_N > 432)
		|| ((PLLCfgr.PLL_P != 2) && (PLLCfgr.PLL_P != 4) && (PLLCfgr.PLL_P != 6) && (PLLCfgr.PLL_P != 8))
		|| (PLLCfgr.PLL_Q < 2)  || (PLLCfgr.PLL_Q > 15)
		|| (PLLCfgr.PLL_R < 2)  || (PLLCfgr.PLL_R > 7))
		{
			return SYS_ERROR;
		}

		uint32_t tmp_src_freq;
		if(PLLCfgr.PLL_ClkSrc == PLL_ClockSource::HSE)
			tmp_src_freq = HSE_Clk;
		else
			tmp_src_freq = HSI_Clock;

		uint32_t PLL_in = tmp_src_freq/PLLCfgr.PLL_M;
		if((PLL_in < 1000000)
		|| (PLL_in > 2000000))
			return SYS_ERROR;
	
		uint32_t VCO_in = PLL_in*PLLCfgr.PLL_N;
		if((VCO_in < 100000000)
		|| (VCO_in > 432000000))
			return SYS_ERROR;

		if(ClkSrc == SystemClockSource::PLL_P)
		{
			sys_clk = VCO_in/PLLCfgr.PLL_P;
		}
#ifdef STM32F446xx		
		if(ClkSrc == SystemClockSource::PLL_R)
		{
			sys_clk = VCO_in/PLLCfgr.PLL_R;
		}
#endif
		if(sys_clk > SYS_CLK_LIMIT)
			return SYS_ERROR;

		tickStart = System::GetTick();
		RCC->CR &= ~RCC_CR_PLLON;// PLL Disable
		while ((RCC->CR & RCC_CR_PLLON)) 
		{
			if((System::GetTick() - tickStart) > PLL_TIMEOUT_VALUE)
				return SYS_TIMEOUT;
		};

		RCC->PLLCFGR = (uint32_t)PLLCfgr.PLL_ClkSrc  |
								 PLLCfgr.PLL_M << RCC_PLLCFGR_PLLM_Pos |
								 PLLCfgr.PLL_N << RCC_PLLCFGR_PLLN_Pos |
								 ((PLLCfgr.PLL_P >> 1) - 1) << RCC_PLLCFGR_PLLP_Pos |
								 PLLCfgr.PLL_Q << RCC_PLLCFGR_PLLQ_Pos;
#ifdef STM32F446xx	
		RCC->PLLCFGR |= PLLCfgr.PLL_R << RCC_PLLCFGR_PLLR_Pos;
#endif

		RCC->CR |= RCC_CR_PLLON;// PLL Disable
		tickStart = System::GetTick();
		while (!(RCC->CR & RCC_CR_PLLON))
		{
			if((System::GetTick() - tickStart) > PLL_TIMEOUT_VALUE)
				return SYS_TIMEOUT;
		};
	}

	uint32_t AHB_Pre = 0;
	uint32_t APB1_Pre = 0;
	uint32_t APB2_Pre = 0;
	if(!((uint32_t)BusDiv.AHB_div))
		AHB_Pre = 1;
	else
		AHB_Pre = (1 << ((((uint32_t)BusDiv.AHB_div) >> RCC_CFGR_HPRE_Pos)  - 8));
	if(!((uint32_t)BusDiv.APB1_div))
		APB1_Pre = 1;
	else
		APB1_Pre = (1 << ((((uint32_t)BusDiv.APB1_div) >> RCC_CFGR_PPRE1_Pos) - 3));
	if(!((uint32_t)BusDiv.APB2_div))
		APB2_Pre = 1;
	else
		APB2_Pre = (1 << ((((uint32_t)BusDiv.APB2_div) >> RCC_CFGR_PPRE2_Pos) - 3));

	if((sys_clk/AHB_Pre > SYS_CLK_LIMIT)
	|| (sys_clk/AHB_Pre/APB1_Pre > APB1_CLK_LIMIT)
	|| (sys_clk/AHB_Pre/APB2_Pre > APB2_CLK_LIMIT))
		return SYS_ERROR;
	
	FLASH->ACR &= ~FLASH_ACR_LATENCY_Msk;
	if(sys_clk < 30000000)
	{
		FLASH->ACR |= FLASH_ACR_LATENCY_0WS;
	}
	else if((sys_clk >= 30000000) && (sys_clk < 60000000))
	{
		FLASH->ACR |= FLASH_ACR_LATENCY_1WS;
	}
	else if((sys_clk >= 60000000) && (sys_clk < 90000000))
	{
		FLASH->ACR |= FLASH_ACR_LATENCY_2WS;
	}
	else if((sys_clk >= 90000000) && (sys_clk < 120000000))
	{
		FLASH->ACR |= FLASH_ACR_LATENCY_3WS;
	}
	else if((sys_clk >= 120000000) && (sys_clk < 150000000))
	{
		FLASH->ACR |= FLASH_ACR_LATENCY_4WS;
	}
	else if((sys_clk >= 150000000) && (sys_clk <= 180000000))
	{
		FLASH->ACR |= FLASH_ACR_LATENCY_5WS;
	}
	else
		return SYS_ERROR;

	RCC->CFGR &= ~(RCC_CFGR_HPRE_Msk | RCC_CFGR_PPRE1_Msk | RCC_CFGR_PPRE2_Msk);
	RCC->CFGR |= (uint32_t)BusDiv.AHB_div |
				 (uint32_t)BusDiv.APB1_div |
				 (uint32_t)BusDiv.APB2_div;

	RCC->CFGR |= RCC_CFGR_SW_PLL;         // PLL -> SYSCKLK
	tickStart = System::GetTick();
	while (!(RCC->CFGR & RCC_CFGR_SWS))
	{
		if((System::GetTick() - tickStart) > CLOCKSWITCH_TIMEOUT_VALUE)
			return SYS_TIMEOUT;
	}// wait PLL used as system clock

	if (!(RCC->CFGR & RCC_CFGR_HPRE))
		AHB_Pre = 1;
	else
		AHB_Pre  = (1 << (((RCC->CFGR & RCC_CFGR_HPRE)  >> RCC_CFGR_HPRE_Pos)  - 8));
	if (!(RCC->CFGR & RCC_CFGR_PPRE1))
		APB1_Pre = 1;
	else
		APB1_Pre = (1 << (((RCC->CFGR & RCC_CFGR_PPRE1) >> RCC_CFGR_PPRE1_Pos) - 3));
	if (!(RCC->CFGR & RCC_CFGR_PPRE2))
		APB2_Pre = 1;
	else
		APB2_Pre = (1 << (((RCC->CFGR & RCC_CFGR_PPRE2) >> RCC_CFGR_PPRE2_Pos) - 3));

	System::SystemCoreClock = sys_clk/AHB_Pre;
	System::APB1BusClock = System::SystemCoreClock/APB1_Pre;
	System::APB2BusClock = System::SystemCoreClock/APB2_Pre;
	System::TIMxAPB1Clock = (APB1_Pre == 1) ? (System::APB1BusClock) : (System::APB1BusClock * 2);
	System::TIMxAPB2Clock = (APB2_Pre == 1) ? (System::APB2BusClock) : (System::APB2BusClock * 2);

	System::InitTicks();

	return SYS_OK;

}

SYS_StatusTypeDef ClockSystem::Init_calc_pll(uint32_t req_freq, PLL_ClockSource pll_src, uint32_t hse_clk)
{
	if((pll_src == PLL_ClockSource::HSE)
	&& (hse_clk == 0))
		return SYS_ERROR;
	
	uint32_t input_clk = 0;
	if(pll_src == PLL_ClockSource::HSE)
	{
		input_clk = hse_clk;
	}
	else if(pll_src == PLL_ClockSource::HSI)
	{
		input_clk = HSI_Clock;
	}
	else
	{
		return SYS_ERROR;
	}

	uint8_t pll_m, pll_p;
	uint16_t pll_n;

	if(input_clk%2000000 == 0)
	{
		pll_m = input_clk/2000000;
	}
	else
	{
		pll_m = input_clk/2000000 + 1;
	}

	if(req_freq%1000000 == 0)
	{
		pll_n = req_freq/1000000;
	}
	else
	{
		pll_n = req_freq/1000000 + 1;
	}

	pll_p = 2;

	BusDividers div;

	div.AHB_div = AHB_Divider::DIV1;
	if(req_freq <= 45000000)
	{
		div.APB1_div = APB1_Divider::DIV1;
		div.APB2_div = APB2_Divider::DIV1;
	}
	else
	if((req_freq > 45000000)
	&& (req_freq <=90000000))
	{
		div.APB1_div = APB1_Divider::DIV2;
		div.APB2_div = APB2_Divider::DIV1;		
	}
	else
	if(req_freq > 90000000)
	{
		div.APB1_div = APB1_Divider::DIV4;
		div.APB2_div = APB2_Divider::DIV2;		
	}

	return Init(SystemClockSource::PLL_P, hse_clk, div, {pll_src,pll_m,pll_n,pll_p,2,2});
}
