/**
 * @file dma.cpp
 * @brief Exact stream/channel lookup, one-owner setup and full resource release.
 *
 * The dma_used array prevents duplicate hardware ownership. Setup and release
 * are serialized main-loop operations. The consumer manages cache and requests.
 */
#include "dma.hpp"

static_assert(sizeof(dma_hw::resources) / sizeof(dma_hw::resources[0])
	== dma_hw::DMA1_COUNT + dma_hw::DMA2_COUNT);
static bool dma_used[dma_hw::DMA1_COUNT + dma_hw::DMA2_COUNT] = {};

SysInitStatus DMA_Sx::SetUp(const StreamSettings& settings)
{
	const int32_t index = GetStreamIndex(_stream);
	if (index < 0)
		return SysInitStatus::InitError;
	if (settings.count > MAX_COUNT
	 || static_cast<uint32_t>(settings.direction) > static_cast<uint32_t>(DIR::Mem_To_Mem)
	 || static_cast<uint32_t>(settings.data_size) > static_cast<uint32_t>(SIZE::Word))
		return SysInitStatus::InitError;

	const bool memory_to_memory = settings.direction == DIR::Mem_To_Mem;
	const uint32_t request = _has_preset ? _preset_ch : settings.channel;
	if ((memory_to_memory && (!dma_hw::MEM_TO_MEM || settings.circ))
	 || (!dma_hw::ValidRequest(request) && !(memory_to_memory && request == 0U)))
		return SysInitStatus::InitError;

	const uint32_t size = static_cast<uint32_t>(settings.data_size);
	const uint32_t alignment_mask = (1U << size) - 1U;
	if ((settings.per_address | settings.mem_address) & alignment_mask)
		return SysInitStatus::InitError;
	if (dma_used[index])
		return SysInitStatus::InitError;

	dma_used[index] = true;
	_info = &dma_hw::resources[index];
	dma_hw::EnableClock(index);
	Stream_EN(DISABLE);
	ClearFlags();

#if defined(STM32F4) || defined(STM32F7)
	// The previous owner, including a bootloader, may have enabled FIFO.
	Stream()->FCR = DMA_SxFCR_FTH_0;
	const uint32_t mode = (request << DMA_SxCR_CHSEL_Pos)
		| (static_cast<uint32_t>(settings.direction) << DMA_SxCR_DIR_Pos);
#else
	dma_hw::SetRequest(index, memory_to_memory ? 0U : request);
	const uint32_t mode = (settings.direction == DIR::To_Per ? DMA_CCR_DIR : 0U)
		| (memory_to_memory ? DMA_CCR_MEM2MEM : 0U);
#endif
	_CR() = mode | (size << dma_hw::PSIZE_POS) | (size << dma_hw::MSIZE_POS)
		| (settings.minc ? dma_hw::MINC : 0U)
		| (settings.pinc ? dma_hw::PINC : 0U)
		| (settings.circ ? dma_hw::CIRC : 0U);
	_PAR()  = settings.per_address;
	_MAR()  = settings.mem_address;
	_NDTR() = settings.count;

	return SysInitStatus::InitOK;
}

SysStatus DMA_Sx::Release()
{
	if (!_info) return SysStatus::NotInit;
	const int32_t index = static_cast<int32_t>(_info - dma_hw::resources);
	Stream_EN(DISABLE);
	_CR() = 0U;
	ClearFlags();
	_NDTR() = 0U;
	_PAR() = 0U;
	_MAR() = 0U;
#if defined(STM32F4) || defined(STM32F7)
	Stream()->M1AR = 0U;
	Stream()->FCR = DMA_SxFCR_FTH_0;
#else
	dma_hw::ClearRequest(index);
#endif
	dma_used[index] = false;
	_info = nullptr;
	return SysStatus::OK;
}
