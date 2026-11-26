#pragma once

namespace dma_hw {

using Registers = DMA_Channel_TypeDef;
inline constexpr auto CONTROL = &Registers::CCR;
inline constexpr auto COUNT = &Registers::CNDTR;
inline constexpr auto PER_ADDRESS = &Registers::CPAR;
inline constexpr auto MEM_ADDRESS = &Registers::CMAR;

inline constexpr uint32_t EN = DMA_CCR_EN;
inline constexpr uint32_t MINC = DMA_CCR_MINC;
inline constexpr uint32_t PINC = DMA_CCR_PINC;
inline constexpr uint32_t CIRC = DMA_CCR_CIRC;
inline constexpr uint32_t MSIZE_MASK = DMA_CCR_MSIZE_Msk;
inline constexpr uint32_t MSIZE_POS = DMA_CCR_MSIZE_Pos;
inline constexpr uint32_t PSIZE_MASK = DMA_CCR_PSIZE_Msk;
inline constexpr uint32_t PSIZE_POS = DMA_CCR_PSIZE_Pos;
inline constexpr uint32_t IRQ_TC = DMA_CCR_TCIE;
inline constexpr uint32_t IRQ_HT = DMA_CCR_HTIE;
inline constexpr uint32_t IRQ_TE = DMA_CCR_TEIE;

inline constexpr uint32_t FLAG_HT = DMA_ISR_HTIF1;
inline constexpr uint32_t FLAG_TC = DMA_ISR_TCIF1;
inline constexpr uint32_t FLAG_ERRORS = DMA_ISR_TEIF1;
// Clear the individual flags. On affected parts CGIF can mask a simultaneous TE.
inline constexpr uint32_t CLEAR_FLAGS = FLAG_HT | FLAG_TC | FLAG_ERRORS;

inline constexpr uintptr_t STRIDE = DMA1_Channel2_BASE - DMA1_Channel1_BASE;
inline constexpr uintptr_t DMA1_FIRST = DMA1_Channel1_BASE;
#ifdef DMA2_Channel1
inline constexpr uintptr_t DMA2_FIRST = DMA2_Channel1_BASE;
#else
inline constexpr uintptr_t DMA2_FIRST = 0U;
#endif

} // namespace dma_hw
