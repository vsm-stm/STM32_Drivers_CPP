/**
 * @file flash.hpp
 * @brief F4/F7/G0 Flash programming and a single-sector settings journal.
 *
 * flash_config.h format 4 is generated from the base map: GetFlashSector(index)
 * gives address, size, bank and hardware erase number. Uniform pages use bank
 * constants and arithmetic; irregular sectors use a table. init() checks bank mode
 * against hardware on switchable parts and rejects active bank swap.
 * It does not change option bytes or unlock programming; enable_access() unlocks.
 *
 * Operations are blocking and belong in main, with a running SysTick. Do not
 * invoke them in IRQ or an interrupt-masked critical section. The configured
 * wait budget is 50 s on F4/F7, 1 s on G0. When code/vector fetches stall in the
 * busy Flash bank, SysTick may not advance: this is not a guaranteed wall-clock
 * timeout. Timeout does not cancel an operation already started by hardware.
 *
 * Raw write arguments (source validity, destination range, alignment and length
 * overflow including physical rounding) are the caller's responsibility.
 * flash_data checks its own fixed sector/record layout in SetUp().
 * Reserve a journal sector outside code/other Flash users before using it.
 */
#ifndef FLASH_HPP_
#define FLASH_HPP_

#include "system.hpp"
#include "flash_config.h"
#include <string.h>

#if !defined(STM32_FLASH_CONFIG_FORMAT) || STM32_FLASH_CONFIG_FORMAT != 4
#error "Flash requires config format 4; update drivers and STM32-base_files/flash_config.h.in"
#endif

#if defined(STM32F7) || defined(STM32F4)
	using status_t = uint8_t;
#elif defined(STM32G0)
	using status_t = uint64_t;
#else
#error "Flash driver supports STM32F4, STM32F7 and STM32G0 only"
#endif
// Blocking operations: call from the main loop, with SysTick running.
// Do not call from interrupts or critical sections; the driver does not mask IRQs.
class flash_base
{
private:
	template<uint32_t width>
	static SysStatus program(uint32_t addr, const void* data, uint32_t bytes);
public:
	enum class bank_type
	{
		SINGLE_BANK,
		DUAL_BANK
	};

	/**
	 * @brief Check configured bank layout and normal hardware bank mapping.
	 * @param tb Expected mode; default is the generated FLASH_DUAL_BANK value.
	 * @return InitOK if consistent; InitError on configuration/mode mismatch
	 *         or unsupported bank swap. No option bytes are modified.
	 * @pre Call before direct Flash operations; inspect this result before unlock.
	 */
	static SysInitStatus init(bank_type tb = FLASH_DUAL_BANK ? bank_type::DUAL_BANK : bank_type::SINGLE_BANK);
	/**
	 * @brief Wait until the controller is ready, then unlock Flash programming.
	 * @return OK when unlocked; Timeout if readiness wait expires; Error if locked.
	 * Programming/erase do not unlock automatically and do not relock on return.
	 */
	static SysStatus enable_access();
	/// Nonblocking test of controller readiness (BSY and G0 CFGBSY); not an error check.
	static bool ready();
	/**
	 * @brief Erase one sector/page selected by its logical GetFlashSector() index.
	 * @param sector Global table index, not the hardware SNB/PNB field number.
	 * @return OK, Error for invalid index/locked controller/hardware error,
	 *         or Timeout. Data may already be erased/modified on failure.
	 * Bank/local erase number come from the generated row. Caller reserves the
	 * entire target sector and keeps programming serialized with other users.
	 */
	static SysStatus erase_sector(uint32_t sector);
	/**
	 * @brief Erase selected physical banks in normal (non-swapped) mapping.
	 * @param bank Bit mask: 1 = bank 1, 2 = bank 2, 3 = both.
	 * @return OK, Error for invalid/absent bank or hardware/lock error, or Timeout.
	 * All contents of selected banks, including code, are affected.
	 */
	static SysStatus mass_erase(uint8_t bank);
	/// Acknowledge defined SR error/completion flags (W1C); does not unlock or stop Flash.
	static void clear_errors();

	/**
	 * @brief Direct typed volatile read of Flash; no copying or address validation.
	 * @tparam T Type readable at the supplied address.
	 * @pre Address is mapped, aligned for T, and Flash is ready for that access.
	 * Do not use reads to probe an invalid address or a busy/timed-out journal.
	 */
	template<typename T>
	static inline T read(uint32_t addr) { return *reinterpret_cast<const volatile T*>(addr); }

	/**
	 * @brief Program size elements without automatically erasing/unlocking.
	 * @tparam T Element width 1, 2, 4 or 8 bytes.
	 * @param addr Destination address; caller validates mapped range and alignment.
	 * @param data Readable source; bytes are copied locally so source alignment
	 *             need not match the physical programming width.
	 * @param size Element count, not bytes. Zero is a no-op returning OK.
	 * @return OK, controller Error or Timeout; an incomplete write is not rolled back.
	 *
	 * F4/F7 use byte/halfword/word programming; uint64_t is two x32 operations
	 * under the agreed 3.0/3.3 V use without external VPP. G0 always programs
	 * 8-byte doublewords and pads a final partial block with FF.
	 * Caller validates multiplication/rounding overflow and the whole physical
	 * destination extent, prepares erased Flash, and keeps it inaccessible during
	 * programming. Rewriting an occupied G0 doubleword is not general bit clearing.
	 *
	 * @code
	 *   // address is an aligned, erased range reserved outside the firmware.
	 *   uint32_t value = 0x12345678U;
	 *   if (flash_base::init() == SysInitStatus::InitOK &&
	 *       flash_base::enable_access() == SysStatus::OK) {
	 *       SysStatus result = flash_base::write(address, &value);
	 *       // Check result; a Timeout may leave the controller busy.
	 *   }
	 * @endcode
	 */
	template<typename T>
	static SysStatus write(uint32_t addr, const T* data, uint32_t size = 1);
};

/**
 * @brief Fixed-size record journal contained in one reserved Flash sector.
 *
 * Slots are scanned at full record stride, never by searching for FF payload
 * bytes; data/padding therefore cannot be mistaken for another record header.
 * A new record is marked WRITING, its payload is programmed, then it is committed
 * OK; the previous good record is invalidated only after the new commit.
 *
 * F4/F7: [one status byte][payload], FF -> 7F -> 0F -> 00.
 * G0: [8-byte commit/status][8-byte invalidation][payload padded to 8 bytes].
 * First G0 word goes FF -> DATA_WRITING -> full zero (DATA_OK); only after a
 * newer commit is the old record's second word programmed DATA_NOT_VALID.
 * The second word takes precedence when interpreting an old committed record.
 *
 * One sector is deliberate: when full, it is erased before the next record.
 * A power failure during that erase can lose the last saved settings. There is
 * no CRC or two-sector recovery; ECC/read faults are not repaired by this API.
 * Layout/payload-size changes need a migration or erased journal before use.
 *
 * @code
 *   struct Settings { uint32_t rate; uint32_t flags; } settings{};
 *   // settings_sector is an index explicitly reserved in the project layout.
 *   flash_data store(settings_sector, reinterpret_cast<uint8_t*>(&settings),
 *                    sizeof(settings));
 *   if (store.SetUp() == SysInitStatus::InitOK) {
 *       auto saved = store.read_data(); // Copies only when saved == DATA_OK.
 *       if (saved != flash_data::Data_Status::DATA_OK) {
 *           settings.rate = 115200; // Application defaults.
 *           settings.flags = 0;
 *       }
 *       // On an explicit save request, from main:
 *       SysStatus result = store.write_data();
 *   }
 * @endcode
 */
class flash_data : private flash_base
{

public:

	/**
	 * @brief API states also used as physical Flash markers; no duplicate encoding.
	 * NO_DATA means erased. WRITING is an incomplete record, OK is committed.
	 * NOT_VALID covers obsolete/invalid records; there is no separate CORRUPT state.
	 * On G0, NOT_VALID is stored in a different 8-byte word from WRITING/OK.
	 */
	enum class Data_Status : status_t
	{
		NO_DATA = static_cast<status_t>(-1),
#if defined(STM32G0)
		DATA_WRITING = 0x00000000FFFFFFFFULL,
		DATA_OK = 0x0000000000000000ULL,
		DATA_NOT_VALID = 0xFFFFFFFF00000000ULL
#else
		DATA_WRITING = 0x7F,
		DATA_OK = 0x0F,
		DATA_NOT_VALID = 0
#endif
	};

	/**
	 * @brief Bind one reserved sector and a RAM payload; performs no Flash access.
	 * @param sec Logical erase-unit index, not an address/hardware page number.
	 * @param _data Persistent RAM source/destination used by write_data/read_data.
	 * @param sz Fixed payload size in bytes; keep layout and size stable for the log.
	 * Use constinit with a static byte array passed directly, without reinterpret_cast:
	 * @code
	 * static uint8_t payload[16]{};
	 * // Index 3 must first be reserved outside the application image/other data.
	 * constinit flash_data settings(3, payload, sizeof payload);
	 * @endcode
	 * Sector/layout validation remains in SetUp; construction only stores arguments.
	 */
	constexpr flash_data(uint32_t sec, uint8_t *_data, uint32_t sz) :
		sector(sec),
		offset(0),
		next_offset(0),
		stride(0),
		status(Data_Status::NO_DATA),
		data(_data),
		size(sz)
	{}

	/**
	 * @brief Validate the record layout, unlock Flash and scan all existing slots.
	 * @return InitOK with last committed record selected; InitError for invalid
	 *         buffer/size/sector, overflow, bad layout/bank mode or unlock failure.
	 * Does not erase a new/old sector or automatically load the RAM payload.
	 * After a programming timeout, wait for readiness and rescan before relying
	 * on journal state; a failed erase also clears the prepared stride.
	 */
	SysInitStatus SetUp(bank_type tb = FLASH_DUAL_BANK ? bank_type::DUAL_BANK : bank_type::SINGLE_BANK);

	/**
	 * @brief Copy the selected committed payload into the constructor's RAM buffer.
	 * @return Selected status; DATA_NOT_VALID if not prepared. Only DATA_OK copies
	 *         data. NO_DATA/incomplete/invalid records leave application RAM intact.
	 */
	Data_Status read_data();
	/**
	 * @brief Append the RAM payload through WRITING -> payload -> OK -> invalidate old.
	 * @return NotInit without a successful SetUp; otherwise OK/Error/Timeout.
	 * A failed attempt consumes its full slot; it is never retried over its data.
	 * If invalidating the previous record fails after the new commit, the new
	 * record is still selected, but the error is returned. A later scan selects
	 * the last committed valid record even when other invalid slots follow it.
	 * When capacity is exhausted, erases the only sector first (not power-safe).
	 */
	SysStatus write_data();

private:
	uint32_t sector;
	uint32_t offset;      // Byte offset of the last completed record within the sector.
	uint32_t next_offset; // Byte offset for the next attempt; advances by stride.
	uint32_t stride; // Zero until SetUp succeeds; also cleared after an erase failure.
	Data_Status status;

	uint8_t *data;
	uint32_t size;

	/// Scan every full slot, selecting the last valid commit and reserving past all occupied slots.
	void find_offset();

};

template<typename T>
SysStatus flash_base::write(uint32_t addr, const T* data, uint32_t size)
{
	static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);
#if defined(STM32G0)
	return program<8U>(addr, data, size * static_cast<uint32_t>(sizeof(T)));
#else
	// x64 requires external VPP on F4/F7: use two x32 writes.
	return program<sizeof(T) == 8U ? 4U : sizeof(T)>(addr, data, size * static_cast<uint32_t>(sizeof(T)));
#endif
}

#endif
