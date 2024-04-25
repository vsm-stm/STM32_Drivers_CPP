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
#include "system.hpp"

uint32_t Tick;

/**************************************************************************************************
 * @brief Init system main features - FPU, FLASH, Systick.
 * @return SYS_OK
 * @return SYS_ERROR
 *************************************************************************************************/
SYS_StatusTypeDef System::Init()
{
	#if defined(STM32F7) /* MPU pre-config */
	/* Make sure outstanding transfers are done */
	__DMB();
	/* Disable MPU*/
	MPU->CTRL = 0U;

	/* Set Region number */
	MPU->RNR = 0x00U;
	/* Set base address */
	MPU->RBAR =  (0x0 & 0xFFFFFFE0U);
	/* Configure MPU */
	MPU->RASR = MPU_RASR_ENABLE_Msk |
				0x87 << MPU_RASR_SRD_Pos |			
				0x1FU << MPU_RASR_SIZE_Pos |		/*!< 4GB Size of the MPU protection region */
				0x00U << MPU_RASR_TEX_Pos |			/*!< b000 for TEX bits */
				0x00U << MPU_RASR_AP_Pos |			/*!< No access*/
				MPU_RASR_XN_Msk |					/*!< Instruction fetches disabled*/
				MPU_RASR_S_Msk;						/*!< Shareable memory attribute */

	/* Enable the MPU*/
	MPU->CTRL = MPU_CTRL_ENABLE_Msk | MPU_CTRL_PRIVDEFENA_Msk;
	/* Ensure MPU settings take effects */
	__DSB();
	/* Sequence instruction fetches using update settings */
	__ISB();

	/* Enable I-Cache */
	SCB_EnableICache();
	/* Enable D-Cache */
	SCB_EnableDCache();
	#endif

	#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
		SCB->CPACR |= ((3UL << 10*2)|(3UL << 11*2));  /* set CP10 and CP11 Full Access */
	#endif

	FLASH->ACR |= FLASH_ACR_PRFTEN |				/* Prefetch enable */
	#if defined(STM32F4)
				  FLASH_ACR_ICEN |
				  FLASH_ACR_DCEN;
	#elif defined(STM32F7)
				  FLASH_ACR_ARTEN;					/* Adaptive real-time memory accelerator */
	#endif

	SystemCoreClock = HSI_Clock;
	APB1BusClock = HSI_Clock;
	APB2BusClock = HSI_Clock;
	TIMxAPB1Clock = HSI_Clock;
	TIMxAPB2Clock = HSI_Clock;
	if(InitTicks() > 0)
		return SYS_ERROR;

	return SYS_OK;
}

/**************************************************************************************************
 * @brief Init systick timer
 * @return SYS_OK
 * @return SYS_ERROR
 *************************************************************************************************/
SYS_StatusTypeDef System::InitTicks()
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
void System::TickIncrease()
{
	Tick++;
};

uint32_t System::GetTick()
{
	return Tick;
}


/**************************************************************************************************
 * @brief Make delay in ms
 * @param delay value in ms
 *************************************************************************************************/
void System::Delay_ms(uint32_t delay)
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

uint32_t System::SWOTrace(uint8_t *ptr, uint32_t len)
{
	for (uint32_t DataIdx = 0; DataIdx < len; DataIdx++)
	{
		ITM_SendChar(*ptr++);
	}
	return len;
}

extern "C" void SysTick_Handler(void)
{
	System::TickIncrease();
}
