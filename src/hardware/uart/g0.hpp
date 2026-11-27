// Device definitions included inside USART. See the device datasheet for package pinout.

// Some G0 devices combine USART2/LPUART2 and USART3..6/LPUART1.
#if defined(LPUART2_BASE)
static constexpr IRQn_Type uart2_irq = USART2_LPUART2_IRQn;
static constexpr IRQn_Type uart_shared_irq = USART3_4_5_6_LPUART1_IRQn;
#elif defined(USART5_BASE)
static constexpr IRQn_Type uart2_irq = USART2_IRQn;
static constexpr IRQn_Type uart_shared_irq = USART3_4_5_6_IRQn;
#elif defined(USART3_BASE)
static constexpr IRQn_Type uart2_irq = USART2_IRQn;
#if defined(LPUART1_BASE)
static constexpr IRQn_Type uart_shared_irq = USART3_4_LPUART1_IRQn;
#else
static constexpr IRQn_Type uart_shared_irq = USART3_4_IRQn;
#endif
#else
static constexpr IRQn_Type uart2_irq = USART2_IRQn;
#endif

struct _1 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX = { 50U, USART1_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX = { 51U, USART1_BASE, DMA_Sx::DIR::To_Per };

	};
	static constexpr Instance instance = { USART1_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APBENR2)), RCC_APBENR2_USART1EN, &System::APB1BusClock, USART1_IRQn, RCC_CCIPR_USART1SEL,
		DMA::TX.ch, DMA::RX.ch, RCC_CCIPR_USART1SEL_Pos, false
	};

	struct TX {
		static constexpr PIN PA9  = { GPIOA_BASE,  9, 1, USART1_BASE };
		static constexpr PIN PB6  = { GPIOB_BASE,  6, 0, USART1_BASE };
	};
	struct RX {
		static constexpr PIN PA10 = { GPIOA_BASE, 10, 1, USART1_BASE };
		static constexpr PIN PB7  = { GPIOB_BASE,  7, 0, USART1_BASE };
	};
};

struct _2 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX = { 52U, USART2_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX = { 53U, USART2_BASE, DMA_Sx::DIR::To_Per };

	};
#ifdef RCC_CCIPR_USART2SEL
	static constexpr Instance instance = { USART2_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APBENR1)), RCC_APBENR1_USART2EN, &System::APB1BusClock, uart2_irq, RCC_CCIPR_USART2SEL,
		DMA::TX.ch, DMA::RX.ch, RCC_CCIPR_USART2SEL_Pos, false
	};
#else
	static constexpr Instance instance = { USART2_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APBENR1)), RCC_APBENR1_USART2EN, &System::APB1BusClock, uart2_irq, 0,
		DMA::TX.ch, DMA::RX.ch, 0U, false
	};
#endif

	struct TX {
		static constexpr PIN PA2  = { GPIOA_BASE,  2, 1, USART2_BASE };
		static constexpr PIN PA14 = { GPIOA_BASE, 14, 1, USART2_BASE };
	};
	struct RX {
		static constexpr PIN PA3  = { GPIOA_BASE,  3, 1, USART2_BASE };
		static constexpr PIN PA15 = { GPIOA_BASE, 15, 1, USART2_BASE };
	};
};

#ifdef USART3_BASE
struct _3 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX = { 54U, USART3_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX = { 55U, USART3_BASE, DMA_Sx::DIR::To_Per };

	};
#ifdef RCC_CCIPR_USART3SEL
	static constexpr Instance instance = { USART3_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APBENR1)), RCC_APBENR1_USART3EN, &System::APB1BusClock, uart_shared_irq, RCC_CCIPR_USART3SEL,
		DMA::TX.ch, DMA::RX.ch, RCC_CCIPR_USART3SEL_Pos, false
	};
#else
	static constexpr Instance instance = { USART3_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APBENR1)), RCC_APBENR1_USART3EN, &System::APB1BusClock, uart_shared_irq, 0,
		DMA::TX.ch, DMA::RX.ch, 0U, false
	};
#endif

	struct TX {
		static constexpr PIN PB8  = { GPIOB_BASE,  8, 4, USART3_BASE };
		static constexpr PIN PC4  = { GPIOC_BASE,  4, 0, USART3_BASE };
		static constexpr PIN PC10 = { GPIOC_BASE, 10, 0, USART3_BASE };
	};
	struct RX {
		static constexpr PIN PB9  = { GPIOB_BASE,  9, 4, USART3_BASE };
		static constexpr PIN PC5  = { GPIOC_BASE,  5, 0, USART3_BASE };
		static constexpr PIN PC11 = { GPIOC_BASE, 11, 0, USART3_BASE };
	};
};
#endif

#ifdef USART4_BASE
struct _4 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX = { 56U, USART4_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX = { 57U, USART4_BASE, DMA_Sx::DIR::To_Per };

	};
	static constexpr Instance instance = { USART4_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APBENR1)), RCC_APBENR1_USART4EN, &System::APB1BusClock, uart_shared_irq, 0,
		DMA::TX.ch, DMA::RX.ch, 0U, false
	};

	struct TX {
		static constexpr PIN PA0  = { GPIOA_BASE,  0, 4, USART4_BASE };
		static constexpr PIN PC10 = { GPIOC_BASE, 10, 1, USART4_BASE };
	};
	struct RX {
		static constexpr PIN PA1  = { GPIOA_BASE,  1, 4, USART4_BASE };
		static constexpr PIN PC11 = { GPIOC_BASE, 11, 1, USART4_BASE };
	};
};
#endif

#ifdef USART5_BASE
struct _5 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX = { 74U, USART5_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX = { 75U, USART5_BASE, DMA_Sx::DIR::To_Per };

	};
	static constexpr Instance instance = { USART5_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APBENR1)), RCC_APBENR1_USART5EN, &System::APB1BusClock, uart_shared_irq, 0,
		DMA::TX.ch, DMA::RX.ch, 0U, false
	};

	struct TX { static constexpr PIN PC12 = { GPIOC_BASE, 12, 3, USART5_BASE }; };
	struct RX {
#ifdef GPIOD_BASE
		static constexpr PIN PD2 = { GPIOD_BASE, 2, 3, USART5_BASE };
#endif
	};
};
#endif
#ifdef USART6_BASE
struct _6 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX = { 76U, USART6_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX = { 77U, USART6_BASE, DMA_Sx::DIR::To_Per };

	};
	static constexpr Instance instance = { USART6_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APBENR1)), RCC_APBENR1_USART6EN, &System::APB1BusClock, uart_shared_irq, 0,
		DMA::TX.ch, DMA::RX.ch, 0U, false
	};

	struct TX { static constexpr PIN PC0 = { GPIOC_BASE, 0, 4, USART6_BASE }; };
	struct RX { static constexpr PIN PC1 = { GPIOC_BASE, 1, 4, USART6_BASE }; };
};
#endif

#ifdef LPUART1_BASE
#if defined(USART3_BASE)
static constexpr IRQn_Type lpuart1_irq = uart_shared_irq;
#else
static constexpr IRQn_Type lpuart1_irq = LPUART1_IRQn;
#endif

struct _LP1 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX = { 14U, LPUART1_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX = { 15U, LPUART1_BASE, DMA_Sx::DIR::To_Per };
	};
	static constexpr Instance instance = { LPUART1_BASE, RCC_BASE + offsetof(RCC_TypeDef, APBENR1),
		RCC_APBENR1_LPUART1EN, &System::APB1BusClock, lpuart1_irq, RCC_CCIPR_LPUART1SEL,
		DMA::TX.ch, DMA::RX.ch, RCC_CCIPR_LPUART1SEL_Pos, true };

	struct TX {
		static constexpr PIN PA2 = { GPIOA_BASE,  2,  6, LPUART1_BASE };
		static constexpr PIN PB11 = { GPIOB_BASE, 11,  1, LPUART1_BASE };
#if defined(USART3_BASE) && defined(GPIOC_BASE)
		static constexpr PIN PC1 = { GPIOC_BASE,  1,  1, LPUART1_BASE };
#endif
#if defined(LPUART2_BASE) && defined(GPIOF_BASE)
		static constexpr PIN PF4 = { GPIOF_BASE,  4,  1, LPUART1_BASE };
#endif
	};
	struct RX {
		static constexpr PIN PA3 = { GPIOA_BASE,  3,  6, LPUART1_BASE };
		static constexpr PIN PB10 = { GPIOB_BASE, 10,  1, LPUART1_BASE };
#if defined(USART3_BASE) && defined(GPIOC_BASE)
		static constexpr PIN PC0 = { GPIOC_BASE,  0,  1, LPUART1_BASE };
#endif
#if defined(LPUART2_BASE) && defined(GPIOF_BASE)
		static constexpr PIN PF5 = { GPIOF_BASE,  5,  1, LPUART1_BASE };
#endif
	};
};
#endif

#ifdef LPUART2_BASE
struct _LP2 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX = { 64U, LPUART2_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX = { 65U, LPUART2_BASE, DMA_Sx::DIR::To_Per };
	};
	static constexpr Instance instance = { LPUART2_BASE, RCC_BASE + offsetof(RCC_TypeDef, APBENR1),
		RCC_APBENR1_LPUART2EN, &System::APB1BusClock, USART2_LPUART2_IRQn, RCC_CCIPR_LPUART2SEL,
		DMA::TX.ch, DMA::RX.ch, RCC_CCIPR_LPUART2SEL_Pos, true };

	struct TX {
		static constexpr PIN PB6 = { GPIOB_BASE,  6, 10, LPUART2_BASE };
#if defined(GPIOC_BASE)
		static constexpr PIN PC0 = { GPIOC_BASE,  0,  3, LPUART2_BASE };
		static constexpr PIN PC6 = { GPIOC_BASE,  6,  3, LPUART2_BASE };
#endif
	};
	struct RX {
		static constexpr PIN PA13 = { GPIOA_BASE, 13, 10, LPUART2_BASE };
		static constexpr PIN PB7 = { GPIOB_BASE,  7, 10, LPUART2_BASE };
#if defined(GPIOC_BASE)
		static constexpr PIN PC1 = { GPIOC_BASE,  1,  3, LPUART2_BASE };
		static constexpr PIN PC7 = { GPIOC_BASE,  7,  3, LPUART2_BASE };
#endif
#if defined(GPIOF_BASE)
		static constexpr PIN PF3 = { GPIOF_BASE,  3,  1, LPUART2_BASE };
#endif
	};
};
#endif
