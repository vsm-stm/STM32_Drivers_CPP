/**
 * @file    tim_defs.hpp
 * @brief   All compile-time and run-time tables for the TIM driver.
 *
 * Two sections, each activated by a different preprocessor symbol.
 *
 * Section A — channel pin tables
 * -------------------------------------------------
 * Guard:  TIM_HPP_  (include-guard of tim.hpp, already defined when
 *                    "#include "tim_defs.hpp"" is reached inside the class body)
 *         also !TIM_DEFS_CPP to prevent re-emission at file scope.
 * Scope:  TIM class body — public nested types.
 * Result: TIM::_1::CH1::PA8, TIM::_3::CH3::PB0, etc.
 * TIM_PIN is defined before TIM in tim.hpp, with a public TIM::TIM_PIN alias.
 *
 * Section B — peripheral-info table (tim_table)
 * -----------------------------------------------
 * Guard:  TIM_DEFS_CPP  (defined in tim.cpp just before the include)
 * Scope:  file scope inside tim.cpp.
 * Result: TIM::tim_table[], tim_dma_routes[] and TIM::itr_table[] are defined.
 *
 * Usage
 * -----
 * tim.hpp (inside class body — TIM_HPP_ already defined):
 * @code
 *   #include "tim_defs.hpp"   // Section A: channel pin tables
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
// Section A: channel pin tables — TIM class body
// ===========================================================================
#if defined(TIM_HPP_) && !defined(TIM_DEFS_CPP)

// ---------------------------------------------------------------------------
// Per-timer, per-channel pin tables (STM32G0)
//
// Naming convention:  TIM::_N::CHx::Pyz
//   N   — timer number (1, 3, 14, 16, 17)
//   CHx — channel (CH1-CH4)
//   Pyz — GPIO pin name (e.g. PA8, PB0)
// ---------------------------------------------------------------------------

#if defined(STM32G0)

#if defined(TIM1_BASE)
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
#endif

#if defined(TIM3_BASE)
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
#endif

#if defined(TIM14_BASE)
struct _14 {
	// TIM14 — basic, APBENR2, single CH1
	struct CH1 {
		static constexpr TIM_PIN PA4  = { GPIOA_BASE,  4, 4, 0, TIM14_BASE };
		static constexpr TIM_PIN PA7  = { GPIOA_BASE,  7, 4, 0, TIM14_BASE };
		static constexpr TIM_PIN PB1  = { GPIOB_BASE,  1, 0, 0, TIM14_BASE };
	};
};
#endif

#if defined(TIM16_BASE)
struct _16 {
	// TIM16 — semi-advanced, APBENR2, single CH1 with BDTR
	struct CH1 {
		static constexpr TIM_PIN PA6  = { GPIOA_BASE,  6, 5, 0, TIM16_BASE };
		static constexpr TIM_PIN PB8  = { GPIOB_BASE,  8, 2, 0, TIM16_BASE };
	};
};
#endif

#if defined(TIM17_BASE)
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

#if defined(TIM1_BASE)
struct _1 {
	struct CH1 {
		static constexpr TIM_PIN PA8  = { GPIOA_BASE,  8, 1, 0, TIM1_BASE };
#if defined(GPIOE_BASE)
		static constexpr TIM_PIN PE9  = { GPIOE_BASE,  9, 1, 0, TIM1_BASE };
#endif
	};
	struct CH2 {
		static constexpr TIM_PIN PA9  = { GPIOA_BASE,  9, 1, 1, TIM1_BASE };
#if defined(GPIOE_BASE)
		static constexpr TIM_PIN PE11 = { GPIOE_BASE, 11, 1, 1, TIM1_BASE };
#endif
	};
	struct CH3 {
		static constexpr TIM_PIN PA10 = { GPIOA_BASE, 10, 1, 2, TIM1_BASE };
#if defined(GPIOE_BASE)
		static constexpr TIM_PIN PE13 = { GPIOE_BASE, 13, 1, 2, TIM1_BASE };
#endif
	};
	struct CH4 {
		static constexpr TIM_PIN PA11 = { GPIOA_BASE, 11, 1, 3, TIM1_BASE };
#if defined(GPIOE_BASE)
		static constexpr TIM_PIN PE14 = { GPIOE_BASE, 14, 1, 3, TIM1_BASE };
#endif
	};
};
#endif

#if defined(TIM2_BASE)
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
#endif

#if defined(TIM3_BASE)
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
#endif

#if defined(TIM4_BASE)
struct _4 {
	struct CH1 {
		static constexpr TIM_PIN PB6  = { GPIOB_BASE,  6, 2, 0, TIM4_BASE };
#if defined(GPIOD_BASE)
		static constexpr TIM_PIN PD12 = { GPIOD_BASE, 12, 2, 0, TIM4_BASE };
#endif
	};
	struct CH2 {
		static constexpr TIM_PIN PB7  = { GPIOB_BASE,  7, 2, 1, TIM4_BASE };
#if defined(GPIOD_BASE)
		static constexpr TIM_PIN PD13 = { GPIOD_BASE, 13, 2, 1, TIM4_BASE };
#endif
	};
	struct CH3 {
		static constexpr TIM_PIN PB8  = { GPIOB_BASE,  8, 2, 2, TIM4_BASE };
#if defined(GPIOD_BASE)
		static constexpr TIM_PIN PD14 = { GPIOD_BASE, 14, 2, 2, TIM4_BASE };
#endif
	};
	struct CH4 {
		static constexpr TIM_PIN PB9  = { GPIOB_BASE,  9, 2, 3, TIM4_BASE };
#if defined(GPIOD_BASE)
		static constexpr TIM_PIN PD15 = { GPIOD_BASE, 15, 2, 3, TIM4_BASE };
#endif
	};
};
#endif

// Existing AF routes are retained. Select a pin bonded out in the chosen package.
#if defined(TIM5_BASE)
struct _5 {
	// TIM5 — general-purpose, 32-bit ARR, AF2. PA0-PA3 are shared with TIM2
	// (AF1) on the same pins. Also available on PH10-12/PI0-3 on 176-pin
	// packages; omitted here since those ports aren't fully bonded out on
	// smaller packages (e.g. LQFP144 such as STM32F429ZI).
	struct CH1 {
		static constexpr TIM_PIN PA0  = { GPIOA_BASE,  0, 2, 0, TIM5_BASE };
	};
	struct CH2 {
		static constexpr TIM_PIN PA1  = { GPIOA_BASE,  1, 2, 1, TIM5_BASE };
	};
	struct CH3 {
		static constexpr TIM_PIN PA2  = { GPIOA_BASE,  2, 2, 2, TIM5_BASE };
	};
	struct CH4 {
		static constexpr TIM_PIN PA3  = { GPIOA_BASE,  3, 2, 3, TIM5_BASE };
	};
};
#endif

#if defined(TIM9_BASE)
struct _9 {
	// TIM9 — general-purpose, 2 channels, AF3.
	struct CH1 {
		static constexpr TIM_PIN PA2  = { GPIOA_BASE,  2, 3, 0, TIM9_BASE };
#if defined(GPIOE_BASE)
		static constexpr TIM_PIN PE5  = { GPIOE_BASE,  5, 3, 0, TIM9_BASE };
#endif
	};
	struct CH2 {
		static constexpr TIM_PIN PA3  = { GPIOA_BASE,  3, 3, 1, TIM9_BASE };
#if defined(GPIOE_BASE)
		static constexpr TIM_PIN PE6  = { GPIOE_BASE,  6, 3, 1, TIM9_BASE };
#endif
	};
};
#endif

#if defined(TIM10_BASE)
struct _10 {
	// TIM10 — general-purpose, single channel, AF3.
	struct CH1 {
		static constexpr TIM_PIN PB8  = { GPIOB_BASE,  8, 3, 0, TIM10_BASE };
#if defined(GPIOF_BASE)
		static constexpr TIM_PIN PF6  = { GPIOF_BASE,  6, 3, 0, TIM10_BASE };
#endif
	};
};
#endif

#if defined(TIM11_BASE)
struct _11 {
	// TIM11 — general-purpose, single channel, AF3.
	struct CH1 {
		static constexpr TIM_PIN PB9  = { GPIOB_BASE,  9, 3, 0, TIM11_BASE };
#if defined(GPIOF_BASE)
		static constexpr TIM_PIN PF7  = { GPIOF_BASE,  7, 3, 0, TIM11_BASE };
#endif
	};
};
#endif

#if defined(TIM12_BASE)
struct _12 {
	// TIM12 — general-purpose, 2 channels, AF9.
	struct CH1 {
		static constexpr TIM_PIN PB14 = { GPIOB_BASE, 14, 9, 0, TIM12_BASE };
#if defined(GPIOH_BASE)
		static constexpr TIM_PIN PH6  = { GPIOH_BASE,  6, 9, 0, TIM12_BASE };
#endif
	};
	struct CH2 {
		static constexpr TIM_PIN PB15 = { GPIOB_BASE, 15, 9, 1, TIM12_BASE };
#if defined(GPIOH_BASE)
		static constexpr TIM_PIN PH9  = { GPIOH_BASE,  9, 9, 1, TIM12_BASE };
#endif
	};
};
#endif

#if defined(TIM13_BASE)
struct _13 {
	// TIM13 — general-purpose, single channel, AF9.
	struct CH1 {
		static constexpr TIM_PIN PA6  = { GPIOA_BASE,  6, 9, 0, TIM13_BASE };
#if defined(GPIOF_BASE)
		static constexpr TIM_PIN PF8  = { GPIOF_BASE,  8, 9, 0, TIM13_BASE };
#endif
	};
};
#endif

#if defined(TIM8_BASE)
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
#define TIM_INFO(dev, bus, up, cc, bdtr, channels, width) \
	{ dev, &RCC->APBENR##bus, RCC_APBENR##bus##_##dev##EN, \
	  &RCC->APBRSTR##bus, RCC_APBRSTR##bus##_##dev##RST, \
	  &System::TIMxAPB1Clock, up, cc, bdtr, channels, width }
#if defined(TIM4_BASE)
#define TIM3_VECTOR TIM3_TIM4_IRQn
#else
#define TIM3_VECTOR TIM3_IRQn
#endif
#if defined(LPTIM1_BASE)
#define TIM6_VECTOR TIM6_DAC_LPTIM1_IRQn
#define TIM7_VECTOR TIM7_LPTIM2_IRQn
#else
#define TIM6_VECTOR TIM6_IRQn
#define TIM7_VECTOR TIM7_IRQn
#endif
#if defined(FDCAN1_BASE)
#define TIM16_VECTOR TIM16_FDCAN_IT0_IRQn
#define TIM17_VECTOR TIM17_FDCAN_IT1_IRQn
#else
#define TIM16_VECTOR TIM16_IRQn
#define TIM17_VECTOR TIM17_IRQn
#endif
const TIM::PeriphInfo TIM::tim_table[] = {
#if defined(TIM1_BASE)
	TIM_INFO(TIM1, 2, TIM1_BRK_UP_TRG_COM_IRQn, TIM1_CC_IRQn, true, 4, 0xFFFFu),
#endif
#if defined(TIM2_BASE)
	TIM_INFO(TIM2, 1, TIM2_IRQn, TIM2_IRQn, false, 4, UINT32_MAX),
#endif
#if defined(TIM3_BASE)
	TIM_INFO(TIM3, 1, TIM3_VECTOR, TIM3_VECTOR, false, 4, 0xFFFFu),
#endif
#if defined(TIM4_BASE)
	TIM_INFO(TIM4, 1, TIM3_VECTOR, TIM3_VECTOR, false, 4, 0xFFFFu),
#endif
#if defined(TIM6_BASE)
	TIM_INFO(TIM6, 1, TIM6_VECTOR, TIM6_VECTOR, false, 0, 0xFFFFu),
#endif
#if defined(TIM7_BASE)
	TIM_INFO(TIM7, 1, TIM7_VECTOR, TIM7_VECTOR, false, 0, 0xFFFFu),
#endif
#if defined(TIM14_BASE)
	TIM_INFO(TIM14, 2, TIM14_IRQn, TIM14_IRQn, false, 1, 0xFFFFu),
#endif
#if defined(TIM15_BASE)
	TIM_INFO(TIM15, 2, TIM15_IRQn, TIM15_IRQn, true, 2, 0xFFFFu),
#endif
#if defined(TIM16_BASE)
	TIM_INFO(TIM16, 2, TIM16_VECTOR, TIM16_VECTOR, true, 1, 0xFFFFu),
#endif
#if defined(TIM17_BASE)
	TIM_INFO(TIM17, 2, TIM17_VECTOR, TIM17_VECTOR, true, 1, 0xFFFFu),
#endif
};
#undef TIM3_VECTOR
#undef TIM6_VECTOR
#undef TIM7_VECTOR
#undef TIM16_VECTOR
#undef TIM17_VECTOR
#undef TIM_INFO

#elif defined(STM32F4) || defined(STM32F7)
#define TIM_INFO(dev, bus, up, cc, bdtr, channels, width) \
	{ dev, &RCC->bus##ENR, RCC_##bus##ENR_##dev##EN, \
	  &RCC->bus##RSTR, RCC_##bus##RSTR_##dev##RST, \
	  &System::TIMx##bus##Clock, up, cc, bdtr, channels, width }
#if defined(TIM10_BASE)
#define TIM1_UP_VECTOR TIM1_UP_TIM10_IRQn
#else
#define TIM1_UP_VECTOR TIM1_UP_IRQn
#endif
#if defined(DAC_BASE) || defined(DAC1_BASE)
#define TIM6_VECTOR TIM6_DAC_IRQn
#else
#define TIM6_VECTOR TIM6_IRQn
#endif
// IRQ/clock/counter capabilities; PWM DMA routes follow this table.
const TIM::PeriphInfo TIM::tim_table[] = {
#if defined(TIM1_BASE)
	TIM_INFO(TIM1, APB2, TIM1_UP_VECTOR, TIM1_CC_IRQn, true, 4, 0xFFFFu),
#endif
#if defined(TIM2_BASE)
	TIM_INFO(TIM2, APB1, TIM2_IRQn, TIM2_IRQn, false, 4, UINT32_MAX),
#endif
#if defined(TIM3_BASE)
	TIM_INFO(TIM3, APB1, TIM3_IRQn, TIM3_IRQn, false, 4, 0xFFFFu),
#endif
#if defined(TIM4_BASE)
	TIM_INFO(TIM4, APB1, TIM4_IRQn, TIM4_IRQn, false, 4, 0xFFFFu),
#endif
#if defined(TIM5_BASE)
	TIM_INFO(TIM5, APB1, TIM5_IRQn, TIM5_IRQn, false, 4, UINT32_MAX),
#endif
#if defined(TIM6_BASE)
	TIM_INFO(TIM6, APB1, TIM6_VECTOR, TIM6_VECTOR, false, 0, 0xFFFFu),
#endif
#if defined(TIM7_BASE)
	TIM_INFO(TIM7, APB1, TIM7_IRQn, TIM7_IRQn, false, 0, 0xFFFFu),
#endif
#if defined(TIM8_BASE)
	TIM_INFO(TIM8, APB2, TIM8_UP_TIM13_IRQn, TIM8_CC_IRQn, true, 4, 0xFFFFu),
#endif
#if defined(TIM9_BASE)
	TIM_INFO(TIM9, APB2, TIM1_BRK_TIM9_IRQn, TIM1_BRK_TIM9_IRQn, false, 2, 0xFFFFu),
#endif
#if defined(TIM10_BASE)
	TIM_INFO(TIM10, APB2, TIM1_UP_TIM10_IRQn, TIM1_UP_TIM10_IRQn, false, 1, 0xFFFFu),
#endif
#if defined(TIM11_BASE)
	TIM_INFO(TIM11, APB2, TIM1_TRG_COM_TIM11_IRQn, TIM1_TRG_COM_TIM11_IRQn, false, 1, 0xFFFFu),
#endif
#if defined(TIM12_BASE)
	TIM_INFO(TIM12, APB1, TIM8_BRK_TIM12_IRQn, TIM8_BRK_TIM12_IRQn, false, 2, 0xFFFFu),
#endif
#if defined(TIM13_BASE)
	TIM_INFO(TIM13, APB1, TIM8_UP_TIM13_IRQn, TIM8_UP_TIM13_IRQn, false, 1, 0xFFFFu),
#endif
#if defined(TIM14_BASE)
	TIM_INFO(TIM14, APB1, TIM8_TRG_COM_TIM14_IRQn, TIM8_TRG_COM_TIM14_IRQn, false, 1, 0xFFFFu),
#endif
};
#undef TIM1_UP_VECTOR
#undef TIM6_VECTOR
#undef TIM_INFO


#endif // STM32 family

#endif // TIM_DEFS_CPP (Section B)

#ifdef TIM_DEFS_CPP
// Separate CC request per channel, selected through DMA_Sx::Req.
struct TimDmaRoute { TIM_TypeDef* periph; uint8_t channel; DMA_Sx::DMAReq request; };
static const TimDmaRoute tim_dma_routes[] = {
#if defined(STM32G0)
#ifdef TIM1_BASE
    { TIM1, 0, DMA_Sx::Req::Tim1::CH1 },
    { TIM1, 1, DMA_Sx::Req::Tim1::CH2 },
    { TIM1, 2, DMA_Sx::Req::Tim1::CH3 },
    { TIM1, 3, DMA_Sx::Req::Tim1::CH4 },
#endif
#ifdef TIM2_BASE
    { TIM2, 0, DMA_Sx::Req::Tim2::CH1 },
    { TIM2, 1, DMA_Sx::Req::Tim2::CH2 },
    { TIM2, 2, DMA_Sx::Req::Tim2::CH3 },
    { TIM2, 3, DMA_Sx::Req::Tim2::CH4 },
#endif
#ifdef TIM3_BASE
    { TIM3, 0, DMA_Sx::Req::Tim3::CH1 },
    { TIM3, 1, DMA_Sx::Req::Tim3::CH2 },
    { TIM3, 2, DMA_Sx::Req::Tim3::CH3 },
    { TIM3, 3, DMA_Sx::Req::Tim3::CH4 },
#endif
#ifdef TIM4_BASE
    { TIM4, 0, DMA_Sx::Req::Tim4::CH1 },
    { TIM4, 1, DMA_Sx::Req::Tim4::CH2 },
    { TIM4, 2, DMA_Sx::Req::Tim4::CH3 },
    { TIM4, 3, DMA_Sx::Req::Tim4::CH4 },
#endif
#ifdef TIM15_BASE
    { TIM15, 0, DMA_Sx::Req::Tim15::CH1 },
    { TIM15, 1, DMA_Sx::Req::Tim15::CH2 },
#endif
#ifdef TIM16_BASE
    { TIM16, 0, DMA_Sx::Req::Tim16::CH1 },
#endif
#ifdef TIM17_BASE
    { TIM17, 0, DMA_Sx::Req::Tim17::CH1 },
#endif
#else
#ifdef TIM1_BASE
    { TIM1, 0, DMA_Sx::Req::Tim1::CH1_DMA2_Stream1 },
    { TIM1, 0, DMA_Sx::Req::Tim1::CH1_DMA2_Stream3 },
    { TIM1, 1, DMA_Sx::Req::Tim1::CH2_DMA2_Stream2 },
    { TIM1, 2, DMA_Sx::Req::Tim1::CH3_DMA2_Stream6 },
    { TIM1, 3, DMA_Sx::Req::Tim1::CH4_DMA2_Stream4 },
#endif
#ifdef TIM2_BASE
    { TIM2, 0, DMA_Sx::Req::Tim2::CH1_DMA1_Stream5 },
    { TIM2, 1, DMA_Sx::Req::Tim2::CH2_DMA1_Stream6 },
    { TIM2, 2, DMA_Sx::Req::Tim2::CH3_DMA1_Stream1 },
    { TIM2, 3, DMA_Sx::Req::Tim2::CH4_DMA1_Stream7 },
#endif
#ifdef TIM3_BASE
    { TIM3, 0, DMA_Sx::Req::Tim3::CH1_DMA1_Stream4 },
    { TIM3, 1, DMA_Sx::Req::Tim3::CH2_DMA1_Stream5 },
    { TIM3, 2, DMA_Sx::Req::Tim3::CH3_DMA1_Stream7 },
    { TIM3, 3, DMA_Sx::Req::Tim3::CH4_DMA1_Stream2 },
#endif
#ifdef TIM4_BASE
    { TIM4, 0, DMA_Sx::Req::Tim4::CH1_DMA1_Stream0 },
    { TIM4, 1, DMA_Sx::Req::Tim4::CH2_DMA1_Stream3 },
    { TIM4, 2, DMA_Sx::Req::Tim4::CH3_DMA1_Stream7 },
#endif
#ifdef TIM5_BASE
    { TIM5, 0, DMA_Sx::Req::Tim5::CH1_DMA1_Stream2 },
    { TIM5, 1, DMA_Sx::Req::Tim5::CH2_DMA1_Stream4 },
    { TIM5, 2, DMA_Sx::Req::Tim5::CH3_DMA1_Stream0 },
    { TIM5, 3, DMA_Sx::Req::Tim5::CH4_DMA1_Stream1 },
    { TIM5, 3, DMA_Sx::Req::Tim5::CH4_DMA1_Stream3 },
#endif
#ifdef TIM8_BASE
    { TIM8, 0, DMA_Sx::Req::Tim8::CH1_DMA2_Stream2 },
    { TIM8, 1, DMA_Sx::Req::Tim8::CH2_DMA2_Stream3 },
    { TIM8, 2, DMA_Sx::Req::Tim8::CH3_DMA2_Stream4 },
    { TIM8, 3, DMA_Sx::Req::Tim8::CH4_DMA2_Stream7 },
#endif
#endif
};
#endif // TIM_DEFS_CPP

#ifdef TIM_DEFS_CPP
// Normal TRGO connections, ITR0..ITR3. OC-only slots and peripheral remaps are
// deliberately null: a source TIM* denotes TRGO, not its OC1 wire.
// F4: RM0090/0368/0383/0390/0401; F7: RM0385/0410/0431;
// G0: RM0444 tables 119/123/130 and RM0454 tables 69/73/80.
#ifdef TIM2_BASE
#define TIM_ITR_2 TIM2
#else
#define TIM_ITR_2 nullptr
#endif
#ifdef TIM3_BASE
#define TIM_ITR_3 TIM3
#else
#define TIM_ITR_3 nullptr
#endif
#ifdef TIM4_BASE
#define TIM_ITR_4 TIM4
#else
#define TIM_ITR_4 nullptr
#endif
#ifdef TIM5_BASE
#define TIM_ITR_5 TIM5
#else
#define TIM_ITR_5 nullptr
#endif
#ifdef TIM8_BASE
#define TIM_ITR_8 TIM8
#else
#define TIM_ITR_8 nullptr
#endif
#ifdef TIM15_BASE
#define TIM_ITR_15 TIM15
#else
#define TIM_ITR_15 nullptr
#endif
const TIM::ITRInfo TIM::itr_table[] = {
#if defined(STM32G0)
#ifdef TIM1_BASE
    { TIM1, {TIM_ITR_15, TIM_ITR_2, TIM_ITR_3, nullptr} },
#endif
#ifdef TIM2_BASE
    { TIM2, {TIM1, TIM_ITR_15, TIM_ITR_3, nullptr} },
#endif
#ifdef TIM3_BASE
    { TIM3, {TIM1, TIM_ITR_2, TIM_ITR_15, nullptr} },
#endif
#ifdef TIM4_BASE
    { TIM4, {TIM1, TIM_ITR_2, TIM_ITR_15, nullptr} },
#endif
#ifdef TIM15_BASE
    { TIM15, {TIM_ITR_2, TIM_ITR_3, nullptr, nullptr} },
#endif
#else
#ifdef TIM1_BASE
    { TIM1, {TIM_ITR_5, TIM_ITR_2, TIM_ITR_3, TIM_ITR_4} },
#endif
#ifdef TIM8_BASE
    { TIM8, {TIM1, TIM_ITR_2, TIM_ITR_4, TIM_ITR_5} },
#endif
#ifdef TIM2_BASE
    { TIM2, {TIM1, TIM_ITR_8, TIM_ITR_3, TIM_ITR_4} },
#endif
#ifdef TIM3_BASE
    { TIM3, {TIM1, TIM_ITR_2, TIM_ITR_5, TIM_ITR_4} },
#endif
#ifdef TIM4_BASE
    { TIM4, {TIM1, TIM_ITR_2, TIM_ITR_3, TIM_ITR_8} },
#endif
#ifdef TIM5_BASE
    { TIM5, {TIM_ITR_2, TIM_ITR_3, TIM_ITR_4, TIM_ITR_8} },
#endif
#ifdef TIM9_BASE
    { TIM9, {TIM_ITR_2, TIM_ITR_3, nullptr, nullptr} },
#endif
#ifdef TIM12_BASE
    { TIM12, {TIM_ITR_4, TIM_ITR_5, nullptr, nullptr} },
#endif
#endif
};
#undef TIM_ITR_2
#undef TIM_ITR_3
#undef TIM_ITR_4
#undef TIM_ITR_5
#undef TIM_ITR_8
#undef TIM_ITR_15
#endif // TIM_DEFS_CPP

// Previous ITR maps, retained as historical reference only; not compiled.
// // G0 ITR routes remain unsupported pending the HWCounter review.
// const TIM::ITR_Route TIM::itr_table[] = {
// 	{ TIM3, TIM1, 0xFF },
// 	{ TIM1, TIM3, 0xFF },
// };
// #if defined(STM32F4)
// // Existing RM0090 TRGO routes, filtered by actual peripheral availability.
// // F7 routing will be handled in its separate HWCounter review.
// const TIM::ITR_Route TIM::itr_table[] = {
// #if defined(TIM5_BASE) && defined(TIM1_BASE)
// 	{ TIM5, TIM1, 0 },
// #endif
// #if defined(TIM2_BASE) && defined(TIM1_BASE)
// 	{ TIM2, TIM1, 1 },
// #endif
// #if defined(TIM3_BASE) && defined(TIM1_BASE)
// 	{ TIM3, TIM1, 2 },
// #endif
// #if defined(TIM4_BASE) && defined(TIM1_BASE)
// 	{ TIM4, TIM1, 3 },
// #endif
// #if defined(TIM1_BASE) && defined(TIM2_BASE)
// 	{ TIM1, TIM2, 0 },
// #endif
// #if defined(TIM8_BASE) && defined(TIM2_BASE)
// 	{ TIM8, TIM2, 1 },
// #endif
// #if defined(TIM3_BASE) && defined(TIM2_BASE)
// 	{ TIM3, TIM2, 2 },
// #endif
// #if defined(TIM4_BASE) && defined(TIM2_BASE)
// 	{ TIM4, TIM2, 3 },
// #endif
// #if defined(TIM1_BASE) && defined(TIM3_BASE)
// 	{ TIM1, TIM3, 0 },
// #endif
// #if defined(TIM2_BASE) && defined(TIM3_BASE)
// 	{ TIM2, TIM3, 1 },
// #endif
// #if defined(TIM5_BASE) && defined(TIM3_BASE)
// 	{ TIM5, TIM3, 2 },
// #endif
// #if defined(TIM4_BASE) && defined(TIM3_BASE)
// 	{ TIM4, TIM3, 3 },
// #endif
// #if defined(TIM1_BASE) && defined(TIM4_BASE)
// 	{ TIM1, TIM4, 0 },
// #endif
// #if defined(TIM2_BASE) && defined(TIM4_BASE)
// 	{ TIM2, TIM4, 1 },
// #endif
// #if defined(TIM3_BASE) && defined(TIM4_BASE)
// 	{ TIM3, TIM4, 2 },
// #endif
// #if defined(TIM8_BASE) && defined(TIM4_BASE)
// 	{ TIM8, TIM4, 3 },
// #endif
// #if defined(TIM2_BASE) && defined(TIM5_BASE)
// 	{ TIM2, TIM5, 0 },
// #endif
// #if defined(TIM3_BASE) && defined(TIM5_BASE)
// 	{ TIM3, TIM5, 1 },
// #endif
// #if defined(TIM4_BASE) && defined(TIM5_BASE)
// 	{ TIM4, TIM5, 2 },
// #endif
// #if defined(TIM8_BASE) && defined(TIM5_BASE)
// 	{ TIM8, TIM5, 3 },
// #endif
// #if defined(TIM1_BASE) && defined(TIM8_BASE)
// 	{ TIM1, TIM8, 0 },
// #endif
// #if defined(TIM2_BASE) && defined(TIM8_BASE)
// 	{ TIM2, TIM8, 1 },
// #endif
// #if defined(TIM4_BASE) && defined(TIM8_BASE)
// 	{ TIM4, TIM8, 2 },
// #endif
// #if defined(TIM5_BASE) && defined(TIM8_BASE)
// 	{ TIM5, TIM8, 3 },
// #endif
// };
// #endif // STM32F4 ITR routes
