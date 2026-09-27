# ============================================================================
# STM32_Drivers_CPP — цель библиотеки stm32_drivers
# ============================================================================
#
# НЕ самостоятельный CMake-проект (здесь нет ни project(), ни тулчейна) —
# всегда add_subdirectory()-ится тем проектом, который тянет этот репозиторий
# (сегодня — stm32_cmake_base, из его CMakeLists.txt), а тот к этому моменту
# уже:
#   - вызвал project() и разрешил тулчейн arm-none-eabi,
#   - создал INTERFACE-цель `stm32_platform` (include-каталоги CMSIS/устройства,
#     дефайны устройства, все флаги компилятора и линкера),
#   - разрешил факты о чипе, из которых драйверы генерируют свой конфиг:
#     STM32_CORE, STM32_FLASH, STM32_SERIES_UC, STM32_NAME, STM32_VECTOR_FILE,
#     STM32_FLASH_SECTORS, STM32_DUAL_BANK,
#   - сделал доступными download_one() / k_to_int() (функции глобальны сразу
#     после определения; см. stm32_cmake_base/cmake/functions.cmake).
#
# Два нужных этой библиотеке заголовка — ТОЛЬКО ДЛЯ ДРАЙВЕРОВ, поэтому
# генерируются ЗДЕСЬ, а не платформой: flash_config.h (карта секторов, читает
# flash.*) и irq_registry_config.h (заглушки диспетчера IRQ — вообще не
# скомпилируются без IRQ_Registry, а он наш). Ложатся в
# cmsis-core/drivers_gen/ в дереве конечного проекта (vendored, как и
# скачанный шаблон линкер-скрипта платформы).
#
# Конфигурация полностью внешняя, через ОДИН вход:
#   STM32_DRIVERS_REQUESTED   имена опциональных модулей через ';', напр.
#                             "UART;SPI;DMA;TIM" — stm32_cmake_base задаёт его
#                             из "drivers" в project.json; можно и вручную
#                             -DSTM32_DRIVERS_REQUESTED=...
#
# Со стороны потребителя: target_link_libraries(<прошивка> PRIVATE stm32_drivers)
# ============================================================================

add_library(stm32_drivers STATIC)
add_library(stm32::drivers ALIAS stm32_drivers)
target_link_libraries(stm32_drivers PUBLIC stm32_platform)

set(STM32_DRIVERS_REQUESTED "${STM32_DRIVERS_REQUESTED}" CACHE STRING
	"Опциональные модули драйверов (';'-разделённые, напр. UART;SPI;DMA). Обычно задаётся из \"drivers\" в project.json.")

# ############################################################################
# Генерируемые заголовки конфига (flash_config.h, irq_registry_config.h)
# ############################################################################

# stm32_gen_irq_handlers(VECTOR_FILE TEMPLATE OUT_FILE)
#   Парсит таблицу векторов текстом: IRQ_TABLE_SIZE из числа записей и по одной
#   `extern "C" void X_IRQHandler(){ IRQ_Registry::Dispatch(X_IRQn); }` на
#   каждый найденный *_IRQHandler. Зависит от того, что vector_<device>.c
#   сохраняет своё форматирование.
function(stm32_gen_irq_handlers VECTOR_FILE TEMPLATE OUT_FILE)
	file(STRINGS "${VECTOR_FILE}" _all_entries REGEX "\\(uint32_t\\)")
	list(LENGTH _all_entries _total)
	math(EXPR IRQ_TABLE_SIZE "${_total} - 16")

	if(STM32_SERIES_UC MATCHES "STM32G0")
		set(IRQ_MAX_SHARED 3)   # DMA1_Ch4_5_DMAMUX1_OVR
	else()
		set(IRQ_MAX_SHARED 2)   # пары таймеров на F4/F7; безопасное значение по умолчанию
	endif()

	file(STRINGS "${VECTOR_FILE}" _handler_lines REGEX "void [A-Za-z0-9_]+_IRQHandler\\(void\\)")
	set(IRQ_HANDLERS_CODE "")
	foreach(_line ${_handler_lines})
		string(REGEX MATCH "void ([A-Za-z0-9_]+_IRQHandler)" _match "${_line}")
		if(CMAKE_MATCH_1)
			set(_h "${CMAKE_MATCH_1}")
			string(REPLACE "_IRQHandler" "_IRQn" _irqn "${_h}")
			string(APPEND IRQ_HANDLERS_CODE
				"extern \"C\" void ${_h}() { IRQ_Registry::Dispatch(${_irqn}); }\n")
		endif()
	endforeach()

	configure_file("${TEMPLATE}" "${OUT_FILE}" @ONLY)
	message(STATUS "IRQ registry generated: ${OUT_FILE}  (IRQ_TABLE_SIZE=${IRQ_TABLE_SIZE}, IRQ_MAX_SHARED=${IRQ_MAX_SHARED})")
endfunction()

# ---- качаем два общих шаблона, генерируем два заголовка --------------------
# Шаблоны — загрузки -> рядом с прочими загрузками платформы.
# Наши сгенерированные заголовки -> cmsis-core/drivers_gen/ в дереве проекта.
set(_dl  "${CMAKE_SOURCE_DIR}/cmsis-core/download_files/cmake")
set(_gen "${CMAKE_SOURCE_DIR}/cmsis-core/drivers_gen")

download_one("flash_config.h.in"        "${_dl}" "cmake/flash_config.h.in")
download_one("irq_registry_config.h.in" "${_dl}" "cmake/irq_registry_config.h.in")

# flash_config.h — список секторов стирания для этого конкретного устройства
# уже готовый CMake-список в скачанном STM32<F>-map.cmake (сгенерирован
# STM32-base_files/flash_sectors.py). Здесь ничего не вычисляется: берём его
# по объёму флеша чипа и форматируем в @FLASH_CFG_SECTORS_LIST@ шаблона.
set(_sect_var "${STM32_CORE}_SECTORS_${STM32_FLASH}")       # напр. STM32F4_SECTORS_512K
set(_sectors  "${${_sect_var}}")                            # напр. 0x08000000;16384;...
if(NOT _sectors)
	message(FATAL_ERROR
		"stm32_drivers: ${_sect_var} not found for ${STM32_DEVICE_UC} (${STM32_FLASH}). "
		"Run STM32-base_files/flash_sectors.py to (re)generate the map tables.")
endif()

set(FLASH_CFG_SECTORS_LIST "")
set(_rest ${_sectors})
set(_i 0)
while(_rest)
	list(POP_FRONT _rest _addr _size)
	string(APPEND FLASH_CFG_SECTORS_LIST "    { ${_addr}UL, ${_size}UL }, // Sector ${_i}\n")
	math(EXPR _i "${_i} + 1")
endwhile()

set(FLASH_CFG_COUNT     "${_i}")
set(FLASH_CFG_DEVICE    "${STM32_DEVICE_UC}")
set(FLASH_CFG_FLASH_STR "${STM32_FLASH}")
if(STM32_DUAL_BANK)
	set(FLASH_CFG_DUAL_BANK true)
else()
	set(FLASH_CFG_DUAL_BANK false)
endif()

configure_file("${_dl}/flash_config.h.in" "${_gen}/flash_config.h" @ONLY)
message(STATUS "flash_config.h: ${FLASH_CFG_COUNT} sectors, dual_bank=${FLASH_CFG_DUAL_BANK}")

stm32_gen_irq_handlers("${STM32_VECTOR_FILE}"
	"${_dl}/irq_registry_config.h.in" "${_gen}/irq_registry_config.h")

target_include_directories(stm32_drivers PUBLIC "${_gen}")

# ############################################################################
# Исходники
# ############################################################################

# ---- всегда включённое ядро ---------------------------------------------
# System / RCC / GPIO / Flash / IRQ_Registry — несущие: без них почти ничего
# не работает (загрузчик в том числе), поэтому они никогда не опциональны.
target_sources(stm32_drivers PRIVATE
	src/system.cpp
	src/rcc.cpp
	src/gpio.cpp
	src/flash.cpp
	src/irq_registry.cpp
)
target_include_directories(stm32_drivers PUBLIC src)

# ---- таблица модулей ---------------------------------------------------
# stm32_drivers_module(NAME [SOURCES ...] [INCLUDE ...])
#   Если NAME есть в STM32_DRIVERS_REQUESTED — его SOURCES/INCLUDE добавляются.
#   Набор объявленных здесь NAME И ЕСТЬ набор допустимых имён модулей —
#   проверка опечаток ниже строит свой список из этих вызовов, синхронизировать
#   нечего.
#
# Добавить / переименовать / убрать модуль = правка ровно одной строки здесь.
set(_stm32_drivers_declared "")
function(stm32_drivers_module NAME)
	cmake_parse_arguments(M "" "" "SOURCES;INCLUDE" ${ARGN})

	list(APPEND _stm32_drivers_declared "${NAME}")
	set(_stm32_drivers_declared "${_stm32_drivers_declared}" PARENT_SCOPE)

	if(NOT "${NAME}" IN_LIST STM32_DRIVERS_REQUESTED)
		return()
	endif()

	if(M_SOURCES)
		list(TRANSFORM M_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
		target_sources(stm32_drivers PRIVATE ${M_SOURCES})
	endif()
	foreach(_inc ${M_INCLUDE})
		target_include_directories(stm32_drivers PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/${_inc}")
	endforeach()
endfunction()

stm32_drivers_module(UART   SOURCES src/uart.cpp)
stm32_drivers_module(SPI    SOURCES src/spi.cpp)
stm32_drivers_module(DMA    SOURCES src/dma.cpp)
stm32_drivers_module(TIM    SOURCES src/tim.cpp)
stm32_drivers_module(RTC    SOURCES src/rtc.cpp)
stm32_drivers_module(ADC    SOURCES src/adc.cpp)
stm32_drivers_module(DAC    SOURCES src/dac.cpp)
# i2c.hpp делает `#include <i2c_timing_utility.h>` (угловые скобки) — src/tools
# должен быть настоящим include-каталогом, а не просто include из той же папки.
stm32_drivers_module(I2C    SOURCES src/i2c.cpp src/tools/i2c_timing_utility.c  INCLUDE src/tools)
stm32_drivers_module(MODBUS SOURCES Interfaces/Modbus-slave.cpp                 INCLUDE Interfaces)
stm32_drivers_module(CONTAINERS INCLUDE Containers)          # только заголовки
# PID — собственный регулятор автора (math/PID.*): переменный dT, мёртвая зона,
# антивиндап. Простая рекуррентная альтернатива — arm_pid_* в модуле DSP.
stm32_drivers_module(PID    SOURCES math/PID.c                        INCLUDE math)

# ---- DSP: подмножество актуального CMSIS-DSP от Arm (Apache-2.0) ---------
# Быстрая математика (sin/cos/sqrt/atan2/...), регуляторы (PID) и общие
# таблицы. Не копируется в репозиторий, а подтягивается FetchContent-ом по
# тегу (в _deps/ build-каталога). Весь CMSIS-DSP не собирается — только эти
# каталоги; f16-ядра отключены. Регистрируем имя в списке допустимых модулей,
# чтобы опечатка в "drivers" ловилась так же, как у остальных.
list(APPEND _stm32_drivers_declared DSP)
if("DSP" IN_LIST STM32_DRIVERS_REQUESTED)
	include(FetchContent)
	FetchContent_Declare(cmsis_dsp
		GIT_REPOSITORY https://github.com/ARM-software/CMSIS-DSP.git
		GIT_TAG        v1.16.2
		GIT_SHALLOW    TRUE
		SOURCE_SUBDIR  _no_cmake_here_   # только клонируем, его CMakeLists не запускаем
	)
	FetchContent_MakeAvailable(cmsis_dsp)

	set(_dsp "${cmsis_dsp_SOURCE_DIR}")
	file(GLOB _dsp_src
		"${_dsp}/Source/FastMathFunctions/*.c"
		"${_dsp}/Source/ControllerFunctions/*.c"
		"${_dsp}/Source/CommonTables/arm_common_tables.c"
		"${_dsp}/Source/CommonTables/arm_const_structs.c"
	)
	list(FILTER _dsp_src EXCLUDE REGEX "_f16\\.c$")

	target_sources(stm32_drivers PRIVATE ${_dsp_src})
	target_include_directories(stm32_drivers
		PUBLIC  "${_dsp}/Include"
		PRIVATE "${_dsp}/PrivateInclude")
endif()

# ---- ловим опечатку в запрошенном списке -------------------------------
foreach(_req ${STM32_DRIVERS_REQUESTED})
	if(NOT "${_req}" IN_LIST _stm32_drivers_declared)
		message(FATAL_ERROR
			"stm32_drivers: unknown module '${_req}' in STM32_DRIVERS_REQUESTED "
			"(из \"drivers\" в project.json). Known: ${_stm32_drivers_declared}")
	endif()
endforeach()
