/**
 * @file gpio.cpp
 * @brief Program one immutable PIN binding; no run-time range guards.
 *
 * GPIO port clock index comes from the CMSIS spacing between GPIOA/ GPIOB.
 * The expression uses division by a compile-time constant so the compiler can
 * choose a shift; no hard-coded "10" is needed for the 0x400-byte port stride.
 */
#include "gpio.hpp"

SysInitStatus PIN::SetUp(PIN::TYPE type, OUTPUT_SPEED speed, uint8_t af_override, LVL initial_level)
{
	const uint8_t raw   = static_cast<uint8_t>(type);
	const uint8_t mode  = (raw >> mode_pos)        & 0x3u;
	const uint8_t pull  = (raw >> pull_pos)        & 0x3u;
	const uint8_t otype = (raw >> output_type_pos) & 0x1u;

	const uint32_t gpio_id = (_port_base - GPIOA_BASE) / (GPIOB_BASE - GPIOA_BASE);
	RCC_GPIO_EN_REG |= (RCC_GPIOA_EN << gpio_id);
	(void)RCC_GPIO_EN_REG;

	GPIO_TypeDef* port = PORT();

	const PIN::MODE modeEnum = static_cast<PIN::MODE>(mode);
	const bool output = modeEnum == PIN::MODE::OUTPUT || modeEnum == PIN::MODE::AF;
	const uint32_t shift = pin * 2u;
	const uint32_t mode_mask = 0x3u << shift;
	// Two AFR registers cover eight pins each, four bits per AF field.
	const uint32_t af_shift = (pin & 0x7u) * 4u;
	const uint32_t new_pull = modeEnum == PIN::MODE::ANALOG ? 0u : uint32_t(pull) << shift;
	const uint32_t new_type = output ? uint32_t(otype) << pin : 0u;
	const uint32_t new_speed = output ? uint32_t(speed) << shift : 0u;
	const uint32_t new_af = modeEnum == PIN::MODE::AF ? uint32_t(af_override) << af_shift : 0u;

	// One write per configuration register; MODER is changed last, without
	// temporarily selecting INPUT and releasing an already driven line.
	if (modeEnum == PIN::MODE::OUTPUT) SetLevel(initial_level);
	port->OTYPER = (port->OTYPER & ~(0x1u << pin)) | new_type;
	port->OSPEEDR = (port->OSPEEDR & ~mode_mask) | new_speed;
	port->PUPDR = (port->PUPDR & ~mode_mask) | new_pull;
	port->AFR[pin >> 3] = (port->AFR[pin >> 3] & ~(0xFu << af_shift)) | new_af;
	port->MODER = (port->MODER & ~mode_mask) | (uint32_t(mode) << shift);

	return SysInitStatus::InitOK;
}
