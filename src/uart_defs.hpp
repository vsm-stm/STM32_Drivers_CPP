/**
 * @file uart_defs.hpp
 * @brief Selected-device instance, pin and DMA definitions for USART.
 *
 * The public driver header includes the declaration section inside its class.
 * Each _N owns an immutable constexpr instance, named pins and DMA routes;
 * unavailable instances are omitted by the selected CMSIS device guards.
 * Numeric peripheral/RCC addresses support constant initialization without MMIO.
 * Bus-clock pointers refer to ordinary System variables, not hardware registers.
 *
 * Application code includes uart.hpp and can shorten names with:
 * @code
 *   using Hardware = USART::_3;
 *   // Hardware::instance is the first argument of the constexpr constructor.
 *   // Hardware::DMA contains that peripheral's existing request definitions.
 *   // Declare the static/global driver object with constinit.
 * @endcode
 * Fixed bindings are checked while evaluating the constructor. Device/package
 * bonding remains an application responsibility. There is no runtime table
 * mapping a peripheral address to its instance description.
 * The register-accessor section temporarily switches to private access and
 * restores public access before returning to uart.hpp.
 * Only the deprecated CMSIS-pointer constructor performs runtime instance lookup.
 */
// ===========================================================================
// Section A: Pin tables + FIFO enums — USART class body
// ===========================================================================
#if defined(UART_H_) && !defined(UART_DEFS_CPP)

// Constant metadata; no address-to-instance table.

// Peripheral availability comes from the selected CMSIS device header.
#if defined(STM32F7)
#define UART_INFO(dev, bus, vector, sel) { dev##_BASE, (RCC_BASE + offsetof(RCC_TypeDef, bus##ENR)), RCC_##bus##ENR_##dev##EN, &System::bus##BusClock, vector, sel }
#elif defined(STM32F4)
#define UART_INFO(dev, bus, vector, sel) { dev##_BASE, (RCC_BASE + offsetof(RCC_TypeDef, bus##ENR)), RCC_##bus##ENR_##dev##EN, &System::bus##BusClock, vector }
#endif

#if defined(STM32G0)
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

#endif

// Request ownership and direction are defined once in the nested DMA descriptions.

// ---------------------------------------------------------------------------
// Each constexpr PIN packs: GPIO port base, pin number, alternate-function
// index, and the owning USART base address.  Zero RAM footprint.
// ---------------------------------------------------------------------------

#if defined(STM32G0)

struct _1 {
    struct DMA {
        static constexpr DMA_Sx::DMAReq RX = { 50U, USART1_BASE, DMA_Sx::DIR::From_Per };
        static constexpr DMA_Sx::DMAReq TX = { 51U, USART1_BASE, DMA_Sx::DIR::To_Per };

    };
    static constexpr Instance instance = { USART1_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APBENR2)), RCC_APBENR2_USART1EN, &System::APB1BusClock, USART1_IRQn, RCC_CCIPR_USART1SEL,
        DMA::TX.ch, DMA::RX.ch
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
        DMA::TX.ch, DMA::RX.ch
    };
#else
    static constexpr Instance instance = { USART2_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APBENR1)), RCC_APBENR1_USART2EN, &System::APB1BusClock, uart2_irq, 0,
        DMA::TX.ch, DMA::RX.ch
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
        DMA::TX.ch, DMA::RX.ch
    };
#else
    static constexpr Instance instance = { USART3_BASE, (RCC_BASE + offsetof(RCC_TypeDef, APBENR1)), RCC_APBENR1_USART3EN, &System::APB1BusClock, uart_shared_irq, 0,
        DMA::TX.ch, DMA::RX.ch
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
        DMA::TX.ch, DMA::RX.ch
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
        DMA::TX.ch, DMA::RX.ch
    };

	struct TX { static constexpr PIN PC12 = { GPIOC_BASE, 12, 3, USART5_BASE }; };
	struct RX {
#ifdef GPIOD_BASE
#ifdef GPIOD_BASE
		static constexpr PIN PD2 = { GPIOD_BASE, 2, 3, USART5_BASE };
#endif
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
        DMA::TX.ch, DMA::RX.ch
    };

	struct TX { static constexpr PIN PC0 = { GPIOC_BASE, 0, 4, USART6_BASE }; };
	struct RX { static constexpr PIN PC1 = { GPIOC_BASE, 1, 4, USART6_BASE }; };
};
#endif

enum class FIFO {
	NO = 0,
	EN = USART_CR1_FIFOEN
};

enum class FIFO_TH {
	NO    = 0,
	TH_1_8 = 0b000,
	TH_1_4 = 0b001,
	TH_1_2 = 0b010,
	TH_3_4 = 0b011,
	TH_7_8 = 0b100,
	all    = 0b101 ///< RX full / TX empty (threshold encoding 5).
};

static constexpr uint32_t FIFO_TH_TX_Pos = USART_CR3_TXFTCFG_Pos;
static constexpr uint32_t FIFO_TH_RX_Pos = USART_CR3_RXFTCFG_Pos;

#elif defined(STM32F4) || defined(STM32F7)

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

enum class FIFO {
	NO = 0,
};

enum class FIFO_TH {
	NO = 0,
};

static constexpr uint32_t FIFO_TH_TX_Pos = 0;
static constexpr uint32_t FIFO_TH_RX_Pos = 0;

#endif  // STM32 family

// ---------------------------------------------------------------------------
// Register-name abstraction (private members)
//
// F4 uses a single DR for both TX and RX, and SR for status/clear.
// G0 and F7 split the data register into TDR/RDR and use ISR/ICR.
// Private accessors hide these differences from all public methods above.
// ---------------------------------------------------------------------------
private:

#if defined(STM32F4)
	static constexpr uint32_t CR1_TXEIE  = USART_CR1_TXEIE;
	static constexpr uint32_t CR1_RXNEIE = USART_CR1_RXNEIE;

	volatile uint32_t& TXD()        const { return USARTx->DR; }
	volatile uint32_t& RXD()        const { return USARTx->DR; }
	volatile uint32_t& Status_reg() const { return USARTx->SR; }
	volatile uint32_t& Clear_reg()  const { return USARTx->SR; }

	static constexpr uint32_t ISR_TXE  = USART_SR_TXE;
	static constexpr uint32_t ISR_RXNE = USART_SR_RXNE;
	static constexpr uint32_t ISR_IDLE = USART_SR_IDLE;
	static constexpr uint32_t ISR_TC   = USART_SR_TC;

#elif defined(STM32F7)
	static constexpr uint32_t CR1_TXEIE  = USART_CR1_TXEIE;
	static constexpr uint32_t CR1_RXNEIE = USART_CR1_RXNEIE;

	volatile uint32_t& TXD()        const { return USARTx->TDR; }
	volatile uint32_t& RXD()        const { return USARTx->RDR; }
	volatile uint32_t& Status_reg() const { return USARTx->ISR; }
	volatile uint32_t& Clear_reg()  const { return USARTx->ICR; }

	static constexpr uint32_t ISR_TXE  = USART_ISR_TXE;
	static constexpr uint32_t ISR_RXNE = USART_ISR_RXNE;
	static constexpr uint32_t ISR_IDLE = USART_ISR_IDLE;
	static constexpr uint32_t ISR_TC   = USART_ISR_TC;

#elif defined(STM32G0)
	static constexpr uint32_t CR1_TXEIE  = USART_CR1_TXEIE_TXFNFIE;
	static constexpr uint32_t CR1_RXNEIE = USART_CR1_RXNEIE_RXFNEIE;

	volatile uint32_t& TXD()        const { return USARTx->TDR; }
	volatile uint32_t& RXD()        const { return USARTx->RDR; }
	volatile uint32_t& Status_reg() const { return USARTx->ISR; }
	volatile uint32_t& Clear_reg()  const { return USARTx->ICR; }

	static constexpr uint32_t ISR_TXE  = USART_ISR_TXE_TXFNF;
	static constexpr uint32_t ISR_RXNE = USART_ISR_RXNE_RXFNE;
	static constexpr uint32_t ISR_IDLE = USART_ISR_IDLE;
	static constexpr uint32_t ISR_TC   = USART_ISR_TC;
	static constexpr uint32_t ISR_TXFT = USART_ISR_TXFT;
	static constexpr uint32_t ISR_RXFT = USART_ISR_RXFT;

#endif

// RX interrupt sources belong to one packet; completion disables these only.
static constexpr uint32_t CR1_RX_IRQ = CR1_RXNEIE | USART_CR1_IDLEIE | USART_CR1_PEIE;
#if defined(STM32G0)
static constexpr uint32_t CR3_RX_IRQ = USART_CR3_EIE | USART_CR3_RXFTIE;
static constexpr uint32_t CR3_TX_IRQ = USART_CR3_TXFTIE;
#else
static constexpr uint32_t CR3_RX_IRQ = USART_CR3_EIE;
static constexpr uint32_t CR3_TX_IRQ = 0;
#endif
#if defined(STM32F4)
static constexpr uint32_t UART_ERRORS = USART_SR_PE | USART_SR_FE | USART_SR_NE | USART_SR_ORE;
#else
static constexpr uint32_t UART_ERRORS = USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE;
#endif

public:  // restore public: for whatever follows this #include in uart.hpp

#endif  // Section A: defined(UART_H_) && !defined(UART_DEFS_CPP)

#undef UART_INFO
