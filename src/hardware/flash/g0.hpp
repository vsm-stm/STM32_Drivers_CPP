#pragma once

#include "common.hpp"
#include "paged.hpp"

namespace flash_hw {

inline constexpr uint32_t busy_mask = FLASH_SR_BSY1
#ifdef FLASH_SR_BSY2
	| FLASH_SR_BSY2
#endif
	;
inline constexpr uint32_t ready_mask = busy_mask | FLASH_SR_CFGBSY;

inline constexpr uint32_t bank_erase_bits(uint8_t banks)
{
	return ((banks & 1U) ? FLASH_CR_MER1 : 0U) | ((banks & 2U) ? second_bank_erase : 0U);
}

inline bool bank_mode_matches()
{
	if constexpr (!FLASH_BANK_SWITCHABLE) return true;
#if defined(STM32G0B1xx) || defined(STM32G0C1xx)
	return ((FLASH->OPTR & FLASH_OPTR_DUAL_BANK) != 0U) == FLASH_DUAL_BANK;
#else
	return false;
#endif
}

inline bool bank_swapped()
{
#if defined(STM32G0B1xx) || defined(STM32G0C1xx)
	return (FLASH->OPTR & FLASH_OPTR_nSWAP_BANK) == 0U;
#else
	return false;
#endif
}

inline constexpr bool pause_data_cache() { return false; }
inline constexpr void resume_data_cache(bool) {}

inline void invalidate_flash(uint32_t, uint32_t)
{
	__DSB();
	reset_cache(FLASH_ACR_ICEN, FLASH_ACR_ICRST);
	__DSB();
	__ISB();
}

inline void invalidate_banks(uint8_t)
{
	invalidate_flash(0U, 0U);
}

} // namespace flash_hw
