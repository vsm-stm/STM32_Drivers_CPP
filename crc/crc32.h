// Portable software CRC32 (no lookup table, no hardware peripheral).
//
// Polynomial 0xEDB88320 (reflected form of the standard IEEE 802.3 / zlib /
// PNG polynomial 0x04C11DB7) — this is the CRC32 most tools already agree on
// (matches Python's zlib.crc32, Ethernet FCS, PNG chunk checksums), so a host
// tool talking to the bootloader can verify against a library it already has
// instead of a bespoke variant.
//
// No table: STM32 hardware CRC peripherals differ per family (fixed
// polynomial on F4, configurable on G0) and are not used here on purpose —
// this file has to build identically on any family. A lookup table would
// trade ~1 KB of flash for speed; at typical firmware-update baud rates the
// bootloader is link-limited, not CRC-limited, so the byte-at-a-time form
// (this file) is the right trade for a minimal image.
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRC32_INIT 0xFFFFFFFFu

// Feed one more chunk into a running CRC. Start with CRC32_INIT, call this
// once per chunk as data arrives (packet-by-packet is fine — the result does
// not depend on how the input was split), then crc32_finish() the result.
uint32_t crc32_update(uint32_t crc, const void *data, size_t len);

// Turn a running CRC (from crc32_update) into the final checksum value.
static inline uint32_t crc32_finish(uint32_t crc)
{
	return crc ^ 0xFFFFFFFFu;
}

// One-shot helper for a single contiguous buffer.
static inline uint32_t crc32_compute(const void *data, size_t len)
{
	return crc32_finish(crc32_update(CRC32_INIT, data, len));
}

#ifdef __cplusplus
}
#endif
