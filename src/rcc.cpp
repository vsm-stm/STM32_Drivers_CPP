#include "rcc.hpp"

uint32_t ClockSystem::HSESrcClk{0U};

namespace {
using namespace RCCDefs;
bool init_started = false;

bool SetLatency(uint32_t latency)
{
    const uint32_t value = latency << FLASH_ACR_LATENCY_Pos;
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY_Msk) | value;
    return System::WaitBits(FLASH->ACR, FLASH_ACR_LATENCY_Msk, value, FLASH_TIMEOUT_MS);
}

// F4/F7 VOS is programmed with PLL off and acknowledged after PLL starts.
// On G0 VOS readiness is independent of PLL.
void SetPowerScale()
{
#if defined(STM32G0)
    RCC->APBENR1 |= RCC_APBENR1_PWREN;
    (void)RCC->APBENR1;
    PWR->CR1 = (PWR->CR1 & ~PWR_CR1_VOS_Msk) | VOS_VALUE;
#elif defined(STM32F7)
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;
    PWR->CR1 = (PWR->CR1 & ~PWR_CR1_VOS_Msk) | VOS_VALUE;
#else
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;
    // F401 has no Scale 1. Other F4 select it using one or two VOS bits.
    PWR->CR = (PWR->CR & ~PWR_CR_VOS_Msk) | VOS_VALUE;
#endif
}

bool WaitPower(bool overdrive)
{
#if defined(STM32G0)
    (void)overdrive;
    return System::WaitBits(PWR->SR2, PWR_SR2_VOSF, 0U, POWER_TIMEOUT_MS);
#elif defined(STM32F7)
    if (!System::WaitBits(PWR->CSR1, PWR_CSR1_VOSRDY, PWR_CSR1_VOSRDY, POWER_TIMEOUT_MS)) return false;
    if (overdrive) {
        PWR->CR1 |= PWR_CR1_ODEN;
        if (!System::WaitBits(PWR->CSR1, PWR_CSR1_ODRDY, PWR_CSR1_ODRDY, POWER_TIMEOUT_MS)) return false;
        PWR->CR1 |= PWR_CR1_ODSWEN;
        if (!System::WaitBits(PWR->CSR1, PWR_CSR1_ODSWRDY, PWR_CSR1_ODSWRDY, POWER_TIMEOUT_MS)) return false;
    }
    return true;
#else
    if (!System::WaitBits(PWR->CSR, PWR_CSR_VOSRDY, PWR_CSR_VOSRDY, POWER_TIMEOUT_MS)) return false;
#ifdef PWR_CR_ODEN
    if (overdrive) {
        PWR->CR |= PWR_CR_ODEN;
        if (!System::WaitBits(PWR->CSR, PWR_CSR_ODRDY, PWR_CSR_ODRDY, POWER_TIMEOUT_MS)) return false;
        PWR->CR |= PWR_CR_ODSWEN;
        if (!System::WaitBits(PWR->CSR, PWR_CSR_ODSWRDY, PWR_CSR_ODSWRDY, POWER_TIMEOUT_MS)) return false;
    }
#else
    (void)overdrive;
#endif
    return true;
#endif
}

bool EnableSource(ClockSystem::SystemClockSource source, bool use_hse)
{
    if (use_hse) {
        RCC->CR |= RCC_CR_HSEON;
        return System::WaitBits(RCC->CR, RCC_CR_HSERDY, RCC_CR_HSERDY, HSE_TIMEOUT_MS);
    }
#if defined(STM32G0)
    if (source == ClockSystem::SystemClockSource::LSI) {
        RCC->CSR |= RCC_CSR_LSION;
        return System::WaitBits(RCC->CSR, RCC_CSR_LSIRDY, RCC_CSR_LSIRDY, LSI_TIMEOUT_MS);
    }
    if (source == ClockSystem::SystemClockSource::LSE) {
        RCC->APBENR1 |= RCC_APBENR1_PWREN;
        (void)RCC->APBENR1;
        const bool was_unlocked = (PWR->CR1 & PWR_CR1_DBP) != 0U;
        PWR->CR1 |= PWR_CR1_DBP;
        if (!System::WaitBits(PWR->CR1, PWR_CR1_DBP, PWR_CR1_DBP, POWER_TIMEOUT_MS)) {
            if (!was_unlocked) PWR->CR1 &= ~PWR_CR1_DBP;
            return false;
        }
        RCC->BDCR |= RCC_BDCR_LSEON;
        const bool ready = System::WaitBits(RCC->BDCR, RCC_BDCR_LSERDY, RCC_BDCR_LSERDY, LSE_TIMEOUT_MS);
        if (!was_unlocked) PWR->CR1 &= ~PWR_CR1_DBP;
        return ready;
    }
#else
    (void)source;
#endif
    // HSI supplies direct SYSCLK or PLL; System::Init() has selected HSI /1.
    RCC->CR |= RCC_CR_HSION;
    return System::WaitBits(RCC->CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY, HSI_TIMEOUT_MS);
}
}

SysInitStatus ClockSystem::Apply(const Prepared& config)
{
    using namespace RCCDefs;
    // One attempt per reset, shared by every Init<config>() specialization.
    if (init_started) return SysInitStatus::InitError;
    init_started = true;

    if (!EnableSource(config.source, config.hse != 0U)) return SysInitStatus::InitError;
    if (config.pll) {
        // SYSCLK is HSI after System::Init(); an old, unused PLL may still be on.
        RCC->CR &= ~RCC_CR_PLLON;
        if (!System::WaitBits(RCC->CR, RCC_CR_PLLRDY, 0U, PLL_TIMEOUT_MS)) return SysInitStatus::InitError;
    }
#if defined(STM32G0)
    SetPowerScale();
    if (!WaitPower(false)) return SysInitStatus::InitError;
#else
    if (config.pll) SetPowerScale();
#endif
    if (config.pll) {
        // Preserve reserved bits and fields not present on this device.
        RCC->PLLCFGR = (RCC->PLLCFGR & ~PLL_MASK) | config.pll_word;
        RCC->CR |= RCC_CR_PLLON;
        if (!System::WaitBits(RCC->CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY, PLL_TIMEOUT_MS)) return SysInitStatus::InitError;
#if !defined(STM32G0)
        if (!WaitPower(config.overdrive)) return SysInitStatus::InitError;
#endif
    }

    const uint32_t old_latency = (FLASH->ACR & FLASH_ACR_LATENCY_Msk) >> FLASH_ACR_LATENCY_Pos;
    if (config.latency > old_latency && !SetLatency(config.latency)) return SysInitStatus::InitError;

    // Apply dividers while still on HSI. This cannot raise HCLK above 16 MHz.
    RCC->CFGR = (RCC->CFGR & ~BUS_MASK) | config.bus_word;
    System::SystemCoreClock = config.hsi_hclk;
    if (System::InitTicks(config.hsi_ticks) != SysInitStatus::InitOK) return SysInitStatus::InitError;

    // Enforce the APB timer x1/x2 rule used by the compile-time calculations.
#if defined(RCC_DCKCFGR1_TIMPRE)
    RCC->DCKCFGR1 &= ~RCC_DCKCFGR1_TIMPRE;
#elif defined(RCC_DCKCFGR_TIMPRE)
    RCC->DCKCFGR &= ~RCC_DCKCFGR_TIMPRE;
#endif
    const uint32_t sw = static_cast<uint32_t>(config.source);
    const uint32_t sws = (sw >> RCC_CFGR_SW_Pos) << RCC_CFGR_SWS_Pos;
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_Msk) | sw;
    if (!System::WaitBits(RCC->CFGR, RCC_CFGR_SWS_Msk, sws, SWITCH_TIMEOUT_MS)) return SysInitStatus::InitError;

    // Restart SysTick immediately after the actual source switch, before late waits.
    System::SystemCoreClock = config.hclk;
    if (System::InitTicks(config.ticks) != SysInitStatus::InitOK) return SysInitStatus::InitError;
    System::APB1BusClock = config.apb1;
    System::APB2BusClock = config.apb2;
    System::TIMxAPB1Clock = config.tim1;
    System::TIMxAPB2Clock = config.tim2;
    HSESrcClk = config.hse;

    if (config.latency < old_latency && !SetLatency(config.latency)) return SysInitStatus::InitError;
    return SysInitStatus::InitOK;
}
