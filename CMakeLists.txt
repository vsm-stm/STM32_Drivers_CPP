# ============================================================================
# STM32_Drivers_CPP - the stm32_drivers library target
# ============================================================================
#
# NOT a standalone CMake project (no project(), no toolchain here) - always
# add_subdirectory()'d by whichever project pulls in this repository (today:
# stm32_cmake_base, from its own CMakeLists.txt), which by this point has
# already:
#   - called project() and resolved the arm-none-eabi toolchain,
#   - created the INTERFACE target `stm32_platform` (CMSIS/device include
#     directories, device defines, every compiler and linker flag),
#   - resolved the chip facts the drivers generate their own config from:
#     STM32_CORE, STM32_FLASH, STM32_SERIES_UC, STM32_NAME, STM32_VECTOR_FILE,
#     STM32_FLASH_SECTORS, STM32_DUAL_BANK,
#   - made download_one() / k_to_int() available (functions are global as
#     soon as defined; see stm32_cmake_base/cmake/functions.cmake).
#
# Two headers this library needs are DRIVERS-ONLY, so they are generated
# HERE rather than by the platform: flash_config.h (the sector map, read by
# flash.*) and irq_registry_config.h (IRQ dispatcher stubs - will not even
# compile without IRQ_Registry, which is ours). They land in
# cmsis-core/drivers_gen/ in the consuming project's own tree (vendored,
# just like the platform's downloaded linker script template).
#
# Configuration is entirely external, through ONE input:
#   STM32_DRIVERS_REQUESTED   optional module names, ';'-separated, e.g.
#                             "UART;SPI;DMA;TIM" - stm32_cmake_base sets it
#                             from "drivers" in project.json; can also be set
#                             manually with -DSTM32_DRIVERS_REQUESTED=...
#
# From the consumer side: target_link_libraries(<firmware> PRIVATE stm32_drivers)
# ============================================================================

add_library(stm32_drivers STATIC)
add_library(stm32::drivers ALIAS stm32_drivers)
target_link_libraries(stm32_drivers PUBLIC stm32_platform)

set(STM32_DRIVERS_REQUESTED "${STM32_DRIVERS_REQUESTED}" CACHE STRING
	"Optional driver modules (';'-separated, e.g. UART;SPI;DMA). Normally set from \"drivers\" in project.json.")

# Module dependencies: a module whose code calls into another module's .cpp
# pulls that module in, so e.g. "drivers": ["ADC"] links without listing
# DMA/TIM by hand. Resolved into a local copy of the list (the cache entry
# keeps what the user asked for).
set(_stm32_drivers_deps_UART DMA)
set(_stm32_drivers_deps_SPI  DMA)
set(_stm32_drivers_deps_TIM  DMA)
set(_stm32_drivers_deps_ADC  DMA TIM)
set(_stm32_drivers_req ${STM32_DRIVERS_REQUESTED})
foreach(_m ${STM32_DRIVERS_REQUESTED})
	list(APPEND _stm32_drivers_req ${_stm32_drivers_deps_${_m}})
endforeach()
foreach(_m ${_stm32_drivers_req})          # second level: ADC -> TIM -> DMA
	list(APPEND _stm32_drivers_req ${_stm32_drivers_deps_${_m}})
endforeach()
if(_stm32_drivers_req)
	list(REMOVE_DUPLICATES _stm32_drivers_req)
endif()
set(STM32_DRIVERS_REQUESTED ${_stm32_drivers_req})

# ############################################################################
# Generated config headers (flash_config.h, irq_registry_config.h)
# ############################################################################

# stm32_gen_irq_handlers(VECTOR_FILE TEMPLATE OUT_FILE)
#   Parses the vector table as text: IRQ_TABLE_SIZE from the entry count, and
#   one `extern "C" void X_IRQHandler(){ IRQ_Registry::Dispatch(X_IRQn); }`
#   per *_IRQHandler found. Relies on vector_<device>.c keeping its formatting.
function(stm32_gen_irq_handlers VECTOR_FILE TEMPLATE OUT_FILE)
	file(STRINGS "${VECTOR_FILE}" _all_entries REGEX "\\(uint32_t\\)")
	list(LENGTH _all_entries _total)
	math(EXPR IRQ_TABLE_SIZE "${_total} - 16")

	if(STM32_SERIES_UC MATCHES "STM32G0")
		set(IRQ_MAX_SHARED 4)   # DMA1_Ch4_7(_DMA2_Ch1_5)_DMAMUX1_OVR
	else()
		set(IRQ_MAX_SHARED 3)   # ADC_IRQn shared by ADC1/2/3 on F4/F7
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

# ---- download two shared templates, generate two headers -------------------
# Templates - downloads -> next to the platform's other downloads.
# Our generated headers -> cmsis-core/drivers_gen/ in the project tree.
set(_dl  "${CMAKE_SOURCE_DIR}/cmsis-core/download_files/cmake")
set(_gen "${CMAKE_SOURCE_DIR}/cmsis-core/drivers_gen")

download_one("flash_config.h.in"        "${_dl}" "cmake/flash_config.h.in")
download_one("irq_registry_config.h.in" "${_dl}" "cmake/irq_registry_config.h.in")

# flash_config.h - the erase-sector list for this specific device is already
# a ready-made CMake list in the downloaded STM32<F>-map.cmake (generated by
# STM32-base_files/flash_sectors.py). Nothing is computed here: we just look
# it up by the chip's flash size and format it into the template's
# @FLASH_CFG_SECTORS_LIST@.
set(_sect_var "${STM32_CORE}_SECTORS_${STM32_FLASH}")       # e.g. STM32F4_SECTORS_512K
set(_sectors  "${${_sect_var}}")                            # e.g. 0x08000000;16384;...
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
# Sources
# ############################################################################

# ---- always-on core ------------------------------------------------------
# System / RCC / GPIO / Flash / IRQ_Registry - load-bearing: almost nothing
# works without them (the bootloader included), so they are never optional.
target_sources(stm32_drivers PRIVATE
	src/system.cpp
	src/rcc.cpp
	src/gpio.cpp
	src/flash.cpp
	src/irq_registry.cpp
)
target_include_directories(stm32_drivers PUBLIC src)

# ---- module table -------------------------------------------------------
# stm32_drivers_module(NAME [SOURCES ...] [INCLUDE ...])
#   If NAME is in STM32_DRIVERS_REQUESTED, its SOURCES/INCLUDE get added.
#   The set of NAMEs declared here IS the set of valid module names - the
#   typo check below builds its own list from these calls, there is nothing
#   to keep in sync separately.
#
# Adding / renaming / removing a module = editing exactly one line here.
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
# i2c.hpp does `#include <i2c_timing_utility.h>` (angle brackets) - src/tools
# must be a real include directory, not just an include from the same folder.
stm32_drivers_module(I2C    SOURCES src/i2c.cpp src/tools/i2c_timing_utility.c  INCLUDE src/tools)
stm32_drivers_module(MODBUS SOURCES Interfaces/Modbus-slave.cpp                 INCLUDE Interfaces)
stm32_drivers_module(CONTAINERS INCLUDE Containers)          # headers only
# PID - the author's own controller (math/PID.*): variable dT, dead zone,
# anti-windup. A simpler recurrence-based alternative is arm_pid_* in the DSP module.
stm32_drivers_module(PID    SOURCES math/PID.c                        INCLUDE math)
# CRC32 - portable software implementation (no HW peripheral, no lookup table),
# see crc/crc32.h for why. Can also be fetched as a single file without the rest
# of the driver library - stm32-bootloader does this itself, in its own
# CMakeLists.txt, for standalone builds where "drivers" is off.
stm32_drivers_module(CRC    SOURCES crc/crc32.c                       INCLUDE crc)

# ---- DSP: a subset of Arm's current CMSIS-DSP (Apache-2.0) ---------------
# Fast math (sin/cos/sqrt/atan2/...), controllers (PID), and common tables.
# Not vendored into the repository, pulled in via FetchContent by tag
# (into _deps/ in the build directory). The whole of CMSIS-DSP is not built -
# only these directories; f16 kernels are excluded. The name is registered
# in the list of valid modules so a typo in "drivers" is caught the same way
# as for every other one.
list(APPEND _stm32_drivers_declared DSP)
if("DSP" IN_LIST STM32_DRIVERS_REQUESTED)
	include(FetchContent)
	FetchContent_Declare(cmsis_dsp
		GIT_REPOSITORY https://github.com/ARM-software/CMSIS-DSP.git
		GIT_TAG        v1.16.2
		GIT_SHALLOW    TRUE
		SOURCE_SUBDIR  _no_cmake_here_   # clone only, its own CMakeLists is not run
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

# ---- catch a typo in the requested list ---------------------------------
foreach(_req ${STM32_DRIVERS_REQUESTED})
	if(NOT "${_req}" IN_LIST _stm32_drivers_declared)
		message(FATAL_ERROR
			"stm32_drivers: unknown module '${_req}' in STM32_DRIVERS_REQUESTED "
			"(from \"drivers\" in project.json). Known: ${_stm32_drivers_declared}")
	endif()
endforeach()
