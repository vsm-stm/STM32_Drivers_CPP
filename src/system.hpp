#ifndef SYSTEM_H_
#define SYSTEM_H_

#include <stdint.h>

extern "C"
{
#if defined(STM32F4)
	#include "stm32f4xx.h"
#elif defined(STM32F7)
	#include "stm32f7xx.h"
#elif defined(STM32L0)
	#include "stm32l0xx.h"
#elif defined(STM32G0)
	#include "stm32g0xx.h"
#endif
}

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// SysTick interrupt frequency in Hz (1 kHz → 1 ms resolution).
static constexpr uint32_t TICK_BASE = 1000U;

/// Default HSI oscillator frequency.
static constexpr uint32_t HSI_Clock = 16000000UL;

// ---------------------------------------------------------------------------
// Status enumerations
// ---------------------------------------------------------------------------

/// Result of a runtime operation.
enum class SysStatus : uint8_t
{
	OK      = 0x00,
	Error   = 0x01,
	Busy    = 0x02,
	NotInit = 0x03,
	Timeout = 0x04
};

/// Result of an initialisation sequence.
enum class SysInitStatus : uint8_t
{
	NotInit   = 0x00,
	InitOK    = 0x01,
	InitError = 0xFF
};

// ---------------------------------------------------------------------------
// System class
// ---------------------------------------------------------------------------

class System
{
public:
	/**
	 * @brief System Core Clock frequency (AHB / core / GPIO / DMA / USB).
	 */
	static uint32_t SystemCoreClock;

	/**
	 * @brief APB1 bus clock (WWDG, SPI2/3, USART2/3, UART4/5, I2C1-3, CAN1/2, DAC).
	 */
	static uint32_t APB1BusClock;

	/**
	 * @brief APB2 bus clock (USART1/6, ADC1-3, SPI1/4, SYSCFG, SAI1/2).
	 */
	static uint32_t APB2BusClock;

	/**
	 * @brief APB1 timer clock (TIM2-7, TIM12-14).
	 */
	static uint32_t TIMxAPB1Clock;

	/**
	 * @brief APB2 timer clock (TIM1, TIM8-11).
	 */
	static uint32_t TIMxAPB2Clock;

	/**
	 * @brief Initialise core system features: FPU, Flash accelerator, SysTick.
	 */
	static SysInitStatus Init();

	/**
	 * @brief Configure and start the SysTick timer.
	 */
	static SysInitStatus InitTicks();

	/**
	 * @brief Increment the millisecond tick counter — call from SysTick_Handler.
	 */
	static void TickIncrease();

	/**
	 * @brief Return the current millisecond tick count.
	 */
	static uint32_t GetTick();

	/**
	 * @brief Blocking delay using the SysTick counter.
	 * @param delay  Delay in milliseconds.
	 */
	static void Delay_ms(uint32_t delay);

	/**
	 * @brief Enable the DWT cycle counter (required before Delay_us).
	 * @note  Not available on Cortex-M0/M0+ (STM32L0). A compile-time
	 *        warning is emitted if called on an unsupported target.
	 */
#if not defined(STM32L0) and not defined(STM32G0)
	static void Enable_CYCCNT();

	/**
	 * @brief Blocking delay using the DWT cycle counter.
	 * @param delay  Delay in microseconds.
	 * @note  Enable_CYCCNT() must be called before using this function.
	 *        Handles counter wrap-around correctly.
	 */
	static void Delay_us(uint32_t delay);

	/**
	 * @brief Write bytes to the ITM/SWO trace port.
	 * @param ptr  Pointer to data buffer (must not be nullptr).
	 * @param len  Number of bytes to send.
	 * @return     Number of bytes written.
	 */
	static uint32_t SWOTrace(const uint8_t *ptr, uint32_t len);
#endif

#if defined(STM32F7)
	/**
	 * @brief Initialise the MPU for STM32F7 targets.
	 */
	static void MPU_Init();
#endif
};

// ---------------------------------------------------------------------------
// Bit-banding helpers (STM32F4 only — Cortex-M4 with bit-band region)
// ---------------------------------------------------------------------------

#if defined(STM32F4)

/**
 * @brief Read a single bit from a peripheral register via bit-banding.
 */
__attribute__((always_inline))
static inline uint32_t BB_RD(volatile uint32_t *addr, uint8_t bitnum)
{
	volatile uint32_t *bitptr =
		reinterpret_cast<volatile uint32_t *>(
			(reinterpret_cast<uint32_t>(addr) - 0x40000000UL) * 32U
			+ bitnum * 4U
			+ 0x42000000UL);
	return *bitptr;
}

/**
 * @brief Write a single bit to a peripheral register via bit-banding.
 */
__attribute__((always_inline))
static inline void BB_WR(volatile uint32_t *addr, uint8_t bitnum, uint32_t value)
{
	volatile uint32_t *bitptr =
		reinterpret_cast<volatile uint32_t *>(
			(reinterpret_cast<uint32_t>(addr) - 0x40000000UL) * 32U
			+ bitnum * 4U
			+ 0x42000000UL);
	*bitptr = value;
}

/**
 * @brief Lvalue macro for bit-band access to a peripheral register bit.
 *        Parentheses around (bit) prevent operator-precedence bugs when
 *        a compound expression (e.g. pin & 7) is passed as the argument.
 */
#define BIT_BB(address, bit) \
	(*( (volatile uint32_t *)( PERIPH_BB_BASE \
		+ ((uint32_t)(address) - PERIPH_BASE) * 32U \
		+ (bit) * 4U ) ))

#endif // STM32F4

#endif // SYSTEM_H_
