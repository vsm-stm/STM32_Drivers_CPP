/**
 * @file rcc_defs_l0.hpp
 * @brief L0 RCC limits, register encodings and clock preparation.
 */
#ifndef RCC_DEFS_L0_HPP_
#define RCC_DEFS_L0_HPP_

#include "rcc_common.hpp"

namespace RCCDefs {

inline constexpr ClockLimits limits{
	.sys_max               = 32U * MHz,
	.apb1_max              = 32U * MHz,
	.apb2_max              = 32U * MHz,
	.pll_in_min            = 2U * MHz,
	.pll_in_max            = 24U * MHz,
};

inline constexpr uint32_t BUS_MASK = RCC_CFGR_HPRE_Msk | RCC_CFGR_PPRE1_Msk | RCC_CFGR_PPRE2_Msk;
inline constexpr uint32_t PLL_MASK = RCC_CFGR_PLLSRC_Msk | RCC_CFGR_PLLMUL_Msk | RCC_CFGR_PLLDIV_Msk;
inline constexpr uint8_t PLL_MULTIPLIERS[] = {3U, 4U, 6U, 8U, 12U, 16U, 24U, 32U, 48U};

inline constexpr uint32_t MSI_TIMEOUT_MS = 2U;
inline constexpr uint32_t HSI_TIMEOUT_MS = 2U;
inline constexpr uint32_t PLLSysMin = 2U * MHz;

constexpr bool ValidateHSE(uint32_t frequency) { return frequency >= 1U * MHz && frequency <= 25U * MHz; }

constexpr bool PrepareVoltage(const Config& config, Prepared& result,
		uint32_t& hclk, uint32_t& vco, uint32_t& flash)
{
	result.vos = static_cast<uint32_t>(config.Voltage);
	switch (config.Voltage) {
	case VoltageRange::Range1:
		hclk = limits.sys_max; vco = 96U * MHz; flash = 16U * MHz; return true;
	case VoltageRange::Range2:
		hclk = 16U * MHz; vco = 48U * MHz; flash = 8U * MHz; return true;
	case VoltageRange::Range3:
		hclk = 4200U * kHz; vco = 24U * MHz; flash = 4200U * kHz; return true;
	}
	result.error = Error::Power;
	return false;
}

constexpr bool PrepareExtraSource(const Config& config, Prepared& result, uint64_t& frequency)
{
	if (config.ClkSrc == SystemClockSource::MSI) {
		result.msi_range = static_cast<uint32_t>(config.MSIRange);
		const uint32_t range = result.msi_range >> RCC_ICSCR_MSIRANGE_Pos;
		if (range <= 6U && (result.msi_range & ~RCC_ICSCR_MSIRANGE_Msk) == 0U) {
			frequency = 65536U << range;
			return true;
		}
	}
	result.error = Error::Source; return false;
}

constexpr bool ValidatePLL(const PLL_CFGR& pll)
{
	if (pll.PLL_DIV < 2U || pll.PLL_DIV > 4U) return false;
	for (uint8_t mul : PLL_MULTIPLIERS)
		if (pll.PLL_MUL == mul) return true;
	return false;
}

constexpr bool CalculatePLL(const Config& config, uint64_t& numerator, uint64_t& denominator, uint32_t vco_limit)
{
	if (numerator < limits.pll_in_min || numerator > limits.pll_in_max) return false;
	numerator *= config.PLLCfgr.PLL_MUL;
	if (numerator > vco_limit) return false;
	denominator = config.PLLCfgr.PLL_DIV;
	return true;
}

constexpr uint32_t EncodePLL(const PLL_CFGR& pll)
{
	uint32_t index = 0U;
	while (PLL_MULTIPLIERS[index] != pll.PLL_MUL) ++index; // Already validated.
	return static_cast<uint32_t>(pll.PLL_ClkSrc) | (index << RCC_CFGR_PLLMUL_Pos)
		| ((pll.PLL_DIV - 1U) << RCC_CFGR_PLLDIV_Pos);
}

constexpr Config FindPLL(Config result, uint32_t frequency, uint32_t input, PrepareFunction& prepare)
{
	for (uint8_t div = 2U; div <= 4U; ++div) {
		for (uint8_t mul : PLL_MULTIPLIERS) {
			if (static_cast<uint64_t>(input) * mul != static_cast<uint64_t>(frequency) * div) continue;
			Config candidate = result;
			candidate.PLLCfgr.PLL_MUL = mul;
			candidate.PLLCfgr.PLL_DIV = div;
			if (prepare(candidate).error == Error::None) return candidate;
		}
	}
	return result;
}

} // namespace RCCDefs

#endif // RCC_DEFS_L0_HPP_
