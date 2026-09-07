# ============================================================================
# STM32_Drivers_CPP — stm32_drivers library target
# ============================================================================
#
# This is NOT a standalone CMake project (no project()/toolchain here) - it is
# always add_subdirectory()'d by whichever project fetches this repo, today
# via FetchContent_MakeAvailable(Drivers) at the end of stm32_cmake_base's
# cmake/cmsis-download.cmake, which by then has already:
#   - called project() and resolved the arm-none-eabi toolchain,
#   - resolved DEVICE into STM32_CORE / STM32_DEVICE_UC / c_target,
#   - generated Device/Include/flash_config.h and irq_registry_config.h.
# add_subdirectory() inherits the caller's variables as they stand at the
# point of the call, so those are simply read below - see cmsis-download.cmake
# for where they come from.
#
# Configuration is entirely external, through ONE input:
#
#   STM32_DRIVERS_REQUESTED   ';'-separated list of optional module names,
#                             e.g. "UART;SPI;DMA;TIM"
#
# set by stm32_cmake_base's stm32_project(DRIVERS ...) upstream, or directly
# with -DSTM32_DRIVERS_REQUESTED=... for a standalone build. There are no
# per-module option()s and no separate "known modules" list - the module
# table below is the single source of truth: the set of valid names is
# exactly what stm32_drivers_module() is called with.
#
# Consumer side, once linked in:
#     target_link_libraries(<your-firmware-target> PRIVATE stm32_drivers)
# ============================================================================

add_library(stm32_drivers STATIC)
add_library(stm32::drivers ALIAS stm32_drivers)

set(STM32_DRIVERS_REQUESTED "${STM32_DRIVERS_REQUESTED}" CACHE STRING
	"Optional driver modules to compile (';'-separated, e.g. UART;SPI;DMA). Normally set by stm32_project(DRIVERS ...).")

# ---- always-on core -------------------------------------------------------
# System / RCC / GPIO / Flash / IRQ_Registry are load-bearing - almost nothing
# (a bootloader included) runs without them, so they are never optional.
target_sources(stm32_drivers PRIVATE
	src/system.cpp
	src/rcc.cpp
	src/gpio.cpp
	src/flash.cpp
	src/irq_registry.cpp
)
target_include_directories(stm32_drivers PUBLIC src)

# ---- module table --------------------------------------------------------
# stm32_drivers_module(NAME [SOURCES ...] [INCLUDE ...])
#   Declares one optional module. If NAME is in STM32_DRIVERS_REQUESTED its
#   SOURCES are compiled into stm32_drivers and its INCLUDE dirs added PUBLIC.
#   Every NAME declared here is automatically the full set of valid module
#   names - nothing else to keep in sync. Paths are relative to this file.
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
# has to be a real include dir, not just a same-directory quote-include.
stm32_drivers_module(I2C    SOURCES src/i2c.cpp src/tools/i2c_timing_utility.c  INCLUDE src/tools)
stm32_drivers_module(MODBUS SOURCES Interfaces/Modbus-slave.cpp                 INCLUDE Interfaces)
stm32_drivers_module(CONTAINERS INCLUDE Containers)          # header-only
stm32_drivers_module(FASTMATH INCLUDE math SOURCES
	math/PID.c
	math/sint.c
	FastMathFunctions/arm_cos_f32.c
	FastMathFunctions/arm_cos_q15.c
	FastMathFunctions/arm_cos_q31.c
	FastMathFunctions/arm_sin_f32.c
	FastMathFunctions/arm_sin_q15.c
	FastMathFunctions/arm_sin_q31.c
	FastMathFunctions/arm_sqrt_q15.c
	FastMathFunctions/arm_sqrt_q31.c
)

# Interface_SPI/Interface_DMA (DMA-driven UART/SPI transfers) live in old/ -
# they reference a DMA_Sx_ns namespace that no longer exists in dma.hpp
# (DMA_Sx is a flat class now) and have never compiled. No module until
# someone rewrites them against the current DMA_Sx API.

# ---- reject a typo in the requested list --------------------------------
foreach(_req ${STM32_DRIVERS_REQUESTED})
	if(NOT "${_req}" IN_LIST _stm32_drivers_declared)
		message(FATAL_ERROR
			"stm32_drivers: unknown module '${_req}' in STM32_DRIVERS_REQUESTED "
			"(via stm32_project(DRIVERS ...)). Known: ${_stm32_drivers_declared}")
	endif()
endforeach()

# ---- MCU selection: inherited from whichever project pulled us in ---------
# STM32_CORE (e.g. STM32F4), STM32_DEVICE_UC (e.g. STM32F446RE), c_target
# (e.g. STM32F446xx), and main_cpu_PARAMS/cpu_PARAMS (e.g. -mcpu=cortex-m4
# -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard, from the downloaded
# STM32<family>-map.cmake) are all read as plain variables inherited from the
# caller's scope at add_subdirectory() time - see the file banner above.
# The cpu/fpu flags are PUBLIC, not just used locally: without -mthumb, the
# assembler defaults to ARM mode and rejects plain Thumb-2 instructions
# (cpsid, dsb, bkpt, ...) that this code uses everywhere.
target_compile_definitions(stm32_drivers PUBLIC
	${STM32_CORE}
	${STM32_DEVICE_UC}
	${c_target}
)
target_compile_options(stm32_drivers PUBLIC
	-mthumb
	${main_cpu_PARAMS}
	${cpu_PARAMS}
)
target_include_directories(stm32_drivers PUBLIC
	${CMAKE_SOURCE_DIR}/cmsis-core/Include
	${CMAKE_SOURCE_DIR}/cmsis-core/download_files/Device/Include
	${CMAKE_SOURCE_DIR}/cmsis-core/generated
)
