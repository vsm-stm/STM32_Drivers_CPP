#include "rtc.hpp"

const RTC_cl::AlarmConfig RTC_cl::kNoAlarm{};

void (*RTC_cl::_wut_cb)(void)     = nullptr;
void (*RTC_cl::_alarm_a_cb)(void) = nullptr;
void (*RTC_cl::_alarm_b_cb)(void) = nullptr;
RTC_cl::RtcHandler RTC_cl::_rtc_handler;


SysInitStatus RTC_cl::SetUp(CLK_Source clk_src, uint32_t prediv_a, uint32_t prediv_s)
{
	RCC->APBENR1 |= RCC_APBENR1_RTCAPBEN | RCC_APBENR1_PWREN;
	PWR->CR1 |= PWR_CR1_DBP;
	// RCC->BDCR &= ~RCC_BDCR_RTCEN;

	if (clk_src == CLK_Source::LSE)
	{
		RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_LSEDRV_Msk) | (0b01 << RCC_BDCR_LSEDRV_Pos); // максимальный ток
		RCC->BDCR |= RCC_BDCR_LSEON;
		while(!(RCC->BDCR & RCC_BDCR_LSERDY));
	}
	else if ((clk_src == CLK_Source::LSI)
		&&   (!(RCC->CSR & RCC_CSR_LSIRDY)))
	{
		RCC->CSR |= RCC_CSR_LSION;
		while(!(RCC->CSR & RCC_CSR_LSIRDY));
	}

	if(!(RTC->ICSR & RTC_ICSR_RSF)) {
		RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_RTCSEL) | static_cast<uint32_t>(clk_src);
		RCC->BDCR |= RCC_BDCR_RTCEN;

		WriteProtection(DISABLE);

		RTC->ICSR = RTC_ICSR_INIT;
		while(!(RTC->ICSR & RTC_ICSR_INITF));

		RTC->PRER = (prediv_s & 0x7FFF) | ((prediv_a & 0x7FFF) << RTC_PRER_PREDIV_A_Pos);

		RTC->ICSR &= ~RTC_ICSR_INIT;
		WriteProtection(ENABLE);
	}

	return SysInitStatus::InitOK;
}

static uint32_t build_alrmr(const RTC_cl::AlarmConfig& cfg)
{
	return (static_cast<uint32_t>(cfg.day.mask)  << RTC_ALRMAR_MSK4_Pos) |
		   (static_cast<uint32_t>(cfg.hour.mask) << RTC_ALRMAR_MSK3_Pos) |
		   (static_cast<uint32_t>(cfg.min.mask)  << RTC_ALRMAR_MSK2_Pos) |
		   (static_cast<uint32_t>(cfg.sec.mask)  << RTC_ALRMAR_MSK1_Pos) |
		   (RTC_cl::to_bcd(cfg.day.val)  << RTC_ALRMAR_DU_Pos)  |
		   (RTC_cl::to_bcd(cfg.hour.val) << RTC_ALRMAR_HU_Pos)  |
		   (RTC_cl::to_bcd(cfg.min.val)  << RTC_ALRMAR_MNU_Pos) |
		   (RTC_cl::to_bcd(cfg.sec.val)  << RTC_ALRMAR_SU_Pos);
}

void RTC_cl::EnableAlarm_A(FunctionalState en, const AlarmConfig& cfg, void (*cb)(void))
{
	_alarm_a_cb = cb;
	WriteProtection(DISABLE);
	RTC->CR &= ~RTC_CR_ALRAE;
	while (!(RTC->ICSR & RTC_ICSR_ALRAWF));
	if (en) {
		RTC->ALRMAR  = build_alrmr(cfg);
		RTC->ALRMASSR = (cfg.sub_sec_msk << RTC_ALRMASSR_MASKSS_Pos) | cfg.sub_sec;
		RTC->CR |= RTC_CR_ALRAE;
	}
	WriteProtection(ENABLE);
	IRQ_en(IRQ_s::ALR_A, en);
}

void RTC_cl::EnableAlarm_B(FunctionalState en, const AlarmConfig& cfg, void (*cb)(void))
{
	_alarm_b_cb = cb;
	WriteProtection(DISABLE);
	RTC->CR &= ~RTC_CR_ALRBE;
	while (!(RTC->ICSR & RTC_ICSR_ALRBWF));
	if (en) {
		RTC->ALRMBR  = build_alrmr(cfg);
		RTC->ALRMBSSR = (cfg.sub_sec_msk << RTC_ALRMBSSR_MASKSS_Pos) | cfg.sub_sec;
		RTC->CR |= RTC_CR_ALRBE;
	}
	WriteProtection(ENABLE);
	IRQ_en(IRQ_s::ALR_B, en);
}