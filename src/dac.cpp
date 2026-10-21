/**
 * @file dac.cpp
 * @brief Configure one direct-output DAC channel without resetting the shared block.
 */
#include "dac.hpp"

#if (defined(STM32F4) || defined(STM32F7) || defined(STM32G0)) && defined(DAC1)

SysInitStatus DAC_Module::SetUp()
{
	if (data_out) return SysInitStatus::InitError;
	DAC_TypeDef* const DACx = reinterpret_cast<DAC_TypeDef*>(_instance.periph);
	DAC_SET_BIT(*reinterpret_cast<volatile uint32_t*>(_instance.clk_reg), _instance.clk_bit);
	(void)*reinterpret_cast<volatile uint32_t*>(_instance.clk_reg);

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
	DAC_CLEAR_BIT(DACx->CR, (channel_mask << shift));
#if defined(STM32G0)
	// Normal operation, external output, buffer enabled. Change only this channel.
	DAC_CLEAR_BIT(DACx->MCR, (DAC_MCR_MODE1 << shift));
#endif
	DACx->SR = DAC_SR_DMAUDR1 << shift; // W1C: clear only this channel's old underrun.
	volatile uint32_t* output = ch == CHANNEL::CH1 ? &DACx->DHR12R1 : &DACx->DHR12R2;
	*output = 0;
	DAC_SET_BIT(DACx->CR, DAC_CR_EN1 << shift);
	data_out = output;
	return SysInitStatus::InitOK;
}

void DAC_Module::DeInit()
{
	if (!data_out) return;
	DAC_TypeDef* const DACx = reinterpret_cast<DAC_TypeDef*>(_instance.periph);
	DAC_CLEAR_BIT(DACx->CR, (ch == CHANNEL::CH1 ? DAC_CR_EN1 : DAC_CR_EN2));
	data_out = nullptr;
}

#endif // Supported family with DAC.
