#pragma once

#include "channel.hpp"
#include "dmamux.hpp"

namespace dma_hw {

#ifdef DMA1_Channel8
inline constexpr int32_t DMA1_COUNT = 8;
inline constexpr int32_t DMA2_COUNT = 8;
#else
inline constexpr int32_t DMA1_COUNT = 6;
inline constexpr int32_t DMA2_COUNT = 6;
#endif
inline constexpr bool MEM_TO_MEM = false;

// DMA2 starts at DMAMUX slot 6 or 8, depending on the device.
inline const dma_sets_typedef resources[] = {
	{ &DMA1->ISR, &DMA1->IFCR,  0, DMA1_Channel1_IRQn },
	{ &DMA1->ISR, &DMA1->IFCR,  4, DMA1_Channel2_IRQn },
	{ &DMA1->ISR, &DMA1->IFCR,  8, DMA1_Channel3_IRQn },
	{ &DMA1->ISR, &DMA1->IFCR, 12, DMA1_Channel4_IRQn },
	{ &DMA1->ISR, &DMA1->IFCR, 16, DMA1_Channel5_IRQn },
	{ &DMA1->ISR, &DMA1->IFCR, 20, DMA1_Channel6_IRQn },
#ifdef DMA1_Channel8
	{ &DMA1->ISR, &DMA1->IFCR, 24, DMA1_Channel7_IRQn },
	{ &DMA1->ISR, &DMA1->IFCR, 28, DMA1_Channel8_IRQn },
#endif
	{ &DMA2->ISR, &DMA2->IFCR,  0, DMA2_Channel1_IRQn },
	{ &DMA2->ISR, &DMA2->IFCR,  4, DMA2_Channel2_IRQn },
	{ &DMA2->ISR, &DMA2->IFCR,  8, DMA2_Channel3_IRQn },
	{ &DMA2->ISR, &DMA2->IFCR, 12, DMA2_Channel4_IRQn },
	{ &DMA2->ISR, &DMA2->IFCR, 16, DMA2_Channel5_IRQn },
	{ &DMA2->ISR, &DMA2->IFCR, 20, DMA2_Channel6_IRQn },
#ifdef DMA2_Channel8
	{ &DMA2->ISR, &DMA2->IFCR, 24, DMA2_Channel7_IRQn },
	{ &DMA2->ISR, &DMA2->IFCR, 28, DMA2_Channel8_IRQn },
#endif
};

inline void EnableClock(int32_t index)
{
	const uint32_t dma_clock = (index < DMA1_COUNT) ? RCC_AHB1ENR_DMA1EN : RCC_AHB1ENR_DMA2EN;
	DMA_SET_BIT(RCC->AHB1ENR, dma_clock | RCC_AHB1ENR_DMAMUX1EN);
	(void)RCC->AHB1ENR;
}

} // namespace dma_hw
