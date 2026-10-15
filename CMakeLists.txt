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
set_property(TARGET stm32_drivers PROPERTY STM32_DMA_REGIONS_SUPPORTED TRUE)
set_property(TARGET stm32_drivers PROPERTY STM32_FLASH_BANK_MODES_SUPPORTED TRUE)
add_library(stm32::drivers ALIAS stm32_drivers)
target_link_libraries(stm32_drivers PUBLIC stm32_platform)

set(STM32_DRIVERS_REQUESTED "${STM32_DRIVERS_REQUESTED}" CACHE STRING
	"Optional driver modules (';'-separated, e.g. UART;SPI;DMA). Normally set from \"drivers\" in project.json.")

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
		set(IRQ_MAX_SHARED 3)   # DMA1_Ch4_5_DMAMUX1_OVR
	else()
		set(IRQ_MAX_SHARED 2)   # timer pairs on F4/F7; a safe default
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
# resolved by the base from its per-model bank mode map. Use that exact list
# for flash_config.h so the driver and firmware linker cannot select different
# layouts. Each record is address, size, bank, hardware erase selector.
if(NOT STM32_FLASH_ERASE_UNITS OR NOT STM32_FLASH_LAYOUT_VERSION EQUAL 3)
	message(FATAL_ERROR
		"stm32_drivers: update stm32_cmake_base and STM32-base_files (Flash layout format 3 required)")
endif()
file(READ "${_dl}/flash_config.h.in" _flash_template)
string(FIND "${_flash_template}" "@FLASH_CFG_FORMAT@" _flash_format_hook)
if(_flash_format_hook LESS 0)
	message(FATAL_ERROR "Refresh flash_config.h.in from STM32-base_files (Flash format 3 required)")
endif()

set(FLASH_CFG_SECTORS_LIST "")
set(_rest ${STM32_FLASH_ERASE_UNITS})
set(_i 0)
while(_rest)
	list(POP_FRONT _rest _addr _size _bank _number)
	string(APPEND FLASH_CFG_SECTORS_LIST "    { ${_addr}UL, ${_size}UL, ${_bank}U, ${_number}U }, // Erase unit ${_i}\n")
	math(EXPR _i "${_i} + 1")
endwhile()

set(FLASH_CFG_COUNT     "${_i}")
set(FLASH_CFG_FORMAT    3)
set(FLASH_CFG_BANK_SWITCHABLE false)
if(STM32_FLASH_BANK_SWITCHABLE)
	set(FLASH_CFG_BANK_SWITCHABLE true)
endif()
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
# stm32_drivers_module(NAME [SOURCES ...] [INCLUDE ...] [DEPENDS ...])
#   Register the module; sources are added after resolving dependencies.
#   The set of NAMEs declared here IS the set of valid module names - the
#   typo check below builds its own list from these calls, there is nothing
#   to keep in sync separately.
#
# Adding / renaming / removing a module = editing exactly one line here.
set(_stm32_drivers_declared "")
function(stm32_drivers_module NAME)
	cmake_parse_arguments(M "" "" "SOURCES;INCLUDE;DEPENDS" ${ARGN})

	list(APPEND _stm32_drivers_declared "${NAME}")
	set(_stm32_drivers_declared "${_stm32_drivers_declared}" PARENT_SCOPE)

	set(_stm32_module_${NAME}_SOURCES "${M_SOURCES}" PARENT_SCOPE)
	set(_stm32_module_${NAME}_INCLUDE "${M_INCLUDE}" PARENT_SCOPE)
	set(_stm32_module_${NAME}_DEPENDS "${M_DEPENDS}" PARENT_SCOPE)
endfunction()

stm32_drivers_module(UART   SOURCES src/uart.cpp DEPENDS DMA)
stm32_drivers_module(SPI    SOURCES src/spi.cpp DEPENDS DMA)
stm32_drivers_module(DMA    SOURCES src/dma.cpp)
stm32_drivers_module(TIM    SOURCES src/tim.cpp DEPENDS DMA)
stm32_drivers_module(RTC    SOURCES src/rtc.cpp)
stm32_drivers_module(ADC    SOURCES src/adc.cpp DEPENDS DMA TIM)
stm32_drivers_module(DAC    SOURCES src/dac.cpp)
# i2c.hpp does `#include <i2c_timing_utility.h>` (angle brackets) - src/tools
# must be a real include directory, not just an include from the same folder.
stm32_drivers_module(I2C    SOURCES src/i2c.cpp src/tools/i2c_timing_utility.c  INCLUDE src/tools DEPENDS DMA)
stm32_drivers_module(MODBUS SOURCES Interfaces/Modbus-slave.cpp                 INCLUDE Interfaces DEPENDS UART DMA)
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
stm32_drivers_module(DSP)

# Example project.json: "drivers": ["MODBUS"] resolves UART and DMA automatically.
# Request modules in project.json; do not patch this target's source list per project.
# Header-only containers contribute include paths; DSP is fetched only if selected.
# Resolve direct and transitive dependencies once, regardless of declaration order.
# Keep the user's requested list unchanged, including its cache value.
set(_stm32_pending ${STM32_DRIVERS_REQUESTED})
set(_stm32_modules "")
while(NOT "${_stm32_pending}" STREQUAL "")
	list(POP_FRONT _stm32_pending _module)
	if(NOT _module IN_LIST _stm32_drivers_declared)
		message(FATAL_ERROR
			"stm32_drivers: unknown module '${_module}' in STM32_DRIVERS_REQUESTED "
			"(from \"drivers\" in project.json). Known: ${_stm32_drivers_declared}")
	endif()
	if(_module IN_LIST _stm32_modules)
		continue()
	endif()
	list(APPEND _stm32_modules "${_module}")
	list(APPEND _stm32_pending ${_stm32_module_${_module}_DEPENDS})
endwhile()
message(STATUS "Drivers modules: ${_stm32_modules}")

foreach(_module IN LISTS _stm32_modules)
	set(_sources ${_stm32_module_${_module}_SOURCES})
	if(_sources)
		list(TRANSFORM _sources PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
		target_sources(stm32_drivers PRIVATE ${_sources})
	endif()
	foreach(_inc IN LISTS _stm32_module_${_module}_INCLUDE)
		target_include_directories(stm32_drivers PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/${_inc}")
	endforeach()
endforeach()

if("DSP" IN_LIST _stm32_modules)
	include(FetchContent)
	FetchContent_Declare(cmsis_dsp
		GIT_REPOSITORY https://github.com/ARM-software/CMSIS-DSP.git
		GIT_TAG        v1.16.2
		GIT_SHALLOW    TRUE
		SOURCE_SUBDIR  _no_cmake_here_   # clone only, its own CMakeLists is not run
	)
	FetchContent_MakeAvailable(cmsis_dsp)

	set(_dsp "${cmsis_dsp_SOURCE_DIR}")
	# This subset is FastMath + Controller + common tables, not the complete CMSIS-DSP.
	# arm_pid_* is a separate recurrence-based controller from math/PID's variable-dT API.
	# Individual kernels only: the aggregate *.c files include those same
	# kernels again, and FastMathFunctionsF16.c bypasses the _f16.c filter.
	file(GLOB _dsp_src
		"${_dsp}/Source/FastMathFunctions/arm_*.c"
		"${_dsp}/Source/ControllerFunctions/arm_*.c"
		"${_dsp}/Source/CommonTables/arm_common_tables.c"
		"${_dsp}/Source/CommonTables/arm_const_structs.c"
	)
	list(FILTER _dsp_src EXCLUDE REGEX "_f16\\.c$")

	target_sources(stm32_drivers PRIVATE ${_dsp_src})
	target_include_directories(stm32_drivers
		PUBLIC  "${_dsp}/Include"
		PRIVATE "${_dsp}/PrivateInclude")
endif()
