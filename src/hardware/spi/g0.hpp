// Device definitions included inside SPI. See the device datasheet for package pinout.

#ifdef SPI1_BASE
struct _1 {
	struct DMA {
		static constexpr DMA_Sx::DMAReq RX = { 16U, SPI1_BASE, DMA_Sx::DIR::From_Per };
		static constexpr DMA_Sx::DMAReq TX = { 17U, SPI1_BASE, DMA_Sx::DIR::To_Per };
	};
	static constexpr Instance instance = { SPI1_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APBENR2)), RCC_APBENR2_SPI1EN, (RCC_BASE + offsetof(RCC_TypeDef, APBRSTR2)), RCC_APBRSTR2_SPI1RST, SPI1_IRQn,
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
	static constexpr Instance instance = { SPI2_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APBENR1)), RCC_APBENR1_SPI2EN, (RCC_BASE + offsetof(RCC_TypeDef, APBRSTR1)), RCC_APBRSTR1_SPI2RST,
#ifdef SPI3_BASE
	  SPI2_3_IRQn
#else
	  SPI2_IRQn
#endif
		, DMA::TX.ch, DMA::RX.ch
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
	static constexpr Instance instance = { SPI3_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APBENR1)), RCC_APBENR1_SPI3EN, (RCC_BASE + offsetof(RCC_TypeDef, APBRSTR1)), RCC_APBRSTR1_SPI3RST, SPI2_3_IRQn,
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
