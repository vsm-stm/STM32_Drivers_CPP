/**
 * @file    rtc.hpp
 * @brief   RTC driver for STM32G0.
 *
 * Fully static class — no instances are created or needed.
 * All methods are called as RTC_cl::SetUp(...), RTC_cl::EnableWakeUpTimer(...).
 *
 * Interrupt architecture
 * ----------------------
 * The private singleton WutHandler implements IIRQHandler and is registered
 * in IRQ_Registry on the first call to IRQ_en(..., ENABLE).  The user does
 * not need to manually register a handler or touch NVIC — it is all automatic.
 *
 * WutHandler::HandleIRQ():
 *   1. Clears the CWUTF flag in RTC->SCR.
 *   2. Calls the user-supplied callback (if set).
 *
 * Typical usage
 * -------------
 * @code
 *   RTC_cl::SetUp(RTC_cl::CLK_Source::LSE);
 *
 *   RTC_cl::SetTime({14, 25, 00});
 *   RTC_cl::SetDate({22, 05, 26}, RTC_cl::WeekDay::Friday);
 *
 *   // Wake every 2 seconds (ck_spre = 1 Hz, period = counter + 1):
 *   RTC_cl::EnableWakeUpTimer(1, 0b100, []{ ... });
 * @endcode
 */

#ifndef RTC_HPP_
#define RTC_HPP_

#include "system.hpp"
#include "irq_registry.hpp"

class RTC_cl
{
public:
	/** @brief User callback type for the WakeUp Timer interrupt. */
	using WutCallback = void(*)();

	/**
	 * @brief RTC clock source selection (RTCSEL bits in RCC->BDCR).
	 *
	 * LSE (32.768 kHz crystal) — most accurate, requires external crystal.
	 * LSI (~32 kHz RC oscillator) — no external component, less accurate.
	 * HSE/128 — rarely used.
	 */
	enum class CLK_Source {
		NO  = 0b00 << RCC_BDCR_RTCSEL_Pos,
		LSE = 0b01 << RCC_BDCR_RTCSEL_Pos,
		LSI = 0b10 << RCC_BDCR_RTCSEL_Pos,
		HSE = 0b11 << RCC_BDCR_RTCSEL_Pos
	};

	/** @brief Day of week (WDU field in RTC->DR, 1 = Monday .. 7 = Sunday). */
	enum class WeekDay {
		Monday    = 1,
		Tuesday   = 2,
		Wednesday = 3,
		Thursday  = 4,
		Friday    = 5,
		Saturday  = 6,
		Sunday    = 7
	};

	/**
	 * @brief RTC interrupt sources (map directly to WUTIE/ALRAIE/ALRBIE/TSIE bits in CR).
	 *
	 * Values are cast to uint32_t and OR-ed into / cleared from RTC->CR.
	 */
	enum class IRQ_s {
		WUT   = RTC_CR_WUTIE,   ///< WakeUp Timer
		ALR_A = RTC_CR_ALRAIE,  ///< Alarm A
		ALR_B = RTC_CR_ALRBIE,  ///< Alarm B
		TS    = RTC_CR_TSIE     ///< Timestamp
	};

	/** @brief Time in decimal (converted to BCD internally by to_bcd). */
	struct Time { uint8_t hours; uint8_t minutes; uint8_t seconds; };

	/** @brief Date in decimal. Year is two-digit: 26 = 2026. */
	struct Date { uint8_t day; uint8_t month; uint8_t year; };

	// -----------------------------------------------------------------------
	// Initialisation
	// -----------------------------------------------------------------------

	/**
	 * @brief Enables RTC clocking, starts the selected oscillator, and
	 *        programs the prescaler dividers.
	 *
	 * Initialisation is skipped if the RTC is already synchronised
	 * (ICSR::RSF == 0), so SetUp() may be called again after returning
	 * from Stop mode without losing the current time.
	 *
	 * Calendar tick frequency:
	 *   f_ck_spre = f_rtcclk / ((prediv_a + 1) * (prediv_s + 1))
	 * With defaults prediv_a = 127, prediv_s = 255:
	 *   32768 / (128 * 256) = 1 Hz — one calendar tick per second.
	 *
	 * @param clk_src  Clock source (LSE recommended for accuracy).
	 * @param prediv_a Asynchronous prescaler (7-bit, range 0..127).
	 * @param prediv_s Synchronous prescaler  (15-bit, range 0..32767).
	 */
	static SysInitStatus SetUp(CLK_Source clk_src = CLK_Source::LSI,
	                           uint32_t prediv_a  = 127,
	                           uint32_t prediv_s  = 255);

	// -----------------------------------------------------------------------
	// Write protection
	// -----------------------------------------------------------------------

	/**
	 * @brief Locks or unlocks the RTC register write protection.
	 *
	 * Most RTC registers are write-protected against spurious writes.
	 * Unlock sequence: write 0xCA then 0x53 to WPR.
	 * Lock:            write any incorrect key (0xFF).
	 *
	 * Every method in this class removes the protection before modifying
	 * registers and restores it immediately after.
	 */
	inline static void WriteProtection(FunctionalState enable) {
		if (enable) { RTC->WPR = 0xFF; }
		else        { RTC->WPR = 0xCA; RTC->WPR = 0x53; }
	}

	// -----------------------------------------------------------------------
	// Time and date
	// -----------------------------------------------------------------------

	/**
	 * @brief Sets the current time.
	 *
	 * Writing TR requires initialisation mode (ICSR::INIT = 1, wait for INITF).
	 * All fields are converted from decimal to BCD via to_bcd().
	 */
	inline static void SetTime(const Time& t) {
		WriteProtection(DISABLE);
		RTC->ICSR = RTC_ICSR_INIT;
		while (!(RTC->ICSR & RTC_ICSR_INITF));
		RTC->TR = to_bcd(t.hours)   << RTC_TR_HU_Pos  |
		          to_bcd(t.minutes) << RTC_TR_MNU_Pos |
		          to_bcd(t.seconds) << RTC_TR_SU_Pos;
		RTC->ICSR &= ~RTC_ICSR_INIT;
		WriteProtection(ENABLE);
	}

	/**
	 * @brief Sets the current date and day of week.
	 *
	 * Writing DR requires initialisation mode (ICSR::INIT = 1, wait for INITF).
	 * Year is two-digit decimal: pass 26 for 2026.
	 */
	inline static void SetDate(const Date& d, WeekDay wd = WeekDay::Monday) {
		WriteProtection(DISABLE);
		RTC->ICSR = RTC_ICSR_INIT;
		while (!(RTC->ICSR & RTC_ICSR_INITF));
		RTC->DR = to_bcd(d.day)   << RTC_DR_DU_Pos  |
		          to_bcd(d.month) << RTC_DR_MU_Pos  |
		          to_bcd(d.year)  << RTC_DR_YU_Pos  |
		          static_cast<uint32_t>(wd) << RTC_DR_WDU_Pos;
		RTC->ICSR &= ~RTC_ICSR_INIT;
		WriteProtection(ENABLE);
	}

	// -----------------------------------------------------------------------
	// WakeUp Timer
	// -----------------------------------------------------------------------

	/**
	 * @brief Configures and starts the WakeUp Timer and registers a callback.
	 *
	 * Generates a periodic interrupt at the chosen rate.  Automatically enables
	 * the WUT interrupt (WUTIE in CR) via IRQ_en(), which registers in
	 * IRQ_Registry and unmasks NVIC on the first call.
	 *
	 * Clock source selection (clock_div = WUCKSEL[2:0]):
	 *   0b000 = RTCCLK / 2
	 *   0b001 = RTCCLK / 4
	 *   0b010 = RTCCLK / 8
	 *   0b011 = RTCCLK / 16
	 *   0b100 = ck_spre (1 Hz with default prescalers)  <- recommended
	 *   0b101 = ck_spre with 65536 added to WUTR
	 *
	 * Period with clock_div = 0b100:  T = (counter + 1) seconds.
	 *   counter = 0  ->  1 s
	 *   counter = 1  ->  2 s
	 *   counter = 59 -> 60 s
	 *
	 * @param counter   WakeUp counter value written to WUTR (0..65535).
	 * @param clock_div WUT clock source (WUCKSEL, 3 bits).
	 * @param cb        Callback invoked from the interrupt after flag clearing.
	 *                  Pass nullptr to enable the timer without a callback.
	 */
	inline static void EnableWakeUpTimer(uint32_t counter   = 0,
	                                     uint8_t  clock_div = 0b100,
	                                     WutCallback cb     = nullptr) {
		_wut_cb = cb;
		WriteProtection(DISABLE);
		RTC->WUTR = counter;
		RTC->CR  |= clock_div << RTC_CR_WUCKSEL_Pos | RTC_CR_WUTE;
		WriteProtection(ENABLE);
		IRQ_en(IRQ_s::WUT, ENABLE);
	}

	// -----------------------------------------------------------------------
	// Interrupt control
	// -----------------------------------------------------------------------

	/**
	 * @brief Enables or disables an RTC interrupt source.
	 *
	 * On first enable (NVIC not yet active):
	 *   - registers WutHandler in IRQ_Registry
	 *   - unmasks the RTC EXTI line
	 *   - enables NVIC
	 *
	 * On last disable (all sources cleared):
	 *   - unregisters from IRQ_Registry
	 *   - disables NVIC
	 *
	 * Write protection is managed internally.
	 *
	 * @param irq Interrupt source (see IRQ_s).
	 * @param en  ENABLE / DISABLE.
	 */
	static void IRQ_en(IRQ_s irq, FunctionalState en);

	// -----------------------------------------------------------------------
	// Calibration output
	// -----------------------------------------------------------------------

	/**
	 * @brief Enables or disables the calibration clock output on RTC_CALIB pin.
	 *
	 * Useful for measuring oscillator accuracy with an oscilloscope.
	 * Outputs ck_spre (1 Hz) or RTCCLK/64 depending on the COSEL bit.
	 */
	static void EnableCOE(bool en) {
		WriteProtection(DISABLE);
		if (en) RTC->CR |=  RTC_CR_COE;
		else    RTC->CR &= ~RTC_CR_COE;
		WriteProtection(ENABLE);
	}

private:
	// -----------------------------------------------------------------------
	// BCD conversion
	// -----------------------------------------------------------------------

	/**
	 * @brief Converts a decimal value (0..99) to packed BCD without division.
	 *
	 * Trick: (v * 205) >> 11 equals v / 10 exactly for v in [0, 99].
	 * Approximately 4 instructions on Cortex-M0+: MUL, LSR, LSL, SUB+OR.
	 * Result: high nibble = tens digit, low nibble = units digit.
	 */
	inline static uint8_t to_bcd(uint8_t v) {
		if (v > 99) return 0xFF;
		uint8_t t = ((uint16_t)v * 205u) >> 11;
		return (t << 4) | (v - t * 10u);
	}

	// -----------------------------------------------------------------------
	// WakeUp IRQ handler (private singleton)
	// -----------------------------------------------------------------------

	static WutCallback _wut_cb; ///< User callback set by EnableWakeUpTimer.

	/**
	 * @brief Private IIRQHandler for the WakeUp Timer interrupt.
	 *
	 * The single instance (_wut_handler) is registered in IRQ_Registry
	 * automatically when IRQ_en(WUT, ENABLE) is called.
	 *
	 * HandleIRQ sequence:
	 *   1. Clear CWUTF in RTC->SCR to prevent immediate re-entry.
	 *   2. Call the user callback if one was provided.
	 */
	struct WutHandler : IIRQHandler {
		void HandleIRQ() override {
			RTC->SCR |= RTC_SCR_CWUTF;
			EXTI->IMR1 |= EXTI_IMR1_IM19;
			if (RTC_cl::_wut_cb) RTC_cl::_wut_cb();
		}
	};
	static WutHandler _wut_handler;
};

#endif // RTC_HPP_
