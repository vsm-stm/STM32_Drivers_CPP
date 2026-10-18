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
 * USART consumes named requests directly and owns its DMA objects. Its
 * constructor checks the USART base and direction stored in these entries.
 * Other consumers retain their existing DMA_Sx object bindings and route checks.
 * CanUseRequest() compares a candidate against constructor selection; it neither
 * claims the channel nor proves its route is valid for a particular peripheral.
 * Consumers with external DMA objects select allowed entries before SetUp.
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
 *  - F4/F7: integer stream base + CHSEL (hardware-fixed per peripheral).
 *           Both fields are required; stream encodes which DMA/stream to use.
 *
 *  - G0:    DMAMUX1 request ID only (RM0444 Table 37).
 *           The DMA channel is chosen separately from those present on the MCU.
 * USART entries also carry periph_base and direction for constructor validation.
 * Zero periph_base means that the existing consumer validates its own route;
 * direction is ignored in that case. This metadata does not reserve hardware.
 */
struct DMAReq {
#if defined(STM32F4) || defined(STM32F7)
	uintptr_t          stream; ///< DMAx_Streamy_BASE; resolved when constructing DMA_Sx.
	uint32_t            ch;     ///< CHSEL field for DMA_SxCR (0–7)
	constexpr bool IsValid() const noexcept { return stream != 0U && ch <= 7U; }
#elif defined(STM32G0)
	uint32_t ch;                ///< DMAMUX1 request ID (0 = invalid / mem-to-mem)
	constexpr bool IsValid() const noexcept { return ch != 0 && (ch & ~DMAMUX_CxCR_DMAREQ_ID_Msk) == 0; }
#endif
	// USART metadata: zero owner means another consumer validates the route.
	uintptr_t periph_base;
	DIR direction;

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
	struct Adc1  { static constexpr DMAReq RX = {  5U, 0U, DIR::From_Per }; };

	struct I2c1  {	static constexpr DMAReq RX = { 10U, 0U, DIR::From_Per };
					static constexpr DMAReq TX = { 11U, 0U, DIR::From_Per }; };
	struct I2c2  {	static constexpr DMAReq RX = { 12U, 0U, DIR::From_Per };
					static constexpr DMAReq TX = { 13U, 0U, DIR::From_Per }; };

	struct Spi1  {	static constexpr DMAReq RX = { 16U, 0U, DIR::From_Per };
					static constexpr DMAReq TX = { 17U, 0U, DIR::From_Per }; };
	struct Spi2  {	static constexpr DMAReq RX = { 18U, 0U, DIR::From_Per };
					static constexpr DMAReq TX = { 19U, 0U, DIR::From_Per }; };

#ifdef SPI3_BASE
	struct Spi3 {
		static constexpr DMAReq RX = { 66U, 0U, DIR::From_Per };
		static constexpr DMAReq TX = { 67U, 0U, DIR::From_Per };
	};
#endif

	struct Tim1 {
					static constexpr DMAReq CH1  = { 20U, 0U, DIR::From_Per };
					static constexpr DMAReq CH2  = { 21U, 0U, DIR::From_Per };
					static constexpr DMAReq CH3  = { 22U, 0U, DIR::From_Per };
					static constexpr DMAReq CH4  = { 23U, 0U, DIR::From_Per };
					static constexpr DMAReq TRIG = { 24U, 0U, DIR::From_Per };
					static constexpr DMAReq UP   = { 25U, 0U, DIR::From_Per };
	};
	struct Tim3 {
					static constexpr DMAReq CH1  = { 32U, 0U, DIR::From_Per };
					static constexpr DMAReq CH2  = { 33U, 0U, DIR::From_Per };
					static constexpr DMAReq CH3  = { 34U, 0U, DIR::From_Per };
					static constexpr DMAReq CH4  = { 35U, 0U, DIR::From_Per };
					static constexpr DMAReq TRIG = { 36U, 0U, DIR::From_Per };
					static constexpr DMAReq UP   = { 37U, 0U, DIR::From_Per };
	};

#ifdef TIM2_BASE
	struct Tim2 {
		static constexpr DMAReq CH1 = { 26U, 0U, DIR::From_Per };
		static constexpr DMAReq CH2 = { 27U, 0U, DIR::From_Per };
		static constexpr DMAReq CH3 = { 28U, 0U, DIR::From_Per };
		static constexpr DMAReq CH4 = { 29U, 0U, DIR::From_Per };
	};
#endif
#ifdef TIM4_BASE
	struct Tim4 {
		static constexpr DMAReq CH1 = { 68U, 0U, DIR::From_Per };
		static constexpr DMAReq CH2 = { 69U, 0U, DIR::From_Per };
		static constexpr DMAReq CH3 = { 70U, 0U, DIR::From_Per };
		static constexpr DMAReq CH4 = { 71U, 0U, DIR::From_Per };
	};
#endif
#ifdef TIM15_BASE
	struct Tim15 {
		static constexpr DMAReq CH1 = { 40U, 0U, DIR::From_Per };
		static constexpr DMAReq CH2 = { 41U, 0U, DIR::From_Per };
	};
#endif

	struct Tim16 {	static constexpr DMAReq CH1 = { 44U, 0U, DIR::From_Per };
					static constexpr DMAReq COM = { 45U, 0U, DIR::From_Per };
					static constexpr DMAReq UP  = { 46U, 0U, DIR::From_Per }; };
	struct Tim17 {	static constexpr DMAReq CH1 = { 47U, 0U, DIR::From_Per };
					static constexpr DMAReq COM = { 48U, 0U, DIR::From_Per };
					static constexpr DMAReq UP  = { 49U, 0U, DIR::From_Per }; };

#ifdef USART1_BASE
	struct Usart1 {	static constexpr DMAReq RX = { 50U, USART1_BASE, DIR::From_Per };
					static constexpr DMAReq TX = { 51U, USART1_BASE, DIR::To_Per }; };
#endif
#ifdef USART2_BASE
	struct Usart2 { static constexpr DMAReq RX = { 52U, USART2_BASE, DIR::From_Per };
					static constexpr DMAReq TX = { 53U, USART2_BASE, DIR::To_Per }; };
#endif
#ifdef USART3_BASE
	struct Usart3 { static constexpr DMAReq RX = { 54U, USART3_BASE, DIR::From_Per };
					static constexpr DMAReq TX = { 55U, USART3_BASE, DIR::To_Per }; };
#endif
#ifdef USART4_BASE
	struct Usart4 { static constexpr DMAReq RX = { 56U, USART4_BASE, DIR::From_Per };
					static constexpr DMAReq TX = { 57U, USART4_BASE, DIR::To_Per }; };
#endif
#ifdef USART5_BASE
	struct Usart5 { static constexpr DMAReq RX = { 74U, USART5_BASE, DIR::From_Per };
					static constexpr DMAReq TX = { 75U, USART5_BASE, DIR::To_Per }; };
#endif
#ifdef USART6_BASE
	struct Usart6 { static constexpr DMAReq RX = { 76U, USART6_BASE, DIR::From_Per };
					static constexpr DMAReq TX = { 77U, USART6_BASE, DIR::To_Per }; };
#endif

#elif defined(STM32F4) || defined(STM32F7)

	// Dedicated timer CC routes; combined CH1/CH2/CH3 routes are omitted.
#ifdef TIM1_BASE
	struct Tim1 {
		static constexpr DMAReq CH1_DMA2_Stream1 = { DMA2_Stream1_BASE, 6U, 0U, DIR::From_Per };
		static constexpr DMAReq CH1_DMA2_Stream3 = { DMA2_Stream3_BASE, 6U, 0U, DIR::From_Per };
		static constexpr DMAReq CH2_DMA2_Stream2 = { DMA2_Stream2_BASE, 6U, 0U, DIR::From_Per };
		static constexpr DMAReq CH3_DMA2_Stream6 = { DMA2_Stream6_BASE, 6U, 0U, DIR::From_Per };
		static constexpr DMAReq CH4_DMA2_Stream4 = { DMA2_Stream4_BASE, 6U, 0U, DIR::From_Per };
	};
#endif
#ifdef TIM2_BASE
	struct Tim2 {
		static constexpr DMAReq CH1_DMA1_Stream5 = { DMA1_Stream5_BASE, 3U, 0U, DIR::From_Per };
		static constexpr DMAReq CH2_DMA1_Stream6 = { DMA1_Stream6_BASE, 3U, 0U, DIR::From_Per };
		static constexpr DMAReq CH3_DMA1_Stream1 = { DMA1_Stream1_BASE, 3U, 0U, DIR::From_Per };
		static constexpr DMAReq CH4_DMA1_Stream7 = { DMA1_Stream7_BASE, 3U, 0U, DIR::From_Per };
	};
#endif
#ifdef TIM3_BASE
	struct Tim3 {
		static constexpr DMAReq CH1_DMA1_Stream4 = { DMA1_Stream4_BASE, 5U, 0U, DIR::From_Per };
		static constexpr DMAReq CH2_DMA1_Stream5 = { DMA1_Stream5_BASE, 5U, 0U, DIR::From_Per };
		static constexpr DMAReq CH3_DMA1_Stream7 = { DMA1_Stream7_BASE, 5U, 0U, DIR::From_Per };
		static constexpr DMAReq CH4_DMA1_Stream2 = { DMA1_Stream2_BASE, 5U, 0U, DIR::From_Per };
	};
#endif
#ifdef TIM4_BASE
	struct Tim4 {
		static constexpr DMAReq CH1_DMA1_Stream0 = { DMA1_Stream0_BASE, 2U, 0U, DIR::From_Per };
		static constexpr DMAReq CH2_DMA1_Stream3 = { DMA1_Stream3_BASE, 2U, 0U, DIR::From_Per };
		static constexpr DMAReq CH3_DMA1_Stream7 = { DMA1_Stream7_BASE, 2U, 0U, DIR::From_Per };
	};
#endif
#ifdef TIM5_BASE
	struct Tim5 {
		static constexpr DMAReq CH1_DMA1_Stream2 = { DMA1_Stream2_BASE, 6U, 0U, DIR::From_Per };
		static constexpr DMAReq CH2_DMA1_Stream4 = { DMA1_Stream4_BASE, 6U, 0U, DIR::From_Per };
		static constexpr DMAReq CH3_DMA1_Stream0 = { DMA1_Stream0_BASE, 6U, 0U, DIR::From_Per };
		static constexpr DMAReq CH4_DMA1_Stream1 = { DMA1_Stream1_BASE, 6U, 0U, DIR::From_Per };
		static constexpr DMAReq CH4_DMA1_Stream3 = { DMA1_Stream3_BASE, 6U, 0U, DIR::From_Per };
	};
#endif
#ifdef TIM8_BASE
	struct Tim8 {
		static constexpr DMAReq CH1_DMA2_Stream2 = { DMA2_Stream2_BASE, 7U, 0U, DIR::From_Per };
		static constexpr DMAReq CH2_DMA2_Stream3 = { DMA2_Stream3_BASE, 7U, 0U, DIR::From_Per };
		static constexpr DMAReq CH3_DMA2_Stream4 = { DMA2_Stream4_BASE, 7U, 0U, DIR::From_Per };
		static constexpr DMAReq CH4_DMA2_Stream7 = { DMA2_Stream7_BASE, 7U, 0U, DIR::From_Per };
	};
#endif

	// Stream + CHSEL values for STM32F4 (RM0090 Table 42) / F7 (RM0385 Table 8).
	// Each route name shows its DMA controller and stream, including alternatives.
#ifdef USART1_BASE
	struct Usart1 {
		static constexpr DMAReq RX_DMA2_Stream2 = { DMA2_Stream2_BASE, 4U, USART1_BASE, DIR::From_Per };
		static constexpr DMAReq TX_DMA2_Stream7 = { DMA2_Stream7_BASE, 4U, USART1_BASE, DIR::To_Per };
		static constexpr DMAReq RX_DMA2_Stream5 = { DMA2_Stream5_BASE, 4U, USART1_BASE, DIR::From_Per };  // USART1_RX, not TX.
	};
#endif
#ifdef USART2_BASE
	struct Usart2 {
		static constexpr DMAReq RX_DMA1_Stream5 = { DMA1_Stream5_BASE, 4U, USART2_BASE, DIR::From_Per };
		static constexpr DMAReq TX_DMA1_Stream6 = { DMA1_Stream6_BASE, 4U, USART2_BASE, DIR::To_Per };
	};
#endif
#ifdef USART3_BASE
	struct Usart3 {
		static constexpr DMAReq RX_DMA1_Stream1 = { DMA1_Stream1_BASE, 4U, USART3_BASE, DIR::From_Per };
		static constexpr DMAReq TX_DMA1_Stream3 = { DMA1_Stream3_BASE, 4U, USART3_BASE, DIR::To_Per };
		static constexpr DMAReq TX_DMA1_Stream4 = { DMA1_Stream4_BASE, 7U, USART3_BASE, DIR::To_Per };  // Stream4 Ch7
	};
#endif
#ifdef UART4_BASE
	struct Uart4 {
		static constexpr DMAReq RX_DMA1_Stream2 = { DMA1_Stream2_BASE, 4U, UART4_BASE, DIR::From_Per };
		static constexpr DMAReq TX_DMA1_Stream4 = { DMA1_Stream4_BASE, 4U, UART4_BASE, DIR::To_Per };
	};
#endif
#ifdef UART5_BASE
	struct Uart5 {
		static constexpr DMAReq RX_DMA1_Stream0 = { DMA1_Stream0_BASE, 4U, UART5_BASE, DIR::From_Per };
		static constexpr DMAReq TX_DMA1_Stream7 = { DMA1_Stream7_BASE, 4U, UART5_BASE, DIR::To_Per };
	};
#endif
#ifdef USART6_BASE
	struct Usart6 {
		static constexpr DMAReq RX_DMA2_Stream1 = { DMA2_Stream1_BASE, 5U, USART6_BASE, DIR::From_Per };
		static constexpr DMAReq TX_DMA2_Stream6 = { DMA2_Stream6_BASE, 5U, USART6_BASE, DIR::To_Per };
		static constexpr DMAReq RX_DMA2_Stream2 = { DMA2_Stream2_BASE, 5U, USART6_BASE, DIR::From_Per };  // Stream2 Ch5
		static constexpr DMAReq TX_DMA2_Stream7 = { DMA2_Stream7_BASE, 5U, USART6_BASE, DIR::To_Per };  // Stream7 Ch5
	};
#endif
#ifdef UART7_BASE
	struct Uart7 {
		static constexpr DMAReq TX_DMA1_Stream1 = { DMA1_Stream1_BASE, 5U, UART7_BASE, DIR::To_Per };
		static constexpr DMAReq RX_DMA1_Stream3 = { DMA1_Stream3_BASE, 5U, UART7_BASE, DIR::From_Per };
	};
#endif
#ifdef UART8_BASE
	struct Uart8 {
		static constexpr DMAReq TX_DMA1_Stream0 = { DMA1_Stream0_BASE, 5U, UART8_BASE, DIR::To_Per };
		static constexpr DMAReq RX_DMA1_Stream6 = { DMA1_Stream6_BASE, 5U, UART8_BASE, DIR::From_Per };
	};
#endif

	struct Spi1 {
		static constexpr DMAReq RX_DMA2_Stream0 = { DMA2_Stream0_BASE, 3U, 0U, DIR::From_Per };
		static constexpr DMAReq TX_DMA2_Stream3 = { DMA2_Stream3_BASE, 3U, 0U, DIR::From_Per };
		static constexpr DMAReq RX_DMA2_Stream2 = { DMA2_Stream2_BASE, 3U, 0U, DIR::From_Per };  // Stream2 Ch3
		static constexpr DMAReq TX_DMA2_Stream5 = { DMA2_Stream5_BASE, 3U, 0U, DIR::From_Per };  // Stream5 Ch3
	};
	struct Spi2 {
		static constexpr DMAReq RX_DMA1_Stream3 = { DMA1_Stream3_BASE, 0U, 0U, DIR::From_Per };
		static constexpr DMAReq TX_DMA1_Stream4 = { DMA1_Stream4_BASE, 0U, 0U, DIR::From_Per };
	};
	struct Spi3 {
		static constexpr DMAReq RX_DMA1_Stream0 = { DMA1_Stream0_BASE, 0U, 0U, DIR::From_Per };
		static constexpr DMAReq TX_DMA1_Stream5 = { DMA1_Stream5_BASE, 0U, 0U, DIR::From_Per };
		static constexpr DMAReq RX_DMA1_Stream2 = { DMA1_Stream2_BASE, 0U, 0U, DIR::From_Per };  // Stream2 Ch0
		static constexpr DMAReq TX_DMA1_Stream7 = { DMA1_Stream7_BASE, 0U, 0U, DIR::From_Per };  // Stream7 Ch0
	};

#ifdef SPI4_BASE
    struct Spi4 {
        static constexpr DMAReq RX_DMA2_Stream0 = { DMA2_Stream0_BASE, 4U, 0U, DIR::From_Per };
        static constexpr DMAReq RX_DMA2_Stream3 = { DMA2_Stream3_BASE, 5U, 0U, DIR::From_Per };
        static constexpr DMAReq TX_DMA2_Stream1 = { DMA2_Stream1_BASE, 4U, 0U, DIR::From_Per };
        static constexpr DMAReq TX_DMA2_Stream4 = { DMA2_Stream4_BASE, 5U, 0U, DIR::From_Per };
    };
#endif
#ifdef SPI5_BASE
    struct Spi5 {
        static constexpr DMAReq RX_DMA2_Stream3 = { DMA2_Stream3_BASE, 2U, 0U, DIR::From_Per };
        static constexpr DMAReq RX_DMA2_Stream5 = { DMA2_Stream5_BASE, 7U, 0U, DIR::From_Per };
        static constexpr DMAReq TX_DMA2_Stream4 = { DMA2_Stream4_BASE, 2U, 0U, DIR::From_Per };
        static constexpr DMAReq TX_DMA2_Stream6 = { DMA2_Stream6_BASE, 7U, 0U, DIR::From_Per };
    };
#endif
#ifdef SPI6_BASE
    struct Spi6 {
        static constexpr DMAReq RX_DMA2_Stream6 = { DMA2_Stream6_BASE, 1U, 0U, DIR::From_Per };
        static constexpr DMAReq TX_DMA2_Stream5 = { DMA2_Stream5_BASE, 1U, 0U, DIR::From_Per };
    };
#endif

	struct I2c1 {
		static constexpr DMAReq RX_DMA1_Stream0 = { DMA1_Stream0_BASE, 1U, 0U, DIR::From_Per };
		static constexpr DMAReq TX_DMA1_Stream6 = { DMA1_Stream6_BASE, 1U, 0U, DIR::From_Per };
		static constexpr DMAReq RX_DMA1_Stream5 = { DMA1_Stream5_BASE, 1U, 0U, DIR::From_Per };  // Stream5 Ch1
	};
	struct I2c2 {
		static constexpr DMAReq RX_DMA1_Stream2 = { DMA1_Stream2_BASE, 7U, 0U, DIR::From_Per };
		static constexpr DMAReq TX_DMA1_Stream7 = { DMA1_Stream7_BASE, 7U, 0U, DIR::From_Per };
		static constexpr DMAReq RX_DMA1_Stream3 = { DMA1_Stream3_BASE, 7U, 0U, DIR::From_Per };  // Stream3 Ch7
	};
	struct I2c3 {
		static constexpr DMAReq RX_DMA1_Stream2 = { DMA1_Stream2_BASE, 1U, 0U, DIR::From_Per };
		static constexpr DMAReq TX_DMA1_Stream4 = { DMA1_Stream4_BASE, 3U, 0U, DIR::From_Per };
	};

	struct Adc1 {
		static constexpr DMAReq RX_DMA2_Stream0 = { DMA2_Stream0_BASE, 0U, 0U, DIR::From_Per };
		static constexpr DMAReq RX_DMA2_Stream4 = { DMA2_Stream4_BASE, 0U, 0U, DIR::From_Per };  // Stream4 Ch0
	};
	struct Adc2 {
		static constexpr DMAReq RX_DMA2_Stream2 = { DMA2_Stream2_BASE, 1U, 0U, DIR::From_Per };
		static constexpr DMAReq RX_DMA2_Stream3 = { DMA2_Stream3_BASE, 1U, 0U, DIR::From_Per };  // Stream3 Ch1
	};
	struct Adc3 {
		static constexpr DMAReq RX_DMA2_Stream0 = { DMA2_Stream0_BASE, 2U, 0U, DIR::From_Per };
		static constexpr DMAReq RX_DMA2_Stream1 = { DMA2_Stream1_BASE, 2U, 0U, DIR::From_Per };  // Stream1 Ch2
	};

#endif

}; // struct Req

#endif // DMA_REQUESTS_H_
