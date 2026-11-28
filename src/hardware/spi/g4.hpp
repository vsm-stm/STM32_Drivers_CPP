// Device definitions included inside SPI. See the device datasheet for package pinout.
// DMAMUX requests are from RM0440. The constructor selects the physical channels.

#if defined(GPIOG_BASE) && (defined(STM32G414xx) || defined(STM32G471xx) || defined(STM32G473xx) \
	|| defined(STM32G474xx) || defined(STM32G483xx) || defined(STM32G484xx))
#define SPI_G4_PORT_G_PINS
#endif

#ifdef SPI1_BASE
struct _1 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX = { 10U, SPI1_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX = { 11U, SPI1_BASE, DMA_Sx::DIR::To_Per };
	};
	static constexpr Instance instance = { SPI1_BASE, RCC_BASE + offsetof(RCC_TypeDef, APB2ENR),
		RCC_APB2ENR_SPI1EN, RCC_BASE + offsetof(RCC_TypeDef, APB2RSTR), RCC_APB2RSTR_SPI1RST,
		SPI1_IRQn, DMA::TX.ch, DMA::RX.ch };

	struct SCK {
		static constexpr PIN PA5 = { GPIOA_BASE, 5, 5, SPI1_BASE };
		static constexpr PIN PB3 = { GPIOB_BASE, 3, 5, SPI1_BASE };
#ifdef SPI_G4_PORT_G_PINS
		static constexpr PIN PG2 = { GPIOG_BASE, 2, 5, SPI1_BASE };
#endif
	};
	struct MOSI {
		static constexpr PIN PA7 = { GPIOA_BASE, 7, 5, SPI1_BASE };
		static constexpr PIN PB5 = { GPIOB_BASE, 5, 5, SPI1_BASE };
#ifdef SPI_G4_PORT_G_PINS
		static constexpr PIN PG4 = { GPIOG_BASE, 4, 5, SPI1_BASE };
#endif
	};
	struct MISO {
		static constexpr PIN PA6 = { GPIOA_BASE, 6, 5, SPI1_BASE };
		static constexpr PIN PB4 = { GPIOB_BASE, 4, 5, SPI1_BASE };
#ifdef SPI_G4_PORT_G_PINS
		static constexpr PIN PG3 = { GPIOG_BASE, 3, 5, SPI1_BASE };
#endif
	};
	struct SS {
		static constexpr PIN PA4  = { GPIOA_BASE,  4, 5, SPI1_BASE };
		static constexpr PIN PA15 = { GPIOA_BASE, 15, 5, SPI1_BASE };
#ifdef SPI_G4_PORT_G_PINS
		static constexpr PIN PG5  = { GPIOG_BASE,  5, 5, SPI1_BASE };
#endif
	};
};
#endif

#ifdef SPI2_BASE
struct _2 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX = { 12U, SPI2_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX = { 13U, SPI2_BASE, DMA_Sx::DIR::To_Per };
	};
	static constexpr Instance instance = { SPI2_BASE, RCC_BASE + offsetof(RCC_TypeDef, APB1ENR1),
		RCC_APB1ENR1_SPI2EN, RCC_BASE + offsetof(RCC_TypeDef, APB1RSTR1), RCC_APB1RSTR1_SPI2RST,
		SPI2_IRQn, DMA::TX.ch, DMA::RX.ch };

	struct SCK {
		static constexpr PIN PB13 = { GPIOB_BASE, 13, 5, SPI2_BASE };
#ifdef GPIOF_BASE
		static constexpr PIN PF1  = { GPIOF_BASE,  1, 5, SPI2_BASE };
		static constexpr PIN PF9  = { GPIOF_BASE,  9, 5, SPI2_BASE };
		static constexpr PIN PF10 = { GPIOF_BASE, 10, 5, SPI2_BASE };
#endif
	};
	struct MOSI {
		static constexpr PIN PA11 = { GPIOA_BASE, 11, 5, SPI2_BASE };
		static constexpr PIN PB15 = { GPIOB_BASE, 15, 5, SPI2_BASE };
	};
	struct MISO {
		static constexpr PIN PA10 = { GPIOA_BASE, 10, 5, SPI2_BASE };
		static constexpr PIN PB14 = { GPIOB_BASE, 14, 5, SPI2_BASE };
	};
	struct SS {
		static constexpr PIN PB12 = { GPIOB_BASE, 12, 5, SPI2_BASE };
#ifdef GPIOD_BASE
		static constexpr PIN PD15 = { GPIOD_BASE, 15, 6, SPI2_BASE };
#endif
#ifdef GPIOF_BASE
		static constexpr PIN PF0  = { GPIOF_BASE,  0, 5, SPI2_BASE };
#endif
	};
};
#endif

#ifdef SPI3_BASE
struct _3 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX = { 14U, SPI3_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX = { 15U, SPI3_BASE, DMA_Sx::DIR::To_Per };
	};
	static constexpr Instance instance = { SPI3_BASE, RCC_BASE + offsetof(RCC_TypeDef, APB1ENR1),
		RCC_APB1ENR1_SPI3EN, RCC_BASE + offsetof(RCC_TypeDef, APB1RSTR1), RCC_APB1RSTR1_SPI3RST,
		SPI3_IRQn, DMA::TX.ch, DMA::RX.ch };

	struct SCK {
		static constexpr PIN PB3  = { GPIOB_BASE,  3, 6, SPI3_BASE };
#ifdef GPIOC_BASE
		static constexpr PIN PC10 = { GPIOC_BASE, 10, 6, SPI3_BASE };
#endif
#ifdef SPI_G4_PORT_G_PINS
		static constexpr PIN PG9  = { GPIOG_BASE,  9, 6, SPI3_BASE };
#endif
	};
	struct MOSI {
		static constexpr PIN PB5  = { GPIOB_BASE,  5, 6, SPI3_BASE };
#ifdef GPIOC_BASE
		static constexpr PIN PC12 = { GPIOC_BASE, 12, 6, SPI3_BASE };
#endif
	};
	struct MISO {
		static constexpr PIN PB4  = { GPIOB_BASE,  4, 6, SPI3_BASE };
#ifdef GPIOC_BASE
		static constexpr PIN PC11 = { GPIOC_BASE, 11, 6, SPI3_BASE };
#endif
	};
	struct SS {
		static constexpr PIN PA4  = { GPIOA_BASE,  4, 6, SPI3_BASE };
		static constexpr PIN PA15 = { GPIOA_BASE, 15, 6, SPI3_BASE };
	};
};
#endif

#ifdef SPI4_BASE
struct _4 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX = { 106U, SPI4_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX = { 107U, SPI4_BASE, DMA_Sx::DIR::To_Per };
	};
	static constexpr Instance instance = { SPI4_BASE, RCC_BASE + offsetof(RCC_TypeDef, APB2ENR),
		RCC_APB2ENR_SPI4EN, RCC_BASE + offsetof(RCC_TypeDef, APB2RSTR), RCC_APB2RSTR_SPI4RST,
		SPI4_IRQn, DMA::TX.ch, DMA::RX.ch };

	struct SCK {
		static constexpr PIN PE2  = { GPIOE_BASE,  2, 5, SPI4_BASE };
		static constexpr PIN PE12 = { GPIOE_BASE, 12, 5, SPI4_BASE };
	};
	struct MOSI {
		static constexpr PIN PE6  = { GPIOE_BASE,  6, 5, SPI4_BASE };
		static constexpr PIN PE14 = { GPIOE_BASE, 14, 5, SPI4_BASE };
	};
	struct MISO {
		static constexpr PIN PE5  = { GPIOE_BASE,  5, 5, SPI4_BASE };
		static constexpr PIN PE13 = { GPIOE_BASE, 13, 5, SPI4_BASE };
	};
	struct SS {
		static constexpr PIN PE3  = { GPIOE_BASE,  3, 5, SPI4_BASE };
		static constexpr PIN PE4  = { GPIOE_BASE,  4, 5, SPI4_BASE };
		static constexpr PIN PE11 = { GPIOE_BASE, 11, 5, SPI4_BASE };
	};
};
#endif

#undef SPI_G4_PORT_G_PINS
