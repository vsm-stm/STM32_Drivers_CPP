#include "dma.hpp"

using namespace DMA_Sx_ns;

// ------------------------------------------------------------
// СТАТИЧЕСКАЯ ТАБЛИЦА DMA ПОТОКОВ   (16 элементов)
// ------------------------------------------------------------

// индекс 0..7 = DMA1 Stream0..7
// индекс 8..15 = DMA2 Stream0..7

static dma_sets_typedef dma_streams[16] = {
	
	{ DMA1, &DMA1->LISR, &DMA1->LIFCR,  0,  DMA1_Stream0_IRQn, false },
	{ DMA1, &DMA1->LISR, &DMA1->LIFCR,  6,  DMA1_Stream1_IRQn, false },
	{ DMA1, &DMA1->LISR, &DMA1->LIFCR, 16,  DMA1_Stream2_IRQn, false },
	{ DMA1, &DMA1->LISR, &DMA1->LIFCR, 22,  DMA1_Stream3_IRQn, false },
	{ DMA1, &DMA1->HISR, &DMA1->HIFCR,  0,  DMA1_Stream4_IRQn, false },
	{ DMA1, &DMA1->HISR, &DMA1->HIFCR,  6,  DMA1_Stream5_IRQn, false },
	{ DMA1, &DMA1->HISR, &DMA1->HIFCR, 16,  DMA1_Stream6_IRQn, false },
	{ DMA1, &DMA1->HISR, &DMA1->HIFCR, 22,  DMA1_Stream7_IRQn, false },

	// DMA2 streams 0..7
	{ DMA2, &DMA2->LISR, &DMA2->LIFCR,  0,  DMA2_Stream0_IRQn, false },
	{ DMA2, &DMA2->LISR, &DMA2->LIFCR,  6,  DMA2_Stream1_IRQn, false },
	{ DMA2, &DMA2->LISR, &DMA2->LIFCR, 16,  DMA2_Stream2_IRQn, false },
	{ DMA2, &DMA2->LISR, &DMA2->LIFCR, 22,  DMA2_Stream3_IRQn, false },
	{ DMA2, &DMA2->HISR, &DMA2->HIFCR,  0,  DMA2_Stream4_IRQn, false },
	{ DMA2, &DMA2->HISR, &DMA2->HIFCR,  6,  DMA2_Stream5_IRQn, false },
	{ DMA2, &DMA2->HISR, &DMA2->HIFCR, 16,  DMA2_Stream6_IRQn, false },
	{ DMA2, &DMA2->HISR, &DMA2->HIFCR, 22,  DMA2_Stream7_IRQn, false }
};

// ------------------------------------------------------------
// Функция получения индекса потока 0..15
// ------------------------------------------------------------

int32_t DMA_Sx::GetStreamIndex(DMA_Stream_TypeDef* s)
{
	uintptr_t base = reinterpret_cast<uintptr_t>(s);

	uintptr_t dma1_base = reinterpret_cast<uintptr_t>(DMA1_Stream0);
	uintptr_t dma1_end  = reinterpret_cast<uintptr_t>(DMA1_Stream7);

	uintptr_t dma2_base = reinterpret_cast<uintptr_t>(DMA2_Stream0);
	uintptr_t dma2_end  = reinterpret_cast<uintptr_t>(DMA2_Stream7);

	// Каждый stream идёт с шагом 0x18
	if (base >= dma1_base && base <= dma1_end)
		return (base - dma1_base) / 0x18;

	if (base >= dma2_base && base <= dma2_end)
		return 8 + (base - dma2_base) / 0x18;

	return -1;
}

SysInitStatus DMA_Sx::SetUp(const StreamSettings &settings)
{
	int32_t idx = GetStreamIndex(DMA_Stream_X);
	if ((idx < 0) 
	|| (settings.channel > 7))
		return SysInitStatus::InitError;

	dma_sets_typedef& info = dma_streams[idx];

	if (info.used)
		return SysInitStatus::InitError;

	info.used = true;
	DMA = &info;
	
	// Enable the clock for the corresponding DMA controller
	if(DMA->ctrl == DMA1)
	{
		RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
	}
	else
	{
		RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
	}

	DMA_Stream_X->CR &= ~DMA_SxCR_EN;
	while (DMA_Stream_X->CR & DMA_SxCR_EN) {};
	ClearFlags();
	
	// Configure DMA control register (CR) based on the channel, direction, and peripheral type
	uint32_t CR = settings.channel << DMA_SxCR_CHSEL_Pos;
	CR |= static_cast<uint32_t>(settings.direction) << DMA_SxCR_DIR_Pos;

	//
	CR |= 	static_cast<uint32_t>(settings.data_size) << DMA_SxCR_PSIZE_Pos |
			static_cast<uint32_t>(settings.data_size) << DMA_SxCR_MSIZE_Pos;

	CR |= settings.minc << DMA_SxCR_MINC_Pos |
		  settings.pinc << DMA_SxCR_PINC_Pos |
		  settings.circ << DMA_SxCR_CIRC_Pos;


	// Apply the configuration to the DMA control register
	DMA_Stream_X->CR = CR;

	// Set peripheral and memory addresses
	DMA_Stream_X->PAR = settings.per_address;
	DMA_Stream_X->M0AR = settings.mem_address;
	DMA_Stream_X->NDTR = settings.count;

	return SysInitStatus::InitOK;
}
