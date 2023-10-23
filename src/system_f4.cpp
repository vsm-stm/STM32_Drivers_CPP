/**************************************************************************************************
 * @file system_f4.c
 * @author Vlasov Serge
 * @brief
 * @version 1.0
 * @date 18.09.2023
 *
 * @copyright Copyright (c) 2022
 *
 *************************************************************************************************/
#include "system_f4.hpp"

uint32_t Tick;

/**************************************************************************************************
 * @brief Init system main features - FPU, FLASH, Systick.
 * @return SYS_OK
 * @return SYS_ERROR
 *************************************************************************************************/
SYS_StatusTypeDef System_F4::Init()
{
	#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
		SCB->CPACR |= ((3UL << 10*2)|(3UL << 11*2));  /* set CP10 and CP11 Full Access */
	#endif

	FLASH->ACR |= FLASH_ACR_ICEN |
				  FLASH_ACR_DCEN |
				  FLASH_ACR_PRFTEN;

	if(System_F4::InitTicks() > 0)
		return SYS_ERROR;

	return SYS_OK;
}

/**************************************************************************************************
 * @brief Init systick timer
 * @return SYS_OK
 * @return SYS_ERROR
 *************************************************************************************************/
SYS_StatusTypeDef System_F4::InitTicks()
{
	// if(SystemCoreClock >= 100000000)
	// 	Ticks_base = 10000;
	// else
	// 	Ticks_base = 1000;
	if(SysTick_Config(SystemCoreClock/TICK_BASE) > 0)
		return SYS_ERROR;

	return SYS_OK;
}

/**************************************************************************************************
 * @brief Increase tick
 *************************************************************************************************/
void System_F4::TickIncrease()
{
	Tick++;
};

uint32_t System_F4::GetTick()
{
	return Tick;
}


/**************************************************************************************************
 * @brief Make delay in ms
 * @param delay value in ms
 *************************************************************************************************/
void System_F4::Delay_ms(uint32_t delay)
{
	uint32_t tick_start = GetTick();
	uint32_t wait = delay;
	
	if(wait < 0xFFFFFFFF)
	{
		wait+= (1000/TICK_BASE);
	}

	while((GetTick() - tick_start) < wait)
	{

	}	
}

extern "C" void SysTick_Handler(void)
{
	System_F4::TickIncrease();
}