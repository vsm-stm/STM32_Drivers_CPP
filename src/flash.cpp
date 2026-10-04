#include "flash.hpp"

// Erase control bits differ between STM32 families:
// F4/F7 — sector erase via SER (Sector Erase) + sector number in SNB
// G0/L0 — page erase via PER (Page Erase) + page number in PNB
#if defined(STM32F7) || defined(STM32F4)
	#define CR_EARSE_BIT	FLASH_CR_SER
	#define CR_SERCTOR_Pos	FLASH_CR_SNB_Pos
	#define CR_SERCTOR_Msk	FLASH_CR_SNB_Msk

#elif defined(STM32L0) || defined(STM32G0) // todo check L0
	#define CR_EARSE_BIT 	FLASH_CR_PER
	#define CR_SERCTOR_Pos	FLASH_CR_PNB_Pos
	#define CR_SERCTOR_Msk	FLASH_CR_PNB_Msk

#endif

// Number of sectors (F4/F7) / pages (G0/L0) - from the flash_config.h map
static constexpr uint32_t FLASH_PAGE_COUNT = FLASH_SECTOR_COUNT;

// Global flag: flash is already unlocked. Writing the keys again after the
// first unlock locks the flash in hardware (an attack countermeasure).
bool flash_access_enabled = false;

bool flash_base::init(bank_type tb)
{
	if constexpr (FLASH_DUAL_BANK)
	{
		// The sector layout for both banks is set in flash_sectors[] at compile
		// time via CMake (STM32_DUAL_BANK). The bank_type parameter is kept for
		// API compatibility.
		return (tb == bank_type::SINGLE_BANK || tb == bank_type::DUAL_BANK);
	}
	return true;
}

bool flash_base::enable_access()
{
	if(!flash_access_enabled)
	{
		// Hardware flash unlock sequence: the two keys must be written strictly
		// back to back. Any other write to KEYR in between locks the flash
		// until the next reset. The key values are fixed in the Reference Manual.
		FLASH->KEYR = 0x45670123;
		FLASH->KEYR = 0xCDEF89AB;
		flash_access_enabled = 1;
		return true;
	}
	return false;
};

void flash_base::erase_sector(uint8_t sector)
{
	if (sector >= FLASH_PAGE_COUNT) return;

	while(!ready()){};
	clear_errors();

	FLASH->CR |= CR_EARSE_BIT;            // Page/sector erase mode
	FLASH->CR |= sector << CR_SERCTOR_Pos; // Page/sector number
	FLASH->CR |= FLASH_CR_STRT;            // Start the operation

	while(!ready()){};

	// Clear the bits once done - otherwise the next flash access could be
	// misread as an erase command.
	FLASH->CR &= ~(CR_SERCTOR_Msk);
	FLASH->CR &= ~(CR_EARSE_BIT);
}

void flash_base::mass_erase(uint8_t bank)
{
	// bank is a bitmask: bit 0 = bank 1, bit 1 = bank 2.
	// 0 is invalid, > 3 is outside the valid bank range.
	if((!bank) || (bank > 3)) return;

	while(!ready()){};
	clear_errors();

	if(bank & 1) FLASH->CR |= CR_MER;
	// if(bank & 2) FLASH->CR |= FLASH_CR_MER2; todo check if MER2 is supported on STM32G0

	FLASH->CR |= FLASH_CR_STRT;

	while(!ready()){};

	FLASH->CR &= ~(CR_MER);
}

// -----------------------------------------------------------------------------
// Flash sector record format (wear leveling):
//
//   [ status_t ][ data ... ][ padding ][ status_t ][ data ... ][ padding ] ...
//   |<---sz--->|<----------size------->|           |
//   |<--------------------stride------------------->|
//
// status_t = uint8_t on F4/F7/H7, uint64_t on G0 (the minimum write unit).
//
// Computing stride: the status (sz bytes) plus the data, rounded up to a
// multiple of sz. The rounding guarantees the next record starts on an
// aligned address.
//
//   stride = sz + ceil(size / sz) * sz
//          = sz + ((size + sz - 1) / sz) * sz
//
// Examples for uint64_t (sz=8):
//   size=5  -> 8 + ((5+7)/8)*8  = 8 + 8  = 16
//   size=8  -> 8 + ((8+7)/8)*8  = 8 + 8  = 16
//   size=9  -> 8 + ((9+7)/8)*8  = 8 + 16 = 24
//
// Examples for uint8_t (sz=1):
//   size=5  -> 1 + ((5+0)/1)*1  = 1 + 5  = 6   (no alignment, sz=1)
// -----------------------------------------------------------------------------

flash_data::Data_Status flash_data::read_data()
{
	const uint32_t sz     = sizeof(status_t);
	const uint32_t stride = sz + ((size + sz - 1) / sz) * sz;

	// Only read the data once we know it is valid.
	// Data address = record start + sizeof(status_t) (skip the status byte/word).
	if(status == Data_Status::DATA_OK)
	{
		memcpy(data, reinterpret_cast<uint8_t*>(flash_sectors[sector].address + offset * stride + sz), size);
	}

	return status;
}

void flash_data::write_data()
{
	const uint32_t sz     = sizeof(status_t);
	const uint32_t stride = sz + ((size + sz - 1) / sz) * sz;

	if((status == Data_Status::DATA_OK)
	|| (status == Data_Status::DATA_CORRUPT))
	{
		if(((offset + 1) * stride > flash_sectors[sector].size)
		|| (status == Data_Status::DATA_CORRUPT))
		{
			// The sector is full or the data is corrupt - erase and start over.
			// After erasing, every byte is 0xFF (NO_DATA).
			offset = 0;
			erase_sector(sector);
		}
		else
		{
			// Invalidate the current record: write DATA_NOT_VALID (0x00) to its
			// status. Flash only allows flipping bits 1->0, so writing a zero is
			// always permitted.
			status_t not_valid = static_cast<status_t>(Data_Status::DATA_NOT_VALID);
			write(flash_sectors[sector].address + offset * stride, &not_valid);
			offset++; // Move on to the next slot
		}
	}

	// Write the new record: status first, then data.
	// The split matters: if power is lost between the two, the status stays
	// invalid and the record will not be read back as valid on the next boot.
	status_t ok = static_cast<status_t>(Data_Status::DATA_OK);
	write(flash_sectors[sector].address + offset * stride, &ok);
	write(flash_sectors[sector].address + offset * stride + sz, data, size);
	status = Data_Status::DATA_OK;
}

void flash_data::find_offset()
{
	const uint32_t sz     = sizeof(status_t);
	const uint32_t stride = sz + ((size + sz - 1) / sz) * sz;

	// Scan the sector from the start, skipping invalidated records
	// (DATA_NOT_VALID), until we find the current record (DATA_OK) or the end
	// of the written area (NO_DATA).
	offset = 0;
	do
	{
		status = static_cast<Data_Status>(read<status_t>(flash_sectors[sector].address + offset * stride));

		if(status == Data_Status::DATA_NOT_VALID)
		{
			// This record was invalidated by a previous write - skip it
			offset++;
			if(offset * stride > flash_sectors[sector].size)
			{
				// Reached the end of the sector without finding valid data - reset
				offset = 0;
				break;
			}
		}
		else
		{
			// DATA_OK    - found the current record, offset points at it
			// NO_DATA    - the sector is clean (or nothing was written past here)
			// anything else - corruption, will overwrite from the start after erasing
			if((status != Data_Status::DATA_OK)
			&& (status != Data_Status::NO_DATA))
			{
				status = Data_Status::DATA_CORRUPT;
				offset = 0;
			}
			break;
		}
	} while (status == Data_Status::DATA_NOT_VALID);
}
