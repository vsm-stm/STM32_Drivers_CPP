#pragma once

#include "channel.hpp"

namespace dma_hw {

#ifdef DMA1_Channel7
inline constexpr int32_t DMA1_COUNT = 7;
inline constexpr IRQn_Type SHARED_CHANNEL_IRQ = DMA1_Channel4_5_6_7_IRQn;
#else
inline constexpr int32_t DMA1_COUNT = 5;
inline constexpr IRQn_Type SHARED_CHANNEL_IRQ = DMA1_Channel4_5_IRQn;
#endif
inline constexpr int32_t DMA2_COUNT = 0;
inline constexpr bool MEM_TO_MEM = false;

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
};

constexpr bool ValidRequest(uint32_t request) { return request <= DMA_CSELR_C1S_Msk; }

inline void EnableClock(int32_t)
{
	DMA_SET_BIT(RCC->AHBENR, RCC_AHBENR_DMA1EN);
	(void)RCC->AHBENR;
}

inline void SetRequest(int32_t index, uint32_t request)
{
	const uint32_t shift = 4U * index;
	DMA_MODIFY_REG(DMA1_CSELR->CSELR, DMA_CSELR_C1S_Msk << shift, request << shift);
}

inline void ClearRequest(int32_t index) { SetRequest(index, 0U); }

} // namespace dma_hw
