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
# Consumer side, once this is linked in:
#     target_link_libraries(<your-firmware-target> PRIVATE stm32_drivers)
# is the whole integration - no manual source list, no manual include dir.
# ============================================================================

add_library(stm32_drivers STATIC)
add_library(stm32::drivers ALIAS stm32_drivers)

# ---- which peripheral modules to compile -----------------------------------
# System / RCC / GPIO / Flash / IRQ_Registry are the load-bearing minimum -
# almost nothing (a bootloader included) runs without them, so they are not
# optional. Everything else is opt-in per project.
#
# Names match the STM32_DRIVERS_<MODULE> cache variables that
# stm32_cmake_base's stm32_project(DRIVERS ...) already sets (see its
# cmake/Platform.cmake) - option() only supplies a default the first time a
# variable doesn't already exist in the cache, so a DRIVERS list set upstream
# takes effect here with no extra wiring.
option(STM32_DRIVERS_UART    "Enable USART driver"              ON)
option(STM32_DRIVERS_SPI     "Enable SPI driver"                ON)
option(STM32_DRIVERS_DMA     "Enable DMA driver"                 ON)
option(STM32_DRIVERS_TIM     "Enable TIM driver"                 ON)
option(STM32_DRIVERS_RTC     "Enable RTC driver"                OFF)
option(STM32_DRIVERS_ADC     "Enable ADC driver"                OFF)
option(STM32_DRIVERS_DAC     "Enable DAC driver"                OFF)
option(STM32_DRIVERS_I2C     "Enable I2C driver"                OFF)
option(STM32_DRIVERS_MODBUS     "Enable Modbus-RTU slave interface" OFF)
option(STM32_DRIVERS_CONTAINERS "Enable Containers/ (RingQueue, StaticList, StaticMap) headers" OFF)
option(STM32_DRIVERS_FASTMATH   "Enable math/ + FastMathFunctions/ (PID, sine table, CMSIS-DSP-style fixed point)" OFF)

# ---- catch a typo in stm32_project(DRIVERS ...) upstream ------------------
# stm32_cmake_base's Platform.cmake deliberately does NOT know this list (it
# would be the wrong repo to maintain it in) - it just forwards whatever was
# requested as STM32_DRIVERS_REQUESTED. This is the one place that actually
# knows which module names exist (the option()s right above), so this is
# where an unknown name gets caught, right next to the list it's checked
# against - one file, one edit, when a module gets added or renamed.
set(_STM32_DRIVERS_KNOWN_MODULES
	UART SPI DMA TIM RTC ADC DAC I2C MODBUS CONTAINERS FASTMATH
)
foreach(_req ${STM32_DRIVERS_REQUESTED})
	if(NOT _req IN_LIST _STM32_DRIVERS_KNOWN_MODULES)
		message(FATAL_ERROR
			"stm32_drivers: unknown driver module '${_req}' requested via "
			"stm32_project(DRIVERS ...). Known: ${_STM32_DRIVERS_KNOWN_MODULES}")
	endif()
endforeach()

target_sources(stm32_drivers PRIVATE
	src/system.cpp
	src/rcc.cpp
	src/gpio.cpp
	src/flash.cpp
	src/irq_registry.cpp
)

if(STM32_DRIVERS_UART)
	target_sources(stm32_drivers PRIVATE src/uart.cpp)
endif()
# Interface_SPI/Interface_DMA (DMA-driven UART/SPI transfers) moved to old/ -
# references a DMA_Sx_ns namespace that no longer exists in dma.hpp (DMA_Sx
# is a flat class now) and has never actually compiled. Not part of any
# module until someone rewrites it against the current DMA_Sx API.
if(STM32_DRIVERS_SPI)
	target_sources(stm32_drivers PRIVATE src/spi.cpp)
endif()
if(STM32_DRIVERS_DMA)
	target_sources(stm32_drivers PRIVATE src/dma.cpp)
endif()
if(STM32_DRIVERS_TIM)
	target_sources(stm32_drivers PRIVATE src/tim.cpp)
endif()
if(STM32_DRIVERS_RTC)
	target_sources(stm32_drivers PRIVATE src/rtc.cpp)
endif()
if(STM32_DRIVERS_ADC)
	target_sources(stm32_drivers PRIVATE src/adc.cpp)
endif()
if(STM32_DRIVERS_DAC)
	target_sources(stm32_drivers PRIVATE src/dac.cpp)
endif()
if(STM32_DRIVERS_I2C)
	target_sources(stm32_drivers PRIVATE src/i2c.cpp src/tools/i2c_timing_utility.c)
	# i2c.hpp does `#include <i2c_timing_utility.h>` (angle brackets), so this
	# needs to be a real include dir, not just a same-directory quote-include.
	target_include_directories(stm32_drivers PUBLIC src/tools)
endif()
if(STM32_DRIVERS_MODBUS)
	target_sources(stm32_drivers PRIVATE Interfaces/Modbus-slave.cpp)
	target_include_directories(stm32_drivers PUBLIC Interfaces)
endif()
if(STM32_DRIVERS_CONTAINERS)
	target_include_directories(stm32_drivers PUBLIC Containers)
endif()
if(STM32_DRIVERS_FASTMATH)
	target_sources(stm32_drivers PRIVATE
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
	target_include_directories(stm32_drivers PUBLIC math)
endif()

target_include_directories(stm32_drivers PUBLIC src)

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
)
