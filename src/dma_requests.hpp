/**
 * @file dma_requests.hpp
 * @brief Common DMA request value; included inside DMA_Sx's public section.
 * Named routes live once in USART/SPI/I2C/ADC/TIM::_N::DMA in their defs headers.
 * F4/F7 select a stream and CHSEL. G0 describes a DMAMUX request; peripheral
 * constructors take a numeric physical channel and derive its request internally.
 */
#ifndef DMA_REQUESTS_H_
#define DMA_REQUESTS_H_
struct DMAReq {
#if defined(STM32F4) || defined(STM32F7)
	uintptr_t          stream; ///< DMAx_Streamy_BASE; resolved when constructing DMA_Sx.
	uint32_t            ch;     ///< CHSEL field for DMA_SxCR (0..7)
	constexpr bool IsValid() const noexcept { return stream != 0U && ch <= 7U; }
#elif defined(STM32G0)
	uint32_t ch;                ///< DMAMUX1 request ID (0 = invalid / mem-to-mem)
	constexpr bool IsValid() const noexcept { return ch != 0 && (ch & ~DMAMUX_CxCR_DMAREQ_ID_Msk) == 0; }
#endif
	// Peripheral metadata: zero owner means the consumer validates its own route.
	uintptr_t periph_base;
	DIR direction;

};

#endif // DMA_REQUESTS_H_
