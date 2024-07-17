#ifndef SYSTEM_H_
#define SYSTEM_H_

#include <stdint.h>
extern "C"
{
#if defined(STM32F4)
	#include "stm32f4xx.h"
#elif defined(STM32F7)
	#include "stm32f7xx.h"
#endif
}

#define TICK_BASE 1000

#define HSI_Clock	16000000UL

typedef enum
{
  SYS_OK = 0x00U,
  SYS_ERROR,
  SYS_BUSY,
  SYS_NO_Init,
  SYS_TIMEOUT
} SYS_StatusTypeDef;

typedef enum
{
  System_NO		= 0x00U,
  System_OK		= 0x01U,
  System_ERROR	= 0xFFU
} SystemInitStatus_TypeDef;

class System
{
private:

public:
	/**************************************************************************************************
	 * @brief System Clock Frequency (Core Clock, GPIO, CRC, DMA, USB)
	 ***************************************************************************************************/
	static uint32_t SystemCoreClock;
	/**************************************************************************************************
	 * @brief APB1 Bus Clock Frequency (WWDG,SPI<2/3>,USART<2/3>,UART<4/5>,I2C<1/2/3>,CAN<1/2>,DAC)
	 ***************************************************************************************************/
	static uint32_t APB1BusClock;
	/**************************************************************************************************
	 * @brief APB2 Bus Clock Frequency (USART<1/6>,ADC<1/2/3>,SPI<1/4>,SYSCFG,SAI<1/2>)
	 ***************************************************************************************************/
	static uint32_t APB2BusClock;
	/**************************************************************************************************
	 * @brief APB1 Timers Clock Frequency (TIM<2/3/4/5/6/7/12/13/14> Clock)
	 ***************************************************************************************************/
	static uint32_t TIMxAPB1Clock;
	/**************************************************************************************************
	 * @brief APB2 Timers Clock Frequency (TIM<1/8/9/10/11> Clock)
	 ***************************************************************************************************/
	static uint32_t TIMxAPB2Clock;

	static SYS_StatusTypeDef Init();
	static SYS_StatusTypeDef InitTicks();
	static void TickIncrease();
	static uint32_t GetTick();
	static void Delay_ms(uint32_t delay);
	static void Enable_CYCCNT();
	static void Delay_us(uint32_t delay);
	static uint32_t SWOTrace(uint8_t *ptr, uint32_t len);
#if defined(STM32F7)
	static void MPU_Init();
#endif
};

#if defined(STM32F4)
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

#define BIT_BB(address, bit) *((uint32_t *)(PERIPH_BB_BASE + ((uint32_t)(address) - PERIPH_BASE)*32 + bit*4))

#endif

#endif /* SYSTEM_H_ */
