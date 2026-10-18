/**
 * @file    i2c_defs.hpp
 * @brief   All compile-time and run-time tables for the I2C driver.
 *
 * Same two-section layout as spi_defs.hpp / adc_defs.hpp:
 *
 * Section A — compile-time pin tables
 * -------------------------------------
 * Guard:  I2C_HPP_ && !I2C_DEFS_CPP
 * Scope:  I2C class body — public nested pin types.
 * Result: I2C::_N::SCL::<PXn> / I2C::_N::SDA::<PXn>.
 *         Each entry packs: GPIO port base, pin number, alternate function,
 *         owning I2C base address.  Generated from the STM32CubeMX GPIO
 *         database; pins that exist only on some lines of a family are
 *         guarded by I2C_LINE_* (defined below from the device macro).
 *
 * Section B — peripheral-info table
 * -----------------------------------
 * Guard:  I2C_DEFS_CPP  (defined in i2c.cpp just before the include)
 * Scope:  file scope inside i2c.cpp.
 * Result: I2C::i2c_table[] — clock/reset bits, kernel clock selector,
 *         NVIC lines, DMA requests, Fast-mode Plus bit.
 */

// ===========================================================================
// Section A: Compile-time pin tables — I2C class body
// ===========================================================================
#if defined(I2C_HPP_) && !defined(I2C_DEFS_CPP)

// ---------------------------------------------------------------------------
// STM32G0
// ---------------------------------------------------------------------------
#if defined(STM32G0)

#if defined(STM32G0B0xx) || defined(STM32G0B1xx) || defined(STM32G0C1xx)
#  define I2C_LINE_G0Bx
#endif

#if defined(I2C1_BASE)
struct _1 {
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

// ===========================================================================
// Section B: Peripheral-info table — i2c.cpp file scope
// ===========================================================================
#ifdef I2C_DEFS_CPP

#if defined(STM32F405xx) || defined(STM32F407xx) || defined(STM32F415xx) || defined(STM32F417xx) \
 || defined(STM32F427xx) || defined(STM32F429xx) || defined(STM32F437xx) || defined(STM32F439xx) \
 || defined(STM32F469xx) || defined(STM32F479xx)
#  define I2C3_RX_ALTERNATE {}
#else
#  define I2C3_RX_ALTERNATE DMA_Sx::Req::I2c3::RX_alt
#endif

// Columns: periph, clk_reg, clk_bit, rst_reg, rst_bit,
//          kernel-clock selector (cleared -> PCLK), irq_ev, irq_er,
//          DMA TX / RX request (G0: DMAMUX ID, F4/F7: CHSEL), Fm+ bit in SYSCFG.
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

const I2C::PeriphInfo I2C::i2c_table[] = {
	{ I2C1, &RCC->APBENR1, RCC_APBENR1_I2C1EN, &RCC->APBRSTR1, RCC_APBRSTR1_I2C1RST,
	  &RCC->CCIPR, RCC_CCIPR_I2C1SEL, I2C1_IRQn, I2C1_IRQn,
	  DMA_Sx::Req::I2c1::TX, DMA_Sx::Req::I2c1::RX, SYSCFG_CFGR1_I2C1_FMP },
	{ I2C2, &RCC->APBENR1, RCC_APBENR1_I2C2EN, &RCC->APBRSTR1, RCC_APBRSTR1_I2C2RST,
	  &RCC->CCIPR, I2C2_KSEL, I2C2_IRQ_LINE, I2C2_IRQ_LINE,
	  DMA_Sx::Req::I2c2::TX, DMA_Sx::Req::I2c2::RX, SYSCFG_CFGR1_I2C2_FMP },
#if defined(I2C3_BASE)
	{ I2C3, &RCC->APBENR1, RCC_APBENR1_I2C3EN, &RCC->APBRSTR1, RCC_APBRSTR1_I2C3RST,
	  &RCC->CCIPR, 0u, I2C2_3_IRQn, I2C2_3_IRQn,
	  DMA_Sx::Req::I2c3::TX, DMA_Sx::Req::I2c3::RX, SYSCFG_CFGR1_I2C3_FMP },
#endif
};

#undef I2C2_IRQ_LINE
#undef I2C2_KSEL

#elif defined(STM32F7)

const I2C::PeriphInfo I2C::i2c_table[] = {
	{ I2C1, &RCC->APB1ENR, RCC_APB1ENR_I2C1EN, &RCC->APB1RSTR, RCC_APB1RSTR_I2C1RST,
	  &RCC->DCKCFGR2, RCC_DCKCFGR2_I2C1SEL, I2C1_EV_IRQn, I2C1_ER_IRQn,
	  {DMA_Sx::Req::I2c1::TX_DMA1_Stream6, DMA_Sx::Req::I2c1::TX_alt}, {DMA_Sx::Req::I2c1::RX_DMA1_Stream0, DMA_Sx::Req::I2c1::RX_DMA1_Stream5}, SYSCFG_PMC_I2C1_FMP },
	{ I2C2, &RCC->APB1ENR, RCC_APB1ENR_I2C2EN, &RCC->APB1RSTR, RCC_APB1RSTR_I2C2RST,
	  &RCC->DCKCFGR2, RCC_DCKCFGR2_I2C2SEL, I2C2_EV_IRQn, I2C2_ER_IRQn,
	  DMA_Sx::Req::I2c2::TX_DMA1_Stream7, {DMA_Sx::Req::I2c2::RX_DMA1_Stream2, DMA_Sx::Req::I2c2::RX_DMA1_Stream3}, SYSCFG_PMC_I2C2_FMP },
	{ I2C3, &RCC->APB1ENR, RCC_APB1ENR_I2C3EN, &RCC->APB1RSTR, RCC_APB1RSTR_I2C3RST,
	  &RCC->DCKCFGR2, RCC_DCKCFGR2_I2C3SEL, I2C3_EV_IRQn, I2C3_ER_IRQn,
	  DMA_Sx::Req::I2c3::TX_DMA1_Stream4, {DMA_Sx::Req::I2c3::RX_DMA1_Stream2, I2C3_RX_ALTERNATE}, SYSCFG_PMC_I2C3_FMP },
#if defined(I2C4_BASE)
	{ I2C4, &RCC->APB1ENR, RCC_APB1ENR_I2C4EN, &RCC->APB1RSTR, RCC_APB1RSTR_I2C4RST,
	  &RCC->DCKCFGR2, RCC_DCKCFGR2_I2C4SEL, I2C4_EV_IRQn, I2C4_ER_IRQn,
	  DMA_Sx::Req::I2c4::TX, DMA_Sx::Req::I2c4::RX, SYSCFG_PMC_I2C4_FMP },
#endif
};

#elif defined(STM32F4)

// F4 I2C (v1 IP) has no kernel clock selector and no Fm+ (FMPI2C1 is a separate IP).
const I2C::PeriphInfo I2C::i2c_table[] = {
	{ I2C1, &RCC->APB1ENR, RCC_APB1ENR_I2C1EN, &RCC->APB1RSTR, RCC_APB1RSTR_I2C1RST,
	  nullptr, 0u, I2C1_EV_IRQn, I2C1_ER_IRQn,
	  {DMA_Sx::Req::I2c1::TX_DMA1_Stream6, DMA_Sx::Req::I2c1::TX_alt}, {DMA_Sx::Req::I2c1::RX_DMA1_Stream0, DMA_Sx::Req::I2c1::RX_DMA1_Stream5}, 0u },
	{ I2C2, &RCC->APB1ENR, RCC_APB1ENR_I2C2EN, &RCC->APB1RSTR, RCC_APB1RSTR_I2C2RST,
	  nullptr, 0u, I2C2_EV_IRQn, I2C2_ER_IRQn,
	  DMA_Sx::Req::I2c2::TX_DMA1_Stream7, {DMA_Sx::Req::I2c2::RX_DMA1_Stream2, DMA_Sx::Req::I2c2::RX_DMA1_Stream3}, 0u },
#if defined(I2C3_BASE)
	{ I2C3, &RCC->APB1ENR, RCC_APB1ENR_I2C3EN, &RCC->APB1RSTR, RCC_APB1RSTR_I2C3RST,
	  nullptr, 0u, I2C3_EV_IRQn, I2C3_ER_IRQn,
	  DMA_Sx::Req::I2c3::TX_DMA1_Stream4, {DMA_Sx::Req::I2c3::RX_DMA1_Stream2, I2C3_RX_ALTERNATE}, 0u },
#endif
};

#endif

#undef I2C3_RX_ALTERNATE

#endif  // I2C_DEFS_CPP (Section B)
