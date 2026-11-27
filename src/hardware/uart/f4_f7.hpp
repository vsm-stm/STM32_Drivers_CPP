// Device definitions included inside USART. See the device datasheet for package pinout.

#if defined(STM32F7)
#define UART_INFO(dev, bus, vector, sel) { dev##_BASE, (RCC_BASE + offsetof(RCC_TypeDef, bus##ENR)), RCC_##bus##ENR_##dev##EN, &System::bus##BusClock, vector, sel, sel##_Pos }
#elif defined(STM32F4)
#define UART_INFO(dev, bus, vector, sel) { dev##_BASE, (RCC_BASE + offsetof(RCC_TypeDef, bus##ENR)), RCC_##bus##ENR_##dev##EN, &System::bus##BusClock, vector }
#endif

struct _1 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX_DMA2_Stream2 = { DMA2_Stream2_BASE, 4U, USART1_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX_DMA2_Stream7 = { DMA2_Stream7_BASE, 4U, USART1_BASE, DMA_Sx::DIR::To_Per };
		static constexpr DMA_Sx::DMAReq RX_DMA2_Stream5 = { DMA2_Stream5_BASE, 4U, USART1_BASE, DMA_Sx::DIR::From_Per };  // USART1_RX, not TX.

	};
	static constexpr Instance instance = UART_INFO(USART1, APB2, USART1_IRQn, RCC_DCKCFGR2_USART1SEL);

	struct TX {
		static constexpr PIN PA9  = { GPIOA_BASE,  9, 7, USART1_BASE };
		static constexpr PIN PB6  = { GPIOB_BASE,  6, 7, USART1_BASE };
	};
	struct RX {
		static constexpr PIN PA10 = { GPIOA_BASE, 10, 7, USART1_BASE };
		static constexpr PIN PB7  = { GPIOB_BASE,  7, 7, USART1_BASE };
	};
};

struct _2 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX_DMA1_Stream5 = { DMA1_Stream5_BASE, 4U, USART2_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX_DMA1_Stream6 = { DMA1_Stream6_BASE, 4U, USART2_BASE, DMA_Sx::DIR::To_Per };

	};
	static constexpr Instance instance = UART_INFO(USART2, APB1, USART2_IRQn, RCC_DCKCFGR2_USART2SEL);

	struct TX {
		static constexpr PIN PA2  = { GPIOA_BASE,  2, 7, USART2_BASE };
#ifdef GPIOD_BASE
		static constexpr PIN PD5  = { GPIOD_BASE,  5, 7, USART2_BASE };
#endif
	};
	struct RX {
		static constexpr PIN PA3  = { GPIOA_BASE,  3, 7, USART2_BASE };
#ifdef GPIOD_BASE
		static constexpr PIN PD6  = { GPIOD_BASE,  6, 7, USART2_BASE };
#endif
	};
};

#ifdef USART3_BASE
struct _3 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX_DMA1_Stream1 = { DMA1_Stream1_BASE, 4U, USART3_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX_DMA1_Stream3 = { DMA1_Stream3_BASE, 4U, USART3_BASE, DMA_Sx::DIR::To_Per };
		static constexpr DMA_Sx::DMAReq TX_DMA1_Stream4 = { DMA1_Stream4_BASE, 7U, USART3_BASE, DMA_Sx::DIR::To_Per };  // Stream4 Ch7

	};
	static constexpr Instance instance = UART_INFO(USART3, APB1, USART3_IRQn, RCC_DCKCFGR2_USART3SEL);

	struct TX {
		static constexpr PIN PB10 = { GPIOB_BASE, 10, 7, USART3_BASE };
		static constexpr PIN PC10 = { GPIOC_BASE, 10, 7, USART3_BASE };
#ifdef GPIOD_BASE
		static constexpr PIN PD8  = { GPIOD_BASE,  8, 7, USART3_BASE };
#endif
	};
	struct RX {
		static constexpr PIN PB11 = { GPIOB_BASE, 11, 7, USART3_BASE };
		static constexpr PIN PC11 = { GPIOC_BASE, 11, 7, USART3_BASE };
#ifdef GPIOD_BASE
		static constexpr PIN PD9  = { GPIOD_BASE,  9, 7, USART3_BASE };
#endif
	};
};
#endif

#ifdef UART4_BASE
struct _4 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX_DMA1_Stream2 = { DMA1_Stream2_BASE, 4U, UART4_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX_DMA1_Stream4 = { DMA1_Stream4_BASE, 4U, UART4_BASE, DMA_Sx::DIR::To_Per };

	};
	static constexpr Instance instance = UART_INFO(UART4, APB1, UART4_IRQn, RCC_DCKCFGR2_UART4SEL);

	struct TX {
		static constexpr PIN PA0  = { GPIOA_BASE,  0, 8, UART4_BASE };
		static constexpr PIN PC10 = { GPIOC_BASE, 10, 8, UART4_BASE };
	};
	struct RX {
		static constexpr PIN PA1  = { GPIOA_BASE,  1, 8, UART4_BASE };
		static constexpr PIN PC11 = { GPIOC_BASE, 11, 8, UART4_BASE };
	};
};
#endif

#ifdef UART5_BASE
struct _5 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX_DMA1_Stream0 = { DMA1_Stream0_BASE, 4U, UART5_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX_DMA1_Stream7 = { DMA1_Stream7_BASE, 4U, UART5_BASE, DMA_Sx::DIR::To_Per };

	};
	static constexpr Instance instance = UART_INFO(UART5, APB1, UART5_IRQn, RCC_DCKCFGR2_UART5SEL);

	struct TX {
		static constexpr PIN PC12 = { GPIOC_BASE, 12, 8, UART5_BASE };
	};
	struct RX {
#ifdef GPIOD_BASE
		static constexpr PIN PD2  = { GPIOD_BASE,  2, 8, UART5_BASE };
#endif
	};
};
#endif

#ifdef USART6_BASE
struct _6 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX_DMA2_Stream1 = { DMA2_Stream1_BASE, 5U, USART6_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX_DMA2_Stream6 = { DMA2_Stream6_BASE, 5U, USART6_BASE, DMA_Sx::DIR::To_Per };
		static constexpr DMA_Sx::DMAReq RX_DMA2_Stream2 = { DMA2_Stream2_BASE, 5U, USART6_BASE, DMA_Sx::DIR::From_Per };  // Stream2 Ch5
		static constexpr DMA_Sx::DMAReq TX_DMA2_Stream7 = { DMA2_Stream7_BASE, 5U, USART6_BASE, DMA_Sx::DIR::To_Per };  // Stream7 Ch5

	};
	static constexpr Instance instance = UART_INFO(USART6, APB2, USART6_IRQn, RCC_DCKCFGR2_USART6SEL);

	struct TX {
		static constexpr PIN PC6  = { GPIOC_BASE,  6, 8, USART6_BASE };
#ifdef GPIOG_BASE
		static constexpr PIN PG14 = { GPIOG_BASE, 14, 8, USART6_BASE };
#endif
	};
	struct RX {
		static constexpr PIN PC7  = { GPIOC_BASE,  7, 8, USART6_BASE };
#ifdef GPIOG_BASE
		static constexpr PIN PG9  = { GPIOG_BASE,  9, 8, USART6_BASE };
#endif
	};
};
#endif

#ifdef UART7_BASE
struct _7 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq TX_DMA1_Stream1 = { DMA1_Stream1_BASE, 5U, UART7_BASE, DMA_Sx::DIR::To_Per };
		static constexpr DMA_Sx::DMAReq RX_DMA1_Stream3 = { DMA1_Stream3_BASE, 5U, UART7_BASE, DMA_Sx::DIR::From_Per };

	};
	static constexpr Instance instance = UART_INFO(UART7, APB1, UART7_IRQn, RCC_DCKCFGR2_UART7SEL);

	struct TX {
#ifdef GPIOE_BASE
		static constexpr PIN PE8  = { GPIOE_BASE,  8, 8, UART7_BASE };
#endif
#ifdef GPIOF_BASE
		static constexpr PIN PF7  = { GPIOF_BASE,  7, 8, UART7_BASE };
#endif
	};
	struct RX {
#ifdef GPIOE_BASE
		static constexpr PIN PE7  = { GPIOE_BASE,  7, 8, UART7_BASE };
#endif
#ifdef GPIOF_BASE
		static constexpr PIN PF6  = { GPIOF_BASE,  6, 8, UART7_BASE };
#endif
	};
};
#endif

#ifdef UART8_BASE
struct _8 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq TX_DMA1_Stream0 = { DMA1_Stream0_BASE, 5U, UART8_BASE, DMA_Sx::DIR::To_Per };
		static constexpr DMA_Sx::DMAReq RX_DMA1_Stream6 = { DMA1_Stream6_BASE, 5U, UART8_BASE, DMA_Sx::DIR::From_Per };

	};
	static constexpr Instance instance = UART_INFO(UART8, APB1, UART8_IRQn, RCC_DCKCFGR2_UART8SEL);

	struct TX {
#ifdef GPIOE_BASE
		static constexpr PIN PE1  = { GPIOE_BASE,  1, 8, UART8_BASE };
#endif
	};
	struct RX {
#ifdef GPIOE_BASE
		static constexpr PIN PE0  = { GPIOE_BASE,  0, 8, UART8_BASE };
#endif
	};
};
#endif

#undef UART_INFO
