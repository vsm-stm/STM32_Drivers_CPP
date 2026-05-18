#include "gpio.hpp"

/**
 * @brief Set up the GPIO pin with the specified type, speed and alternate function.
 *
 * @param type   Combined pin configuration (mode + pull + output type).
 * @param speed  Output slew-rate (ignored for INPUT / ANALOG).
 * @param af     Alternate function index 0..15 (used only in AF mode).
 * @return       SysInitStatus::InitOK on success.
 */
SysInitStatus PIN::SetUp(PIN::TYPE type, OUTPUT_SPEED speed, uint8_t af)
{
	// ------------------------------------------------------------------
	// 1. Extract individual fields from the packed TYPE byte.
	//
	// FIX: original code had wrong operator-precedence:
	//      "type & 0x3 << pull_pos" is "type & (0x3 << pull_pos)" which
	//      masks the wrong bits and never actually shifts back to [1:0].
	//      Correct form: shift first, then mask.
	// ------------------------------------------------------------------
	const uint8_t raw   = static_cast<uint8_t>(type);
	const uint8_t mode  = (raw >> mode_pos)        & 0x3u;  // bits [1:0]
	const uint8_t pull  = (raw >> pull_pos)        & 0x3u;  // bits [3:2] → [1:0]
	const uint8_t otype = (raw >> output_type_pos) & 0x1u;  // bit  [4]   → [0]

	// ------------------------------------------------------------------
	// 2. Enable the GPIO peripheral clock (AHB1).
	//    Clock bit for GPIOx = RCC_AHB1ENR_GPIOAEN << gpio_id,
	//    where gpio_id = 0 for GPIOA, 1 for GPIOB, …
	// ------------------------------------------------------------------
	const uint32_t gpio_id =
		(reinterpret_cast<uint32_t>(PORT) - GPIOA_BASE) / (GPIOB_BASE - GPIOA_BASE);

	RCC_GPIO_EN_REG |= (RCC_GPIOA_EN << gpio_id);
	(void)RCC_GPIO_EN_REG; // from reference manual: "A read access to the peripheral clock register is required after an RCC peripheral clock enabling to ensure that the clock is effectively enabled before starting the configuration of the peripheral."
	
	// ------------------------------------------------------------------
	// 3. Clear the relevant register fields before writing new values.
	//
	// FIX: PUPDR is a 2-bit-per-pin register just like MODER, so the
	//      clear mask must use (pin * 2), not plain (pin).
	// ------------------------------------------------------------------
	PORT->MODER   &= ~(0x3u << (pin * 2));  // 2 bits per pin
	PORT->PUPDR   &= ~(0x3u << (pin * 2));  // FIX: was ~(3 << pin) — wrong shift
	PORT->OTYPER  &= ~(0x1u <<  pin);       // 1 bit per pin
	PORT->OSPEEDR &= ~(0x3u << (pin * 2));  // 2 bits per pin
	PORT->AFR[pin >> 3] &= ~(0xFu << ((pin & 0x7u) * 4u));

	// ------------------------------------------------------------------
	// 4. Configure the registers according to the selected mode.
	// ------------------------------------------------------------------
	const PIN::MODE modeEnum = static_cast<PIN::MODE>(mode);

	if (modeEnum != PIN::MODE::ANALOG)
	{
		if (modeEnum == PIN::MODE::OUTPUT || modeEnum == PIN::MODE::AF)
		{
			PORT->OTYPER  |= (static_cast<uint32_t>(otype) << pin);
			PORT->OSPEEDR |= (static_cast<uint32_t>(speed) << (pin * 2));
		}

		// Pull-up / pull-down is valid for INPUT, OUTPUT and AF modes
		PORT->PUPDR |= (static_cast<uint32_t>(pull) << (pin * 2));

		if (modeEnum == PIN::MODE::AF)
		{
			PORT->AFR[pin >> 3] |= (static_cast<uint32_t>(af) << ((pin & 0x7u) * 4u));
		}
	}

	// 5. Finally write the mode bits.
	PORT->MODER |= (static_cast<uint32_t>(mode) << (pin * 2));

	return SysInitStatus::InitOK;
}
