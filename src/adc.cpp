#include <adc.hpp>

SYS_StatusTypeDef ADC_N::SetUp(MODE mode, TRIG trig)
{
	_mode = mode;
	if(ADCx == ADC1)
	{
		RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
	}else
	if(ADCx == ADC2)
	{
		RCC->APB2ENR |= RCC_APB2ENR_ADC2EN;
	}else
	if(ADCx == ADC3)
	{
		RCC->APB2ENR |= RCC_APB2ENR_ADC3EN;
	}else
		return SYS_ERROR;

	ADCx->CR2  = ADC_CR2_ADON;
	ADCx->CR2 |= (static_cast<uint8_t>(_mode) & 1) << ADC_CR2_CONT_Pos;
	ADCx->CR1 = ((static_cast<uint8_t>(_mode) & 2) >> 1) << ADC_CR1_SCAN_Pos;

	if(trig != TRIG::Manual)
	{
		ADCx->CR2 |= ADC_CR2_EXTEN_0;
		ADCx->CR2 |= static_cast<uint8_t>(trig) << ADC_CR2_EXTSEL_Pos;
	}

	ADCx->SQR1 = 0;
	ADCx->SQR2 = 0;
	ADCx->SQR3 = 0;

	return SYS_OK;
};

void ADC_N::Add_Channel(uint8_t ch)
{
	if(ch > 19)
		return;

	if(static_cast<uint8_t>(_mode) < 2)
	{
		uint8_t prev_pin = ADCx->SQR3 & ADC_SQR3_SQ1_Msk;

		if(IN[prev_pin].use)
		{
			IN[prev_pin].pin.SetUp(PIN::TYPE::INPUT_NO_Pull);
			IN[prev_pin].use = false;
		}

		ADCx->SQR3 = ch;
	}
	else
	{
		uint8_t curr_sql_count = ((ADCx->SQR1 & ADC_SQR1_L_Msk) >> ADC_SQR1_L_Pos);

		if(((ADCx->SQR3 & ADC_SQR3_SQ1_Msk) == 0)
		 && !(IN[0].use))
		{
			curr_sql_count = 0;
		}
		else
		{
			curr_sql_count++;
			ADCx->SQR1 &= ~ADC_SQR1_L_Msk;
			ADCx->SQR1 |= curr_sql_count << ADC_SQR1_L_Pos;
		}

		if(curr_sql_count < 6)
		{
			ADCx->SQR3 |= ch << (ADC_SQR3_SQ2_Pos*curr_sql_count);
		}else
		if((curr_sql_count >= 6)
		&&((curr_sql_count < 12)))
		{
			ADCx->SQR2 |= ch << (ADC_SQR2_SQ8_Pos*(curr_sql_count-6));
		}
		else
		{
			ADCx->SQR1 |= ch << (ADC_SQR1_SQ14_Pos*(curr_sql_count-12));
		}
	}

	IN[ch].pin.SetUp(PIN::TYPE::ANALOG);
	IN[ch].use = true;

}