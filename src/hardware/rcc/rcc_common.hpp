/**
 * @file rcc_common.hpp
 * @brief Shared types and helpers for STM32 RCC clock preparation.
 */
#ifndef RCC_COMMON_HPP_
#define RCC_COMMON_HPP_

// Included by the selected RCC family header after system.hpp.

namespace RCCDefs {

/**
 * @brief Encoded RCC SYSCLK selection, not a frequency value.
 * F4/F7 PLL normally selects P; PLL_R exists only on devices exposing it.
 * G0/G4 PLL selects R; G0 also supports LSE/LSI as SYSCLK.
 * L0 adds MSI and uses its PLL multiplier/output divider.
 */
enum class SystemClockSource : uint32_t {
#if defined(STM32G0)
	HSI = RCC_CFGR_SW_HSISYS, HSE = RCC_CFGR_SW_HSE, PLL = RCC_CFGR_SW_PLLRCLK,
	LSE = RCC_CFGR_SW_LSE, LSI = RCC_CFGR_SW_LSI,
#else
	HSI = RCC_CFGR_SW_HSI, HSE = RCC_CFGR_SW_HSE, PLL = RCC_CFGR_SW_PLL,
#ifdef RCC_CFGR_SW_PLLR
	PLL_R = RCC_CFGR_SW_PLLR,
#endif
#if defined(STM32L0)
	MSI = RCC_CFGR_SW_MSI,
#endif
#endif
};

/// HPRE field encodings; DIV8 is a divide-by-eight choice, not numeric value 8.
enum class AHB_Divider : uint32_t {
	DIV1 = 0, DIV2 = 8, DIV4 = 9, DIV8 = 10, DIV16 = 11,
	DIV64 = 12, DIV128 = 13, DIV256 = 14, DIV512 = 15,
};
/// PPRE field encodings. Timer clock is PCLK for /1 and 2*PCLK otherwise.
enum class APB_Divider : uint32_t {
	DIV1 = 0, DIV2 = 4, DIV4 = 5, DIV8 = 6, DIV16 = 7,
};
/// PLL input selection; NO is invalid when a PLL output supplies SYSCLK.
enum class PLL_ClockSource : uint32_t {
	NO = 0xFFFFFFFFU,
#if defined(STM32L0)
	HSI = RCC_CFGR_PLLSRC_HSI, HSE = RCC_CFGR_PLLSRC_HSE,
#else
	HSI = RCC_PLLCFGR_PLLSRC_HSI, HSE = RCC_PLLCFGR_PLLSRC_HSE,
#endif
};

#if defined(STM32L0)
/// MSI ranges: nominal frequency = 65,536 Hz << range number.
enum class MSI_Range : uint32_t {
	Range0 = RCC_ICSCR_MSIRANGE_0, Range1 = RCC_ICSCR_MSIRANGE_1,
	Range2 = RCC_ICSCR_MSIRANGE_2, Range3 = RCC_ICSCR_MSIRANGE_3,
	Range4 = RCC_ICSCR_MSIRANGE_4, Range5 = RCC_ICSCR_MSIRANGE_5,
	Range6 = RCC_ICSCR_MSIRANGE_6,
};
/// Regulator range; checked against HCLK, PLL VCO and Flash timing.
enum class VoltageRange : uint32_t {
	Range1 = PWR_CR_VOS_0, Range2 = PWR_CR_VOS_1, Range3 = PWR_CR_VOS_Msk,
};
#endif

/**
 * @brief AHB/APB prescalers. Defaults are /1; APB2 is ignored on G0.
 * Manual Config must choose bus clocks within the selected device limits.
 */
struct BusDividers {
	AHB_Divider AHB_div = AHB_Divider::DIV1;
	APB_Divider APB1_div = APB_Divider::DIV1;
	APB_Divider APB2_div = APB_Divider::DIV1; // Ignored on G0 (no APB2).
};

/**
 * @brief Numeric PLL coefficients, not their encoded register bit patterns.
 * M divides the source, N multiplies it, P/Q/R divide the VCO.
 * F4/F7 normally use P for SYSCLK; R is ignored when absent.
 * G0/G4 configure P/Q divisors where present but enable only the R output.
 * L0 instead has PLL_MUL and PLL_DIV; no M/N/P/Q/R fields.
 * These defaults are placeholders, not an automatically chosen board clock.
 */
struct PLL_CFGR {
	PLL_ClockSource PLL_ClkSrc = PLL_ClockSource::NO;
#if defined(STM32L0)
	uint8_t PLL_MUL = 3U;
	uint8_t PLL_DIV = 2U;
#else
	uint8_t PLL_M = 2U;
	uint16_t PLL_N = 50U;
	uint8_t PLL_P = 2U;
	uint8_t PLL_Q = 2U;
	uint8_t PLL_R = 2U;
#endif
};

/**
 * @brief Complete nominal clock request evaluated by Init<config>().
 * HSE_Clk is in Hz and used only if HSE supplies SYSCLK or the active PLL.
 * PLLCfgr is ignored for direct oscillator clocking.
 * L0: MSIRange is used only with MSI; Voltage defaults to reset Range 2.
 */
struct Config {
	SystemClockSource ClkSrc = SystemClockSource::HSI;
	uint32_t HSE_Clk = 0U; // Nominal Hz; read only if HSE actually supplies SYSCLK/PLL.
	BusDividers BusDiv{};
	PLL_CFGR PLLCfgr{};    // Ignored unless ClkSrc selects a PLL output.
#if defined(STM32L0)
	MSI_Range MSIRange = MSI_Range::Range5; // Used only for direct MSI SYSCLK.
	VoltageRange Voltage = VoltageRange::Range2; // Hardware reset choice.
#endif
};

enum class Error : uint8_t { None, Source, HSE, PLL, VCO, Divider, Frequency, Tick, Power };
/**
 * @brief Compiler-produced register words and derived clock/tick values.
 * Rational frequencies are checked before integer division; reported integer
 * clocks/reload use truncation. Apply() consumes this result without rerunning
 * the configuration search. boot_* values keep timeout ticks usable while
 * applying bus dividers before the final SYSCLK switch.
 */
struct Prepared {
	Error error = Error::None;
	SystemClockSource source = SystemClockSource::HSI;
	uint32_t hse = 0U, pll_word = 0U, bus_word = 0U;
	uint32_t hclk = 0U, apb1 = 0U, apb2 = 0U, tim1 = 0U, tim2 = 0U;
	uint32_t ticks = 0U, boot_hclk = 0U, boot_ticks = 0U, latency = 0U;
	bool pll = false, overdrive = false;
#if defined(STM32L0)
	uint32_t vos = 0U, msi_range = 0U;
#elif defined(STM32G4)
	uint32_t switch_bus_word = 0U, switch_hclk = 0U, switch_ticks = 0U;
	bool boost = false;
#endif
};

// Numeric operating limits selected entirely at compilation.
// Only quantities shared by the automatic search and frequency calculations live here.
// Family headers supply the remaining validation, power and latency rules.
struct ClockLimits {
	uint32_t sys_max = 0U;
	uint32_t apb1_max = 0U;
	uint32_t apb2_max = 0U;
	uint32_t pll_m_min = 0U;
	uint32_t pll_m_max = 0U;
	uint32_t pll_n_min = 0U;
	uint32_t pll_n_max = 0U;
	uint32_t pll_in_min = 0U;
	uint32_t pll_in_max = 0U;
};

// The 3.0 V and 3.3 V supplies use the same Flash timing band.
inline constexpr uint32_t VDD_MV = 3000U;

// Run-time hardware waits, in System tick units (nominal milliseconds).
// LSE startup/switching gets a longer budget than PLL lock/disable.
inline constexpr uint32_t PLL_TIMEOUT_MS = 2U;
inline constexpr uint32_t HSE_TIMEOUT_MS = 100U;
inline constexpr uint32_t LSI_TIMEOUT_MS = 2U;
inline constexpr uint32_t LSE_TIMEOUT_MS = 5000U;
inline constexpr uint32_t POWER_TIMEOUT_MS = 100U;
inline constexpr uint32_t FLASH_TIMEOUT_MS = 100U;
inline constexpr uint32_t SWITCH_TIMEOUT_MS = 5000U;
inline constexpr uint32_t LSE_HZ = 32768U;
static_assert(VDD_MV >= 2700U && VDD_MV <= 3600U, "RCC Flash timing assumes 2.7..3.6 V");

// The search reuses ClockSystem::Prepare through a function reference.
// Init<config>() validates the supplied configuration at compilation.
using PrepareFunction = Prepared(const Config&);

constexpr uint32_t AHBDivisor(AHB_Divider divider)
{
	switch (divider) {
	case AHB_Divider::DIV1: return 1U;
	case AHB_Divider::DIV2: return 2U;
	case AHB_Divider::DIV4: return 4U;
	case AHB_Divider::DIV8: return 8U;
	case AHB_Divider::DIV16: return 16U;
	case AHB_Divider::DIV64: return 64U;
	case AHB_Divider::DIV128: return 128U;
	case AHB_Divider::DIV256: return 256U;
	case AHB_Divider::DIV512: return 512U;
	}
	return 0U;
}

constexpr uint32_t APBDivisor(APB_Divider divider)
{
	switch (divider) {
	case APB_Divider::DIV1: return 1U;
	case APB_Divider::DIV2: return 2U;
	case APB_Divider::DIV4: return 4U;
	case APB_Divider::DIV8: return 8U;
	case APB_Divider::DIV16: return 16U;
	}
	return 0U;
}

constexpr APB_Divider SelectAPB(uint32_t hclk, uint32_t limit)
{
	constexpr APB_Divider dividers[] = {APB_Divider::DIV1, APB_Divider::DIV2, APB_Divider::DIV4,
									  APB_Divider::DIV8, APB_Divider::DIV16};
	for (APB_Divider divider : dividers)
		if (static_cast<uint64_t>(hclk) <= static_cast<uint64_t>(limit) * APBDivisor(divider))
			return divider;
	return static_cast<APB_Divider>(0xFFFFFFFFU);
}

#if defined(STM32G0) || defined(STM32G4)
constexpr bool ValidateHSE(uint32_t frequency) { return frequency >= 4U * MHz && frequency <= 48U * MHz; }
constexpr uint32_t PLLOutput(const Config& config) { return config.PLLCfgr.PLL_R; }
constexpr void SetPLLOutput(PLL_CFGR& pll, uint8_t divider) { pll.PLL_R = divider; }
#endif

#if !defined(STM32G0)
constexpr uint32_t EncodeBuses(const BusDividers& bus)
{
	return (static_cast<uint32_t>(bus.AHB_div) << RCC_CFGR_HPRE_Pos)
		| (static_cast<uint32_t>(bus.APB1_div) << RCC_CFGR_PPRE1_Pos)
		| (static_cast<uint32_t>(bus.APB2_div) << RCC_CFGR_PPRE2_Pos);
}
#endif

#if !defined(STM32L0) && !defined(STM32G0)
constexpr bool PrepareExtraSource(const Config&, Prepared& result, uint64_t&)
{
	result.error = Error::Source;
	return false;
}
#endif

#if defined(STM32L0) || defined(STM32G0)
constexpr void PrepareLatency(uint32_t, Prepared& result, uint64_t numerator, uint64_t denominator, uint32_t step)
{
	result.latency = static_cast<uint32_t>((numerator - 1U) / (denominator * step));
}
#endif

#if !defined(STM32G4)
constexpr void PrepareTransition(uint32_t, Prepared&, uint64_t, uint64_t) {}
#endif

} // namespace RCCDefs

#endif // RCC_COMMON_HPP_
