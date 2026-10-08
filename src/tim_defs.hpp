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
/** @brief GPIO pin bound to a specific timer/channel, used for compile-time pin tables. */
struct TIM_PIN {
	uint32_t port     = 0;
	uint8_t  pin      = 0;
	uint8_t  af       = 0;
	uint8_t  channel  = 0;   ///< 0 = CH1, 1 = CH2, 2 = CH3, 3 = CH4
	uint32_t tim_base = 0;

	constexpr bool IsValid() const { return port != 0; }

	/**
	 * @brief Converts to a plain PIN for GPIO configuration, carrying over this
	 * entry's own alternate-function index.
	 *
	 * Some timer pins share the same timer but need a different AF index (e.g.
	 * TIM3 on STM32G0: PA6/PB4 use AF1, PC6 uses AF0) — always go through this
	 * instead of looking up a single per-timer AF value, which would be wrong
	 * for those pins.
	 */
	inline PIN ToPin() const { return PIN(reinterpret_cast<GPIO_TypeDef*>(port), pin, af); }
};

// ---------------------------------------------------------------------------
// Per-timer, per-channel pin tables
//
// Naming convention:  TIM::_N::CHx::Pyz
//   N   — timer number
//   CHx — channel (CH1-CH4)
//   Pyz — GPIO pin name (e.g. PA8, PB0)
//
// Generated from the STM32CubeMX MCU database (GPIO alternate-function data)
// for every G0 / F4 / F7 line; no AF conflicts between lines. A pin is
// compiled in only if its timer (TIMx_BASE) and port (GPIOx_BASE) exist and,
// where the AF exists on some lines only, if the line matches (TIM_LINE_*
// below). Whether the pin is bonded out in your package is not checked.
// ---------------------------------------------------------------------------

#if defined(STM32G0)

#if defined(STM32G030xx) || defined(STM32G031xx) || defined(STM32G041xx)
#  define TIM_LINE_G03x
#endif
#if defined(STM32G050xx) || defined(STM32G051xx) || defined(STM32G061xx)
#  define TIM_LINE_G05x
#endif
#if defined(STM32G0B0xx) || defined(STM32G0B1xx) || defined(STM32G0C1xx)
#  define TIM_LINE_G0Bx
#endif

#if defined(TIM1_BASE)
struct _1 {
	struct CH1 {
		static constexpr TIM_PIN PA8  = { GPIOA_BASE,  8, 2, 0, TIM1_BASE };
#if !(defined(TIM_LINE_G03x) || defined(TIM_LINE_G05x)) && defined(GPIOC_BASE)
		static constexpr TIM_PIN PC8  = { GPIOC_BASE,  8, 2, 0, TIM1_BASE };
#endif
#if defined(GPIOE_BASE)
		static constexpr TIM_PIN PE9  = { GPIOE_BASE,  9, 1, 0, TIM1_BASE };
#endif
	};
	struct CH2 {
		static constexpr TIM_PIN PA9  = { GPIOA_BASE,  9, 2, 1, TIM1_BASE };
		static constexpr TIM_PIN PB3  = { GPIOB_BASE,  3, 1, 1, TIM1_BASE };
#if !(defined(TIM_LINE_G03x) || defined(TIM_LINE_G05x)) && defined(GPIOC_BASE)
		static constexpr TIM_PIN PC9  = { GPIOC_BASE,  9, 2, 1, TIM1_BASE };
#endif
#if defined(GPIOE_BASE)
		static constexpr TIM_PIN PE11 = { GPIOE_BASE, 11, 1, 1, TIM1_BASE };
#endif
	};
	struct CH3 {
		static constexpr TIM_PIN PA10 = { GPIOA_BASE, 10, 2, 2, TIM1_BASE };
		static constexpr TIM_PIN PB6  = { GPIOB_BASE,  6, 1, 2, TIM1_BASE };
#if !(defined(TIM_LINE_G03x) || defined(TIM_LINE_G05x)) && defined(GPIOC_BASE)
		static constexpr TIM_PIN PC10 = { GPIOC_BASE, 10, 2, 2, TIM1_BASE };
#endif
#if defined(GPIOE_BASE)
		static constexpr TIM_PIN PE13 = { GPIOE_BASE, 13, 1, 2, TIM1_BASE };
#endif
	};
	struct CH4 {
		static constexpr TIM_PIN PA11 = { GPIOA_BASE, 11, 2, 3, TIM1_BASE };
#if !(defined(TIM_LINE_G03x) || defined(TIM_LINE_G05x)) && defined(GPIOC_BASE)
		static constexpr TIM_PIN PC11 = { GPIOC_BASE, 11, 2, 3, TIM1_BASE };
#endif
#if defined(GPIOE_BASE)
		static constexpr TIM_PIN PE14 = { GPIOE_BASE, 14, 1, 3, TIM1_BASE };
#endif
	};
};
#endif

#if defined(TIM2_BASE)
struct _2 {
	struct CH1 {
		static constexpr TIM_PIN PA0  = { GPIOA_BASE,  0, 2, 0, TIM2_BASE };
		static constexpr TIM_PIN PA5  = { GPIOA_BASE,  5, 2, 0, TIM2_BASE };
		static constexpr TIM_PIN PA15 = { GPIOA_BASE, 15, 2, 0, TIM2_BASE };
#if !(defined(TIM_LINE_G03x) || defined(TIM_LINE_G05x)) && defined(GPIOC_BASE)
		static constexpr TIM_PIN PC4  = { GPIOC_BASE,  4, 2, 0, TIM2_BASE };
#endif
	};
	struct CH2 {
		static constexpr TIM_PIN PA1  = { GPIOA_BASE,  1, 2, 1, TIM2_BASE };
		static constexpr TIM_PIN PB3  = { GPIOB_BASE,  3, 2, 1, TIM2_BASE };
#if !(defined(TIM_LINE_G03x) || defined(TIM_LINE_G05x)) && defined(GPIOC_BASE)
		static constexpr TIM_PIN PC5  = { GPIOC_BASE,  5, 2, 1, TIM2_BASE };
#endif
	};
	struct CH3 {
		static constexpr TIM_PIN PA2  = { GPIOA_BASE,  2, 2, 2, TIM2_BASE };
		static constexpr TIM_PIN PB10 = { GPIOB_BASE, 10, 2, 2, TIM2_BASE };
#if defined(GPIOC_BASE)
		static constexpr TIM_PIN PC6  = { GPIOC_BASE,  6, 2, 2, TIM2_BASE };
#endif
	};
	struct CH4 {
		static constexpr TIM_PIN PA3  = { GPIOA_BASE,  3, 2, 3, TIM2_BASE };
		static constexpr TIM_PIN PB11 = { GPIOB_BASE, 11, 2, 3, TIM2_BASE };
#if defined(GPIOC_BASE)
		static constexpr TIM_PIN PC7  = { GPIOC_BASE,  7, 2, 3, TIM2_BASE };
#endif
	};
};
#endif

#if defined(TIM3_BASE)
struct _3 {
	struct CH1 {
		static constexpr TIM_PIN PA6  = { GPIOA_BASE,  6, 1, 0, TIM3_BASE };
		static constexpr TIM_PIN PB4  = { GPIOB_BASE,  4, 1, 0, TIM3_BASE };
#if defined(GPIOC_BASE)
		static constexpr TIM_PIN PC6  = { GPIOC_BASE,  6, 1, 0, TIM3_BASE };
#endif
#if defined(GPIOE_BASE)
		static constexpr TIM_PIN PE3  = { GPIOE_BASE,  3, 1, 0, TIM3_BASE };
#endif
	};
	struct CH2 {
		static constexpr TIM_PIN PA7  = { GPIOA_BASE,  7, 1, 1, TIM3_BASE };
		static constexpr TIM_PIN PB5  = { GPIOB_BASE,  5, 1, 1, TIM3_BASE };
#if defined(GPIOC_BASE)
		static constexpr TIM_PIN PC7  = { GPIOC_BASE,  7, 1, 1, TIM3_BASE };
#endif
#if defined(GPIOE_BASE)
		static constexpr TIM_PIN PE4  = { GPIOE_BASE,  4, 1, 1, TIM3_BASE };
#endif
	};
	struct CH3 {
		static constexpr TIM_PIN PB0  = { GPIOB_BASE,  0, 1, 2, TIM3_BASE };
#if !(defined(TIM_LINE_G03x) || defined(TIM_LINE_G05x)) && defined(GPIOC_BASE)
		static constexpr TIM_PIN PC8  = { GPIOC_BASE,  8, 1, 2, TIM3_BASE };
#endif
#if defined(GPIOE_BASE)
		static constexpr TIM_PIN PE5  = { GPIOE_BASE,  5, 1, 2, TIM3_BASE };
#endif
	};
	struct CH4 {
		static constexpr TIM_PIN PB1  = { GPIOB_BASE,  1, 1, 3, TIM3_BASE };
#if !(defined(TIM_LINE_G03x) || defined(TIM_LINE_G05x)) && defined(GPIOC_BASE)
		static constexpr TIM_PIN PC9  = { GPIOC_BASE,  9, 1, 3, TIM3_BASE };
#endif
#if defined(GPIOE_BASE)
		static constexpr TIM_PIN PE6  = { GPIOE_BASE,  6, 1, 3, TIM3_BASE };
#endif
	};
};
#endif

#if defined(TIM4_BASE)
struct _4 {
	struct CH1 {
		static constexpr TIM_PIN PB6  = { GPIOB_BASE,  6, 9, 0, TIM4_BASE };
#if defined(GPIOD_BASE)
		static constexpr TIM_PIN PD12 = { GPIOD_BASE, 12, 2, 0, TIM4_BASE };
#endif
	};
	struct CH2 {
		static constexpr TIM_PIN PB7  = { GPIOB_BASE,  7, 9, 1, TIM4_BASE };
#if defined(GPIOD_BASE)
		static constexpr TIM_PIN PD13 = { GPIOD_BASE, 13, 2, 1, TIM4_BASE };
#endif
	};
	struct CH3 {
		static constexpr TIM_PIN PB8  = { GPIOB_BASE,  8, 9, 2, TIM4_BASE };
#if defined(GPIOD_BASE)
		static constexpr TIM_PIN PD14 = { GPIOD_BASE, 14, 2, 2, TIM4_BASE };
#endif
	};
	struct CH4 {
		static constexpr TIM_PIN PB9  = { GPIOB_BASE,  9, 9, 3, TIM4_BASE };
#if defined(GPIOD_BASE)
		static constexpr TIM_PIN PD15 = { GPIOD_BASE, 15, 2, 3, TIM4_BASE };
#endif
	};
};
#endif

#if defined(TIM14_BASE)
struct _14 {
	struct CH1 {
		static constexpr TIM_PIN PA4  = { GPIOA_BASE,  4, 4, 0, TIM14_BASE };
		static constexpr TIM_PIN PA7  = { GPIOA_BASE,  7, 4, 0, TIM14_BASE };
		static constexpr TIM_PIN PB1  = { GPIOB_BASE,  1, 0, 0, TIM14_BASE };
#if !(defined(TIM_LINE_G03x) || defined(TIM_LINE_G05x)) && defined(GPIOC_BASE)
		static constexpr TIM_PIN PC12 = { GPIOC_BASE, 12, 2, 0, TIM14_BASE };
#endif
#if defined(GPIOF_BASE)
		static constexpr TIM_PIN PF0  = { GPIOF_BASE,  0, 2, 0, TIM14_BASE };
#endif
	};
};
#endif

#if defined(TIM15_BASE)
struct _15 {
	struct CH1 {
		static constexpr TIM_PIN PA2  = { GPIOA_BASE,  2, 5, 0, TIM15_BASE };
		static constexpr TIM_PIN PB14 = { GPIOB_BASE, 14, 5, 0, TIM15_BASE };
#if !defined(TIM_LINE_G05x) && defined(GPIOC_BASE)
		static constexpr TIM_PIN PC1  = { GPIOC_BASE,  1, 2, 0, TIM15_BASE };
#endif
#if defined(TIM_LINE_G0Bx) && defined(GPIOF_BASE)
		static constexpr TIM_PIN PF12 = { GPIOF_BASE, 12, 0, 0, TIM15_BASE };
#endif
	};
	struct CH2 {
		static constexpr TIM_PIN PA3  = { GPIOA_BASE,  3, 5, 1, TIM15_BASE };
		static constexpr TIM_PIN PB15 = { GPIOB_BASE, 15, 5, 1, TIM15_BASE };
#if !defined(TIM_LINE_G05x) && defined(GPIOC_BASE)
		static constexpr TIM_PIN PC2  = { GPIOC_BASE,  2, 2, 1, TIM15_BASE };
#endif
#if defined(TIM_LINE_G0Bx) && defined(GPIOF_BASE)
		static constexpr TIM_PIN PF13 = { GPIOF_BASE, 13, 0, 1, TIM15_BASE };
#endif
	};
};
#endif

#if defined(TIM16_BASE)
struct _16 {
	struct CH1 {
		static constexpr TIM_PIN PA6  = { GPIOA_BASE,  6, 5, 0, TIM16_BASE };
		static constexpr TIM_PIN PB8  = { GPIOB_BASE,  8, 2, 0, TIM16_BASE };
#if defined(GPIOD_BASE)
		static constexpr TIM_PIN PD0  = { GPIOD_BASE,  0, 2, 0, TIM16_BASE };
#endif
#if defined(GPIOE_BASE)
		static constexpr TIM_PIN PE0  = { GPIOE_BASE,  0, 0, 0, TIM16_BASE };
#endif
	};
};
#endif

#if defined(TIM17_BASE)
struct _17 {
	struct CH1 {
		static constexpr TIM_PIN PA7  = { GPIOA_BASE,  7, 5, 0, TIM17_BASE };
		static constexpr TIM_PIN PB9  = { GPIOB_BASE,  9, 2, 0, TIM17_BASE };
#if defined(GPIOD_BASE)
		static constexpr TIM_PIN PD1  = { GPIOD_BASE,  1, 2, 0, TIM17_BASE };
#endif
#if defined(GPIOE_BASE)
		static constexpr TIM_PIN PE1  = { GPIOE_BASE,  1, 0, 0, TIM17_BASE };
#endif
	};
};
#endif

// ---------------------------------------------------------------------------
// STM32F4
// ---------------------------------------------------------------------------
#elif defined(STM32F4)

#if defined(STM32F410Cx) || defined(STM32F410Rx) || defined(STM32F410Tx)
#  define TIM_LINE_F410
#endif
#if defined(STM32F412Cx) || defined(STM32F412Rx) || defined(STM32F412Vx) || defined(STM32F412Zx)
#  define TIM_LINE_F412
#endif
#if defined(STM32F413xx) || defined(STM32F423xx)
#  define TIM_LINE_F413
#endif
#if defined(STM32F405xx) || defined(STM32F407xx) || defined(STM32F415xx) || defined(STM32F417xx)
#  define TIM_LINE_F417
#endif
#if defined(STM32F427xx) || defined(STM32F429xx) || defined(STM32F437xx) || defined(STM32F439xx)
#  define TIM_LINE_F427
#endif
#if defined(STM32F446xx)
#  define TIM_LINE_F446
#endif
#if defined(STM32F469xx) || defined(STM32F479xx)
#  define TIM_LINE_F469
#endif

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
#if defined(TIM_LINE_F446)
		static constexpr TIM_PIN PB8  = { GPIOB_BASE,  8, 1, 0, TIM2_BASE };
#endif
	};
	struct CH2 {
		static constexpr TIM_PIN PA1  = { GPIOA_BASE,  1, 1, 1, TIM2_BASE };
		static constexpr TIM_PIN PB3  = { GPIOB_BASE,  3, 1, 1, TIM2_BASE };
#if defined(TIM_LINE_F446)
		static constexpr TIM_PIN PB9  = { GPIOB_BASE,  9, 1, 1, TIM2_BASE };
#endif
	};
	struct CH3 {
		static constexpr TIM_PIN PA2  = { GPIOA_BASE,  2, 1, 2, TIM2_BASE };
		static constexpr TIM_PIN PB10 = { GPIOB_BASE, 10, 1, 2, TIM2_BASE };
	};
	struct CH4 {
		static constexpr TIM_PIN PA3  = { GPIOA_BASE,  3, 1, 3, TIM2_BASE };
#if defined(TIM_LINE_F446)
		static constexpr TIM_PIN PB2  = { GPIOB_BASE,  2, 1, 3, TIM2_BASE };
#endif
		static constexpr TIM_PIN PB11 = { GPIOB_BASE, 11, 1, 3, TIM2_BASE };
	};
};
#endif

#if defined(TIM3_BASE)
struct _3 {
	struct CH1 {
		static constexpr TIM_PIN PA6  = { GPIOA_BASE,  6, 2, 0, TIM3_BASE };
		static constexpr TIM_PIN PB4  = { GPIOB_BASE,  4, 2, 0, TIM3_BASE };
#if defined(GPIOC_BASE)
		static constexpr TIM_PIN PC6  = { GPIOC_BASE,  6, 2, 0, TIM3_BASE };
#endif
	};
	struct CH2 {
		static constexpr TIM_PIN PA7  = { GPIOA_BASE,  7, 2, 1, TIM3_BASE };
		static constexpr TIM_PIN PB5  = { GPIOB_BASE,  5, 2, 1, TIM3_BASE };
#if defined(GPIOC_BASE)
		static constexpr TIM_PIN PC7  = { GPIOC_BASE,  7, 2, 1, TIM3_BASE };
#endif
	};
	struct CH3 {
		static constexpr TIM_PIN PB0  = { GPIOB_BASE,  0, 2, 2, TIM3_BASE };
#if defined(GPIOC_BASE)
		static constexpr TIM_PIN PC8  = { GPIOC_BASE,  8, 2, 2, TIM3_BASE };
#endif
	};
	struct CH4 {
		static constexpr TIM_PIN PB1  = { GPIOB_BASE,  1, 2, 3, TIM3_BASE };
#if defined(GPIOC_BASE)
		static constexpr TIM_PIN PC9  = { GPIOC_BASE,  9, 2, 3, TIM3_BASE };
#endif
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

#if defined(TIM5_BASE)
struct _5 {
	struct CH1 {
		static constexpr TIM_PIN PA0  = { GPIOA_BASE,  0, 2, 0, TIM5_BASE };
#if defined(TIM_LINE_F410)
		static constexpr TIM_PIN PB12 = { GPIOB_BASE, 12, 2, 0, TIM5_BASE };
#endif
#if (defined(TIM_LINE_F412) || defined(TIM_LINE_F413)) && defined(GPIOF_BASE)
		static constexpr TIM_PIN PF3  = { GPIOF_BASE,  3, 2, 0, TIM5_BASE };
#endif
#if (defined(TIM_LINE_F417) || defined(TIM_LINE_F427) || defined(TIM_LINE_F469)) && defined(GPIOH_BASE)
		static constexpr TIM_PIN PH10 = { GPIOH_BASE, 10, 2, 0, TIM5_BASE };
#endif
	};
	struct CH2 {
		static constexpr TIM_PIN PA1  = { GPIOA_BASE,  1, 2, 1, TIM5_BASE };
#if defined(TIM_LINE_F410) && defined(GPIOC_BASE)
		static constexpr TIM_PIN PC10 = { GPIOC_BASE, 10, 2, 1, TIM5_BASE };
#endif
#if (defined(TIM_LINE_F412) || defined(TIM_LINE_F413)) && defined(GPIOF_BASE)
		static constexpr TIM_PIN PF4  = { GPIOF_BASE,  4, 2, 1, TIM5_BASE };
#endif
#if (defined(TIM_LINE_F417) || defined(TIM_LINE_F427) || defined(TIM_LINE_F469)) && defined(GPIOH_BASE)
		static constexpr TIM_PIN PH11 = { GPIOH_BASE, 11, 2, 1, TIM5_BASE };
#endif
	};
	struct CH3 {
		static constexpr TIM_PIN PA2  = { GPIOA_BASE,  2, 2, 2, TIM5_BASE };
#if defined(TIM_LINE_F410) && defined(GPIOC_BASE)
		static constexpr TIM_PIN PC11 = { GPIOC_BASE, 11, 2, 2, TIM5_BASE };
#endif
#if (defined(TIM_LINE_F412) || defined(TIM_LINE_F413)) && defined(GPIOF_BASE)
		static constexpr TIM_PIN PF5  = { GPIOF_BASE,  5, 2, 2, TIM5_BASE };
#endif
#if (defined(TIM_LINE_F417) || defined(TIM_LINE_F427) || defined(TIM_LINE_F469)) && defined(GPIOH_BASE)
		static constexpr TIM_PIN PH12 = { GPIOH_BASE, 12, 2, 2, TIM5_BASE };
#endif
	};
	struct CH4 {
		static constexpr TIM_PIN PA3  = { GPIOA_BASE,  3, 2, 3, TIM5_BASE };
#if defined(TIM_LINE_F410)
		static constexpr TIM_PIN PB11 = { GPIOB_BASE, 11, 2, 3, TIM5_BASE };
#endif
#if (defined(TIM_LINE_F412) || defined(TIM_LINE_F413)) && defined(GPIOF_BASE)
		static constexpr TIM_PIN PF10 = { GPIOF_BASE, 10, 2, 3, TIM5_BASE };
#endif
#if defined(GPIOI_BASE)
		static constexpr TIM_PIN PI0  = { GPIOI_BASE,  0, 2, 3, TIM5_BASE };
#endif
	};
};
#endif

#if defined(TIM8_BASE)
struct _8 {
	struct CH1 {
#if defined(GPIOC_BASE)
		static constexpr TIM_PIN PC6  = { GPIOC_BASE,  6, 3, 0, TIM8_BASE };
#endif
#if defined(GPIOI_BASE)
		static constexpr TIM_PIN PI5  = { GPIOI_BASE,  5, 3, 0, TIM8_BASE };
#endif
	};
	struct CH2 {
#if defined(GPIOC_BASE)
		static constexpr TIM_PIN PC7  = { GPIOC_BASE,  7, 3, 1, TIM8_BASE };
#endif
#if defined(GPIOI_BASE)
		static constexpr TIM_PIN PI6  = { GPIOI_BASE,  6, 3, 1, TIM8_BASE };
#endif
	};
	struct CH3 {
#if defined(GPIOC_BASE)
		static constexpr TIM_PIN PC8  = { GPIOC_BASE,  8, 3, 2, TIM8_BASE };
#endif
#if defined(GPIOI_BASE)
		static constexpr TIM_PIN PI7  = { GPIOI_BASE,  7, 3, 2, TIM8_BASE };
#endif
	};
	struct CH4 {
#if defined(GPIOC_BASE)
		static constexpr TIM_PIN PC9  = { GPIOC_BASE,  9, 3, 3, TIM8_BASE };
#endif
#if defined(GPIOI_BASE)
		static constexpr TIM_PIN PI2  = { GPIOI_BASE,  2, 3, 3, TIM8_BASE };
#endif
	};
};
#endif

#if defined(TIM9_BASE)
struct _9 {
	struct CH1 {
		static constexpr TIM_PIN PA2  = { GPIOA_BASE,  2, 3, 0, TIM9_BASE };
#if defined(TIM_LINE_F410) && defined(GPIOC_BASE)
		static constexpr TIM_PIN PC4  = { GPIOC_BASE,  4, 3, 0, TIM9_BASE };
#endif
#if defined(GPIOE_BASE)
		static constexpr TIM_PIN PE5  = { GPIOE_BASE,  5, 3, 0, TIM9_BASE };
#endif
	};
	struct CH2 {
		static constexpr TIM_PIN PA3  = { GPIOA_BASE,  3, 3, 1, TIM9_BASE };
#if defined(TIM_LINE_F410) && defined(GPIOC_BASE)
		static constexpr TIM_PIN PC5  = { GPIOC_BASE,  5, 3, 1, TIM9_BASE };
#endif
#if defined(GPIOE_BASE)
		static constexpr TIM_PIN PE6  = { GPIOE_BASE,  6, 3, 1, TIM9_BASE };
#endif
	};
};
#endif

#if defined(TIM10_BASE)
struct _10 {
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
	struct CH1 {
		static constexpr TIM_PIN PB9  = { GPIOB_BASE,  9, 3, 0, TIM11_BASE };
#if defined(TIM_LINE_F410) && defined(GPIOC_BASE)
		static constexpr TIM_PIN PC12 = { GPIOC_BASE, 12, 3, 0, TIM11_BASE };
#endif
#if defined(GPIOF_BASE)
		static constexpr TIM_PIN PF7  = { GPIOF_BASE,  7, 3, 0, TIM11_BASE };
#endif
	};
};
#endif

#if defined(TIM12_BASE)
struct _12 {
	struct CH1 {
		static constexpr TIM_PIN PB14 = { GPIOB_BASE, 14, 9, 0, TIM12_BASE };
#if !(defined(TIM_LINE_F412) || defined(TIM_LINE_F413) || defined(TIM_LINE_F446)) && defined(GPIOH_BASE)
		static constexpr TIM_PIN PH6  = { GPIOH_BASE,  6, 9, 0, TIM12_BASE };
#endif
	};
	struct CH2 {
		static constexpr TIM_PIN PB15 = { GPIOB_BASE, 15, 9, 1, TIM12_BASE };
#if !(defined(TIM_LINE_F412) || defined(TIM_LINE_F413) || defined(TIM_LINE_F446)) && defined(GPIOH_BASE)
		static constexpr TIM_PIN PH9  = { GPIOH_BASE,  9, 9, 1, TIM12_BASE };
#endif
	};
};
#endif

#if defined(TIM13_BASE)
struct _13 {
	struct CH1 {
		static constexpr TIM_PIN PA6  = { GPIOA_BASE,  6, 9, 0, TIM13_BASE };
#if defined(GPIOF_BASE)
		static constexpr TIM_PIN PF8  = { GPIOF_BASE,  8, 9, 0, TIM13_BASE };
#endif
	};
};
#endif

#if defined(TIM14_BASE)
struct _14 {
	struct CH1 {
		static constexpr TIM_PIN PA7  = { GPIOA_BASE,  7, 9, 0, TIM14_BASE };
#if defined(GPIOF_BASE)
		static constexpr TIM_PIN PF9  = { GPIOF_BASE,  9, 9, 0, TIM14_BASE };
#endif
	};
};
#endif

// ---------------------------------------------------------------------------
// STM32F7
// ---------------------------------------------------------------------------
#elif defined(STM32F7)


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
#if defined(GPIOC_BASE)
		static constexpr TIM_PIN PC6  = { GPIOC_BASE,  6, 2, 0, TIM3_BASE };
#endif
	};
	struct CH2 {
		static constexpr TIM_PIN PA7  = { GPIOA_BASE,  7, 2, 1, TIM3_BASE };
		static constexpr TIM_PIN PB5  = { GPIOB_BASE,  5, 2, 1, TIM3_BASE };
#if defined(GPIOC_BASE)
		static constexpr TIM_PIN PC7  = { GPIOC_BASE,  7, 2, 1, TIM3_BASE };
#endif
	};
	struct CH3 {
		static constexpr TIM_PIN PB0  = { GPIOB_BASE,  0, 2, 2, TIM3_BASE };
#if defined(GPIOC_BASE)
		static constexpr TIM_PIN PC8  = { GPIOC_BASE,  8, 2, 2, TIM3_BASE };
#endif
	};
	struct CH4 {
		static constexpr TIM_PIN PB1  = { GPIOB_BASE,  1, 2, 3, TIM3_BASE };
#if defined(GPIOC_BASE)
		static constexpr TIM_PIN PC9  = { GPIOC_BASE,  9, 2, 3, TIM3_BASE };
#endif
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

#if defined(TIM5_BASE)
struct _5 {
	struct CH1 {
		static constexpr TIM_PIN PA0  = { GPIOA_BASE,  0, 2, 0, TIM5_BASE };
#if defined(GPIOH_BASE)
		static constexpr TIM_PIN PH10 = { GPIOH_BASE, 10, 2, 0, TIM5_BASE };
#endif
	};
	struct CH2 {
		static constexpr TIM_PIN PA1  = { GPIOA_BASE,  1, 2, 1, TIM5_BASE };
#if defined(GPIOH_BASE)
		static constexpr TIM_PIN PH11 = { GPIOH_BASE, 11, 2, 1, TIM5_BASE };
#endif
	};
	struct CH3 {
		static constexpr TIM_PIN PA2  = { GPIOA_BASE,  2, 2, 2, TIM5_BASE };
#if defined(GPIOH_BASE)
		static constexpr TIM_PIN PH12 = { GPIOH_BASE, 12, 2, 2, TIM5_BASE };
#endif
	};
	struct CH4 {
		static constexpr TIM_PIN PA3  = { GPIOA_BASE,  3, 2, 3, TIM5_BASE };
#if defined(GPIOI_BASE)
		static constexpr TIM_PIN PI0  = { GPIOI_BASE,  0, 2, 3, TIM5_BASE };
#endif
	};
};
#endif

#if defined(TIM8_BASE)
struct _8 {
	struct CH1 {
#if defined(GPIOC_BASE)
		static constexpr TIM_PIN PC6  = { GPIOC_BASE,  6, 3, 0, TIM8_BASE };
#endif
#if defined(GPIOI_BASE)
		static constexpr TIM_PIN PI5  = { GPIOI_BASE,  5, 3, 0, TIM8_BASE };
#endif
	};
	struct CH2 {
#if defined(GPIOC_BASE)
		static constexpr TIM_PIN PC7  = { GPIOC_BASE,  7, 3, 1, TIM8_BASE };
#endif
#if defined(GPIOI_BASE)
		static constexpr TIM_PIN PI6  = { GPIOI_BASE,  6, 3, 1, TIM8_BASE };
#endif
	};
	struct CH3 {
#if defined(GPIOC_BASE)
		static constexpr TIM_PIN PC8  = { GPIOC_BASE,  8, 3, 2, TIM8_BASE };
#endif
#if defined(GPIOI_BASE)
		static constexpr TIM_PIN PI7  = { GPIOI_BASE,  7, 3, 2, TIM8_BASE };
#endif
	};
	struct CH4 {
#if defined(GPIOC_BASE)
		static constexpr TIM_PIN PC9  = { GPIOC_BASE,  9, 3, 3, TIM8_BASE };
#endif
#if defined(GPIOI_BASE)
		static constexpr TIM_PIN PI2  = { GPIOI_BASE,  2, 3, 3, TIM8_BASE };
#endif
	};
};
#endif

#if defined(TIM9_BASE)
struct _9 {
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
	struct CH1 {
		static constexpr TIM_PIN PA6  = { GPIOA_BASE,  6, 9, 0, TIM13_BASE };
#if defined(GPIOF_BASE)
		static constexpr TIM_PIN PF8  = { GPIOF_BASE,  8, 9, 0, TIM13_BASE };
#endif
	};
};
#endif

#if defined(TIM14_BASE)
struct _14 {
	struct CH1 {
		static constexpr TIM_PIN PA7  = { GPIOA_BASE,  7, 9, 0, TIM14_BASE };
#if defined(GPIOF_BASE)
		static constexpr TIM_PIN PF9  = { GPIOF_BASE,  9, 9, 0, TIM14_BASE };
#endif
	};
};
#endif

#endif  // STM32 family

#undef TIM_LINE_F410
#undef TIM_LINE_F412
#undef TIM_LINE_F413
#undef TIM_LINE_F417
#undef TIM_LINE_F427
#undef TIM_LINE_F446
#undef TIM_LINE_F469
#undef TIM_LINE_G03x
#undef TIM_LINE_G05x
#undef TIM_LINE_G0Bx

#endif  // Section A: defined(TIM_HPP_) && !defined(TIM_DEFS_CPP)

// ===========================================================================
// Section B: Peripheral-info table — tim.cpp file scope
// ===========================================================================
#ifdef TIM_DEFS_CPP

#if defined(STM32G0)
// One APB on G0: every timer runs from TIMxAPB1Clock. IRQ vector names depend
// on which peripherals share the line on the given device.
#if defined(TIM4_BASE)
#  define TIM_G0_TIM3_IRQN   TIM3_TIM4_IRQn
#else
#  define TIM_G0_TIM3_IRQN   TIM3_IRQn
#endif
#if defined(DAC1_BASE)
#  define TIM_G0_TIM6_IRQN   TIM6_DAC_LPTIM1_IRQn
#else
#  define TIM_G0_TIM6_IRQN   TIM6_IRQn
#endif
#if defined(LPTIM2_BASE)
#  define TIM_G0_TIM7_IRQN   TIM7_LPTIM2_IRQn
#else
#  define TIM_G0_TIM7_IRQN   TIM7_IRQn
#endif
#if defined(FDCAN1_BASE)
#  define TIM_G0_TIM16_IRQN  TIM16_FDCAN_IT0_IRQn
#  define TIM_G0_TIM17_IRQN  TIM17_FDCAN_IT1_IRQn
#else
#  define TIM_G0_TIM16_IRQN  TIM16_IRQn
#  define TIM_G0_TIM17_IRQN  TIM17_IRQn
#endif

const TIM::PeriphInfo TIM::tim_table[] = {
	{ TIM1,  &RCC->APBENR2, RCC_APBENR2_TIM1EN,  &RCC->APBRSTR2, RCC_APBRSTR2_TIM1RST,
	  &System::TIMxAPB1Clock,
	  TIM1_BRK_UP_TRG_COM_IRQn, TIM1_CC_IRQn,      true,  4, 0xFFFF,     DMA_Sx::Req::Tim1::UP.ch  },
#if defined(TIM2_BASE)
	{ TIM2,  &RCC->APBENR1, RCC_APBENR1_TIM2EN,  &RCC->APBRSTR1, RCC_APBRSTR1_TIM2RST,
	  &System::TIMxAPB1Clock,
	  TIM2_IRQn,                TIM2_IRQn,         false, 4, 0xFFFFFFFF, DMA_Sx::Req::Tim2::UP.ch  },
#endif
	{ TIM3,  &RCC->APBENR1, RCC_APBENR1_TIM3EN,  &RCC->APBRSTR1, RCC_APBRSTR1_TIM3RST,
	  &System::TIMxAPB1Clock,
	  TIM_G0_TIM3_IRQN,         TIM_G0_TIM3_IRQN,  false, 4, 0xFFFF,     DMA_Sx::Req::Tim3::UP.ch  },
#if defined(TIM4_BASE)
	{ TIM4,  &RCC->APBENR1, RCC_APBENR1_TIM4EN,  &RCC->APBRSTR1, RCC_APBRSTR1_TIM4RST,
	  &System::TIMxAPB1Clock,
	  TIM3_TIM4_IRQn,           TIM3_TIM4_IRQn,    false, 4, 0xFFFF,     DMA_Sx::Req::Tim4::UP.ch  },
#endif
#if defined(TIM6_BASE)
	{ TIM6,  &RCC->APBENR1, RCC_APBENR1_TIM6EN,  &RCC->APBRSTR1, RCC_APBRSTR1_TIM6RST,
	  &System::TIMxAPB1Clock,
	  TIM_G0_TIM6_IRQN,         TIM_G0_TIM6_IRQN,  false, 0, 0xFFFF,     DMA_Sx::Req::Tim6::UP.ch  },
#endif
#if defined(TIM7_BASE)
	{ TIM7,  &RCC->APBENR1, RCC_APBENR1_TIM7EN,  &RCC->APBRSTR1, RCC_APBRSTR1_TIM7RST,
	  &System::TIMxAPB1Clock,
	  TIM_G0_TIM7_IRQN,         TIM_G0_TIM7_IRQN,  false, 0, 0xFFFF,     DMA_Sx::Req::Tim7::UP.ch  },
#endif
	{ TIM14, &RCC->APBENR2, RCC_APBENR2_TIM14EN, &RCC->APBRSTR2, RCC_APBRSTR2_TIM14RST,
	  &System::TIMxAPB1Clock,
	  TIM14_IRQn,               TIM14_IRQn,        false, 1, 0xFFFF,     0                          },
#if defined(TIM15_BASE)
	{ TIM15, &RCC->APBENR2, RCC_APBENR2_TIM15EN, &RCC->APBRSTR2, RCC_APBRSTR2_TIM15RST,
	  &System::TIMxAPB1Clock,
	  TIM15_IRQn,               TIM15_IRQn,        true,  2, 0xFFFF,     DMA_Sx::Req::Tim15::UP.ch },
#endif
	{ TIM16, &RCC->APBENR2, RCC_APBENR2_TIM16EN, &RCC->APBRSTR2, RCC_APBRSTR2_TIM16RST,
	  &System::TIMxAPB1Clock,
	  TIM_G0_TIM16_IRQN,        TIM_G0_TIM16_IRQN, true,  1, 0xFFFF,     DMA_Sx::Req::Tim16::UP.ch },
	{ TIM17, &RCC->APBENR2, RCC_APBENR2_TIM17EN, &RCC->APBRSTR2, RCC_APBRSTR2_TIM17RST,
	  &System::TIMxAPB1Clock,
	  TIM_G0_TIM17_IRQN,        TIM_G0_TIM17_IRQN, true,  1, 0xFFFF,     DMA_Sx::Req::Tim17::UP.ch },
};

#undef TIM_G0_TIM3_IRQN
#undef TIM_G0_TIM6_IRQN
#undef TIM_G0_TIM7_IRQN
#undef TIM_G0_TIM16_IRQN
#undef TIM_G0_TIM17_IRQN

// TODO: verify against RM0444 "TIMx internal trigger connection" before use.
// itr=0xFF marks these as unverified so TIM_HWCounter::SetUp() refuses them
// rather than risk a silently wrong route.
const TIM::ITR_Route TIM::itr_table[] = {
	{ TIM3, TIM1, 0xFF },
	{ TIM1, TIM3, 0xFF },
};

#elif defined(STM32F4) || defined(STM32F7)
// Same RCC bits and vector layout on F4 and F7; each row exists only where
// the timer does. Vector names that depend on the device:
#if defined(TIM10_BASE)
#  define TIM_F4_TIM1_UP_IRQN  TIM1_UP_TIM10_IRQn
#else
#  define TIM_F4_TIM1_UP_IRQN  TIM1_UP_IRQn          // F410: no TIM10
#endif
#if defined(DAC_BASE)
#  define TIM_F4_TIM6_IRQN     TIM6_DAC_IRQn
#else
#  define TIM_F4_TIM6_IRQN     TIM6_IRQn             // F412: no DAC
#endif

const TIM::PeriphInfo TIM::tim_table[] = {
	{ TIM1,  &RCC->APB2ENR, RCC_APB2ENR_TIM1EN,  &RCC->APB2RSTR, RCC_APB2RSTR_TIM1RST,
	  &System::TIMxAPB2Clock,
	  TIM_F4_TIM1_UP_IRQN,     TIM1_CC_IRQn,               true,  4, 0xFFFF,     0 },
#if defined(TIM2_BASE)
	{ TIM2,  &RCC->APB1ENR, RCC_APB1ENR_TIM2EN,  &RCC->APB1RSTR, RCC_APB1RSTR_TIM2RST,
	  &System::TIMxAPB1Clock,
	  TIM2_IRQn,               TIM2_IRQn,                  false, 4, 0xFFFFFFFF, 0 },
#endif
#if defined(TIM3_BASE)
	{ TIM3,  &RCC->APB1ENR, RCC_APB1ENR_TIM3EN,  &RCC->APB1RSTR, RCC_APB1RSTR_TIM3RST,
	  &System::TIMxAPB1Clock,
	  TIM3_IRQn,               TIM3_IRQn,                  false, 4, 0xFFFF,     0 },
#endif
#if defined(TIM4_BASE)
	{ TIM4,  &RCC->APB1ENR, RCC_APB1ENR_TIM4EN,  &RCC->APB1RSTR, RCC_APB1RSTR_TIM4RST,
	  &System::TIMxAPB1Clock,
	  TIM4_IRQn,               TIM4_IRQn,                  false, 4, 0xFFFF,     0 },
#endif
	{ TIM5,  &RCC->APB1ENR, RCC_APB1ENR_TIM5EN,  &RCC->APB1RSTR, RCC_APB1RSTR_TIM5RST,
	  &System::TIMxAPB1Clock,
	  TIM5_IRQn,               TIM5_IRQn,                  false, 4, 0xFFFFFFFF, 0 },
#if defined(TIM6_BASE)
	{ TIM6,  &RCC->APB1ENR, RCC_APB1ENR_TIM6EN,  &RCC->APB1RSTR, RCC_APB1RSTR_TIM6RST,
	  &System::TIMxAPB1Clock,
	  TIM_F4_TIM6_IRQN,        TIM_F4_TIM6_IRQN,           false, 0, 0xFFFF,     0 },
#endif
#if defined(TIM7_BASE)
	{ TIM7,  &RCC->APB1ENR, RCC_APB1ENR_TIM7EN,  &RCC->APB1RSTR, RCC_APB1RSTR_TIM7RST,
	  &System::TIMxAPB1Clock,
	  TIM7_IRQn,               TIM7_IRQn,                  false, 0, 0xFFFF,     0 },
#endif
#if defined(TIM8_BASE)
	{ TIM8,  &RCC->APB2ENR, RCC_APB2ENR_TIM8EN,  &RCC->APB2RSTR, RCC_APB2RSTR_TIM8RST,
	  &System::TIMxAPB2Clock,
	  TIM8_UP_TIM13_IRQn,      TIM8_CC_IRQn,               true,  4, 0xFFFF,     0 },
#endif
#if defined(TIM9_BASE)
	{ TIM9,  &RCC->APB2ENR, RCC_APB2ENR_TIM9EN,  &RCC->APB2RSTR, RCC_APB2RSTR_TIM9RST,
	  &System::TIMxAPB2Clock,
	  TIM1_BRK_TIM9_IRQn,      TIM1_BRK_TIM9_IRQn,         false, 2, 0xFFFF,     0 },
#endif
#if defined(TIM10_BASE)
	{ TIM10, &RCC->APB2ENR, RCC_APB2ENR_TIM10EN, &RCC->APB2RSTR, RCC_APB2RSTR_TIM10RST,
	  &System::TIMxAPB2Clock,
	  TIM1_UP_TIM10_IRQn,      TIM1_UP_TIM10_IRQn,         false, 1, 0xFFFF,     0 },
#endif
#if defined(TIM11_BASE)
	{ TIM11, &RCC->APB2ENR, RCC_APB2ENR_TIM11EN, &RCC->APB2RSTR, RCC_APB2RSTR_TIM11RST,
	  &System::TIMxAPB2Clock,
	  TIM1_TRG_COM_TIM11_IRQn, TIM1_TRG_COM_TIM11_IRQn,   false, 1, 0xFFFF,     0 },
#endif
#if defined(TIM12_BASE)
	{ TIM12, &RCC->APB1ENR, RCC_APB1ENR_TIM12EN, &RCC->APB1RSTR, RCC_APB1RSTR_TIM12RST,
	  &System::TIMxAPB1Clock,
	  TIM8_BRK_TIM12_IRQn,     TIM8_BRK_TIM12_IRQn,        false, 2, 0xFFFF,     0 },
#endif
#if defined(TIM13_BASE)
	{ TIM13, &RCC->APB1ENR, RCC_APB1ENR_TIM13EN, &RCC->APB1RSTR, RCC_APB1RSTR_TIM13RST,
	  &System::TIMxAPB1Clock,
	  TIM8_UP_TIM13_IRQn,      TIM8_UP_TIM13_IRQn,         false, 1, 0xFFFF,     0 },
#endif
#if defined(TIM14_BASE)
	{ TIM14, &RCC->APB1ENR, RCC_APB1ENR_TIM14EN, &RCC->APB1RSTR, RCC_APB1RSTR_TIM14RST,
	  &System::TIMxAPB1Clock,
	  TIM8_TRG_COM_TIM14_IRQn, TIM8_TRG_COM_TIM14_IRQn,   false, 1, 0xFFFF,     0 },
#endif
};

#undef TIM_F4_TIM1_UP_IRQN
#undef TIM_F4_TIM6_IRQN

#if defined(STM32F4)
// RM0090 "TIMx internal trigger connection" — TIM1/2/3/4/5/8 (the only ones
// with a full slave-mode controller). TIM9-14 are omitted: on F4 they either
// lack an SMCR or their ITR sources are OC outputs, not TRGO, so they don't
// fit this master-TRGO -> slave-TS scheme. Rows whose timers are missing on
// the device (F401/F410/F411: no TIM8; F410: no TIM2/3/4) are compiled out.
const TIM::ITR_Route TIM::itr_table[] = {
	{ TIM5, TIM1, 0 },
#if defined(TIM2_BASE)
	// slave TIM1
	{ TIM2, TIM1, 1 }, { TIM3, TIM1, 2 }, { TIM4, TIM1, 3 },
	// slave TIM2
	{ TIM1, TIM2, 0 }, { TIM3, TIM2, 2 }, { TIM4, TIM2, 3 },
	// slave TIM3
	{ TIM1, TIM3, 0 }, { TIM2, TIM3, 1 }, { TIM5, TIM3, 2 }, { TIM4, TIM3, 3 },
	// slave TIM4
	{ TIM1, TIM4, 0 }, { TIM2, TIM4, 1 }, { TIM3, TIM4, 2 },
	// slave TIM5
	{ TIM2, TIM5, 0 }, { TIM3, TIM5, 1 }, { TIM4, TIM5, 2 },
#endif
#if defined(TIM8_BASE)
	{ TIM8, TIM2, 1 }, { TIM8, TIM4, 3 }, { TIM8, TIM5, 3 },
	// slave TIM8
	{ TIM1, TIM8, 0 }, { TIM2, TIM8, 1 }, { TIM4, TIM8, 2 }, { TIM5, TIM8, 3 },
#endif
};

#else  // STM32F7
// TODO: verify against RM0385/RM0410/RM0431 "TIMx internal trigger
// connection". Until then no route is known: FindITR() returns 0xFF and
// TIM_HWCounter::SetUp() refuses instead of risking a wrong route.
const TIM::ITR_Route TIM::itr_table[] = {
	{ TIM1, TIM2, 0xFF },
};
#endif

#endif

#endif  // TIM_DEFS_CPP (Section B)
