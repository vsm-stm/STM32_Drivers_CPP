#ifndef RCC_H_
#define RCC_H_

#include "system.hpp"

// ---------------------------------------------------------------------------
// ClockSystem
// ---------------------------------------------------------------------------

class ClockSystem
{
public:

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

	#if defined(STM32F4) || defined(STM32F7)

		static constexpr uint32_t PLL_CLK_IN_MIN = 1*MHz;  ///< Minimum PLL input clock frequency
		static constexpr uint32_t PLL_CLK_IN_MAX = 2*MHz; ///< Maximum PLL

		static constexpr uint32_t PLL_N_CLK_MIN = 100*MHz;  ///< Minimum PLL output clock frequency (VCO_in * N)
		static constexpr uint32_t PLL_N_CLK_MAX = 432*MHz; ///< Maximum PLL output clock frequency (VCO_in * N)

		static constexpr uint32_t PPRE_BUS_1_Msk = RCC_CFGR_PPRE1_Msk;
		static constexpr uint32_t PPRE_BUS_2_Msk = RCC_CFGR_PPRE2_Msk;
		static constexpr uint32_t PPRE_BUS_1_Pos = RCC_CFGR_PPRE1_Pos;
		static constexpr uint32_t PPRE_BUS_2_Pos = RCC_CFGR_PPRE2_Pos;

		static constexpr uint32_t LATENCY_DIV = 30*MHz; ///< Flash latency divider — 1 wait state per LATENCY_DIV MHz

		#if   defined(STM32F446xx) || defined(STM32F429xx)
			static constexpr uint32_t SYS_CLK_LIMIT  = 180*MHz;
			static constexpr uint32_t APB1_CLK_LIMIT =  45*MHz;
			static constexpr uint32_t APB2_CLK_LIMIT =  90*MHz;
		#elif defined(STM32F405xx) || defined(STM32F407xx)
			static constexpr uint32_t SYS_CLK_LIMIT  = 168*MHz;
			static constexpr uint32_t APB1_CLK_LIMIT =  42*MHz;
			static constexpr uint32_t APB2_CLK_LIMIT =  84*MHz;
		#elif defined(STM32F722xx) || defined(STM32F746xx) || defined(STM32F767xx)
			static constexpr uint32_t SYS_CLK_LIMIT  = 216*MHz;
			static constexpr uint32_t APB1_CLK_LIMIT =  54*MHz;
			static constexpr uint32_t APB2_CLK_LIMIT = 108*MHz;
		#elif defined(STM32F411xE)
			static constexpr uint32_t SYS_CLK_LIMIT  = 100*MHz;
			static constexpr uint32_t APB1_CLK_LIMIT =  50*MHz;
			static constexpr uint32_t APB2_CLK_LIMIT = 100*MHz;
		#endif
	
	#elif defined(STM32G0)
		static constexpr uint32_t SYS_CLK_LIMIT  = 64*MHz;
		static constexpr uint32_t APB1_CLK_LIMIT = 64*MHz;
		static constexpr uint32_t APB2_CLK_LIMIT = 0*MHz;

		static constexpr uint32_t PLL_CLK_IN_MIN = 2.66*MHz;  ///< Minimum PLL input clock frequency
		static constexpr uint32_t PLL_CLK_IN_MAX = 16*MHz; ///< Maximum PLL input clock frequency

		static constexpr uint32_t PLL_N_CLK_MIN = 64*MHz;  ///< Minimum PLL output clock frequency (VCO_in * N)
		static constexpr uint32_t PLL_N_CLK_MAX = 344*MHz; ///< Maximum PLL output clock frequency (VCO_in * N)

		static constexpr uint32_t PPRE_BUS_1_Msk = RCC_CFGR_PPRE_Msk;
		static constexpr uint32_t PPRE_BUS_2_Msk = 0;
		static constexpr uint32_t PPRE_BUS_1_Pos = RCC_CFGR_PPRE_Pos;
		static constexpr uint32_t PPRE_BUS_2_Pos = 0xFFFFFFFFU;

		static constexpr uint32_t LATENCY_DIV = 24*MHz; ///< Flash latency divider — 1 wait state per LATENCY_DIV MHz

	#else
		#error "rcc.hpp: unsupported STM32 target — please add SYS/APB clock limits for your device."
	#endif


	// -----------------------------------------------------------------------
	// Enumerations
	// -----------------------------------------------------------------------

	/// System clock source — maps directly to RCC_CFGR_SW field values.
	enum class SystemClockSource : uint32_t
	{
	#if defined(STM32F4) || defined(STM32F7)
		HSI = RCC_CFGR_SW_HSI,
		HSE = RCC_CFGR_SW_HSE,
		PLL = RCC_CFGR_SW_PLL,
	#if defined(STM32F446xx)
		PLL_R = RCC_CFGR_SW_PLLR,
	#endif

	#elif defined(STM32G0)
		HSI = RCC_CFGR_SW_HSISYS,
		HSE = RCC_CFGR_SW_HSE,
		PLL = RCC_CFGR_SW_PLLRCLK,
		LSE = RCC_CFGR_SW_LSE,
		LSI = RCC_CFGR_SW_LSI,
	#endif
	};

	/// AHB prescaler — maps to RCC_CFGR_HPRE field values.
	enum class AHB_Divider : uint32_t
	{
		DIV1	= 0b0000,
		DIV2	= 0b1000,
		DIV4	= 0b1001,
		DIV8	= 0b1010,
		DIV16	= 0b1011,
		DIV64	= 0b1100,
		DIV128	= 0b1101,
		DIV256	= 0b1110,
		DIV512	= 0b1111,
	};

	/// APB1 prescaler — maps to RCC_CFGR_PPRE1 field values.
	enum class APB_Divider : uint32_t
	{
		DIV1	= 0b000,
		DIV2	= 0b100,
		DIV4	= 0b101,
		DIV8	= 0b110,
		DIV16	= 0b111,
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
		APB_Divider  APB1_div = APB_Divider::DIV1;
		APB_Divider  APB2_div = APB_Divider::DIV1;
	};

	/**
	 * @brief PLL configuration registers.
	 *
	 * Constraints (validated in Init()): 
	 *   M : 2 – 63      (VCO input = Fsrc / M, target 1–2 MHz) 
	 *   N : 50 – 432    (VCO output = VCO_in * N, must be 100–432 MHz) 
	 *   P : 2, 4, 6, 8 
	 *   Q : 2 – 15
	 *   R : 2 – 7       (STM32F446/F767 only, and main for STM32G0)
	 */
	struct PLL_CFGR
	{
		PLL_ClockSource PLL_ClkSrc = PLL_ClockSource::NO;
		uint8_t  PLL_M = 2U;
		uint16_t PLL_N = 50U;
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
	// InitCalcPLL — auto-calculate PLL coefficients
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
	static SysInitStatus InitCalcPLL(
		uint32_t        req_freq,
		PLL_ClockSource pll_src,
		uint32_t        hse_clk = 0U,
		uint8_t         pll_q   = 2U);

private:
	static uint32_t HSESrcClk; ///< Stored HSE frequency after successful Init()

	struct validate_out{
		SysInitStatus status;
		uint32_t clk_value;
	};
	
	static uint32_t AHB_Pre;
	static uint32_t APB1_Pre;
	static uint32_t APB2_Pre;
	
	static SysInitStatus EnableHSE(uint32_t HSE_Clk);
	static validate_out ValidatePLLCfgr(PLL_CFGR pllcfgr, bool f446xx_pllr_out = false);
	static SysInitStatus ValidateBusDividers(uint32_t sys_clk, BusDividers div);
	static void ConfigurePLL(PLL_CFGR pllcfgr);

};

#endif // RCC_H_
