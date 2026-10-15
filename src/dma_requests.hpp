/**
 * @file    dma_requests.hpp
 * @brief   Compile-time DMA peripheral request tables for STM32 (F4/F7/G0).
 *
 * THIS FILE IS INCLUDED INSIDE THE DMA_Sx CLASS BODY (public section).
 * It defines DMA_Sx::DMAReq and DMA_Sx::Req as nested types.
 *
 * Usage
 * -----
 * @code
 *   DMA_Sx dma_tx(DMA1_Channel4, DMA_Sx::Req::Usart1::TX);  // G0
 *   DMA_Sx dma_tx(DMA_Sx::Req::Usart1::TX_DMA2_Stream7);     // F4/F7
 * @endcode
 *
 * G0 notes
 * --------
 * DMAMUX1 routes each available DMA1/DMA2 channel to a request source.
 * The channel is chosen by the user; only the DMAMUX1 request ID is encoded
 * here.  Pass these to DMA_Sx(DMA_Channel_TypeDef*, DMAReq).
 *
 * Request IDs and peripheral availability must match the selected G0 device.
 *
 * F4/F7 notes
 * -----------
 * Stream + CHSEL are hardware-fixed. Route names include controller and
 * stream: Usart3::TX_DMA1_Stream3 and Usart3::TX_DMA1_Stream4, for example.
 *
 * Route tables are shared with peripheral consumers, not inferred from CHSEL
 * alone: F4/F7 descriptors include the hardware stream as well.
 * USART1 RX_DMA2_Stream5/CH4 is an RX route. G0 USART3..6 and UART7/8
 * request declarations are conditional on peripheral availability.
 *
 * CanUseRequest() compares a candidate against constructor selection; it neither
 * claims the channel nor proves its route is valid for a particular peripheral.
 * The consumer selects from its own allowed DMAReq entries before calling SetUp.
 * See the selected device's DMA request map and the UART.md references.
 */

#ifndef DMA_REQUESTS_H_
#define DMA_REQUESTS_H_

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
 *           The DMA channel is chosen separately from those present on the MCU.
 */
struct DMAReq {
#if defined(STM32F4) || defined(STM32F7)
	DMA_Stream_TypeDef* stream;
	uint32_t            ch;     ///< CHSEL field for DMA_SxCR (0–7)
	constexpr bool IsValid() const noexcept { return stream != nullptr && ch <= 7U; }
#elif defined(STM32G0)
	uint32_t ch;                ///< DMAMUX1 request ID (0 = invalid / mem-to-mem)
	constexpr bool IsValid() const noexcept { return ch != 0 && (ch & ~DMAMUX_CxCR_DMAREQ_ID_Msk) == 0; }
#endif
};

// ---------------------------------------------------------------------------
// Req
// ---------------------------------------------------------------------------

/**
 * @brief Named DMA request constants, organised by peripheral.
 *
 * Struct names use CamelCase to avoid clashing with CMSIS peripheral macros
 * (USART1, SPI1, ADC1, TIM1, etc. are all #define'd as register pointers).
 *
 * Usage: F4/F7 DMA_Sx::Req::Usart1::TX_DMA2_Stream7; G0 DMA_Sx::Req::Usart1::TX.
 */
struct Req {

#if defined(STM32G0)

	// Common G0 request IDs; the peripheral driver checks device availability.
	struct Adc1  { static constexpr DMAReq RX = {  5U }; };

	struct I2c1  {	static constexpr DMAReq RX = { 10U };
					static constexpr DMAReq TX = { 11U }; };
	struct I2c2  {	static constexpr DMAReq RX = { 12U };
					static constexpr DMAReq TX = { 13U }; };

	struct Spi1  {	static constexpr DMAReq RX = { 16U };
					static constexpr DMAReq TX = { 17U }; };
	struct Spi2  {	static constexpr DMAReq RX = { 18U };
					static constexpr DMAReq TX = { 19U }; };

	struct Tim1 {
					static constexpr DMAReq CH1  = { 20U };
					static constexpr DMAReq CH2  = { 21U };
					static constexpr DMAReq CH3  = { 22U };
					static constexpr DMAReq CH4  = { 23U };
					static constexpr DMAReq TRIG = { 24U };
					static constexpr DMAReq UP   = { 25U };
	};
	struct Tim3 {
					static constexpr DMAReq CH1  = { 32U };
					static constexpr DMAReq CH2  = { 33U };
					static constexpr DMAReq CH3  = { 34U };
					static constexpr DMAReq CH4  = { 35U };
					static constexpr DMAReq TRIG = { 36U };
					static constexpr DMAReq UP   = { 37U };
	};

	struct Tim16 {	static constexpr DMAReq CH1 = { 44U };
					static constexpr DMAReq COM = { 45U };
					static constexpr DMAReq UP  = { 46U }; };
	struct Tim17 {	static constexpr DMAReq CH1 = { 47U };
					static constexpr DMAReq COM = { 48U };
					static constexpr DMAReq UP  = { 49U }; };

	struct Usart1 {	static constexpr DMAReq RX = { 50U };
					static constexpr DMAReq TX = { 51U }; };
	struct Usart2 { static constexpr DMAReq RX = { 52U };
					static constexpr DMAReq TX = { 53U }; };
#ifdef USART3_BASE
	struct Usart3 { static constexpr DMAReq RX = { 54U };
					static constexpr DMAReq TX = { 55U }; };
#endif
#ifdef USART4_BASE
	struct Usart4 { static constexpr DMAReq RX = { 56U };
					static constexpr DMAReq TX = { 57U }; };
#endif
#ifdef USART5_BASE
	struct Usart5 { static constexpr DMAReq RX = { 74U };
					static constexpr DMAReq TX = { 75U }; };
#endif
#ifdef USART6_BASE
	struct Usart6 { static constexpr DMAReq RX = { 76U };
					static constexpr DMAReq TX = { 77U }; };
#endif

#elif defined(STM32F4) || defined(STM32F7)

	// Stream + CHSEL values for STM32F4 (RM0090 Table 42) / F7 (RM0385 Table 8).
	// Each route name shows its DMA controller and stream, including alternatives.
	struct Usart1 {
		static constexpr DMAReq RX_DMA2_Stream2 = { DMA2_Stream2, 4U };
		static constexpr DMAReq TX_DMA2_Stream7 = { DMA2_Stream7, 4U };
		static constexpr DMAReq RX_DMA2_Stream5 = { DMA2_Stream5, 4U };  // USART1_RX, not TX.
	};
	struct Usart2 {
		static constexpr DMAReq RX_DMA1_Stream5 = { DMA1_Stream5, 4U };
		static constexpr DMAReq TX_DMA1_Stream6 = { DMA1_Stream6, 4U };
	};
	struct Usart3 {
		static constexpr DMAReq RX_DMA1_Stream1 = { DMA1_Stream1, 4U };
		static constexpr DMAReq TX_DMA1_Stream3 = { DMA1_Stream3, 4U };
		static constexpr DMAReq TX_DMA1_Stream4 = { DMA1_Stream4, 7U };  // Stream4 Ch7
	};
	struct Uart4 {
		static constexpr DMAReq RX_DMA1_Stream2 = { DMA1_Stream2, 4U };
		static constexpr DMAReq TX_DMA1_Stream4 = { DMA1_Stream4, 4U };
	};
	struct Uart5 {
		static constexpr DMAReq RX_DMA1_Stream0 = { DMA1_Stream0, 4U };
		static constexpr DMAReq TX_DMA1_Stream7 = { DMA1_Stream7, 4U };
	};
	struct Usart6 {
		static constexpr DMAReq RX_DMA2_Stream1 = { DMA2_Stream1, 5U };
		static constexpr DMAReq TX_DMA2_Stream6 = { DMA2_Stream6, 5U };
		static constexpr DMAReq RX_DMA2_Stream2 = { DMA2_Stream2, 5U };  // Stream2 Ch5
		static constexpr DMAReq TX_DMA2_Stream7 = { DMA2_Stream7, 5U };  // Stream7 Ch5
	};
#ifdef UART7_BASE
	struct Uart7 {
		static constexpr DMAReq TX_DMA1_Stream1 = { DMA1_Stream1, 5U };
		static constexpr DMAReq RX_DMA1_Stream3 = { DMA1_Stream3, 5U };
	};
#endif
#ifdef UART8_BASE
	struct Uart8 {
		static constexpr DMAReq TX_DMA1_Stream0 = { DMA1_Stream0, 5U };
		static constexpr DMAReq RX_DMA1_Stream6 = { DMA1_Stream6, 5U };
	};
#endif

	struct Spi1 {
		static constexpr DMAReq RX_DMA2_Stream0 = { DMA2_Stream0, 3U };
		static constexpr DMAReq TX_DMA2_Stream3 = { DMA2_Stream3, 3U };
		static constexpr DMAReq RX_DMA2_Stream2 = { DMA2_Stream2, 3U };  // Stream2 Ch3
		static constexpr DMAReq TX_DMA2_Stream5 = { DMA2_Stream5, 3U };  // Stream5 Ch3
	};
	struct Spi2 {
		static constexpr DMAReq RX_DMA1_Stream3 = { DMA1_Stream3, 0U };
		static constexpr DMAReq TX_DMA1_Stream4 = { DMA1_Stream4, 0U };
	};
	struct Spi3 {
		static constexpr DMAReq RX_DMA1_Stream0 = { DMA1_Stream0, 0U };
		static constexpr DMAReq TX_DMA1_Stream5 = { DMA1_Stream5, 0U };
		static constexpr DMAReq RX_DMA1_Stream2 = { DMA1_Stream2, 0U };  // Stream2 Ch0
		static constexpr DMAReq TX_DMA1_Stream7 = { DMA1_Stream7, 0U };  // Stream7 Ch0
	};

	struct I2c1 {
		static constexpr DMAReq RX_DMA1_Stream0 = { DMA1_Stream0, 1U };
		static constexpr DMAReq TX_DMA1_Stream6 = { DMA1_Stream6, 1U };
		static constexpr DMAReq RX_DMA1_Stream5 = { DMA1_Stream5, 1U };  // Stream5 Ch1
	};
	struct I2c2 {
		static constexpr DMAReq RX_DMA1_Stream2 = { DMA1_Stream2, 7U };
		static constexpr DMAReq TX_DMA1_Stream7 = { DMA1_Stream7, 7U };
		static constexpr DMAReq RX_DMA1_Stream3 = { DMA1_Stream3, 7U };  // Stream3 Ch7
	};
	struct I2c3 {
		static constexpr DMAReq RX_DMA1_Stream2 = { DMA1_Stream2, 1U };
		static constexpr DMAReq TX_DMA1_Stream4 = { DMA1_Stream4, 3U };
	};

	struct Adc1 {
		static constexpr DMAReq RX_DMA2_Stream0 = { DMA2_Stream0, 0U };
		static constexpr DMAReq RX_DMA2_Stream4 = { DMA2_Stream4, 0U };  // Stream4 Ch0
	};
	struct Adc2 {
		static constexpr DMAReq RX_DMA2_Stream2 = { DMA2_Stream2, 1U };
		static constexpr DMAReq RX_DMA2_Stream3 = { DMA2_Stream3, 1U };  // Stream3 Ch1
	};
	struct Adc3 {
		static constexpr DMAReq RX_DMA2_Stream0 = { DMA2_Stream0, 2U };
		static constexpr DMAReq RX_DMA2_Stream1 = { DMA2_Stream1, 2U };  // Stream1 Ch2
	};

#endif

}; // struct Req

#endif // DMA_REQUESTS_H_
