#include <dac.hpp>

SYS_StatusTypeDef DAC_Module::SetUp()
{
	RCC->APB1ENR |= RCC_APB1ENR_DACEN;

	if(ch == CHANNEL::CH1)
	{
		DAC->CR |= DAC_CR_EN1;
		data_out = const_cast<uint32_t*>(&DAC->DHR12R1);
	}
	if(ch == CHANNEL::CH2)
	{
		DAC->CR |= DAC_CR_EN2;
		data_out = const_cast<uint32_t*>(&DAC->DHR12R2);
	}
	*data_out = 0;
	out_pin.SetUp(PIN::TYPE::ANALOG);

	return SYS_OK;
}
