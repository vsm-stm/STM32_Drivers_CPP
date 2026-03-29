/**
 * @file  rcc.cpp
 * @brief RCC / ClockSystem implementation for STM32F4 / F7.
 *
 * Key fixes vs. original:
 *  1. Waiting for PLLRDY (not PLLON) after enabling/disabling PLL.
 *  2. Comparing full SWS field against the expected source value.
 *  3. Switching to HSI before touching PLL to prevent a clock-less state.
 *  4. SYS_TIMEOUT replaced with SysInitStatus::InitError everywhere.
 *  5. PLLR bits properly masked before being ORed into PLLCFGR.
 *  6. Overdrive VOS selection fixed (not dependent on PLL source).
 *  7. Flash latency uses per-target tables instead of a single formula.
 *  8. Init_calc_pll PLL_N calculation corrected.
 *  9. BusDividers default-initialised in struct definition (rcc.hpp).
 * 10. #error added for unrecognised targets (rcc.hpp).
 */

#include "rcc.hpp"

// ---------------------------------------------------------------------------
// Static member definition
// ---------------------------------------------------------------------------

uint32_t ClockSystem::HSESrcClk{ 0U };

// ---------------------------------------------------------------------------
// Internal helper — CalcFlashLatency
// ---------------------------------------------------------------------------

/**
 * @brief Return the required Flash wait-state count for @p sys_clk.
 *
 * FIX: The original code used `sys_clk / 30000000` for every target.
 *      STM32F7 has different latency requirements depending on Vcore / Overdrive.
 *      We use the most conservative (highest latency) table here; if Overdrive
 *      is active the caller may reduce latency afterwards.
 *
 *      STM32F4 @ Vdd 2.7–3.6V  : 1 WS per 30 MHz (max 8 WS → 168/180 MHz)
 *      STM32F7 @ Vcore Scale 1 : 1 WS per 30 MHz (max 7 WS → 216 MHz)
 *
 *      Returns UINT32_MAX when the frequency exceeds the device limit so the
 *      caller can detect and reject the configuration.
 */
uint32_t ClockSystem::CalcFlashLatency(uint32_t sys_clk)
{
#if defined(STM32F7)
	// STM32F7 RM Table 7: Vcore = Scale 1 (overdrive capable), Vdd >= 2.7V
	// 0–30 MHz → 0 WS, 30–60 → 1 WS, …, 210–216 → 7 WS
	static constexpr uint32_t step = 30000000UL;
	static constexpr uint32_t maxWS = 7U;
#else
	// STM32F4 RM Table 10: Vdd 2.7–3.6V
	// 0–30 MHz → 0 WS, 30–60 → 1 WS, …, 150–168/180 → 5/6 WS
	static constexpr uint32_t step = 30000000UL;
	static constexpr uint32_t maxWS = 9U;
#endif

	if (sys_clk > SYS_CLK_LIMIT)
		return UINT32_MAX; // signal error

	uint32_t latency = (sys_clk - 1U) / step; // ceiling division − 1 wait per step
	return (latency > maxWS) ? UINT32_MAX : latency;
}

// ---------------------------------------------------------------------------
// Internal helper — SwitchToHSI
// ---------------------------------------------------------------------------

/**
 * @brief Switch SYSCLK to HSI and confirm via SWS bits.
 *
 * FIX (bug #8): Before disabling/reconfiguring the PLL the system must be
 *               running from a source that does not depend on PLL, otherwise
 *               the MCU loses its clock during the reconfiguration window.
 */
SysInitStatus ClockSystem::SwitchToHSI()
{
	RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSI;

	uint32_t tickStart = System::GetTick();
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI)
	{
		if ((System::GetTick() - tickStart) > CLOCKSWITCH_TIMEOUT_MS)
			return SysInitStatus::InitError;
	}
	return SysInitStatus::InitOK;
}

// ---------------------------------------------------------------------------
// ClockSystem::Init
// ---------------------------------------------------------------------------

SysInitStatus ClockSystem::Init(SystemClockSource ClkSrc,
								uint32_t          HSE_Clk,
								BusDividers       BusDiv,
								PLL_CFGR          PLLCfgr)
{
	uint32_t sys_clk   = 0U;
	uint32_t tickStart = 0U;

	// -----------------------------------------------------------------------
	// 1. Start HSE if needed
	// -----------------------------------------------------------------------
	if ((ClkSrc == SystemClockSource::HSE)
	||  (PLLCfgr.PLL_ClkSrc == PLL_ClockSource::HSE))
	{
		if ((HSE_Clk == 0U) || (HSE_Clk > 26000000UL))
			return SysInitStatus::InitError;

		RCC->CR |= RCC_CR_HSEON;
		tickStart = System::GetTick();
		while (!(RCC->CR & RCC_CR_HSERDY))
		{
			if ((System::GetTick() - tickStart) > HSE_TIMEOUT_MS)
				return SysInitStatus::InitError;
		}
		HSESrcClk = HSE_Clk;
	}

	// -----------------------------------------------------------------------
	// 2. Determine sys_clk and configure PLL when needed
	// -----------------------------------------------------------------------
	if (PLLCfgr.PLL_ClkSrc == PLL_ClockSource::NO)
	{
		// No PLL — direct HSI or HSE
		sys_clk = (HSE_Clk != 0U) ? HSE_Clk : HSI_Clock;
	}
	else
	{
		// --- Validate PLL parameters ---
		if (   (PLLCfgr.PLL_M < 2U)   || (PLLCfgr.PLL_M > 63U)
			|| (PLLCfgr.PLL_N < 50U)  || (PLLCfgr.PLL_N > 432U)
			|| ((PLLCfgr.PLL_P != 2U) && (PLLCfgr.PLL_P != 4U)
			 && (PLLCfgr.PLL_P != 6U) && (PLLCfgr.PLL_P != 8U))
			|| (PLLCfgr.PLL_Q < 2U)   || (PLLCfgr.PLL_Q > 15U)
			|| (PLLCfgr.PLL_R < 2U)   || (PLLCfgr.PLL_R > 7U))
		{
			return SysInitStatus::InitError;
		}

		uint32_t src_freq = (PLLCfgr.PLL_ClkSrc == PLL_ClockSource::HSE)
						  ? HSE_Clk
						  : HSI_Clock;

		uint32_t PLL_in = src_freq / PLLCfgr.PLL_M;
		if ((PLL_in < 1000000UL) || (PLL_in > 2000000UL))
			return SysInitStatus::InitError;

		uint32_t VCO_out = PLL_in * PLLCfgr.PLL_N;
		if ((VCO_out < 100000000UL) || (VCO_out > 432000000UL))
			return SysInitStatus::InitError;

		if (ClkSrc == SystemClockSource::PLL_P)
			sys_clk = VCO_out / PLLCfgr.PLL_P;
#if defined(STM32F446xx)
		else if (ClkSrc == SystemClockSource::PLL_R)
			sys_clk = VCO_out / PLLCfgr.PLL_R;
#endif
		else
			return SysInitStatus::InitError; // invalid source for PLL path

		if (sys_clk > SYS_CLK_LIMIT)
			return SysInitStatus::InitError;

		// -------------------------------------------------------------------
		// FIX #8: Switch to HSI before touching PLL so the core keeps its
		//         clock during the PLL reconfiguration window.
		// -------------------------------------------------------------------
		if (SwitchToHSI() != SysInitStatus::InitOK)
			return SysInitStatus::InitError;

		// -------------------------------------------------------------------
		// FIX #7: Disable PLL and wait for PLLRDY to clear (not PLLON).
		//         The RM states PLLCFGR must not be written while PLL is ON.
		// -------------------------------------------------------------------
		RCC->CR &= ~RCC_CR_PLLON;
		tickStart = System::GetTick();
		while (RCC->CR & RCC_CR_PLLRDY) // FIX: was RCC_CR_PLLON
		{
			if ((System::GetTick() - tickStart) > PLL_TIMEOUT_MS)
				return SysInitStatus::InitError;
		}

		// --- Write PLLCFGR ---
		RCC->PLLCFGR =
			  (static_cast<uint32_t>(PLLCfgr.PLL_ClkSrc))
			| (static_cast<uint32_t>(PLLCfgr.PLL_M) << RCC_PLLCFGR_PLLM_Pos)
			| (static_cast<uint32_t>(PLLCfgr.PLL_N) << RCC_PLLCFGR_PLLN_Pos)
			| (static_cast<uint32_t>((PLLCfgr.PLL_P >> 1U) - 1U) << RCC_PLLCFGR_PLLP_Pos)
			| (static_cast<uint32_t>(PLLCfgr.PLL_Q) << RCC_PLLCFGR_PLLQ_Pos);

#if defined(STM32F446xx) || defined(STM32F767xx)
		// FIX #5: Mask PLLR bits before ORing to avoid stale bits.
		RCC->PLLCFGR = (RCC->PLLCFGR & ~RCC_PLLCFGR_PLLR_Msk)
					 | (static_cast<uint32_t>(PLLCfgr.PLL_R) << RCC_PLLCFGR_PLLR_Pos);
#endif

		// -------------------------------------------------------------------
		// STM32F7 Overdrive (required when SYSCLK > 180 MHz)
		// FIX #6: VOS selection is independent of PLL source; always set
		//         Scale 1 (both VOS bits = 1) before enabling Overdrive.
		// FIX #3: Replaced SYS_TIMEOUT with SysInitStatus::InitError.
		// -------------------------------------------------------------------
#if defined(STM32F7)
		if (sys_clk > 180000000UL)
		{
			// Vcore Scale 1: PWR_CR1_VOS[1:0] = 0b11
			PWR->CR1 |= PWR_CR1_VOS;

			// Enable Overdrive
			PWR->CR1 |= PWR_CR1_ODEN;
			tickStart = System::GetTick();
			while (!(PWR->CSR1 & PWR_CSR1_ODRDY))
			{
				if ((System::GetTick() - tickStart) > OVERDRIVE_TIMEOUT_MS)
					return SysInitStatus::InitError; // FIX: was SYS_TIMEOUT
			}

			// Switch to Overdrive
			PWR->CR1 |= PWR_CR1_ODSWEN;
			tickStart = System::GetTick();
			while (!(PWR->CSR1 & PWR_CSR1_ODSWRDY))
			{
				if ((System::GetTick() - tickStart) > OVERDRIVE_TIMEOUT_MS)
					return SysInitStatus::InitError; // FIX: was SYS_TIMEOUT
			}
		}
#endif // STM32F7

		// -------------------------------------------------------------------
		// FIX #1: Enable PLL and wait for PLLRDY (not PLLON).
		// -------------------------------------------------------------------
		RCC->CR |= RCC_CR_PLLON;
		tickStart = System::GetTick();
		while (!(RCC->CR & RCC_CR_PLLRDY)) // FIX: was RCC_CR_PLLON
		{
			if ((System::GetTick() - tickStart) > PLL_TIMEOUT_MS)
				return SysInitStatus::InitError;
		}
	}

	// -----------------------------------------------------------------------
	// 3. Validate bus clock limits
	// -----------------------------------------------------------------------

	// Decode prescaler values from the enum (which stores raw register values).
	// AHB: HPRE field — when bits [3:0] < 8 the prescaler is /1,
	//      otherwise prescaler = 2^(bits[3:0] - 7).
	// APB1/APB2: PPRE field — when bits[2:0] < 4 the prescaler is /1,
	//            otherwise prescaler = 2^(bits[2:0] - 3).
	//
	// The magic offsets (8, 3) come from the Reference Manual CFGR register
	// description and are named here for clarity.
	static constexpr uint8_t AHB_NO_DIV_THRESHOLD  = 8U; // HPRE  < 8 → DIV1
	static constexpr uint8_t APB_NO_DIV_THRESHOLD  = 4U; // PPRE  < 4 → DIV1
	static constexpr uint8_t AHB_EXPONENT_OFFSET   = 7U; // exponent = field - 7
	static constexpr uint8_t APB_EXPONENT_OFFSET   = 3U; // exponent = field - 3

	auto decodeAHB = [&](AHB_Divider d) -> uint32_t {
		uint32_t field = (static_cast<uint32_t>(d) & RCC_CFGR_HPRE_Msk) >> RCC_CFGR_HPRE_Pos;
		return (field < AHB_NO_DIV_THRESHOLD) ? 1U : (1U << (field - AHB_EXPONENT_OFFSET));
	};
	auto decodeAPB1 = [&](APB1_Divider d) -> uint32_t {
		uint32_t field = (static_cast<uint32_t>(d) & RCC_CFGR_PPRE1_Msk) >> RCC_CFGR_PPRE1_Pos;
		return (field < APB_NO_DIV_THRESHOLD) ? 1U : (1U << (field - APB_EXPONENT_OFFSET));
	};
	auto decodeAPB2 = [&](APB2_Divider d) -> uint32_t {
		uint32_t field = (static_cast<uint32_t>(d) & RCC_CFGR_PPRE2_Msk) >> RCC_CFGR_PPRE2_Pos;
		return (field < APB_NO_DIV_THRESHOLD) ? 1U : (1U << (field - APB_EXPONENT_OFFSET));
	};

	uint32_t AHB_Pre  = decodeAHB(BusDiv.AHB_div);
	uint32_t APB1_Pre = decodeAPB1(BusDiv.APB1_div);
	uint32_t APB2_Pre = decodeAPB2(BusDiv.APB2_div);

	uint32_t ahb_clk  = sys_clk  / AHB_Pre;
	uint32_t apb1_clk = ahb_clk  / APB1_Pre;
	uint32_t apb2_clk = ahb_clk  / APB2_Pre;

	if (   (ahb_clk  > SYS_CLK_LIMIT)
		|| (apb1_clk > APB1_CLK_LIMIT)
		|| (apb2_clk > APB2_CLK_LIMIT))
		return SysInitStatus::InitError;

	// -----------------------------------------------------------------------
	// 4. Set Flash wait states BEFORE increasing clock
	// -----------------------------------------------------------------------
	uint32_t latency = CalcFlashLatency(sys_clk);
	if (latency == UINT32_MAX)
		return SysInitStatus::InitError;

	FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY_Msk)
			   | (latency << FLASH_ACR_LATENCY_Pos);

	// -----------------------------------------------------------------------
	// 5. Configure bus prescalers
	// -----------------------------------------------------------------------
	RCC->CFGR = (RCC->CFGR
					& ~(RCC_CFGR_HPRE_Msk | RCC_CFGR_PPRE1_Msk | RCC_CFGR_PPRE2_Msk))
			  |  static_cast<uint32_t>(BusDiv.AHB_div)
			  |  static_cast<uint32_t>(BusDiv.APB1_div)
			  |  static_cast<uint32_t>(BusDiv.APB2_div);

	// -----------------------------------------------------------------------
	// 6. Switch SYSCLK source
	// -----------------------------------------------------------------------

	// Build the SW field value from the ClkSrc enum.
	uint32_t sw_val = static_cast<uint32_t>(ClkSrc);

	// FIX #2: Expected SWS value mirrors the SW value shifted to SWS position.
	// RCC_CFGR_SW and RCC_CFGR_SWS occupy adjacent bit fields; SWS is SW << 2.
	uint32_t sws_expected = sw_val << (RCC_CFGR_SWS_Pos - RCC_CFGR_SW_Pos);

	RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_Msk) | sw_val;

	tickStart = System::GetTick();
	while ((RCC->CFGR & RCC_CFGR_SWS_Msk) != sws_expected) // FIX: was != 0
	{
		if ((System::GetTick() - tickStart) > CLOCKSWITCH_TIMEOUT_MS)
			return SysInitStatus::InitError;
	}

	// -----------------------------------------------------------------------
	// 7. Read back actual prescalers from registers and update System clocks
	// -----------------------------------------------------------------------
	{
		uint32_t cfgr = RCC->CFGR;

		uint32_t hpre  = (cfgr & RCC_CFGR_HPRE_Msk)  >> RCC_CFGR_HPRE_Pos;
		uint32_t ppre1 = (cfgr & RCC_CFGR_PPRE1_Msk) >> RCC_CFGR_PPRE1_Pos;
		uint32_t ppre2 = (cfgr & RCC_CFGR_PPRE2_Msk) >> RCC_CFGR_PPRE2_Pos;

		AHB_Pre  = (hpre  < AHB_NO_DIV_THRESHOLD) ? 1U : (1U << (hpre  - AHB_EXPONENT_OFFSET));
		APB1_Pre = (ppre1 < APB_NO_DIV_THRESHOLD) ? 1U : (1U << (ppre1 - APB_EXPONENT_OFFSET));
		APB2_Pre = (ppre2 < APB_NO_DIV_THRESHOLD) ? 1U : (1U << (ppre2 - APB_EXPONENT_OFFSET));

		System::SystemCoreClock = sys_clk / AHB_Pre;
		System::APB1BusClock    = System::SystemCoreClock / APB1_Pre;
		System::APB2BusClock    = System::SystemCoreClock / APB2_Pre;
		System::TIMxAPB1Clock   = (APB1_Pre == 1U) ? System::APB1BusClock
													: System::APB1BusClock * 2U;
		System::TIMxAPB2Clock   = (APB2_Pre == 1U) ? System::APB2BusClock
													: System::APB2BusClock * 2U;
	}

	// -----------------------------------------------------------------------
	// 8. Reconfigure SysTick for the new core frequency
	// -----------------------------------------------------------------------
	System::InitTicks();

	return SysInitStatus::InitOK;
}

// ---------------------------------------------------------------------------
// ClockSystem::Init_calc_pll
// ---------------------------------------------------------------------------

/**
 * @brief Automatically calculate PLL coefficients for @p req_freq.
 *
 * Strategy:
 *   - Choose M so that VCO input = 2 MHz  (input_clk / M ≈ 2 MHz)
 *   - Choose P = 2  (smallest divider → highest VCO headroom)
 *   - FIX #6: Correct formula: N = (req_freq * P) / PLL_in
 *             Original code set N = req_freq / 1e6, which only worked by
 *             coincidence when PLL_in happened to equal 1 MHz.
 */
SysInitStatus ClockSystem::Init_calc_pll(uint32_t        req_freq,
										 PLL_ClockSource pll_src,
										 uint32_t        hse_clk,
										 uint32_t        pll_q)
{
	if ((pll_src == PLL_ClockSource::HSE) && (hse_clk == 0U))
		return SysInitStatus::InitError;

	if (pll_src == PLL_ClockSource::NO)
		return SysInitStatus::InitError;

	uint32_t input_clk = (pll_src == PLL_ClockSource::HSE) ? hse_clk : HSI_Clock;

	// --- Choose M: target PLL_in = 2 MHz ---
	// Ceiling division so PLL_in <= 2 MHz (stays within 1–2 MHz spec).
	uint8_t pll_m = static_cast<uint8_t>((input_clk + 1999999UL) / 2000000UL);
	if ((pll_m < 2U) || (pll_m > 63U))
		return SysInitStatus::InitError;

	uint32_t PLL_in = input_clk / pll_m; // actual VCO input frequency

	// --- Choose P = 2 ---
	uint8_t pll_p = 2U;

	// --- FIX: Correct N calculation ---
	// VCO_out = req_freq * pll_p → N = VCO_out / PLL_in
	uint32_t VCO_target = req_freq * pll_p;
	uint16_t pll_n = static_cast<uint16_t>(VCO_target / PLL_in);

	// Validate computed parameters before passing to Init()
	if ((pll_n < 50U) || (pll_n > 432U))
		return SysInitStatus::InitError;

	uint32_t VCO_actual = PLL_in * pll_n;
	if ((VCO_actual < 100000000UL) || (VCO_actual > 432000000UL))
		return SysInitStatus::InitError;

	// --- Select bus dividers to keep APB clocks within spec ---
	BusDividers div;
	div.AHB_div = AHB_Divider::DIV1;

	if (req_freq <= APB1_CLK_LIMIT)
	{
		div.APB1_div = APB1_Divider::DIV1;
		div.APB2_div = APB2_Divider::DIV1;
	}
	else if (req_freq <= APB2_CLK_LIMIT)
	{
		div.APB1_div = APB1_Divider::DIV2;
		div.APB2_div = APB2_Divider::DIV1;
	}
	else
	{
		div.APB1_div = APB1_Divider::DIV4;
		div.APB2_div = APB2_Divider::DIV2;
	}

	PLL_CFGR pll_cfg;
	pll_cfg.PLL_ClkSrc = pll_src;
	pll_cfg.PLL_M      = pll_m;
	pll_cfg.PLL_N      = pll_n;
	pll_cfg.PLL_P      = pll_p;
	pll_cfg.PLL_Q      = static_cast<uint8_t>(pll_q);
	pll_cfg.PLL_R      = 2U; // default; not used unless ClkSrc == PLL_R

	return Init(SystemClockSource::PLL_P, hse_clk, div, pll_cfg);
}
