/**
 * @file  system.cpp
 * @brief System initialisation, timekeeping and debug utilities.
 */

#include "system.hpp"

#if defined(STM32F7)
// Linker records: MPU region number, base address, size in bytes.
// Weak symbols keep System usable with linker scripts that have no DMA areas.
extern "C" {
	extern const uint32_t __stm32_dma_regions_start__[] __attribute__((weak));
	extern const uint32_t __stm32_dma_regions_end__[] __attribute__((weak));
}
#endif

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

#if defined(STM32F7)
// Project records are triples: MPU number/base/size. Region 0 remains reserved;
// configuration validates 1..7, power-of-two sizes and aligned SRAM1/SRAM2 tails.
// Keep custom non-project MPU regions, and preserve existing dirty RAM data.
// Apply the project's non-cacheable DMA regions before enabling D-cache.
static void InitDMARegions()
{
	if (!__stm32_dma_regions_start__ || !__stm32_dma_regions_end__
	 || &__stm32_dma_regions_start__[0] == &__stm32_dma_regions_end__[0])
		return;

	// CMSIS cleans/invalidates D-cache when disabling it, retaining dirty data.
	if (SCB->CCR & SCB_CCR_DC_Msk)
		SCB_DisableDCache();

	const uint32_t mpu_ctrl = MPU->CTRL;
	__DMB();
	MPU->CTRL = 0U;
	for (const uint32_t* p = __stm32_dma_regions_start__;
		 p != __stm32_dma_regions_end__; p += 3) {
		MPU->RNR = p[0];
		MPU->RBAR = p[1];
		// Project configuration checks power-of-two sizes and base alignment.
		MPU->RASR = MPU_RASR_ENABLE_Msk
			| ((30U - __CLZ(p[2])) << MPU_RASR_SIZE_Pos)
			| (3U << MPU_RASR_AP_Pos)  // Full read/write access
			| (1U << MPU_RASR_TEX_Pos) // Normal, non-cacheable (C=0, B=0)
			| MPU_RASR_S_Msk | MPU_RASR_XN_Msk;
	}
	// Retain other regions and control flags; enable the privileged default map.
	MPU->CTRL = mpu_ctrl | MPU_CTRL_ENABLE_Msk | MPU_CTRL_PRIVDEFENA_Msk;
	__DSB();
	__ISB();
}
#endif

// Publish HSI frequencies and start the millisecond time base.
static SysInitStatus InitClockState()
{
	System::SystemCoreClock = System::APB1BusClock = System::TIMxAPB1Clock = HSI_Clock;
#if defined(STM32G0)
	System::APB2BusClock = System::TIMxAPB2Clock = 0U; // G0 has no APB2 bus
#else
	System::APB2BusClock = System::TIMxAPB2Clock = HSI_Clock;
#endif
	return System::InitTicks(HSI_Clock / TICK_BASE);
}

#if defined(STM32_USE_BOOTLOADER) && (defined(STM32F4) || defined(STM32F7) || defined(STM32G0))
// Restore HSI, bus dividers, G0 power and Flash latency after the bootloader.
// This is clock handover preparation, not an RCC/peripheral factory reset.
// HSI and the actual source switch precede timed waits because no trustworthy
// software tick is available yet. Once HSI /1 is published, VOS/latency use 100 ms.
static SysInitStatus InitAfterBootloader()
{
	// Stop the previous time base while its input clock is being changed.
	SysTick->CTRL = 0U;
	SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
	RCC->CR |= RCC_CR_HSION;
	while (!(RCC->CR & RCC_CR_HSIRDY)) {}

#if defined(STM32G0)
	// HSI /1 can reach 16 MHz even if a previous setup left VOS in Range 2.
	// Keep existing higher latency; raise 0 WS to 1 WS before the switch.
	if ((FLASH->ACR & FLASH_ACR_LATENCY_Msk) == 0U) {
		FLASH->ACR |= FLASH_ACR_LATENCY_0;
		if ((FLASH->ACR & FLASH_ACR_LATENCY_Msk) == 0U)
			return SysInitStatus::InitError;
	}
#endif
	// Preserve prescalers until the hardware confirms HSI as SYSCLK.
	RCC->CFGR &= ~RCC_CFGR_SW_Msk;
	while ((RCC->CFGR & RCC_CFGR_SWS_Msk) != 0U) {}
#if defined(STM32G0)
	RCC->CFGR &= ~(RCC_CFGR_HPRE_Msk | RCC_CFGR_PPRE_Msk);
	RCC->CR &= ~RCC_CR_HSIDIV_Msk;
#else
	RCC->CFGR &= ~(RCC_CFGR_HPRE_Msk | RCC_CFGR_PPRE1_Msk | RCC_CFGR_PPRE2_Msk);
#endif
	if (InitClockState() != SysInitStatus::InitOK)
		return SysInitStatus::InitError;

	static constexpr uint32_t INIT_TIMEOUT_MS = 100U;
	uint32_t tick_start;

#if defined(STM32G0)
	RCC->APBENR1 |= RCC_APBENR1_PWREN;
	(void)RCC->APBENR1; // Allow the PWR clock enable to reach the peripheral.
	PWR->CR1 = (PWR->CR1 & ~PWR_CR1_VOS_Msk) | PWR_CR1_VOS_0;
	tick_start = System::GetTick();
	while (PWR->SR2 & PWR_SR2_VOSF) {
		if ((System::GetTick() - tick_start) > INIT_TIMEOUT_MS)
			return SysInitStatus::InitError;
	}
#endif

	// Reduce wait states only after HSI /1 and (on G0) VOS Range 1 are ready.
	FLASH->ACR &= ~FLASH_ACR_LATENCY_Msk;
	tick_start = System::GetTick();
	while (FLASH->ACR & FLASH_ACR_LATENCY_Msk) {
		if ((System::GetTick() - tick_start) > INIT_TIMEOUT_MS)
			return SysInitStatus::InitError;
	}

	return SysInitStatus::InitOK;
}
#endif

// ---------------------------------------------------------------------------
// System::Init
// ---------------------------------------------------------------------------

/**
 * @brief Initialise reset clock state, SysTick and core features.
 * With STM32_USE_BOOTLOADER, restore HSI clocking and power/latency first.
 * Otherwise the first call must follow hardware reset, before clock setup.
 */
SysInitStatus System::Init()
{
	// --- FPU (Cortex-M4/M7 only) ---
#ifndef STM32_STARTUP_ENABLES_FPU
#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
	SCB->CPACR |= (3UL << (10U * 2U)) | (3UL << (11U * 2U)); // CP10 + CP11 full access
	__DSB();
	__ISB();
#endif
#endif

#if defined(STM32_USE_BOOTLOADER) && (defined(STM32F4) || defined(STM32F7) || defined(STM32G0))
	if (InitAfterBootloader() != SysInitStatus::InitOK)
		return SysInitStatus::InitError;
#else
	if (InitClockState() != SysInitStatus::InitOK)
		return SysInitStatus::InitError;
#endif

	// --- Flash accelerator & cache configuration ---
#if defined(STM32F4)
	FLASH->ACR |= FLASH_ACR_PRFTEN  // Prefetch buffer
				| FLASH_ACR_ICEN    // Instruction cache
				| FLASH_ACR_DCEN;   // Data cache

#elif defined(STM32F7)
	FLASH->ACR |= FLASH_ACR_PRFTEN  // Prefetch buffer
				| FLASH_ACR_ARTEN;  // Adaptive real-time accelerator (ART)

	InitDMARegions();

	SCB_EnableICache();              // Enable instruction cache
	SCB_EnableDCache();              // Enable data cache

	RCC->APB1ENR |= RCC_APB1ENR_PWREN; // Enable PWR clock (required for voltage scaling)
#elif defined(STM32G0)
	FLASH->ACR |= FLASH_ACR_PRFTEN  // Prefetch buffer
				| FLASH_ACR_ICEN;   // Instruction cache
#elif defined(STM32L0)
	// Legacy path: assumes 16 MHz HSI already selected; RCC/latency are not reset.
#endif

	return SysInitStatus::InitOK;
}

// ---------------------------------------------------------------------------
// System::InitTicks
// ---------------------------------------------------------------------------

SysInitStatus System::InitTicks()
{
	return InitTicks(SystemCoreClock / TICK_BASE);
}

SysInitStatus System::InitTicks(uint32_t ticks)
{
	if (SysTick_Config(ticks) > 0U)
		return SysInitStatus::InitError;

	return SysInitStatus::InitOK;
}

// ---------------------------------------------------------------------------
// System::TickIncrease / GetTick
// ---------------------------------------------------------------------------

void System::TickIncrease()
{
	Tick = Tick + 1;
}

uint32_t System::GetTick()
{
	// One aligned volatile read; reading the tick does not require a lock.
	return static_cast<uint32_t>(Tick);
}

// One unsigned elapsed-tick budget, also allowing a single check at timeout zero.
// Register reads remain volatile; the expected value is already a masked field.
bool System::WaitBits(volatile uint32_t& reg, uint32_t mask, uint32_t value, uint32_t timeout)
{
	const uint32_t start = GetTick();
	while ((reg & mask) != value)
		if ((GetTick() - start) >= timeout) return false;
	return true;
}

// ---------------------------------------------------------------------------
// System::Delay_ms
// ---------------------------------------------------------------------------

/**
 * @brief Blocking delay in nominal milliseconds.
 *
 * SysTick must be running and able to interrupt this call. In particular,
 * do not wait here from an IRQ with equal/higher priority than SysTick, or
 * with SysTick masked. Adding one tick accounts for the partial first tick.
 */
void System::Delay_ms(uint32_t delay)
{
	uint32_t tick_start = GetTick();

	// Add one tick to compensate for the partial tick already in progress.
	// Clamp the compensated wait to half the tick counter range.
	const uint32_t wait = (delay < 0x7FFFFFFFUL) ? delay + 1UL : 0x7FFFFFFFUL;

	while ((GetTick() - tick_start) < wait) {}
}

#if not defined(STM32L0) && not defined(STM32G0)
// ---------------------------------------------------------------------------
// System::Enable_CYCCNT
// ---------------------------------------------------------------------------

/**
 * @brief Enable the DWT cycle counter.
 *
 * Available on F4/F7. It enables the cycle counter, not the ITM/SWO output.
 */
void System::Enable_CYCCNT()
{
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; // Enable trace
#if defined(STM32F7)
	DWT->LAR = 0xC5ACCE55UL;                      // Unlock Cortex-M7 DWT writes
#endif
	DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;      // Start CYCCNT
}

// ---------------------------------------------------------------------------
// System::Delay_us
// ---------------------------------------------------------------------------

/**
 * @brief Blocking microsecond delay using the DWT cycle counter.
 *
 * Round up fractional cycles and clamp the interval to 0x7FFFFFFF cycles.
 * Unsigned subtraction handles one counter wrap; the half-range limit leaves
 * time for the polling loop to observe completion.
 *
 * @note Enable_CYCCNT() must be called before using this function.
 */
void System::Delay_us(uint32_t delay)
{
	const uint64_t cycles = (static_cast<uint64_t>(delay) * SystemCoreClock
							 + 999999ULL) / 1000000ULL;
	const uint32_t wait = cycles > 0x7FFFFFFFULL
		? 0x7FFFFFFFUL : static_cast<uint32_t>(cycles);
	const uint32_t start = DWT->CYCCNT;

	// Subtraction-based comparison is wrap-around safe for uint32_t.
	while ((DWT->CYCCNT - start) < wait) {}
}

// ---------------------------------------------------------------------------
// System::SWOTrace
// ---------------------------------------------------------------------------

/**
 * @brief Write as many bytes as the ITM channel accepts without waiting.
 */
uint32_t System::SWOTrace(const uint8_t *ptr, uint32_t len)
{
	if (ptr == nullptr)
		return 0U;

	uint32_t written = 0;
	while (written < len) {
		if (!(CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk)
		 || !(ITM->TCR & ITM_TCR_ITMENA_Msk) || !(ITM->TER & 1UL)
		 || ITM->PORT[0].u32 == 0UL) break;
		ITM->PORT[0].u8 = ptr[written++];
	}
	return written;
}
#endif // STM32L0 or STM32G0
// ---------------------------------------------------------------------------
// System cache maintenance / MPU_Init (STM32F7 only)
// ---------------------------------------------------------------------------

#if defined(STM32F7)
// Project DMA regions use the same linker records as InitDMARegions().
// Only full containment within one configured region bypasses maintenance;
// a mixed/cross-region range takes the conservative CMSIS line operation.
// This is not a run-time MPU attribute decoder.
static bool CacheMaintenanceNeeded(const void* addr, uint32_t size)
{
	if (!addr || !size || !(SCB->CCR & SCB_CCR_DC_Msk)) return false;
	if (__stm32_dma_regions_start__ && __stm32_dma_regions_end__) {
		const uintptr_t base = reinterpret_cast<uintptr_t>(addr);
		for (const uint32_t* p = __stm32_dma_regions_start__;
			 p != __stm32_dma_regions_end__; p += 3) {
			if (base >= p[1] && base - p[1] < p[2]
			 && size <= p[2] - (base - p[1])) return false;
		}
	}
	return true;
}

// CMSIS handles 32-byte line coverage itself; do not round the byte count again.
void System::CacheClean(const void* addr, uint32_t size)
{
	if (CacheMaintenanceNeeded(addr, size))
		SCB_CleanDCache_by_Addr(const_cast<void*>(addr), static_cast<int32_t>(size));
}

void System::CacheInvalidate(void* addr, uint32_t size)
{
	if (CacheMaintenanceNeeded(addr, size))
		SCB_InvalidateDCache_by_Addr(addr, static_cast<int32_t>(size));
}

void System::CacheCleanInvalidate(void* addr, uint32_t size)
{
	if (CacheMaintenanceNeeded(addr, size))
		SCB_CleanInvalidateDCache_by_Addr(addr, static_cast<int32_t>(size));
}

/**
 * @brief Configure the MPU for the STM32F7.
 *
 * Region 0 guards 0x60000000..0xDFFFFFFF against accesses to unused external
 * memory, including speculative accesses. Higher-numbered regions must
 * explicitly permit any SDRAM, memory-mapped QSPI or other mapping in that
 * range. Call before enabling caches. This does not configure DMA buffers.
 */
void System::MPU_Init()
{
	__DMB(); // Order preceding memory accesses before changing the MPU map

	MPU->CTRL = 0U; // Disable MPU while configuring

	MPU->RNR  = 0x00U; // Overwrites region 0; other region configurations remain
	MPU->RBAR = 0x00000000U & MPU_RBAR_ADDR_Msk; // Base address: 0x00000000

	// Sub-region disable: bits [7:0] of SRD field.
	// Each subregion is 512 MiB: only 3-6 (0x60000000..0xDFFFFFFF) are guarded.
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
// System::DebugTrap
// ---------------------------------------------------------------------------

#ifdef DEBUG

/// Last message passed to DebugTrap — inspect in debugger Watch / Memory view.
static const char* volatile _debug_trap_msg = nullptr;

[[noreturn]] void System::DebugTrap(const char* msg)
{
	_debug_trap_msg = msg;          // visible in debugger even without call-stack
#if defined(STM32F4) || defined(STM32F7)
	// Fault reporting deliberately waits for ITM; it does not depend on SysTick.
	// When trace is disabled the message is available only in the debugger.
	if (msg != nullptr && (CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk)
	 && (ITM->TCR & ITM_TCR_ITMENA_Msk) && (ITM->TER & 1UL)) {
		while (*msg != '\0') ITM_SendChar(static_cast<uint8_t>(*msg++));
	}
#endif
	__BKPT(0);                      // Breakpoint for an attached debugger
	while (1) {}
}

#else // Release ignores the message; removal of string literals depends on the build.

[[noreturn]] void System::DebugTrap(const char*)
{
	while (1) {}
}

#endif // DEBUG

// ---------------------------------------------------------------------------
// SysTick IRQ handler
// ---------------------------------------------------------------------------

extern "C" void SysTick_Handler(void)
{
	System::TickIncrease();
}
