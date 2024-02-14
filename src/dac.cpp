#include <dac.hpp>

SYS_StatusTypeDef DAC_Module::SetUp()
{
	RCC->APB1ENR |= RCC_APB1ENR_DACEN;

	if(_CH1.PORT != 0)
	{
		_CH1.SetUp(PIN::TYPE::ANALOG);
		DAC->CR |= DAC_CR_EN1;
		DAC->DHR12R1 = 0;
	}

	if(_CH2.PORT != 0)
	{
		_CH2.SetUp(PIN::TYPE::ANALOG);
		DAC->CR |= DAC_CR_EN2;
		DAC->DHR12R2 = 0;
	}

	return SYS_OK;
}
