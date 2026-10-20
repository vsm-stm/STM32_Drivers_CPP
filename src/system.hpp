/**
 * @file system.hpp
 * @brief Startup-facing clock state, tick delays, trace and F7 cache/MPU helpers.
 *
 * Typical order in main, after the provided startup initialized RAM/constructors:
 * @code
 *   if (System::Init() != SysInitStatus::InitOK)
 *       System::DebugTrap("System init");
 *   // Apply one ClockSystem::Init<config>() before configuring peripherals.
 *   uint32_t previous = System::GetTick();
 *   // In the application loop:
 *   if (System::GetTick() - previous >= 1000U) {
 *       previous = System::GetTick();
 *       // Nominal one-second work; unsigned subtraction handles counter wrap.
 *   }
 * @endcode
 * The driver's SysTick_Handler calls TickIncrease(); do not define another
 * handler when linking this implementation. Time-based waits need SysTick to
 * preempt them. No IPSR/PRIMASK guard, fallback timer or internal lock is added.
 * F4/F7/G0 are the configured driver families; L0 retains a limited legacy path.
 */
#ifndef SYSTEM_H_
#define SYSTEM_H_

#include <stdint.h>
#include <cstddef>

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

/// Target SysTick frequency in Hz; integer reload can change the actual rate.
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

/**
 * @brief Shared bare-metal core utilities; all methods/state are static.
 * Published clocks are software values maintained by System/ClockSystem,
 * not live RCC register measurements. Peripheral kernel selectors can differ
 * from the APB clock, so their consumers must select/derive the correct source.
 */
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
	 * @brief Initialise clock state, FPU, Flash/cache and SysTick at 16 MHz.
	 * @note With STM32_USE_BOOTLOADER on F4/F7/G0, restores HSI and dividers /1;
	 *       HSI readiness and source switch wait without a timeout. G0 restores
	 *       VOS Range 1, then Flash latency becomes 0; VOS/latency waits have
	 *       a 100 ms timeout. PLL/HSE configuration and RCC interrupts remain.
	 *       Without that definition, assumes the reset clock/power/latency state;
	 *       call once after hardware reset, before changing clock settings.
	 *       Call at the beginning of main with interrupts enabled, before
	 *       peripheral setup. L0 retains its legacy assumption of 16 MHz HSI;
	 *       it does not restore RCC, power or Flash settings.
	 *       F7 applies project DMA areas to their MPU regions before enabling
	 *       D-cache. An already enabled D-cache is disabled with clean/invalidate
	 *       first. Call before DMA starts; other MPU regions are retained.
	 * @return InitOK on success; InitError if clock/tick/power preparation fails.
	 * @note STM32_USE_BOOTLOADER is supplied by the base's project.json handling,
	 *       not a request to reset PLL/HSE or all peripherals. The startup can
	 *       define STM32_STARTUP_ENABLES_FPU to omit duplicate FPU enabling here.
	 */
	static SysInitStatus Init();

	/**
	 * @brief Configure SysTick using floor(SystemCoreClock/TICK_BASE) cycles.
	 * @note Uses the lowest IRQ priority. A non-integer division changes the
	 *       actual tick rate; see RCC.md for the LSE and low-HCLK cases.
	 */
	static SysInitStatus InitTicks();

	/**
	 * @brief Configure SysTick with a precomputed number of HCLK cycles per tick.
	 * @note Used by RCC to keep frequency division at compile time.
	 */
	static SysInitStatus InitTicks(uint32_t ticks);

	/**
	 * @brief Increment the software tick counter — call from SysTick_Handler.
	 */
	static void TickIncrease();

	/**
	 * @brief Return the unsigned 32-bit software tick count (nominal milliseconds).
	 * It wraps modulo 2^32; compare intervals by unsigned subtraction.
	 * Reading does not clear the counter or require interrupt masking.
	 * Integer SysTick reload can make the actual tick rate differ from 1 kHz.
	 */
	static uint32_t GetTick();

	/**
	 * @brief Wait until (reg & mask) equals value; return false on timeout.
	 * @param value Expected masked value: 0 for cleared bits, mask for all set.
	 * @param timeout Maximum wait in nominal milliseconds; 0 checks once.
	 * @note SysTick must run during the wait, as required by Delay_ms().
	 *       value must contain only bits inside mask. This checks equality of
	 *       the entire masked field, not whether any selected bit is nonzero.
	 * @code
	 *   bool ready = System::WaitBits(RCC->CR, RCC_CR_HSERDY,
	 *                                 RCC_CR_HSERDY, 100);
	 * @endcode
	 * Enabling the source and handling a false result belong to the caller.
	 */
	static bool WaitBits(volatile uint32_t& reg, uint32_t mask, uint32_t value, uint32_t timeout);

	/**
	 * @brief Blocking delay in nominal milliseconds using a running SysTick.
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
	 * @code
	 *   System::Enable_CYCCNT(); // F4/F7, after the core frequency is established.
	 *   System::Delay_us(10);
	 * @endcode
	 */
	static void Delay_us(uint32_t delay);

	/**
	 * @brief Non-blocking write to ITM channel 0. Stops at the first unready port.
	 * @param ptr Pointer to data; nullptr returns zero.
	 * @param len Number of bytes requested.
	 * @return Number of bytes written into ITM, possibly fewer than len.
	 * @note The debugger must configure and enable ITM/SWO. Does not wait
	 *       for transmission or use SysTick; hardware receipt is not confirmed.
	 * @code
	 *   static const uint8_t message[] = "ready\n";
	 *   uint32_t sent = System::SWOTrace(message, sizeof(message) - 1);
	 *   // Retry or drop the unsent suffix according to application policy.
	 * @endcode
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
	 * @code
	 *   System::MPU_Init(); // F7: optional external-address guard, before cache enable.
	 *   auto status = System::Init(); // Applies project DMA regions, then caches.
	 * @endcode
	 * External RAM/QSPI actually used in the guarded range needs an appropriate
	 * higher-priority region and its controller initialized separately.
	 */
	static void MPU_Init();
#endif

	/**
	 * @brief Cache maintenance for a byte range used by a peripheral driver.
	 * @note F7 calls the CMSIS operations for the intersecting 32-byte lines.
	 *       A range wholly inside a project's non-cacheable DMA area is skipped.
	 *       Call after Init(); retain the configured MPU attributes afterwards.
	 *       The caller supplies a valid range fitting the CMSIS signed byte count
	 *       including line rounding. nullptr or zero size performs no operation.
	 *       RX buffers must own every affected cache line, including padding;
	 *       do not share those lines with independently modified data.
	 *       F4/G0/L0 have no SRAM D-cache; these functions perform no operation.
	 *
	 * Ownership transitions:
	 * - CPU -> TX DMA: CacheClean (write dirty source bytes to RAM).
	 * - CPU -> RX DMA: CacheCleanInvalidate (remove old/dirty destination lines).
	 * - Stopped RX DMA -> CPU: CacheInvalidate (discard stale CPU copies).
	 * Stop DMA and confirm EN=0 before the final invalidate/read. An invalidate
	 * does not preserve dirty CPU changes, so do not access an owned RX buffer.
	 *
	 * @code
	 *   alignas(32) static uint8_t buffer[64];
	 *   System::CacheCleanInvalidate(buffer, 37); // Before RX, after System::Init().
	 *   // Prepare/start DMA for 37 bytes; CPU leaves all touched lines alone.
	 *   // Stop DMA, wait for EN=0, then obtain the actual transferred byte count.
	 *   System::CacheInvalidate(buffer, received_bytes);
	 *   // CPU may now inspect buffer[0..received_bytes).
	 * @endcode
	 * received_bytes comes from the peripheral's completed transfer. Automatic
	 * non-cacheable detection uses the project's linker DMA records, not a
	 * general scan of arbitrary user MPU mappings.
	 */
#if defined(STM32F7)
	static void CacheClean(const void* addr, uint32_t size);
	static void CacheInvalidate(void* addr, uint32_t size);
	static void CacheCleanInvalidate(void* addr, uint32_t size);
#else
	static void CacheClean(const void*, uint32_t) {}
	static void CacheInvalidate(void*, uint32_t) {}
	static void CacheCleanInvalidate(void*, uint32_t) {}
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
	 *       Release uses an inline body which ignores the message and loops forever.
	 *       This lets optimisation discard unused diagnostic strings at call sites.
	 *       This function is deliberately not constexpr: a failing constructor
	 *       check cannot be evaluated in a constinit/consteval initializer.
	 *
	 * @param msg  Null-terminated ASCII description of the fault.
	 *             Must remain readable; nullptr skips the output.
	 */
#ifdef DEBUG
	[[noreturn]] static void DebugTrap(const char* msg);
#else
	[[noreturn]] static inline void DebugTrap(const char*) { while (1) {} }
#endif
};

namespace stm32 {

/** @brief Structural string value: the reason appears in template diagnostics. */
template<std::size_t N>
struct Text {
	char s[N];
	consteval Text(const char (&message)[N])
	{
		for (std::size_t i = 0; i < N; ++i) s[i] = message[i];
	}
};

/**
 * @brief Reject an invalid constant configuration with the reason in diagnostics.
 * Deliberately not constexpr: constinit/consteval cannot call this function.
 * GCC includes the reason template argument in the diagnostic. A runtime
 * constructor reaches the same DebugTrap behavior as a direct call.
 */
template<Text reason>
[[noreturn]] void CONFIGURATION_ERROR()
{
	System::DebugTrap(reason.s);
}

} // namespace stm32

#define STM32_CONFIG_ERROR(text) ::stm32::CONFIGURATION_ERROR<text>()

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
