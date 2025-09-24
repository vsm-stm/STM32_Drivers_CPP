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
#elif defined(STM32F767VI) || defined(STM32F767ZI)// 
#define sector_count_SB 12
#define sector_count_DB 24

static const flash_map_typedef flash_map_SB [sector_count_SB] = {
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

static const flash_map_typedef flash_map_DB [sector_count_DB] = {
//	Bank 1
	{0x08000000UL, 0x03FFF},	// Sector 0  - 16Kbytes
	{0x08004000UL, 0x03FFF},	// Sector 1  - 16Kbytes
	{0x08008000UL, 0x03FFF},	// Sector 2  - 16Kbytes
	{0x0800C000UL, 0x03FFF},	// Sector 3  - 16Kbytes
	{0x08010000UL, 0x0FFFF},	// Sector 4  - 64Kbytes
	{0x08020000UL, 0x3FFFF},	// Sector 5  - 128Kbytes
	{0x08040000UL, 0x3FFFF},	// Sector 6  - 128Kbytes
	{0x08060000UL, 0x3FFFF},	// Sector 7  - 128Kbytes
	{0x08080000UL, 0x3FFFF},	// Sector 8  - 128Kbytes
	{0x080A0000UL, 0x3FFFF},	// Sector 9  - 128Kbytes
	{0x080C0000UL, 0x3FFFF},	// Sector 10 - 128Kbytes
	{0x080E0000UL, 0x3FFFF},	// Sector 11 - 128Kbytes

//	Bank 2
	{0x08100000UL, 0x03FFF},	// Sector 12  - 16Kbytes
	{0x08104000UL, 0x03FFF},	// Sector 13  - 16Kbytes
	{0x08108000UL, 0x03FFF},	// Sector 14  - 16Kbytes
	{0x0810C000UL, 0x03FFF},	// Sector 15  - 16Kbytes
	{0x08110000UL, 0x0FFFF},	// Sector 16  - 64Kbytes
	{0x08120000UL, 0x3FFFF},	// Sector 17  - 128Kbytes
	{0x08140000UL, 0x3FFFF},	// Sector 18  - 128Kbytes
	{0x08160000UL, 0x3FFFF},	// Sector 19  - 128Kbytes
	{0x08180000UL, 0x3FFFF},	// Sector 20  - 128Kbytes
	{0x081A0000UL, 0x3FFFF},	// Sector 21  - 128Kbytes
	{0x081C0000UL, 0x3FFFF},	// Sector 22 - 128Kbytes
	{0x081E0000UL, 0x3FFFF},	// Sector 23 - 128Kbytes
	};

static const flash_map_typedef *flash_map = nullptr;
static uint32_t sector_count = 0;
#elif defined(STM32F429ZI)
#define sector_count 24
static const flash_map_typedef flash_map [sector_count] = {
//	Bank 1
	{0x08000000UL, 0x03FFF},	// Sector 0  - 16Kbytes
	{0x08004000UL, 0x03FFF},	// Sector 1  - 16Kbytes
	{0x08008000UL, 0x03FFF},	// Sector 2  - 16Kbytes
	{0x0800C000UL, 0x03FFF},	// Sector 3  - 16Kbytes
	{0x08010000UL, 0x0FFFF},	// Sector 4  - 64Kbytes
	{0x08020000UL, 0x3FFFF},	// Sector 5  - 128Kbytes
	{0x08040000UL, 0x3FFFF},	// Sector 6  - 128Kbytes
	{0x08060000UL, 0x3FFFF},	// Sector 7  - 128Kbytes
	{0x08080000UL, 0x3FFFF},	// Sector 8  - 128Kbytes
	{0x080A0000UL, 0x3FFFF},	// Sector 9  - 128Kbytes
	{0x080C0000UL, 0x3FFFF},	// Sector 10 - 128Kbytes
	{0x080E0000UL, 0x3FFFF},	// Sector 11 - 128Kbytes

//	Bank 2
	{0x08100000UL, 0x03FFF},	// Sector 12  - 16Kbytes
	{0x08104000UL, 0x03FFF},	// Sector 13  - 16Kbytes
	{0x08108000UL, 0x03FFF},	// Sector 14  - 16Kbytes
	{0x0810C000UL, 0x03FFF},	// Sector 15  - 16Kbytes
	{0x08110000UL, 0x0FFFF},	// Sector 16  - 64Kbytes
	{0x08120000UL, 0x3FFFF},	// Sector 17  - 128Kbytes
	{0x08140000UL, 0x3FFFF},	// Sector 18  - 128Kbytes
	{0x08160000UL, 0x3FFFF},	// Sector 19  - 128Kbytes
	{0x08180000UL, 0x3FFFF},	// Sector 20  - 128Kbytes
	{0x081A0000UL, 0x3FFFF},	// Sector 21  - 128Kbytes
	{0x081C0000UL, 0x3FFFF},	// Sector 22 - 128Kbytes
	{0x081E0000UL, 0x3FFFF},	// Sector 23 - 128Kbytes
	};
#else
#error "No flash map for this chip"
#endif

#endif