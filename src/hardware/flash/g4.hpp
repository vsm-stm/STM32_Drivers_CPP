#pragma once

#include "common.hpp"
#include "paged.hpp"

namespace flash_hw {

inline constexpr uint32_t busy_mask = FLASH_SR_BSY;
inline constexpr uint32_t ready_mask = busy_mask;

inline constexpr uint32_t bank_erase_bits(uint8_t banks)
{
	// In 128-bit single-bank mode, MER1 and MER2 must both be set.
	if constexpr (!FLASH_DUAL_BANK) return FLASH_CR_MER1 | second_bank_erase;
	return ((banks & 1U) ? FLASH_CR_MER1 : 0U) | ((banks & 2U) ? second_bank_erase : 0U);
}

inline bool bank_mode_matches()
{
#ifdef FLASH_OPTR_DBANK
	// A switchable part requires a map with both layouts and the correct page size.
	if constexpr (!FLASH_BANK_SWITCHABLE) return false;
	return ((FLASH->OPTR & FLASH_OPTR_DBANK) != 0U) == FLASH_DUAL_BANK;
#else
	return !FLASH_BANK_SWITCHABLE && !FLASH_DUAL_BANK;
#endif
}

inline bool bank_swapped()
{
	return syscfg_bank_swapped(SYSCFG->MEMRMP, SYSCFG_MEMRMP_FB_MODE);
}

// ES0430: disable the Flash data cache for operations that can overlap
// reads from the other bank. Reset it before restoring the original state.
inline bool pause_data_cache()
{
	const uint32_t acr = FLASH->ACR;
	if (!(acr & FLASH_ACR_DCEN)) return false;
	FLASH->ACR = acr & ~FLASH_ACR_DCEN;
	return true;
}

inline void resume_data_cache(bool was_enabled)
{
	if (!was_enabled) return;
	FLASH->ACR |= FLASH_ACR_DCRST;
	FLASH->ACR &= ~FLASH_ACR_DCRST;
	FLASH->ACR |= FLASH_ACR_DCEN;
	__DSB();
	__ISB();
}

inline void invalidate_flash(uint32_t, uint32_t)
{
	__DSB();
	reset_cache(FLASH_ACR_ICEN, FLASH_ACR_ICRST);
	reset_cache(FLASH_ACR_DCEN, FLASH_ACR_DCRST);
	__DSB();
	__ISB();
}

inline void invalidate_banks(uint8_t)
{
	invalidate_flash(0U, 0U);
}

} // namespace flash_hw
