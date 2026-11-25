/**
 * @file rcc_defs_g0.hpp
 * @brief G0 RCC limits, register encodings and clock preparation.
 */
#ifndef RCC_DEFS_G0_HPP_
#define RCC_DEFS_G0_HPP_

#include "rcc_common.hpp"

namespace RCCDefs {

// Datasheet operating limits; the RCC register description alone is broader.
inline constexpr ClockLimits limits{
	.sys_max               = 64U * MHz,
	.apb1_max              = 64U * MHz,
	.apb2_max              = 0U,
	.pll_m_min             = 1U,
	.pll_m_max             = 8U,
	.pll_n_min             = 8U,
	.pll_n_max             = 86U,
	.pll_in_min            = 2660U * kHz,
	.pll_in_max            = 16U * MHz,
};

inline constexpr uint32_t VOS_VALUE = PWR_CR1_VOS_0;
inline constexpr uint32_t BUS_MASK = RCC_CFGR_HPRE_Msk | RCC_CFGR_PPRE_Msk;
inline constexpr uint32_t PLL_MASK = RCC_PLLCFGR_PLLSRC_Msk | RCC_PLLCFGR_PLLM_Msk
	| RCC_PLLCFGR_PLLN_Msk | RCC_PLLCFGR_PLLP_Msk | RCC_PLLCFGR_PLLR_Msk
	| RCC_PLLCFGR_PLLPEN | RCC_PLLCFGR_PLLREN
#ifdef RCC_PLLQ_SUPPORT
	| RCC_PLLCFGR_PLLQ_Msk | RCC_PLLCFGR_PLLQEN
#endif
	;

inline constexpr uint32_t PLLOutputStep = 1U;
inline constexpr uint32_t FlashStep = 24U * MHz;
inline constexpr uint32_t PLLSysMin = 12U * MHz;

constexpr bool ValidateVCO(uint64_t numerator, uint32_t divisor)
{
	return numerator >= 96ULL * MHz * divisor && numerator <= 344ULL * MHz * divisor;
}

constexpr bool PrepareExtraSource(const Config& config, Prepared& result, uint64_t& frequency)
{
	switch (config.ClkSrc) {
	case SystemClockSource::LSI: frequency = LSI_Clock; return true;
	case SystemClockSource::LSE: frequency = LSE_HZ; return true;
	default: result.error = Error::Source; return false;
	}
}

constexpr bool ValidatePLL(const PLL_CFGR& pll)
{
	return pll.PLL_M >= limits.pll_m_min && pll.PLL_M <= limits.pll_m_max
		&& pll.PLL_N >= limits.pll_n_min && pll.PLL_N <= limits.pll_n_max
		&& pll.PLL_R >= 2U && pll.PLL_R <= 8U && pll.PLL_P >= 2U && pll.PLL_P <= 32U
#ifdef RCC_PLLQ_SUPPORT
		&& pll.PLL_Q >= 2U && pll.PLL_Q <= 8U
#endif
		;
}

constexpr uint32_t EncodePLL(const PLL_CFGR& pll)
{
	return static_cast<uint32_t>(pll.PLL_ClkSrc)
		| ((pll.PLL_M - 1U) << RCC_PLLCFGR_PLLM_Pos)
		| (static_cast<uint32_t>(pll.PLL_N) << RCC_PLLCFGR_PLLN_Pos)
		| ((pll.PLL_P - 1U) << RCC_PLLCFGR_PLLP_Pos)
		| ((pll.PLL_R - 1U) << RCC_PLLCFGR_PLLR_Pos) | RCC_PLLCFGR_PLLREN
#ifdef RCC_PLLQ_SUPPORT
		| ((pll.PLL_Q - 1U) << RCC_PLLCFGR_PLLQ_Pos)
#endif
		;
}

constexpr uint32_t EncodeBuses(const BusDividers& bus)
{
	return (static_cast<uint32_t>(bus.AHB_div) << RCC_CFGR_HPRE_Pos)
		| (static_cast<uint32_t>(bus.APB1_div) << RCC_CFGR_PPRE_Pos);
}

} // namespace RCCDefs

#endif // RCC_DEFS_G0_HPP_
