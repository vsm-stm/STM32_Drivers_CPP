#pragma once

// Page commands and aligned doubleword programming shared by G0 and G4.
namespace flash_hw {

using status_t = uint64_t;
inline constexpr uint32_t record_header_words = 2U;
inline constexpr bool doubleword = true;

inline constexpr uint32_t write_width(uint32_t) { return 8U; }
inline constexpr uint32_t timeout_ms = 1000U;

inline constexpr uint32_t error_mask = FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_OPERR
	| FLASH_SR_PGSERR | FLASH_SR_PROGERR | FLASH_SR_SIZERR | FLASH_SR_MISERR
	| FLASH_SR_FASTERR | FLASH_SR_OPTVERR
#ifdef FLASH_SR_RDERR
	| FLASH_SR_RDERR
#endif
	;

#ifdef FLASH_CR_BKER
inline constexpr uint32_t bank_select = FLASH_CR_BKER;
#else
inline constexpr uint32_t bank_select = 0U;
#endif
inline constexpr uint32_t operation_mask = FLASH_CR_PG | FLASH_CR_PER | FLASH_CR_PNB_Msk
	| FLASH_CR_MER1 | second_bank_erase | bank_select | FLASH_CR_FSTPG;

inline constexpr uint32_t page_erase_bits(const FlashSector& page)
{
	return FLASH_CR_PER | ((page.number << FLASH_CR_PNB_Pos) & FLASH_CR_PNB_Msk)
		| (page.bank == 2U ? bank_select : 0U);
}

inline constexpr uint32_t program_bits(uint32_t) { return FLASH_CR_PG; }

template<uint32_t width>
inline void store(uint32_t addr, const uint8_t* data, uint32_t remaining)
{
	static_assert(width == 8U);
	uint64_t value = UINT64_MAX;
	memcpy(&value, data, remaining < width ? remaining : width);
	*reinterpret_cast<volatile uint32_t*>(addr) = static_cast<uint32_t>(value);
	__ISB();
	*reinterpret_cast<volatile uint32_t*>(addr + 4U) = static_cast<uint32_t>(value >> 32U);
}

} // namespace flash_hw
