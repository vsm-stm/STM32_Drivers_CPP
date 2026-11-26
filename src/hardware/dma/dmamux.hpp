#pragma once

namespace dma_hw {

constexpr bool ValidRequest(uint32_t request)
{
	return request != 0U && (request & ~DMAMUX_CxCR_DMAREQ_ID_Msk) == 0U;
}

inline void SetRequest(int32_t index, uint32_t request)
{
	// The descriptor index follows the DMAMUX slot order across DMA1 and DMA2.
	(DMAMUX1_Channel0 + index)->CCR = request;
}

inline void ClearRequest(int32_t index)
{
	SetRequest(index, 0U);
	DMAMUX1_ChannelStatus->CFR = 1U << index;
}

} // namespace dma_hw
