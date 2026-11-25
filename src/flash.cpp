/**
 * @file flash.cpp
 * @brief Blocking Flash operations and fixed-stride single-sector journal.
 *
 * The driver never masks interrupts. SysTick-based waits can be delayed by
 * same-bank instruction/vector fetch stalls; timeout is not hardware cancellation.
 * Destination cache invalidation prevents stale Flash reads; F4/G0/G4 accelerator
 * resets are distinct from Cortex-M7 SRAM/data-cache range maintenance.
 */
#include "flash.hpp"

namespace {
using namespace flash_hw;

constexpr uint32_t record_header_size = record_header_words * sizeof(status_t);
constexpr uint32_t commit_offset = record_header_words == 3U ? sizeof(status_t) : 0U;

} // namespace

SysInitStatus flash_base::init(bank_type tb)
{
	const bank_type expected = FLASH_DUAL_BANK ? bank_type::DUAL_BANK : bank_type::SINGLE_BANK;
	if (tb != expected || !bank_mode_matches()) return SysInitStatus::InitError;
	if constexpr (FLASH_DUAL_BANK) {
		if (bank_swapped()) return SysInitStatus::InitError;
	}
	return SysInitStatus::InitOK;
}

bool flash_base::ready()
{
	return (FLASH->SR & ready_mask) == 0U;
}

void flash_base::clear_errors()
{
	// W1C: clear only defined completion/error flags, not reserved SR bits.
	FLASH->SR = error_mask | FLASH_SR_EOP;
}

#if defined(STM32L0)
// STM32L0 uses PECR. Page erase starts by writing at the page address.
namespace {
using namespace flash_hw;

constexpr uint32_t lock_mask = FLASH_PECR_PELOCK | FLASH_PECR_PRGLOCK;
constexpr uint32_t page_erase = FLASH_PECR_ERASE | FLASH_PECR_PROG;
constexpr uint32_t operation_mask = page_erase | FLASH_PECR_FPRG | FLASH_PECR_DATA;

SysStatus wait_operation()
{
	if (!System::WaitBits(FLASH->SR, ready_mask, 0U, timeout_ms))
		return SysStatus::Timeout;
	return (FLASH->SR & error_mask) ? SysStatus::Error : SysStatus::OK;
}
} // namespace

SysStatus flash_base::enable_access()
{
	if (!System::WaitBits(FLASH->SR, ready_mask, 0U, timeout_ms))
		return SysStatus::Timeout;
	if (FLASH->PECR & FLASH_PECR_PELOCK) {
		FLASH->PEKEYR = 0x89ABCDEFU;
		FLASH->PEKEYR = 0x02030405U;
	}
	if (FLASH->PECR & FLASH_PECR_PRGLOCK) {
		FLASH->PRGKEYR = 0x8C9DAEBFU;
		FLASH->PRGKEYR = 0x13141516U;
	}
	return (FLASH->PECR & lock_mask) ? SysStatus::Error : SysStatus::OK;
}

SysStatus flash_base::erase_sector(uint32_t sector)
{
	if (sector >= FLASH_SECTOR_COUNT) return SysStatus::Error;
	if (!System::WaitBits(FLASH->SR, ready_mask, 0U, timeout_ms))
		return SysStatus::Timeout;
	if (FLASH->PECR & lock_mask) return SysStatus::Error;
	clear_errors();

	const uint32_t addr = GetFlashSector(sector).address;
	FLASH->PECR = (FLASH->PECR & ~operation_mask) | page_erase;
	*reinterpret_cast<volatile uint32_t*>(addr) = 0U;
	__DSB();
	const SysStatus result = wait_operation();
	if (ready()) FLASH->PECR &= ~page_erase;
	return result;
}

SysStatus flash_base::mass_erase(uint8_t banks)
{
	if (!banks || banks > 3U || (!FLASH_DUAL_BANK && banks != 1U)) return SysStatus::Error;
	for (uint32_t sector = 0U; sector < FLASH_SECTOR_COUNT; ++sector) {
		const auto bank = GetFlashSector(sector).bank;
		if (!(banks & (1U << (bank - 1U)))) continue;
		const SysStatus result = erase_sector(sector);
		if (result != SysStatus::OK) return result;
	}
	return SysStatus::OK;
}

template<uint32_t width>
SysStatus flash_base::program(uint32_t addr, const void* data, uint32_t bytes)
{
	static_assert(width == 4U);
	if (!bytes) return SysStatus::OK;
	if (!System::WaitBits(FLASH->SR, ready_mask, 0U, timeout_ms))
		return SysStatus::Timeout;
	if (FLASH->PECR & lock_mask) return SysStatus::Error;
	clear_errors();
	// A previous erase timeout can leave ERASE and PROG set.
	FLASH->PECR &= ~operation_mask;

	const auto* source = static_cast<const uint8_t*>(data);
	const uint32_t physical_bytes = (bytes + width - 1U) & ~(width - 1U);
	for (uint32_t i = 0; i < physical_bytes; i += width) {
		uint32_t value = 0U;
		const uint32_t remaining = bytes - i;
		memcpy(&value, source + i, remaining < width ? remaining : width);
		*reinterpret_cast<volatile uint32_t*>(addr + i) = value;
		__DSB();
		const SysStatus result = wait_operation();
		if (result != SysStatus::OK) return result;
	}
	return SysStatus::OK;
}

template SysStatus flash_base::program<4U>(uint32_t, const void*, uint32_t);
#else
namespace {
// Check errors as soon as the hardware operation stops. G0 also requires
// CFGBSY clear before CR can be written again.
SysStatus wait_operation()
{
	uint32_t started = 0U;
	if constexpr (ready_mask != busy_mask) started = System::GetTick();
	if (!System::WaitBits(FLASH->SR, busy_mask, 0U, timeout_ms))
		return SysStatus::Timeout;
	if (FLASH->SR & error_mask) return SysStatus::Error;
	if constexpr (ready_mask != busy_mask) {
		const uint32_t elapsed = System::GetTick() - started;
		const uint32_t remaining = elapsed < timeout_ms ? timeout_ms - elapsed : 0U;
		if (!System::WaitBits(FLASH->SR, ready_mask & ~busy_mask, 0U, remaining))
			return SysStatus::Timeout;
	}
	return SysStatus::OK;
}
} // namespace

SysStatus flash_base::enable_access()
{
	if (!System::WaitBits(FLASH->SR, ready_mask, 0U, timeout_ms))
		return SysStatus::Timeout;
	if (FLASH->CR & FLASH_CR_LOCK) {
		FLASH->KEYR = 0x45670123U;
		FLASH->KEYR = 0xCDEF89ABU;
	}
	return (FLASH->CR & FLASH_CR_LOCK) ? SysStatus::Error : SysStatus::OK;
}

SysStatus flash_base::erase_sector(uint32_t sector)
{
	if (sector >= FLASH_SECTOR_COUNT) return SysStatus::Error;
	if (!System::WaitBits(FLASH->SR, ready_mask, 0U, timeout_ms))
		return SysStatus::Timeout;
	if (FLASH->CR & FLASH_CR_LOCK) return SysStatus::Error;
	clear_errors();
	const FlashSector geometry = GetFlashSector(sector);
	const bool restore_cache = pause_data_cache();
	invalidate_flash(geometry.address, geometry.size);
	FLASH->CR = (FLASH->CR & ~operation_mask) | page_erase_bits(geometry);
	FLASH->CR |= FLASH_CR_STRT;
	const SysStatus completed = wait_operation();
	// Timeout does not cancel erase. Modify CR only when hardware is ready.
	if (ready()) {
		FLASH->CR &= ~operation_mask;
		invalidate_flash(geometry.address, geometry.size);
	}
	resume_data_cache(restore_cache);
	return completed;
}

SysStatus flash_base::mass_erase(uint8_t bank)
{
	// Mask: 1 = bank 1, 2 = bank 2, 3 = both. Reject absent banks.
	if (!bank || bank > 3U || (!FLASH_DUAL_BANK && bank != 1U)) return SysStatus::Error;
	if ((bank & 2U) && !second_bank_erase) return SysStatus::Error;
	if (!System::WaitBits(FLASH->SR, ready_mask, 0U, timeout_ms))
		return SysStatus::Timeout;
	if (FLASH->CR & FLASH_CR_LOCK) return SysStatus::Error;
	clear_errors();
	const bool restore_cache = pause_data_cache();
	invalidate_banks(bank);
	FLASH->CR = (FLASH->CR & ~operation_mask) | bank_erase_bits(bank);
	FLASH->CR |= FLASH_CR_STRT;
	const SysStatus completed = wait_operation();
	if (ready()) {
		FLASH->CR &= ~operation_mask;
		invalidate_banks(bank);
	}
	resume_data_cache(restore_cache);
	return completed;
}

// A final partial G0/G4 doubleword is padded with FF by the physical store.
template<uint32_t width>
SysStatus flash_base::program(uint32_t addr, const void* data, uint32_t bytes)
{
	if (!bytes) return SysStatus::OK;
	const uint32_t physical_bytes = doubleword ? (bytes + 7U) & ~7U : bytes;
	if (!System::WaitBits(FLASH->SR, ready_mask, 0U, timeout_ms))
		return SysStatus::Timeout;
	if (FLASH->CR & FLASH_CR_LOCK) return SysStatus::Error;
	clear_errors();
	const bool restore_cache = pause_data_cache();
	// Clear stale lines even if the operation later times out while busy.
	invalidate_flash(addr, physical_bytes);
	FLASH->CR = (FLASH->CR & ~operation_mask) | program_bits(width);
	const uint8_t* src = static_cast<const uint8_t*>(data);
	SysStatus result = SysStatus::OK;
	for (uint32_t i = 0; i < physical_bytes; i += width) {
		store<width>(addr + i, src + i, bytes - i);
		__DSB();
		result = wait_operation();
		if (result != SysStatus::OK) break;
	}
	if (ready()) {
		FLASH->CR &= ~operation_mask;
		invalidate_flash(addr, physical_bytes);
	}
	resume_data_cache(restore_cache);
	return result;
}

// Emit only the physical programming widths supported by the selected controller.
#if defined(STM32F4) || defined(STM32F7)
template SysStatus flash_base::program<1U>(uint32_t, const void*, uint32_t);
template SysStatus flash_base::program<2U>(uint32_t, const void*, uint32_t);
template SysStatus flash_base::program<4U>(uint32_t, const void*, uint32_t);
#else
template SysStatus flash_base::program<8U>(uint32_t, const void*, uint32_t);
#endif

#endif // STM32L0

// One sector contains full, fixed-size slots:
// F4/F7: [status byte][payload]. G0/G4: [commit word][invalid word][payload][padding].
// L0: [writing word][commit word][invalid word][payload][padding].
SysInitStatus flash_data::SetUp(bank_type tb)
{
	stride = 0;
	status = Data_Status::NO_DATA;
	if (sector >= FLASH_SECTOR_COUNT || !data || !size) return SysInitStatus::InitError;
	const uint32_t unit = sizeof(status_t);
	if (size > UINT32_MAX - (unit - 1U)) return SysInitStatus::InitError;
	const uint32_t rounded = (size + unit - 1U) & ~(unit - 1U);
	if (rounded > UINT32_MAX - record_header_size) return SysInitStatus::InitError;
	const uint32_t record_size = record_header_size + rounded;
	const FlashSector geometry = GetFlashSector(sector);
	if (record_size > geometry.size || (geometry.address & (unit - 1U)))
		return SysInitStatus::InitError;
	if (init(tb) != SysInitStatus::InitOK || enable_access() != SysStatus::OK)
		return SysInitStatus::InitError;
	invalidate_flash(geometry.address, geometry.size);
	stride = record_size;
	find_offset();
	return SysInitStatus::InitOK;
}

flash_data::Data_Status flash_data::read_data()
{
	if (!stride) return Data_Status::DATA_NOT_VALID;
	if (status == Data_Status::DATA_OK) {
		const uint32_t addr = GetFlashSector(sector).address + offset + record_header_size;
		memcpy(data, reinterpret_cast<const uint8_t*>(addr), size);
	}
	return status;
}

SysStatus flash_data::write_data()
{
	if (!stride) return SysStatus::NotInit;
	if (!System::WaitBits(FLASH->SR, ready_mask, 0U, timeout_ms))
		return SysStatus::Timeout;
	const FlashSector geometry = GetFlashSector(sector);
	if (next_offset > geometry.size - stride) {
		// One-sector storage cannot preserve the previous version during erase.
		const SysStatus erased = erase_sector(sector);
		if (erased != SysStatus::OK) {
			stride = 0; // A failed erase may already have destroyed old data.
			status = Data_Status::DATA_NOT_VALID;
			return erased;
		}
		offset = 0;
		next_offset = 0;
		status = Data_Status::NO_DATA;
	}

	// Reserve the whole attempted slot; failures must not retry its data blocks.
	const uint32_t candidate = next_offset;
	next_offset += stride;
	const uint32_t addr = geometry.address + candidate;
	if (status != Data_Status::DATA_OK) status = Data_Status::DATA_NOT_VALID;
	Data_Status marker = Data_Status::DATA_WRITING;
	SysStatus result = write(addr, &marker);
	if (result != SysStatus::OK) return result;
	result = write(addr + record_header_size, data, size);
	if (result != SysStatus::OK) return result;
	// L0 commits in a second word. G0/G4 commit by writing a full zero.
	marker = Data_Status::DATA_OK;
	result = write(addr + commit_offset, &marker);
	if (result != SysStatus::OK) return result;

	// Invalidate the previous record only after the new commit succeeded.
	if (status == Data_Status::DATA_OK) {
		// The invalidation marker occupies the last unit of the record header.
		marker = Data_Status::DATA_NOT_VALID;
		result = write(geometry.address + offset + record_header_size - sizeof(status_t),
					   &marker);
	}
	// The new record remains selected even if invalidating the old one failed.
	offset = candidate;
	status = Data_Status::DATA_OK;
	return result;
}

// Continue past invalid/incomplete slots after a good record. offset retains the
// last valid commit; next_offset follows the last occupied slot of any status.
// Payload and padding are skipped because the scan always steps by stride.
void flash_data::find_offset()
{
	offset = 0;
	next_offset = 0;
	status = Data_Status::NO_DATA;
	const FlashSector geometry = GetFlashSector(sector);
	const uint32_t last_offset = geometry.size - stride;
	for (uint32_t pos = 0; pos <= last_offset; pos += stride) {
		const uint32_t addr = geometry.address + pos;
		if constexpr (record_header_words > 1U) {
			// The invalidation word takes priority over the commit word.
			if (read<Data_Status>(addr + record_header_size - sizeof(status_t)) != Data_Status::NO_DATA) {
				next_offset = pos + stride;
				if (status != Data_Status::DATA_OK) status = Data_Status::DATA_NOT_VALID;
				continue;
			}
		}
		const Data_Status marker = read<Data_Status>(addr);
		const Data_Status commit = commit_offset ? read<Data_Status>(addr + commit_offset) : marker;
		if (marker == Data_Status::NO_DATA && commit == Data_Status::NO_DATA) continue;
		// Skip the entire occupied record, including its payload and padding.
		next_offset = pos + stride;
		if (commit == Data_Status::DATA_OK && (!commit_offset || marker == Data_Status::DATA_WRITING)) {
			offset = pos;
			status = Data_Status::DATA_OK;
		} else if (status != Data_Status::DATA_OK) {
			status = Data_Status::DATA_NOT_VALID;
		}
	}
}
