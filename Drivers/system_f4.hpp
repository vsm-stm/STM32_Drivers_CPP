/*
 * system.h
 *
 *  Created on: Sep 11, 2023
 *      Author: smvla
 */

#ifndef SYSTEM_F4_H_
#define SYSTEM_F4_H_

#include <stdint.h>
#include "stm32f4xx.h"

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
};

__attribute__ ((always_inline)) static inline uint32_t BB_RD(volatile uint32_t * addr, uint8_t bitnum) 
{
	volatile uint32_t * bitptr;
	bitptr = ((uint32_t *)( (((uint32_t)addr)-(0x40000000UL))*32 + bitnum*4 + (0x42000000UL) ));
	return *bitptr;
}

__attribute__ ((always_inline)) static inline void BB_WR(volatile uint32_t * addr, uint8_t bitnum, uint32_t value)
{
	volatile uint32_t * bitptr;
	bitptr = ((uint32_t *)( (((uint32_t)addr)-(0x40000000UL))*32 + bitnum*4 + (0x42000000UL) ));
	*bitptr = value;
}

#endif /* SYSTEM_F4_H_ */
