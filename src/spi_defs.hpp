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
 * Section B — peripheral-info table + dummy/discard words
 * --------------------------------------------------------------------
 * Guard:  SPI_DEFS_CPP  (defined in spi.cpp just before the include)
 * Scope:  file scope inside spi.cpp.
 * Result: SPI::spi_table[], spi_dummy_word and spi_discard_word.
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

#ifdef SPI1_BASE
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
#endif

#ifdef SPI2_BASE
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
#endif

#ifdef SPI3_BASE
struct _3 {
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

// ---------------------------------------------------------------------------
// STM32F4 / STM32F7
// ---------------------------------------------------------------------------
#elif defined(STM32F4) || defined(STM32F7)

#ifdef SPI1_BASE
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
#endif

#ifdef SPI2_BASE
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
#endif

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

#ifdef SPI4_BASE
struct _4 {
	struct SCK {
#if defined(STM32F411xE) || defined(STM32F412Cx) || defined(STM32F412Rx) || defined(STM32F412Vx) || defined(STM32F412Zx) || defined(STM32F413xx) || defined(STM32F423xx)
		static constexpr PIN PB13 = { GPIOB_BASE, 13, 6, SPI4_BASE };
#endif
		static constexpr PIN PE2 = { GPIOE_BASE, 2, 5, SPI4_BASE };
		static constexpr PIN PE12 = { GPIOE_BASE, 12, 5, SPI4_BASE };
	};
	struct MOSI {
#if defined(STM32F411xE) || defined(STM32F412Cx) || defined(STM32F412Rx) || defined(STM32F412Vx) || defined(STM32F412Zx) || defined(STM32F413xx) || defined(STM32F423xx)
		static constexpr PIN PA1 = { GPIOA_BASE, 1, 5, SPI4_BASE };
#endif
		static constexpr PIN PE6 = { GPIOE_BASE, 6, 5, SPI4_BASE };
		static constexpr PIN PE14 = { GPIOE_BASE, 14, 5, SPI4_BASE };
	};
	struct MISO {
#if defined(STM32F411xE) || defined(STM32F412Cx) || defined(STM32F412Rx) || defined(STM32F412Vx) || defined(STM32F412Zx) || defined(STM32F413xx) || defined(STM32F423xx)
		static constexpr PIN PA11 = { GPIOA_BASE, 11, 6, SPI4_BASE };
#endif
		static constexpr PIN PE5 = { GPIOE_BASE, 5, 5, SPI4_BASE };
		static constexpr PIN PE13 = { GPIOE_BASE, 13, 5, SPI4_BASE };
	};
	struct SS {
#if defined(STM32F411xE) || defined(STM32F412Cx) || defined(STM32F412Rx) || defined(STM32F412Vx) || defined(STM32F412Zx) || defined(STM32F413xx) || defined(STM32F423xx)
		static constexpr PIN PB12 = { GPIOB_BASE, 12, 6, SPI4_BASE };
#endif
		static constexpr PIN PE4 = { GPIOE_BASE, 4, 5, SPI4_BASE };
		static constexpr PIN PE11 = { GPIOE_BASE, 11, 5, SPI4_BASE };
	};
};
#endif

#ifdef SPI5_BASE
// F410/F411/F412/F413/F423 route SPI5 through AF6; other F4/F7 use AF5.
#if defined(STM32F410Cx) || defined(STM32F410Rx) || defined(STM32F411xE) || defined(STM32F412Cx) || defined(STM32F412Rx) || defined(STM32F412Vx) || defined(STM32F412Zx) || defined(STM32F413xx) || defined(STM32F423xx)
struct _5 {
	struct SCK {
		static constexpr PIN PB0 = { GPIOB_BASE, 0, 6, SPI5_BASE };
#ifdef GPIOE_BASE
		static constexpr PIN PE2 = { GPIOE_BASE, 2, 6, SPI5_BASE };
		static constexpr PIN PE12 = { GPIOE_BASE, 12, 6, SPI5_BASE };
#endif
	};
	struct MOSI {
		static constexpr PIN PA10 = { GPIOA_BASE, 10, 6, SPI5_BASE };
#if !defined(STM32F410Cx) && !defined(STM32F410Rx)
		static constexpr PIN PB8 = { GPIOB_BASE, 8, 6, SPI5_BASE };
#endif
#ifdef GPIOE_BASE
		static constexpr PIN PE6 = { GPIOE_BASE, 6, 6, SPI5_BASE };
		static constexpr PIN PE14 = { GPIOE_BASE, 14, 6, SPI5_BASE };
#endif
	};
	struct MISO {
		static constexpr PIN PA12 = { GPIOA_BASE, 12, 6, SPI5_BASE };
#ifdef GPIOE_BASE
		static constexpr PIN PE5 = { GPIOE_BASE, 5, 6, SPI5_BASE };
		static constexpr PIN PE13 = { GPIOE_BASE, 13, 6, SPI5_BASE };
#endif
	};
	struct SS {
		static constexpr PIN PB1 = { GPIOB_BASE, 1, 6, SPI5_BASE };
#ifdef GPIOE_BASE
		static constexpr PIN PE4 = { GPIOE_BASE, 4, 6, SPI5_BASE };
		static constexpr PIN PE11 = { GPIOE_BASE, 11, 6, SPI5_BASE };
#endif
	};
};
#else
struct _5 {
	struct SCK {
		static constexpr PIN PF7 = { GPIOF_BASE, 7, 5, SPI5_BASE };
	};
	struct MOSI {
		static constexpr PIN PF9 = { GPIOF_BASE, 9, 5, SPI5_BASE };
	};
	struct MISO {
		static constexpr PIN PF8 = { GPIOF_BASE, 8, 5, SPI5_BASE };
	};
	struct SS {
		static constexpr PIN PF6 = { GPIOF_BASE, 6, 5, SPI5_BASE };
	};
};
#endif
#endif

#ifdef SPI6_BASE
struct _6 {
	struct SCK {
		static constexpr PIN PG13 = { GPIOG_BASE, 13, 5, SPI6_BASE };
	};
	struct MOSI {
		static constexpr PIN PG14 = { GPIOG_BASE, 14, 5, SPI6_BASE };
	};
	struct MISO {
		static constexpr PIN PG12 = { GPIOG_BASE, 12, 5, SPI6_BASE };
	};
	struct SS {
		static constexpr PIN PG8 = { GPIOG_BASE, 8, 5, SPI6_BASE };
	};
};
#endif

#endif  // STM32 family

#endif  // Section A: defined(SPI_HPP_) && !defined(SPI_DEFS_CPP)

// ===========================================================================
// Section B: Peripheral-info table — spi.cpp file scope
// ===========================================================================
#ifdef SPI_DEFS_CPP

// ---------------------------------------------------------------------------
// spi_table: maps each SPI_TypeDef* to its clock/reset registers and NVIC IRQ.
// Searched linearly by SPI::SetUp() to fill the _info pointer.
// ---------------------------------------------------------------------------

#if defined(STM32F4) || defined(STM32F7)
const SPI::PeriphInfo SPI::spi_table[] = {
#ifdef SPI1_BASE
    { SPI1, &RCC->APB2ENR, RCC_APB2ENR_SPI1EN, &RCC->APB2RSTR, RCC_APB2RSTR_SPI1RST, &System::APB2BusClock, SPI1_IRQn },
#endif
#ifdef SPI2_BASE
    { SPI2, &RCC->APB1ENR, RCC_APB1ENR_SPI2EN, &RCC->APB1RSTR, RCC_APB1RSTR_SPI2RST, &System::APB1BusClock, SPI2_IRQn },
#endif
#ifdef SPI3_BASE
    { SPI3, &RCC->APB1ENR, RCC_APB1ENR_SPI3EN, &RCC->APB1RSTR, RCC_APB1RSTR_SPI3RST, &System::APB1BusClock, SPI3_IRQn },
#endif
#ifdef SPI4_BASE
    { SPI4, &RCC->APB2ENR, RCC_APB2ENR_SPI4EN, &RCC->APB2RSTR, RCC_APB2RSTR_SPI4RST, &System::APB2BusClock, SPI4_IRQn },
#endif
#ifdef SPI5_BASE
    { SPI5, &RCC->APB2ENR, RCC_APB2ENR_SPI5EN, &RCC->APB2RSTR, RCC_APB2RSTR_SPI5RST, &System::APB2BusClock, SPI5_IRQn },
#endif
#ifdef SPI6_BASE
    { SPI6, &RCC->APB2ENR, RCC_APB2ENR_SPI6EN, &RCC->APB2RSTR, RCC_APB2RSTR_SPI6RST, &System::APB2BusClock, SPI6_IRQn },
#endif
};
#elif defined(STM32G0)
const SPI::PeriphInfo SPI::spi_table[] = {
#ifdef SPI1_BASE
    { SPI1, &RCC->APBENR2, RCC_APBENR2_SPI1EN, &RCC->APBRSTR2, RCC_APBRSTR2_SPI1RST, &System::APB1BusClock, SPI1_IRQn },
#endif
#ifdef SPI2_BASE
    { SPI2, &RCC->APBENR1, RCC_APBENR1_SPI2EN, &RCC->APBRSTR1, RCC_APBRSTR1_SPI2RST, &System::APB1BusClock,
#ifdef SPI3_BASE
      SPI2_3_IRQn
#else
      SPI2_IRQn
#endif
 },
#endif
#ifdef SPI3_BASE
    { SPI3, &RCC->APBENR1, RCC_APBENR1_SPI3EN, &RCC->APBRSTR1, RCC_APBRSTR1_SPI3RST, &System::APB1BusClock, SPI2_3_IRQn },
#endif
};
#endif


// Immutable all-one dummy source is read directly from Flash. The discard sink
// is written only by DMA and is never consumed or modified by the CPU.
static const uint16_t spi_dummy_word = 0xFFFFU;
static uint16_t spi_discard_word;

#endif  // SPI_DEFS_CPP (Section B)
