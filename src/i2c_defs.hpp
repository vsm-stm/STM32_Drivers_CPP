/**
 * @file i2c_defs.hpp
 * @brief Selected-device instance, pin and DMA definitions for I2C.
 *
 * The public driver header includes the declaration section inside its class.
 * Each _N owns an immutable constexpr instance, named pins and DMA routes;
 * unavailable instances are omitted by the selected CMSIS device guards.
 * Numeric peripheral/RCC addresses support constant initialization without MMIO.
 * Bus-clock pointers refer to ordinary System variables, not hardware registers.
 *
 * Application code includes i2c.hpp and can shorten names with:
 * @code
 *   using Hardware = I2C::_1;
 *   // Hardware::instance is the first argument of the constexpr constructor.
 *   // Hardware::DMA contains that peripheral's existing request definitions.
 *   // Declare the static/global driver object with constinit.
 * @endcode
 * Fixed bindings are checked while evaluating the constructor. Device/package
 * bonding remains an application responsibility. There is no runtime table
 * mapping a peripheral address to its instance description.
 */
// ===========================================================================
// Section A: Compile-time pin tables — I2C class body
// ===========================================================================
#if defined(I2C_HPP_) && !defined(I2C_DEFS_CPP)

// Constant metadata; no address-to-instance table.

// Columns: periph, clk_reg, clk_bit, rst_reg, rst_bit,
//          kernel-clock selector (cleared -> PCLK), irq_ev, irq_er,
//          Fm+ bit in SYSCFG; DMA selection is defined in each _N::DMA.
#if defined(STM32G0)

#if defined(I2C3_BASE)
#  define I2C2_IRQ_LINE I2C2_3_IRQn     // G0B0/G0B1/G0C1: I2C2 and I2C3 share one line
#else
#  define I2C2_IRQ_LINE I2C2_IRQn
#endif
#if defined(RCC_CCIPR_I2C2SEL)
#  define I2C2_KSEL RCC_CCIPR_I2C2SEL
#else
#  define I2C2_KSEL 0u                  // I2C2 always clocked from PCLK
#endif

#endif

// F4 v1 I2C has neither a kernel selector nor Fm+; FMPI2C1 is a separate IP.

// ---------------------------------------------------------------------------
// STM32G0
// ---------------------------------------------------------------------------
#if defined(STM32G0)

#if defined(STM32G0B0xx) || defined(STM32G0B1xx) || defined(STM32G0C1xx)
#  define I2C_LINE_G0Bx
#endif

#if defined(I2C1_BASE)
struct _1 {
    struct DMA {
        static constexpr DMA_Sx::DMAReq RX = { 10U, I2C1_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq TX = { 11U, I2C1_BASE, DMA_Sx::DIR::To_Per };
    };
    static constexpr Instance instance = { I2C1_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APBENR1)), RCC_APBENR1_I2C1EN, (RCC_BASE + offsetof(RCC_TypeDef, APBRSTR1)), RCC_APBRSTR1_I2C1RST,
	  (RCC_BASE + offsetof(RCC_TypeDef, CCIPR)), RCC_CCIPR_I2C1SEL, I2C1_IRQn, I2C1_IRQn,
	  SYSCFG_CFGR1_I2C1_FMP,
        DMA::TX.ch, DMA::RX.ch
    };

	struct SCL {
		static constexpr PIN PA9  = { GPIOA_BASE,  9,  6, I2C1_BASE };
		static constexpr PIN PB6  = { GPIOB_BASE,  6,  6, I2C1_BASE };
		static constexpr PIN PB8  = { GPIOB_BASE,  8,  6, I2C1_BASE };
	};
	struct SDA {
		static constexpr PIN PA10 = { GPIOA_BASE, 10,  6, I2C1_BASE };
		static constexpr PIN PB7  = { GPIOB_BASE,  7,  6, I2C1_BASE };
		static constexpr PIN PB9  = { GPIOB_BASE,  9,  6, I2C1_BASE };
	};
};
#endif

#if defined(I2C2_BASE)
struct _2 {
    struct DMA {
        static constexpr DMA_Sx::DMAReq RX = { 12U, I2C2_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq TX = { 13U, I2C2_BASE, DMA_Sx::DIR::To_Per };
    };
    static constexpr Instance instance = { I2C2_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APBENR1)), RCC_APBENR1_I2C2EN, (RCC_BASE + offsetof(RCC_TypeDef, APBRSTR1)), RCC_APBRSTR1_I2C2RST,
	  (RCC_BASE + offsetof(RCC_TypeDef, CCIPR)), I2C2_KSEL, I2C2_IRQ_LINE, I2C2_IRQ_LINE,
	  SYSCFG_CFGR1_I2C2_FMP,
        DMA::TX.ch, DMA::RX.ch
    };

	struct SCL {
#if defined(I2C_LINE_G0Bx)
		static constexpr PIN PA7  = { GPIOA_BASE,  7,  8, I2C2_BASE };
#endif
#if defined(I2C_LINE_G0Bx)
		static constexpr PIN PA9  = { GPIOA_BASE,  9,  8, I2C2_BASE };
#endif
		static constexpr PIN PA11 = { GPIOA_BASE, 11,  6, I2C2_BASE };
#if defined(I2C_LINE_G0Bx)
		static constexpr PIN PB3  = { GPIOB_BASE,  3,  8, I2C2_BASE };
#endif
		static constexpr PIN PB10 = { GPIOB_BASE, 10,  6, I2C2_BASE };
		static constexpr PIN PB13 = { GPIOB_BASE, 13,  6, I2C2_BASE };
	};
	struct SDA {
#if defined(I2C_LINE_G0Bx)
		static constexpr PIN PA6  = { GPIOA_BASE,  6,  8, I2C2_BASE };
#endif
#if defined(I2C_LINE_G0Bx)
		static constexpr PIN PA10 = { GPIOA_BASE, 10,  8, I2C2_BASE };
#endif
		static constexpr PIN PA12 = { GPIOA_BASE, 12,  6, I2C2_BASE };
#if defined(I2C_LINE_G0Bx)
		static constexpr PIN PB4  = { GPIOB_BASE,  4,  8, I2C2_BASE };
#endif
		static constexpr PIN PB11 = { GPIOB_BASE, 11,  6, I2C2_BASE };
		static constexpr PIN PB14 = { GPIOB_BASE, 14,  6, I2C2_BASE };
	};
};
#endif

#if defined(I2C3_BASE)
struct _3 {
    struct DMA {
        static constexpr DMA_Sx::DMAReq RX = { 62U, I2C3_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq TX = { 63U, I2C3_BASE, DMA_Sx::DIR::To_Per };
    };
    static constexpr Instance instance = { I2C3_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APBENR1)), RCC_APBENR1_I2C3EN, (RCC_BASE + offsetof(RCC_TypeDef, APBRSTR1)), RCC_APBRSTR1_I2C3RST,
	  (RCC_BASE + offsetof(RCC_TypeDef, CCIPR)), 0u, I2C2_3_IRQn, I2C2_3_IRQn,
	  SYSCFG_CFGR1_I2C3_FMP,
        DMA::TX.ch, DMA::RX.ch
    };

	struct SCL {
		static constexpr PIN PA7  = { GPIOA_BASE,  7,  9, I2C3_BASE };
		static constexpr PIN PB3  = { GPIOB_BASE,  3,  6, I2C3_BASE };
#if defined(GPIOC_BASE)
		static constexpr PIN PC0  = { GPIOC_BASE,  0,  6, I2C3_BASE };
#endif
	};
	struct SDA {
		static constexpr PIN PA6  = { GPIOA_BASE,  6,  9, I2C3_BASE };
		static constexpr PIN PB4  = { GPIOB_BASE,  4,  6, I2C3_BASE };
#if defined(GPIOC_BASE)
		static constexpr PIN PC1  = { GPIOC_BASE,  1,  6, I2C3_BASE };
#endif
	};
};
#endif

// ---------------------------------------------------------------------------
// STM32F4
// ---------------------------------------------------------------------------
#elif defined(STM32F4)

#if defined(STM32F410Cx) || defined(STM32F410Rx) || defined(STM32F410Tx)
#  define I2C_LINE_F410
#endif
#if defined(STM32F411xE)
#  define I2C_LINE_F411
#endif
#if defined(STM32F412Cx) || defined(STM32F412Rx) || defined(STM32F412Vx) || defined(STM32F412Zx)
#  define I2C_LINE_F412
#endif
#if defined(STM32F413xx) || defined(STM32F423xx)
#  define I2C_LINE_F413
#endif
#if defined(STM32F405xx) || defined(STM32F407xx) || defined(STM32F415xx) || defined(STM32F417xx)
#  define I2C_LINE_F417
#endif
#if defined(STM32F427xx) || defined(STM32F429xx) || defined(STM32F437xx) || defined(STM32F439xx)
#  define I2C_LINE_F427
#endif
#if defined(STM32F446xx)
#  define I2C_LINE_F446
#endif
#if defined(STM32F469xx) || defined(STM32F479xx)
#  define I2C_LINE_F469
#endif

#if defined(I2C1_BASE)
struct _1 {
    struct DMA {
        static constexpr DMA_Sx::DMAReq RX_DMA1_Stream0 = { DMA1_Stream0_BASE, 1U, I2C1_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq RX_DMA1_Stream5 = { DMA1_Stream5_BASE, 1U, I2C1_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq TX_DMA1_Stream6 = { DMA1_Stream6_BASE, 1U, I2C1_BASE, DMA_Sx::DIR::To_Per };
        static constexpr DMA_Sx::DMAReq TX_DMA1_Stream7 = { DMA1_Stream7_BASE, 1U, I2C1_BASE, DMA_Sx::DIR::To_Per };
    };
    static constexpr Instance instance = { I2C1_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APB1ENR)), RCC_APB1ENR_I2C1EN, (RCC_BASE + offsetof(RCC_TypeDef, APB1RSTR)), RCC_APB1RSTR_I2C1RST,
	  0, 0u, I2C1_EV_IRQn, I2C1_ER_IRQn,
	  0u

 };

	struct SCL {
		static constexpr PIN PB6  = { GPIOB_BASE,  6,  4, I2C1_BASE };
		static constexpr PIN PB8  = { GPIOB_BASE,  8,  4, I2C1_BASE };
	};
	struct SDA {
		static constexpr PIN PB7  = { GPIOB_BASE,  7,  4, I2C1_BASE };
		static constexpr PIN PB9  = { GPIOB_BASE,  9,  4, I2C1_BASE };
	};
};
#endif

#if defined(I2C2_BASE)
struct _2 {
    struct DMA {
        static constexpr DMA_Sx::DMAReq RX_DMA1_Stream2 = { DMA1_Stream2_BASE, 7U, I2C2_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq RX_DMA1_Stream3 = { DMA1_Stream3_BASE, 7U, I2C2_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq TX_DMA1_Stream7 = { DMA1_Stream7_BASE, 7U, I2C2_BASE, DMA_Sx::DIR::To_Per };
    };
    static constexpr Instance instance = { I2C2_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APB1ENR)), RCC_APB1ENR_I2C2EN, (RCC_BASE + offsetof(RCC_TypeDef, APB1RSTR)), RCC_APB1RSTR_I2C2RST,
	  0, 0u, I2C2_EV_IRQn, I2C2_ER_IRQn,
	  0u

 };

	struct SCL {
		static constexpr PIN PB10 = { GPIOB_BASE, 10,  4, I2C2_BASE };
#if defined(GPIOF_BASE)
		static constexpr PIN PF1  = { GPIOF_BASE,  1,  4, I2C2_BASE };
#endif
#if (defined(I2C_LINE_F417) || defined(I2C_LINE_F427) || defined(I2C_LINE_F469)) && defined(GPIOH_BASE)
		static constexpr PIN PH4  = { GPIOH_BASE,  4,  4, I2C2_BASE };
#endif
	};
	struct SDA {
#if defined(I2C_LINE_F446)
		static constexpr PIN PB3  = { GPIOB_BASE,  3,  4, I2C2_BASE };
#endif
#if !(defined(I2C_LINE_F417) || defined(I2C_LINE_F427) || defined(I2C_LINE_F446) || defined(I2C_LINE_F469))
		static constexpr PIN PB3  = { GPIOB_BASE,  3,  9, I2C2_BASE };
#endif
#if (defined(I2C_LINE_F410) || defined(I2C_LINE_F411) || defined(I2C_LINE_F412) || defined(I2C_LINE_F413))
		static constexpr PIN PB9  = { GPIOB_BASE,  9,  9, I2C2_BASE };
#endif
		static constexpr PIN PB11 = { GPIOB_BASE, 11,  4, I2C2_BASE };
#if defined(I2C_LINE_F446) && defined(GPIOC_BASE)
		static constexpr PIN PC12 = { GPIOC_BASE, 12,  4, I2C2_BASE };
#endif
#if defined(GPIOF_BASE)
		static constexpr PIN PF0  = { GPIOF_BASE,  0,  4, I2C2_BASE };
#endif
#if (defined(I2C_LINE_F417) || defined(I2C_LINE_F427) || defined(I2C_LINE_F469)) && defined(GPIOH_BASE)
		static constexpr PIN PH5  = { GPIOH_BASE,  5,  4, I2C2_BASE };
#endif
	};
};
#endif

#if defined(I2C3_BASE)
struct _3 {
    struct DMA {
        static constexpr DMA_Sx::DMAReq RX_DMA1_Stream2 = { DMA1_Stream2_BASE, 3U, I2C3_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq TX_DMA1_Stream4 = { DMA1_Stream4_BASE, 3U, I2C3_BASE, DMA_Sx::DIR::To_Per };
#if defined(STM32F7)
        static constexpr DMA_Sx::DMAReq RX_DMA1_Stream1 = { DMA1_Stream1_BASE, 1U, I2C3_BASE, DMA_Sx::DIR::From_Per };
#endif
    };
    static constexpr Instance instance = { I2C3_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APB1ENR)), RCC_APB1ENR_I2C3EN, (RCC_BASE + offsetof(RCC_TypeDef, APB1RSTR)), RCC_APB1RSTR_I2C3RST,
	  0, 0u, I2C3_EV_IRQn, I2C3_ER_IRQn,
	  0u

 };

	struct SCL {
		static constexpr PIN PA8  = { GPIOA_BASE,  8,  4, I2C3_BASE };
#if (defined(I2C_LINE_F417) || defined(I2C_LINE_F427) || defined(I2C_LINE_F469)) && defined(GPIOH_BASE)
		static constexpr PIN PH7  = { GPIOH_BASE,  7,  4, I2C3_BASE };
#endif
	};
	struct SDA {
#if defined(I2C_LINE_F446)
		static constexpr PIN PB4  = { GPIOB_BASE,  4,  4, I2C3_BASE };
#endif
#if !(defined(I2C_LINE_F417) || defined(I2C_LINE_F427) || defined(I2C_LINE_F446) || defined(I2C_LINE_F469))
		static constexpr PIN PB4  = { GPIOB_BASE,  4,  9, I2C3_BASE };
#endif
#if (defined(I2C_LINE_F411) || defined(I2C_LINE_F412) || defined(I2C_LINE_F413))
		static constexpr PIN PB8  = { GPIOB_BASE,  8,  9, I2C3_BASE };
#endif
#if defined(GPIOC_BASE)
		static constexpr PIN PC9  = { GPIOC_BASE,  9,  4, I2C3_BASE };
#endif
#if (defined(I2C_LINE_F417) || defined(I2C_LINE_F427) || defined(I2C_LINE_F469)) && defined(GPIOH_BASE)
		static constexpr PIN PH8  = { GPIOH_BASE,  8,  4, I2C3_BASE };
#endif
	};
};
#endif

// ---------------------------------------------------------------------------
// STM32F7
// ---------------------------------------------------------------------------
#elif defined(STM32F7)

#if defined(STM32F745xx) || defined(STM32F746xx) || defined(STM32F750xx) || defined(STM32F756xx)
#  define I2C_LINE_F746
#endif

#if defined(I2C1_BASE)
struct _1 {
    struct DMA {
        static constexpr DMA_Sx::DMAReq RX_DMA1_Stream0 = { DMA1_Stream0_BASE, 1U, I2C1_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq RX_DMA1_Stream5 = { DMA1_Stream5_BASE, 1U, I2C1_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq TX_DMA1_Stream6 = { DMA1_Stream6_BASE, 1U, I2C1_BASE, DMA_Sx::DIR::To_Per };
        static constexpr DMA_Sx::DMAReq TX_DMA1_Stream7 = { DMA1_Stream7_BASE, 1U, I2C1_BASE, DMA_Sx::DIR::To_Per };
    };
    static constexpr Instance instance = { I2C1_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APB1ENR)), RCC_APB1ENR_I2C1EN, (RCC_BASE + offsetof(RCC_TypeDef, APB1RSTR)), RCC_APB1RSTR_I2C1RST,
	  (RCC_BASE + offsetof(RCC_TypeDef, DCKCFGR2)), RCC_DCKCFGR2_I2C1SEL, I2C1_EV_IRQn, I2C1_ER_IRQn,
	  SYSCFG_PMC_I2C1_FMP

 };

	struct SCL {
		static constexpr PIN PB6  = { GPIOB_BASE,  6,  4, I2C1_BASE };
		static constexpr PIN PB8  = { GPIOB_BASE,  8,  4, I2C1_BASE };
	};
	struct SDA {
		static constexpr PIN PB7  = { GPIOB_BASE,  7,  4, I2C1_BASE };
		static constexpr PIN PB9  = { GPIOB_BASE,  9,  4, I2C1_BASE };
	};
};
#endif

#if defined(I2C2_BASE)
struct _2 {
    struct DMA {
        static constexpr DMA_Sx::DMAReq RX_DMA1_Stream2 = { DMA1_Stream2_BASE, 7U, I2C2_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq RX_DMA1_Stream3 = { DMA1_Stream3_BASE, 7U, I2C2_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq TX_DMA1_Stream7 = { DMA1_Stream7_BASE, 7U, I2C2_BASE, DMA_Sx::DIR::To_Per };
    };
    static constexpr Instance instance = { I2C2_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APB1ENR)), RCC_APB1ENR_I2C2EN, (RCC_BASE + offsetof(RCC_TypeDef, APB1RSTR)), RCC_APB1RSTR_I2C2RST,
	  (RCC_BASE + offsetof(RCC_TypeDef, DCKCFGR2)), RCC_DCKCFGR2_I2C2SEL, I2C2_EV_IRQn, I2C2_ER_IRQn,
	  SYSCFG_PMC_I2C2_FMP

 };

	struct SCL {
		static constexpr PIN PB10 = { GPIOB_BASE, 10,  4, I2C2_BASE };
#if defined(GPIOF_BASE)
		static constexpr PIN PF1  = { GPIOF_BASE,  1,  4, I2C2_BASE };
#endif
#if defined(GPIOH_BASE)
		static constexpr PIN PH4  = { GPIOH_BASE,  4,  4, I2C2_BASE };
#endif
	};
	struct SDA {
		static constexpr PIN PB11 = { GPIOB_BASE, 11,  4, I2C2_BASE };
#if defined(GPIOF_BASE)
		static constexpr PIN PF0  = { GPIOF_BASE,  0,  4, I2C2_BASE };
#endif
#if defined(GPIOH_BASE)
		static constexpr PIN PH5  = { GPIOH_BASE,  5,  4, I2C2_BASE };
#endif
	};
};
#endif

#if defined(I2C3_BASE)
struct _3 {
    struct DMA {
        static constexpr DMA_Sx::DMAReq RX_DMA1_Stream2 = { DMA1_Stream2_BASE, 3U, I2C3_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq TX_DMA1_Stream4 = { DMA1_Stream4_BASE, 3U, I2C3_BASE, DMA_Sx::DIR::To_Per };
#if defined(STM32F7)
        static constexpr DMA_Sx::DMAReq RX_DMA1_Stream1 = { DMA1_Stream1_BASE, 1U, I2C3_BASE, DMA_Sx::DIR::From_Per };
#endif
    };
    static constexpr Instance instance = { I2C3_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APB1ENR)), RCC_APB1ENR_I2C3EN, (RCC_BASE + offsetof(RCC_TypeDef, APB1RSTR)), RCC_APB1RSTR_I2C3RST,
	  (RCC_BASE + offsetof(RCC_TypeDef, DCKCFGR2)), RCC_DCKCFGR2_I2C3SEL, I2C3_EV_IRQn, I2C3_ER_IRQn,
	  SYSCFG_PMC_I2C3_FMP

 };

	struct SCL {
		static constexpr PIN PA8  = { GPIOA_BASE,  8,  4, I2C3_BASE };
#if defined(GPIOH_BASE)
		static constexpr PIN PH7  = { GPIOH_BASE,  7,  4, I2C3_BASE };
#endif
	};
	struct SDA {
#if defined(GPIOC_BASE)
		static constexpr PIN PC9  = { GPIOC_BASE,  9,  4, I2C3_BASE };
#endif
#if defined(GPIOH_BASE)
		static constexpr PIN PH8  = { GPIOH_BASE,  8,  4, I2C3_BASE };
#endif
	};
};
#endif

#if defined(I2C4_BASE)
struct _4 {
    struct DMA {
        static constexpr DMA_Sx::DMAReq RX_DMA1_Stream2 = { DMA1_Stream2_BASE, 2U, I2C4_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq TX_DMA1_Stream5 = { DMA1_Stream5_BASE, 2U, I2C4_BASE, DMA_Sx::DIR::To_Per };
    };
    static constexpr Instance instance = { I2C4_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APB1ENR)), RCC_APB1ENR_I2C4EN, (RCC_BASE + offsetof(RCC_TypeDef, APB1RSTR)), RCC_APB1RSTR_I2C4RST,
	  (RCC_BASE + offsetof(RCC_TypeDef, DCKCFGR2)), RCC_DCKCFGR2_I2C4SEL, I2C4_EV_IRQn, I2C4_ER_IRQn,
	  SYSCFG_PMC_I2C4_FMP

 };

	struct SCL {
#if !defined(I2C_LINE_F746)
		static constexpr PIN PB6  = { GPIOB_BASE,  6, 11, I2C4_BASE };
#endif
#if !defined(I2C_LINE_F746)
		static constexpr PIN PB8  = { GPIOB_BASE,  8,  1, I2C4_BASE };
#endif
#if defined(GPIOD_BASE)
		static constexpr PIN PD12 = { GPIOD_BASE, 12,  4, I2C4_BASE };
#endif
#if defined(GPIOF_BASE)
		static constexpr PIN PF14 = { GPIOF_BASE, 14,  4, I2C4_BASE };
#endif
#if defined(GPIOH_BASE)
		static constexpr PIN PH11 = { GPIOH_BASE, 11,  4, I2C4_BASE };
#endif
	};
	struct SDA {
#if !defined(I2C_LINE_F746)
		static constexpr PIN PB7  = { GPIOB_BASE,  7, 11, I2C4_BASE };
#endif
#if !defined(I2C_LINE_F746)
		static constexpr PIN PB9  = { GPIOB_BASE,  9,  1, I2C4_BASE };
#endif
#if defined(GPIOD_BASE)
		static constexpr PIN PD13 = { GPIOD_BASE, 13,  4, I2C4_BASE };
#endif
#if defined(GPIOF_BASE)
		static constexpr PIN PF15 = { GPIOF_BASE, 15,  4, I2C4_BASE };
#endif
#if defined(GPIOH_BASE)
		static constexpr PIN PH12 = { GPIOH_BASE, 12,  4, I2C4_BASE };
#endif
	};
};
#endif

#endif  // STM32 family

#endif  // Section A

#undef I2C2_IRQ_LINE
#undef I2C2_KSEL
