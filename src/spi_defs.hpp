/**
 * @file    spi_defs.hpp
 * @brief   All compile-time and run-time tables for the SPI driver.
 *
 * This file has two sections, each activated by a different preprocessor symbol
 * so that the right content appears in the right C++ scope.
 *
 * Section A — compile-time pin tables
 * -------------------------------------
 * Guard:  SPI_HPP_ (the include-guard of spi.hpp, already defined by the time
 *                   the "#include "spi_defs.hpp"" inside the class body is reached)
 *         also !SPI_DEFS_CPP to prevent re-emission at file scope from spi.cpp.
 * Scope:  SPI class body — public nested pin types.
 * Result: SPI::_N::SCK / MOSI / MISO / SS for G0 and F4/F7 families.
 *
 * Section B — peripheral-info table + DMA request table + dummy byte
 * --------------------------------------------------------------------
 * Guard:  SPI_DEFS_CPP  (defined in spi.cpp just before the include)
 * Scope:  file scope inside spi.cpp.
 * Result: SPI::spi_table[], SpiDmaInfo struct, spi_dma_req_table[],
 *         spi_dummy_byte.
 *
 * Usage
 * -----
 * spi.hpp (inside class body — SPI_HPP_ already defined):
 * @code
 *   #include "spi_defs.hpp"   // Section A: compile-time pin tables
 * @endcode
 *
 * spi.cpp (after #include "spi.hpp"):
 * @code
 *   #define SPI_DEFS_CPP
 *   #include "spi_defs.hpp"   // Section B: peripheral + DMA tables
 *   #undef  SPI_DEFS_CPP
 * @endcode
 *
 * If this file is opened directly (e.g. by an IDE indexer) neither symbol is
 * defined — the file produces nothing, no false global-scope declarations.
 */

// ===========================================================================
// Section A: Compile-time pin tables — SPI class body
// ===========================================================================
#if defined(SPI_HPP_) && !defined(SPI_DEFS_CPP)

// ---------------------------------------------------------------------------
// Each constexpr PIN packs: GPIO port base, pin number, alternate-function
// index, and the owning SPI base address.  Zero RAM footprint.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// STM32G0
// ---------------------------------------------------------------------------
#if defined(STM32G0)

struct _1 {
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

struct _2 {
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

// ---------------------------------------------------------------------------
// STM32F4 / STM32F7
// ---------------------------------------------------------------------------
#elif defined(STM32F4) || defined(STM32F7)

struct _1 {
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

struct _2 {
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

#ifdef SPI3_BASE
struct _3 {
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

#endif  // STM32 family

#endif  // Section A: defined(SPI_HPP_) && !defined(SPI_DEFS_CPP)

// ===========================================================================
// Section B: Peripheral-info table + DMA request table — spi.cpp file scope
// ===========================================================================
#ifdef SPI_DEFS_CPP

// ---------------------------------------------------------------------------
// spi_table: maps each SPI_TypeDef* to its clock/reset registers and NVIC IRQ.
// Searched linearly by SPI::SetHard() to fill the _info pointer.
// ---------------------------------------------------------------------------

#if defined(STM32F4) || defined(STM32F7)
const SPI::PeriphInfo SPI::spi_table[] = {
	{ SPI1, &RCC->APB2ENR, RCC_APB2ENR_SPI1EN, &RCC->APB2RSTR, RCC_APB2RSTR_SPI1RST, &System::APB2BusClock, SPI1_IRQn },
	{ SPI2, &RCC->APB1ENR, RCC_APB1ENR_SPI2EN, &RCC->APB1RSTR, RCC_APB1RSTR_SPI2RST, &System::APB1BusClock, SPI2_IRQn },
	{ SPI3, &RCC->APB1ENR, RCC_APB1ENR_SPI3EN, &RCC->APB1RSTR, RCC_APB1RSTR_SPI3RST, &System::APB1BusClock, SPI3_IRQn },
#if defined(STM32F446xx) || defined(STM32F429xx)
	{ SPI4, &RCC->APB2ENR, RCC_APB2ENR_SPI4EN, &RCC->APB2RSTR, RCC_APB2RSTR_SPI4RST, &System::APB2BusClock, SPI4_IRQn },
#endif
};
#elif defined(STM32G0)
const SPI::PeriphInfo SPI::spi_table[] = {
	{ SPI1, &RCC->APBENR2, RCC_APBENR2_SPI1EN, &RCC->APBRSTR2, RCC_APBRSTR2_SPI1RST, &System::APB1BusClock, SPI1_IRQn },
	{ SPI2, &RCC->APBENR1, RCC_APBENR1_SPI2EN, &RCC->APBRSTR1, RCC_APBRSTR1_SPI2RST, &System::APB1BusClock, SPI2_IRQn },
};
#endif

// ---------------------------------------------------------------------------
// spi_dma_req_table: maps each SPI_TypeDef* to its DMAMUX request IDs (G0)
// or CHSEL values (F4/F7).  Used by AttachDMA() so the user does not have to
// know peripheral-specific request IDs.
// ---------------------------------------------------------------------------

struct SpiDmaInfo { SPI_TypeDef* periph; uint32_t tx_req; uint32_t rx_req; };

#if defined(STM32G0)
static const SpiDmaInfo spi_dma_req_table[] = {
	{ SPI1, DMA_Sx::Req::Spi1::TX.ch, DMA_Sx::Req::Spi1::RX.ch },
	{ SPI2, DMA_Sx::Req::Spi2::TX.ch, DMA_Sx::Req::Spi2::RX.ch },
};
#elif defined(STM32F4) || defined(STM32F7)
static const SpiDmaInfo spi_dma_req_table[] = {
	{ SPI1, DMA_Sx::Req::Spi1::TX.ch, DMA_Sx::Req::Spi1::RX.ch },
	{ SPI2, DMA_Sx::Req::Spi2::TX.ch, DMA_Sx::Req::Spi2::RX.ch },
#ifdef SPI3_BASE
	{ SPI3, DMA_Sx::Req::Spi3::TX.ch, DMA_Sx::Req::Spi3::RX.ch },
#endif
};
#endif

// Dummy byte transmitted on MOSI during ReceiveDMA to generate the SPI clock.
static uint8_t spi_dummy_byte = 0xFF;

#endif  // SPI_DEFS_CPP (Section B)
