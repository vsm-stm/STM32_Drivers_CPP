#include <dma.hpp>

SYS_StatusTypeDef DMA_Sx::SetUp()
{
	// Determine the DMA controller, IRQ vector, Configuration Register, and offset based on the DMA stream
	if(DMA_Stream_X == DMA1_Stream0)
	{
		DMA_controller = DMA1;
		IRQ_vector = DMA1_Stream0_IRQn;

		DMA_CFR = (uint32_t*)&(DMA1->LIFCR);
		DMA_SR = (uint32_t*)&(DMA1->LISR);
		cfr_offset = 0;
	}else
	if(DMA_Stream_X == DMA1_Stream1)
	{
		DMA_controller = DMA1;
		IRQ_vector = DMA1_Stream1_IRQn;

		DMA_CFR = (uint32_t*)&(DMA1->LIFCR);
		DMA_SR = (uint32_t*)&(DMA1->LISR);
		cfr_offset = 6;
	}else
	if(DMA_Stream_X == DMA1_Stream2)
	{
		DMA_controller = DMA1;
		IRQ_vector = DMA1_Stream2_IRQn;
		
		DMA_CFR = (uint32_t*)&(DMA1->LIFCR);
		DMA_SR = (uint32_t*)&(DMA1->LISR);
		cfr_offset = 16;
	}else
	if(DMA_Stream_X == DMA1_Stream3)
	{
		DMA_controller = DMA1;
		IRQ_vector = DMA1_Stream3_IRQn;
		
		DMA_CFR = (uint32_t*)&(DMA1->LIFCR);
		DMA_SR = (uint32_t*)&(DMA1->LISR);
		cfr_offset = 22;
	}else
	if(DMA_Stream_X == DMA1_Stream4)
	{
		DMA_controller = DMA1;
		IRQ_vector = DMA1_Stream4_IRQn;

		DMA_CFR = (uint32_t*)&(DMA1->HIFCR);
		DMA_SR = (uint32_t*)&(DMA1->HISR);
		cfr_offset = 0;
	}else
	if(DMA_Stream_X == DMA1_Stream5)
	{
		DMA_controller = DMA1;
		IRQ_vector = DMA1_Stream5_IRQn;

		DMA_CFR = (uint32_t*)&(DMA1->HIFCR);
		DMA_SR = (uint32_t*)&(DMA1->HISR);
		cfr_offset = 6;
	}else
	if(DMA_Stream_X == DMA1_Stream6)
	{
		DMA_controller = DMA1;
		IRQ_vector = DMA1_Stream6_IRQn;
		
		DMA_CFR = (uint32_t*)&(DMA1->HIFCR);
		DMA_SR = (uint32_t*)&(DMA1->HISR);
		cfr_offset = 16;
	}else
	if(DMA_Stream_X == DMA1_Stream7)
	{
		DMA_controller = DMA1;
		IRQ_vector = DMA1_Stream7_IRQn;
		
		DMA_CFR = (uint32_t*)&(DMA1->HIFCR);
		DMA_SR = (uint32_t*)&(DMA1->HISR);
		cfr_offset = 22;
	}else
	if(DMA_Stream_X == DMA2_Stream0)
	{
		DMA_controller = DMA2;
		IRQ_vector = DMA2_Stream0_IRQn;

		DMA_CFR = (uint32_t*)&(DMA2->LIFCR);
		DMA_SR = (uint32_t*)&(DMA2->LISR);
		cfr_offset = 0;
	}else
	if(DMA_Stream_X == DMA2_Stream1)
	{
		DMA_controller = DMA2;
		IRQ_vector = DMA2_Stream1_IRQn;

		DMA_CFR = (uint32_t*)&(DMA2->LIFCR);
		DMA_SR = (uint32_t*)&(DMA2->LISR);
		cfr_offset = 6;
	}else
	if(DMA_Stream_X == DMA2_Stream2)
	{
		DMA_controller = DMA2;
		IRQ_vector = DMA2_Stream2_IRQn;
		
		DMA_CFR = (uint32_t*)&(DMA2->LIFCR);
		DMA_SR = (uint32_t*)&(DMA2->LISR);
		cfr_offset = 16;
	}else
	if(DMA_Stream_X == DMA2_Stream3)
	{
		DMA_controller = DMA2;
		IRQ_vector = DMA2_Stream3_IRQn;
		
		DMA_CFR = (uint32_t*)&(DMA2->LIFCR);
		DMA_SR = (uint32_t*)&(DMA2->LISR);
		cfr_offset = 22;
	}else
	if(DMA_Stream_X == DMA2_Stream4)
	{
		DMA_controller = DMA2;
		IRQ_vector = DMA2_Stream4_IRQn;

		DMA_CFR = (uint32_t*)&(DMA2->HIFCR);
		DMA_SR = (uint32_t*)&(DMA2->HISR);
		cfr_offset = 0;
	}else
	if(DMA_Stream_X == DMA2_Stream5)
	{
		DMA_controller = DMA2;
		IRQ_vector = DMA2_Stream5_IRQn;

		DMA_CFR = (uint32_t*)&(DMA2->HIFCR);
		DMA_SR = (uint32_t*)&(DMA2->HISR);
		cfr_offset = 6;
	}else
	if(DMA_Stream_X == DMA2_Stream6)
	{
		DMA_controller = DMA2;
		IRQ_vector = DMA2_Stream6_IRQn;
		
		DMA_CFR = (uint32_t*)&(DMA2->HIFCR);
		DMA_SR = (uint32_t*)&(DMA2->HISR);
		cfr_offset = 16;
	}else
	if(DMA_Stream_X == DMA2_Stream7)
	{
		DMA_controller = DMA2;
		IRQ_vector = DMA2_Stream7_IRQn;
		
		DMA_CFR = (uint32_t*)&(DMA2->HIFCR);
		DMA_SR = (uint32_t*)&(DMA2->HISR);
		cfr_offset = 22;
	}else
		return SYS_ERROR;

	// Enable the clock for the corresponding DMA controller
	if(DMA_controller == DMA1)
	{
		RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
	}
	else
	{
		RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
	}

	// Set peripheral address for the DMA stream
	DMA_Stream_X->PAR = _paddr;
	
	// Configure DMA control register (CR) based on the channel, direction, and peripheral type
	uint32_t sets = _channel << DMA_SxCR_CHSEL_Pos;
	sets |= static_cast<uint32_t>(_direction) << DMA_SxCR_DIR_Pos;

	// Specific configuration for USART and SPI peripheral types
	if(_per_type == Per_Type::usart)
	{
		sets &= ~(DMA_SxCR_PSIZE_Msk |
				  DMA_SxCR_MSIZE_Msk);
	}else
	if((_per_type == Per_Type::spi)
	|| (_per_type == Per_Type::adc))
	{
		sets |= static_cast<uint32_t>(_size) << DMA_SxCR_PSIZE_Pos |
				static_cast<uint32_t>(_size) << DMA_SxCR_MSIZE_Pos;
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
