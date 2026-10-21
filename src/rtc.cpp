#include "rtc.hpp"

const RTC_cl::AlarmConfig RTC_cl::kNoAlarm{};
RTC_cl* RTC_cl::_owner = nullptr;

namespace {
constexpr uint32_t timeout_ms = 1000;
constexpr uint32_t all_irqs = RTC_CR_ALRAIE | RTC_CR_ALRBIE | RTC_CR_WUTIE | RTC_CR_TSIE;
#if defined(STM32G0)
#define RTC_STATE RTC->ICSR
constexpr uint32_t init_bit = RTC_ICSR_INIT, init_ready = RTC_ICSR_INITF;
constexpr uint32_t synced = RTC_ICSR_RSF, initialized = RTC_ICSR_INITS;
constexpr uint32_t alarm_a_ready = RTC_ICSR_ALRAWF, alarm_b_ready = RTC_ICSR_ALRBWF;
constexpr uint32_t wake_ready = RTC_ICSR_WUTWF, recal_pending = RTC_ICSR_RECALPF;
constexpr IRQn_Type vectors[] = {RTC_TAMP_IRQn};
// G0 ICSR has no event flags: those use SR/SCR instead.
constexpr uint32_t state_w0c = RTC_ICSR_RSF;
#else
#define RTC_STATE RTC->ISR
constexpr uint32_t init_bit = RTC_ISR_INIT, init_ready = RTC_ISR_INITF;
constexpr uint32_t synced = RTC_ISR_RSF, initialized = RTC_ISR_INITS;
constexpr uint32_t alarm_a_ready = RTC_ISR_ALRAWF, alarm_b_ready = RTC_ISR_ALRBWF;
constexpr uint32_t wake_ready = RTC_ISR_WUTWF, recal_pending = RTC_ISR_RECALPF;
constexpr IRQn_Type vectors[] = {RTC_Alarm_IRQn, RTC_WKUP_IRQn, TAMP_STAMP_IRQn};
// Write ones to other rc_w0 flags, including TAMP flags, to preserve events
// arriving after the status read. Only INIT is an ordinary control bit here.
constexpr uint32_t state_w0c = RTC_ISR_RSF | RTC_ISR_ALRAF | RTC_ISR_ALRBF
    | RTC_ISR_WUTF | RTC_ISR_TSF | RTC_ISR_TSOVF | RTC_ISR_TAMP1F
#ifdef RTC_ISR_TAMP2F
    | RTC_ISR_TAMP2F
#endif
#ifdef RTC_ISR_TAMP3F
    | RTC_ISR_TAMP3F
#endif
#ifdef RTC_ISR_ITSF
    | RTC_ISR_ITSF
#endif
    ;
#endif

uint32_t event_flags()
{
#if defined(STM32G0)
    return RTC->SR;
#else
    return (RTC->ISR >> RTC_ISR_ALRAF_Pos) & 0x1Fu;
#endif
}

uint8_t from_bcd(uint8_t value)
{
    if ((value & 15u) > 9 || (value >> 4) > 9) return 0xFF;
    return (value >> 4) * 10u + (value & 15u);
}

bool valid_alarm_field(const RTC_cl::AlarmField& field, uint8_t min, uint8_t max)
{
    return field.mask == RTC_cl::Alarm_Masks::Ignore
        || (field.mask == RTC_cl::Alarm_Masks::Care && field.val >= min && field.val <= max);
}

uint32_t alarm_field(const RTC_cl::AlarmField& field, uint32_t mask, uint32_t pos)
{
    return field.mask == RTC_cl::Alarm_Masks::Ignore ? mask
        : static_cast<uint32_t>(RTC_cl::to_bcd(field.val)) << pos;
}
}

SysInitStatus RTC_cl::SetUp(CLK_Source source, uint32_t prediv_a, uint32_t prediv_s)
{
    if (_owner || prediv_a > 127 || prediv_s > 32767
        || (source != CLK_Source::LSE && source != CLK_Source::LSI && source != CLK_Source::HSE))
        return SysInitStatus::InitError;
    // HSE is not started here. F4/F7 also need a caller-configured RTCPRE.
    if (source == CLK_Source::HSE) {
        if (!(RCC->CR & RCC_CR_HSERDY)) return SysInitStatus::InitError;
#if !defined(STM32G0)
        if (((RCC->CFGR & RCC_CFGR_RTCPRE) >> RCC_CFGR_RTCPRE_Pos) < 2)
            return SysInitStatus::InitError;
#endif
    }
#if defined(STM32G0)
    RCC->APBENR1 |= RCC_APBENR1_PWREN | RCC_APBENR1_RTCAPBEN;
    (void)RCC->APBENR1;
    PWR->CR1 |= PWR_CR1_DBP;
    if (!System::WaitBits(PWR->CR1, PWR_CR1_DBP, PWR_CR1_DBP, timeout_ms))
        return SysInitStatus::InitError;
#elif defined(STM32F7)
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;
    PWR->CR1 |= PWR_CR1_DBP;
    if (!System::WaitBits(PWR->CR1, PWR_CR1_DBP, PWR_CR1_DBP, timeout_ms))
        return SysInitStatus::InitError;
#else
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;
    PWR->CR |= PWR_CR_DBP;
    if (!System::WaitBits(PWR->CR, PWR_CR_DBP, PWR_CR_DBP, timeout_ms))
        return SysInitStatus::InitError;
#endif
    const uint32_t selection = static_cast<uint32_t>(source);
    const uint32_t previous = RCC->BDCR & RCC_BDCR_RTCSEL;
    if (previous && previous != selection) {
        // Explicit policy: a changed source discards retained calendar/backup data.
        // Retain board-selected LSE electrical settings if LSE will be restarted.
        const uint32_t lse_config = RCC->BDCR & (RCC_BDCR_LSEBYP
#ifdef RCC_BDCR_LSEDRV
            | RCC_BDCR_LSEDRV
#endif
            );
        RCC->BDCR |= RCC_BDCR_BDRST;
        RCC->BDCR &= ~RCC_BDCR_BDRST;
        if (source == CLK_Source::LSE) RCC->BDCR |= lse_config;
    }
    if (source == CLK_Source::LSE) {
        RCC->BDCR |= RCC_BDCR_LSEON;
        if (!System::WaitBits(RCC->BDCR, RCC_BDCR_LSERDY, RCC_BDCR_LSERDY,
                LSE_STARTUP_TIMEOUT)) return SysInitStatus::InitError;
    } else if (source == CLK_Source::LSI) {
        RCC->CSR |= RCC_CSR_LSION;
        if (!System::WaitBits(RCC->CSR, RCC_CSR_LSIRDY, RCC_CSR_LSIRDY, timeout_ms))
            return SysInitStatus::InitError;
    }
    const bool retained = (RCC->BDCR & RCC_BDCR_RTCEN) && (RTC_STATE & initialized);
    RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_RTCSEL) | selection | RCC_BDCR_RTCEN;
    // Mask retained event sources before any wait; their previous owner is gone.
    WriteProtection(DISABLE);
    RTC->CR &= ~(all_irqs | RTC_CR_ALRAE | RTC_CR_ALRBE | RTC_CR_WUTE | RTC_CR_TSE);
    WriteProtection(ENABLE);
    if (!retained) {
        if (!EnterInit()) return SysInitStatus::InitError;
        RTC->CR &= ~(RTC_CR_FMT | RTC_CR_BYPSHAD);
        // Program the synchronous field first, then the asynchronous field.
        RTC->PRER = prediv_s;
        RTC->PRER = (prediv_a << RTC_PRER_PREDIV_A_Pos) | prediv_s;
        if (!ExitInit()) return SysInitStatus::InitError;
    } else {
        // The API reads 24-hour shadow registers; do not silently reinterpret
        // a retained 12-hour/bypass configuration from another implementation.
        if (RTC->CR & (RTC_CR_FMT | RTC_CR_BYPSHAD)) return SysInitStatus::InitError;
        WriteProtection(DISABLE);
        const bool ready = Sync();
        WriteProtection(ENABLE);
        if (!ready) return SysInitStatus::InitError;
    }
    WriteProtection(DISABLE);
    ClearFlags(0x1Fu);
    WriteProtection(ENABLE);
    _owner = this;
    return SysInitStatus::InitOK;
}

bool RTC_cl::EnterInit()
{
    WriteProtection(DISABLE);
    RTC_STATE = state_w0c | init_bit;
    if (System::WaitBits(RTC_STATE, init_ready, init_ready, timeout_ms)) return true;
    RTC_STATE = state_w0c;
    WriteProtection(ENABLE);
    return false;
}

bool RTC_cl::Sync()
{
    RTC_STATE = (state_w0c & ~synced) | (RTC_STATE & init_bit);
    return System::WaitBits(RTC_STATE, synced, synced, timeout_ms);
}

bool RTC_cl::ExitInit()
{
    RTC_STATE = state_w0c; // INIT=0, preserve all rc_w0 flags.
    const bool ready = Sync();
    WriteProtection(ENABLE);
    return ready;
}

void RTC_cl::ClearFlags(uint32_t flags)
{
#if defined(STM32G0)
    RTC->SCR = flags;
#else
    RTC->ISR = (state_w0c & ~(flags << RTC_ISR_ALRAF_Pos)) | (RTC->ISR & RTC_ISR_INIT);
#endif
}

SysStatus RTC_cl::SetTime(const Time& time)
{
    if (_owner != this) return SysStatus::NotInit;
    if (time.hours > 23 || time.minutes > 59 || time.seconds > 59) return SysStatus::Error;
    if (!EnterInit()) return SysStatus::Timeout;
    RTC->TR = (to_bcd(time.hours) << RTC_TR_HU_Pos)
        | (to_bcd(time.minutes) << RTC_TR_MNU_Pos) | (to_bcd(time.seconds) << RTC_TR_SU_Pos);
    return ExitInit() ? SysStatus::OK : SysStatus::Timeout;
}

SysStatus RTC_cl::SetDate(const Date& date, WeekDay weekday)
{
    if (_owner != this) return SysStatus::NotInit;
    if (date.month < 1 || date.month > 12 || date.year > 99
        || static_cast<uint32_t>(weekday) < 1 || static_cast<uint32_t>(weekday) > 7)
        return SysStatus::Error;
    constexpr uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    const uint8_t max_day = days[date.month - 1] + (date.month == 2 && (date.year & 3u) == 0);
    if (!date.day || date.day > max_day) return SysStatus::Error;
    if (!EnterInit()) return SysStatus::Timeout;
    RTC->DR = (to_bcd(date.day) << RTC_DR_DU_Pos) | (to_bcd(date.month) << RTC_DR_MU_Pos)
        | (to_bcd(date.year) << RTC_DR_YU_Pos) | (static_cast<uint32_t>(weekday) << RTC_DR_WDU_Pos);
    return ExitInit() ? SysStatus::OK : SysStatus::Timeout;
}

SysStatus RTC_cl::SetTime_hex(const Time& time)
{
    return SetTime({from_bcd(time.hours), from_bcd(time.minutes), from_bcd(time.seconds)});
}

SysStatus RTC_cl::SetDate_hex(const Date& date, WeekDay weekday)
{
    return SetDate({from_bcd(date.day), from_bcd(date.month), from_bcd(date.year)}, weekday);
}

RTC_cl::RtcTime RTC_cl::DecodeTime(uint32_t tr)
{
    return {uint8_t((tr & RTC_TR_SU) >> RTC_TR_SU_Pos), uint8_t((tr & RTC_TR_ST) >> RTC_TR_ST_Pos),
        uint8_t((tr & RTC_TR_MNU) >> RTC_TR_MNU_Pos), uint8_t((tr & RTC_TR_MNT) >> RTC_TR_MNT_Pos),
        uint8_t((tr & RTC_TR_HU) >> RTC_TR_HU_Pos), uint8_t((tr & RTC_TR_HT) >> RTC_TR_HT_Pos), {}};
}

RTC_cl::RtcDate RTC_cl::DecodeDate(uint32_t dr)
{
    return {uint8_t((dr & RTC_DR_DU) >> RTC_DR_DU_Pos), uint8_t((dr & RTC_DR_DT) >> RTC_DR_DT_Pos),
        uint8_t((dr & RTC_DR_MU) >> RTC_DR_MU_Pos), uint8_t((dr & RTC_DR_MT) >> RTC_DR_MT_Pos),
        uint8_t((dr & RTC_DR_YU) >> RTC_DR_YU_Pos), uint8_t((dr & RTC_DR_YT) >> RTC_DR_YT_Pos),
        uint8_t((dr & RTC_DR_WDU) >> RTC_DR_WDU_Pos), 0};
}

void RTC_cl::GetDateTime(RtcTime& time, RtcDate& date) const
{
    const uint32_t tr = RTC->TR;
    const uint32_t dr = RTC->DR; // Always release the shadow lock before returning.
    time = DecodeTime(tr);
    date = DecodeDate(dr);
}

RTC_cl::RtcTime RTC_cl::GetTime() const
{
    const uint32_t tr = RTC->TR;
    (void)RTC->DR;
    return DecodeTime(tr);
}

RTC_cl::RtcDate RTC_cl::GetDate() const { return DecodeDate(RTC->DR); }

SysStatus RTC_cl::EnableWakeUpTimer(uint32_t counter, uint8_t clock_div)
{
    if (_owner != this) return SysStatus::NotInit;
    if (counter > 65535 || (clock_div > 4 && clock_div != 6)) return SysStatus::Error;
    WriteProtection(DISABLE);
    RTC->CR &= ~(RTC_CR_WUTE | RTC_CR_WUTIE);
    const bool ready = System::WaitBits(RTC_STATE, wake_ready, wake_ready, timeout_ms);
    if (ready) {
        RTC->WUTR = counter;
        RTC->CR = (RTC->CR & ~RTC_CR_WUCKSEL) | (uint32_t(clock_div) << RTC_CR_WUCKSEL_Pos);
        ClearFlags(1u << 2);
    }
    WriteProtection(ENABLE);
    if (!ready) return SysStatus::Timeout;
    if (IRQ_en(IRQ_s::WUT, ENABLE) != SysInitStatus::InitOK) return SysStatus::Error;
    WriteProtection(DISABLE);
    RTC->CR |= RTC_CR_WUTE;
    WriteProtection(ENABLE);
    return SysStatus::OK;
}

SysStatus RTC_cl::DisableWakeUpTimer()
{
    if (_owner != this) return SysStatus::NotInit;
    WriteProtection(DISABLE);
    RTC->CR &= ~(RTC_CR_WUTE | RTC_CR_WUTIE);
    ClearFlags(1u << 2);
    WriteProtection(ENABLE);
    return SysStatus::OK;
}

SysStatus RTC_cl::Alarm(bool alarm_b, FunctionalState enable, const AlarmConfig& config)
{
    if (_owner != this) return SysStatus::NotInit;
    if (enable && (!valid_alarm_field(config.day, 1, 31)
        || !valid_alarm_field(config.hour, 0, 23) || !valid_alarm_field(config.min, 0, 59)
        || !valid_alarm_field(config.sec, 0, 59) || config.sub_sec_msk > 15 || config.sub_sec > 32767))
        return SysStatus::Error;
    const uint32_t arm = alarm_b ? RTC_CR_ALRBE : RTC_CR_ALRAE;
    const uint32_t irq = alarm_b ? RTC_CR_ALRBIE : RTC_CR_ALRAIE;
    const uint32_t flag = alarm_b ? 2u : 1u;
    WriteProtection(DISABLE);
    RTC->CR &= ~(arm | irq);
    const uint32_t writable = alarm_b ? alarm_b_ready : alarm_a_ready;
    if (enable && !System::WaitBits(RTC_STATE, writable, writable, timeout_ms)) {
        WriteProtection(ENABLE);
        return SysStatus::Timeout;
    }
    ClearFlags(flag);
    if (enable) {
        const uint32_t value = alarm_field(config.day, RTC_ALRMAR_MSK4, RTC_ALRMAR_DU_Pos)
            | alarm_field(config.hour, RTC_ALRMAR_MSK3, RTC_ALRMAR_HU_Pos)
            | alarm_field(config.min, RTC_ALRMAR_MSK2, RTC_ALRMAR_MNU_Pos)
            | alarm_field(config.sec, RTC_ALRMAR_MSK1, RTC_ALRMAR_SU_Pos);
        const uint32_t subseconds = (config.sub_sec_msk << RTC_ALRMASSR_MASKSS_Pos) | config.sub_sec;
        if (alarm_b) { RTC->ALRMBR = value; RTC->ALRMBSSR = subseconds; }
        else { RTC->ALRMAR = value; RTC->ALRMASSR = subseconds; }
    }
    WriteProtection(ENABLE);
    if (!enable) return SysStatus::OK;
    if (IRQ_en(alarm_b ? IRQ_s::ALR_B : IRQ_s::ALR_A, ENABLE) != SysInitStatus::InitOK)
        return SysStatus::Error;
    WriteProtection(DISABLE);
    RTC->CR |= arm;
    WriteProtection(ENABLE);
    return SysStatus::OK;
}

SysStatus RTC_cl::EnableAlarm_A(FunctionalState en, const AlarmConfig& cfg) { return Alarm(false, en, cfg); }
SysStatus RTC_cl::EnableAlarm_B(FunctionalState en, const AlarmConfig& cfg) { return Alarm(true, en, cfg); }

SysStatus RTC_cl::EnableTimestamp(FunctionalState enable, TimestampEdge edge)
{
    if (_owner != this) return SysStatus::NotInit;
    if (edge != TimestampEdge::Rising && edge != TimestampEdge::Falling) return SysStatus::Error;
    WriteProtection(DISABLE);
    RTC->CR &= ~(RTC_CR_TSE | RTC_CR_TSIE);
    ClearFlags((1u << 3) | (1u << 4));
    RTC->CR = (RTC->CR & ~RTC_CR_TSEDGE) | static_cast<uint32_t>(edge);
    WriteProtection(ENABLE);
    if (!enable) return SysStatus::OK;
    if (IRQ_en(IRQ_s::TS, ENABLE) != SysInitStatus::InitOK) return SysStatus::Error;
    WriteProtection(DISABLE);
    RTC->CR |= RTC_CR_TSE;
    WriteProtection(ENABLE);
    return SysStatus::OK;
}

RTC_cl::Timestamp RTC_cl::ReadTimestamp(uint32_t flags)
{
    const uint32_t tr = RTC->TSTR;
    const uint32_t dr = RTC->TSDR;
    const uint32_t ss = RTC->TSSSR;
    return {DecodeTime(tr), DecodeDate(dr), ss, (flags & (1u << 4)) != 0};
}

SysStatus RTC_cl::GetTimestamp(Timestamp& stamp)
{
    if (_owner != this) return SysStatus::NotInit;
    const uint32_t flags = event_flags();
    if (!(flags & (1u << 3))) return SysStatus::Busy;
    stamp = ReadTimestamp(flags);
    ClearFlags((1u << 3) | (1u << 4));
    return SysStatus::OK;
}

SysInitStatus RTC_cl::IRQ_en(IRQ_s source, FunctionalState enable)
{
    if (_owner != this) return SysInitStatus::InitError;
    const uint32_t bit = static_cast<uint32_t>(source);
    if (bit != RTC_CR_ALRAIE && bit != RTC_CR_ALRBIE && bit != RTC_CR_WUTIE && bit != RTC_CR_TSIE)
        return SysInitStatus::InitError;
#if defined(STM32G0)
    constexpr uint32_t index = 0;
#else
    const uint32_t index = bit == RTC_CR_WUTIE ? 1u : (bit == RTC_CR_TSIE ? 2u : 0u);
#endif
    const IRQn_Type vector = vectors[index];
    if (enable && !(_registered & (1u << index))) {
        const bool was_enabled = NVIC_GetEnableIRQ(vector) != 0;
        NVIC_DisableIRQ(vector);
        const SysInitStatus result = IRQ_Registry::Register(vector, this);
        if (result != SysInitStatus::InitOK) {
            if (was_enabled) NVIC_EnableIRQ(vector);
            return result;
        }
        _registered |= 1u << index;
#if defined(STM32G0)
        EXTI->IMR1 |= EXTI_IMR1_IM19;
#else
        const uint32_t line = 1u << (index == 0 ? 17 : (index == 1 ? 22 : 21));
        EXTI->PR = line;
        EXTI->FTSR &= ~line;
        EXTI->RTSR |= line;
        EXTI->IMR |= line;
#endif
        NVIC_EnableIRQ(vector);
    }
    WriteProtection(DISABLE);
    if (enable) RTC->CR |= bit;
    else RTC->CR &= ~bit;
    WriteProtection(ENABLE);
    return SysInitStatus::InitOK;
}

void RTC_cl::HandleIRQ()
{
    const uint32_t cr = RTC->CR;
    const uint32_t flags = event_flags();
    const uint32_t pending = flags & (cr >> RTC_CR_ALRAIE_Pos) & 15u;
    if (!pending) return; // Shared TAMP vector: no enabled RTC source is pending.
#if !defined(STM32G0)
    // Clear only EXTI routes for sources handled in this pass. RTC flags below
    // remain the authority, including when timestamp shares a vector with TAMP.
    uint32_t lines = 0;
    if (pending & 3u) lines |= 1u << 17;
    if (pending & 4u) lines |= 1u << 22;
    if (pending & 8u) lines |= 1u << 21;
    EXTI->PR = lines;
#endif
    // Timestamp registers are erased by TSF clear: capture first, then acknowledge.
    if (pending & 8u) {
        const Timestamp stamp = ReadTimestamp(flags);
        ClearFlags((1u << 3) | (flags & (1u << 4)));
        OnTimestamp(stamp);
    }
    if (pending & 7u) ClearFlags(pending & 7u);
    if (pending & 1u) OnAlarmA();
    if (pending & 2u) OnAlarmB();
    if (pending & 4u) OnWakeup();
}

SysStatus RTC_cl::SetCalibration(uint16_t calm, bool calp)
{
    if (_owner != this) return SysStatus::NotInit;
    if (calm > 511) return SysStatus::Error;
    if (RTC_STATE & recal_pending) return SysStatus::Busy;
    WriteProtection(DISABLE);
    RTC->CALR = (calp ? RTC_CALR_CALP : 0u) | calm;
    WriteProtection(ENABLE);
    return SysStatus::OK;
}

SysStatus RTC_cl::EnableCOE(bool enable, CalOut output)
{
    if (_owner != this) return SysStatus::NotInit;
    if (output != CalOut::Out_512 && output != CalOut::Out_1Hz) return SysStatus::Error;
    WriteProtection(DISABLE);
    RTC->CR = (RTC->CR & ~(RTC_CR_COE | RTC_CR_COSEL))
        | (enable ? RTC_CR_COE | static_cast<uint32_t>(output) : 0u);
    WriteProtection(ENABLE);
    return SysStatus::OK;
}

void RTC_cl::DeInit()
{
    if (_owner != this) return;
    WriteProtection(DISABLE);
    RTC->CR &= ~(all_irqs | RTC_CR_ALRAE | RTC_CR_ALRBE | RTC_CR_WUTE | RTC_CR_TSE | RTC_CR_COE);
    ClearFlags(0x1Fu);
    WriteProtection(ENABLE);
    for (uint32_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
        if (!(_registered & (1u << i))) continue;
        const IRQn_Type vector = vectors[i];
        const bool enabled = NVIC_GetEnableIRQ(vector) != 0;
        NVIC_DisableIRQ(vector);
        IRQ_Registry::Unregister(vector, this);
#if defined(STM32G0)
        EXTI->IMR1 &= ~EXTI_IMR1_IM19; // TAMP uses its own internal line 21.
#else
        // TAMP and timestamp share line 21; retain it for remaining handlers.
        if (i != 2 || !IRQ_Registry::HasHandlers(vector)) {
            const uint32_t line = 1u << (i == 0 ? 17 : (i == 1 ? 22 : 21));
            EXTI->IMR &= ~line;
            EXTI->PR = line;
        }
#endif
        if (enabled && IRQ_Registry::HasHandlers(vector)) NVIC_EnableIRQ(vector);
    }
    _registered = 0;
    _owner = nullptr;
}
