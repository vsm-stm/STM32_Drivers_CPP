// Device definitions included inside USART. See the device datasheet for package pinout.

// GPIO alternate functions differ across four L0 device groups.
#if defined(STM32L010x4) || defined(STM32L011xx) || defined(STM32L021xx)
#define UART_L0_PINS_021
#elif defined(STM32L010x6) || defined(STM32L031xx) || defined(STM32L041xx)
#define UART_L0_PINS_031
#elif defined(STM32L010x8) || defined(STM32L051xx) || defined(STM32L052xx) || defined(STM32L053xx) || defined(STM32L062xx) || defined(STM32L063xx)
#define UART_L0_PINS_051
#else
#define UART_L0_PINS_071
#endif

#ifdef USART1_BASE
struct _1 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq TX_DMA1_Channel2 = { DMA1_Channel2_BASE, 3U, USART1_BASE, DMA_Sx::DIR::To_Per };
		static constexpr DMA_Sx::DMAReq TX_DMA1_Channel4 = { DMA1_Channel4_BASE, 3U, USART1_BASE, DMA_Sx::DIR::To_Per };
		static constexpr DMA_Sx::DMAReq RX_DMA1_Channel3 = { DMA1_Channel3_BASE, 3U, USART1_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq RX_DMA1_Channel5 = { DMA1_Channel5_BASE, 3U, USART1_BASE, DMA_Sx::DIR::From_Per };
	};
	static constexpr Instance instance = { USART1_BASE, RCC_BASE + offsetof(RCC_TypeDef, APB2ENR),
		RCC_APB2ENR_USART1EN, &System::APB2BusClock, USART1_IRQn, RCC_CCIPR_USART1SEL, RCC_CCIPR_USART1SEL_Pos, false };

	struct TX {
		static constexpr PIN PA9 = { GPIOA_BASE,  9,  4, USART1_BASE };
		static constexpr PIN PB6 = { GPIOB_BASE,  6,  0, USART1_BASE };
	};
	struct RX {
		static constexpr PIN PA10 = { GPIOA_BASE, 10,  4, USART1_BASE };
		static constexpr PIN PB7 = { GPIOB_BASE,  7,  0, USART1_BASE };
	};
};
#endif

#ifdef USART2_BASE
struct _2 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq TX_DMA1_Channel4 = { DMA1_Channel4_BASE, 4U, USART2_BASE, DMA_Sx::DIR::To_Per };
		static constexpr DMA_Sx::DMAReq RX_DMA1_Channel5 = { DMA1_Channel5_BASE, 4U, USART2_BASE, DMA_Sx::DIR::From_Per };
#if defined(UART_L0_PINS_021)
		static constexpr DMA_Sx::DMAReq TX_DMA1_Channel2 = { DMA1_Channel2_BASE, 4U, USART2_BASE, DMA_Sx::DIR::To_Per };
		static constexpr DMA_Sx::DMAReq RX_DMA1_Channel3 = { DMA1_Channel3_BASE, 4U, USART2_BASE, DMA_Sx::DIR::From_Per };
#endif
#if defined(DMA1_Channel7)
		static constexpr DMA_Sx::DMAReq TX_DMA1_Channel7 = { DMA1_Channel7_BASE, 4U, USART2_BASE, DMA_Sx::DIR::To_Per };
		static constexpr DMA_Sx::DMAReq RX_DMA1_Channel6 = { DMA1_Channel6_BASE, 4U, USART2_BASE, DMA_Sx::DIR::From_Per };
#endif
	};
	static constexpr Instance instance = { USART2_BASE, RCC_BASE + offsetof(RCC_TypeDef, APB1ENR),
		RCC_APB1ENR_USART2EN, &System::APB1BusClock, USART2_IRQn, RCC_CCIPR_USART2SEL, RCC_CCIPR_USART2SEL_Pos, false };

	struct TX {
		static constexpr PIN PA2 = { GPIOA_BASE,  2,  4, USART2_BASE };
		static constexpr PIN PA14 = { GPIOA_BASE, 14,  4, USART2_BASE };
#if (defined(UART_L0_PINS_021) || defined(UART_L0_PINS_031))
		static constexpr PIN PA9 = { GPIOA_BASE,  9,  4, USART2_BASE };
		static constexpr PIN PB6 = { GPIOB_BASE,  6,  0, USART2_BASE };
#endif
#if defined(UART_L0_PINS_021)
		static constexpr PIN PB8 = { GPIOB_BASE,  8,  0, USART2_BASE };
#endif
#if defined(UART_L0_PINS_071) && defined(GPIOD_BASE)
		static constexpr PIN PD5 = { GPIOD_BASE,  5,  0, USART2_BASE };
#endif
	};
	struct RX {
		static constexpr PIN PA3 = { GPIOA_BASE,  3,  4, USART2_BASE };
		static constexpr PIN PA15 = { GPIOA_BASE, 15,  4, USART2_BASE };
#if (defined(UART_L0_PINS_021) || defined(UART_L0_PINS_031))
		static constexpr PIN PA10 = { GPIOA_BASE, 10,  4, USART2_BASE };
		static constexpr PIN PB7 = { GPIOB_BASE,  7,  0, USART2_BASE };
#endif
#if defined(UART_L0_PINS_071) && defined(GPIOD_BASE)
		static constexpr PIN PD6 = { GPIOD_BASE,  6,  0, USART2_BASE };
#endif
	};
};
#endif

#ifdef USART4_BASE
struct _4 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq TX_DMA1_Channel3 = { DMA1_Channel3_BASE, 12U, USART4_BASE, DMA_Sx::DIR::To_Per };
		static constexpr DMA_Sx::DMAReq TX_DMA1_Channel7 = { DMA1_Channel7_BASE, 12U, USART4_BASE, DMA_Sx::DIR::To_Per };
		static constexpr DMA_Sx::DMAReq RX_DMA1_Channel2 = { DMA1_Channel2_BASE, 12U, USART4_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq RX_DMA1_Channel6 = { DMA1_Channel6_BASE, 12U, USART4_BASE, DMA_Sx::DIR::From_Per };
	};
	static constexpr Instance instance = { USART4_BASE, RCC_BASE + offsetof(RCC_TypeDef, APB1ENR),
		RCC_APB1ENR_USART4EN, &System::APB1BusClock, USART4_5_IRQn, 0U, 0U, false };

	struct TX {
		static constexpr PIN PA0 = { GPIOA_BASE,  0,  6, USART4_BASE };
#if defined(GPIOC_BASE)
		static constexpr PIN PC10 = { GPIOC_BASE, 10,  6, USART4_BASE };
#endif
#if defined(GPIOE_BASE)
		static constexpr PIN PE8 = { GPIOE_BASE,  8,  6, USART4_BASE };
#endif
	};
	struct RX {
		static constexpr PIN PA1 = { GPIOA_BASE,  1,  6, USART4_BASE };
#if defined(GPIOC_BASE)
		static constexpr PIN PC11 = { GPIOC_BASE, 11,  6, USART4_BASE };
#endif
#if defined(GPIOE_BASE)
		static constexpr PIN PE9 = { GPIOE_BASE,  9,  6, USART4_BASE };
#endif
	};
};
#endif

#ifdef USART5_BASE
struct _5 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq TX_DMA1_Channel3 = { DMA1_Channel3_BASE, 13U, USART5_BASE, DMA_Sx::DIR::To_Per };
		static constexpr DMA_Sx::DMAReq TX_DMA1_Channel7 = { DMA1_Channel7_BASE, 13U, USART5_BASE, DMA_Sx::DIR::To_Per };
		static constexpr DMA_Sx::DMAReq RX_DMA1_Channel2 = { DMA1_Channel2_BASE, 13U, USART5_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq RX_DMA1_Channel6 = { DMA1_Channel6_BASE, 13U, USART5_BASE, DMA_Sx::DIR::From_Per };
	};
	static constexpr Instance instance = { USART5_BASE, RCC_BASE + offsetof(RCC_TypeDef, APB1ENR),
		RCC_APB1ENR_USART5EN, &System::APB1BusClock, USART4_5_IRQn, 0U, 0U, false };

	struct TX {
		static constexpr PIN PB3 = { GPIOB_BASE,  3,  6, USART5_BASE };
#if defined(GPIOC_BASE)
		static constexpr PIN PC12 = { GPIOC_BASE, 12,  2, USART5_BASE };
#endif
#if defined(GPIOE_BASE)
		static constexpr PIN PE10 = { GPIOE_BASE, 10,  6, USART5_BASE };
#endif
	};
	struct RX {
		static constexpr PIN PB4 = { GPIOB_BASE,  4,  6, USART5_BASE };
#if defined(GPIOD_BASE)
		static constexpr PIN PD2 = { GPIOD_BASE,  2,  6, USART5_BASE };
#endif
#if defined(GPIOE_BASE)
		static constexpr PIN PE11 = { GPIOE_BASE, 11,  6, USART5_BASE };
#endif
	};
};
#endif

#ifdef LPUART1_BASE
struct _LP1 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq TX_DMA1_Channel2 = { DMA1_Channel2_BASE, 5U, LPUART1_BASE, DMA_Sx::DIR::To_Per };
		static constexpr DMA_Sx::DMAReq RX_DMA1_Channel3 = { DMA1_Channel3_BASE, 5U, LPUART1_BASE, DMA_Sx::DIR::From_Per };
#if defined(UART_L0_PINS_021)
		static constexpr DMA_Sx::DMAReq TX_DMA1_Channel4 = { DMA1_Channel4_BASE, 5U, LPUART1_BASE, DMA_Sx::DIR::To_Per };
		static constexpr DMA_Sx::DMAReq RX_DMA1_Channel5 = { DMA1_Channel5_BASE, 5U, LPUART1_BASE, DMA_Sx::DIR::From_Per };
#endif
#if defined(DMA1_Channel7)
		static constexpr DMA_Sx::DMAReq TX_DMA1_Channel7 = { DMA1_Channel7_BASE, 5U, LPUART1_BASE, DMA_Sx::DIR::To_Per };
		static constexpr DMA_Sx::DMAReq RX_DMA1_Channel6 = { DMA1_Channel6_BASE, 5U, LPUART1_BASE, DMA_Sx::DIR::From_Per };
#endif
	};
	static constexpr Instance instance = { LPUART1_BASE, RCC_BASE + offsetof(RCC_TypeDef, APB1ENR),
		RCC_APB1ENR_LPUART1EN, &System::APB1BusClock, LPUART1_IRQn, RCC_CCIPR_LPUART1SEL, RCC_CCIPR_LPUART1SEL_Pos, true };

	struct TX {
#if defined(UART_L0_PINS_021)
		static constexpr PIN PA1 = { GPIOA_BASE,  1,  6, LPUART1_BASE };
		static constexpr PIN PA4 = { GPIOA_BASE,  4,  6, LPUART1_BASE };
		static constexpr PIN PB6 = { GPIOB_BASE,  6,  6, LPUART1_BASE };
#endif
#if !defined(UART_L0_PINS_051)
		static constexpr PIN PA2 = { GPIOA_BASE,  2,  6, LPUART1_BASE };
		static constexpr PIN PA14 = { GPIOA_BASE, 14,  6, LPUART1_BASE };
#endif
#if (defined(UART_L0_PINS_051) || defined(UART_L0_PINS_071))
		static constexpr PIN PB10 = { GPIOB_BASE, 10,  4, LPUART1_BASE };
#endif
#if defined(UART_L0_PINS_031)
		static constexpr PIN PB10 = { GPIOB_BASE, 10,  6, LPUART1_BASE };
#endif
#if defined(UART_L0_PINS_071)
		static constexpr PIN PB11 = { GPIOB_BASE, 11,  7, LPUART1_BASE };
#endif
#if defined(UART_L0_PINS_071) && defined(GPIOC_BASE)
		static constexpr PIN PC1 = { GPIOC_BASE,  1,  6, LPUART1_BASE };
#endif
#if (defined(UART_L0_PINS_051) || defined(UART_L0_PINS_071)) && defined(GPIOC_BASE)
		static constexpr PIN PC4 = { GPIOC_BASE,  4,  2, LPUART1_BASE };
		static constexpr PIN PC10 = { GPIOC_BASE, 10,  0, LPUART1_BASE };
#endif
#if defined(UART_L0_PINS_071) && defined(GPIOD_BASE)
		static constexpr PIN PD8 = { GPIOD_BASE,  8,  0, LPUART1_BASE };
#endif
	};
	struct RX {
#if !defined(UART_L0_PINS_051)
		static constexpr PIN PA3 = { GPIOA_BASE,  3,  6, LPUART1_BASE };
		static constexpr PIN PA13 = { GPIOA_BASE, 13,  6, LPUART1_BASE };
#endif
#if defined(UART_L0_PINS_021)
		static constexpr PIN PB7 = { GPIOB_BASE,  7,  6, LPUART1_BASE };
#endif
#if defined(UART_L0_PINS_071)
		static constexpr PIN PB10 = { GPIOB_BASE, 10,  7, LPUART1_BASE };
#endif
#if (defined(UART_L0_PINS_051) || defined(UART_L0_PINS_071))
		static constexpr PIN PB11 = { GPIOB_BASE, 11,  4, LPUART1_BASE };
#endif
#if defined(UART_L0_PINS_031)
		static constexpr PIN PB11 = { GPIOB_BASE, 11,  6, LPUART1_BASE };
#endif
#if (defined(UART_L0_PINS_031) || defined(UART_L0_PINS_071)) && defined(GPIOC_BASE)
		static constexpr PIN PC0 = { GPIOC_BASE,  0,  6, LPUART1_BASE };
#endif
#if (defined(UART_L0_PINS_051) || defined(UART_L0_PINS_071)) && defined(GPIOC_BASE)
		static constexpr PIN PC5 = { GPIOC_BASE,  5,  2, LPUART1_BASE };
		static constexpr PIN PC11 = { GPIOC_BASE, 11,  0, LPUART1_BASE };
#endif
#if defined(UART_L0_PINS_071) && defined(GPIOD_BASE)
		static constexpr PIN PD9 = { GPIOD_BASE,  9,  0, LPUART1_BASE };
#endif
	};
};
#endif

#undef UART_L0_PINS_021
#undef UART_L0_PINS_031
#undef UART_L0_PINS_051
#undef UART_L0_PINS_071
