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
 * Result: ADC_PIN, ADC_N::IN::PA0 / ADC_N::_1::PA0 tables, INTERNAL, SMPL / CH_SMPL enums, EXTSEL_EXTI11.
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
// Section A: ADC_PIN, channel tables + enums — ADC_N class body
// ===========================================================================
#if defined(ADC_HPP_) && !defined(ADC_DEFS_CPP)

// ---------------------------------------------------------------------------
// ADC_PIN — one analog input pin: GPIO pin + ADC channel.
// ---------------------------------------------------------------------------

/** @brief Analog input pin, used for the compile-time channel tables below. */
struct ADC_PIN {
	uint32_t port;
	uint8_t  pin;
	uint8_t  channel;    ///< ADC channel number (0-18)
	uint32_t adc_base;   ///< Owning ADC peripheral base (for validation)

	constexpr bool IsValid() const { return port != 0; }
};

// ---------------------------------------------------------------------------
// Internal inputs — added with AddChannel(INTERNAL, ...). Channel numbers
// and enable bits per family (RM, LL_ADC_CHANNEL_*):
//   G0:    TEMP IN12, VREFINT IN13, VBAT IN14 (VBAT/3); VREFEN/TSEN/VBATEN
//   F4/F7: ADC1 only. VREFINT IN17, VBAT IN18; TEMP IN16 on
//          F401/F405/F407/F410/F415/F417, IN18 (shared with VBAT) elsewhere;
//          TSVREFE (TEMP + VREFINT), VBATE
// ---------------------------------------------------------------------------

/** @brief Internal ADC inputs (no GPIO pin). */
enum class INTERNAL : uint8_t {
	VREFINT,   ///< internal voltage reference
	TEMP,      ///< temperature sensor (VSENSE)
	VBAT,      ///< VBAT through the internal divider
};

#if defined(STM32G0)
static constexpr uint8_t  CH_TEMP    = 12;
static constexpr uint8_t  CH_VREFINT = 13;
static constexpr uint8_t  CH_VBAT    = 14;
static constexpr uint32_t EN_VREFINT = ADC_CCR_VREFEN;    // enable bits in ADC->CCR
static constexpr uint32_t EN_TEMP    = ADC_CCR_TSEN;
static constexpr uint32_t EN_VBAT    = ADC_CCR_VBATEN;
#elif defined(STM32F4) || defined(STM32F7)
static constexpr uint32_t EN_VREFINT = ADC_CCR_TSVREFE;   // enable bits in ADC->CCR
static constexpr uint32_t EN_TEMP    = ADC_CCR_TSVREFE;   // one bit for both
static constexpr uint32_t EN_VBAT    = ADC_CCR_VBATE;
static constexpr uint8_t  CH_VREFINT = 17;
static constexpr uint8_t  CH_VBAT    = 18;
#if defined(STM32F401xC) || defined(STM32F401xE) || defined(STM32F405xx) || defined(STM32F407xx) || \
    defined(STM32F410Cx) || defined(STM32F410Rx) || defined(STM32F410Tx) || defined(STM32F415xx) || defined(STM32F417xx)
static constexpr uint8_t  CH_TEMP    = 16;
#else
static constexpr uint8_t  CH_TEMP    = 18;   // shared with VBAT
#endif
#endif

// ---------------------------------------------------------------------------
// EXTSEL value of the external trigger "EXTI line 11" (AttachExtTrig()).
// Timer triggers come with the timer: TIM_TriggerGenerator::Req::Adc.
// Verified against the ST LL headers (LL_ADC_REG_TRIG_EXT_EXTI_LINE11).
// ---------------------------------------------------------------------------
#if defined(STM32G0)
static constexpr uint8_t EXTSEL_EXTI11 = 7;    // CFGR1.EXTSEL[2:0]
#else
static constexpr uint8_t EXTSEL_EXTI11 = 15;   // CR2.EXTSEL[3:0]
#endif

// ---------------------------------------------------------------------------
// Sampling time. The same SetUp() / AddChannel() signatures on every family;
// what the enums hold follows the hardware:
//
//   SMPL    — SetUp(smp1, smp2).
//             G0: the two sampling times SMP1/SMP2 of the whole ADC.
//             F4/F7: stub — the sampling time is per channel, SetUp()
//             ignores these arguments.
//   CH_SMPL — AddChannel(pin, smpl).
//             G0: which of SMP1/SMP2 the channel uses (SMPSELx).
//             F4/F7: the channel's own sampling time (SMPRx).
//
// Values map directly to the register fields.
// ---------------------------------------------------------------------------
#if defined(STM32G0)

enum class SMPL : uint8_t {
	CYC_1_5   = 0,
	CYC_3_5   = 1,
	CYC_7_5   = 2,
	CYC_12_5  = 3,
	CYC_19_5  = 4,
	CYC_39_5  = 5,
	CYC_79_5  = 6,
	CYC_160_5 = 7,
	DEFAULT   = CYC_12_5,
};

enum class CH_SMPL : uint8_t {
	SMP1    = 0,
	SMP2    = 1,
	DEFAULT = SMP1,
};

#elif defined(STM32F4) || defined(STM32F7)

enum class SMPL : uint8_t {
	DEFAULT = 0,   ///< stub: SetUp() ignores it, see CH_SMPL
};

enum class CH_SMPL : uint8_t {
	CYC_3   = 0,
	CYC_15  = 1,
	CYC_28  = 2,
	CYC_56  = 3,
	CYC_84  = 4,
	CYC_112 = 5,
	CYC_144 = 6,
	CYC_480 = 7,
	DEFAULT = CYC_15,
};

#endif

// ---------------------------------------------------------------------------
// Per-family channel pin tables
//
// Naming convention:  ADC_N::IN::Pyz    (G0, single ADC)
//                     ADC_N::_1::Pyz    (F4/F7, ADC1)
//                     ADC_N::_2::Pyz    (F4/F7, ADC2)
//                     ADC_N::_3::Pyz    (F4/F7, ADC3)
//
// Channel numbers verified against the STM32CubeMX MCU database (all G0 and
// F7 part numbers); F4 and F7 share the same mapping. A pin is usable only
// if it is bonded out in the chosen package.
// ---------------------------------------------------------------------------

#if defined(STM32G0)

/// External ADC1 channels for all STM32G0 lines.
struct IN {
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
	static constexpr ADC_PIN PB10 = { GPIOB_BASE, 10, 11, ADC1_BASE };   ///< 48+ pin packages
	static constexpr ADC_PIN PB11 = { GPIOB_BASE, 11, 15, ADC1_BASE };   ///< 48+ pin packages
	static constexpr ADC_PIN PB12 = { GPIOB_BASE, 12, 16, ADC1_BASE };   ///< 48+ pin packages
#if defined(STM32G030xx) || defined(STM32G031xx) || defined(STM32G041xx) || \
    defined(STM32G050xx) || defined(STM32G051xx) || defined(STM32G061xx)
	// Alternatives for IN11/IN15/IN16 in packages without PB10-PB12 (≤ 32 pins)
	static constexpr ADC_PIN PB7  = { GPIOB_BASE,  7, 11, ADC1_BASE };
	static constexpr ADC_PIN PA11 = { GPIOA_BASE, 11, 15, ADC1_BASE };
	static constexpr ADC_PIN PA12 = { GPIOA_BASE, 12, 16, ADC1_BASE };
	// SWDIO / SWCLK — using them as analog inputs disables debugging
	static constexpr ADC_PIN PA13 = { GPIOA_BASE, 13, 17, ADC1_BASE };
	static constexpr ADC_PIN PA14 = { GPIOA_BASE, 14, 18, ADC1_BASE };
#else
	// G07x/G08x/G0Bx/G0Cx, 64+ pin packages
	static constexpr ADC_PIN PC4  = { GPIOC_BASE,  4, 17, ADC1_BASE };
	static constexpr ADC_PIN PC5  = { GPIOC_BASE,  5, 18, ADC1_BASE };
#endif
};

#elif defined(STM32F4) || defined(STM32F7)

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

#if defined(ADC2_BASE)
/// ADC2 — same GPIO pins as ADC1, different adc_base.
/// Absent on F401/F410/F411/F412/F413/F423.
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
#endif  // ADC2_BASE

#if defined(ADC3_BASE)
/// ADC3 — PA0-PA3, PC0-PC3 shared with ADC1/2, the rest on PORTF.
struct _3 {
	static constexpr ADC_PIN PA0  = { GPIOA_BASE,  0,  0, ADC3_BASE };
	static constexpr ADC_PIN PA1  = { GPIOA_BASE,  1,  1, ADC3_BASE };
	static constexpr ADC_PIN PA2  = { GPIOA_BASE,  2,  2, ADC3_BASE };
	static constexpr ADC_PIN PA3  = { GPIOA_BASE,  3,  3, ADC3_BASE };
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
#endif  // ADC3_BASE

#endif  // STM32 family

#endif  // Section A: defined(ADC_HPP_) && !defined(ADC_DEFS_CPP)


// ===========================================================================
// Section B: Peripheral-info table — adc.cpp file scope
// ===========================================================================
#ifdef ADC_DEFS_CPP

#if defined(STM32G0)
// G051/G061/G071/G081/G0B1/G0C1 (devices with COMP) share the line with COMP.
#if defined(COMP1_BASE)
#  define ADC_N_G0_IRQN  ADC1_COMP_IRQn
#else
#  define ADC_N_G0_IRQN  ADC1_IRQn
#endif
const ADC_N::PeriphInfo ADC_N::adc_table[] = {
	{ ADC1, &RCC->APBENR2, RCC_APBENR2_ADCEN, ADC_N_G0_IRQN, DMA_Sx::Req::Adc1::RX }
};
#undef ADC_N_G0_IRQN
#elif defined(STM32F4) || defined(STM32F7)
// Primary and alternate hardware routes, including the stream and CHSEL.
const ADC_N::PeriphInfo ADC_N::adc_table[] = {
	{ ADC1, &RCC->APB2ENR, RCC_APB2ENR_ADC1EN, ADC_IRQn, { DMA_Sx::Req::Adc1::RX, DMA_Sx::Req::Adc1::RX_alt } },
#if defined(ADC2_BASE)
	{ ADC2, &RCC->APB2ENR, RCC_APB2ENR_ADC2EN, ADC_IRQn, { DMA_Sx::Req::Adc2::RX, DMA_Sx::Req::Adc2::RX_alt } },
#endif
#if defined(ADC3_BASE)
	{ ADC3, &RCC->APB2ENR, RCC_APB2ENR_ADC3EN, ADC_IRQn, { DMA_Sx::Req::Adc3::RX, DMA_Sx::Req::Adc3::RX_alt } },
#endif
};
#endif

#endif  // ADC_DEFS_CPP (Section B)
