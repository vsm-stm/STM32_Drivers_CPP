#pragma once

#include "common.hpp"

namespace flash_hw {

using status_t = uint32_t;
inline constexpr uint32_t record_header_words = 3U;

inline constexpr uint32_t write_width(uint32_t) { return 4U; }
inline constexpr uint32_t timeout_ms = 50000U;
inline constexpr uint32_t ready_mask = FLASH_SR_BSY;
inline constexpr uint32_t error_mask = FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR
	| FLASH_SR_OPTVERR | FLASH_SR_RDERR | FLASH_SR_NOTZEROERR | FLASH_SR_FWWERR;

#if defined(SYSCFG_CFGR1_UFB)
inline constexpr bool bank_mode_matches() { return !FLASH_BANK_SWITCHABLE; }
inline bool bank_swapped() { return syscfg_bank_swapped(SYSCFG->CFGR1, SYSCFG_CFGR1_UFB); }
#else
inline constexpr bool bank_mode_matches() { return !FLASH_BANK_SWITCHABLE && !FLASH_DUAL_BANK; }
inline constexpr bool bank_swapped() { return false; }
#endif

// Prefetch on L0 has no Flash cache reset sequence.
inline constexpr void invalidate_flash(uint32_t, uint32_t) {}

} // namespace flash_hw
