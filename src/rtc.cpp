#include "rtc.hpp"

RTC_cl::WutCallback RTC_cl::_wut_cb      = nullptr;
RTC_cl::WutHandler  RTC_cl::_wut_handler = {};

void RTC_cl::IRQ_en(IRQ_s irq, FunctionalState en)
{
	WriteProtection(DISABLE);
	if (en) RTC->CR |=  static_cast<uint32_t>(irq);
	else    RTC->CR &= ~static_cast<uint32_t>(irq);
	WriteProtection(ENABLE);

	constexpr uint32_t all_irq_bits = RTC_CR_WUTIE | RTC_CR_ALRAIE | RTC_CR_ALRBIE | RTC_CR_TSIE;
	if (en && !NVIC_GetEnableIRQ(RTC_TAMP_IRQn)) {
		IRQ_Registry::Register(RTC_TAMP_IRQn, &_wut_handler);
		EXTI->IMR1 |= EXTI_IMR1_IM19;
		NVIC_EnableIRQ(RTC_TAMP_IRQn);
	} else if (!en && !(RTC->CR & all_irq_bits)) {
		IRQ_Registry::Unregister(RTC_TAMP_IRQn);
		NVIC_DisableIRQ(RTC_TAMP_IRQn);
	}
}

SysInitStatus RTC_cl::SetUp(CLK_Source clk_src, uint32_t prediv_a, uint32_t prediv_s)
{
	RCC->APBENR1 |= RCC_APBENR1_RTCAPBEN | RCC_APBENR1_PWREN;
	PWR->CR1 |= PWR_CR1_DBP;
	// RCC->BDCR &= ~RCC_BDCR_RTCEN;

	if (clk_src == CLK_Source::LSE)
	{
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