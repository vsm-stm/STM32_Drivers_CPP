#ifndef FLASH_HPP_
#define FLASH_HPP_

#include "system.hpp"
#include "flash_config.h"
#include <string.h>

#if defined(STM32F7) or defined(STM32F4)
	#define SR_READY_BIT FLASH_SR_BSY
	using status_t = uint8_t;

#elif defined(STM32L0) or defined(STM32G0) // todo check L0
	#define SR_READY_BIT FLASH_SR_BSY1
	using status_t = uint64_t;

#endif


class flash_base
{
private:

public:
	enum class bank_type
	{
		SINGLE_BANK,
		DUAL_BANK
	};

	static bool init(bank_type tb = bank_type::SINGLE_BANK);
	static bool enable_access();
	static inline uint32_t ready() { return !(FLASH->SR & SR_READY_BIT); };
	static void erase_sector(uint8_t sector);
	static void mass_erase(uint8_t bank);
	static void clear_errors(){FLASH->SR = 0xFFFFFFFF;};

	template<typename T>
	static inline T read(uint32_t addr) { return (*reinterpret_cast<T*>(addr));}

	template<typename T>
	static void write(uint32_t addr, T *data, uint32_t size = 1);
};

class flash_data : private flash_base
{

public:

	enum class Data_Status : status_t
	{
		NO_DATA = ~status_t(0),
		DATA_OK = ~status_t(0) >> (sizeof(status_t) * 4),
		DATA_CORRUPT = 0x1,
		DATA_NOT_VALID = 0
	};

	flash_data(uint32_t sec, uint8_t *_data, uint32_t sz) :
		sector(sec),
		data(_data),
		size(sz)
	{

	};
	~flash_data(){};

	void SetUp(bank_type tb = bank_type::SINGLE_BANK)
	{
		init(tb);
		enable_access();
		find_offset();
	}

	Data_Status read_data();
	void write_data();

private:
	uint32_t sector;
	uint32_t offset;
	Data_Status status;

	uint8_t *data;
	uint32_t size;

	void find_offset();

};

template<typename T>
void flash_base::write(uint32_t addr, T *data, uint32_t size)
{
	static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);


	while(!ready()){};
	clear_errors();

	__disable_irq();
		
	FLASH->CR |= FLASH_CR_PG; //Разрешаем программирование флеша

#if defined(STM32G0)
	// G0: только 64-битная запись двумя последовательными 32-битными словами
	const uint32_t total = size * sizeof(T);
	const uint8_t* src = reinterpret_cast<const uint8_t*>(data);

	for(uint32_t i = 0; i < total; i += 8)
	{
		uint64_t dword;
		if(total - i < 8)
		{
			dword = 0xFFFFFFFFFFFFFFFFULL;
			memcpy(&dword, src + i, total - i);
		}
		else
		{
			dword = *reinterpret_cast<const uint64_t*>(src + i);
		}

		*reinterpret_cast<volatile uint32_t*>(addr) = (uint32_t)(dword);
		/* Barrier to ensure programming is performed in 2 steps, in right order
			(independently of compiler optimization behavior) */
		__ISB();
		*reinterpret_cast<volatile uint32_t*>(addr + 4) = (uint32_t)(dword >> 32);

		while(!ready()){};
	}
	__DSB();

#elif defined(STM32F7) or defined(STM32F4)
	uint8_t sz = sizeof(T);

	FLASH->CR &= ~(FLASH_CR_PSIZE_Msk);
	FLASH->CR |= ((sz == 8)? 3 : (sz >> 1)) << FLASH_CR_PSIZE_Pos;

	while(!ready()){}; //Ожидаем готовности флеша к записи

	for(uint32_t i = 0;i<size;i++)
	{
		*reinterpret_cast<volatile T*>(addr + i * sizeof(T)) = data[i];
		while(!ready()){};
		__DSB();
	}

#endif

	FLASH->CR &= ~(FLASH_CR_PG); //Запрещаем программирование флеша

	__enable_irq();
}

#endif