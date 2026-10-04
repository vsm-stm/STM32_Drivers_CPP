#include "crc32.h"

uint32_t crc32_update(uint32_t crc, const void *data, size_t len)
{
	const uint8_t *p = (const uint8_t *)data;

	while (len--) {
		crc ^= *p++;
		for (int bit = 0; bit < 8; bit++) {
			// Reflected polynomial 0xEDB88320. If the low bit is set, shift
			// right and XOR in the polynomial; if not, just shift. Written
			// branchless: mask is all-ones when bit 0 was set, all-zeros
			// otherwise, so "& mask" applies the XOR only in that case.
			uint32_t mask = -(crc & 1u);
			crc = (crc >> 1) ^ (0xEDB88320u & mask);
		}
	}
	return crc;
}
