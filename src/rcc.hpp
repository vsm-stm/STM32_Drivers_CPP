/**
 * @file rcc.hpp
 * @brief Compile-time clock configuration and one hardware application on F4/F7/G0/L0/G4.
 *
 * Config is a C++20 structural value passed to Init<config>(). Prepare() calculates
 * PLL register fields, bus/timer clocks, Flash latency and SysTick reload during
 * compilation. static_assert reports invalid configurations; run-time code waits
 * for hardware ready flags and applies the prepared register values.
 *
 * Initialize System first, on the reset source with a serviceable SysTick.
 * L0 starts on MSI range 5; the other families start on HSI16 /1.
 * Configure clocks once before peripheral setup. Reconfiguration and retry after
 * a failed hardware initialization are not supported; reset before another attempt.
 * Device limits assume VDD=3.0 V (also the 3.3 V timing band); see the selected family header.
 *
 * Automatic exact nominal SYSCLK example (F4/F7/G0/G4):
 * @code
 *   constexpr auto clocks = ClockSystem::CalcPLL(
 *       64 * MHz, ClockSystem::PLL_ClockSource::HSI);
 *   if (System::Init() != SysInitStatus::InitOK)
 *       System::DebugTrap("System init");
 *   if (ClockSystem::Init<clocks>() != SysInitStatus::InitOK)
 *       System::DebugTrap("Clock hardware timeout");
 * @endcode
 *
 * Direct HSE example (the selected MCU/board must support an 8 MHz HSE):
 * @code
 *   constexpr ClockSystem::Config clocks{
 *       .ClkSrc = ClockSystem::SystemClockSource::HSE,
 *       .HSE_Clk = 8 * MHz
 *   };
 *   // After System::Init(): ClockSystem::Init<clocks>().
 * @endcode
 *
 * Manual G0 PLL example, SYSCLK=16 MHz / 1 * 8 / 2 = 64 MHz:
 * @code
 *   constexpr ClockSystem::Config clocks{
 *       .ClkSrc = ClockSystem::SystemClockSource::PLL,
 *       .PLLCfgr = {
 *           .PLL_ClkSrc = ClockSystem::PLL_ClockSource::HSI,
 *           .PLL_M = 1, .PLL_N = 8, .PLL_P = 2, .PLL_Q = 2, .PLL_R = 2
 *       }
 *   };
 * @endcode
 * The manual example is G0-specific; F4/F7 coefficients have different limits
 * and use PLL_P for the usual SYSCLK output. No USB/audio clock setup is implied.
 *
 * L0 example (Range 2 is the default; select Range 1 for 32 MHz):
 * @code
 *   constexpr auto clocks = ClockSystem::CalcPLL(
 *       32 * MHz, ClockSystem::PLL_ClockSource::HSI, 0U,
 *       ClockSystem::VoltageRange::Range1);
 *   // System::Init(), then ClockSystem::Init<clocks>().
 * @endcode
 */
#ifndef RCC_H_
#define RCC_H_

#include "system.hpp"
#if defined(STM32F4) || defined(STM32F7)
#include "hardware/rcc/rcc_defs_f4_f7.hpp"
#elif defined(STM32G0)
#include "hardware/rcc/rcc_defs_g0.hpp"
#elif defined(STM32G4)
#include "hardware/rcc/rcc_defs_g4.hpp"
#elif defined(STM32L0)
#include "hardware/rcc/rcc_defs_l0.hpp"
#else
#error "RCC supports STM32F4, STM32F7, STM32G0, STM32L0 and STM32G4 only"
#endif

// One clock setup per hardware reset, before peripheral setup.
// Config and all derived values are validated by Init<config>() at compile time.
class ClockSystem
{
public:
	using SystemClockSource = RCCDefs::SystemClockSource;
	using AHB_Divider = RCCDefs::AHB_Divider;
	using APB_Divider = RCCDefs::APB_Divider;
	using PLL_ClockSource = RCCDefs::PLL_ClockSource;
#if defined(STM32L0)
	using MSI_Range = RCCDefs::MSI_Range;
	using VoltageRange = RCCDefs::VoltageRange;
#endif
	using BusDividers = RCCDefs::BusDividers;
	using PLL_CFGR = RCCDefs::PLL_CFGR;
	using Config = RCCDefs::Config;

	/**
	 * @brief Search for an exact nominal SYSCLK configuration without rounding a target.
	 * @param frequency Requested SYSCLK in Hz; AHB remains /1.
	 * @param source HSI or HSE; hse_clock must describe the board when HSE is used.
	 * @param hse_clock Nominal external oscillator frequency in Hz, otherwise zero.
	 * @param pll_q Numeric Q divider on F4/F7/G0/G4, subject to validation.
	 * @param voltage L0 fourth argument instead of pll_q; defaults to Range 2.
	 *        Request Range 1 explicitly for a 32 MHz L0 configuration.
	 * @return Config using the fastest valid APB clocks; PLL_M=0 (L0: PLL_MUL=0) if no solution.
	 *
	 * Search M ascending to maximize the PLL comparison frequency, then prefer
	 * the smallest P/R output divisor. L0 searches its discrete multipliers
	 * and output divisors, preferring the smallest valid output divisor. The choice is deterministic and preserves
	 * the intended low-jitter preference. It does not measure oscillator error
	 * or guarantee a 48 MHz USB/Q output. If no exact solution exists, provide
	 * a manual Config with deliberate coefficients; Init diagnoses an invalid one.
	 * Use a constexpr result with Init<config>() to keep calculation out of firmware.
	 */
	static constexpr Config CalcPLL(uint32_t frequency, PLL_ClockSource source,
									uint32_t hse_clock = 0U,
#if defined(STM32L0)
									VoltageRange voltage = VoltageRange::Range2);
#else
									uint8_t pll_q = 2U);
#endif

	/**
	 * @brief Validate a constant Config and apply it once to hardware.
	 * @tparam config Automatic or manual C++20 constant configuration.
	 * @return InitOK after switching and publishing clocks; InitError for a
	 *         repeated attempt or hardware-ready timeout. Configuration errors
	 *         are compilation errors, not a run-time InitError.
	 * @pre Successful System::Init(), reset clock source, working SysTick; call before drivers.
	 *
	 * Raises Flash latency before raising HCLK, lowers it only after switching
	 * to the lower clock, and publishes System bus/timer clocks and SysTick.
	 * A timeout can leave partial hardware changes; no automatic rollback/retry.
	 * With an integer SysTick reload, some clocks cannot produce exactly 1 ms:
	 * G0 LSE /1 uses 32 cycles per tick (1024 ticks/s), so GetTick is nominal.
	 */
	template<Config config>
	static SysInitStatus Init()
	{
		static constexpr Prepared prepared = Prepare(config);
		static_assert(prepared.error != Error::Power, "RCC: unsupported voltage range");
		static_assert(prepared.error != Error::Source, "RCC: unsupported SYSCLK or PLL source");
		static_assert(prepared.error != Error::HSE, "RCC: HSE frequency is outside the device limits");
		static_assert(prepared.error != Error::PLL, "RCC: invalid PLL coefficients or no exact CalcPLL solution; supply a manual Config");
		static_assert(prepared.error != Error::VCO, "RCC: PLL input or VCO frequency is outside the device limits");
		static_assert(prepared.error != Error::Divider, "RCC: invalid AHB/APB divider");
		static_assert(prepared.error != Error::Frequency, "RCC: SYSCLK/HCLK/APB frequency is outside the device limits");
		static_assert(prepared.error != Error::Tick, "RCC: HCLK cannot provide the configured SysTick period");
		return Apply(prepared);
	}

	/**
	 * @brief Nominal APB1 frequency derived from a constant Config, in Hz.
	 * Uses the same preparation and device validation as Init<config>(), without
	 * applying clocks or accessing hardware. Invalid Config is a compile error.
	 * For example I2C::SetUp<config>() uses this result for its timing calculation.
	 * A derived value alone does not prove that Init<config>() succeeded at runtime.
	 */
	template<Config config>
	static consteval uint32_t GetAPB1Clock()
	{
		constexpr Prepared prepared = Prepare(config);
		static_assert(prepared.error == Error::None, "RCC: invalid clock configuration for APB1 calculation");
		return prepared.apb1;
	}

	/// Nominal HSE input used by the completed clock setup; zero if unused/not published.
	static uint32_t GetHSEClock() noexcept { return HSESrcClk; }

private:
	using Error = RCCDefs::Error;
	using Prepared = RCCDefs::Prepared;
	static uint32_t HSESrcClk;
	static SysInitStatus Apply(const Prepared& config);
	static constexpr Prepared Prepare(const Config& config);
};

namespace RCCDefs {

// Standard SYSCLK/PLL sources share their input-frequency validation.
constexpr bool PrepareSource(const Config& config, Prepared& result, uint64_t& frequency)
{
	switch (config.ClkSrc) {
	case SystemClockSource::HSI: frequency = HSI_Clock; return true;
	case SystemClockSource::HSE: break;
	case SystemClockSource::PLL:
#ifdef RCC_CFGR_SW_PLLR
	case SystemClockSource::PLL_R:
#endif
		result.pll = true;
		if (config.PLLCfgr.PLL_ClkSrc == PLL_ClockSource::HSI) {
			frequency = HSI_Clock;
			return true;
		}
		if (config.PLLCfgr.PLL_ClkSrc != PLL_ClockSource::HSE) {
			result.error = Error::Source;
			return false;
		}
		break;
	default: return PrepareExtraSource(config, result, frequency);
	}
	frequency = result.hse = config.HSE_Clk;
	if (!ValidateHSE(result.hse)) {
		result.error = Error::HSE;
		return false;
	}
	return true;
}

constexpr bool ValidateSYSCLK(uint64_t numerator, uint64_t denominator, bool pll)
{
	return numerator != 0U && numerator <= static_cast<uint64_t>(limits.sys_max) * denominator
		&& (!pll || numerator >= static_cast<uint64_t>(PLLSysMin) * denominator);
}

constexpr bool PrepareBuses(const BusDividers& bus, Prepared& result, uint64_t numerator,
		uint64_t& denominator, uint32_t hclk_limit, uint32_t ahb)
{
	const uint32_t apb1 = APBDivisor(bus.APB1_div);
	uint32_t apb2 = 1U;
	if constexpr (limits.apb2_max != 0U) apb2 = APBDivisor(bus.APB2_div);
	if (ahb == 0U || apb1 == 0U || apb2 == 0U) { result.error = Error::Divider; return false; }
	if (!ValidateSYSCLK(numerator, denominator, result.pll)
		|| numerator > static_cast<uint64_t>(hclk_limit) * denominator * ahb
		|| numerator > static_cast<uint64_t>(limits.apb1_max) * denominator * ahb * apb1) {
		result.error = Error::Frequency; return false;
	}
	if constexpr (limits.apb2_max != 0U) {
		if (numerator > static_cast<uint64_t>(limits.apb2_max) * denominator * ahb * apb2) {
			result.error = Error::Frequency; return false;
		}
	}
	denominator *= ahb;
	result.hclk = static_cast<uint32_t>(numerator / denominator);
	result.apb1 = static_cast<uint32_t>(numerator / (denominator * apb1));
	result.tim1 = static_cast<uint32_t>((numerator * (apb1 == 1U ? 1U : 2U)) / (denominator * apb1));
	if constexpr (limits.apb2_max != 0U) {
		result.apb2 = static_cast<uint32_t>(numerator / (denominator * apb2));
		result.tim2 = static_cast<uint32_t>((numerator * (apb2 == 1U ? 1U : 2U)) / (denominator * apb2));
	}
	result.bus_word = EncodeBuses(bus);
	return true;
}

constexpr bool PrepareTicks(Prepared& result, uint64_t numerator, uint64_t denominator, uint32_t ahb)
{
	result.ticks = static_cast<uint32_t>(numerator / (denominator * TICK_BASE));
	result.boot_hclk = System::ResetClock / ahb;
	result.boot_ticks = result.boot_hclk / TICK_BASE;
	if (result.ticks == 0U || result.ticks > SysTick_LOAD_RELOAD_Msk + 1U) {
		result.error = Error::Tick; return false;
	}
	return true;
}

#if !defined(STM32L0)
constexpr bool PrepareVoltage(const Config&, Prepared&,
		uint32_t& hclk, uint32_t& vco, uint32_t& flash)
{
	hclk = limits.sys_max;
	vco = 0U; // M/N PLL validation uses the selected ValidateVCO rule.
	flash = FlashStep;
	return true;
}

constexpr bool CalculatePLL(const Config& config, uint64_t& numerator, uint64_t& denominator, uint32_t)
{
	const auto& pll = config.PLLCfgr;
	if (numerator < static_cast<uint64_t>(limits.pll_in_min) * pll.PLL_M
		|| numerator > static_cast<uint64_t>(limits.pll_in_max) * pll.PLL_M) return false;
	numerator *= pll.PLL_N;
	if (!ValidateVCO(numerator, pll.PLL_M)) return false;
	denominator = static_cast<uint64_t>(pll.PLL_M) * PLLOutput(config);
	return true;
}

// The M/N search is shared by F4/F7, G0 and G4.
constexpr Config FindPLL(Config result, uint32_t frequency, uint32_t input, PrepareFunction& prepare)
{
	// Preserve M ascending, then the smallest output divisor (low-jitter preference).
	for (uint32_t m = limits.pll_m_min; m <= limits.pll_m_max; ++m) {
		if (input < static_cast<uint64_t>(limits.pll_in_min) * m
			|| input > static_cast<uint64_t>(limits.pll_in_max) * m) continue;
		for (uint32_t out = 2U; out <= 8U; out += PLLOutputStep) {
			const uint64_t target = static_cast<uint64_t>(frequency) * m * out;
			if (target % input != 0U) continue;
			const uint64_t n = target / input;
			if (n < limits.pll_n_min || n > limits.pll_n_max) continue;
			Config candidate = result;
			candidate.PLLCfgr.PLL_M = static_cast<uint8_t>(m);
			candidate.PLLCfgr.PLL_N = static_cast<uint16_t>(n);
			SetPLLOutput(candidate.PLLCfgr, static_cast<uint8_t>(out));
			if (prepare(candidate).error == Error::None) return candidate;
		}
	}
	return result;
}
#endif

} // namespace RCCDefs

// Functional preparation is evaluated by static constexpr in Init<config>().
// The common sequence uses the device rules from the selected family header.
constexpr ClockSystem::Prepared ClockSystem::Prepare(const Config& config)
{
	using namespace RCCDefs;
	Prepared result{};
	result.source = config.ClkSrc;
	uint64_t numerator = 0U, denominator = 1U;
	uint32_t hclk_limit, vco_limit, flash_step;
	if (!PrepareVoltage(config, result, hclk_limit, vco_limit, flash_step)) return result;
	if (!PrepareSource(config, result, numerator)) return result;
	if (result.pll) {
		if (!ValidatePLL(config.PLLCfgr)) { result.error = Error::PLL; return result; }
		if (!CalculatePLL(config, numerator, denominator, vco_limit)) {
			result.error = Error::VCO; return result;
		}
		result.pll_word = EncodePLL(config.PLLCfgr);
	}
	const uint32_t ahb = AHBDivisor(config.BusDiv.AHB_div);
	if (!PrepareBuses(config.BusDiv, result, numerator, denominator, hclk_limit, ahb)) return result;
	PrepareLatency(ahb, result, numerator, denominator, flash_step);
	if (!PrepareTicks(result, numerator, denominator, ahb)) return result;
	PrepareTransition(ahb, result, numerator, denominator);
	return result;
}

constexpr ClockSystem::Config ClockSystem::CalcPLL(uint32_t frequency, PLL_ClockSource source,
												  uint32_t hse_clock,
#if defined(STM32L0)
												  VoltageRange voltage)
#else
												  uint8_t pll_q)
#endif
{
	using namespace RCCDefs;
	Config result{};
	result.ClkSrc = SystemClockSource::PLL;
	result.HSE_Clk = hse_clock;
	result.PLLCfgr.PLL_ClkSrc = source;
#if defined(STM32L0)
	result.Voltage = voltage;
	result.PLLCfgr.PLL_MUL = 0U;
#else
	result.PLLCfgr.PLL_Q = pll_q;
	result.PLLCfgr.PLL_M = 0U;
#endif
	const uint32_t input = source == PLL_ClockSource::HSI ? HSI_Clock
						 : source == PLL_ClockSource::HSE ? hse_clock : 0U;
	if (input == 0U || frequency == 0U || frequency > limits.sys_max) return result;
	result.BusDiv.APB1_div = SelectAPB(frequency, limits.apb1_max);
	if constexpr (limits.apb2_max != 0U)
		result.BusDiv.APB2_div = SelectAPB(frequency, limits.apb2_max);
	return FindPLL(result, frequency, input, Prepare);
}

#endif // RCC_H_
