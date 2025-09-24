#include <flash.hpp>
#include <string.h>

bool flash_access_enabled = false;

bool flash_base::init(bank_type tb)
{
#ifdef DUAL_MODE_MEM
	if(tb == bank_type::SINGLE_BANK)
	{
		flash_map = flash_map_SB;
		sector_count = sector_count_SB;
		return true;
	}
	else 
	if(tb == bank_type::DUAL_BANK)
	{
		flash_map = flash_map_DB;
		sector_count = sector_count_DB;
		return true;
	}
	else
		return false;
#else
		return true;
#endif
}

bool flash_base::enable_access()
{
	if(!flash_access_enabled)
	{
		FLASH->KEYR = 0x45670123;               //Enable access to FLASH
		FLASH->KEYR = 0xCDEF89AB;
		flash_access_enabled = 1;
		return true;
	}
	return false;
};

void flash_base::erase_sector(uint8_t sector)
{
	FLASH->CR |= FLASH_CR_SER; //Устанавливаем бит стирания одной страницы
	FLASH->CR |= sector << FLASH_CR_SNB_Pos; // Задаем её адрес
	FLASH->CR |= FLASH_CR_STRT; // Запускаем стирание

	while(!ready()){};//Ждем пока страница сотрется.

	FLASH->CR &= ~(FLASH_CR_SNB);
	FLASH->CR &= ~(FLASH_CR_SER); //Сбрасываем бит обратно
}

flash_data::Data_Status flash_data::read_data()
{
	if(status == Data_Status::DATA_OK)
	{
		memcpy(data, reinterpret_cast<uint8_t*>(flash_map[sector].address + (offset*(size + 1)) + 1), size);
	}

	return status;
}

void flash_data::write_data()
{
	if((status == Data_Status::DATA_OK)
	|| (status == Data_Status::DATA_CORRUPT))
	{
		if((((offset+1)*(size + 1)) > flash_map[sector].size)
		|| (status == Data_Status::DATA_CORRUPT))
		{
			offset = 0;
			erase_sector(sector);
		}
		else
		{
			write(flash_map[sector].address + (offset*(size + 1)), static_cast<uint8_t>(Data_Status::DATA_NOT_VALID));
			offset++;
		}
	}
	
	write(flash_map[sector].address + (offset*(size + 1)), static_cast<uint8_t>(Data_Status::DATA_OK));
	write_array(flash_map[sector].address + (offset*(size + 1)) + 1, data, size);
	status = Data_Status::DATA_OK;
}

void flash_data::find_offset()
{
	offset = 0;
	do
	{
		status = static_cast<Data_Status>(read<uint8_t>(flash_map[sector].address + (offset*(size + 1))));
		if(status == Data_Status::DATA_NOT_VALID)
		{
			if((++offset*(size + 1) > flash_map[sector].size))
			{
				offset = 0;
				break;
			}
		}
		else
		{
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
