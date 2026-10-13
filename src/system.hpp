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

/// Frequency viewing convenience constants.
static constexpr uint32_t kHz = 1000U;
static constexpr uint32_t MHz = 1000000U;

/// SysTick interrupt frequency in Hz (1 kHz → 1 ms resolution).
static constexpr uint32_t TICK_BASE = 1*kHz;

/// Default HSI oscillator frequency.
static constexpr uint32_t HSI_Clock = 16*MHz;

/// Default LSI oscillator frequency.
static constexpr uint32_t LSI_Clock = 32*kHz;
static constexpr uint32_t LSE_STARTUP_TIMEOUT = 5000UL; //ms;

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
	 * @brief Core/AHB frequency. Peripheral kernel clocks such as USB are separate.
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
	 * @brief Initialise HSI system clocking, FPU, Flash/cache and SysTick.
	 * @note F4/F7/G0: waits for HSI and the initial SYSCLK switch without a
	 *       timeout, restores bus dividers /1 and starts SysTick at 16 MHz.
	 *       Sets Flash latency 0; G0 first restores VOS Range 1. These waits
	 *       require serviceable SysTick and return InitError on timeout (100 ms).
	 *       PLL/HSE configuration and RCC interrupts are retained.
	 *       Call at the beginning of main with interrupts enabled, before
	 *       peripheral setup. L0 retains its legacy assumption of 16 MHz HSI;
	 *       it does not restore RCC, power or Flash settings.
	 *       F7 applies project DMA areas to their MPU regions before enabling
	 *       D-cache. An already enabled D-cache is disabled with clean/invalidate
	 *       first. Call before DMA starts; other MPU regions are retained.
	 */
	static SysInitStatus Init();

	/**
	 * @brief Configure SysTick at TICK_BASE Hz and its fixed lowest IRQ priority.
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
	 * @brief Blocking delay using a running SysTick counter.
	 * @param delay  Delay in milliseconds.
	 * @note The wait includes one compensating tick and is capped at
	 *       0x7FFFFFFF ticks (about 24.9 days at the default 1 kHz tick rate).
	 * @note SysTick must be able to run during this call: do not call with
	 *       IRQs masked or from an IRQ of equal/higher preemption priority.
	 *       The counter loses elapsed ticks while interrupts are masked for
	 *       multiple periods. No IPSR/PRIMASK checks or fallback timer are used.
	 */
	static void Delay_ms(uint32_t delay);

	/**
	 * @brief Enable the DWT cycle counter (required before Delay_us).
	 * @note Not declared on STM32G0/L0 (Cortex-M0+). This does not configure SWO.
	 */
#if not defined(STM32L0) && not defined(STM32G0)
	static void Enable_CYCCNT();

	/**
	 * @brief Blocking delay using the DWT cycle counter.
	 * @param delay  Delay in microseconds.
	 * @note Enable_CYCCNT() must be called first; SystemCoreClock must be current.
	 *       Rounds up fractional cycles and clamps to 0x7FFFFFFF core cycles.
	 *       The clock must remain unchanged during the delay. Counter subtraction
	 *       handles wrap-around provided polling resumes within half the counter range.
	 */
	static void Delay_us(uint32_t delay);

	/**
	 * @brief Non-blocking write to ITM channel 0. Stops at the first unready port.
	 * @param ptr Pointer to data; nullptr returns zero.
	 * @param len Number of bytes requested.
	 * @return Number of bytes written into ITM, possibly fewer than len.
	 * @note The debugger must configure and enable ITM/SWO. Does not wait
	 *       for transmission or use SysTick; hardware receipt is not confirmed.
	 */
	static uint32_t SWOTrace(const uint8_t *ptr, uint32_t len);
#endif

#if defined(STM32F7)
	/**
	 * @brief Guard unused external memory addresses on STM32F7 using MPU region 0.
	 * @note Blocks 0x60000000..0xDFFFFFFF. Configure higher-numbered regions
	 *       for external memory you use, before enabling caches. Other regions
	 *       are retained. Does not create a non-cacheable DMA memory area.
	 *       See System.md for the memory map and a region configuration example.
	 */
	static void MPU_Init();
#endif

	// ---------------------------------------------------------------------------
	// Debug assertion
	// ---------------------------------------------------------------------------

	/**
	 * @brief Stop execution after reporting a diagnostic in a DEBUG build.
	 * @note DEBUG saves the message pointer in _debug_trap_msg, sends it through
	 *       blocking ITM channel 0 on F4/F7 when trace is enabled, then executes
	 *       BKPT and loops forever. The debugger must configure SWO; an enabled
	 *       but stalled ITM port can hold this call before BKPT. G0/L0 have no SWO.
	 *       Release ignores the message and loops forever; elimination of the
	 *       caller's string literals depends on compiler/linker optimisation.
	 *
	 * @param msg  Null-terminated ASCII description of the fault.
	 *             Must remain readable; nullptr skips the output.
	 */
	[[noreturn]] static void DebugTrap(const char* msg);
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
