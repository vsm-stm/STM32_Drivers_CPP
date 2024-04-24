#ifndef FLASH_MAPS_H_
#define FLASH_MAPS_H_

#include <stdint.h>

typedef struct 
{
	uint32_t address;
	uint32_t size;
}flash_map_typedef;

#if defined(STM32F405RG)
#define sector_count 12
static const flash_map_typedef flash_map [sector_count] = {
	{0x08000000UL, 0x3FFF},		// 16Kbytes
	{0x08004000UL, 0x3FFF},		// 16Kbytes
	{0x08008000UL, 0x3FFF},		// 16Kbytes
	{0x0800C000UL, 0x3FFF},		// 16Kbytes
	{0x08010000UL, 0xFFFF},		// 64Kbytes
	{0x08020000UL, 0x3FFFF},	// 128Kbytes
	{0x08040000UL, 0x3FFFF},	// 128Kbytes
	{0x08060000UL, 0x3FFFF},	// 128Kbytes
	{0x08080000UL, 0x3FFFF},	// 128Kbytes
	{0x080A0000UL, 0x3FFFF},	// 128Kbytes
	{0x080C0000UL, 0x3FFFF},	// 128Kbytes
	{0x080E0000UL, 0x3FFFF}};	// 128Kbytes
#elif defined(STM32F446RE) || defined(STM32F722RE)
#define sector_count 8
static const flash_map_typedef flash_map [sector_count] = {
	{0x08000000UL, 0x3FFF},		// 16Kbytes
	{0x08004000UL, 0x3FFF},		// 16Kbytes
	{0x08008000UL, 0x3FFF},		// 16Kbytes
	{0x0800C000UL, 0x3FFF},		// 16Kbytes
	{0x08010000UL, 0xFFFF},		// 64Kbytes
	{0x08020000UL, 0x3FFFF},	// 128Kbytes
	{0x08040000UL, 0x3FFFF},	// 128Kbytes
	{0x08060000UL, 0x3FFFF}};	// 128Kbytes
#else
#error "No flash map for this chip"
#endif

#endif