#include "gpio.hpp"

SysInitStatus PIN::SetUp(PIN::TYPE type, OUTPUT_SPEED speed, uint8_t af_override)
{
	const uint8_t raw   = static_cast<uint8_t>(type);
	const uint8_t mode  = (raw >> mode_pos)        & 0x3u;
	const uint8_t pull  = (raw >> pull_pos)        & 0x3u;
	const uint8_t otype = (raw >> output_type_pos) & 0x1u;

	const uint32_t gpio_id = (_port_base - GPIOA_BASE) / (GPIOB_BASE - GPIOA_BASE);
	RCC_GPIO_EN_REG |= (RCC_GPIOA_EN << gpio_id);
	(void)RCC_GPIO_EN_REG;

	GPIO_TypeDef* port = PORT();

	port->MODER   &= ~(0x3u << (pin * 2));
	port->PUPDR   &= ~(0x3u << (pin * 2));
	port->OTYPER  &= ~(0x1u <<  pin);
	port->OSPEEDR &= ~(0x3u << (pin * 2));
	port->AFR[pin >> 3] &= ~(0xFu << ((pin & 0x7u) * 4u));

	const PIN::MODE modeEnum = static_cast<PIN::MODE>(mode);

	if (modeEnum != PIN::MODE::ANALOG)
	{
		if (modeEnum == PIN::MODE::OUTPUT || modeEnum == PIN::MODE::AF)
		{
			port->OTYPER  |= (static_cast<uint32_t>(otype) << pin);
			port->OSPEEDR |= (static_cast<uint32_t>(speed) << (pin * 2));
		}

		port->PUPDR |= (static_cast<uint32_t>(pull) << (pin * 2));

		if (modeEnum == PIN::MODE::AF)
		{
			port->AFR[pin >> 3] |= (static_cast<uint32_t>(af_override) << ((pin & 0x7u) * 4u));
		}
	}

	port->MODER |= (static_cast<uint32_t>(mode) << (pin * 2));

	return SysInitStatus::InitOK;
}
