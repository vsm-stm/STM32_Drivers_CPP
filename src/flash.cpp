#include "flash.hpp"

// Биты управления стиранием различаются между семействами STM32:
// F4/F7 — постраничное стирание через SER (Sector Erase) + номер сектора в SNB
// G0/L0 — постраничное стирание через PER (Page Erase)  + номер страницы в PNB
#if defined(STM32F7) or defined(STM32F4)
	#define CR_EARSE_BIT	FLASH_CR_SER
	#define CR_SERCTOR_Pos	FLASH_CR_SNB_Pos
	#define CR_SERCTOR_Msk	FLASH_CR_SNB_Msk

#elif defined(STM32L0) or defined(STM32G0) // todo check L0
	#define CR_EARSE_BIT 	FLASH_CR_PER
	#define CR_SERCTOR_Pos	FLASH_CR_PNB_Pos
	#define CR_SERCTOR_Msk	FLASH_CR_PNB_Msk

#endif

// Глобальный флаг: флеш уже разблокирован. Повторная подача ключей после
// первой разблокировки приведёт к аппаратной блокировке (защита от атак).
bool flash_access_enabled = false;

bool flash_base::init(bank_type tb)
{
	if constexpr (FLASH_DUAL_BANK)
	{
		// Раскладка секторов обоих банков задана в flash_sectors[] на этапе компиляции
		// через CMake (STM32_DUAL_BANK). Параметр bank_type оставлен для совместимости API.
		return (tb == bank_type::SINGLE_BANK || tb == bank_type::DUAL_BANK);
	}
	return true;
}

bool flash_base::enable_access()
{
	if(!flash_access_enabled)
	{
		// Аппаратная процедура разблокировки флеша: два ключа должны быть записаны
		// строго последовательно. Любая другая запись в KEYR между ними заблокирует флеш
		// до следующего сброса. Значения ключей фиксированы в Reference Manual.
		FLASH->KEYR = 0x45670123;
		FLASH->KEYR = 0xCDEF89AB;
		flash_access_enabled = 1;
		return true;
	}
	return false;
};

void flash_base::erase_sector(uint8_t sector)
{
	if (sector >= FLASH_PAGE_COUNT) return;

	while(!ready()){};
	clear_errors();

	FLASH->CR |= CR_EARSE_BIT;            // Режим постраничного стирания
	FLASH->CR |= sector << CR_SERCTOR_Pos; // Номер страницы/сектора
	FLASH->CR |= FLASH_CR_STRT;            // Запуск операции

	while(!ready()){};

	// Сбрасываем биты после завершения — иначе следующий доступ к флешу
	// может быть ошибочно интерпретирован как команда стирания
	FLASH->CR &= ~(CR_SERCTOR_Msk);
	FLASH->CR &= ~(CR_EARSE_BIT);
}

void flash_base::mass_erase(uint8_t bank)
{
	// bank — битовая маска: бит 0 = банк 1, бит 1 = банк 2.
	// 0 — невалидное значение, > 3 — выходит за пределы допустимых банков.
	if((!bank) || (bank > 3)) return;

	while(!ready()){};
	clear_errors();

	if(bank & 1) FLASH->CR |= FLASH_CR_MER1;
	// if(bank & 2) FLASH->CR |= FLASH_CR_MER2; todo check if MER2 is supported on STM32G0

	FLASH->CR |= FLASH_CR_STRT;

	while(!ready()){};

	FLASH->CR &= ~(FLASH_CR_MER1);
}

// -----------------------------------------------------------------------------
// Формат записи в секторе флеша (wear leveling):
//
//   [ status_t ][ data ... ][ padding ][ status_t ][ data ... ][ padding ] ...
//   |<---sz--->|<----------size------->|           |
//   |<--------------------stride------------------->|
//
// status_t = uint8_t на F4/F7/H7, uint64_t на G0 (минимальная единица записи).
//
// Вычисление stride: статус (sz байт) + данные, округлённые вверх до кратного sz.
// Округление гарантирует, что следующая запись начнётся по выровненному адресу.
//
//   stride = sz + ceil(size / sz) * sz
//          = sz + ((size + sz - 1) / sz) * sz
//
// Примеры для uint64_t (sz=8):
//   size=5  -> 8 + ((5+7)/8)*8  = 8 + 8  = 16
//   size=8  -> 8 + ((8+7)/8)*8  = 8 + 8  = 16
//   size=9  -> 8 + ((9+7)/8)*8  = 8 + 16 = 24
//
// Примеры для uint8_t (sz=1):
//   size=5  -> 1 + ((5+0)/1)*1  = 1 + 5  = 6   (нет выравнивания, sz=1)
// -----------------------------------------------------------------------------

flash_data::Data_Status flash_data::read_data()
{
	const uint32_t sz     = sizeof(status_t);
	const uint32_t stride = sz + ((size + sz - 1) / sz) * sz;

	// Читаем данные только если они заведомо валидны.
	// Адрес данных = начало записи + sizeof(status_t) (пропускаем байт/слово статуса).
	if(status == Data_Status::DATA_OK)
	{
		memcpy(data, reinterpret_cast<uint8_t*>(flash_sectors[sector].address + offset * stride + sz), size);
	}

	return status;
}

void flash_data::write_data()
{
	const uint32_t sz     = sizeof(status_t);
	const uint32_t stride = sz + ((size + sz - 1) / sz) * sz;

	if((status == Data_Status::DATA_OK)
	|| (status == Data_Status::DATA_CORRUPT))
	{
		if(((offset + 1) * stride > flash_sectors[sector].size)
		|| (status == Data_Status::DATA_CORRUPT))
		{
			// Сектор заполнен или данные повреждены — стираем и начинаем с начала.
			// После стирания все байты = 0xFF (NO_DATA).
			offset = 0;
			erase_sector(sector);
		}
		else
		{
			// Инвалидируем текущую запись: записываем DATA_NOT_VALID (0x00) в статус.
			// Флеш позволяет менять биты только 1->0, поэтому запись нуля всегда допустима.
			status_t not_valid = static_cast<status_t>(Data_Status::DATA_NOT_VALID);
			write(flash_sectors[sector].address + offset * stride, &not_valid);
			offset++; // Переходим к следующему слоту
		}
	}

	// Записываем новую запись: сначала статус, затем данные.
	// Разделение важно: если питание пропадёт между ними, статус останется невалидным
	// и запись не будет прочитана как корректная при следующем старте.
	status_t ok = static_cast<status_t>(Data_Status::DATA_OK);
	write(flash_sectors[sector].address + offset * stride, &ok);
	write(flash_sectors[sector].address + offset * stride + sz, data, size);
	status = Data_Status::DATA_OK;
}

void flash_data::find_offset()
{
	const uint32_t sz     = sizeof(status_t);
	const uint32_t stride = sz + ((size + sz - 1) / sz) * sz;

	// Сканируем сектор с начала, пропуская инвалидированные записи (DATA_NOT_VALID),
	// пока не найдём актуальную запись (DATA_OK) или конец записанной области (NO_DATA).
	offset = 0;
	do
	{
		status = static_cast<Data_Status>(read<status_t>(flash_sectors[sector].address + offset * stride));

		if(status == Data_Status::DATA_NOT_VALID)
		{
			// Эта запись была инвалидирована при предыдущей записи — пропускаем
			offset++;
			if(offset * stride > flash_sectors[sector].size)
			{
				// Дошли до конца сектора не найдя валидных данных — сбрасываем
				offset = 0;
				break;
			}
		}
		else
		{
			// DATA_OK    — нашли актуальную запись, offset указывает на неё
			// NO_DATA    — сектор чистый (или с этого места не писали)
			// иное       — повреждение, будем перезаписывать с начала после стирания
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
