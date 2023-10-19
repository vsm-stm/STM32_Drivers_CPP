#include "gpio.hpp"

SYS_StatusTypeDef PIN::SetUp(PIN::TYPE type)
{
	return SetUp(type, 0);
}

SYS_StatusTypeDef PIN::SetUp(PIN::TYPE type, uint8_t af)
{
	uint8_t mode  = static_cast<uint8_t>(type) & 0x3 << mode_pos;
	uint8_t pull  = static_cast<uint8_t>(type) & 0x3 << pull_pos;
	uint8_t otype = (static_cast<uint8_t>(type) & 0x1 << output_type_pos) >> output_type_pos;

	uint32_t gpio_id = (reinterpret_cast<uint32_t>(PORT) - GPIOA_BASE)/(GPIOB_BASE - GPIOA_BASE);
	if(!(RCC->AHB1ENR & (RCC_AHB1ENR_GPIOAEN + gpio_id)))
		RCC->AHB1ENR |= (RCC_AHB1ENR_GPIOAEN + gpio_id);
	
	PORT->MODER 	&= ~(3 << (pin*2));
	PORT->PUPDR 	&= ~(1 <<  pin);
	PORT->OTYPER 	&= ~(1 <<  pin);
	PORT->OSPEEDR 	&= ~(3 << (pin*2));
	PORT->AFR[pin >> 3] &= ~(0xF << ((pin & 0x7)*4U));

	if(static_cast<PIN::MODE>(mode) != PIN::MODE::ANALOG)
	{
		if((static_cast<PIN::MODE>(mode) == PIN::MODE::OUTPUT)
		|| (static_cast<PIN::MODE>(mode) == PIN::MODE::AF))
		{
			PORT->OTYPER |= otype << pin;
		}

		PORT->PUPDR |= pull << (pin*2);

		if(static_cast<PIN::MODE>(mode) == PIN::MODE::AF)
		{
			PORT->AFR[pin >> 3] |= af << ((pin & 0x7)*4U);
		}
	}

	PORT->MODER |= (mode << (pin*2));
	return SYS_OK;
}