// Device definitions included inside SPI. See the device datasheet for package pinout.
// CSELR selects a request on a specific channel. Routes are from the L0 reference manuals.

#if defined(STM32L010x4) || defined(STM32L011xx) || defined(STM32L021xx)
#define SPI_L0_PINS_021
#elif defined(STM32L010x6) || defined(STM32L031xx) || defined(STM32L041xx)
#define SPI_L0_PINS_031
#elif defined(STM32L010xB) || defined(STM32L071xx) || defined(STM32L072xx) \
	|| defined(STM32L073xx) || defined(STM32L081xx) || defined(STM32L082xx) || defined(STM32L083xx)
#define SPI_L0_PINS_071
#endif

#ifdef SPI1_BASE
struct _1 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX_DMA1_Channel2 = { DMA1_Channel2_BASE, 1U, SPI1_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX_DMA1_Channel3 = { DMA1_Channel3_BASE, 1U, SPI1_BASE, DMA_Sx::DIR::To_Per };
#ifdef SPI_L0_PINS_021
		static constexpr DMA_Sx::DMAReq RX_DMA1_Channel4 = { DMA1_Channel4_BASE, 1U, SPI1_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX_DMA1_Channel5 = { DMA1_Channel5_BASE, 1U, SPI1_BASE, DMA_Sx::DIR::To_Per };
#endif
	};
	static constexpr Instance instance = { SPI1_BASE, RCC_BASE + offsetof(RCC_TypeDef, APB2ENR),
		RCC_APB2ENR_SPI1EN, RCC_BASE + offsetof(RCC_TypeDef, APB2RSTR), RCC_APB2RSTR_SPI1RST,
		SPI1_IRQn };

	struct SCK {
		static constexpr PIN PA5  = { GPIOA_BASE,  5, 0, SPI1_BASE };
		static constexpr PIN PB3  = { GPIOB_BASE,  3, 0, SPI1_BASE };
#ifdef SPI_L0_PINS_021
		static constexpr PIN PA13 = { GPIOA_BASE, 13, 5, SPI1_BASE };
#endif
#ifdef SPI_L0_PINS_031
		static constexpr PIN PB13 = { GPIOB_BASE, 13, 0, SPI1_BASE };
#endif
#if defined(SPI_L0_PINS_071) && defined(GPIOE_BASE)
		static constexpr PIN PE13 = { GPIOE_BASE, 13, 2, SPI1_BASE };
#endif
	};
	struct MOSI {
		static constexpr PIN PA7  = { GPIOA_BASE,  7, 0, SPI1_BASE };
		static constexpr PIN PA12 = { GPIOA_BASE, 12, 0, SPI1_BASE };
		static constexpr PIN PB5  = { GPIOB_BASE,  5, 0, SPI1_BASE };
#if defined(SPI_L0_PINS_021) || defined(SPI_L0_PINS_031)
		static constexpr PIN PB1  = { GPIOB_BASE,  1, 1, SPI1_BASE };
#endif
#ifdef SPI_L0_PINS_031
		static constexpr PIN PB15 = { GPIOB_BASE, 15, 0, SPI1_BASE };
#endif
#if defined(SPI_L0_PINS_071) && defined(GPIOE_BASE)
		static constexpr PIN PE15 = { GPIOE_BASE, 15, 2, SPI1_BASE };
#endif
	};
	struct MISO {
		static constexpr PIN PA6  = { GPIOA_BASE,  6, 0, SPI1_BASE };
		static constexpr PIN PA11 = { GPIOA_BASE, 11, 0, SPI1_BASE };
		static constexpr PIN PB4  = { GPIOB_BASE,  4, 0, SPI1_BASE };
#ifdef SPI_L0_PINS_021
		static constexpr PIN PA14 = { GPIOA_BASE, 14, 5, SPI1_BASE };
#endif
#if defined(SPI_L0_PINS_021) || defined(SPI_L0_PINS_031)
		static constexpr PIN PB0  = { GPIOB_BASE,  0, 1, SPI1_BASE };
#endif
#ifdef SPI_L0_PINS_031
		static constexpr PIN PB14 = { GPIOB_BASE, 14, 0, SPI1_BASE };
#endif
#if defined(SPI_L0_PINS_071) && defined(GPIOE_BASE)
		static constexpr PIN PE14 = { GPIOE_BASE, 14, 2, SPI1_BASE };
#endif
	};
	struct SS {
		static constexpr PIN PA4  = { GPIOA_BASE,  4, 0, SPI1_BASE };
		static constexpr PIN PA15 = { GPIOA_BASE, 15, 0, SPI1_BASE };
#ifdef SPI_L0_PINS_021
		static constexpr PIN PB8  = { GPIOB_BASE,  8, 5, SPI1_BASE };
#endif
#ifdef SPI_L0_PINS_031
		static constexpr PIN PB12 = { GPIOB_BASE, 12, 0, SPI1_BASE };
#endif
#if defined(SPI_L0_PINS_071) && defined(GPIOE_BASE)
		static constexpr PIN PE12 = { GPIOE_BASE, 12, 2, SPI1_BASE };
#endif
	};
};
#endif

#ifdef SPI2_BASE
struct _2 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX_DMA1_Channel4 = { DMA1_Channel4_BASE, 2U, SPI2_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX_DMA1_Channel5 = { DMA1_Channel5_BASE, 2U, SPI2_BASE, DMA_Sx::DIR::To_Per };
#ifdef DMA1_Channel7_BASE
		static constexpr DMA_Sx::DMAReq RX_DMA1_Channel6 = { DMA1_Channel6_BASE, 2U, SPI2_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX_DMA1_Channel7 = { DMA1_Channel7_BASE, 2U, SPI2_BASE, DMA_Sx::DIR::To_Per };
#endif
	};
	static constexpr Instance instance = { SPI2_BASE, RCC_BASE + offsetof(RCC_TypeDef, APB1ENR),
		RCC_APB1ENR_SPI2EN, RCC_BASE + offsetof(RCC_TypeDef, APB1RSTR), RCC_APB1RSTR_SPI2RST,
		SPI2_IRQn };

	struct SCK {
		static constexpr PIN PB10 = { GPIOB_BASE, 10, 5, SPI2_BASE };
		static constexpr PIN PB13 = { GPIOB_BASE, 13, 0, SPI2_BASE };
#if defined(SPI_L0_PINS_071) && defined(GPIOD_BASE)
		static constexpr PIN PD1  = { GPIOD_BASE,  1, 1, SPI2_BASE };
#endif
	};
	struct MOSI {
		static constexpr PIN PB15 = { GPIOB_BASE, 15, 0, SPI2_BASE };
#ifdef GPIOC_BASE
		static constexpr PIN PC3  = { GPIOC_BASE,  3, 2, SPI2_BASE };
#endif
#if defined(SPI_L0_PINS_071) && defined(GPIOD_BASE)
		static constexpr PIN PD4  = { GPIOD_BASE,  4, 1, SPI2_BASE };
#endif
	};
	struct MISO {
		static constexpr PIN PB14 = { GPIOB_BASE, 14, 0, SPI2_BASE };
#ifdef GPIOC_BASE
		static constexpr PIN PC2  = { GPIOC_BASE,  2, 2, SPI2_BASE };
#endif
#if defined(SPI_L0_PINS_071) && defined(GPIOD_BASE)
		static constexpr PIN PD3  = { GPIOD_BASE,  3, 2, SPI2_BASE };
#endif
	};
	struct SS {
		static constexpr PIN PB9  = { GPIOB_BASE,  9, 5, SPI2_BASE };
		static constexpr PIN PB12 = { GPIOB_BASE, 12, 0, SPI2_BASE };
#if defined(SPI_L0_PINS_071) && defined(GPIOD_BASE)
		static constexpr PIN PD0  = { GPIOD_BASE,  0, 1, SPI2_BASE };
#endif
	};
};
#endif

#undef SPI_L0_PINS_021
#undef SPI_L0_PINS_031
#undef SPI_L0_PINS_071
