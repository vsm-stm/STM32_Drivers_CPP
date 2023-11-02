/*
 * gpio.h
 *
 *  Created on: Oct 11, 2023
 *      Author: smvla
 */

#ifndef GPIO_H_
#define GPIO_H_

#include <system_f4.hpp>
#include "stm32f4xx.h"

class PIN
{
private:
	static const uint8_t mode_pos = 0;
	enum class MODE
	{
		INPUT = (0b00 << mode_pos),
		OUTPUT = (0b1 << mode_pos),
		AF = (0b10 << mode_pos),
		ANALOG = (0b11 << mode_pos)
	};

	static const uint8_t pull_pos = 2;
	enum class PULL
	{
		NO_Pull = (0b0 << pull_pos),
		PullUP = (0b1 << pull_pos),
		PullDown = (0b10 << pull_pos)
	};
	static const uint8_t output_type_pos = 4;
	enum class OUTPUT_TYPE
	{
		PushPull = (0b0 << output_type_pos),
		OpenDrain = (0b1 << output_type_pos)
	};

	void Reset()
	{
		SetUp(TYPE::INPUT_NO_Pull);
	};

public:
	GPIO_TypeDef *PORT{};
	uint8_t pin{};

	enum class TYPE
	{
		INPUT_NO_Pull		= ((uint8_t)MODE::INPUT | (uint8_t)PULL::NO_Pull),
		INPUT_PullUp		= ((uint8_t)MODE::INPUT | (uint8_t)PULL::PullUP),
		INPUT_PullDown		= ((uint8_t)MODE::INPUT | (uint8_t)PULL::PullDown),
		OUTPUT_PushPull		= ((uint8_t)MODE::OUTPUT | (uint8_t)OUTPUT_TYPE::PushPull),
		OUTPUT_OD			= ((uint8_t)MODE::OUTPUT | (uint8_t)OUTPUT_TYPE::OpenDrain),
		OUTPUT_OD_PulUp		= ((uint8_t)MODE::OUTPUT | (uint8_t)OUTPUT_TYPE::OpenDrain | (uint8_t)PULL::PullUP),
		AF_PushPull			= ((uint8_t)MODE::AF | (uint8_t)OUTPUT_TYPE::PushPull),
		AF_OD				= ((uint8_t)MODE::AF | (uint8_t)OUTPUT_TYPE::OpenDrain),
		AF_OD_PulUp			= ((uint8_t)MODE::AF | (uint8_t)OUTPUT_TYPE::OpenDrain | (uint8_t)PULL::PullUP),
		ANALOG				= ((uint8_t)MODE::ANALOG)
	};

	enum class LVL
	{
		LOW,
		HIGH
	};


    PIN()                       = default;
    PIN(PIN const &)            = default;
    PIN(PIN &&)                 = default;
    PIN &operator=(PIN const &) = default;
    PIN &operator=(PIN &&)      = default;

	explicit PIN(GPIO_TypeDef *port, uint8_t pn):
			PORT(port),
			pin(pn)
	{
	}

	SYS_StatusTypeDef SetUp(TYPE type);
	SYS_StatusTypeDef SetUp(TYPE type, uint8_t af);

	inline bool GetLevel()
	{
		return ((PORT->IDR & (0x1 << pin)) >> pin);
	};

	inline bool GetLevel_BB()
	{
		return BIT_BB(&PORT->IDR, pin);
	};

	inline void SetLevel(uint32_t lvl_int)
	{
		BIT_BB(&PORT->ODR, pin) = lvl_int;
	};

	inline void SetLevel(LVL lvl)
	{
		PORT->BSRR |= ((lvl == LVL::HIGH) ? GPIO_BSRR_BS0 : GPIO_BSRR_BR0) << pin;
	};

	inline void SetLevel_BB(uint32_t lvl_int)
	{
		BIT_BB(&PORT->ODR, pin) = lvl_int;
	}
	inline void SetLevel_BB(LVL lvl)
	{
		BIT_BB(&PORT->ODR, pin) = static_cast<uint8_t>(lvl);
	};

	inline void TogglePin()
	{
		PORT->ODR ^= 0x1 << pin;
	};

	inline void TogglePin_BB()
	{
		BIT_BB(&PORT->ODR, pin) ^= 1;
	};

	~PIN()
	{
		Reset();
	};


};

#endif