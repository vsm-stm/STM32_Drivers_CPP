/**
 * @file    uart_defs.hpp
 * @brief   All compile-time and run-time tables for the USART driver.
 *
 * This file has two sections, each activated by a different preprocessor symbol
 * so that the right content appears in the right C++ scope.
 *
 * Section A — pin tables, FIFO enums, register abstraction
 * ----------------------------------------------------------
 * Guard:  UART_H_  (the include-guard of uart.hpp, already defined by the
 *                   time the "#include "uart_defs.hpp"" inside the class body
 *                   is reached)
 * Scope:  USART class body — public pin tables first, then switches to
 *         private: for register accessors, then restores public: at the end.
 * Result: USART::_N::TX/RX nested pin types, USART::FIFO, USART::FIFO_TH,
 *         plus private TXD()/RXD()/Status_reg()/Clear_reg() and ISR_* flags.
 *
 * Section B — peripheral-info table + DMA request table
 * -------------------------------------------------------
 * Guard:  UART_DEFS_CPP  (defined in uart.cpp just before the include)
 * Scope:  file scope inside uart.cpp.
 * Result: USART::usart_table[] is defined; UartDmaInfo + uart_dma_req_table
 *         are visible to all functions in uart.cpp.
 *
 * Usage
 * -----
 * uart.hpp (inside class body — UART_H_ already defined):
 * @code
 *   #include "uart_defs.hpp"   // Section A: pin tables + register abstraction
 * @endcode
 *
 * uart.cpp (after #include "uart.hpp"):
 * @code
 *   #define UART_DEFS_CPP
 *   #include "uart_defs.hpp"   // Section B: peripheral + DMA tables
 *   #undef  UART_DEFS_CPP
 * @endcode
 *
 * If this file is opened directly (e.g. by an IDE indexer) neither symbol is
 * defined — the file produces nothing, no false global-scope declarations.
 */

// ===========================================================================
// Section A: Pin tables + FIFO enums — USART class body
// ===========================================================================
#if defined(UART_H_) && !defined(UART_DEFS_CPP)

// ---------------------------------------------------------------------------
// Each constexpr PIN packs: GPIO port base, pin number, alternate-function
// index, and the owning USART base address.  Zero RAM footprint.
// ---------------------------------------------------------------------------

#if defined(STM32G0)

struct _1 {
	struct TX {
		static constexpr PIN PA9  = { GPIOA_BASE,  9, 0, USART1_BASE };
		static constexpr PIN PB6  = { GPIOB_BASE,  6, 0, USART1_BASE };
	};
	struct RX {
		static constexpr PIN PA10 = { GPIOA_BASE, 10, 0, USART1_BASE };
		static constexpr PIN PB7  = { GPIOB_BASE,  7, 0, USART1_BASE };
	};
};

struct _2 {
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

#ifdef UART4_BASE
struct _4 {
	struct TX {
		static constexpr PIN PA0  = { GPIOA_BASE,  0, 0, UART4_BASE };
		static constexpr PIN PC10 = { GPIOC_BASE, 10, 0, UART4_BASE };
	};
	struct RX {
		static constexpr PIN PA1  = { GPIOA_BASE,  1, 0, UART4_BASE };
		static constexpr PIN PC11 = { GPIOC_BASE, 11, 0, UART4_BASE };
	};
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
	all    = 0b111
};

static constexpr uint32_t FIFO_TH_TX_Pos = USART_CR3_TXFTCFG_Pos;
static constexpr uint32_t FIFO_TH_RX_Pos = USART_CR3_RXFTCFG_Pos;

#elif defined(STM32F4) || defined(STM32F7)

struct _1 {
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
	struct TX {
		static constexpr PIN PA2  = { GPIOA_BASE,  2, 7, USART2_BASE };
		static constexpr PIN PD5  = { GPIOD_BASE,  5, 7, USART2_BASE };
	};
	struct RX {
		static constexpr PIN PA3  = { GPIOA_BASE,  3, 7, USART2_BASE };
		static constexpr PIN PD6  = { GPIOD_BASE,  6, 7, USART2_BASE };
	};
};

#ifdef USART3_BASE
struct _3 {
	struct TX {
		static constexpr PIN PB10 = { GPIOB_BASE, 10, 7, USART3_BASE };
		static constexpr PIN PC10 = { GPIOC_BASE, 10, 7, USART3_BASE };
		static constexpr PIN PD8  = { GPIOD_BASE,  8, 7, USART3_BASE };
	};
	struct RX {
		static constexpr PIN PB11 = { GPIOB_BASE, 11, 7, USART3_BASE };
		static constexpr PIN PC11 = { GPIOC_BASE, 11, 7, USART3_BASE };
		static constexpr PIN PD9  = { GPIOD_BASE,  9, 7, USART3_BASE };
	};
};
#endif

#ifdef UART4_BASE
struct _4 {
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
	struct TX {
		static constexpr PIN PC12 = { GPIOC_BASE, 12, 8, UART5_BASE };
	};
	struct RX {
		static constexpr PIN PD2  = { GPIOD_BASE,  2, 8, UART5_BASE };
	};
};
#endif

#ifdef USART6_BASE
struct _6 {
	struct TX {
		static constexpr PIN PC6  = { GPIOC_BASE,  6, 8, USART6_BASE };
		static constexpr PIN PG14 = { GPIOG_BASE, 14, 8, USART6_BASE };
	};
	struct RX {
		static constexpr PIN PC7  = { GPIOC_BASE,  7, 8, USART6_BASE };
		static constexpr PIN PG9  = { GPIOG_BASE,  9, 8, USART6_BASE };
	};
};
#endif

#ifdef UART7_BASE
struct _7 {
	struct TX {
		static constexpr PIN PE8  = { GPIOE_BASE,  8, 8, UART7_BASE };
		static constexpr PIN PF7  = { GPIOF_BASE,  7, 8, UART7_BASE };
	};
	struct RX {
		static constexpr PIN PE7  = { GPIOE_BASE,  7, 8, UART7_BASE };
		static constexpr PIN PF6  = { GPIOF_BASE,  6, 8, UART7_BASE };
	};
};
#endif

#ifdef UART8_BASE
struct _8 {
	struct TX {
		static constexpr PIN PE1  = { GPIOE_BASE,  1, 8, UART8_BASE };
	};
	struct RX {
		static constexpr PIN PE0  = { GPIOE_BASE,  0, 8, UART8_BASE };
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

public:  // restore public: for whatever follows this #include in uart.hpp

#endif  // Section A: defined(UART_H_) && !defined(UART_DEFS_CPP)

// ===========================================================================
// Section B: Peripheral-info table + DMA request table — uart.cpp file scope
// ===========================================================================
#ifdef UART_DEFS_CPP

// ---------------------------------------------------------------------------
// usart_table: maps each USART_TypeDef* to its clock-enable register, bit
// mask, bus-frequency pointer, and NVIC IRQ number.  Searched linearly by
// USART::SetUp() to fill the _info pointer.
// ---------------------------------------------------------------------------

#if defined(STM32F4) || defined(STM32F7)
const USART::PeriphInfo USART::usart_table[] = {
	{ USART1, &RCC->APB2ENR, RCC_APB2ENR_USART1EN, &System::APB2BusClock, USART1_IRQn },
	{ USART2, &RCC->APB1ENR, RCC_APB1ENR_USART2EN, &System::APB1BusClock, USART2_IRQn },
#ifndef STM32F411xE
	{ USART3, &RCC->APB1ENR, RCC_APB1ENR_USART3EN, &System::APB1BusClock, USART3_IRQn },
	{ UART4,  &RCC->APB1ENR, RCC_APB1ENR_UART4EN,  &System::APB1BusClock, UART4_IRQn  },
	{ UART5,  &RCC->APB1ENR, RCC_APB1ENR_UART5EN,  &System::APB1BusClock, UART5_IRQn  },
	{ USART6, &RCC->APB2ENR, RCC_APB2ENR_USART6EN, &System::APB2BusClock, USART6_IRQn },
#endif
};
#elif defined(STM32G0)
const USART::PeriphInfo USART::usart_table[] = {
	{ USART1, &RCC->APBENR2, RCC_APBENR2_USART1EN, &System::APB1BusClock, USART1_IRQn },
	{ USART2, &RCC->APBENR1, RCC_APBENR1_USART2EN, &System::APB1BusClock, USART2_IRQn },
};
#endif

// ---------------------------------------------------------------------------
// uart_dma_req_table: maps each USART_TypeDef* to its DMAMUX request IDs
// (G0) or CHSEL values (F4/F7).  Used by AttachDMA() so the user does not
// have to pass the request ID explicitly to DMA_Sx.
// ---------------------------------------------------------------------------

struct UartDmaInfo {
	USART_TypeDef* periph;
	uint32_t       tx_req;
	uint32_t       rx_req;
};

#if defined(STM32G0)
static const UartDmaInfo uart_dma_req_table[] = {
	{ USART1, DMA_Sx::Req::Usart1::TX.ch, DMA_Sx::Req::Usart1::RX.ch },
	{ USART2, DMA_Sx::Req::Usart2::TX.ch, DMA_Sx::Req::Usart2::RX.ch },
};
#elif defined(STM32F4) || defined(STM32F7)
static const UartDmaInfo uart_dma_req_table[] = {
	{ USART1, DMA_Sx::Req::Usart1::TX.ch, DMA_Sx::Req::Usart1::RX.ch },
	{ USART2, DMA_Sx::Req::Usart2::TX.ch, DMA_Sx::Req::Usart2::RX.ch },
#ifndef STM32F411xE
	{ USART3, DMA_Sx::Req::Usart3::TX.ch, DMA_Sx::Req::Usart3::RX.ch },
	{ UART4,  DMA_Sx::Req::Uart4::TX.ch,  DMA_Sx::Req::Uart4::RX.ch  },
	{ UART5,  DMA_Sx::Req::Uart5::TX.ch,  DMA_Sx::Req::Uart5::RX.ch  },
	{ USART6, DMA_Sx::Req::Usart6::TX.ch, DMA_Sx::Req::Usart6::RX.ch },
#endif
};
#endif

#endif  // UART_DEFS_CPP (Section B)
