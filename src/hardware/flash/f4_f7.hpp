#pragma once

#include "common.hpp"

namespace flash_hw {

using status_t = uint8_t;
inline constexpr uint32_t record_header_words = 1U;
inline constexpr bool doubleword = false;

inline constexpr uint32_t write_width(uint32_t size) { return size == 8U ? 4U : size; }
inline constexpr uint32_t timeout_ms = 50000U;
inline constexpr uint32_t busy_mask = FLASH_SR_BSY;
inline constexpr uint32_t ready_mask = busy_mask;

inline constexpr uint32_t error_mask = FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR
#if defined(STM32F7)
	| FLASH_SR_OPERR | FLASH_SR_ERSERR
#else
	| FLASH_SR_SOP | FLASH_SR_PGSERR
#endif
#ifdef FLASH_SR_RDERR
	| FLASH_SR_RDERR
#endif
	;

inline constexpr uint32_t operation_mask = FLASH_CR_PG | FLASH_CR_SER | FLASH_CR_SNB_Msk
	| FLASH_CR_MER | second_bank_erase | FLASH_CR_PSIZE_Msk;

// At 3.0–3.3 V, program at most 32 bits per operation without external VPP.
inline constexpr uint32_t erase_parallelism = 2U << FLASH_CR_PSIZE_Pos;

inline constexpr uint32_t program_bits(uint32_t width)
{
	return FLASH_CR_PG | ((width >> 1U) << FLASH_CR_PSIZE_Pos);
}

inline constexpr uint32_t page_erase_bits(const FlashSector& sector)
{
	return FLASH_CR_SER | ((sector.number << FLASH_CR_SNB_Pos) & FLASH_CR_SNB_Msk) | erase_parallelism;
}

inline constexpr uint32_t bank_erase_bits(uint8_t banks)
{
	return ((banks & 1U) ? FLASH_CR_MER : 0U) | ((banks & 2U) ? second_bank_erase : 0U)
		| erase_parallelism;
}

inline bool bank_mode_matches()
{
	if constexpr (!FLASH_BANK_SWITCHABLE) return true;
#if defined(STM32F4) && defined(FLASH_OPTCR_DB1M)
	return ((FLASH->OPTCR & FLASH_OPTCR_DB1M) != 0U) == FLASH_DUAL_BANK;
#elif defined(STM32F7) && defined(FLASH_OPTCR_nDBANK)
	return ((FLASH->OPTCR & FLASH_OPTCR_nDBANK) == 0U) == FLASH_DUAL_BANK;
#else
	return false;
#endif
}

inline bool bank_swapped()
{
#if defined(SYSCFG_MEMRMP_UFB_MODE)
	return syscfg_bank_swapped(SYSCFG->MEMRMP, SYSCFG_MEMRMP_UFB_MODE);
#elif defined(SYSCFG_MEMRMP_SWP_FB)
	return syscfg_bank_swapped(SYSCFG->MEMRMP, SYSCFG_MEMRMP_SWP_FB);
#else
	return false;
#endif
}

template<uint32_t width>
inline void store(uint32_t addr, const uint8_t* data, uint32_t)
{
	uint32_t value = 0;
	memcpy(&value, data, width);
	if constexpr (width == 1U) *reinterpret_cast<volatile uint8_t*>(addr) = static_cast<uint8_t>(value);
	else if constexpr (width == 2U) *reinterpret_cast<volatile uint16_t*>(addr) = static_cast<uint16_t>(value);
	else *reinterpret_cast<volatile uint32_t*>(addr) = value;
}

inline constexpr bool pause_data_cache() { return false; }
inline constexpr void resume_data_cache(bool) {}

#if defined(STM32F7)
inline void invalidate_flash(uint32_t addr, uint32_t bytes)
{
	__DSB();
	if (SCB->CCR & SCB_CCR_DC_Msk) {
		const uint32_t first = addr & ~31U;
		const uint32_t length = (addr - first + bytes + 31U) & ~31U;
		SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(first), static_cast<int32_t>(length));
	}
	if (FLASH->ACR & FLASH_ACR_ARTEN) {
		FLASH->ACR &= ~FLASH_ACR_ARTEN;
		FLASH->ACR |= FLASH_ACR_ARTRST;
		FLASH->ACR &= ~FLASH_ACR_ARTRST;
		FLASH->ACR |= FLASH_ACR_ARTEN;
	}
	if (SCB->CCR & SCB_CCR_IC_Msk) SCB_InvalidateICache();
	__DSB();
	__ISB();
}

// Derive each selected bank's address span from the generated geometry.
// Never assume equal contiguous bank sizes in this helper.
inline void invalidate_banks(uint8_t banks)
{
	for (uint32_t bank = 1U; bank <= 2U; ++bank) {
		if (!(banks & (1U << (bank - 1U)))) continue;
		uint32_t first = UINT32_MAX;
		uint32_t last = 0;
		for (uint32_t index = 0; index < FLASH_SECTOR_COUNT; ++index) {
			const FlashSector sector = GetFlashSector(index);
			if (sector.bank != bank) continue;
			if (sector.address < first) first = sector.address;
			const uint32_t end = sector.address + sector.size;
			if (end > last) last = end;
		}
		if (first != UINT32_MAX) invalidate_flash(first, last - first);
	}
}
#else
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
	invalidate_flash(0U, 0U); // Flash accelerator cache is reset as a whole.
}
#endif

} // namespace flash_hw
