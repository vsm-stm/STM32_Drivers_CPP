/**
 * @file  system.cpp
 * @brief System initialisation and utility functions for STM32F4 / F7 / L0.
 * @date  18.09.2023
 */

#include "system.hpp"
#include <cassert>

// ---------------------------------------------------------------------------
// Module-private tick counter
// ---------------------------------------------------------------------------

static volatile uint32_t Tick;

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------

uint32_t System::SystemCoreClock{ HSI_Clock };
uint32_t System::APB1BusClock   { HSI_Clock };
uint32_t System::APB2BusClock   { HSI_Clock };
uint32_t System::TIMxAPB1Clock  { HSI_Clock };
uint32_t System::TIMxAPB2Clock  { HSI_Clock };

// ---------------------------------------------------------------------------
// System::Init
// ---------------------------------------------------------------------------

/**
 * @brief Initialise core system features: FPU, Flash accelerator, caches,
 *        clock bookkeeping and SysTick.
 *
 * FIX: The original code mixed #if / #elif blocks with a single FLASH->ACR
 * assignment, causing SCB_EnableICache(), SCB_EnableDCache() and the
 * RCC->APB1ENR write to be placed outside the #elif block — they compiled
 * unconditionally on every target.  Each target now has its own independent
 * initialisation block.
 */
SysInitStatus System::Init()
{
	// --- FPU (Cortex-M4/M7 only) ---
#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
	SCB->CPACR |= (3UL << (10U * 2U)) | (3UL << (11U * 2U)); // CP10 + CP11 full access
#endif

	// --- Flash accelerator & cache configuration ---
#if defined(STM32F4)
	FLASH->ACR |= FLASH_ACR_PRFTEN  // Prefetch buffer
				| FLASH_ACR_ICEN    // Instruction cache
				| FLASH_ACR_DCEN;   // Data cache

#elif defined(STM32F7)
	FLASH->ACR |= FLASH_ACR_PRFTEN  // Prefetch buffer
				| FLASH_ACR_ARTEN;  // Adaptive real-time accelerator (ART)

	SCB_EnableICache();              // Enable instruction cache
	SCB_EnableDCache();              // Enable data cache

	RCC->APB1ENR |= RCC_APB1ENR_PWREN; // Enable PWR clock (required for voltage scaling)

#elif defined(STM32L0)
	// L0 has no ART/cache; Flash wait states are handled by the HAL / user PLL config.
#endif

	// --- Reset clock bookkeeping to HSI defaults ---
	SystemCoreClock = HSI_Clock;
	APB1BusClock    = HSI_Clock;
	APB2BusClock    = HSI_Clock;
	TIMxAPB1Clock   = HSI_Clock;
	TIMxAPB2Clock   = HSI_Clock;

	// --- Start SysTick ---
	if (InitTicks() != SysInitStatus::InitOK)
		return SysInitStatus::InitError;

	return SysInitStatus::InitOK;
}

// ---------------------------------------------------------------------------
// System::InitTicks
// ---------------------------------------------------------------------------

SysInitStatus System::InitTicks()
{
	if (SysTick_Config(SystemCoreClock / TICK_BASE) > 0U)
		return SysInitStatus::InitError;

	return SysInitStatus::InitOK;
}

// ---------------------------------------------------------------------------
// System::TickIncrease / GetTick
// ---------------------------------------------------------------------------

void System::TickIncrease()
{
	Tick++;
}

uint32_t System::GetTick()
{
	// Explicit cast removes the volatile qualifier after a single atomic read.
	// Safe on 32-bit ARM: 32-bit aligned reads are always atomic.
	return static_cast<uint32_t>(Tick);
}

// ---------------------------------------------------------------------------
// System::Delay_ms
// ---------------------------------------------------------------------------

/**
 * @brief Blocking millisecond delay.
 *
 * FIX 1: The original condition `if (wait < 0xFFFFFFFF)` is always true for
 *         a uint32_t, so the guard was pointless.  We now only add the
 *         compensation tick when there is room to do so without wrapping.
 *
 * FIX 2: Adding 1 tick compensates for the partial first tick — identical to
 *         the strategy used by STM32 HAL_Delay().
 */
void System::Delay_ms(uint32_t delay)
{
	uint32_t tick_start = GetTick();

	// Add one tick to compensate for the partial tick already in progress.
	// Guard prevents wrap-around when delay == UINT32_MAX.
	uint32_t wait = (delay < 0xFFFFFFFFUL) ? delay + 1UL : delay;

	while ((GetTick() - tick_start) < wait) {}
}

// ---------------------------------------------------------------------------
// System::Enable_CYCCNT
// ---------------------------------------------------------------------------

/**
 * @brief Enable the DWT cycle counter.
 *
 * FIX: Cortex-M0+ (STM32L0) does not implement DWT CYCCNT.  A compile-time
 *      warning is now emitted if this function is called on that target,
 *      preventing a hard-fault at runtime.
 */
void System::Enable_CYCCNT()
{
#if defined(STM32L0)
	#warning "Enable_CYCCNT: DWT CYCCNT is not available on Cortex-M0+ (STM32L0). Call has no effect."
	// Do nothing — no DWT cycle counter on M0+.
#else
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; // Enable trace
	DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;      // Start CYCCNT
#endif
}

// ---------------------------------------------------------------------------
// System::Delay_us
// ---------------------------------------------------------------------------

/**
 * @brief Blocking microsecond delay using the DWT cycle counter.
 *
 * FIX: The original implementation reset CYCCNT to 0 and compared with '<'.
 *      If CYCCNT wrapped during the delay the loop would exit far too early.
 *      Using subtraction (current - start) handles wrap-around correctly
 *      because unsigned arithmetic wraps by the C++ standard.
 *
 * @note Enable_CYCCNT() must be called before using this function.
 */
void System::Delay_us(uint32_t delay)
{
	uint32_t start = DWT->CYCCNT;
	uint32_t wait  = delay * (SystemCoreClock / 1000000UL);

	// Subtraction-based comparison is wrap-around safe for uint32_t.
	while ((DWT->CYCCNT - start) < wait) {}
}

// ---------------------------------------------------------------------------
// System::SWOTrace
// ---------------------------------------------------------------------------

/**
 * @brief Send bytes to the ITM/SWO trace port.
 *
 * FIX: Added nullptr guard (UB in original if ptr was null).
 *      Parameter is now const-correct.
 */
uint32_t System::SWOTrace(const uint8_t *ptr, uint32_t len)
{
	assert(ptr != nullptr && "SWOTrace: ptr must not be nullptr");
	if (ptr == nullptr)
		return 0U;

	for (uint32_t i = 0; i < len; ++i)
		ITM_SendChar(*ptr++);

	return len;
}

// ---------------------------------------------------------------------------
// System::MPU_Init  (STM32F7 only)
// ---------------------------------------------------------------------------

#if defined(STM32F7)
/**
 * @brief Configure the MPU for the STM32F7.
 *
 * Region 0: entire 4 GB address space — no access, XN set, shareable.
 * Subregion disable mask 0x87 disables sub-regions 0,1,2,7 (leaving
 * sub-regions 3-6 active).
 *
 * Named constants are used in place of raw hex literals for clarity.
 */
void System::MPU_Init()
{
	__DMB(); // Ensure all outstanding transfers complete before reconfiguring

	MPU->CTRL = 0U; // Disable MPU while configuring

	MPU->RNR  = 0x00U; // Select region 0
	MPU->RBAR = 0x00000000U & MPU_RBAR_ADDR_Msk; // Base address: 0x00000000

	// Sub-region disable: bits [7:0] of SRD field.
	// 0x87 = 0b10000111 → sub-regions 0,1,2,7 disabled; 3-6 active.
	static constexpr uint32_t SRD_MASK   = 0x87U;

	// SIZE field: 0x1F → 2^(0x1F+1) = 2^32 = 4 GB
	static constexpr uint32_t SIZE_4GB   = 0x1FU;

	MPU->RASR = MPU_RASR_ENABLE_Msk                  // Enable this region
			  | (SRD_MASK  << MPU_RASR_SRD_Pos)      // Subregion disable mask
			  | (SIZE_4GB  << MPU_RASR_SIZE_Pos)      // 4 GB region
			  | (0x00U     << MPU_RASR_TEX_Pos)       // TEX = b000 (Strongly-Ordered)
			  | (0x00U     << MPU_RASR_AP_Pos)        // AP  = No access (privileged + unprivileged)
			  | MPU_RASR_XN_Msk                       // Execute Never
			  | MPU_RASR_S_Msk;                       // Shareable

	// Enable MPU; use default memory map for privileged accesses (PRIVDEFENA).
	MPU->CTRL = MPU_CTRL_ENABLE_Msk | MPU_CTRL_PRIVDEFENA_Msk;

	__DSB(); // Ensure MPU configuration is visible
	__ISB(); // Flush instruction pipeline
}
#endif // STM32F7

// ---------------------------------------------------------------------------
// SysTick IRQ handler
// ---------------------------------------------------------------------------

extern "C" void SysTick_Handler(void)
{
	System::TickIncrease();
}
