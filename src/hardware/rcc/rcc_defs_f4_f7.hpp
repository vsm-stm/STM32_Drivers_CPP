/**
 * @file rcc_defs_f4_f7.hpp
 * @brief F4/F7 RCC limits, register encodings and clock preparation.
 */
#ifndef RCC_DEFS_F4_F7_HPP_
#define RCC_DEFS_F4_F7_HPP_

#include "rcc_common.hpp"

namespace RCCDefs {

#if defined(STM32F401xC) || defined(STM32F401xE)
inline constexpr ClockLimits limits{
	.sys_max               = 84U * MHz,
	.apb1_max              = 42U * MHz,
	.apb2_max              = 84U * MHz,
	.pll_m_min             = 2U,
	.pll_m_max             = 63U,
	.pll_n_min             = 192U,
	.pll_n_max             = 432U,
	.pll_in_min            = 1U * MHz,
	.pll_in_max            = 2U * MHz,
};
#elif defined(STM32F410Cx) || defined(STM32F410Rx) || defined(STM32F410Tx) \
   || defined(STM32F411xE) || defined(STM32F412Cx) || defined(STM32F412Rx) \
   || defined(STM32F412Vx) || defined(STM32F412Zx) || defined(STM32F413xx) || defined(STM32F423xx)
inline constexpr ClockLimits limits{
	.sys_max               = 100U * MHz,
	.apb1_max              = 50U * MHz,
	.apb2_max              = 100U * MHz,
	.pll_m_min             = 2U,
	.pll_m_max             = 63U,
	.pll_n_min             = 50U,
	.pll_n_max             = 432U,
	.pll_in_min            = 1U * MHz,
	.pll_in_max            = 2U * MHz,
};
#elif defined(STM32F405xx) || defined(STM32F407xx) || defined(STM32F415xx) || defined(STM32F417xx)
inline constexpr ClockLimits limits{
	.sys_max               = 168U * MHz,
	.apb1_max              = 42U * MHz,
	.apb2_max              = 84U * MHz,
	.pll_m_min             = 2U,
	.pll_m_max             = 63U,
	.pll_n_min             = 50U,
	.pll_n_max             = 432U,
	.pll_in_min            = 1U * MHz,
	.pll_in_max            = 2U * MHz,
};
#elif defined(STM32F427xx) || defined(STM32F429xx) || defined(STM32F437xx) || defined(STM32F439xx) \
   || defined(STM32F446xx) || defined(STM32F469xx) || defined(STM32F479xx)
inline constexpr ClockLimits limits{
	.sys_max               = 180U * MHz,
	.apb1_max              = 45U * MHz,
	.apb2_max              = 90U * MHz,
	.pll_m_min             = 2U,
	.pll_m_max             = 63U,
	.pll_n_min             = 50U,
	.pll_n_max             = 432U,
	.pll_in_min            = 1U * MHz,
	.pll_in_max            = 2U * MHz,
};
#elif defined(STM32F722xx) || defined(STM32F723xx) || defined(STM32F730xx) \
   || defined(STM32F732xx) || defined(STM32F733xx) || defined(STM32F745xx) \
   || defined(STM32F746xx) || defined(STM32F750xx) || defined(STM32F756xx) \
   || defined(STM32F765xx) || defined(STM32F767xx) || defined(STM32F769xx) \
   || defined(STM32F777xx) || defined(STM32F779xx)
inline constexpr ClockLimits limits{
	.sys_max               = 216U * MHz,
	.apb1_max              = 54U * MHz,
	.apb2_max              = 108U * MHz,
	.pll_m_min             = 2U,
	.pll_m_max             = 63U,
	.pll_n_min             = 50U,
	.pll_n_max             = 432U,
	.pll_in_min            = 1U * MHz,
	.pll_in_max            = 2U * MHz,
};
#else
#error "RCC: unknown F4/F7 device; add verified clock limits to rcc_defs_f4_f7.hpp"
#endif

inline constexpr uint32_t BUS_MASK = RCC_CFGR_HPRE_Msk | RCC_CFGR_PPRE1_Msk | RCC_CFGR_PPRE2_Msk;
#if defined(STM32F7)
inline constexpr uint32_t VOS_VALUE = PWR_CR1_VOS_Msk;
#elif defined(STM32F401xC) || defined(STM32F401xE)
inline constexpr uint32_t VOS_VALUE = PWR_CR_VOS_1;
#else
inline constexpr uint32_t VOS_VALUE = PWR_CR_VOS_Msk;
#endif
inline constexpr uint32_t PLL_MASK = RCC_PLLCFGR_PLLSRC_Msk | RCC_PLLCFGR_PLLM_Msk
	| RCC_PLLCFGR_PLLN_Msk | RCC_PLLCFGR_PLLP_Msk | RCC_PLLCFGR_PLLQ_Msk
#ifdef RCC_PLLCFGR_PLLR_Msk
	| RCC_PLLCFGR_PLLR_Msk
#endif
	;

inline constexpr uint32_t PLLOutputStep = 2U;
inline constexpr uint32_t FlashStep = 30U * MHz;
#if defined(STM32F446xx)
inline constexpr uint32_t PLLSysMin = 12500U * kHz;
#else
inline constexpr uint32_t PLLSysMin = 24U * MHz;
#endif

constexpr bool ValidateHSE(uint32_t frequency) { return frequency >= 4U * MHz && frequency <= 26U * MHz; }
constexpr bool ValidateVCO(uint64_t numerator, uint32_t divisor)
{
#if defined(STM32F401xC) || defined(STM32F401xE) || defined(STM32F469xx) || defined(STM32F479xx)
	// DS11189/DS11118 require 192 MHz even though RM0386 permits 100 MHz.
	constexpr uint32_t minimum = 192U * MHz;
#else
	constexpr uint32_t minimum = 100U * MHz;
#endif
	return numerator >= static_cast<uint64_t>(minimum) * divisor
		&& numerator <= 432ULL * MHz * divisor;
}

constexpr bool ValidatePLL(const PLL_CFGR& pll)
{
	return pll.PLL_M >= limits.pll_m_min && pll.PLL_M <= limits.pll_m_max
		&& pll.PLL_N >= limits.pll_n_min && pll.PLL_N <= limits.pll_n_max
		&& (pll.PLL_P == 2U || pll.PLL_P == 4U || pll.PLL_P == 6U || pll.PLL_P == 8U)
		&& pll.PLL_Q >= 2U && pll.PLL_Q <= 15U
#ifdef RCC_PLLCFGR_PLLR_Msk
		&& pll.PLL_R >= 2U && pll.PLL_R <= 7U
#endif
		;
}

constexpr uint32_t PLLOutput(const Config& config)
{
#ifdef RCC_CFGR_SW_PLLR
	if (config.ClkSrc == SystemClockSource::PLL_R) return config.PLLCfgr.PLL_R;
#endif
	return config.PLLCfgr.PLL_P;
}

constexpr uint32_t EncodePLL(const PLL_CFGR& pll)
{
	return static_cast<uint32_t>(pll.PLL_ClkSrc)
		| (static_cast<uint32_t>(pll.PLL_M) << RCC_PLLCFGR_PLLM_Pos)
		| (static_cast<uint32_t>(pll.PLL_N) << RCC_PLLCFGR_PLLN_Pos)
		| ((pll.PLL_P / 2U - 1U) << RCC_PLLCFGR_PLLP_Pos)
		| (static_cast<uint32_t>(pll.PLL_Q) << RCC_PLLCFGR_PLLQ_Pos)
#ifdef RCC_PLLCFGR_PLLR_Msk
		| (static_cast<uint32_t>(pll.PLL_R) << RCC_PLLCFGR_PLLR_Pos)
#endif
		;
}

constexpr void SetPLLOutput(PLL_CFGR& pll, uint8_t divider) { pll.PLL_P = divider; }

constexpr void PrepareLatency(uint32_t, Prepared& result, uint64_t numerator, uint64_t denominator, uint32_t step)
{
#if defined(STM32F7)
	result.overdrive = numerator > 180ULL * MHz * denominator;
#elif defined(PWR_CR_ODEN)
	result.overdrive = numerator > 168ULL * MHz * denominator;
#else
	result.overdrive = false;
#endif
	result.latency = static_cast<uint32_t>((numerator - 1U) / (denominator * step));
}

} // namespace RCCDefs

#endif // RCC_DEFS_F4_F7_HPP_
