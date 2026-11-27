// Device definitions included inside USART. See the device datasheet for package pinout.

#ifdef USART1_BASE
struct _1 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX = { 24U, USART1_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX = { 25U, USART1_BASE, DMA_Sx::DIR::To_Per };
	};
	static constexpr Instance instance = { USART1_BASE, RCC_BASE + offsetof(RCC_TypeDef, APB2ENR),
		RCC_APB2ENR_USART1EN, &System::APB2BusClock, USART1_IRQn, RCC_CCIPR_USART1SEL,
		DMA::TX.ch, DMA::RX.ch, RCC_CCIPR_USART1SEL_Pos, false };

	struct TX {
		static constexpr PIN PA9 = { GPIOA_BASE,  9,  7, USART1_BASE };
		static constexpr PIN PB6 = { GPIOB_BASE,  6,  7, USART1_BASE };
#if defined(GPIOC_BASE)
		static constexpr PIN PC4 = { GPIOC_BASE,  4,  7, USART1_BASE };
#endif
#if defined(GPIOE_BASE)
		static constexpr PIN PE0 = { GPIOE_BASE,  0,  7, USART1_BASE };
#endif
#if (defined(STM32G414xx) || defined(STM32G471xx) || defined(STM32G473xx) || defined(STM32G474xx) || defined(STM32G483xx) || defined(STM32G484xx)) && defined(GPIOG_BASE)
		static constexpr PIN PG9 = { GPIOG_BASE,  9,  7, USART1_BASE };
#endif
	};
	struct RX {
		static constexpr PIN PA10 = { GPIOA_BASE, 10,  7, USART1_BASE };
		static constexpr PIN PB7 = { GPIOB_BASE,  7,  7, USART1_BASE };
#if defined(GPIOC_BASE)
		static constexpr PIN PC5 = { GPIOC_BASE,  5,  7, USART1_BASE };
#endif
#if defined(GPIOE_BASE)
		static constexpr PIN PE1 = { GPIOE_BASE,  1,  7, USART1_BASE };
#endif
	};
};
#endif

#ifdef USART2_BASE
struct _2 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX = { 26U, USART2_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX = { 27U, USART2_BASE, DMA_Sx::DIR::To_Per };
	};
	static constexpr Instance instance = { USART2_BASE, RCC_BASE + offsetof(RCC_TypeDef, APB1ENR1),
		RCC_APB1ENR1_USART2EN, &System::APB1BusClock, USART2_IRQn, RCC_CCIPR_USART2SEL,
		DMA::TX.ch, DMA::RX.ch, RCC_CCIPR_USART2SEL_Pos, false };

	struct TX {
		static constexpr PIN PA2 = { GPIOA_BASE,  2,  7, USART2_BASE };
		static constexpr PIN PA14 = { GPIOA_BASE, 14,  7, USART2_BASE };
		static constexpr PIN PB3 = { GPIOB_BASE,  3,  7, USART2_BASE };
#if defined(GPIOD_BASE)
		static constexpr PIN PD5 = { GPIOD_BASE,  5,  7, USART2_BASE };
#endif
	};
	struct RX {
		static constexpr PIN PA3 = { GPIOA_BASE,  3,  7, USART2_BASE };
		static constexpr PIN PA15 = { GPIOA_BASE, 15,  7, USART2_BASE };
		static constexpr PIN PB4 = { GPIOB_BASE,  4,  7, USART2_BASE };
#if defined(GPIOD_BASE)
		static constexpr PIN PD6 = { GPIOD_BASE,  6,  7, USART2_BASE };
#endif
	};
};
#endif

#ifdef USART3_BASE
struct _3 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX = { 28U, USART3_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX = { 29U, USART3_BASE, DMA_Sx::DIR::To_Per };
	};
	static constexpr Instance instance = { USART3_BASE, RCC_BASE + offsetof(RCC_TypeDef, APB1ENR1),
		RCC_APB1ENR1_USART3EN, &System::APB1BusClock, USART3_IRQn, RCC_CCIPR_USART3SEL,
		DMA::TX.ch, DMA::RX.ch, RCC_CCIPR_USART3SEL_Pos, false };

	struct TX {
		static constexpr PIN PB9 = { GPIOB_BASE,  9,  7, USART3_BASE };
		static constexpr PIN PB10 = { GPIOB_BASE, 10,  7, USART3_BASE };
#if defined(GPIOC_BASE)
		static constexpr PIN PC10 = { GPIOC_BASE, 10,  7, USART3_BASE };
#endif
#if defined(GPIOD_BASE)
		static constexpr PIN PD8 = { GPIOD_BASE,  8,  7, USART3_BASE };
#endif
	};
	struct RX {
		static constexpr PIN PB11 = { GPIOB_BASE, 11,  7, USART3_BASE };
#if defined(GPIOC_BASE)
		static constexpr PIN PC11 = { GPIOC_BASE, 11,  7, USART3_BASE };
#endif
#if defined(GPIOD_BASE)
		static constexpr PIN PD9 = { GPIOD_BASE,  9,  7, USART3_BASE };
#endif
#if defined(GPIOE_BASE)
		static constexpr PIN PE15 = { GPIOE_BASE, 15,  7, USART3_BASE };
#endif
	};
};
#endif

#ifdef UART4_BASE
struct _4 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX = { 30U, UART4_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX = { 31U, UART4_BASE, DMA_Sx::DIR::To_Per };
	};
	static constexpr Instance instance = { UART4_BASE, RCC_BASE + offsetof(RCC_TypeDef, APB1ENR1),
		RCC_APB1ENR1_UART4EN, &System::APB1BusClock, UART4_IRQn, RCC_CCIPR_UART4SEL,
		DMA::TX.ch, DMA::RX.ch, RCC_CCIPR_UART4SEL_Pos, false };

	struct TX {
#if defined(GPIOC_BASE)
		static constexpr PIN PC10 = { GPIOC_BASE, 10,  5, UART4_BASE };
#endif
	};
	struct RX {
#if defined(GPIOC_BASE)
		static constexpr PIN PC11 = { GPIOC_BASE, 11,  5, UART4_BASE };
#endif
	};
};
#endif

#ifdef UART5_BASE
struct _5 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX = { 32U, UART5_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX = { 33U, UART5_BASE, DMA_Sx::DIR::To_Per };
	};
	static constexpr Instance instance = { UART5_BASE, RCC_BASE + offsetof(RCC_TypeDef, APB1ENR1),
		RCC_APB1ENR1_UART5EN, &System::APB1BusClock, UART5_IRQn, RCC_CCIPR_UART5SEL,
		DMA::TX.ch, DMA::RX.ch, RCC_CCIPR_UART5SEL_Pos, false };

	struct TX {
#if defined(GPIOC_BASE)
		static constexpr PIN PC12 = { GPIOC_BASE, 12,  5, UART5_BASE };
#endif
	};
	struct RX {
#if defined(GPIOD_BASE)
		static constexpr PIN PD2 = { GPIOD_BASE,  2,  5, UART5_BASE };
#endif
	};
};
#endif

#ifdef LPUART1_BASE
struct _LP1 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX = { 34U, LPUART1_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX = { 35U, LPUART1_BASE, DMA_Sx::DIR::To_Per };
	};
	static constexpr Instance instance = { LPUART1_BASE, RCC_BASE + offsetof(RCC_TypeDef, APB1ENR2),
		RCC_APB1ENR2_LPUART1EN, &System::APB1BusClock, LPUART1_IRQn, RCC_CCIPR_LPUART1SEL,
		DMA::TX.ch, DMA::RX.ch, RCC_CCIPR_LPUART1SEL_Pos, true };

	struct TX {
		static constexpr PIN PA2 = { GPIOA_BASE,  2, 12, LPUART1_BASE };
		static constexpr PIN PB11 = { GPIOB_BASE, 11,  8, LPUART1_BASE };
#if defined(GPIOC_BASE)
		static constexpr PIN PC1 = { GPIOC_BASE,  1,  8, LPUART1_BASE };
#endif
#if (defined(STM32G414xx) || defined(STM32G471xx) || defined(STM32G473xx) || defined(STM32G474xx) || defined(STM32G483xx) || defined(STM32G484xx)) && defined(GPIOG_BASE)
		static constexpr PIN PG7 = { GPIOG_BASE,  7,  8, LPUART1_BASE };
#endif
	};
	struct RX {
		static constexpr PIN PA3 = { GPIOA_BASE,  3, 12, LPUART1_BASE };
		static constexpr PIN PB10 = { GPIOB_BASE, 10,  8, LPUART1_BASE };
#if defined(GPIOC_BASE)
		static constexpr PIN PC0 = { GPIOC_BASE,  0,  8, LPUART1_BASE };
#endif
#if (defined(STM32G414xx) || defined(STM32G471xx) || defined(STM32G473xx) || defined(STM32G474xx) || defined(STM32G483xx) || defined(STM32G484xx)) && defined(GPIOG_BASE)
		static constexpr PIN PG8 = { GPIOG_BASE,  8,  8, LPUART1_BASE };
#endif
	};
};
#endif
