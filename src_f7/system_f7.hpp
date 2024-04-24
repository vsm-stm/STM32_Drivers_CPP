/*
 * system.h
 *
 *  Created on: Sep 11, 2023
 *      Author: smvla
 */

#ifndef SYSTEM_F7_H_
#define SYSTEM_F7_H_

#include <stdint.h>
extern "C"
{
	 #include "stm32f7xx.h"
}

#define TICK_BASE 1000

typedef enum
{
  SYS_OK       = 0x00U,
  SYS_ERROR    = 0x01U,
  SYS_BUSY     = 0x02U,
  SYS_TIMEOUT  = 0x03U
} SYS_StatusTypeDef;

class System_F4
{
private:

public:
	static SYS_StatusTypeDef Init();
	static SYS_StatusTypeDef InitTicks();
	static void TickIncrease();
	static uint32_t GetTick();
	static void Delay_ms(uint32_t delay);
	static uint32_t SWOTrace(uint8_t *ptr, uint32_t len);
};

#endif /* SYSTEM_F4_H_ */
