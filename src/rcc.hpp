/**
 * @file rcc.hpp
 * @brief Compile-time clock configuration and one hardware application on F4/F7/G0.
 *
 * Config is a C++20 structural value passed to Init<config>(). Prepare() calculates
 * PLL register fields, bus/timer clocks, Flash latency and SysTick reload during
 * compilation. static_assert reports invalid configurations; run-time code waits
 * for hardware ready flags and applies the prepared register values.
 *
 * Initialize System first, while HSI /1 and a serviceable SysTick are available.
 * Configure clocks once before peripheral setup. Reconfiguration and retry after
 * a failed hardware initialization are not supported; reset before another attempt.
 * Device limits assume VDD=3.0 V (also the 3.3 V timing band); see rcc_defs.hpp.
 *
 * Automatic exact nominal SYSCLK example:
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
 */
#ifndef RCC_H_
#define RCC_H_

#include "system.hpp"
#include "rcc_defs.hpp"

// One clock setup per hardware reset, before peripheral setup.
// Config and all derived values are validated by Init<config>() at compile time.
class ClockSystem
{
public:
    /**
     * @brief Encoded RCC SYSCLK selection, not a frequency value.
     * F4/F7 PLL normally selects P; PLL_R exists only on devices exposing it.
     * G0 PLL selects R; LSE/LSI can also supply SYSCLK.
     */
    enum class SystemClockSource : uint32_t {
#if defined(STM32F4) || defined(STM32F7)
        HSI = RCC_CFGR_SW_HSI, HSE = RCC_CFGR_SW_HSE, PLL = RCC_CFGR_SW_PLL,
#ifdef RCC_CFGR_SW_PLLR
        PLL_R = RCC_CFGR_SW_PLLR,
#endif
#elif defined(STM32G0)
        HSI = RCC_CFGR_SW_HSISYS, HSE = RCC_CFGR_SW_HSE, PLL = RCC_CFGR_SW_PLLRCLK,
        LSE = RCC_CFGR_SW_LSE, LSI = RCC_CFGR_SW_LSI,
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
        NO = 0xFFFFFFFFU, HSI = RCC_PLLCFGR_PLLSRC_HSI, HSE = RCC_PLLCFGR_PLLSRC_HSE,
    };

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
     * G0 configures P/Q divisors where present but enables only the R output.
     * These defaults are placeholders, not an automatically chosen board clock.
     */
    struct PLL_CFGR {
        PLL_ClockSource PLL_ClkSrc = PLL_ClockSource::NO;
        uint8_t PLL_M = 2U;
        uint16_t PLL_N = 50U;
        uint8_t PLL_P = 2U;
        uint8_t PLL_Q = 2U;
        uint8_t PLL_R = 2U;
    };

    /**
     * @brief Complete nominal clock request evaluated by Init<config>().
     * HSE_Clk is in Hz and used only if HSE supplies SYSCLK or the active PLL.
     * PLLCfgr is ignored for direct HSI/HSE/LSE/LSI clocking.
     */
    struct Config {
        SystemClockSource ClkSrc = SystemClockSource::HSI;
        uint32_t HSE_Clk = 0U; // Nominal Hz; read only if HSE actually supplies SYSCLK/PLL.
        BusDividers BusDiv{};
        PLL_CFGR PLLCfgr{};    // Ignored unless ClkSrc selects a PLL output.
    };

    /**
     * @brief Search for an exact nominal SYSCLK configuration without rounding a target.
     * @param frequency Requested SYSCLK in Hz; AHB remains /1.
     * @param source HSI or HSE; hse_clock must describe the board when HSE is used.
     * @param hse_clock Nominal external oscillator frequency in Hz, otherwise zero.
     * @param pll_q Numeric Q divider, subject to device validation.
     * @return Config using the fastest valid APB clocks; PLL_M=0 if no solution.
     *
     * Search M ascending to maximize the PLL comparison frequency, then prefer
     * the smallest P/R output divisor. The choice is deterministic and preserves
     * the intended low-jitter preference. It does not measure oscillator error
     * or guarantee a 48 MHz USB/Q output. If no exact solution exists, provide
     * a manual Config with deliberate coefficients; Init diagnoses an invalid one.
     * Use a constexpr result with Init<config>() to keep calculation out of firmware.
     */
    static constexpr Config CalcPLL(uint32_t frequency, PLL_ClockSource source,
                                    uint32_t hse_clock = 0U, uint8_t pll_q = 2U);

    /**
     * @brief Validate a constant Config and apply it once to hardware.
     * @tparam config Automatic or manual C++20 constant configuration.
     * @return InitOK after switching and publishing clocks; InitError for a
     *         repeated attempt or hardware-ready timeout. Configuration errors
     *         are compilation errors, not a run-time InitError.
     * @pre Successful System::Init(), HSI /1, working SysTick; call before drivers.
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
    enum class Error : uint8_t { None, Source, HSE, PLL, VCO, Divider, Frequency, Tick };
    /**
     * @brief Compiler-produced register words and derived clock/tick values.
     * Rational frequencies are checked before integer division; reported integer
     * clocks/reload use truncation. Apply() consumes this result without rerunning
     * the configuration search. hsi_* values keep timeout ticks usable while
     * applying bus dividers before the final SYSCLK switch.
     */
    struct Prepared {
        Error error = Error::None;
        SystemClockSource source = SystemClockSource::HSI;
        uint32_t hse = 0U, pll_word = 0U, bus_word = 0U;
        uint32_t hclk = 0U, apb1 = 0U, apb2 = 0U, tim1 = 0U, tim2 = 0U;
        uint32_t ticks = 0U, hsi_hclk = 0U, hsi_ticks = 0U, latency = 0U;
        bool pll = false, overdrive = false;
    };
    static uint32_t HSESrcClk;
    static SysInitStatus Apply(const Prepared& config);
    static constexpr Prepared Prepare(const Config& config);
    static constexpr uint32_t AHBDivisor(AHB_Divider divider);
    static constexpr uint32_t APBDivisor(APB_Divider divider);
    static constexpr APB_Divider SelectAPB(uint32_t hclk, uint32_t limit);
};

constexpr uint32_t ClockSystem::AHBDivisor(AHB_Divider divider)
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

constexpr uint32_t ClockSystem::APBDivisor(APB_Divider divider)
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

constexpr ClockSystem::APB_Divider ClockSystem::SelectAPB(uint32_t hclk, uint32_t limit)
{
    constexpr APB_Divider dividers[] = {APB_Divider::DIV1, APB_Divider::DIV2, APB_Divider::DIV4,
                                      APB_Divider::DIV8, APB_Divider::DIV16};
    for (APB_Divider divider : dividers)
        if (static_cast<uint64_t>(hclk) <= static_cast<uint64_t>(limit) * APBDivisor(divider))
            return divider;
    return static_cast<APB_Divider>(0xFFFFFFFFU);
}

// Validate source-specific coefficients and limits before constructing register
// words. This is constexpr implementation shared by manual Init and CalcPLL.
constexpr ClockSystem::Prepared ClockSystem::Prepare(const Config& config)
{
    using namespace RCCDefs;
    Prepared result{};
    result.source = config.ClkSrc;
    uint64_t numerator = 0U, denominator = 1U;
    switch (config.ClkSrc) {
    case SystemClockSource::HSI: numerator = HSI_Clock; break;
    case SystemClockSource::HSE: numerator = config.HSE_Clk; result.hse = config.HSE_Clk; break;
    case SystemClockSource::PLL:
#ifdef RCC_CFGR_SW_PLLR
    case SystemClockSource::PLL_R:
#endif
        result.pll = true;
        break;
#if defined(STM32G0)
    case SystemClockSource::LSI: numerator = LSI_Clock; break;
    case SystemClockSource::LSE: numerator = LSE_HZ; break;
#endif
    default: result.error = Error::Source; return result;
    }

    const PLL_CFGR& pll = config.PLLCfgr;
    if (result.pll) {
        if (pll.PLL_ClkSrc == PLL_ClockSource::HSI) numerator = HSI_Clock;
        else if (pll.PLL_ClkSrc == PLL_ClockSource::HSE) numerator = result.hse = config.HSE_Clk;
        else { result.error = Error::Source; return result; }
    }
    if ((config.ClkSrc == SystemClockSource::HSE || result.hse != 0U
         || (result.pll && pll.PLL_ClkSrc == PLL_ClockSource::HSE))
        && (config.HSE_Clk < HSE_MIN || config.HSE_Clk > HSE_MAX)) {
        result.error = Error::HSE; return result;
    }

    if (result.pll) {
        if (pll.PLL_M < PLL_M_MIN || pll.PLL_M > PLL_M_MAX
            || pll.PLL_N < PLL_N_MIN || pll.PLL_N > PLL_N_MAX) {
            result.error = Error::PLL; return result;
        }
#if defined(STM32G0)
        if (pll.PLL_R < 2U || pll.PLL_R > 8U || pll.PLL_P < 2U || pll.PLL_P > 32U
#ifdef RCC_PLLQ_SUPPORT
            || pll.PLL_Q < 2U || pll.PLL_Q > 8U
#endif
            ) { result.error = Error::PLL; return result; }
        const uint32_t output_divider = pll.PLL_R;
        result.pll_word = static_cast<uint32_t>(pll.PLL_ClkSrc)
            | ((pll.PLL_M - 1U) << RCC_PLLCFGR_PLLM_Pos)
            | (static_cast<uint32_t>(pll.PLL_N) << RCC_PLLCFGR_PLLN_Pos)
            | ((pll.PLL_P - 1U) << RCC_PLLCFGR_PLLP_Pos)
            | ((pll.PLL_R - 1U) << RCC_PLLCFGR_PLLR_Pos) | RCC_PLLCFGR_PLLREN;
#ifdef RCC_PLLQ_SUPPORT
        result.pll_word |= (pll.PLL_Q - 1U) << RCC_PLLCFGR_PLLQ_Pos;
#endif
#else
        if ((pll.PLL_P != 2U && pll.PLL_P != 4U && pll.PLL_P != 6U && pll.PLL_P != 8U)
            || pll.PLL_Q < 2U || pll.PLL_Q > 15U
#ifdef RCC_PLLCFGR_PLLR_Msk
            || pll.PLL_R < 2U || pll.PLL_R > 7U
#endif
            ) { result.error = Error::PLL; return result; }
        uint32_t output_divider = pll.PLL_P;
#ifdef RCC_CFGR_SW_PLLR
        if (config.ClkSrc == SystemClockSource::PLL_R) output_divider = pll.PLL_R;
#endif
        result.pll_word = static_cast<uint32_t>(pll.PLL_ClkSrc)
            | (static_cast<uint32_t>(pll.PLL_M) << RCC_PLLCFGR_PLLM_Pos)
            | (static_cast<uint32_t>(pll.PLL_N) << RCC_PLLCFGR_PLLN_Pos)
            | ((pll.PLL_P / 2U - 1U) << RCC_PLLCFGR_PLLP_Pos)
            | (static_cast<uint32_t>(pll.PLL_Q) << RCC_PLLCFGR_PLLQ_Pos);
#ifdef RCC_PLLCFGR_PLLR_Msk
        result.pll_word |= static_cast<uint32_t>(pll.PLL_R) << RCC_PLLCFGR_PLLR_Pos;
#endif
#endif
        // Compare rational frequencies before performing any integer division.
        if (numerator < static_cast<uint64_t>(PLL_IN_MIN) * pll.PLL_M
            || numerator > static_cast<uint64_t>(PLL_IN_MAX) * pll.PLL_M) {
            result.error = Error::VCO; return result;
        }
        numerator *= pll.PLL_N;
        if (numerator < static_cast<uint64_t>(VCO_MIN) * pll.PLL_M
            || numerator > static_cast<uint64_t>(VCO_MAX) * pll.PLL_M) {
            result.error = Error::VCO; return result;
        }
        denominator = static_cast<uint64_t>(pll.PLL_M) * output_divider;
    }
    const uint32_t ahb = AHBDivisor(config.BusDiv.AHB_div);
    const uint32_t apb1 = APBDivisor(config.BusDiv.APB1_div);
#if defined(STM32G0)
    const uint32_t apb2 = 1U;
#else
    const uint32_t apb2 = APBDivisor(config.BusDiv.APB2_div);
#endif
    if (ahb == 0U || apb1 == 0U || apb2 == 0U) {
        result.error = Error::Divider; return result;
    }
    if (numerator == 0U || (result.pll && numerator < static_cast<uint64_t>(PLL_SYS_MIN) * denominator)
        || numerator > static_cast<uint64_t>(SYS_MAX) * denominator
        || numerator > static_cast<uint64_t>(SYS_MAX) * denominator * ahb
        || numerator > static_cast<uint64_t>(APB1_MAX) * denominator * ahb * apb1
#if !defined(STM32G0)
        || numerator > static_cast<uint64_t>(APB2_MAX) * denominator * ahb * apb2
#endif
        ) { result.error = Error::Frequency; return result; }

    denominator *= ahb;
    result.hclk = static_cast<uint32_t>(numerator / denominator);
    result.apb1 = static_cast<uint32_t>(numerator / (denominator * apb1));
    result.tim1 = static_cast<uint32_t>((numerator * (apb1 == 1U ? 1U : 2U)) / (denominator * apb1));
#if !defined(STM32G0)
    result.apb2 = static_cast<uint32_t>(numerator / (denominator * apb2));
    result.tim2 = static_cast<uint32_t>((numerator * (apb2 == 1U ? 1U : 2U)) / (denominator * apb2));
#endif
    result.latency = static_cast<uint32_t>((numerator - 1U) / (denominator * FLASH_STEP));
    result.overdrive = numerator > static_cast<uint64_t>(OVERDRIVE_LIMIT) * denominator;
    result.ticks = static_cast<uint32_t>(numerator / (denominator * TICK_BASE));
    result.hsi_hclk = HSI_Clock / ahb;
    result.hsi_ticks = result.hsi_hclk / TICK_BASE;
    if (result.ticks == 0U || result.ticks > (SysTick_LOAD_RELOAD_Msk + 1U)) {
        result.error = Error::Tick; return result;
    }
    result.bus_word = static_cast<uint32_t>(config.BusDiv.AHB_div) << RCC_CFGR_HPRE_Pos;
#if defined(STM32G0)
    result.bus_word |= static_cast<uint32_t>(config.BusDiv.APB1_div) << RCC_CFGR_PPRE_Pos;
#else
    result.bus_word |= (static_cast<uint32_t>(config.BusDiv.APB1_div) << RCC_CFGR_PPRE1_Pos)
                    | (static_cast<uint32_t>(config.BusDiv.APB2_div) << RCC_CFGR_PPRE2_Pos);
#endif
    return result;
}

constexpr ClockSystem::Config ClockSystem::CalcPLL(uint32_t frequency, PLL_ClockSource source,
                                                  uint32_t hse_clock, uint8_t pll_q)
{
    using namespace RCCDefs;
    Config result{};
    result.ClkSrc = SystemClockSource::PLL;
    result.HSE_Clk = hse_clock;
    result.PLLCfgr.PLL_ClkSrc = source;
    result.PLLCfgr.PLL_Q = pll_q;
    result.PLLCfgr.PLL_M = 0U;
    const uint32_t input = source == PLL_ClockSource::HSI ? HSI_Clock
                         : source == PLL_ClockSource::HSE ? hse_clock : 0U;
    if (input == 0U || frequency == 0U || frequency > SYS_MAX) return result;
    result.BusDiv.APB1_div = SelectAPB(frequency, APB1_MAX);
#if !defined(STM32G0)
    result.BusDiv.APB2_div = SelectAPB(frequency, APB2_MAX);
#endif
    // M ascending maximizes the PLL comparison frequency, as recommended by ST.
    // For the same M retain the original preference for the smallest output divisor.
    for (uint32_t m = PLL_M_MIN; m <= PLL_M_MAX; ++m) {
        if (input < static_cast<uint64_t>(PLL_IN_MIN) * m
            || input > static_cast<uint64_t>(PLL_IN_MAX) * m) continue;
#if defined(STM32G0)
        constexpr uint32_t step = 1U;
#else
        constexpr uint32_t step = 2U;
#endif
        for (uint32_t out = 2U; out <= 8U; out += step) {
            const uint64_t target = static_cast<uint64_t>(frequency) * m * out;
            if (target % input != 0U) continue;
            const uint64_t n = target / input;
            if (n < PLL_N_MIN || n > PLL_N_MAX) continue;
            Config candidate = result;
            candidate.PLLCfgr.PLL_M = static_cast<uint8_t>(m);
            candidate.PLLCfgr.PLL_N = static_cast<uint16_t>(n);
#if defined(STM32G0)
            candidate.PLLCfgr.PLL_R = static_cast<uint8_t>(out);
#else
            candidate.PLLCfgr.PLL_P = static_cast<uint8_t>(out);
#endif
            if (Prepare(candidate).error == Error::None) return candidate;
        }
    }
    return result;
}

#endif // RCC_H_
