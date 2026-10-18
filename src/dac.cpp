/**
 * @file dac.cpp
 * @brief Configure one direct-output DAC channel without resetting the shared block.
 */
#include "dac.hpp"

#if (defined(STM32F4) || defined(STM32F7) || defined(STM32G0)) && defined(DAC1)

SysInitStatus DAC_Module::SetUp()
{
	if (data_out || DACx != DAC1 || (ch != CHANNEL::CH1 && ch != CHANNEL::CH2))
		return SysInitStatus::InitError;
#if defined(STM32F4) && !defined(DAC_CHANNEL2_SUPPORT)
	// F410 headers describe both register slots, but the device has only CH1.
	if (ch == CHANNEL::CH2) return SysInitStatus::InitError;
#endif

#if defined(STM32G0)
	RCC->APBENR1 |= RCC_APBENR1_DAC1EN;
	(void)RCC->APBENR1;
#else
	RCC->APB1ENR |= RCC_APB1ENR_DACEN;
	(void)RCC->APB1ENR;
#endif

	out_pin.SetUp(PIN::TYPE::ANALOG);
	const uint32_t shift = ch == CHANNEL::CH1 ? DAC_CR_EN1_Pos : DAC_CR_EN2_Pos;
	// CR channel fields have identical layout, shifted by EN2_Pos for CH2.
	constexpr uint32_t channel_mask = DAC_CR_EN1 | DAC_CR_TEN1 | DAC_CR_TSEL1
		| DAC_CR_WAVE1 | DAC_CR_MAMP1 | DAC_CR_DMAEN1 | DAC_CR_DMAUDRIE1
#if defined(STM32G0)
		| DAC_CR_CEN1;
#else
		| DAC_CR_BOFF1;
#endif
	DACx->CR &= ~(channel_mask << shift);
#if defined(STM32G0)
	// Normal operation, external output, buffer enabled. Change only this channel.
	DACx->MCR &= ~(DAC_MCR_MODE1 << shift);
#endif
	DACx->SR = DAC_SR_DMAUDR1 << shift; // W1C: clear only this channel's old underrun.
	volatile uint32_t* output = ch == CHANNEL::CH1 ? &DACx->DHR12R1 : &DACx->DHR12R2;
	*output = 0;
	DACx->CR |= DAC_CR_EN1 << shift;
	data_out = output;
	return SysInitStatus::InitOK;
}

void DAC_Module::DeInit()
{
	if (!data_out) return;
	DACx->CR &= ~(ch == CHANNEL::CH1 ? DAC_CR_EN1 : DAC_CR_EN2);
	data_out = nullptr;
}

#endif // Supported family with DAC.
