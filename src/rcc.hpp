#ifndef RCC_H_
#define RCC_H_

#include "system.hpp"

// ---------------------------------------------------------------------------
// Timeout constants
// ---------------------------------------------------------------------------

static constexpr uint32_t PLL_TIMEOUT_MS           =    2U; ///< PLL lock timeout, ms
static constexpr uint32_t HSE_TIMEOUT_MS           =  100U; ///< HSE startup timeout, ms
static constexpr uint32_t HSI_TIMEOUT_MS           =    2U; ///< HSI startup timeout, ms
static constexpr uint32_t LSI_TIMEOUT_MS           =    2U; ///< LSI startup timeout, ms
static constexpr uint32_t CLOCKSWITCH_TIMEOUT_MS   = 5000U; ///< Clock-switch timeout, ms
static constexpr uint32_t OVERDRIVE_TIMEOUT_MS     =  100U; ///< Overdrive enable timeout, ms

// ---------------------------------------------------------------------------
// Per-device frequency limits
// Emit a hard compile error when the target is not recognised so the
// developer gets a clear message instead of a silent wrong value.
// ---------------------------------------------------------------------------

#if   defined(STM32F446xx) || defined(STM32F429xx)
	static constexpr uint32_t SYS_CLK_LIMIT  = 180000000UL;
	static constexpr uint32_t APB1_CLK_LIMIT =  45000000UL;
	static constexpr uint32_t APB2_CLK_LIMIT =  90000000UL;
#elif defined(STM32F405xx) || defined(STM32F407xx)
	static constexpr uint32_t SYS_CLK_LIMIT  = 168000000UL;
	static constexpr uint32_t APB1_CLK_LIMIT =  42000000UL;
	static constexpr uint32_t APB2_CLK_LIMIT =  84000000UL;
#elif defined(STM32F722xx) || defined(STM32F746xx) || defined(STM32F767xx)
	static constexpr uint32_t SYS_CLK_LIMIT  = 216000000UL;
	static constexpr uint32_t APB1_CLK_LIMIT =  54000000UL;
	static constexpr uint32_t APB2_CLK_LIMIT = 108000000UL;
#elif defined(STM32F411xE)
	static constexpr uint32_t SYS_CLK_LIMIT  = 100000000UL;
	static constexpr uint32_t APB1_CLK_LIMIT =  50000000UL;
	static constexpr uint32_t APB2_CLK_LIMIT = 100000000UL;
#else
	#error "rcc.hpp: unsupported STM32 target — please add SYS/APB clock limits for your device."
#endif

// ---------------------------------------------------------------------------
// ClockSystem
// ---------------------------------------------------------------------------

class ClockSystem
{
public:
	// -----------------------------------------------------------------------
	// Enumerations
	// -----------------------------------------------------------------------

	/// System clock source — maps directly to RCC_CFGR_SW field values.
	enum class SystemClockSource : uint32_t
	{
		HSI   = RCC_CFGR_SW_HSI,
		HSE   = RCC_CFGR_SW_HSE,
		PLL_P = RCC_CFGR_SW_PLL,
#if defined(STM32F446xx)
		PLL_R = RCC_CFGR_SW_PLLR,
#endif
	};

	/// AHB prescaler — maps to RCC_CFGR_HPRE field values.
	enum class AHB_Divider : uint32_t
	{
		DIV1   = RCC_CFGR_HPRE_DIV1,
		DIV2   = RCC_CFGR_HPRE_DIV2,
		DIV4   = RCC_CFGR_HPRE_DIV4,
		DIV8   = RCC_CFGR_HPRE_DIV8,
		DIV16  = RCC_CFGR_HPRE_DIV16,
		DIV64  = RCC_CFGR_HPRE_DIV64,
		DIV128 = RCC_CFGR_HPRE_DIV128,
		DIV256 = RCC_CFGR_HPRE_DIV256,
		DIV512 = RCC_CFGR_HPRE_DIV512,
	};

	/// APB1 prescaler — maps to RCC_CFGR_PPRE1 field values.
	enum class APB1_Divider : uint32_t
	{
		DIV1  = RCC_CFGR_PPRE1_DIV1,
		DIV2  = RCC_CFGR_PPRE1_DIV2,
		DIV4  = RCC_CFGR_PPRE1_DIV4,
		DIV8  = RCC_CFGR_PPRE1_DIV8,
		DIV16 = RCC_CFGR_PPRE1_DIV16,
	};

	/// APB2 prescaler — maps to RCC_CFGR_PPRE2 field values.
	enum class APB2_Divider : uint32_t
	{
		DIV1  = RCC_CFGR_PPRE2_DIV1,
		DIV2  = RCC_CFGR_PPRE2_DIV2,
		DIV4  = RCC_CFGR_PPRE2_DIV4,
		DIV8  = RCC_CFGR_PPRE2_DIV8,
		DIV16 = RCC_CFGR_PPRE2_DIV16,
	};

	/// PLL input clock source.
	enum class PLL_ClockSource : uint32_t
	{
		NO  = 0xFFFFFFFFUL,            ///< PLL not used
		HSI = RCC_PLLCFGR_PLLSRC_HSI,
		HSE = RCC_PLLCFGR_PLLSRC_HSE,
	};

	// -----------------------------------------------------------------------
	// Aggregates
	// -----------------------------------------------------------------------

	/// AHB / APB1 / APB2 prescaler bundle.
	struct BusDividers
	{
		AHB_Divider  AHB_div  = AHB_Divider::DIV1;
		APB1_Divider APB1_div = APB1_Divider::DIV1;
		APB2_Divider APB2_div = APB2_Divider::DIV1;
	};

	/**
	 * @brief PLL configuration registers.
	 *
	 * Constraints (validated in Init()):
	 *   M : 2 – 63      (VCO input = Fsrc / M, target 1–2 MHz)
	 *   N : 50 – 432    (VCO output = VCO_in * N, must be 100–432 MHz)
	 *   P : 2, 4, 6, 8
	 *   Q : 2 – 15
	 *   R : 2 – 7       (STM32F446/F767 only)
	 */
	struct PLL_CFGR
	{
		PLL_ClockSource PLL_ClkSrc = PLL_ClockSource::NO;
		uint8_t  PLL_M = 2U;
		uint16_t PLL_N = 2U;
		uint8_t  PLL_P = 2U;
		uint8_t  PLL_Q = 2U;
		uint8_t  PLL_R = 2U;
	};

	// -----------------------------------------------------------------------
	// Public data
	// -----------------------------------------------------------------------

	/**
	 * @brief Frequency of the external oscillator passed to Init().
	 *        Read-only after initialisation — use GetHSEClock().
	 */
	static uint32_t GetHSEClock() noexcept { return HSESrcClk; }

	// -----------------------------------------------------------------------
	// Init — full manual configuration
	// -----------------------------------------------------------------------

	/**
	 * @brief Configure system clocks.
	 *
	 * @param ClkSrc   Final SYSCLK source.
	 * @param HSE_Clk  HSE crystal frequency in Hz (0 if HSE not used).
	 * @param BusDiv   AHB / APB1 / APB2 prescalers.
	 * @param PLLCfgr  PLL parameters (ignored when ClkSrc is HSI or HSE).
	 * @return         InitOK on success, InitError on any failure.
	 */
	static SysInitStatus Init(
		SystemClockSource ClkSrc,
		uint32_t          HSE_Clk,
		BusDividers       BusDiv,
		PLL_CFGR          PLLCfgr);

	/// Overload — HSI with default bus dividers (no PLL, no HSE).
	static inline SysInitStatus Init(
		SystemClockSource ClkSrc  = SystemClockSource::HSI,
		uint32_t          HSE_Clk = 0U)
	{
		return Init(ClkSrc, HSE_Clk, BusDividers{}, PLL_CFGR{});
	}

	/// Convenience overload — no HSE.
	static inline SysInitStatus Init(
		SystemClockSource ClkSrc,
		BusDividers       BusDiv,
		PLL_CFGR          PLLCfgr)
	{
		return Init(ClkSrc, 0U, BusDiv, PLLCfgr);
	}

	// -----------------------------------------------------------------------
	// Init_calc_pll — auto-calculate PLL coefficients
	// -----------------------------------------------------------------------

	/**
	 * @brief Configure clocks targeting @p req_freq, calculating PLL
	 *        coefficients automatically.
	 *
	 * @param req_freq  Desired SYSCLK in Hz.
	 * @param pll_src   PLL input source (HSI or HSE).
	 * @param hse_clk   HSE frequency in Hz (required when pll_src == HSE).
	 * @param pll_q     PLL Q divider for USB/SDIO/RNG (default 2).
	 */
	static SysInitStatus Init_calc_pll(
		uint32_t        req_freq,
		PLL_ClockSource pll_src,
		uint32_t        hse_clk = 0U,
		uint32_t        pll_q   = 2U);

private:
	static uint32_t HSESrcClk; ///< Stored HSE frequency after successful Init()

	// -----------------------------------------------------------------------
	// Internal helpers
	// -----------------------------------------------------------------------

	/// Switch SYSCLK to HSI and wait for confirmation (used before PLL changes).
	static SysInitStatus SwitchToHSI();

	/// Calculate Flash wait-state count for a given SYSCLK frequency.
	static uint32_t CalcFlashLatency(uint32_t sys_clk);
};

#endif // RCC_H_
