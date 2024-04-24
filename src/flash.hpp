#ifndef FLASH_HPP_
#define FLASH_HPP_

#if defined(STM32F4)
#include <system_f4.hpp>
#elif defined(STM32F7)
#include <system_f7.hpp>
#endif
#include <flash_maps.h>

class flash_base
{
private:

public:
	static void Enable_access();
	static uint32_t ready();
	static void erase_sector(uint8_t sector);

	template<typename T>
	static T read(uint32_t addr);

	template<typename T>
	static void write(uint32_t addr, T data);

	template<typename T>
	static void write_array(uint32_t addr, T *data, uint32_t size);
};

class flash_data : private flash_base
{

public:

	enum class Data_Status
	{
		NO_DATA = 0xFF,
		DATA_OK = 0xF,
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

	void SetUp()
	{
		Enable_access();
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
T flash_base::read(uint32_t addr)
{
	return (*reinterpret_cast<T*>(addr));
};

template<typename T>
void flash_base::write(uint32_t addr, T data)
{
	uint8_t sz = sizeof(data);
	
	FLASH->CR |= FLASH_CR_PG; //Разрешаем программирование флеша

	FLASH->CR &= ~(FLASH_CR_PSIZE_Msk);
	FLASH->CR |= ((sz == 8)? 3 : (sz >> 1)) << FLASH_CR_PSIZE_Pos;

	while(!ready()){}; //Ожидаем готовности флеша к записи

	*reinterpret_cast<T*>(addr) = data;

	while(!ready()){};

	FLASH->CR &= ~(FLASH_CR_PG); //Запрещаем программирование флеша
}

template<typename T>
void flash_base::write_array(uint32_t addr, T *data, uint32_t size)
{
	uint8_t sz = sizeof(data[0]);
	
	FLASH->CR |= FLASH_CR_PG; //Разрешаем программирование флеша

	FLASH->CR &= ~(FLASH_CR_PSIZE_Msk);
	FLASH->CR |= ((sz == 4)? 3 : (sz >> 1)) << FLASH_CR_PSIZE_Pos;

	while(!ready()){}; //Ожидаем готовности флеша к записи

	for(uint32_t i = 0;i<size;i++)
	{
		*reinterpret_cast<T*>(addr + i) = data[i];
		while(!ready()){};
	}

	FLASH->CR &= ~(FLASH_CR_PG); //Запрещаем программирование флеша
}


#endif