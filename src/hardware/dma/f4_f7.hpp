#pragma once

namespace dma_hw {

using Registers = DMA_Stream_TypeDef;
inline constexpr auto CONTROL = &Registers::CR;
inline constexpr auto COUNT = &Registers::NDTR;
inline constexpr auto PER_ADDRESS = &Registers::PAR;
inline constexpr auto MEM_ADDRESS = &Registers::M0AR;

inline constexpr uint32_t EN = DMA_SxCR_EN;
inline constexpr uint32_t MINC = DMA_SxCR_MINC;
inline constexpr uint32_t PINC = DMA_SxCR_PINC;
inline constexpr uint32_t CIRC = DMA_SxCR_CIRC;
inline constexpr uint32_t MSIZE_MASK = DMA_SxCR_MSIZE_Msk;
inline constexpr uint32_t MSIZE_POS = DMA_SxCR_MSIZE_Pos;
inline constexpr uint32_t PSIZE_MASK = DMA_SxCR_PSIZE_Msk;
inline constexpr uint32_t PSIZE_POS = DMA_SxCR_PSIZE_Pos;
inline constexpr uint32_t IRQ_TC = DMA_SxCR_TCIE;
inline constexpr uint32_t IRQ_HT = DMA_SxCR_HTIE;
inline constexpr uint32_t IRQ_TE = DMA_SxCR_TEIE;

inline constexpr uint32_t FLAG_HT = DMA_LISR_HTIF0;
inline constexpr uint32_t FLAG_TC = DMA_LISR_TCIF0;
inline constexpr uint32_t FLAG_ERRORS = DMA_LISR_TEIF0 | DMA_LISR_DMEIF0;
inline constexpr uint32_t CLEAR_FLAGS = FLAG_HT | FLAG_TC | FLAG_ERRORS | DMA_LIFCR_CFEIF0;

inline constexpr uintptr_t STRIDE = DMA1_Stream1_BASE - DMA1_Stream0_BASE;
inline constexpr uintptr_t DMA1_FIRST = DMA1_Stream0_BASE;
inline constexpr uintptr_t DMA2_FIRST = DMA2_Stream0_BASE;
inline constexpr int32_t DMA1_COUNT = 8;
inline constexpr int32_t DMA2_COUNT = 8;
inline constexpr bool MEM_TO_MEM = false;

// Each LISR/HISR has flag groups at offsets 0, 6, 16 and 22.
inline const dma_sets_typedef resources[] = {
	{ &DMA1->LISR, &DMA1->LIFCR,  0, DMA1_Stream0_IRQn },
	{ &DMA1->LISR, &DMA1->LIFCR,  6, DMA1_Stream1_IRQn },
	{ &DMA1->LISR, &DMA1->LIFCR, 16, DMA1_Stream2_IRQn },
	{ &DMA1->LISR, &DMA1->LIFCR, 22, DMA1_Stream3_IRQn },
	{ &DMA1->HISR, &DMA1->HIFCR,  0, DMA1_Stream4_IRQn },
	{ &DMA1->HISR, &DMA1->HIFCR,  6, DMA1_Stream5_IRQn },
	{ &DMA1->HISR, &DMA1->HIFCR, 16, DMA1_Stream6_IRQn },
	{ &DMA1->HISR, &DMA1->HIFCR, 22, DMA1_Stream7_IRQn },
	{ &DMA2->LISR, &DMA2->LIFCR,  0, DMA2_Stream0_IRQn },
	{ &DMA2->LISR, &DMA2->LIFCR,  6, DMA2_Stream1_IRQn },
	{ &DMA2->LISR, &DMA2->LIFCR, 16, DMA2_Stream2_IRQn },
	{ &DMA2->LISR, &DMA2->LIFCR, 22, DMA2_Stream3_IRQn },
	{ &DMA2->HISR, &DMA2->HIFCR,  0, DMA2_Stream4_IRQn },
	{ &DMA2->HISR, &DMA2->HIFCR,  6, DMA2_Stream5_IRQn },
	{ &DMA2->HISR, &DMA2->HIFCR, 16, DMA2_Stream6_IRQn },
	{ &DMA2->HISR, &DMA2->HIFCR, 22, DMA2_Stream7_IRQn },
};

constexpr bool ValidRequest(uint32_t request) { return request <= 7U; }

inline void EnableClock(int32_t index)
{
	DMA_SET_BIT(RCC->AHB1ENR, (index < DMA1_COUNT) ? RCC_AHB1ENR_DMA1EN : RCC_AHB1ENR_DMA2EN);
	(void)RCC->AHB1ENR;
}

} // namespace dma_hw
