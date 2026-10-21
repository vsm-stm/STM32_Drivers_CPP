/**
 * @file rcc_defs.hpp
 * @brief Device-specific constants for ClockSystem compile-time validation.
 *
 * Values describe nominal supported operating limits, numeric PLL coefficients,
 * voltage scale, Flash timing band and hardware wait timeouts. They do not set
 * registers themselves. Device selection is by CMSIS model definitions; an
 * unknown F4/F7 model is rejected rather than assigned a guessed clock limit.
 *
 * VDD is fixed at 3000 mV for the agreed 3.0/3.3 V use. FLASH_STEP selects the
 * wait-state band used by Prepare(); changing supply assumptions requires
 * revisiting these limits, not merely choosing a larger SYSCLK.
 */
#ifndef RCC_DEFS_H_
#define RCC_DEFS_H_

#include "system.hpp"

// Clock limits for the F4/F7 devices listed in STM32-base_files and for G0.
// VDD is fixed at 3.0 V (the same Flash timing band applies at 3.3 V).
// F401 uses Scale 2; other F4/F7 use Scale 1; G0 uses Range 1.
namespace RCCDefs {
inline constexpr uint32_t VDD_MV = 3000U;
inline constexpr uint32_t HSE_MIN = 4U * MHz;

#if defined(STM32F4) || defined(STM32F7)
inline constexpr uint32_t HSE_MAX = 26U * MHz;
inline constexpr uint32_t PLL_M_MIN = 2U, PLL_M_MAX = 63U;
inline constexpr uint32_t PLL_N_MAX = 432U;
inline constexpr uint32_t PLL_IN_MIN = 1U * MHz, PLL_IN_MAX = 2U * MHz;
inline constexpr uint32_t VCO_MAX = 432U * MHz;
inline constexpr uint32_t FLASH_STEP = 30U * MHz;
inline constexpr uint32_t BUS_MASK = RCC_CFGR_HPRE_Msk | RCC_CFGR_PPRE1_Msk | RCC_CFGR_PPRE2_Msk;
#if defined(STM32F446xx)
inline constexpr uint32_t PLL_SYS_MIN = 12500U * kHz;
#else
inline constexpr uint32_t PLL_SYS_MIN = 24U * MHz;
#endif

#if defined(STM32F401xC) || defined(STM32F401xE)
inline constexpr uint32_t SYS_MAX = 84U * MHz, APB1_MAX = 42U * MHz, APB2_MAX = 84U * MHz;
inline constexpr uint32_t PLL_N_MIN = 192U, VCO_MIN = 192U * MHz;
#elif defined(STM32F410Cx) || defined(STM32F410Rx) || defined(STM32F410Tx) \
   || defined(STM32F411xE) || defined(STM32F412Cx) || defined(STM32F412Rx) \
   || defined(STM32F412Vx) || defined(STM32F412Zx) || defined(STM32F413xx) || defined(STM32F423xx)
inline constexpr uint32_t SYS_MAX = 100U * MHz, APB1_MAX = 50U * MHz, APB2_MAX = 100U * MHz;
inline constexpr uint32_t PLL_N_MIN = 50U, VCO_MIN = 100U * MHz;
#elif defined(STM32F405xx) || defined(STM32F407xx) || defined(STM32F415xx) || defined(STM32F417xx)
inline constexpr uint32_t SYS_MAX = 168U * MHz, APB1_MAX = 42U * MHz, APB2_MAX = 84U * MHz;
inline constexpr uint32_t PLL_N_MIN = 50U, VCO_MIN = 100U * MHz;
#elif defined(STM32F427xx) || defined(STM32F429xx) || defined(STM32F437xx) || defined(STM32F439xx) \
   || defined(STM32F446xx)
inline constexpr uint32_t SYS_MAX = 180U * MHz, APB1_MAX = 45U * MHz, APB2_MAX = 90U * MHz;
inline constexpr uint32_t PLL_N_MIN = 50U, VCO_MIN = 100U * MHz;
#elif defined(STM32F469xx) || defined(STM32F479xx)
inline constexpr uint32_t SYS_MAX = 180U * MHz, APB1_MAX = 45U * MHz, APB2_MAX = 90U * MHz;
// DS11189/DS11118 specify 192 MHz even though RM0386 permits 100 MHz.
inline constexpr uint32_t PLL_N_MIN = 50U, VCO_MIN = 192U * MHz;
#elif defined(STM32F722xx) || defined(STM32F723xx) || defined(STM32F730xx) \
   || defined(STM32F732xx) || defined(STM32F733xx) || defined(STM32F745xx) \
   || defined(STM32F746xx) || defined(STM32F750xx) || defined(STM32F756xx) \
   || defined(STM32F765xx) || defined(STM32F767xx) || defined(STM32F769xx) \
   || defined(STM32F777xx) || defined(STM32F779xx)
inline constexpr uint32_t SYS_MAX = 216U * MHz, APB1_MAX = 54U * MHz, APB2_MAX = 108U * MHz;
inline constexpr uint32_t PLL_N_MIN = 50U, VCO_MIN = 100U * MHz;
#else
#error "RCC: unknown F4/F7 device; add verified clock limits to rcc_defs.hpp"
#endif

#if defined(STM32F7)
inline constexpr uint32_t VOS_VALUE = PWR_CR1_VOS_Msk;
inline constexpr uint32_t OVERDRIVE_LIMIT = 180U * MHz;
#elif defined(PWR_CR_ODEN)
inline constexpr uint32_t VOS_VALUE = PWR_CR_VOS_Msk;
inline constexpr uint32_t OVERDRIVE_LIMIT = 168U * MHz;
#else
#if defined(STM32F401xC) || defined(STM32F401xE)
inline constexpr uint32_t VOS_VALUE = PWR_CR_VOS_1;
#else
inline constexpr uint32_t VOS_VALUE = PWR_CR_VOS_Msk;
#endif
inline constexpr uint32_t OVERDRIVE_LIMIT = SYS_MAX;
#endif

inline constexpr uint32_t PLL_MASK = RCC_PLLCFGR_PLLSRC_Msk | RCC_PLLCFGR_PLLM_Msk
    | RCC_PLLCFGR_PLLN_Msk | RCC_PLLCFGR_PLLP_Msk | RCC_PLLCFGR_PLLQ_Msk
#ifdef RCC_PLLCFGR_PLLR_Msk
    | RCC_PLLCFGR_PLLR_Msk
#endif
    ;
#elif defined(STM32G0)
inline constexpr uint32_t HSE_MAX = 48U * MHz;
inline constexpr uint32_t SYS_MAX = 64U * MHz, APB1_MAX = 64U * MHz, APB2_MAX = 0U;
inline constexpr uint32_t PLL_M_MIN = 1U, PLL_M_MAX = 8U;
inline constexpr uint32_t PLL_N_MIN = 8U, PLL_N_MAX = 86U;
inline constexpr uint32_t PLL_IN_MIN = 2660U * kHz, PLL_IN_MAX = 16U * MHz;
// Datasheet operating limits; the RCC register description alone is broader.
inline constexpr uint32_t VCO_MIN = 96U * MHz, VCO_MAX = 344U * MHz;
inline constexpr uint32_t PLL_SYS_MIN = 12U * MHz;
inline constexpr uint32_t FLASH_STEP = 24U * MHz;
inline constexpr uint32_t OVERDRIVE_LIMIT = SYS_MAX;
inline constexpr uint32_t VOS_VALUE = PWR_CR1_VOS_0;
inline constexpr uint32_t BUS_MASK = RCC_CFGR_HPRE_Msk | RCC_CFGR_PPRE_Msk;
inline constexpr uint32_t PLL_MASK = RCC_PLLCFGR_PLLSRC_Msk | RCC_PLLCFGR_PLLM_Msk
    | RCC_PLLCFGR_PLLN_Msk | RCC_PLLCFGR_PLLP_Msk | RCC_PLLCFGR_PLLR_Msk
    | RCC_PLLCFGR_PLLPEN | RCC_PLLCFGR_PLLREN
#ifdef RCC_PLLQ_SUPPORT
    | RCC_PLLCFGR_PLLQ_Msk | RCC_PLLCFGR_PLLQEN
#endif
    ;
#else
#error "RCC supports STM32F4, STM32F7 and STM32G0 only"
#endif

// Run-time hardware waits, in System tick units (nominal milliseconds).
// LSE startup/switching gets a longer budget than PLL lock/disable.
inline constexpr uint32_t PLL_TIMEOUT_MS = 2U;
inline constexpr uint32_t HSI_TIMEOUT_MS = 2U;
inline constexpr uint32_t HSE_TIMEOUT_MS = 100U;
inline constexpr uint32_t LSI_TIMEOUT_MS = 2U;
inline constexpr uint32_t LSE_TIMEOUT_MS = 5000U;
inline constexpr uint32_t POWER_TIMEOUT_MS = 100U;
inline constexpr uint32_t FLASH_TIMEOUT_MS = 100U;
inline constexpr uint32_t SWITCH_TIMEOUT_MS = 5000U;
inline constexpr uint32_t LSE_HZ = 32768U;
static_assert(VDD_MV >= 2700U && VDD_MV <= 3600U, "RCC Flash timing assumes 2.7..3.6 V");
}

#endif // RCC_DEFS_H_
