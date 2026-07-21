/**
 * @file    tim_defs.hpp
 * @brief   All compile-time and run-time tables for the TIM driver.
 *
 * Two sections, each activated by a different preprocessor symbol.
 *
 * Section A — TIM_PIN struct + channel pin tables
 * -------------------------------------------------
 * Guard:  TIM_HPP_  (include-guard of tim.hpp, already defined when
 *                    "#include "tim_defs.hpp"" is reached inside the class body)
 *         also !TIM_DEFS_CPP to prevent re-emission at file scope.
 * Scope:  TIM class body — public nested types.
 * Result: TIM_PIN, TIM::_1::CH1::PA8, TIM::_3::CH3::PB0, etc.
 *
 * Section B — peripheral-info table (tim_table)
 * -----------------------------------------------
 * Guard:  TIM_DEFS_CPP  (defined in tim.cpp just before the include)
 * Scope:  file scope inside tim.cpp.
 * Result: TIM::tim_table[] is defined (replaces the inline #if block).
 *
 * Usage
 * -----
 * tim.hpp (inside class body — TIM_HPP_ already defined):
 * @code
 *   #include "tim_defs.hpp"   // Section A: TIM_PIN + channel pin tables
 * @endcode
 *
 * tim.cpp (after #include "tim.hpp"):
 * @code
 *   #define TIM_DEFS_CPP
 *   #include "tim_defs.hpp"   // Section B: tim_table
 *   #undef  TIM_DEFS_CPP
 * @endcode
 */

// ===========================================================================
// Section A: TIM_PIN struct + channel pin tables — TIM class body
// ===========================================================================
#if defined(TIM_HPP_) && !defined(TIM_DEFS_CPP)

// ---------------------------------------------------------------------------
// TIM_PIN — a GPIO pin that knows which timer channel it belongs to.
//
// Fields:
//   port     — GPIO port base address (GPIOA_BASE, GPIOB_BASE, …)
//   pin      — pin number within the port (0-15)
//   af       — alternate-function index for this pin/timer combination
//   channel  — 0-indexed TIM channel (0=CH1, 1=CH2, 2=CH3, 3=CH4)
//   tim_base — TIM peripheral base (TIM1_BASE, TIM3_BASE, …)
//
// Because channel is encoded in the struct, TIM_PWM can accept pins in any
// order and assign them to the correct CCMRx/CCERx fields automatically.
// Zero RAM footprint — all instances are constexpr in flash.
// ---------------------------------------------------------------------------
struct TIM_PIN {
	uint32_t port     = 0;
	uint8_t  pin      = 0;
	uint8_t  af       = 0;
	uint8_t  channel  = 0;   ///< 0 = CH1, 1 = CH2, 2 = CH3, 3 = CH4
	uint32_t tim_base = 0;

	constexpr bool IsValid() const { return port != 0; }
};

// ---------------------------------------------------------------------------
// Per-timer, per-channel pin tables (STM32G0)
//
// Naming convention:  TIM::_N::CHx::Pyz
//   N   — timer number (1, 3, 14, 16, 17)
//   CHx — channel (CH1-CH4)
//   Pyz — GPIO pin name (e.g. PA8, PB0)
// ---------------------------------------------------------------------------

#if defined(STM32G0)

struct _1 {
	// TIM1 — advanced timer, APBENR2, AF=2 for all G0 pins
	struct CH1 {
		static constexpr TIM_PIN PA8  = { GPIOA_BASE,  8, 2, 0, TIM1_BASE };
	};
	struct CH2 {
		static constexpr TIM_PIN PA9  = { GPIOA_BASE,  9, 2, 1, TIM1_BASE };
		static constexpr TIM_PIN PB3  = { GPIOB_BASE,  3, 1, 1, TIM1_BASE };
	};
	struct CH3 {
		static constexpr TIM_PIN PA10 = { GPIOA_BASE, 10, 2, 2, TIM1_BASE };
	};
	struct CH4 {
		static constexpr TIM_PIN PA11 = { GPIOA_BASE, 11, 2, 3, TIM1_BASE };
	};
};

struct _3 {
	// TIM3 — general-purpose, APBENR1, AF=1 for all G0 pins
	struct CH1 {
		static constexpr TIM_PIN PA6  = { GPIOA_BASE,  6, 1, 0, TIM3_BASE };
		static constexpr TIM_PIN PB4  = { GPIOB_BASE,  4, 1, 0, TIM3_BASE };
		static constexpr TIM_PIN PC6  = { GPIOC_BASE,  6, 0, 0, TIM3_BASE };
	};
	struct CH2 {
		static constexpr TIM_PIN PA7  = { GPIOA_BASE,  7, 1, 1, TIM3_BASE };
		static constexpr TIM_PIN PB5  = { GPIOB_BASE,  5, 1, 1, TIM3_BASE };
		static constexpr TIM_PIN PC7  = { GPIOC_BASE,  7, 0, 1, TIM3_BASE };
	};
	struct CH3 {
		static constexpr TIM_PIN PB0  = { GPIOB_BASE,  0, 1, 2, TIM3_BASE };
		static constexpr TIM_PIN PC8  = { GPIOC_BASE,  8, 0, 2, TIM3_BASE };
	};
	struct CH4 {
		static constexpr TIM_PIN PB1  = { GPIOB_BASE,  1, 1, 3, TIM3_BASE };
		static constexpr TIM_PIN PC9  = { GPIOC_BASE,  9, 0, 3, TIM3_BASE };
	};
};

#ifdef TIM14_BASE
struct _14 {
	// TIM14 — basic, APBENR2, single CH1
	struct CH1 {
		static constexpr TIM_PIN PA4  = { GPIOA_BASE,  4, 4, 0, TIM14_BASE };
		static constexpr TIM_PIN PA7  = { GPIOA_BASE,  7, 4, 0, TIM14_BASE };
		static constexpr TIM_PIN PB1  = { GPIOB_BASE,  1, 0, 0, TIM14_BASE };
	};
};
#endif

#ifdef TIM16_BASE
struct _16 {
	// TIM16 — semi-advanced, APBENR2, single CH1 with BDTR
	struct CH1 {
		static constexpr TIM_PIN PA6  = { GPIOA_BASE,  6, 5, 0, TIM16_BASE };
		static constexpr TIM_PIN PB8  = { GPIOB_BASE,  8, 2, 0, TIM16_BASE };
	};
};
#endif

#ifdef TIM17_BASE
struct _17 {
	// TIM17 — semi-advanced, APBENR2, single CH1 with BDTR
	struct CH1 {
		static constexpr TIM_PIN PA7  = { GPIOA_BASE,  7, 5, 0, TIM17_BASE };
		static constexpr TIM_PIN PB9  = { GPIOB_BASE,  9, 2, 0, TIM17_BASE };
	};
};
#endif

// ---------------------------------------------------------------------------
// STM32F4 / STM32F7
// ---------------------------------------------------------------------------
#elif defined(STM32F4) || defined(STM32F7)

struct _1 {
	struct CH1 {
		static constexpr TIM_PIN PA8  = { GPIOA_BASE,  8, 1, 0, TIM1_BASE };
		static constexpr TIM_PIN PE9  = { GPIOE_BASE,  9, 1, 0, TIM1_BASE };
	};
	struct CH2 {
		static constexpr TIM_PIN PA9  = { GPIOA_BASE,  9, 1, 1, TIM1_BASE };
		static constexpr TIM_PIN PE11 = { GPIOE_BASE, 11, 1, 1, TIM1_BASE };
	};
	struct CH3 {
		static constexpr TIM_PIN PA10 = { GPIOA_BASE, 10, 1, 2, TIM1_BASE };
		static constexpr TIM_PIN PE13 = { GPIOE_BASE, 13, 1, 2, TIM1_BASE };
	};
	struct CH4 {
		static constexpr TIM_PIN PA11 = { GPIOA_BASE, 11, 1, 3, TIM1_BASE };
		static constexpr TIM_PIN PE14 = { GPIOE_BASE, 14, 1, 3, TIM1_BASE };
	};
};

struct _2 {
	struct CH1 {
		static constexpr TIM_PIN PA0  = { GPIOA_BASE,  0, 1, 0, TIM2_BASE };
		static constexpr TIM_PIN PA5  = { GPIOA_BASE,  5, 1, 0, TIM2_BASE };
		static constexpr TIM_PIN PA15 = { GPIOA_BASE, 15, 1, 0, TIM2_BASE };
	};
	struct CH2 {
		static constexpr TIM_PIN PA1  = { GPIOA_BASE,  1, 1, 1, TIM2_BASE };
		static constexpr TIM_PIN PB3  = { GPIOB_BASE,  3, 1, 1, TIM2_BASE };
	};
	struct CH3 {
		static constexpr TIM_PIN PA2  = { GPIOA_BASE,  2, 1, 2, TIM2_BASE };
		static constexpr TIM_PIN PB10 = { GPIOB_BASE, 10, 1, 2, TIM2_BASE };
	};
	struct CH4 {
		static constexpr TIM_PIN PA3  = { GPIOA_BASE,  3, 1, 3, TIM2_BASE };
		static constexpr TIM_PIN PB11 = { GPIOB_BASE, 11, 1, 3, TIM2_BASE };
	};
};

struct _3 {
	struct CH1 {
		static constexpr TIM_PIN PA6  = { GPIOA_BASE,  6, 2, 0, TIM3_BASE };
		static constexpr TIM_PIN PB4  = { GPIOB_BASE,  4, 2, 0, TIM3_BASE };
		static constexpr TIM_PIN PC6  = { GPIOC_BASE,  6, 2, 0, TIM3_BASE };
	};
	struct CH2 {
		static constexpr TIM_PIN PA7  = { GPIOA_BASE,  7, 2, 1, TIM3_BASE };
		static constexpr TIM_PIN PB5  = { GPIOB_BASE,  5, 2, 1, TIM3_BASE };
		static constexpr TIM_PIN PC7  = { GPIOC_BASE,  7, 2, 1, TIM3_BASE };
	};
	struct CH3 {
		static constexpr TIM_PIN PB0  = { GPIOB_BASE,  0, 2, 2, TIM3_BASE };
		static constexpr TIM_PIN PC8  = { GPIOC_BASE,  8, 2, 2, TIM3_BASE };
	};
	struct CH4 {
		static constexpr TIM_PIN PB1  = { GPIOB_BASE,  1, 2, 3, TIM3_BASE };
		static constexpr TIM_PIN PC9  = { GPIOC_BASE,  9, 2, 3, TIM3_BASE };
	};
};

struct _4 {
	struct CH1 {
		static constexpr TIM_PIN PB6  = { GPIOB_BASE,  6, 2, 0, TIM4_BASE };
		static constexpr TIM_PIN PD12 = { GPIOD_BASE, 12, 2, 0, TIM4_BASE };
	};
	struct CH2 {
		static constexpr TIM_PIN PB7  = { GPIOB_BASE,  7, 2, 1, TIM4_BASE };
		static constexpr TIM_PIN PD13 = { GPIOD_BASE, 13, 2, 1, TIM4_BASE };
	};
	struct CH3 {
		static constexpr TIM_PIN PB8  = { GPIOB_BASE,  8, 2, 2, TIM4_BASE };
		static constexpr TIM_PIN PD14 = { GPIOD_BASE, 14, 2, 2, TIM4_BASE };
	};
	struct CH4 {
		static constexpr TIM_PIN PB9  = { GPIOB_BASE,  9, 2, 3, TIM4_BASE };
		static constexpr TIM_PIN PD15 = { GPIOD_BASE, 15, 2, 3, TIM4_BASE };
	};
};

#ifdef TIM8_BASE
struct _8 {
	struct CH1 {
		static constexpr TIM_PIN PC6  = { GPIOC_BASE,  6, 3, 0, TIM8_BASE };
	};
	struct CH2 {
		static constexpr TIM_PIN PC7  = { GPIOC_BASE,  7, 3, 1, TIM8_BASE };
	};
	struct CH3 {
		static constexpr TIM_PIN PC8  = { GPIOC_BASE,  8, 3, 2, TIM8_BASE };
	};
	struct CH4 {
		static constexpr TIM_PIN PC9  = { GPIOC_BASE,  9, 3, 3, TIM8_BASE };
	};
};
#endif

#endif  // STM32 family

#endif  // Section A: defined(TIM_HPP_) && !defined(TIM_DEFS_CPP)

// ===========================================================================
// Section B: Peripheral-info table — tim.cpp file scope
// ===========================================================================
#ifdef TIM_DEFS_CPP

#if defined(STM32G0)
const TIM::PeriphInfo TIM::tim_table[] = {
	{ TIM1,  &RCC->APBENR2, RCC_APBENR2_TIM1EN,  &System::TIMxAPB1Clock,
	  TIM1_BRK_UP_TRG_COM_IRQn, TIM1_CC_IRQn,  true,  0xFFFF, 2, DMA_Sx::Req::Tim1::UP.ch  },
	{ TIM3,  &RCC->APBENR1, RCC_APBENR1_TIM3EN,  &System::TIMxAPB1Clock,
	  TIM3_IRQn,              TIM3_IRQn,         false, 0xFFFF, 1, DMA_Sx::Req::Tim3::UP.ch  },
	{ TIM14, &RCC->APBENR2, RCC_APBENR2_TIM14EN, &System::TIMxAPB1Clock,
	  TIM14_IRQn,             TIM14_IRQn,        false, 0xFFFF, 4, 0                          },
	{ TIM16, &RCC->APBENR2, RCC_APBENR2_TIM16EN, &System::TIMxAPB1Clock,
	  TIM16_IRQn,             TIM16_IRQn,        true,  0xFFFF, 2, DMA_Sx::Req::Tim16::UP.ch },
	{ TIM17, &RCC->APBENR2, RCC_APBENR2_TIM17EN, &System::TIMxAPB1Clock,
	  TIM17_IRQn,             TIM17_IRQn,        true,  0xFFFF, 2, DMA_Sx::Req::Tim17::UP.ch },
};

// TODO: verify against RM0444 "TIMx internal trigger connection" before use.
// Only TIM1/TIM3 have an SMCR on G0; itr=0xFF marks these as unverified so
// TIM_HWCounter::SetUp() refuses them rather than risk a silently wrong route.
const TIM::ITR_Route TIM::itr_table[] = {
	{ TIM3, TIM1, 0xFF },
	{ TIM1, TIM3, 0xFF },
};
#elif defined(STM32F4)
const TIM::PeriphInfo TIM::tim_table[] = {
	{ TIM1,  &RCC->APB2ENR, RCC_APB2ENR_TIM1EN,  &System::TIMxAPB2Clock,
	  TIM1_UP_TIM10_IRQn,      TIM1_CC_IRQn,               true,  0xFFFF,     1, 0 },
	{ TIM2,  &RCC->APB1ENR, RCC_APB1ENR_TIM2EN,  &System::TIMxAPB1Clock,
	  TIM2_IRQn,               TIM2_IRQn,                  false, 0xFFFFFFFF, 1, 0 },
	{ TIM3,  &RCC->APB1ENR, RCC_APB1ENR_TIM3EN,  &System::TIMxAPB1Clock,
	  TIM3_IRQn,               TIM3_IRQn,                  false, 0xFFFF,     2, 0 },
	{ TIM4,  &RCC->APB1ENR, RCC_APB1ENR_TIM4EN,  &System::TIMxAPB1Clock,
	  TIM4_IRQn,               TIM4_IRQn,                  false, 0xFFFF,     2, 0 },
	{ TIM5,  &RCC->APB1ENR, RCC_APB1ENR_TIM5EN,  &System::TIMxAPB1Clock,
	  TIM5_IRQn,               TIM5_IRQn,                  false, 0xFFFFFFFF, 2, 0 },
	{ TIM6,  &RCC->APB1ENR, RCC_APB1ENR_TIM6EN,  &System::TIMxAPB1Clock,
	  TIM6_DAC_IRQn,           TIM6_DAC_IRQn,              false, 0xFFFF,     0, 0 },
	{ TIM7,  &RCC->APB1ENR, RCC_APB1ENR_TIM7EN,  &System::TIMxAPB1Clock,
	  TIM7_IRQn,               TIM7_IRQn,                  false, 0xFFFF,     0, 0 },
	{ TIM8,  &RCC->APB2ENR, RCC_APB2ENR_TIM8EN,  &System::TIMxAPB2Clock,
	  TIM8_UP_TIM13_IRQn,      TIM8_CC_IRQn,               true,  0xFFFF,     3, 0 },
	{ TIM9,  &RCC->APB2ENR, RCC_APB2ENR_TIM9EN,  &System::TIMxAPB2Clock,
	  TIM1_BRK_TIM9_IRQn,      TIM1_BRK_TIM9_IRQn,         false, 0xFFFF,     3, 0 },
	{ TIM10, &RCC->APB2ENR, RCC_APB2ENR_TIM10EN, &System::TIMxAPB2Clock,
	  TIM1_UP_TIM10_IRQn,      TIM1_UP_TIM10_IRQn,         false, 0xFFFF,     3, 0 },
	{ TIM11, &RCC->APB2ENR, RCC_APB2ENR_TIM11EN, &System::TIMxAPB2Clock,
	  TIM1_TRG_COM_TIM11_IRQn, TIM1_TRG_COM_TIM11_IRQn,   false, 0xFFFF,     3, 0 },
	{ TIM12, &RCC->APB1ENR, RCC_APB1ENR_TIM12EN, &System::TIMxAPB1Clock,
	  TIM8_BRK_TIM12_IRQn,     TIM8_BRK_TIM12_IRQn,        false, 0xFFFF,     9, 0 },
	{ TIM13, &RCC->APB1ENR, RCC_APB1ENR_TIM13EN, &System::TIMxAPB1Clock,
	  TIM8_UP_TIM13_IRQn,      TIM8_UP_TIM13_IRQn,         false, 0xFFFF,     9, 0 },
	{ TIM14, &RCC->APB1ENR, RCC_APB1ENR_TIM14EN, &System::TIMxAPB1Clock,
	  TIM8_TRG_COM_TIM14_IRQn, TIM8_TRG_COM_TIM14_IRQn,   false, 0xFFFF,     9, 0 },
};

// RM0090 "TIMx internal trigger connection" — TIM1/2/3/4/5/8 (the only ones
// with a full slave-mode controller). TIM9-14 are omitted: on F4 they either
// lack an SMCR or their ITR sources are OC outputs, not TRGO, so they don't
// fit this master-TRGO -> slave-TS scheme.
const TIM::ITR_Route TIM::itr_table[] = {
	// slave TIM1
	{ TIM5, TIM1, 0 }, { TIM2, TIM1, 1 }, { TIM3, TIM1, 2 }, { TIM4, TIM1, 3 },
	// slave TIM2
	{ TIM1, TIM2, 0 }, { TIM8, TIM2, 1 }, { TIM3, TIM2, 2 }, { TIM4, TIM2, 3 },
	// slave TIM3
	{ TIM1, TIM3, 0 }, { TIM2, TIM3, 1 }, { TIM5, TIM3, 2 }, { TIM4, TIM3, 3 },
	// slave TIM4
	{ TIM1, TIM4, 0 }, { TIM2, TIM4, 1 }, { TIM3, TIM4, 2 }, { TIM8, TIM4, 3 },
	// slave TIM5
	{ TIM2, TIM5, 0 }, { TIM3, TIM5, 1 }, { TIM4, TIM5, 2 }, { TIM8, TIM5, 3 },
	// slave TIM8
	{ TIM1, TIM8, 0 }, { TIM2, TIM8, 1 }, { TIM4, TIM8, 2 }, { TIM5, TIM8, 3 },
};
#endif

#endif  // TIM_DEFS_CPP (Section B)
