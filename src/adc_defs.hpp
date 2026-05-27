/**
 * @file    adc_defs.hpp
 * @brief   Compile-time and run-time tables for the ADC_N driver.
 *
 * Two sections, each activated by a different preprocessor symbol.
 *
 * Section A — ADC_PIN struct + channel tables + enums
 * ----------------------------------------------------
 * Guard:  ADC_HPP_  (include-guard of adc.hpp, already defined when
 *                    "#include "adc_defs.hpp"" is reached inside the class body)
 *         also !ADC_DEFS_CPP to prevent re-emission at file scope.
 * Scope:  ADC_N class body — public nested types.
 * Result: ADC_PIN, ADC_N::IN::PA0, TRIG, SMPL enums.
 *
 * Section B — peripheral-info table (adc_table)
 * -----------------------------------------------
 * Guard:  ADC_DEFS_CPP  (defined in adc.cpp just before the include)
 * Scope:  file scope inside adc.cpp.
 * Result: ADC_N::adc_table[] is defined.
 *
 * Usage
 * -----
 * adc.hpp (inside class body — ADC_HPP_ already defined):
 * @code
 *   #include "adc_defs.hpp"
 * @endcode
 *
 * adc.cpp (after #include "adc.hpp"):
 * @code
 *   #define ADC_DEFS_CPP
 *   #include "adc_defs.hpp"
 *   #undef  ADC_DEFS_CPP
 * @endcode
 */

// ===========================================================================
// Section A: channel tables + enums — ADC_N class body
// (ADC_PIN is defined at file scope in adc.hpp, before the class)
// ===========================================================================
#if defined(ADC_HPP_) && !defined(ADC_DEFS_CPP)

// ---------------------------------------------------------------------------
// External trigger source — values map directly to the EXTSEL register field.
//
// STM32G0: CFGR1[8:6] (3 bits).
// STM32F4: CR2[27:24] (4 bits).
//
// SW = 0xFF is a sentinel meaning "software trigger" (no external source).
// ---------------------------------------------------------------------------
#if defined(STM32G0)
enum class TRIG : uint8_t {
	TIM1_TRGO2 = 0,  ///< TIM1 TRGO2 output
	TIM1_CC4   = 1,  ///< TIM1 capture/compare channel 4
	TIM2_TRGO  = 2,  ///< TIM2 TRGO  (not present on all G0 sub-families)
	TIM3_TRGO  = 3,  ///< TIM3 TRGO
	EXTI11     = 7,  ///< EXTI line 11
	SW         = 0xFF ///< Software trigger (ADSTART bit)
};
#elif defined(STM32F4)
enum class TRIG : uint8_t {
	TIM1_CC1   = 0,
	TIM1_CC2   = 1,
	TIM1_CC3   = 2,
	TIM2_CC2   = 3,
	TIM2_CC3   = 4,
	TIM2_CC4   = 5,
	TIM2_TRGO  = 6,
	TIM3_CC1   = 7,
	TIM3_TRGO  = 8,
	TIM4_CC4   = 9,
	TIM5_CC1   = 10,
	TIM5_CC2   = 11,
	TIM5_CC3   = 12,
	TIM8_CC1   = 13,
	TIM8_TRGO  = 14,
	EXTI11     = 15,
	SW         = 0xFF
};
#endif

// ---------------------------------------------------------------------------
// Sampling time — values map directly to the SMPRx register field (3 bits).
//
// STM32G0: written to SMPR[2:0] (SMP1 field); all channels use SMP1
//          by default (SMPSEL = 0).
// STM32F4: written to SMPRx (3 bits per channel); applied uniformly to
//          all selected channels.
// ---------------------------------------------------------------------------
enum class SMPL : uint8_t {
#if defined(STM32G0)
	CYC_1_5   = 0,
	CYC_3_5   = 1,
	CYC_7_5   = 2,
	CYC_12_5  = 3,
	CYC_19_5  = 4,
	CYC_39_5  = 5,
	CYC_79_5  = 6,
	CYC_160_5 = 7,
#elif defined(STM32F4)
	CYC_3   = 0,
	CYC_15  = 1,
	CYC_28  = 2,
	CYC_56  = 3,
	CYC_84  = 4,
	CYC_112 = 5,
	CYC_144 = 6,
	CYC_480 = 7,
#endif
};

// ---------------------------------------------------------------------------
// Per-family channel pin tables
//
// Naming convention:  ADC_N::IN::Pyz    (G0, single ADC)
//                     ADC_N::_1::Pyz    (F4, ADC1)
//                     ADC_N::_2::Pyz    (F4, ADC2)
//                     ADC_N::_3::Pyz    (F4, ADC3)
//
// Channel numbers are taken from the device datasheet "Pin definitions" table.
// ---------------------------------------------------------------------------

#if defined(STM32G0)

/// All external ADC1 channels on STM32G0x0 devices.
struct IN {
	// External channels — verified against STM32G030 DS Rev 3, Table 13.
	static constexpr ADC_PIN PA0  = { GPIOA_BASE,  0,  0, ADC1_BASE };
	static constexpr ADC_PIN PA1  = { GPIOA_BASE,  1,  1, ADC1_BASE };
	static constexpr ADC_PIN PA2  = { GPIOA_BASE,  2,  2, ADC1_BASE };
	static constexpr ADC_PIN PA3  = { GPIOA_BASE,  3,  3, ADC1_BASE };
	static constexpr ADC_PIN PA4  = { GPIOA_BASE,  4,  4, ADC1_BASE };
	static constexpr ADC_PIN PA5  = { GPIOA_BASE,  5,  5, ADC1_BASE };
	static constexpr ADC_PIN PA6  = { GPIOA_BASE,  6,  6, ADC1_BASE };
	static constexpr ADC_PIN PA7  = { GPIOA_BASE,  7,  7, ADC1_BASE };
	static constexpr ADC_PIN PB0  = { GPIOB_BASE,  0,  8, ADC1_BASE };
	static constexpr ADC_PIN PB1  = { GPIOB_BASE,  1,  9, ADC1_BASE };
	static constexpr ADC_PIN PB2  = { GPIOB_BASE,  2, 10, ADC1_BASE };
	static constexpr ADC_PIN PB10 = { GPIOB_BASE, 10, 11, ADC1_BASE };
	static constexpr ADC_PIN PB11 = { GPIOB_BASE, 11, 15, ADC1_BASE };
};

#elif defined(STM32F4)

/// ADC1 channels (shared pin mapping with ADC2 — different adc_base).
struct _1 {
	static constexpr ADC_PIN PA0  = { GPIOA_BASE,  0,  0, ADC1_BASE };
	static constexpr ADC_PIN PA1  = { GPIOA_BASE,  1,  1, ADC1_BASE };
	static constexpr ADC_PIN PA2  = { GPIOA_BASE,  2,  2, ADC1_BASE };
	static constexpr ADC_PIN PA3  = { GPIOA_BASE,  3,  3, ADC1_BASE };
	static constexpr ADC_PIN PA4  = { GPIOA_BASE,  4,  4, ADC1_BASE };
	static constexpr ADC_PIN PA5  = { GPIOA_BASE,  5,  5, ADC1_BASE };
	static constexpr ADC_PIN PA6  = { GPIOA_BASE,  6,  6, ADC1_BASE };
	static constexpr ADC_PIN PA7  = { GPIOA_BASE,  7,  7, ADC1_BASE };
	static constexpr ADC_PIN PB0  = { GPIOB_BASE,  0,  8, ADC1_BASE };
	static constexpr ADC_PIN PB1  = { GPIOB_BASE,  1,  9, ADC1_BASE };
	static constexpr ADC_PIN PC0  = { GPIOC_BASE,  0, 10, ADC1_BASE };
	static constexpr ADC_PIN PC1  = { GPIOC_BASE,  1, 11, ADC1_BASE };
	static constexpr ADC_PIN PC2  = { GPIOC_BASE,  2, 12, ADC1_BASE };
	static constexpr ADC_PIN PC3  = { GPIOC_BASE,  3, 13, ADC1_BASE };
	static constexpr ADC_PIN PC4  = { GPIOC_BASE,  4, 14, ADC1_BASE };
	static constexpr ADC_PIN PC5  = { GPIOC_BASE,  5, 15, ADC1_BASE };
};

/// ADC2 — same GPIO pins as ADC1, different adc_base.
struct _2 {
	static constexpr ADC_PIN PA0  = { GPIOA_BASE,  0,  0, ADC2_BASE };
	static constexpr ADC_PIN PA1  = { GPIOA_BASE,  1,  1, ADC2_BASE };
	static constexpr ADC_PIN PA2  = { GPIOA_BASE,  2,  2, ADC2_BASE };
	static constexpr ADC_PIN PA3  = { GPIOA_BASE,  3,  3, ADC2_BASE };
	static constexpr ADC_PIN PA4  = { GPIOA_BASE,  4,  4, ADC2_BASE };
	static constexpr ADC_PIN PA5  = { GPIOA_BASE,  5,  5, ADC2_BASE };
	static constexpr ADC_PIN PA6  = { GPIOA_BASE,  6,  6, ADC2_BASE };
	static constexpr ADC_PIN PA7  = { GPIOA_BASE,  7,  7, ADC2_BASE };
	static constexpr ADC_PIN PB0  = { GPIOB_BASE,  0,  8, ADC2_BASE };
	static constexpr ADC_PIN PB1  = { GPIOB_BASE,  1,  9, ADC2_BASE };
	static constexpr ADC_PIN PC0  = { GPIOC_BASE,  0, 10, ADC2_BASE };
	static constexpr ADC_PIN PC1  = { GPIOC_BASE,  1, 11, ADC2_BASE };
	static constexpr ADC_PIN PC2  = { GPIOC_BASE,  2, 12, ADC2_BASE };
	static constexpr ADC_PIN PC3  = { GPIOC_BASE,  3, 13, ADC2_BASE };
	static constexpr ADC_PIN PC4  = { GPIOC_BASE,  4, 14, ADC2_BASE };
	static constexpr ADC_PIN PC5  = { GPIOC_BASE,  5, 15, ADC2_BASE };
};

/// ADC3 — dedicated pins on PORTF (F4 devices with ADC3).
struct _3 {
	static constexpr ADC_PIN PC0  = { GPIOC_BASE,  0, 10, ADC3_BASE };
	static constexpr ADC_PIN PC1  = { GPIOC_BASE,  1, 11, ADC3_BASE };
	static constexpr ADC_PIN PC2  = { GPIOC_BASE,  2, 12, ADC3_BASE };
	static constexpr ADC_PIN PC3  = { GPIOC_BASE,  3, 13, ADC3_BASE };
	static constexpr ADC_PIN PF3  = { GPIOF_BASE,  3,  9, ADC3_BASE };
	static constexpr ADC_PIN PF4  = { GPIOF_BASE,  4, 14, ADC3_BASE };
	static constexpr ADC_PIN PF5  = { GPIOF_BASE,  5, 15, ADC3_BASE };
	static constexpr ADC_PIN PF6  = { GPIOF_BASE,  6,  4, ADC3_BASE };
	static constexpr ADC_PIN PF7  = { GPIOF_BASE,  7,  5, ADC3_BASE };
	static constexpr ADC_PIN PF8  = { GPIOF_BASE,  8,  6, ADC3_BASE };
	static constexpr ADC_PIN PF9  = { GPIOF_BASE,  9,  7, ADC3_BASE };
	static constexpr ADC_PIN PF10 = { GPIOF_BASE, 10,  8, ADC3_BASE };
};

#endif  // STM32 family

#endif  // Section A: defined(ADC_HPP_) && !defined(ADC_DEFS_CPP)


// ===========================================================================
// Section B: Peripheral-info table — adc.cpp file scope
// ===========================================================================
#ifdef ADC_DEFS_CPP

#if defined(STM32G0)
const ADC_N::PeriphInfo ADC_N::adc_table[] = {
	{ ADC1, &RCC->APBENR2, RCC_APBENR2_ADCEN, ADC1_IRQn, DMA_Sx::Req::Adc1::RX.ch }
};
#elif defined(STM32F4)
const ADC_N::PeriphInfo ADC_N::adc_table[] = {
	{ ADC1, &RCC->APB2ENR, RCC_APB2ENR_ADC1EN, ADC_IRQn, 0 },
	{ ADC2, &RCC->APB2ENR, RCC_APB2ENR_ADC2EN, ADC_IRQn, 0 },
	{ ADC3, &RCC->APB2ENR, RCC_APB2ENR_ADC3EN, ADC_IRQn, 0 },
};
#endif

#endif  // ADC_DEFS_CPP (Section B)
