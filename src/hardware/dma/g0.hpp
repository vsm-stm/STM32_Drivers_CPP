#pragma once

#include "channel.hpp"
#include "dmamux.hpp"

namespace dma_hw {

#ifdef DMA1_Channel7
inline constexpr int32_t DMA1_COUNT = 7;
#else
inline constexpr int32_t DMA1_COUNT = 5;
#endif
#ifdef DMA2_Channel1
inline constexpr int32_t DMA2_COUNT = 5;
inline constexpr IRQn_Type SHARED_CHANNEL_IRQ = DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX1_OVR_IRQn;
#elif defined(DMA1_Channel7)
inline constexpr int32_t DMA2_COUNT = 0;
inline constexpr IRQn_Type SHARED_CHANNEL_IRQ = DMA1_Ch4_7_DMAMUX1_OVR_IRQn;
#else
inline constexpr int32_t DMA2_COUNT = 0;
inline constexpr IRQn_Type SHARED_CHANNEL_IRQ = DMA1_Ch4_5_DMAMUX1_OVR_IRQn;
#endif
inline constexpr bool MEM_TO_MEM = true;

inline const dma_sets_typedef resources[] = {
	{ &DMA1->ISR, &DMA1->IFCR,  0, DMA1_Channel1_IRQn },
	{ &DMA1->ISR, &DMA1->IFCR,  4, DMA1_Channel2_3_IRQn },
	{ &DMA1->ISR, &DMA1->IFCR,  8, DMA1_Channel2_3_IRQn },
	{ &DMA1->ISR, &DMA1->IFCR, 12, SHARED_CHANNEL_IRQ },
	{ &DMA1->ISR, &DMA1->IFCR, 16, SHARED_CHANNEL_IRQ },
#ifdef DMA1_Channel7
	{ &DMA1->ISR, &DMA1->IFCR, 20, SHARED_CHANNEL_IRQ },
	{ &DMA1->ISR, &DMA1->IFCR, 24, SHARED_CHANNEL_IRQ },
#endif
#ifdef DMA2_Channel1
	{ &DMA2->ISR, &DMA2->IFCR,  0, SHARED_CHANNEL_IRQ },
	{ &DMA2->ISR, &DMA2->IFCR,  4, SHARED_CHANNEL_IRQ },
	{ &DMA2->ISR, &DMA2->IFCR,  8, SHARED_CHANNEL_IRQ },
	{ &DMA2->ISR, &DMA2->IFCR, 12, SHARED_CHANNEL_IRQ },
	{ &DMA2->ISR, &DMA2->IFCR, 16, SHARED_CHANNEL_IRQ },
#endif
};

inline void EnableClock([[maybe_unused]] int32_t index)
{
#ifdef DMA2_Channel1
	DMA_SET_BIT(RCC->AHBENR, (index < DMA1_COUNT) ? RCC_AHBENR_DMA1EN : RCC_AHBENR_DMA2EN);
#else
	DMA_SET_BIT(RCC->AHBENR, RCC_AHBENR_DMA1EN);
#endif
	(void)RCC->AHBENR; // Either DMA clock also enables DMAMUX1.
}

} // namespace dma_hw
