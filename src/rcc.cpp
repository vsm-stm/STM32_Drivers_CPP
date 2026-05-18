#include "rcc.hpp"


uint32_t ClockSystem::HSESrcClk{0};
uint32_t ClockSystem::AHB_Pre{1};
uint32_t ClockSystem::APB1_Pre{1};
uint32_t ClockSystem::APB2_Pre{1};

SysInitStatus ClockSystem::Init(SystemClockSource ClkSrc, uint32_t HSE_Clk, BusDividers BusDiv, PLL_CFGR PLLCfgr)
{
	uint32_t sys_clk = 0;
	uint32_t tickStart;
	// ---------------------------------------------------------------------------
	// if HSE is used - Enable it.
	// ---------------------------------------------------------------------------
	if((ClkSrc == SystemClockSource::HSE)
	|| (PLLCfgr.PLL_ClkSrc == PLL_ClockSource::HSE))
	{
		SysInitStatus hse_status = EnableHSE(HSE_Clk);
		if(hse_status != SysInitStatus::InitOK)
			return hse_status;
	}

	// ---------------------------------------------------------------------------
	// Chek pll used and if used - check PLL parameters, set PLL and switch system clock to PLL output.
	// ---------------------------------------------------------------------------
	if(PLLCfgr.PLL_ClkSrc == PLL_ClockSource::NO) // if PLL is not used, choose clock source from HSI or HSE if available
	{
		if(HSE_Clk == 0)
			sys_clk = HSI_Clock;
		else
			sys_clk = HSE_Clk;
	}
	else										// if PLL check PLL parameters and set PLL
	{
		// ---------------------------------------------------------------------------
		// Validate PLL parameters before applying them, to avoid getting stuck in a bad configuration.
		// ---------------------------------------------------------------------------
		
		bool f446xx_pllr_out = false;
		#if defined(STM32f446xx)
		f446xx_pllr_out = (ClkSrc == SystemClockSource::PLL_R);
		#endif
		validate_out pll_result = ValidatePLLCfgr(PLLCfgr, f446xx_pllr_out);
		if(pll_result.status != SysInitStatus::InitOK)
			return pll_result.status;
		
		sys_clk = pll_result.clk_value;

		// ---------------------------------------------------------------------------
		// Disable PLL
		// ---------------------------------------------------------------------------
		tickStart = System::GetTick();
		RCC->CR &= ~RCC_CR_PLLON;// PLL Disable
		while ((RCC->CR & RCC_CR_PLLRDY)) 
		{
			if((System::GetTick() - tickStart) > PLL_TIMEOUT_MS)
				return SysInitStatus::InitError;
		};

		// ---------------------------------------------------------------------------
		// Configure PLL
		// ---------------------------------------------------------------------------
		ConfigurePLL(PLLCfgr);

		// ---------------------------------------------------------------------------
		// Set VOS and enable overdrive if required for the target frequency, before switching to the PLL output.
		// ---------------------------------------------------------------------------

#ifdef STM32F7
		if(sys_clk > 180000000)
		{
			// if(PLLCfgr.PLL_ClkSrc == PLL_ClockSource::HSI) // todo one more check
			// 	PWR->CR1 &= ~(PWR_CR1_VOS_1);
			// else
			// 	PWR->CR1 &= ~(PWR_CR1_VOS_0);
			PWR->CR1 |= PWR_CR1_ODEN;
			tickStart = System::GetTick();
			while (!(PWR->CSR1 & PWR_CSR1_ODRDY))
			{
				if((System::GetTick() - tickStart) > 100)
					return SysInitStatus::InitError;
			};

			PWR->CR1 |= PWR_CR1_ODSWEN;
			tickStart = System::GetTick();
			while (!(PWR->CSR1 & PWR_CSR1_ODSWRDY))
			{
				if((System::GetTick() - tickStart) > 100)
					return SysInitStatus::InitError;
			};
		}
#elif defined (STM32G0)
		PWR->CR1 = (PWR->CR1 & ~PWR_CR1_VOS_Msk) | PWR_CR1_VOS_0; // Set VOS to 1. Scale 2mode allows up to 170 MHz, but is more power efficient than Scale 1 (up to 170 MHz) and Scale 0 (up to 170 MHz with overdrive).
		tickStart = System::GetTick();
			while ((PWR->SR2 & PWR_SR2_VOSF))
			{
				if((System::GetTick() - tickStart) > 100)
					return SysInitStatus::InitError;
			}; 
#endif

		// ---------------------------------------------------------------------------
		// Enable PLL
		// ---------------------------------------------------------------------------

		RCC->CR |= RCC_CR_PLLON;// PLL Enable
		tickStart = System::GetTick();
		while (!(RCC->CR & RCC_CR_PLLRDY))
		{
			if((System::GetTick() - tickStart) > PLL_TIMEOUT_MS)
				return SysInitStatus::InitError;
		};
	}

	if(ValidateBusDividers(sys_clk, BusDiv) != SysInitStatus::InitOK)
		return SysInitStatus::InitError;
	
	uint32_t latency = (sys_clk - 1) / LATENCY_DIV;
	if(latency > 9)
		return SysInitStatus::InitError;

	FLASH->ACR &= ~FLASH_ACR_LATENCY_Msk;
	FLASH->ACR |= latency << FLASH_ACR_LATENCY_Pos;

	RCC->CFGR &= ~(RCC_CFGR_HPRE_Msk | PPRE_BUS_1_Msk | PPRE_BUS_2_Msk);
	RCC->CFGR |= (uint32_t)BusDiv.AHB_div << RCC_CFGR_HPRE_Pos |
				 (uint32_t)BusDiv.APB1_div << PPRE_BUS_1_Pos;
	if constexpr (PPRE_BUS_2_Pos != 0xFFFFFFFFU)
		RCC->CFGR |= (uint32_t)BusDiv.APB2_div << PPRE_BUS_2_Pos;

	RCC->CFGR |= (uint32_t)ClkSrc;         // PLL -> SYSCKLK
	tickStart = System::GetTick();
	while (!(RCC->CFGR & RCC_CFGR_SWS))
	{
		if((System::GetTick() - tickStart) > CLOCKSWITCH_TIMEOUT_MS)
			return SysInitStatus::InitError;
	}// wait PLL used as system clock


	System::SystemCoreClock = sys_clk/AHB_Pre;
	System::APB1BusClock = System::SystemCoreClock/APB1_Pre;
	System::APB2BusClock = System::SystemCoreClock/APB2_Pre;
	System::TIMxAPB1Clock = (APB1_Pre == 1) ? (System::APB1BusClock) : (System::APB1BusClock * 2);
	System::TIMxAPB2Clock = (APB2_Pre == 1) ? (System::APB2BusClock) : (System::APB2BusClock * 2);

	System::InitTicks();

	return SysInitStatus::InitOK;

}

SysInitStatus ClockSystem::EnableHSE(uint32_t HSE_Clk){
	uint32_t tickStart;
	if((HSE_Clk == 0)
		|| (HSE_Clk > 26*MHz))		// todo set digfferent limits for differen MCU and is it bypass
			return SysInitStatus::InitError;
		
		RCC->CR |= RCC_CR_HSEON;
		tickStart = System::GetTick();
		while (!(RCC->CR & RCC_CR_HSERDY))
		{
			if((System::GetTick() - tickStart) > HSE_TIMEOUT_MS)
				return SysInitStatus::InitError;
		};
		HSESrcClk = HSE_Clk;
		return SysInitStatus::InitOK;
};

ClockSystem::validate_out ClockSystem::ValidatePLLCfgr(PLL_CFGR pllcfgr, bool f446xx_pllr_out){
	
	validate_out result{SysInitStatus::InitError, 0};

	constexpr auto InRange = [](auto val, auto min, auto max) { 
		return val >= min && val <= max; 
	};

#if defined(STM32F446xx) || defined(STM32F767xx)
	if(!InRange(pllcfgr.PLL_M, 2, 63)
	|| !InRange(pllcfgr.PLL_N, 50, 432)
	|| !((pllcfgr.PLL_P == 2) || (pllcfgr.PLL_P == 4) || (pllcfgr.PLL_P == 6) || (pllcfgr.PLL_P == 8))
	|| !InRange(pllcfgr.PLL_Q, 2, 15)
	|| !InRange(pllcfgr.PLL_R, 2, 7))
	{
		return {SysInitStatus::InitError, 0};
	}
#elif defined (STM32G0)
	if(!InRange(pllcfgr.PLL_M, 1, 8)
	|| !InRange(pllcfgr.PLL_N, 8, 86)
	|| !InRange(pllcfgr.PLL_R, 2, 16)
	|| !InRange(pllcfgr.PLL_P, 2, 16))
	{
		return result;
	}
#endif

	uint32_t tmp_src_freq;
	if(pllcfgr.PLL_ClkSrc == PLL_ClockSource::HSE)
		tmp_src_freq = HSESrcClk;
	else
		tmp_src_freq = HSI_Clock;

	uint32_t PLL_in = tmp_src_freq/pllcfgr.PLL_M;
	if(!InRange(PLL_in, PLL_CLK_IN_MIN, PLL_CLK_IN_MAX))
		return result;

	uint32_t PLL_N_Clk = PLL_in*pllcfgr.PLL_N;
	if(!InRange(PLL_N_Clk, PLL_N_CLK_MIN, PLL_N_CLK_MAX))
		return result;

	#if defined(STM32F446xx) || defined(STM32F767xx)
		uint32_t PLL_Out = f446xx_pllr_out ? PLL_N_Clk/pllcfgr.PLL_R : PLL_N_Clk/pllcfgr.PLL_P;
	#elif defined (STM32G0)
		uint32_t PLL_Out = PLL_N_Clk/pllcfgr.PLL_R;
	#endif
	if(PLL_Out > SYS_CLK_LIMIT)
		return result;

	result.status = SysInitStatus::InitOK;
	result.clk_value = PLL_Out;

	return result;
};

SysInitStatus ClockSystem::ValidateBusDividers(uint32_t sys_clk, BusDividers div){
	AHB_Pre = 1;
	APB1_Pre = 1;
	APB2_Pre = 1;

	if(((uint32_t)div.AHB_div))
		AHB_Pre = (1 << (((uint32_t)div.AHB_div)  - 8));

	if(((uint32_t)div.APB1_div))
		APB1_Pre = (1 << (((uint32_t)div.APB1_div) - 3));

	if constexpr (PPRE_BUS_2_Pos != 0xFFFFFFFFU) {
		if(((uint32_t)div.APB2_div))
			APB2_Pre = (1 << ((((uint32_t)div.APB2_div)) - 3));
	}

	if((sys_clk/AHB_Pre > SYS_CLK_LIMIT)
	|| (sys_clk/AHB_Pre/APB1_Pre > APB1_CLK_LIMIT))
		return SysInitStatus::InitError;

	if constexpr (PPRE_BUS_2_Pos != 0xFFFFFFFFU) {
		if(sys_clk/AHB_Pre/APB2_Pre > APB2_CLK_LIMIT)
			return SysInitStatus::InitError;
	}

	return SysInitStatus::InitOK;
};

void ClockSystem::ConfigurePLL(PLL_CFGR pllcfgr){
	#if defined(STM32F4) || defined(STM32F7)
		RCC->PLLCFGR = (uint32_t)pllcfgr.PLL_ClkSrc  |
								 pllcfgr.PLL_M << RCC_PLLCFGR_PLLM_Pos |
								 pllcfgr.PLL_N << RCC_PLLCFGR_PLLN_Pos |
								 ((pllcfgr.PLL_P >> 1) - 1) << RCC_PLLCFGR_PLLP_Pos |
								 pllcfgr.PLL_Q << RCC_PLLCFGR_PLLQ_Pos;

		#if defined(STM32F446xx) || defined(STM32F767xx)
			RCC->PLLCFGR |= pllcfgr.PLL_R << RCC_PLLCFGR_PLLR_Pos;
		#endif
	#elif defined(STM32G0)
		RCC->PLLCFGR = (uint32_t)pllcfgr.PLL_ClkSrc  |
								 pllcfgr.PLL_M << RCC_PLLCFGR_PLLM_Pos |
								 pllcfgr.PLL_N << RCC_PLLCFGR_PLLN_Pos |
								 (pllcfgr.PLL_P - 1) << RCC_PLLCFGR_PLLP_Pos |
								 (pllcfgr.PLL_R - 1) << RCC_PLLCFGR_PLLR_Pos |
								 RCC_PLLCFGR_PLLREN; // Enable PLL output to system clock
	#endif
};


SysInitStatus ClockSystem::InitCalcPLL(uint32_t req_freq, PLL_ClockSource pll_src, uint32_t hse_clk, uint8_t pll_q)
{
	if((pll_src == PLL_ClockSource::HSE)
	&& (hse_clk == 0))
		return SysInitStatus::InitError;

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
		return SysInitStatus::InitError;
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
	if(req_freq <= APB1_CLK_LIMIT)
	{
		div.APB1_div = APB_Divider::DIV1;
		div.APB2_div = APB_Divider::DIV1;
	}
	else
	if((req_freq > APB1_CLK_LIMIT)
	&& (req_freq <=APB2_CLK_LIMIT))
	{
		div.APB1_div = APB_Divider::DIV2;
		div.APB2_div = APB_Divider::DIV1;		
	}
	else
	if(req_freq > APB2_CLK_LIMIT)
	{
		div.APB1_div = APB_Divider::DIV4;
		div.APB2_div = APB_Divider::DIV2;		
	}

	return Init(SystemClockSource::PLL, hse_clk, div, {pll_src,pll_m,pll_n,pll_p,pll_q,2});
}

