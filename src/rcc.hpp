#ifndef RCC_H_
#define RCC_H_

#include "stm32f4xx.h"
#include <stdint.h>
#include <system_f4.hpp>

#define HSI_Clock	16000000UL

#define PLL_TIMEOUT_VALUE			2U  /* 2 ms */
#define HSE_STARTUP_TIMEOUT			100U   /*!< Time out for HSE start up, in ms */
#define HSE_TIMEOUT_VALUE			HSE_STARTUP_TIMEOUT
#define HSI_TIMEOUT_VALUE			2U  /* 2 ms */
#define LSI_TIMEOUT_VALUE			2U  /* 2 ms */
#define CLOCKSWITCH_TIMEOUT_VALUE	5000U /* 5 s */

/* @todo: add #ifdef for different controllers */
#if defined(STM32F446xx) || defined(STM32F429xx)
#define SYS_CLK_LIMIT		180000000 
#define APB1_CLK_LIMIT		45000000
#define APB2_CLK_LIMIT		90000000
#endif

class ClockSystem
{
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

	static uint32_t HSESrcClk;

	/**************************************************************************************************
	 * @brief SystemClockSource (HSI,HSE,PLL_P,PLL_R)
	 ***************************************************************************************************/
	enum class SystemClockSource
	{
		HSI = RCC_CFGR_SW_HSI, /* interal clock source*/
		HSE = RCC_CFGR_SW_HSE,
		PLL_P = RCC_CFGR_SW_PLL
#ifdef STM32F446xx		
		,PLL_R = RCC_CFGR_SW_PLLR
#endif
	};

	/**************************************************************************************************
	 * @brief AHB Dividers
	 ***************************************************************************************************/
	enum class AHB_Divider
	{
		DIV1 	= RCC_CFGR_HPRE_DIV1,
		DIV2 	= RCC_CFGR_HPRE_DIV2,
		DIV4 	= RCC_CFGR_HPRE_DIV4,
		DIV8 	= RCC_CFGR_HPRE_DIV8,
		DIV16 	= RCC_CFGR_HPRE_DIV16,
		DIV64 	= RCC_CFGR_HPRE_DIV64,
		DIV128 	= RCC_CFGR_HPRE_DIV128,
		DIV256 	= RCC_CFGR_HPRE_DIV256,
		DIV512 	= RCC_CFGR_HPRE_DIV512
	};
	/**************************************************************************************************
	 * @brief APB1 dividers
	 ***************************************************************************************************/
	enum class APB1_Divider
	{
		DIV1 	= RCC_CFGR_PPRE1_DIV1,
		DIV2 	= RCC_CFGR_PPRE1_DIV2,
		DIV4 	= RCC_CFGR_PPRE1_DIV4,
		DIV8 	= RCC_CFGR_PPRE1_DIV8,
		DIV16 	= RCC_CFGR_PPRE1_DIV16
	};
	/**************************************************************************************************
	 * @brief APB2 dividers
	 ***************************************************************************************************/
	enum class APB2_Divider
	{
		DIV1 	= RCC_CFGR_PPRE2_DIV1,
		DIV2 	= RCC_CFGR_PPRE2_DIV2,
		DIV4 	= RCC_CFGR_PPRE2_DIV4,
		DIV8 	= RCC_CFGR_PPRE2_DIV8,
		DIV16 	= RCC_CFGR_PPRE2_DIV16
	};
	/**************************************************************************************************
	 * @brief BusDividers - AHB, APB1, APB2
	 ***************************************************************************************************/
	struct BusDividers
	{
		AHB_Divider AHB_div;
		APB1_Divider APB1_div;
		APB2_Divider APB2_div;
	};
	
	/**************************************************************************************************
	 * @brief PLL Clock sourse - HSI or HSE
	 ***************************************************************************************************/
	enum class PLL_ClockSource
	{	
		NO  = 0xFFFF,
		HSI = RCC_PLLCFGR_PLLSRC_HSI,
		HSE = RCC_PLLCFGR_PLLSRC_HSE
	};
	/**************************************************************************************************
	 * @brief PLL configuration = Src, M,N,P,Q,R
	 ***************************************************************************************************/
	struct PLL_CFGR
	{
		PLL_ClockSource PLL_ClkSrc;
		uint8_t  PLL_M; /* 2 - 63*/
		uint16_t PLL_N; /* 50 - 432*/
		uint8_t  PLL_P; /* 2, 4, 6, 8*/
		uint8_t  PLL_Q;
		uint8_t  PLL_R;
	};

	/**************************************************************************************************
	 * @brief CLK init directly on HSI or HSE
	 * @param ClkSrc Part of SystemClockSource enum can be HSI or HSE
	 ***************************************************************************************************/
	static SYS_StatusTypeDef Init(SystemClockSource ClkSrc)
	{
		return Init(ClkSrc, 0, {AHB_Divider::DIV1,APB1_Divider::DIV1,APB2_Divider::DIV1}, {PLL_ClockSource::NO,2,2,2,2,2});
	};

	/**************************************************************************************************
	 * @brief CLK init directly on HSI or HSE
	 * @param ClkSrc Part of SystemClockSource enum can be HSI or HSE
	 * @param HSE_Clk HSE Freq
	 ***************************************************************************************************/
	static SYS_StatusTypeDef Init(SystemClockSource ClkSrc, uint32_t HSE_Clk)
	{
		return Init(ClkSrc, HSE_Clk, {AHB_Divider::DIV1,APB1_Divider::DIV1,APB2_Divider::DIV1}, {PLL_ClockSource::NO,2,2,2,2,2});
	};

	/**************************************************************************************************
	 * @brief CLK init directly on HSI or HSE with divide bus CLK
	 * @param ClkSrc Part of SystemClockSource enum can be HSI or HSE
	 * @param BusDiv AHB, APB1, APB2 dividers
	 ***************************************************************************************************/
	static SYS_StatusTypeDef Init(SystemClockSource ClkSrc, BusDividers BusDiv)
	{
		return Init(ClkSrc, 0, BusDiv, {PLL_ClockSource::NO,2,2,2,2,2});
	};

	/**************************************************************************************************
	 * @brief CLK init directly on HSI or HSE with divide bus CLK
	 * @param ClkSrc Part of SystemClockSource enum can be HSI or HSE
	 * @param HSE_Clk HSE Freq
	 * @param BusDiv AHB, APB1, APB2 dividers
	 ***************************************************************************************************/	
	static SYS_StatusTypeDef Init(SystemClockSource ClkSrc, uint32_t HSE_Clk, BusDividers BusDiv)
	{
		return Init(ClkSrc, HSE_Clk, {AHB_Divider::DIV1,APB1_Divider::DIV1,APB2_Divider::DIV1}, {PLL_ClockSource::NO,2,2,2,2,2});
	};

	/**************************************************************************************************
	 * @brief CLK init with PLL
	 * @param ClkSrc Part of SystemClockSource enum can be PLL_P or PLL_R
	 * @param BusDiv AHB, APB1, APB2 dividers
	 * @param PLLCfgr PLL_Cls_Src, PLL_M, PLL_N, PLL_P, PLL_Q, PLL_R
	 ***************************************************************************************************/
	static SYS_StatusTypeDef Init(SystemClockSource ClkSrc, BusDividers BusDiv, PLL_CFGR PLLCfgr)
	{
		return Init(ClkSrc, 0, BusDiv, PLLCfgr);
	};

	/**************************************************************************************************
	 * @brief CLK init with PLL
	 * @param ClkSrc Part of SystemClockSource enum can be PLL_P or PLL_R
	 * @param HSE_Clk HSE Freq
	 * @param BusDiv AHB, APB1, APB2 dividers
	 * @param PLLCfgr PLL_Cls_Src, PLL_M, PLL_N, PLL_P, PLL_Q, PLL_R
	 ***************************************************************************************************/
	static SYS_StatusTypeDef Init(SystemClockSource ClkSrc, uint32_t HSE_Clk, BusDividers BusDiv, PLL_CFGR PLLCfgr);

	/**************************************************************************************************
	 * @brief CLK init with calculating PLL value from internal clk
	 * @param pll_src PLL Clock source - HSI
	 * @param req_freq required frequency
	 ***************************************************************************************************/
	static SYS_StatusTypeDef Init_calc_pll(uint32_t req_freq, PLL_ClockSource pll_src)
	{
		return Init_calc_pll(req_freq, pll_src, 0);
	};

	/**************************************************************************************************
	 * @brief CLK init with calculating PLL value from internal clk
	 * @param pll_src PLL Clock source - HSI
	 * @param req_freq required frequency
	 * @param hse_clk HSE clock value
	 * @todo more intellegent calculations
	 ***************************************************************************************************/
	static SYS_StatusTypeDef Init_calc_pll(uint32_t req_freq, PLL_ClockSource pll_src, uint32_t hse_clk);

	private:
		/* data */
};


#endif
