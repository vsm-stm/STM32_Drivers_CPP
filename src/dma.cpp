#include <dma.hpp>
#include <map>


class dma_using_td
{
public:
	const DMA_Sx::dma_sets_typedef dma1_s0 = {.used = false,
										.ctrl = DMA1,
										.cfr = (uint32_t*)&(DMA1->LIFCR),
										.sr = (uint32_t*)&(DMA1->LISR),
										.cfr_offset = 0,
										.irqn = DMA1_Stream0_IRQn};

	const DMA_Sx::dma_sets_typedef dma1_s1 = {.used = false,
										.ctrl = DMA1,
										.cfr = (uint32_t*)&(DMA1->LIFCR),
										.sr = (uint32_t*)&(DMA1->LISR),
										.cfr_offset = 6,
										.irqn = DMA1_Stream1_IRQn};

	const DMA_Sx::dma_sets_typedef dma1_s2 = {.used = false,
										.ctrl = DMA1,
										.cfr = (uint32_t*)&(DMA1->LIFCR),
										.sr = (uint32_t*)&(DMA1->LISR),
										.cfr_offset = 16,
										.irqn = DMA1_Stream2_IRQn};

	const DMA_Sx::dma_sets_typedef dma1_s3 = {.used = false,
										.ctrl = DMA1,
										.cfr = (uint32_t*)&(DMA1->LIFCR),
										.sr = (uint32_t*)&(DMA1->LISR),
										.cfr_offset = 22,
										.irqn = DMA1_Stream3_IRQn};

	const DMA_Sx::dma_sets_typedef dma1_s4 = {.used = false,
										.ctrl = DMA1,
										.cfr = (uint32_t*)&(DMA1->HIFCR),
										.sr = (uint32_t*)&(DMA1->HISR),
										.cfr_offset = 0,
										.irqn = DMA1_Stream4_IRQn};

	const DMA_Sx::dma_sets_typedef dma1_s5 = {.used = false,
										.ctrl = DMA1,
										.cfr = (uint32_t*)&(DMA1->HIFCR),
										.sr = (uint32_t*)&(DMA1->HISR),
										.cfr_offset = 6,
										.irqn = DMA1_Stream5_IRQn};

	const DMA_Sx::dma_sets_typedef dma1_s6 = {.used = false,
										.ctrl = DMA1,
										.cfr = (uint32_t*)&(DMA1->HIFCR),
										.sr = (uint32_t*)&(DMA1->HISR),
										.cfr_offset = 16,
										.irqn = DMA1_Stream6_IRQn};

	const DMA_Sx::dma_sets_typedef dma1_s7 = {.used = false,
										.ctrl = DMA1,
										.cfr = (uint32_t*)&(DMA1->HIFCR),
										.sr = (uint32_t*)&(DMA1->HISR),
										.cfr_offset = 22,
										.irqn = DMA1_Stream7_IRQn};

	const DMA_Sx::dma_sets_typedef dma2_s0 = {.used = false,
										.ctrl = DMA2,
										.cfr = (uint32_t*)&(DMA2->LIFCR),
										.sr = (uint32_t*)&(DMA2->LISR),
										.cfr_offset = 0,
										.irqn = DMA2_Stream0_IRQn};

	const DMA_Sx::dma_sets_typedef dma2_s1 = {.used = false,
										.ctrl = DMA2,
										.cfr = (uint32_t*)&(DMA2->LIFCR),
										.sr = (uint32_t*)&(DMA2->LISR),
										.cfr_offset = 6,
										.irqn = DMA2_Stream1_IRQn};

	const DMA_Sx::dma_sets_typedef dma2_s2 = {.used = false,
										.ctrl = DMA2,
										.cfr = (uint32_t*)&(DMA2->LIFCR),
										.sr = (uint32_t*)&(DMA2->LISR),
										.cfr_offset = 16,
										.irqn = DMA2_Stream2_IRQn};

	const DMA_Sx::dma_sets_typedef dma2_s3 = {.used = false,
										.ctrl = DMA2,
										.cfr = (uint32_t*)&(DMA2->LIFCR),
										.sr = (uint32_t*)&(DMA2->LISR),
										.cfr_offset = 22,
										.irqn = DMA2_Stream3_IRQn};

	const DMA_Sx::dma_sets_typedef dma2_s4 = {.used = false,
										.ctrl = DMA2,
										.cfr = (uint32_t*)&(DMA2->HIFCR),
										.sr = (uint32_t*)&(DMA2->HISR),
										.cfr_offset = 0,
										.irqn = DMA2_Stream4_IRQn};

	const DMA_Sx::dma_sets_typedef dma2_s5 = {.used = false,
										.ctrl = DMA2,
										.cfr = (uint32_t*)&(DMA2->HIFCR),
										.sr = (uint32_t*)&(DMA2->HISR),
										.cfr_offset = 6,
										.irqn = DMA2_Stream5_IRQn};

	const DMA_Sx::dma_sets_typedef dma2_s6 = {.used = false,
										.ctrl = DMA2,
										.cfr = (uint32_t*)&(DMA2->HIFCR),
										.sr = (uint32_t*)&(DMA2->HISR),
										.cfr_offset = 16,
										.irqn = DMA2_Stream6_IRQn};

	const DMA_Sx::dma_sets_typedef dma2_s7 = {.used = false,
										.ctrl = DMA2,
										.cfr = (uint32_t*)&(DMA2->HIFCR),
										.sr = (uint32_t*)&(DMA2->HISR),
										.cfr_offset = 22,
										.irqn = DMA2_Stream7_IRQn};

std::map<DMA_Stream_TypeDef*, DMA_Sx::dma_sets_typedef> used_dma;

	dma_using_td(){
		used_dma.insert({DMA1_Stream0, dma1_s0});
		used_dma.insert({DMA1_Stream1, dma1_s1});
		used_dma.insert({DMA1_Stream2, dma1_s2});
		used_dma.insert({DMA1_Stream3, dma1_s3});
		used_dma.insert({DMA1_Stream4, dma1_s4});
		used_dma.insert({DMA1_Stream5, dma1_s5});
		used_dma.insert({DMA1_Stream6, dma1_s6});
		used_dma.insert({DMA1_Stream7, dma1_s7});
		used_dma.insert({DMA2_Stream0, dma2_s0});
		used_dma.insert({DMA2_Stream1, dma2_s1});
		used_dma.insert({DMA2_Stream2, dma2_s2});
		used_dma.insert({DMA2_Stream3, dma2_s3});
		used_dma.insert({DMA2_Stream4, dma2_s4});
		used_dma.insert({DMA2_Stream5, dma2_s5});
		used_dma.insert({DMA2_Stream6, dma2_s6});
		used_dma.insert({DMA2_Stream7, dma2_s7});
	};
	~dma_using_td(){};

}dma_using;


SYS_StatusTypeDef DMA_Sx::SetUp(StreamSettings settings)
{
	return SetUp(settings.channel, settings.peripheral_address, settings.peripheral_type, settings.direction, settings.data_size);
};

SYS_StatusTypeDef DMA_Sx::SetUp(
		uint32_t channel,
		uint32_t peripheral_address,
		Per_Type peripheral_type,
		DIR direction,
		SIZE data_size)
{
	if((dma_using.used_dma[DMA_Stream_X].used == true)
	|| !(IS_DMA_STREAM_ALL_INSTANCE(DMA_Stream_X)))
		return SYS_ERROR;
	else
		dma_using.used_dma[DMA_Stream_X].used = true;

	DMA = dma_using.used_dma[DMA_Stream_X];
	
	// Enable the clock for the corresponding DMA controller
	if(DMA.ctrl == DMA1)
	{
		RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
	}
	else
	{
		RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
	}

	// Set peripheral address for the DMA stream
	if(peripheral_address == 0)
		return SYS_ERROR;
	else
		DMA_Stream_X->PAR = peripheral_address;
	
	// Configure DMA control register (CR) based on the channel, direction, and peripheral type
	uint32_t sets = channel << DMA_SxCR_CHSEL_Pos;
	sets |= static_cast<uint32_t>(direction) << DMA_SxCR_DIR_Pos;

	// Specific configuration for USART and SPI peripheral types
	if(peripheral_type == Per_Type::usart)
	{
		sets &= ~(DMA_SxCR_PSIZE_Msk |
				  DMA_SxCR_MSIZE_Msk);
	}else
	if((peripheral_type == Per_Type::spi)
	|| (peripheral_type == Per_Type::adc))
	{
		sets |= static_cast<uint32_t>(data_size) << DMA_SxCR_PSIZE_Pos |
				static_cast<uint32_t>(data_size) << DMA_SxCR_MSIZE_Pos;
	}

	// Apply the configuration to the DMA control register
	DMA_Stream_X->CR = sets;

	return SYS_OK;
}

SYS_StatusTypeDef DMA_Sx::SetMemAddr(uint32_t addr)
{
	// Check if the memory address is valid
	if (addr == 0)
		return SYS_ERROR;

	// Set the memory address for the DMA stream
	DMA_Stream_X->M0AR = addr;

	return SYS_OK;
}

SYS_StatusTypeDef DMA_Sx::SetMemAddr(uint32_t addr, uint16_t size)
{
	// Check if the size is valid
	if (size == 0)
		return SYS_ERROR;

	// Set the memory size and address for the DMA stream
	DMA_Stream_X->NDTR = size;

	// Call the function to set the memory address
	return SetMemAddr(addr);
}
