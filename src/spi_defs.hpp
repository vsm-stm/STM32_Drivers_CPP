/**
 * @file spi_defs.hpp
 * @brief Selected-device instance, pin and DMA definitions for SPI.
 *
 * The public driver header includes the declaration section inside its class.
 * Each _N owns an immutable constexpr instance, named pins and DMA routes;
 * unavailable instances are omitted by the selected CMSIS device guards.
 * Numeric peripheral/RCC addresses support constant initialization without MMIO.
 * Bus-clock pointers refer to ordinary System variables, not hardware registers.
 *
 * Application code includes spi.hpp and can shorten names with:
 * @code
 *   using Hardware = SPI::_1;
 *   // Hardware::instance is the first argument of the constexpr constructor.
 *   // Hardware::DMA contains that peripheral's existing request definitions.
 *   // Declare the static/global driver object with constinit.
 * @endcode
 * Fixed bindings are checked while evaluating the constructor. Device/package
 * bonding remains an application responsibility. There is no runtime table
 * mapping a peripheral address to its instance description.
 */
// ===========================================================================
// Section A: Compile-time pin tables — SPI class body
// ===========================================================================
#if defined(SPI_HPP_) && !defined(SPI_DEFS_CPP)

// Constant metadata; no address-to-instance table.

// ---------------------------------------------------------------------------
// Each constexpr PIN packs: GPIO port base, pin number, alternate-function
// index, and the owning SPI base address.  Zero RAM footprint.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// STM32G0
// ---------------------------------------------------------------------------
#if defined(STM32G0)

#ifdef SPI1_BASE
struct _1 {
    struct DMA {
        static constexpr DMA_Sx::DMAReq RX = { 16U, SPI1_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq TX = { 17U, SPI1_BASE, DMA_Sx::DIR::To_Per };
    };
    static constexpr Instance instance = { SPI1_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APBENR2)), RCC_APBENR2_SPI1EN, (RCC_BASE + offsetof(RCC_TypeDef, APBRSTR2)), RCC_APBRSTR2_SPI1RST, &System::APB1BusClock, SPI1_IRQn,
        DMA::TX.ch, DMA::RX.ch
    };

	struct SCK {
		static constexpr PIN PA5 = { GPIOA_BASE,  5, 0, SPI1_BASE };
		static constexpr PIN PB3 = { GPIOB_BASE,  3, 0, SPI1_BASE };
	};
	struct MOSI {
		static constexpr PIN PA7 = { GPIOA_BASE,  7, 0, SPI1_BASE };
		static constexpr PIN PB5 = { GPIOB_BASE,  5, 0, SPI1_BASE };
	};
	struct MISO {
		static constexpr PIN PA6 = { GPIOA_BASE,  6, 0, SPI1_BASE };
		static constexpr PIN PB4 = { GPIOB_BASE,  4, 0, SPI1_BASE };
	};
	struct SS {
		static constexpr PIN PA4  = { GPIOA_BASE,  4,  0, SPI1_BASE };
		static constexpr PIN PA15 = { GPIOA_BASE, 15,  0, SPI1_BASE };
	};
};
#endif

#ifdef SPI2_BASE
struct _2 {
    struct DMA {
        static constexpr DMA_Sx::DMAReq RX = { 18U, SPI2_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq TX = { 19U, SPI2_BASE, DMA_Sx::DIR::To_Per };
    };
    static constexpr Instance instance = { SPI2_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APBENR1)), RCC_APBENR1_SPI2EN, (RCC_BASE + offsetof(RCC_TypeDef, APBRSTR1)), RCC_APBRSTR1_SPI2RST, &System::APB1BusClock,
#ifdef SPI3_BASE
      SPI2_3_IRQn
#else
      SPI2_IRQn
#endif,
        DMA::TX.ch, DMA::RX.ch
    };

	struct SCK {
		static constexpr PIN PB8  = { GPIOB_BASE,  8, 1, SPI2_BASE };
		static constexpr PIN PB10 = { GPIOB_BASE, 10, 5, SPI2_BASE };
		static constexpr PIN PB13 = { GPIOB_BASE, 13, 0, SPI2_BASE };
	};
	struct MOSI {
		static constexpr PIN PB7  = { GPIOB_BASE,  7, 1, SPI2_BASE };
		static constexpr PIN PB11 = { GPIOB_BASE, 11, 0, SPI2_BASE };
		static constexpr PIN PB15 = { GPIOB_BASE, 15, 0, SPI2_BASE };
	};
	struct MISO {
		static constexpr PIN PB6  = { GPIOB_BASE,  6, 4, SPI2_BASE };
		static constexpr PIN PB14 = { GPIOB_BASE, 14, 0, SPI2_BASE };
	};
	struct SS {
		static constexpr PIN PB9  = { GPIOB_BASE,  9, 5, SPI2_BASE };
		static constexpr PIN PB12 = { GPIOB_BASE, 12, 0, SPI2_BASE };
	};
};
#endif

#ifdef SPI3_BASE
struct _3 {
    struct DMA {
        static constexpr DMA_Sx::DMAReq RX = { 66U, SPI3_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq TX = { 67U, SPI3_BASE, DMA_Sx::DIR::To_Per };
    };
    static constexpr Instance instance = { SPI3_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APBENR1)), RCC_APBENR1_SPI3EN, (RCC_BASE + offsetof(RCC_TypeDef, APBRSTR1)), RCC_APBRSTR1_SPI3RST, &System::APB1BusClock, SPI2_3_IRQn,
        DMA::TX.ch, DMA::RX.ch
    };

	struct SCK {
		static constexpr PIN PB3 = { GPIOB_BASE, 3, 9, SPI3_BASE };
		static constexpr PIN PC10 = { GPIOC_BASE, 10, 4, SPI3_BASE };
	};
	struct MOSI {
		static constexpr PIN PB5 = { GPIOB_BASE, 5, 9, SPI3_BASE };
		static constexpr PIN PC12 = { GPIOC_BASE, 12, 4, SPI3_BASE };
	};
	struct MISO {
		static constexpr PIN PB4 = { GPIOB_BASE, 4, 9, SPI3_BASE };
		static constexpr PIN PC11 = { GPIOC_BASE, 11, 4, SPI3_BASE };
	};
	struct SS {
		static constexpr PIN PA4 = { GPIOA_BASE, 4, 9, SPI3_BASE };
		static constexpr PIN PA15 = { GPIOA_BASE, 15, 9, SPI3_BASE };
	};
};
#endif

// ---------------------------------------------------------------------------
// STM32F4 / STM32F7
// ---------------------------------------------------------------------------
#elif defined(STM32F4) || defined(STM32F7)

#ifdef SPI1_BASE
struct _1 {
    struct DMA {
        static constexpr DMA_Sx::DMAReq RX_DMA2_Stream0 = { DMA2_Stream0_BASE, 3U, SPI1_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq TX_DMA2_Stream3 = { DMA2_Stream3_BASE, 3U, SPI1_BASE, DMA_Sx::DIR::To_Per };
        static constexpr DMA_Sx::DMAReq RX_DMA2_Stream2 = { DMA2_Stream2_BASE, 3U, SPI1_BASE, DMA_Sx::DIR::From_Per };  // Stream2 Ch3
        static constexpr DMA_Sx::DMAReq TX_DMA2_Stream5 = { DMA2_Stream5_BASE, 3U, SPI1_BASE, DMA_Sx::DIR::To_Per };  // Stream5 Ch3
    };
    static constexpr Instance instance = { SPI1_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APB2ENR)), RCC_APB2ENR_SPI1EN, (RCC_BASE + offsetof(RCC_TypeDef, APB2RSTR)), RCC_APB2RSTR_SPI1RST, &System::APB2BusClock, SPI1_IRQn

 };

	struct SCK {
		static constexpr PIN PA5 = { GPIOA_BASE, 5, 5, SPI1_BASE };
		static constexpr PIN PB3 = { GPIOB_BASE, 3, 5, SPI1_BASE };
	};
	struct MOSI {
		static constexpr PIN PA7 = { GPIOA_BASE, 7, 5, SPI1_BASE };
		static constexpr PIN PB5 = { GPIOB_BASE, 5, 5, SPI1_BASE };
	};
	struct MISO {
		static constexpr PIN PA6 = { GPIOA_BASE, 6, 5, SPI1_BASE };
		static constexpr PIN PB4 = { GPIOB_BASE, 4, 5, SPI1_BASE };
	};
	struct SS {
		static constexpr PIN PA4  = { GPIOA_BASE,  4, 5, SPI1_BASE };
		static constexpr PIN PA15 = { GPIOA_BASE, 15, 5, SPI1_BASE };
	};
};
#endif

#ifdef SPI2_BASE
struct _2 {
    struct DMA {
        static constexpr DMA_Sx::DMAReq RX_DMA1_Stream3 = { DMA1_Stream3_BASE, 0U, SPI2_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq TX_DMA1_Stream4 = { DMA1_Stream4_BASE, 0U, SPI2_BASE, DMA_Sx::DIR::To_Per };
    };
    static constexpr Instance instance = { SPI2_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APB1ENR)), RCC_APB1ENR_SPI2EN, (RCC_BASE + offsetof(RCC_TypeDef, APB1RSTR)), RCC_APB1RSTR_SPI2RST, &System::APB1BusClock, SPI2_IRQn

 };

	struct SCK {
		static constexpr PIN PB10 = { GPIOB_BASE, 10, 5, SPI2_BASE };
		static constexpr PIN PB13 = { GPIOB_BASE, 13, 5, SPI2_BASE };
	};
	struct MOSI {
		static constexpr PIN PB15 = { GPIOB_BASE, 15, 5, SPI2_BASE };
		static constexpr PIN PC3  = { GPIOC_BASE,  3, 5, SPI2_BASE };
	};
	struct MISO {
		static constexpr PIN PB14 = { GPIOB_BASE, 14, 5, SPI2_BASE };
		static constexpr PIN PC2  = { GPIOC_BASE,  2, 5, SPI2_BASE };
	};
	struct SS {
		static constexpr PIN PB9  = { GPIOB_BASE,  9, 5, SPI2_BASE };
		static constexpr PIN PB12 = { GPIOB_BASE, 12, 5, SPI2_BASE };
	};
};
#endif

#ifdef SPI3_BASE
struct _3 {
    struct DMA {
        static constexpr DMA_Sx::DMAReq RX_DMA1_Stream0 = { DMA1_Stream0_BASE, 0U, SPI3_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq TX_DMA1_Stream5 = { DMA1_Stream5_BASE, 0U, SPI3_BASE, DMA_Sx::DIR::To_Per };
        static constexpr DMA_Sx::DMAReq RX_DMA1_Stream2 = { DMA1_Stream2_BASE, 0U, SPI3_BASE, DMA_Sx::DIR::From_Per };  // Stream2 Ch0
        static constexpr DMA_Sx::DMAReq TX_DMA1_Stream7 = { DMA1_Stream7_BASE, 0U, SPI3_BASE, DMA_Sx::DIR::To_Per };  // Stream7 Ch0
    };
    static constexpr Instance instance = { SPI3_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APB1ENR)), RCC_APB1ENR_SPI3EN, (RCC_BASE + offsetof(RCC_TypeDef, APB1RSTR)), RCC_APB1RSTR_SPI3RST, &System::APB1BusClock, SPI3_IRQn

 };

	struct SCK {
		static constexpr PIN PB3  = { GPIOB_BASE,  3, 6, SPI3_BASE };
		static constexpr PIN PC10 = { GPIOC_BASE, 10, 6, SPI3_BASE };
	};
	struct MOSI {
		static constexpr PIN PB5  = { GPIOB_BASE,  5, 6, SPI3_BASE };
		static constexpr PIN PC12 = { GPIOC_BASE, 12, 6, SPI3_BASE };
	};
	struct MISO {
		static constexpr PIN PB4  = { GPIOB_BASE,  4, 6, SPI3_BASE };
		static constexpr PIN PC11 = { GPIOC_BASE, 11, 6, SPI3_BASE };
	};
	struct SS {
		static constexpr PIN PA15 = { GPIOA_BASE, 15, 6, SPI3_BASE };
	};
};
#endif

#ifdef SPI4_BASE
struct _4 {
    struct DMA {
        static constexpr DMA_Sx::DMAReq RX_DMA2_Stream0 = { DMA2_Stream0_BASE, 4U, SPI4_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq RX_DMA2_Stream3 = { DMA2_Stream3_BASE, 5U, SPI4_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq TX_DMA2_Stream1 = { DMA2_Stream1_BASE, 4U, SPI4_BASE, DMA_Sx::DIR::To_Per };
        static constexpr DMA_Sx::DMAReq TX_DMA2_Stream4 = { DMA2_Stream4_BASE, 5U, SPI4_BASE, DMA_Sx::DIR::To_Per };
    };
    static constexpr Instance instance = { SPI4_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APB2ENR)), RCC_APB2ENR_SPI4EN, (RCC_BASE + offsetof(RCC_TypeDef, APB2RSTR)), RCC_APB2RSTR_SPI4RST, &System::APB2BusClock, SPI4_IRQn

 };

	struct SCK {
#if defined(STM32F411xE) || defined(STM32F412Cx) || defined(STM32F412Rx) || defined(STM32F412Vx) || defined(STM32F412Zx) || defined(STM32F413xx) || defined(STM32F423xx)
		static constexpr PIN PB13 = { GPIOB_BASE, 13, 6, SPI4_BASE };
#endif
		static constexpr PIN PE2 = { GPIOE_BASE, 2, 5, SPI4_BASE };
		static constexpr PIN PE12 = { GPIOE_BASE, 12, 5, SPI4_BASE };
	};
	struct MOSI {
#if defined(STM32F411xE) || defined(STM32F412Cx) || defined(STM32F412Rx) || defined(STM32F412Vx) || defined(STM32F412Zx) || defined(STM32F413xx) || defined(STM32F423xx)
		static constexpr PIN PA1 = { GPIOA_BASE, 1, 5, SPI4_BASE };
#endif
		static constexpr PIN PE6 = { GPIOE_BASE, 6, 5, SPI4_BASE };
		static constexpr PIN PE14 = { GPIOE_BASE, 14, 5, SPI4_BASE };
	};
	struct MISO {
#if defined(STM32F411xE) || defined(STM32F412Cx) || defined(STM32F412Rx) || defined(STM32F412Vx) || defined(STM32F412Zx) || defined(STM32F413xx) || defined(STM32F423xx)
		static constexpr PIN PA11 = { GPIOA_BASE, 11, 6, SPI4_BASE };
#endif
		static constexpr PIN PE5 = { GPIOE_BASE, 5, 5, SPI4_BASE };
		static constexpr PIN PE13 = { GPIOE_BASE, 13, 5, SPI4_BASE };
	};
	struct SS {
#if defined(STM32F411xE) || defined(STM32F412Cx) || defined(STM32F412Rx) || defined(STM32F412Vx) || defined(STM32F412Zx) || defined(STM32F413xx) || defined(STM32F423xx)
		static constexpr PIN PB12 = { GPIOB_BASE, 12, 6, SPI4_BASE };
#endif
		static constexpr PIN PE4 = { GPIOE_BASE, 4, 5, SPI4_BASE };
		static constexpr PIN PE11 = { GPIOE_BASE, 11, 5, SPI4_BASE };
	};
};
#endif

#ifdef SPI5_BASE
// F410/F411/F412/F413/F423 route SPI5 through AF6; other F4/F7 use AF5.
#if defined(STM32F410Cx) || defined(STM32F410Rx) || defined(STM32F411xE) || defined(STM32F412Cx) || defined(STM32F412Rx) || defined(STM32F412Vx) || defined(STM32F412Zx) || defined(STM32F413xx) || defined(STM32F423xx)
struct _5 {
    struct DMA {
        static constexpr DMA_Sx::DMAReq RX_DMA2_Stream3 = { DMA2_Stream3_BASE, 2U, SPI5_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq RX_DMA2_Stream5 = { DMA2_Stream5_BASE, 7U, SPI5_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq TX_DMA2_Stream4 = { DMA2_Stream4_BASE, 2U, SPI5_BASE, DMA_Sx::DIR::To_Per };
        static constexpr DMA_Sx::DMAReq TX_DMA2_Stream6 = { DMA2_Stream6_BASE, 7U, SPI5_BASE, DMA_Sx::DIR::To_Per };
    };
    static constexpr Instance instance = { SPI5_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APB2ENR)), RCC_APB2ENR_SPI5EN, (RCC_BASE + offsetof(RCC_TypeDef, APB2RSTR)), RCC_APB2RSTR_SPI5RST, &System::APB2BusClock, SPI5_IRQn

 };

	struct SCK {
		static constexpr PIN PB0 = { GPIOB_BASE, 0, 6, SPI5_BASE };
#ifdef GPIOE_BASE
		static constexpr PIN PE2 = { GPIOE_BASE, 2, 6, SPI5_BASE };
		static constexpr PIN PE12 = { GPIOE_BASE, 12, 6, SPI5_BASE };
#endif
	};
	struct MOSI {
		static constexpr PIN PA10 = { GPIOA_BASE, 10, 6, SPI5_BASE };
#if !defined(STM32F410Cx) && !defined(STM32F410Rx)
		static constexpr PIN PB8 = { GPIOB_BASE, 8, 6, SPI5_BASE };
#endif
#ifdef GPIOE_BASE
		static constexpr PIN PE6 = { GPIOE_BASE, 6, 6, SPI5_BASE };
		static constexpr PIN PE14 = { GPIOE_BASE, 14, 6, SPI5_BASE };
#endif
	};
	struct MISO {
		static constexpr PIN PA12 = { GPIOA_BASE, 12, 6, SPI5_BASE };
#ifdef GPIOE_BASE
		static constexpr PIN PE5 = { GPIOE_BASE, 5, 6, SPI5_BASE };
		static constexpr PIN PE13 = { GPIOE_BASE, 13, 6, SPI5_BASE };
#endif
	};
	struct SS {
		static constexpr PIN PB1 = { GPIOB_BASE, 1, 6, SPI5_BASE };
#ifdef GPIOE_BASE
		static constexpr PIN PE4 = { GPIOE_BASE, 4, 6, SPI5_BASE };
		static constexpr PIN PE11 = { GPIOE_BASE, 11, 6, SPI5_BASE };
#endif
	};
};
#else
struct _5 {
    struct DMA {
        static constexpr DMA_Sx::DMAReq RX_DMA2_Stream3 = { DMA2_Stream3_BASE, 2U, SPI5_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq RX_DMA2_Stream5 = { DMA2_Stream5_BASE, 7U, SPI5_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq TX_DMA2_Stream4 = { DMA2_Stream4_BASE, 2U, SPI5_BASE, DMA_Sx::DIR::To_Per };
        static constexpr DMA_Sx::DMAReq TX_DMA2_Stream6 = { DMA2_Stream6_BASE, 7U, SPI5_BASE, DMA_Sx::DIR::To_Per };
    };
    static constexpr Instance instance = { SPI5_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APB2ENR)), RCC_APB2ENR_SPI5EN, (RCC_BASE + offsetof(RCC_TypeDef, APB2RSTR)), RCC_APB2RSTR_SPI5RST, &System::APB2BusClock, SPI5_IRQn

 };

	struct SCK {
		static constexpr PIN PF7 = { GPIOF_BASE, 7, 5, SPI5_BASE };
	};
	struct MOSI {
		static constexpr PIN PF9 = { GPIOF_BASE, 9, 5, SPI5_BASE };
	};
	struct MISO {
		static constexpr PIN PF8 = { GPIOF_BASE, 8, 5, SPI5_BASE };
	};
	struct SS {
		static constexpr PIN PF6 = { GPIOF_BASE, 6, 5, SPI5_BASE };
	};
};
#endif
#endif

#ifdef SPI6_BASE
struct _6 {
    struct DMA {
        static constexpr DMA_Sx::DMAReq RX_DMA2_Stream6 = { DMA2_Stream6_BASE, 1U, SPI6_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq TX_DMA2_Stream5 = { DMA2_Stream5_BASE, 1U, SPI6_BASE, DMA_Sx::DIR::To_Per };
    };
    static constexpr Instance instance = { SPI6_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APB2ENR)), RCC_APB2ENR_SPI6EN, (RCC_BASE + offsetof(RCC_TypeDef, APB2RSTR)), RCC_APB2RSTR_SPI6RST, &System::APB2BusClock, SPI6_IRQn

 };

	struct SCK {
		static constexpr PIN PG13 = { GPIOG_BASE, 13, 5, SPI6_BASE };
	};
	struct MOSI {
		static constexpr PIN PG14 = { GPIOG_BASE, 14, 5, SPI6_BASE };
	};
	struct MISO {
		static constexpr PIN PG12 = { GPIOG_BASE, 12, 5, SPI6_BASE };
	};
	struct SS {
		static constexpr PIN PG8 = { GPIOG_BASE, 8, 5, SPI6_BASE };
	};
};
#endif

#endif  // STM32 family

#endif  // Section A: defined(SPI_HPP_) && !defined(SPI_DEFS_CPP)
