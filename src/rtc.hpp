/**
 * @file rtc.hpp
 * @brief Calendar RTC for STM32F4/F7/G0, callbacks through inheritance.
 *
 * One object owns RTC after SetUp. Repeat setup is an error until DeInit.
 * Configuration runs in main with a working SysTick. IRQ callbacks must not
 * wait or change the registry. Object lifetime covers all its registrations.
 * On F4/F7, keep all RTC vectors at the same preemption priority.
 * DeInit releases events/IRQs but keeps the calendar running.
 *
 * Example (after System/RCC initialization):
 *   class Clock : public RTC_cl {
 *   protected:
 *       void OnWakeup() override {} // Notify main here.
 *       void OnTimestamp(const Timestamp& stamp) override {} // Copy if needed.
 *   };
 *   static Clock clock;
 *   clock.SetUp(RTC_cl::CLK_Source::LSE, 127, 255);
 *   clock.SetTime({14, 25, 0});
 *   clock.SetDate({22, 5, 26}, RTC_cl::WeekDay::Friday);
 *   clock.EnableWakeUpTimer(1); // Every two seconds with 1 Hz calendar.
 */
#ifndef RTC_HPP_
#define RTC_HPP_

#include "system.hpp"
#include "irq_registry.hpp"

#if !defined(STM32F4) && !defined(STM32F7) && !defined(STM32G0)
#error "RTC supports STM32F4/F7/G0 only"
#endif

class RTC_cl : public IIRQHandler
{
public:
	/// LSE/LSI are started by SetUp. HSE and its RTC divider are caller prepared.
	enum class CLK_Source {
		NO = 0, LSE = 1u << RCC_BDCR_RTCSEL_Pos,
		LSI = 2u << RCC_BDCR_RTCSEL_Pos, HSE = 3u << RCC_BDCR_RTCSEL_Pos
	};
	enum class WeekDay { Monday = 1, Tuesday, Wednesday, Thursday, Friday, Saturday, Sunday };
	enum class IRQ_s { WUT = RTC_CR_WUTIE, ALR_A = RTC_CR_ALRAIE,
		ALR_B = RTC_CR_ALRBIE, TS = RTC_CR_TSIE };
	enum class CalOut { Out_512 = 0, Out_1Hz = RTC_CR_COSEL };
	enum class TimestampEdge { Rising = 0, Falling = RTC_CR_TSEDGE };

	struct Time { uint8_t hours, minutes, seconds; }; ///< Decimal, 24-hour format.
	struct Date { uint8_t day, month, year; }; ///< Decimal, years 2000..2099.
	struct RtcTime {
		uint8_t su, st, mnu, mnt, hu, ht, pad[2];
		uint8_t sec() const { return st * 10 + su; }
		uint8_t min() const { return mnt * 10 + mnu; }
		uint8_t hour() const { return ht * 10 + hu; }
	};
	struct RtcDate {
		uint8_t du, dt, mu, mt, yu, yt, wdu, pad;
		uint8_t day() const { return dt * 10 + du; }
		uint8_t month() const { return mt * 10 + mu; }
		uint8_t year() const { return yt * 10 + yu; }
	};
	struct Timestamp {
		RtcTime time;
		RtcDate date; ///< Timestamp hardware has no year: year() returns zero.
		uint32_t sub_seconds; ///< Downcounter value, not milliseconds.
		bool overflow; ///< Another edge arrived before the previous capture was consumed.
	};
	enum class Alarm_Masks { Care = 0, Ignore = 1 };
	struct AlarmField { uint8_t val = 0; Alarm_Masks mask = Alarm_Masks::Ignore; };
	/**
	 * Day is day-of-month. Fields with Ignore are not compared.
	 * MASKSS=0 ignores subseconds, 1..15 compares that number of low bits,
	 * 15 compares all 15 subsecond bits.
	 * Example: AlarmConfig alarm{.hour={14, Alarm_Masks::Care},
	 *     .min={25, Alarm_Masks::Care}, .sec={0, Alarm_Masks::Care}};
	 * clock.EnableAlarm_A(ENABLE, alarm); // Daily at 14:25:00.
	 */
	struct AlarmConfig {
		AlarmField day, hour, min, sec;
		uint32_t sub_sec_msk = 0;
		uint32_t sub_sec = 0;
	};
	static const AlarmConfig kNoAlarm;

	RTC_cl() = default;
	RTC_cl(const RTC_cl&) = delete;
	RTC_cl& operator=(const RTC_cl&) = delete;
	RTC_cl(RTC_cl&&) = delete;
	RTC_cl& operator=(RTC_cl&&) = delete;

	/**
	 * Same retained source preserves time and dividers; shadows are synchronized.
	 * Changing an already selected source resets the backup domain, losing time,
	 * backup registers and other backup-domain configuration. NO is rejected.
	 * LSE wait: 5000 ms; all other waits: 1000 ms, using System::WaitBits.
	 * Calendar frequency = RTCCLK / ((prediv_a+1)*(prediv_s+1)).
	 * Defaults 127/255 suit LSE 32768 Hz. For nominal LSI 32000 Hz use 127/249.
	 * LSI tolerance remains: measure/calibrate it when clock accuracy matters.
	 */
	SysInitStatus SetUp(CLK_Source source = CLK_Source::LSI,
		uint32_t prediv_a = 127, uint32_t prediv_s = 255);
	/// Main only: stop owned events/IRQs; keep calendar/source/backup data.
	void DeInit();

	/** Decimal setters, main only. Invalid fields leave calendar untouched.
	 * Returns NotInit, Error, Timeout or OK. Setters enter INIT then sync shadows.
	 */
	SysStatus SetTime(const Time& time);
	SysStatus SetDate(const Date& date, WeekDay weekday = WeekDay::Monday);
	/// Compatibility input: packed BCD, e.g. hours=0x14 means 14:00.
	SysStatus SetTime_hex(const Time& time);
	SysStatus SetDate_hex(const Date& date, WeekDay weekday = WeekDay::Monday);
	/// Coherent snapshot: TR then DR, releasing the shadow lock.
	void GetDateTime(RtcTime& time, RtcDate& date) const;
	/// Independent snapshots; GetTime also reads DR to release the shadow lock.
	RtcTime GetTime() const;
	RtcDate GetDate() const;

	/**
	 * Arm WakeUp + IRQ. counter 0..65535, normally period=counter+1 ticks.
	 * WUCKSEL: 000=/16, 001=/8, 010=/4, 011=/2, 100=ck_spre,
	 * 110=ck_spre with 65536 added to counter. Codes 101/111 are rejected.
	 * With a 1 Hz calendar, EnableWakeUpTimer(1, 0b100) gives two seconds.
	 * Rearm disables WUTE/WUTIE, waits WUTWF, clears stale WUTF, then arms.
	 */
	SysStatus EnableWakeUpTimer(uint32_t counter = 0, uint8_t clock_div = 0b100);
	SysStatus DisableWakeUpTimer(); ///< Stop without waiting; main only.
	/// Arm/disarm alarm, clear stale flag, bound the write-ready wait.
	SysStatus EnableAlarm_A(FunctionalState enable, const AlarmConfig& config = kNoAlarm);
	SysStatus EnableAlarm_B(FunctionalState enable, const AlarmConfig& config = kNoAlarm);

	/** Timestamp on currently selected RTC_TS input (normally PC13).
	 * Application prepares physical input and any alternate TS routing.
	 * IRQ enabled automatically; IRQ_en(TS, DISABLE) selects polling instead.
	 * Captured values are read before clearing TSF; OnTimestamp receives the
	 * snapshot after acknowledgement, so callbacks cannot read TSTR/TSDR again.
	 */
	SysStatus EnableTimestamp(FunctionalState enable, TimestampEdge edge = TimestampEdge::Rising);
	/// Poll capture: Busy if no TSF; read before clearing TSF/TSOVF.
	SysStatus GetTimestamp(Timestamp& stamp);

	/**
	 * Main only: register this object on first enable, retain until DeInit.
	 * Disable masks only this source. Returns InitError if not initialized,
	 * source invalid or registry full. RTC manages its own internal EXTI routes;
	 * these do not use the external GPIO EXTI_Line driver.
	 */
	SysInitStatus IRQ_en(IRQ_s source, FunctionalState enable);
	void HandleIRQ() override final;

	/**
	 * Smooth calibration, 32-second window: calm 0..511 suppressed pulses;
	 * calp adds 512 pulses. Step about 0.954 ppm at 32768 Hz.
	 * Busy immediately while RECALPF=1, without waiting.
	 * Example: SetCalibration(6) slows nominal LSE by about 5.72 ppm.
	 */
	SysStatus SetCalibration(uint16_t calm = 0, bool calp = false);
	/// Output RTCCLK/64 or ck_spre on RTC_CALIB; caller prepares pin routing.
	SysStatus EnableCOE(bool enable, CalOut output = CalOut::Out_512);

	/// WPR keys; all configuration methods unlock/relock internally.
	static void WriteProtection(FunctionalState enable) {
		if (enable) RTC->WPR = 0xFF;
		else { RTC->WPR = 0xCA; RTC->WPR = 0x53; }
	}
	/// Decimal 0..99 to packed BCD; 0xFF for out-of-range input.
	static uint8_t to_bcd(uint8_t value) {
		if (value > 99) return 0xFF;
		const uint8_t tens = (static_cast<uint16_t>(value) * 205u) >> 11;
		return (tens << 4) | (value - tens * 10u);
	}

protected:
	/// IRQ notifications: no waits, registry changes or configuration here.
	virtual void OnWakeup() {}
	virtual void OnAlarmA() {}
	virtual void OnAlarmB() {}
	virtual void OnTimestamp(const Timestamp&) {}

private:
	static RTC_cl* _owner;
	uint8_t _registered = 0; ///< Own registrations, independent of NVIC state.
	bool EnterInit(); ///< Unlock WPR, enter INIT; on failure exit INIT and relock.
	bool ExitInit(); ///< Exit INIT, sync shadows, lock WPR.
	static bool Sync();
	static void ClearFlags(uint32_t flags); ///< ALRA/B/WUT/TS/TSOV normalized bits 0..4.
	static Timestamp ReadTimestamp(uint32_t flags);
	static RtcTime DecodeTime(uint32_t value);
	static RtcDate DecodeDate(uint32_t value);
	SysStatus Alarm(bool alarm_b, FunctionalState enable, const AlarmConfig& config);
};
#endif // RTC_HPP_
