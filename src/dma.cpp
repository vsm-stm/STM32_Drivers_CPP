#include "dma.hpp"

using namespace DMA_Sx_ns;

// ---------------------------------------------------------------------------
// Static descriptor table — one entry per hardware stream/channel.
//
// F4/F7: 16 entries (DMA1 streams 0-7 + DMA2 streams 0-7).
//   Flag offset within LISR/HISR: bits 15:12 are reserved, so the pattern
//   is 0, 6, 16, 22 (not evenly spaced).
//
// G0: 5 entries (DMA1 channels 1-5 on G030/G031/G041).
//   Channels 2-3 share one NVIC line; channels 4-7 share another.
//   Flag offset = (channel_number - 1) * 4.
// ---------------------------------------------------------------------------

#if defined(STM32F4) || defined(STM32F7)

static dma_sets_typedef dma_streams[16] = {
	{ DMA1, &DMA1->LISR, &DMA1->LIFCR,  0, DMA1_Stream0_IRQn, false },
	{ DMA1, &DMA1->LISR, &DMA1->LIFCR,  6, DMA1_Stream1_IRQn, false },
	{ DMA1, &DMA1->LISR, &DMA1->LIFCR, 16, DMA1_Stream2_IRQn, false },
	{ DMA1, &DMA1->LISR, &DMA1->LIFCR, 22, DMA1_Stream3_IRQn, false },
	{ DMA1, &DMA1->HISR, &DMA1->HIFCR,  0, DMA1_Stream4_IRQn, false },
	{ DMA1, &DMA1->HISR, &DMA1->HIFCR,  6, DMA1_Stream5_IRQn, false },
	{ DMA1, &DMA1->HISR, &DMA1->HIFCR, 16, DMA1_Stream6_IRQn, false },
	{ DMA1, &DMA1->HISR, &DMA1->HIFCR, 22, DMA1_Stream7_IRQn, false },
	{ DMA2, &DMA2->LISR, &DMA2->LIFCR,  0, DMA2_Stream0_IRQn, false },
	{ DMA2, &DMA2->LISR, &DMA2->LIFCR,  6, DMA2_Stream1_IRQn, false },
	{ DMA2, &DMA2->LISR, &DMA2->LIFCR, 16, DMA2_Stream2_IRQn, false },
	{ DMA2, &DMA2->LISR, &DMA2->LIFCR, 22, DMA2_Stream3_IRQn, false },
	{ DMA2, &DMA2->HISR, &DMA2->HIFCR,  0, DMA2_Stream4_IRQn, false },
	{ DMA2, &DMA2->HISR, &DMA2->HIFCR,  6, DMA2_Stream5_IRQn, false },
	{ DMA2, &DMA2->HISR, &DMA2->HIFCR, 16, DMA2_Stream6_IRQn, false },
	{ DMA2, &DMA2->HISR, &DMA2->HIFCR, 22, DMA2_Stream7_IRQn, false },
};

#elif defined(STM32G0)

static dma_sets_typedef dma_streams[] = {
	{ DMA1, &DMA1->ISR, &DMA1->IFCR,  0, DMA1_Channel1_IRQn,           false },
	{ DMA1, &DMA1->ISR, &DMA1->IFCR,  4, DMA1_Channel2_3_IRQn,         false },
	{ DMA1, &DMA1->ISR, &DMA1->IFCR,  8, DMA1_Channel2_3_IRQn,         false },
	{ DMA1, &DMA1->ISR, &DMA1->IFCR, 12, DMA1_Ch4_5_DMAMUX1_OVR_IRQn, false },
	{ DMA1, &DMA1->ISR, &DMA1->IFCR, 16, DMA1_Ch4_5_DMAMUX1_OVR_IRQn, false },
};

#endif

static constexpr int32_t kStreamCount =
	(int32_t)(sizeof(dma_streams) / sizeof(dma_streams[0]));

// ---------------------------------------------------------------------------

int32_t DMA_Sx::GetStreamIndex(void* s)
{
	uintptr_t base = reinterpret_cast<uintptr_t>(s);

#if defined(STM32F4) || defined(STM32F7)
	uintptr_t dma1_base = reinterpret_cast<uintptr_t>(DMA1_Stream0);
	uintptr_t dma1_end  = reinterpret_cast<uintptr_t>(DMA1_Stream7);
	uintptr_t dma2_base = reinterpret_cast<uintptr_t>(DMA2_Stream0);
	uintptr_t dma2_end  = reinterpret_cast<uintptr_t>(DMA2_Stream7);

	if (base >= dma1_base && base <= dma1_end)
		return (base - dma1_base) / 0x18;
	if (base >= dma2_base && base <= dma2_end)
		return 8 + (base - dma2_base) / 0x18;

#elif defined(STM32G0)
	uintptr_t ch1_base = reinterpret_cast<uintptr_t>(DMA1_Channel1);

	if (base >= ch1_base && base < ch1_base + kStreamCount * 0x14)
		return (base - ch1_base) / 0x14;
#endif

	return -1;
}

// ---------------------------------------------------------------------------

SysInitStatus DMA_Sx::SetUp(const StreamSettings& settings)
{
	int32_t idx = GetStreamIndex(_stream);
	if (idx < 0 || idx >= kStreamCount)
		return SysInitStatus::InitError;

#if defined(STM32F4) || defined(STM32F7)
	if (settings.channel > 7)
		return SysInitStatus::InitError;
#endif

	dma_sets_typedef& info = dma_streams[idx];
	if (info.used)
		return SysInitStatus::InitError;

	info.used = true;
	_info = &info;

	// Enable DMA clock
#if defined(STM32F4) || defined(STM32F7)
	RCC->AHB1ENR |= (_info->ctrl == DMA1) ? RCC_AHB1ENR_DMA1EN : RCC_AHB1ENR_DMA2EN;
#elif defined(STM32G0)
	RCC->AHBENR |= RCC_AHBENR_DMA1EN;  // also enables DMAMUX1 on G0
#endif

	// Disable stream and wait for hardware to clear EN
	_CR() &= ~CR_EN;
	while (_CR() & CR_EN) {}
	ClearFlags();

	// Effective channel/request: preset from DMAReq constructor takes priority.
	const uint32_t eff_ch = _has_preset ? _preset_ch : settings.channel;

	// Build control register value
	uint32_t cr = 0;

#if defined(STM32F4) || defined(STM32F7)
	cr |= eff_ch                                    << DMA_SxCR_CHSEL_Pos;
	cr |= static_cast<uint32_t>(settings.direction) << DMA_SxCR_DIR_Pos;
	cr |= static_cast<uint32_t>(settings.data_size) << DMA_SxCR_PSIZE_Pos;
	cr |= static_cast<uint32_t>(settings.data_size) << DMA_SxCR_MSIZE_Pos;
	cr |= settings.minc                             << DMA_SxCR_MINC_Pos;
	cr |= settings.pinc                             << DMA_SxCR_PINC_Pos;
	cr |= settings.circ                             << DMA_SxCR_CIRC_Pos;

#elif defined(STM32G0)
	// G0 direction: DIR (1 bit) + separate MEM2MEM bit
	switch (settings.direction) {
		case DIR::From_Per:                              break;
		case DIR::To_Per:     cr |= DMA_CCR_DIR;        break;
		case DIR::Mem_To_Mem: cr |= DMA_CCR_MEM2MEM;    break;
	}
	cr |= static_cast<uint32_t>(settings.data_size) << DMA_CCR_PSIZE_Pos;
	cr |= static_cast<uint32_t>(settings.data_size) << DMA_CCR_MSIZE_Pos;
	cr |= settings.minc << DMA_CCR_MINC_Pos;
	cr |= settings.pinc << DMA_CCR_PINC_Pos;
	cr |= settings.circ << DMA_CCR_CIRC_Pos;

	// Map peripheral request via DMAMUX1 (channel index matches DMA channel index)
	(DMAMUX1_Channel0 + idx)->CCR = eff_ch & DMAMUX_CxCR_DMAREQ_ID;
#endif

	_CR()   = cr;
	_PAR()  = settings.per_address;
	_MAR()  = settings.mem_address;
	_NDTR() = settings.count;

	return SysInitStatus::InitOK;
}
