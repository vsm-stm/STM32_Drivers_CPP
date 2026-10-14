#include "flash.hpp"

namespace {
#if defined(STM32F4) || defined(STM32F7)
constexpr uint32_t flash_timeout_ms = 50000U;
constexpr uint32_t busy_mask = FLASH_SR_BSY;
constexpr uint32_t erase_bit = FLASH_CR_SER;
constexpr uint32_t sector_pos = FLASH_CR_SNB_Pos;
constexpr uint32_t sector_mask = FLASH_CR_SNB_Msk;
constexpr uint32_t mass_erase_bit = FLASH_CR_MER;
#else // STM32G0
constexpr uint32_t flash_timeout_ms = 1000U;
constexpr uint32_t busy_mask = FLASH_SR_BSY1
#if defined(FLASH_SR_BSY2)
    | FLASH_SR_BSY2
#endif
    ;
constexpr uint32_t erase_bit = FLASH_CR_PER;
constexpr uint32_t sector_pos = FLASH_CR_PNB_Pos;
constexpr uint32_t sector_mask = FLASH_CR_PNB_Msk;
constexpr uint32_t mass_erase_bit = FLASH_CR_MER1;
#endif

constexpr uint32_t ready_mask = busy_mask
#if defined(STM32G0)
    | FLASH_SR_CFGBSY
#endif
    ;

constexpr uint32_t error_mask = FLASH_SR_WRPERR | FLASH_SR_PGAERR
#if defined(FLASH_SR_OPERR)
    | FLASH_SR_OPERR
#elif defined(FLASH_SR_SOP)
    | FLASH_SR_SOP
#endif
#if defined(FLASH_SR_PGPERR)
    | FLASH_SR_PGPERR
#endif
#if defined(FLASH_SR_PGSERR)
    | FLASH_SR_PGSERR
#endif
#if defined(FLASH_SR_ERSERR)
    | FLASH_SR_ERSERR
#endif
#if defined(FLASH_SR_PROGERR)
    | FLASH_SR_PROGERR
#endif
#if defined(FLASH_SR_SIZERR)
    | FLASH_SR_SIZERR
#endif
#if defined(FLASH_SR_MISERR)
    | FLASH_SR_MISERR
#elif defined(FLASH_SR_MISSERR)
    | FLASH_SR_MISSERR
#endif
#if defined(FLASH_SR_FASTERR)
    | FLASH_SR_FASTERR
#endif
#if defined(FLASH_SR_RDERR)
    | FLASH_SR_RDERR
#endif
#if defined(FLASH_SR_OPTVERR)
    | FLASH_SR_OPTVERR
#endif
    ;

// Replace all mutually exclusive operation fields, including a previous
// operation's fields left set after a timeout. Only access CR when ready.
constexpr uint32_t operation_mask = FLASH_CR_PG | erase_bit | sector_mask | mass_erase_bit
#if defined(FLASH_CR_MER2)
    | FLASH_CR_MER2
#endif
#if defined(FLASH_CR_BKER)
    | FLASH_CR_BKER
#endif
#if defined(FLASH_CR_FSTPG)
    | FLASH_CR_FSTPG
#endif
#if defined(FLASH_CR_PSIZE_Msk)
    | FLASH_CR_PSIZE_Msk
#endif
    ;

bool valid_range(uint32_t addr, uint32_t bytes)
{
    if (!bytes || bytes > UINT32_MAX - addr) return false;
    for (const auto& sector : flash_sectors) {
        if (addr < sector.address || addr - sector.address >= sector.size) continue;
        const uint32_t available = sector.size - (addr - sector.address);
        const uint32_t part = bytes < available ? bytes : available;
        addr += part;
        bytes -= part;
        if (!bytes) return true;
    }
    return false;
}

SysStatus wait_operation()
{
#if defined(STM32G0)
    const uint32_t started = System::GetTick();
#endif
    if (!System::WaitBits(FLASH->SR, busy_mask, 0U, flash_timeout_ms))
        return SysStatus::Timeout;
    // G0 programming errors are checked before waiting for CR availability.
    if (FLASH->SR & error_mask) return SysStatus::Error;
#if defined(STM32G0)
    const uint32_t elapsed = System::GetTick() - started;
    const uint32_t remaining = elapsed < flash_timeout_ms ? flash_timeout_ms - elapsed : 0U;
    if (!System::WaitBits(FLASH->SR, FLASH_SR_CFGBSY, 0U, remaining))
        return SysStatus::Timeout;
#endif
    return SysStatus::OK;
}

// Invalidate only modified Flash addresses in the Cortex-M7 D-cache.
// Flash accelerator caches and the instruction cache have no range API.
void invalidate_flash(uint32_t addr, uint32_t bytes)
{
    __DSB();
#if defined(STM32F7)
    if (SCB->CCR & SCB_CCR_DC_Msk) {
        const uint32_t first = addr & ~31U;
        const uint32_t length = (addr - first + bytes + 31U) & ~31U;
        SCB_InvalidateDCache_byAddr(reinterpret_cast<uint32_t*>(first), static_cast<int32_t>(length));
    }
    if (FLASH->ACR & FLASH_ACR_ARTEN) {
        FLASH->ACR &= ~FLASH_ACR_ARTEN;
        FLASH->ACR |= FLASH_ACR_ARTRST;
        FLASH->ACR &= ~FLASH_ACR_ARTRST;
        FLASH->ACR |= FLASH_ACR_ARTEN;
    }
    if (SCB->CCR & SCB_CCR_IC_Msk) SCB_InvalidateICache();
#else
    (void)addr;
    (void)bytes;
    if (FLASH->ACR & FLASH_ACR_ICEN) {
        FLASH->ACR &= ~FLASH_ACR_ICEN;
        FLASH->ACR |= FLASH_ACR_ICRST;
        FLASH->ACR &= ~FLASH_ACR_ICRST;
        FLASH->ACR |= FLASH_ACR_ICEN;
    }
#if defined(STM32F4)
    if (FLASH->ACR & FLASH_ACR_DCEN) {
        FLASH->ACR &= ~FLASH_ACR_DCEN;
        FLASH->ACR |= FLASH_ACR_DCRST;
        FLASH->ACR &= ~FLASH_ACR_DCRST;
        FLASH->ACR |= FLASH_ACR_DCEN;
    }
#endif
#endif
    __DSB();
    __ISB();
}

void invalidate_banks(uint8_t banks)
{
    for (uint32_t bank = 1U; bank <= 2U; ++bank) {
        if (!(banks & (1U << (bank - 1U)))) continue;
        uint32_t first = UINT32_MAX;
        uint32_t last = 0;
        for (const auto& sector : flash_sectors) {
            if (sector.bank != bank) continue;
            if (sector.address < first) first = sector.address;
            const uint32_t end = sector.address + sector.size;
            if (end > last) last = end;
        }
        if (first != UINT32_MAX) invalidate_flash(first, last - first);
    }
}

} // namespace

SysInitStatus flash_base::init(bank_type tb)
{
	const bank_type expected = FLASH_DUAL_BANK ? bank_type::DUAL_BANK : bank_type::SINGLE_BANK;
	if (tb != expected) return SysInitStatus::InitError;
	if constexpr (FLASH_BANK_SWITCHABLE) {
		bool dual;
#if defined(STM32F4) && defined(FLASH_OPTCR_DB1M)
		dual = (FLASH->OPTCR & FLASH_OPTCR_DB1M) != 0U;
#elif defined(STM32F7) && defined(FLASH_OPTCR_nDBANK)
		dual = (FLASH->OPTCR & FLASH_OPTCR_nDBANK) == 0U;
#elif defined(STM32G0) && (defined(STM32G0B1xx) || defined(STM32G0C1xx))
		dual = (FLASH->OPTR & FLASH_OPTR_DUAL_BANK) != 0U;
#else
		return SysInitStatus::InitError;
#endif
		if (dual != FLASH_DUAL_BANK) return SysInitStatus::InitError;
	}
	if constexpr (FLASH_DUAL_BANK) {
#if defined(SYSCFG_MEMRMP_UFB_MODE) || defined(SYSCFG_MEMRMP_SWP_FB)
		// Read the active mapping; restore the clock if it was initially disabled.
		const bool clock_enabled = (RCC->APB2ENR & RCC_APB2ENR_SYSCFGEN) != 0U;
		if (!clock_enabled) {
			RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
			(void)RCC->APB2ENR;
		}
#if defined(SYSCFG_MEMRMP_UFB_MODE)
		const bool swapped = (SYSCFG->MEMRMP & SYSCFG_MEMRMP_UFB_MODE) != 0U;
#else
		const bool swapped = (SYSCFG->MEMRMP & SYSCFG_MEMRMP_SWP_FB) != 0U;
#endif
		if (!clock_enabled) RCC->APB2ENR &= ~RCC_APB2ENR_SYSCFGEN;
		if (swapped) return SysInitStatus::InitError;
#elif defined(STM32G0B1xx) || defined(STM32G0C1xx)
		if (!(FLASH->OPTR & FLASH_OPTR_nSWAP_BANK)) return SysInitStatus::InitError;
#endif
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

SysStatus flash_base::enable_access()
{
    if (init() != SysInitStatus::InitOK) return SysStatus::Error;
    if (!System::WaitBits(FLASH->SR, ready_mask, 0U, flash_timeout_ms))
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
    if (init() != SysInitStatus::InitOK) return SysStatus::Error;
    if (!System::WaitBits(FLASH->SR, ready_mask, 0U, flash_timeout_ms))
        return SysStatus::Timeout;
    if (FLASH->CR & FLASH_CR_LOCK) return SysStatus::Error;
    clear_errors();
    invalidate_flash(flash_sectors[sector].address, flash_sectors[sector].size);

    uint32_t erase = erase_bit | ((flash_sectors[sector].number << sector_pos) & sector_mask);
#if defined(STM32F4) || defined(STM32F7)
    // VDD = 3-3.3 V, no external VPP: use x32 parallelism for erase.
    erase |= 2U << FLASH_CR_PSIZE_Pos;
#endif
#if defined(STM32G0) && defined(FLASH_CR_BKER)
    if (flash_sectors[sector].bank == 2U) erase |= FLASH_CR_BKER;
#endif
    FLASH->CR = (FLASH->CR & ~operation_mask) | erase;
    FLASH->CR |= FLASH_CR_STRT;
    const SysStatus completed = wait_operation();
    // Timeout does not cancel erase: touch CR and Flash caches only when ready.
    if (ready()) {
        FLASH->CR &= ~operation_mask;
        invalidate_flash(flash_sectors[sector].address, flash_sectors[sector].size);
    }
    return completed;
}

SysStatus flash_base::mass_erase(uint8_t bank)
{
    // Mask: 1 = bank 1, 2 = bank 2, 3 = both. Reject absent banks.
    if (!bank || bank > 3U || (!FLASH_DUAL_BANK && bank != 1U)) return SysStatus::Error;
#if !defined(FLASH_CR_MER2)
    if (bank & 2U) return SysStatus::Error;
#endif
    if (init() != SysInitStatus::InitOK) return SysStatus::Error;
    if (!System::WaitBits(FLASH->SR, ready_mask, 0U, flash_timeout_ms))
        return SysStatus::Timeout;
    if (FLASH->CR & FLASH_CR_LOCK) return SysStatus::Error;
    clear_errors();
    invalidate_banks(bank);
    uint32_t erase = (bank & 1U) ? mass_erase_bit : 0U;
#if defined(FLASH_CR_MER2)
    if (bank & 2U) erase |= FLASH_CR_MER2;
#endif
#if defined(STM32F4) || defined(STM32F7)
    erase |= 2U << FLASH_CR_PSIZE_Pos;
#endif
    FLASH->CR = (FLASH->CR & ~operation_mask) | erase;
    FLASH->CR |= FLASH_CR_STRT;
    const SysStatus completed = wait_operation();
    if (ready()) {
        FLASH->CR &= ~operation_mask;
        invalidate_banks(bank);
    }
    return completed;
}

SysStatus flash_base::program(uint32_t addr, const void* data, uint32_t bytes, uint32_t width)
{
    if (!bytes) return SysStatus::OK;
    if (!data) return SysStatus::Error;
#if defined(STM32G0)
    (void)width;
    if ((addr & 7U) || bytes > UINT32_MAX - 7U) return SysStatus::Error;
    const uint32_t physical_bytes = (bytes + 7U) & ~7U;
#else
    // F4/F7 x64 requires external VPP. Split 64-bit input into x32 writes.
    if (width == 8U) width = 4U;
    if (addr & (width - 1U)) return SysStatus::Error;
    const uint32_t physical_bytes = bytes;
#endif
    if (!valid_range(addr, physical_bytes)) return SysStatus::Error;
    if (init() != SysInitStatus::InitOK) return SysStatus::Error;
    if (!System::WaitBits(FLASH->SR, ready_mask, 0U, flash_timeout_ms))
        return SysStatus::Timeout;
    if (FLASH->CR & FLASH_CR_LOCK) return SysStatus::Error;
    clear_errors();
    // Remove old cache lines before starting, including when completion times out.
    // The caller must not access the destination range during the operation.
    invalidate_flash(addr, physical_bytes);

    uint32_t program_bits = FLASH_CR_PG;
#if defined(STM32F4) || defined(STM32F7)
    program_bits |= (width >> 1U) << FLASH_CR_PSIZE_Pos;
#endif
    FLASH->CR = (FLASH->CR & ~operation_mask) | program_bits;
    const uint8_t* src = static_cast<const uint8_t*>(data);
    SysStatus result = SysStatus::OK;
#if defined(STM32G0)
    for (uint32_t i = 0; i < physical_bytes; i += 8U) {
        uint64_t value = UINT64_MAX;
        const uint32_t count = bytes - i < 8U ? bytes - i : 8U;
        // Source buffers can be unaligned. Only the destination must be 8-byte aligned.
        memcpy(&value, src + i, count);
        *reinterpret_cast<volatile uint32_t*>(addr + i) = static_cast<uint32_t>(value);
        __ISB(); // Preserve the controller's two-word programming sequence.
        *reinterpret_cast<volatile uint32_t*>(addr + i + 4U) = static_cast<uint32_t>(value >> 32U);
        __DSB();
        result = wait_operation();
        if (result != SysStatus::OK) break;
    }
#else
    for (uint32_t i = 0; i < bytes; i += width) {
        uint32_t value = 0;
        memcpy(&value, src + i, width);
        if (width == 1U) *reinterpret_cast<volatile uint8_t*>(addr + i) = static_cast<uint8_t>(value);
        else if (width == 2U) *reinterpret_cast<volatile uint16_t*>(addr + i) = static_cast<uint16_t>(value);
        else *reinterpret_cast<volatile uint32_t*>(addr + i) = value;
        __DSB();
        result = wait_operation();
        if (result != SysStatus::OK) break;
    }
#endif
    // A timed-out hardware write may still be running; do not write CR then.
    if (ready()) {
        FLASH->CR &= ~operation_mask;
        invalidate_flash(addr, physical_bytes);
    }
    return result;
}

// One sector contains full, fixed-size slots:
// [status_t][payload][padding], stride = unit + ceil(size / unit) * unit.
// The status is separate from the data and uses the minimum programming unit.
SysInitStatus flash_data::SetUp(bank_type tb)
{
    initialized = false;
    status = Data_Status::NO_DATA;
    if (sector >= FLASH_SECTOR_COUNT || !data || !size) return SysInitStatus::InitError;
    const uint32_t unit = sizeof(status_t);
    if (size > UINT32_MAX - (unit - 1U)) return SysInitStatus::InitError;
    const uint32_t rounded = ((size + unit - 1U) / unit) * unit;
    if (rounded > UINT32_MAX - unit) return SysInitStatus::InitError;
    stride = unit + rounded;
    if (stride > flash_sectors[sector].size || flash_sectors[sector].address % unit)
        return SysInitStatus::InitError;
    if (init(tb) != SysInitStatus::InitOK || enable_access() != SysStatus::OK)
        return SysInitStatus::InitError;
    invalidate_flash(flash_sectors[sector].address, flash_sectors[sector].size);
    find_offset();
    initialized = true;
    return SysInitStatus::InitOK;
}

flash_data::Data_Status flash_data::read_data()
{
    if (!initialized) return Data_Status::DATA_CORRUPT;
    if (status == Data_Status::DATA_OK) {
        const uint32_t addr = flash_sectors[sector].address + offset * stride + sizeof(status_t);
        memcpy(data, reinterpret_cast<const uint8_t*>(addr), size);
    }
    return status;
}

SysStatus flash_data::write_data()
{
    if (!initialized) return SysStatus::NotInit;
    if (!System::WaitBits(FLASH->SR, ready_mask, 0U, flash_timeout_ms))
        return SysStatus::Timeout;
    const uint32_t slots = flash_sectors[sector].size / stride;
    if (next_offset == slots) {
        // One-sector storage cannot preserve the previous version during erase.
        const SysStatus erased = erase_sector(sector);
        if (erased != SysStatus::OK) {
            initialized = false; // A failed erase may already have destroyed old data.
            status = Data_Status::DATA_CORRUPT;
            return erased;
        }
        offset = 0;
        next_offset = 0;
        status = Data_Status::NO_DATA;
    }

    // Reserve the whole attempted slot; failures must not retry its data blocks.
    const uint32_t candidate = next_offset++;
    const uint32_t addr = flash_sectors[sector].address + candidate * stride;
    if (status != Data_Status::DATA_OK) status = Data_Status::DATA_CORRUPT;
    const status_t writing = static_cast<status_t>(Data_Status::DATA_WRITING);
    SysStatus result = write(addr, &writing);
    if (result != SysStatus::OK) return result;
    result = write(addr + sizeof(status_t), data, size);
    if (result != SysStatus::OK) return result;
    const status_t ok = static_cast<status_t>(Data_Status::DATA_OK);
    result = write(addr, &ok);
    if (result != SysStatus::OK) return result;

    // Publish the new record only after all three operations succeeded.
    // Previous completed records remain readable until the sector is erased.
    offset = candidate;
    status = Data_Status::DATA_OK;
    return SysStatus::OK;
}

void flash_data::find_offset()
{
    offset = 0;
    next_offset = 0;
    status = Data_Status::NO_DATA;
    const uint32_t slots = flash_sectors[sector].size / stride;
    for (uint32_t i = 0; i < slots; ++i) {
        const Data_Status marker = static_cast<Data_Status>(
            read<status_t>(flash_sectors[sector].address + i * stride));
        if (marker == Data_Status::NO_DATA) continue;
        // Skip an entire incomplete/corrupt/legacy-invalid record by stride.
        next_offset = i + 1U;
        if (marker == Data_Status::DATA_OK) {
            offset = i;
            status = Data_Status::DATA_OK;
        } else if (status != Data_Status::DATA_OK) {
            status = Data_Status::DATA_CORRUPT;
        }
    }
}
