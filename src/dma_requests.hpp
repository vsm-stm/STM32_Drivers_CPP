/**
 * @file    dma_requests.hpp
 * @brief   Compile-time DMA peripheral request tables for STM32 (F4/F7/G0).
 *
 * Defines DMAReq — a struct that pairs a DMA stream/channel pointer with the
 * peripheral selection code — and a namespace Req containing named constants
 * for every supported peripheral direction.
 *
 * Usage
 * -----
 * @code
 *   // Construct a DMA_Sx driver for a specific peripheral:
 *   DMA_Sx dma(Req::USART1::TX);
 *
 *   // F4/F7 — when you need a stream other than the primary one:
 *   DMA_Sx dma(Req::SPI1::RX_alt);
 * @endcode
 *
 * G0 notes
 * --------
 * DMAMUX1 allows any DMA1 channel to be routed to any peripheral.
 * The channel assignments in this file are a recommended default.
 * If two peripherals listed here share a channel (e.g. SPI1 and USART1
 * both suggest Channel2), reassign one of them by constructing DMA_Sx
 * with a raw channel pointer and passing the request ID in
 * StreamSettings::channel.
 *
 * Request IDs: RM0444 Table 37 (STM32G0x0 — G030/G050/G070).
 *
 * F4/F7 notes
 * -----------
 * Stream + CHSEL are hardware-fixed.  Where a peripheral can be served
 * by more than one stream, the primary stream is named ::TX / ::RX and
 * the alternative is named ::TX_alt / ::RX_alt.
 *
 * CHSEL values: RM0090 Table 42 (F407) / RM0385 Table 8 (F7).
 */

#ifndef DMA_REQUESTS_H_
#define DMA_REQUESTS_H_

#include "system.hpp"

namespace DMA_Sx_ns {

// ---------------------------------------------------------------------------
// DMAReq
// ---------------------------------------------------------------------------

/**
 * @brief Compile-time DMA peripheral selection descriptor.
 *
 *  - F4/F7: stream pointer + CHSEL value (hardware-fixed per peripheral).
 *           Both fields are required; stream encodes which DMA/stream to use.
 *
 *  - G0:    DMAMUX1 request ID only (RM0444 Table 37).
 *           The DMA channel is chosen separately by the user (any of the five
 *           DMA1 channels may serve any peripheral via DMAMUX1).
 */
struct DMAReq {
#if defined(STM32F4) || defined(STM32F7)
	DMA_Stream_TypeDef* stream;
	uint32_t            ch;     ///< CHSEL field for DMA_SxCR (0–7)
	constexpr bool IsValid() const noexcept { return stream != nullptr; }
#elif defined(STM32G0)
	uint32_t ch;                ///< DMAMUX1 request ID (0 = invalid / mem-to-mem)
	constexpr bool IsValid() const noexcept { return ch != 0; }
#endif
};

// ---------------------------------------------------------------------------
// Req namespace
// ---------------------------------------------------------------------------

namespace Req {

// Struct names use CamelCase to avoid clashing with CMSIS peripheral macros
// (USART1, SPI1, ADC1, TIM1, etc. are all #define'd as register pointers).

#if defined(STM32G0)

// Request IDs for STM32G0x0 (G030/G050/G070) — RM0444 Table 37.
// On G0 the DMA channel is chosen by the user; only the DMAMUX1 request ID
// is encoded here.  Pass these to DMA_Sx(DMA_Channel_TypeDef*, DMAReq).

struct Adc1  { static constexpr DMAReq RX = {  5U }; };

struct Spi1  { static constexpr DMAReq RX = {  9U };
			   static constexpr DMAReq TX = { 10U }; };
struct Spi2  { static constexpr DMAReq RX = { 11U };
			   static constexpr DMAReq TX = { 12U }; };

struct I2c1  { static constexpr DMAReq RX = { 13U };
			   static constexpr DMAReq TX = { 14U }; };

struct Usart1 { static constexpr DMAReq RX = { 47U };
				static constexpr DMAReq TX = { 48U }; };
struct Usart2 { static constexpr DMAReq RX = { 49U };
				static constexpr DMAReq TX = { 50U }; };

struct Tim1 {
	static constexpr DMAReq CH1  = { 17U };
	static constexpr DMAReq CH2  = { 18U };
	static constexpr DMAReq CH3  = { 19U };
	static constexpr DMAReq CH4  = { 20U };
	static constexpr DMAReq TRIG = { 21U };
	static constexpr DMAReq UP   = { 22U };
};
struct Tim3 {
	static constexpr DMAReq CH1  = { 29U };
	static constexpr DMAReq CH2  = { 30U };
	static constexpr DMAReq CH3  = { 31U };
	static constexpr DMAReq CH4  = { 32U };
	static constexpr DMAReq TRIG = { 33U };
	static constexpr DMAReq UP   = { 34U };
};
struct Tim16 { static constexpr DMAReq CH1 = { 43U };
			   static constexpr DMAReq UP  = { 44U }; };
struct Tim17 { static constexpr DMAReq CH1 = { 45U };
			   static constexpr DMAReq UP  = { 46U }; };

#elif defined(STM32F4) || defined(STM32F7)

// Stream + CHSEL values for STM32F4 (RM0090 Table 42) / F7 (RM0385 Table 8).
// _alt = alternate stream when more than one exists for the same direction.

struct Usart1 {
	static constexpr DMAReq RX     = { DMA2_Stream2, 4U };
	static constexpr DMAReq TX     = { DMA2_Stream7, 4U };
	static constexpr DMAReq TX_alt = { DMA2_Stream5, 4U };  // Stream5 Ch4
};
struct Usart2 {
	static constexpr DMAReq RX = { DMA1_Stream5, 4U };
	static constexpr DMAReq TX = { DMA1_Stream6, 4U };
};
struct Usart3 {
	static constexpr DMAReq RX     = { DMA1_Stream1, 4U };
	static constexpr DMAReq TX     = { DMA1_Stream3, 4U };
	static constexpr DMAReq TX_alt = { DMA1_Stream4, 7U };  // Stream4 Ch7
};
struct Uart4 {
	static constexpr DMAReq RX = { DMA1_Stream2, 4U };
	static constexpr DMAReq TX = { DMA1_Stream4, 4U };
};
struct Uart5 {
	static constexpr DMAReq RX = { DMA1_Stream0, 4U };
	static constexpr DMAReq TX = { DMA1_Stream7, 4U };
};
struct Usart6 {
	static constexpr DMAReq RX     = { DMA2_Stream1, 5U };
	static constexpr DMAReq TX     = { DMA2_Stream6, 5U };
	static constexpr DMAReq RX_alt = { DMA2_Stream2, 5U };  // Stream2 Ch5
	static constexpr DMAReq TX_alt = { DMA2_Stream7, 5U };  // Stream7 Ch5
};

struct Spi1 {
	static constexpr DMAReq RX     = { DMA2_Stream0, 3U };
	static constexpr DMAReq TX     = { DMA2_Stream3, 3U };
	static constexpr DMAReq RX_alt = { DMA2_Stream2, 3U };  // Stream2 Ch3
	static constexpr DMAReq TX_alt = { DMA2_Stream5, 3U };  // Stream5 Ch3
};
struct Spi2 {
	static constexpr DMAReq RX = { DMA1_Stream3, 0U };
	static constexpr DMAReq TX = { DMA1_Stream4, 0U };
};
struct Spi3 {
	static constexpr DMAReq RX     = { DMA1_Stream0, 0U };
	static constexpr DMAReq TX     = { DMA1_Stream5, 0U };
	static constexpr DMAReq RX_alt = { DMA1_Stream2, 0U };  // Stream2 Ch0
	static constexpr DMAReq TX_alt = { DMA1_Stream7, 0U };  // Stream7 Ch0
};

struct I2c1 {
	static constexpr DMAReq RX     = { DMA1_Stream0, 1U };
	static constexpr DMAReq TX     = { DMA1_Stream6, 1U };
	static constexpr DMAReq RX_alt = { DMA1_Stream5, 1U };  // Stream5 Ch1
};
struct I2c2 {
	static constexpr DMAReq RX     = { DMA1_Stream2, 7U };
	static constexpr DMAReq TX     = { DMA1_Stream7, 7U };
	static constexpr DMAReq RX_alt = { DMA1_Stream3, 7U };  // Stream3 Ch7
};
struct I2c3 {
	static constexpr DMAReq RX = { DMA1_Stream2, 1U };
	static constexpr DMAReq TX = { DMA1_Stream4, 3U };
};

struct ADC1 {
	static constexpr DMAReq RX     = { DMA2_Stream0, 0U };
	static constexpr DMAReq RX_alt = { DMA2_Stream4, 0U };  // Stream4 Ch0
};
struct ADC2 {
	static constexpr DMAReq RX     = { DMA2_Stream2, 1U };
	static constexpr DMAReq RX_alt = { DMA2_Stream3, 1U };  // Stream3 Ch1
};
struct ADC3 {
	static constexpr DMAReq RX     = { DMA2_Stream0, 2U };
	static constexpr DMAReq RX_alt = { DMA2_Stream1, 2U };  // Stream1 Ch2
};

#endif

} // namespace Req

} // namespace DMA_Sx_ns

#endif // DMA_REQUESTS_H_
