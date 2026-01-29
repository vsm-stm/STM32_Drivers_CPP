#include "gpio.hpp"

/**
 * @brief Set up the GPIO pin with the specified type and alternate function.
 * @param type The enumerate type of pin configuration.
 * @param af The alternate function number.
 * @return The status of the setup operation.
 */
SysInitStatus PIN::SetUp(PIN::TYPE type, OUTPUT_SPEED speed, uint8_t af)
{
	// Extract individual configuration bits from the type
	uint8_t mode  = static_cast<uint8_t>(type) & 0x3 << mode_pos;
	uint8_t pull  = (static_cast<uint8_t>(type) & 0x3 << pull_pos) >> pull_pos;
	uint8_t otype = (static_cast<uint8_t>(type) & 0x1 << output_type_pos) >> output_type_pos;

	// Calculate the GPIO ID to enable the corresponding clock
	uint32_t gpio_id = (reinterpret_cast<uint32_t>(PORT) - GPIOA_BASE) / (GPIOB_BASE - GPIOA_BASE);
	if (!(RCC->AHB1ENR & (RCC_AHB1ENR_GPIOAEN << gpio_id)))
		RCC->AHB1ENR |= (RCC_AHB1ENR_GPIOAEN << gpio_id);

	// Clear the relevant bits in the GPIO registers
	PORT->MODER &= ~(3 << (pin * 2));
	PORT->PUPDR &= ~(3 << pin);
	PORT->OTYPER &= ~(1 << pin);
	PORT->OSPEEDR &= ~(3 << (pin * 2));
	PORT->AFR[pin >> 3] &= ~(0xF << ((pin & 0x7) * 4U));

	// Configure the GPIO registers based on the selected pin mode
	if (static_cast<PIN::MODE>(mode) != PIN::MODE::ANALOG)
	{
		if ((static_cast<PIN::MODE>(mode) == PIN::MODE::OUTPUT)
		||  (static_cast<PIN::MODE>(mode) == PIN::MODE::AF))
		{
			PORT->OTYPER |= otype << pin;
			PORT->OSPEEDR |= static_cast<uint8_t>(speed) << (pin*2);
		}

		PORT->PUPDR |= pull << (pin * 2);

		if (static_cast<PIN::MODE>(mode) == PIN::MODE::AF)
		{
			PORT->AFR[pin >> 3] |= af << ((pin & 0x7) * 4U);
		}
	}

	// Set the mode bits in the MODER register
	PORT->MODER |= (mode << (pin * 2));
	
	return SysInitStatus::InitOK;
}
