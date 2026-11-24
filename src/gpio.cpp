/**
 * @file gpio.cpp
 * @brief Configure one immutable GPIO binding using compile-time clock selection.
 * No run-time range guards, hardware call layer or per-pin state are added.
 */
#include "gpio.hpp"

SysInitStatus PIN::SetUp(PIN::TYPE type, OUTPUT_SPEED speed, uint8_t af_override, LVL initial_level)
{
	const uint8_t raw   = static_cast<uint8_t>(type);
	const uint8_t mode  = (raw >> mode_pos)        & 0x3u;
	const uint8_t pull  = (raw >> pull_pos)        & 0x3u;
	const uint8_t otype = (raw >> output_type_pos) & 0x1u;

	const uint32_t gpio_id = (_port_base - GPIOA_BASE) / (GPIOB_BASE - GPIOA_BASE);
	auto& clock = gpio_hw::ClockEnableRegister();
	clock |= gpio_hw::port_a_clock << gpio_id;
	(void)static_cast<uint32_t>(clock); // Read back the enable before port access.

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

	// Write each configuration register once and MODER last, without a temporary
	// INPUT mode. Set the OUTPUT latch before enabling the output driver.
	if (modeEnum == PIN::MODE::OUTPUT) SetLevel(initial_level);
	port->OTYPER = (port->OTYPER & ~(0x1u << pin)) | new_type;
	port->OSPEEDR = (port->OSPEEDR & ~mode_mask) | new_speed;
	port->PUPDR = (port->PUPDR & ~mode_mask) | new_pull;
	port->AFR[pin >> 3] = (port->AFR[pin >> 3] & ~(0xFu << af_shift)) | new_af;
	port->MODER = (port->MODER & ~mode_mask) | (uint32_t(mode) << shift);

	return SysInitStatus::InitOK;
}
