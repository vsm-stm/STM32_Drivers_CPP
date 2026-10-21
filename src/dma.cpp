/**
 * @file dma.cpp
 * @brief Exact stream/channel lookup, one-owner setup and full resource release.
 *
 * The dma_used array prevents duplicate hardware ownership, not concurrent
 * access synchronization. Setup/release are serialized main-loop operations.
 * G0 DMA2 maps to its own ISR/IFCR and DMAMUX slots 7..11, not DMA1 registers.
 * Consumer-specific peripheral routing and cache handling remain outside DMA.
 */
#include "dma.hpp"

// ---------------------------------------------------------------------------
// Read-only descriptor table in Flash — one entry per hardware stream/channel.
// Mutable ownership is kept separately in dma_used; both arrays use the same index.
//
// F4/F7: 16 entries (DMA1 streams 0-7 + DMA2 streams 0-7).
//   Flag offset within LISR/HISR: bits 15:12 are reserved, so the pattern
//   is 0, 6, 16, 22 (not evenly spaced).
//
// G0: DMA1 channels present in the device header, plus DMA2 when present.
//   Channels 2-3 share one NVIC line; channels 4-7 share another.
//   Flag offset = (channel_number - 1) * 4.
// ---------------------------------------------------------------------------

#if defined(STM32F4) || defined(STM32F7)

static const dma_sets_typedef dma_streams[16] = {
	{ DMA1, &DMA1->LISR, &DMA1->LIFCR,  0, DMA1_Stream0_IRQn },
	{ DMA1, &DMA1->LISR, &DMA1->LIFCR,  6, DMA1_Stream1_IRQn },
	{ DMA1, &DMA1->LISR, &DMA1->LIFCR, 16, DMA1_Stream2_IRQn },
	{ DMA1, &DMA1->LISR, &DMA1->LIFCR, 22, DMA1_Stream3_IRQn },
	{ DMA1, &DMA1->HISR, &DMA1->HIFCR,  0, DMA1_Stream4_IRQn },
	{ DMA1, &DMA1->HISR, &DMA1->HIFCR,  6, DMA1_Stream5_IRQn },
	{ DMA1, &DMA1->HISR, &DMA1->HIFCR, 16, DMA1_Stream6_IRQn },
	{ DMA1, &DMA1->HISR, &DMA1->HIFCR, 22, DMA1_Stream7_IRQn },
	{ DMA2, &DMA2->LISR, &DMA2->LIFCR,  0, DMA2_Stream0_IRQn },
	{ DMA2, &DMA2->LISR, &DMA2->LIFCR,  6, DMA2_Stream1_IRQn },
	{ DMA2, &DMA2->LISR, &DMA2->LIFCR, 16, DMA2_Stream2_IRQn },
	{ DMA2, &DMA2->LISR, &DMA2->LIFCR, 22, DMA2_Stream3_IRQn },
	{ DMA2, &DMA2->HISR, &DMA2->HIFCR,  0, DMA2_Stream4_IRQn },
	{ DMA2, &DMA2->HISR, &DMA2->HIFCR,  6, DMA2_Stream5_IRQn },
	{ DMA2, &DMA2->HISR, &DMA2->HIFCR, 16, DMA2_Stream6_IRQn },
	{ DMA2, &DMA2->HISR, &DMA2->HIFCR, 22, DMA2_Stream7_IRQn },
};

#elif defined(STM32G0)

#if defined(DMA2_Channel1)
static constexpr IRQn_Type kSharedDMA_IRQn = DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX1_OVR_IRQn;
#elif defined(DMA1_Channel7)
static constexpr IRQn_Type kSharedDMA_IRQn = DMA1_Ch4_7_DMAMUX1_OVR_IRQn;
#else
static constexpr IRQn_Type kSharedDMA_IRQn = DMA1_Ch4_5_DMAMUX1_OVR_IRQn;
#endif

static const dma_sets_typedef dma_streams[] = {
	{ DMA1, &DMA1->ISR, &DMA1->IFCR,  0, DMA1_Channel1_IRQn },
	{ DMA1, &DMA1->ISR, &DMA1->IFCR,  4, DMA1_Channel2_3_IRQn },
	{ DMA1, &DMA1->ISR, &DMA1->IFCR,  8, DMA1_Channel2_3_IRQn },
	{ DMA1, &DMA1->ISR, &DMA1->IFCR, 12, kSharedDMA_IRQn },
	{ DMA1, &DMA1->ISR, &DMA1->IFCR, 16, kSharedDMA_IRQn },
#if defined(DMA1_Channel7)
	{ DMA1, &DMA1->ISR, &DMA1->IFCR, 20, kSharedDMA_IRQn },
	{ DMA1, &DMA1->ISR, &DMA1->IFCR, 24, kSharedDMA_IRQn },
#endif
#if defined(DMA2_Channel1)
	{ DMA2, &DMA2->ISR, &DMA2->IFCR,  0, kSharedDMA_IRQn },
	{ DMA2, &DMA2->ISR, &DMA2->IFCR,  4, kSharedDMA_IRQn },
	{ DMA2, &DMA2->ISR, &DMA2->IFCR,  8, kSharedDMA_IRQn },
	{ DMA2, &DMA2->ISR, &DMA2->IFCR, 12, kSharedDMA_IRQn },
	{ DMA2, &DMA2->ISR, &DMA2->IFCR, 16, kSharedDMA_IRQn },
#endif
};

#endif

static constexpr int32_t kStreamCount =
	(int32_t)(sizeof(dma_streams) / sizeof(dma_streams[0]));

// Mutable ownership flags share the descriptor-table index and start clear in .bss.
static bool dma_used[kStreamCount] = {};

// ---------------------------------------------------------------------------

// Accept exact hardware object addresses only: range plus stride divisibility.
// A pointer inside a stream/channel register block must not acquire that resource.

// ---------------------------------------------------------------------------

SysInitStatus DMA_Sx::SetUp(const StreamSettings& settings)
{
	int32_t idx = GetStreamIndex(_stream);
	if (idx < 0 || idx >= kStreamCount)
		return SysInitStatus::InitError;
	if (settings.count > MAX_COUNT
	 || static_cast<uint32_t>(settings.direction) > static_cast<uint32_t>(DIR::Mem_To_Mem)
	 || static_cast<uint32_t>(settings.data_size) > static_cast<uint32_t>(SIZE::Word)
	 || (settings.direction == DIR::Mem_To_Mem && settings.circ))
		return SysInitStatus::InitError;

	const uint32_t eff_ch = _has_preset ? _preset_ch : settings.channel;
#if defined(STM32F4) || defined(STM32F7)
	if (eff_ch > 7U || settings.direction == DIR::Mem_To_Mem)
		return SysInitStatus::InitError;
#elif defined(STM32G0)
	if (eff_ch & ~DMAMUX_CxCR_DMAREQ_ID_Msk)
		return SysInitStatus::InitError;
#endif
	const uint32_t alignment_mask = (1U << static_cast<uint32_t>(settings.data_size)) - 1U;
	if ((settings.per_address | settings.mem_address) & alignment_mask)
		return SysInitStatus::InitError;

	if (dma_used[idx])
		return SysInitStatus::InitError;

	dma_used[idx] = true;
	_info = &dma_streams[idx];

	// Enable DMA clock
#if defined(STM32F4) || defined(STM32F7)
	DMA_SET_BIT(RCC->AHB1ENR, (_info->ctrl == DMA1) ? RCC_AHB1ENR_DMA1EN : RCC_AHB1ENR_DMA2EN);
	(void)RCC->AHB1ENR; // Complete the clock-enable write before accessing DMA.
#elif defined(STM32G0)
#if defined(DMA2_Channel1)
	DMA_SET_BIT(RCC->AHBENR, (_info->ctrl == DMA1) ? RCC_AHBENR_DMA1EN : RCC_AHBENR_DMA2EN);
#else
	DMA_SET_BIT(RCC->AHBENR, RCC_AHBENR_DMA1EN);
#endif
	(void)RCC->AHBENR; // Either DMA enable also clocks DMAMUX1.
#endif

	// Disable stream and wait for hardware to clear EN
	Stream_EN(DISABLE);
	ClearFlags();

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
	(DMAMUX1_Channel0 + idx)->CCR = settings.direction == DIR::Mem_To_Mem ? 0U : eff_ch;
#endif

	_CR()   = cr;
	_PAR()  = settings.per_address;
	_MAR()  = settings.mem_address;
	_NDTR() = settings.count;

	return SysInitStatus::InitOK;
}

/**
 * @brief Stop/reset only this acquired stream/channel and return it to the pool.
 * Consumer disables peripheral requests and unregisters its handler beforehand.
 * Shared controller clocks/NVIC and other consumers are deliberately untouched.
 * Constructor selection remains, allowing a later explicit SetUp after release.
 */
SysStatus DMA_Sx::Release()
{
	if (!_info) return SysStatus::NotInit;
	const int32_t idx = static_cast<int32_t>(_info - dma_streams);
	Stream_EN(DISABLE);
	_CR() = 0U; // Disable this stream/channel's interrupt sources and configuration.
	ClearFlags();
	_NDTR() = 0U;
	_PAR() = 0U;
	_MAR() = 0U;
#if defined(STM32F4) || defined(STM32F7)
	Stream()->M1AR = 0U;
	Stream()->FCR = DMA_SxFCR_FTH_0; // Reset writable FIFO fields; status bits are read-only.
#elif defined(STM32G0)
	(DMAMUX1_Channel0 + idx)->CCR = 0U;
	DMAMUX1_ChannelStatus->CFR = 1U << idx;
#endif
	dma_used[idx] = false;
	_info = nullptr;
	return SysStatus::OK;
}
