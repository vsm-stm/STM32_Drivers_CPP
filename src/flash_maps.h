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
	{0x08000000UL, 0x03FFF},	// Sector 0 - 16Kbytes
	{0x08004000UL, 0x03FFF},	// Sector 1 - 16Kbytes
	{0x08008000UL, 0x03FFF},	// Sector 2 - 16Kbytes
	{0x0800C000UL, 0x03FFF},	// Sector 3 - 16Kbytes
	{0x08010000UL, 0x0FFFF},	// Sector 4 - 64Kbytes
	{0x08020000UL, 0x3FFFF},	// Sector 5 - 128Kbytes
	{0x08040000UL, 0x3FFFF},	// Sector 6 - 128Kbytes
	{0x08060000UL, 0x3FFFF},	// Sector 7 - 128Kbytes
	{0x08080000UL, 0x3FFFF},	// Sector 8 - 128Kbytes
	{0x080A0000UL, 0x3FFFF},	// Sector 9 - 128Kbytes
	{0x080C0000UL, 0x3FFFF},	// Sector 10- 128Kbytes
	{0x080E0000UL, 0x3FFFF}};	// Sector 11 - 128Kbytes
#elif defined(STM32F446RE) || defined(STM32F411СE)|| defined(STM32F722RE)
#define sector_count 8
static const flash_map_typedef flash_map [sector_count] = {
	{0x08000000UL, 0x03FFF},	// Sector 0 - 16Kbytes
	{0x08004000UL, 0x03FFF},	// Sector 1 - 16Kbytes
	{0x08008000UL, 0x03FFF},	// Sector 2 - 16Kbytes
	{0x0800C000UL, 0x03FFF},	// Sector 3 - 16Kbytes
	{0x08010000UL, 0x0FFFF},	// Sector 4 - 64Kbytes
	{0x08020000UL, 0x3FFFF},	// Sector 5 - 128Kbytes
	{0x08040000UL, 0x3FFFF},	// Sector 6 - 128Kbytes
	{0x08060000UL, 0x3FFFF}};	// Sector 7 - 128Kbytes
#elif defined(STM32F746ZG)
#define sector_count 8
static const flash_map_typedef flash_map [sector_count] = {
	{0x08000000UL, 0x07FFF},	// Sector 0 - 32Kbytes
	{0x08008000UL, 0x07FFF},	// Sector 1 - 32Kbytes
	{0x08010000UL, 0x07FFF},	// Sector 2 - 32Kbytes
	{0x08018000UL, 0x07FFF},	// Sector 3 - 32Kbytes
	{0x08020000UL, 0x3FFFF},	// Sector 4 - 128Kbytes
	{0x08040000UL, 0x7FFFF},	// Sector 5 - 256Kbytes
	{0x08080000UL, 0x7FFFF},	// Sector 6 - 256Kbytes
	{0x080C0000UL, 0x7FFFF}};	// Sector 7 - 256Kbytes
#elif defined(STM32F767VI)
#define sector_count 12
static const flash_map_typedef flash_map [sector_count] = {
	{0x08000000UL, 0x07FFF},	// Sector 0 - 32Kbytes
	{0x08008000UL, 0x07FFF},	// Sector 1 - 32Kbytes
	{0x08010000UL, 0x07FFF},	// Sector 2 - 32Kbytes
	{0x08018000UL, 0x07FFF},	// Sector 3 - 32Kbytes
	{0x08020000UL, 0x3FFFF},	// Sector 4 - 128Kbytes
	{0x08040000UL, 0x7FFFF},	// Sector 5 - 256Kbytes
	{0x08080000UL, 0x7FFFF},	// Sector 6 - 256Kbytes
	{0x08040000UL, 0x7FFFF},	// Sector 7 - 256Kbytes
	{0x08040000UL, 0x7FFFF},	// Sector 8 - 256Kbytes
	{0x08080000UL, 0x7FFFF},	// Sector 9 - 256Kbytes
	{0x08040000UL, 0x7FFFF},	// Sector 10- 256Kbytes
	{0x080C0000UL, 0x7FFFF}};	// Sector 11- 256Kbytes
#else
#error "No flash map for this chip"
#endif

#endif