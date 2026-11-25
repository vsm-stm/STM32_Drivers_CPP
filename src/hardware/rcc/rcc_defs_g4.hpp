/**
 * @file rcc_defs_g4.hpp
 * @brief G4 RCC limits, register encodings and clock preparation.
 */
#ifndef RCC_DEFS_G4_HPP_
#define RCC_DEFS_G4_HPP_

#include "rcc_common.hpp"

namespace RCCDefs {

inline constexpr ClockLimits limits{
	.sys_max               = 170U * MHz,
	.apb1_max              = 170U * MHz,
	.apb2_max              = 170U * MHz,
	.pll_m_min             = 1U,
	.pll_m_max             = 16U,
	.pll_n_min             = 8U,
	.pll_n_max             = 127U,
	.pll_in_min            = 2660U * kHz,
	.pll_in_max            = 16U * MHz,
};

inline constexpr uint32_t VOS_VALUE = PWR_CR1_VOS_0;
inline constexpr uint32_t BUS_MASK = RCC_CFGR_HPRE_Msk | RCC_CFGR_PPRE1_Msk | RCC_CFGR_PPRE2_Msk;
inline constexpr uint32_t PLL_MASK = RCC_PLLCFGR_PLLSRC_Msk | RCC_PLLCFGR_PLLM_Msk
	| RCC_PLLCFGR_PLLN_Msk | RCC_PLLCFGR_PLLP_Msk | RCC_PLLCFGR_PLLPDIV_Msk
	| RCC_PLLCFGR_PLLQ_Msk | RCC_PLLCFGR_PLLR_Msk
	| RCC_PLLCFGR_PLLPEN | RCC_PLLCFGR_PLLQEN | RCC_PLLCFGR_PLLREN;

inline constexpr uint32_t PLLOutputStep = 2U;
inline constexpr uint32_t FlashStep = 30U * MHz;
inline constexpr uint32_t PLLSysMin = 8U * MHz;

constexpr bool ValidateVCO(uint64_t numerator, uint32_t divisor)
{
	return numerator >= 64ULL * MHz * divisor && numerator <= 344ULL * MHz * divisor;
}

constexpr bool ValidatePLL(const PLL_CFGR& pll)
{
	return pll.PLL_M >= limits.pll_m_min && pll.PLL_M <= limits.pll_m_max
		&& pll.PLL_N >= limits.pll_n_min && pll.PLL_N <= limits.pll_n_max
		&& (pll.PLL_R == 2U || pll.PLL_R == 4U || pll.PLL_R == 6U || pll.PLL_R == 8U)
		&& (pll.PLL_Q == 2U || pll.PLL_Q == 4U || pll.PLL_Q == 6U || pll.PLL_Q == 8U)
		&& pll.PLL_P >= 2U && pll.PLL_P <= 31U;
}

constexpr uint32_t EncodePLL(const PLL_CFGR& pll)
{
	return static_cast<uint32_t>(pll.PLL_ClkSrc)
		| ((pll.PLL_M - 1U) << RCC_PLLCFGR_PLLM_Pos)
		| (static_cast<uint32_t>(pll.PLL_N) << RCC_PLLCFGR_PLLN_Pos)
		| (static_cast<uint32_t>(pll.PLL_P) << RCC_PLLCFGR_PLLPDIV_Pos)
		| ((pll.PLL_Q / 2U - 1U) << RCC_PLLCFGR_PLLQ_Pos)
		| ((pll.PLL_R / 2U - 1U) << RCC_PLLCFGR_PLLR_Pos) | RCC_PLLCFGR_PLLREN;
}

constexpr void PrepareLatency(uint32_t ahb, Prepared& result, uint64_t numerator, uint64_t denominator, uint32_t normal_step)
{
	constexpr uint32_t boost_limit = 150U * MHz;
	constexpr uint32_t boost_step = 34U * MHz;
	result.boost = numerator * ahb > static_cast<uint64_t>(boost_limit) * denominator;
	const uint32_t step = result.boost ? boost_step : normal_step;
	result.latency = static_cast<uint32_t>((numerator - 1U) / (denominator * step));
}

constexpr void PrepareTransition(uint32_t ahb, Prepared& result, uint64_t numerator, uint64_t denominator)
{
	result.switch_bus_word = result.bus_word;
	result.switch_hclk = result.hclk;
	result.switch_ticks = result.ticks;
	if (result.pll && ahb == 1U
		&& numerator > 80ULL * MHz * denominator) {
		result.switch_bus_word = (result.bus_word & ~RCC_CFGR_HPRE_Msk)
			| (static_cast<uint32_t>(AHB_Divider::DIV2) << RCC_CFGR_HPRE_Pos);
		result.boot_hclk = System::ResetClock / 2U;
		result.boot_ticks = result.boot_hclk / TICK_BASE;
		result.switch_hclk = result.hclk / 2U;
		result.switch_ticks = result.switch_hclk / TICK_BASE;
	}
}

} // namespace RCCDefs

#endif // RCC_DEFS_G4_HPP_
