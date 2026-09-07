# ============================================================================
# STM32_Drivers_CPP — stm32_drivers library target
# ============================================================================
#
# NOT a standalone CMake project (no project()/toolchain here) - always
# add_subdirectory()'d by whichever project pulls this repo in (today
# stm32_cmake_base, from its CMakeLists.txt DRIVERS section), which by then
# has already:
#   - called project() and resolved the arm-none-eabi toolchain,
#   - defined the INTERFACE target `stm32_platform` (CMSIS/device include
#     dirs, device defines, cpu/fpu flags),
#   - resolved the chip facts these drivers generate config from:
#     STM32_CORE, STM32_FLASH, STM32_SERIES_UC, STM32_VECTOR_FILE,
#     ${STM32_CORE}_SECTOR_MAP, STM32_DUAL_BANK,
#   - made download_one() / k_to_int() available (functions are global once
#     defined; see stm32_cmake_base/cmake/functions.cmake).
#
# Two headers this library needs are DRIVERS-ONLY, so they're generated HERE,
# not by the platform: flash_config.h (the sector map, read by flash.*) and
# irq_registry_config.h (the IRQ dispatch stubs - won't even compile without
# IRQ_Registry, which is ours). They land in this target's build dir.
#
# Configuration is entirely external, through ONE input:
#   STM32_DRIVERS_REQUESTED   ';'-separated optional module names, e.g.
#                             "UART;SPI;DMA;TIM" - set by stm32_project(DRIVERS
#                             ...) upstream, or -DSTM32_DRIVERS_REQUESTED=...
#
# Consumer side: target_link_libraries(<firmware> PRIVATE stm32_drivers)
# ============================================================================

add_library(stm32_drivers STATIC)
add_library(stm32::drivers ALIAS stm32_drivers)
target_link_libraries(stm32_drivers PUBLIC stm32_platform)

set(STM32_DRIVERS_REQUESTED "${STM32_DRIVERS_REQUESTED}" CACHE STRING
	"Optional driver modules to compile (';'-separated, e.g. UART;SPI;DMA). Normally set by stm32_project(DRIVERS ...).")

# ############################################################################
# Generated config headers (flash_config.h, irq_registry_config.h)
# ############################################################################

# Uniform-page families: flash pages all one size, sector list is a formula.
# Families not listed here use a ${SERIES}_SECTOR_MAP from the downloaded
# STM32<family>-map.cmake instead.
set(STM32G0_PAGE_SIZE 2048)
set(STM32C0_PAGE_SIZE 2048)

# stm32_gen_flash_config(SERIES FLASH_STR DEVICE_NAME TEMPLATE OUT_FILE)
#   Sector-map mode (${SERIES}_SECTOR_MAP defined): look up FLASH_KB -> count,
#   doubled when STM32_DUAL_BANK. Uniform-page mode (${SERIES}_PAGE_SIZE):
#   count = flash / page, and build the {address,size} list for the template.
function(stm32_gen_flash_config SERIES FLASH_STR DEVICE_NAME TEMPLATE OUT_FILE)
	k_to_int("${FLASH_STR}" _kb)
	set(_map     "${${SERIES}_SECTOR_MAP}")
	set(_page_sz "${${SERIES}_PAGE_SIZE}")
	set(FLASH_CFG_SECTORS_LIST "")

	if(_map)
		list(FIND _map "${_kb}" _idx)
		if(_idx LESS 0)
			message(FATAL_ERROR "stm32_gen_flash_config: no entry for ${_kb}K in ${SERIES}_SECTOR_MAP")
		endif()
		math(EXPR _i1 "${_idx} + 1")
		list(GET _map ${_i1} FLASH_CFG_COUNT)
		if(STM32_DUAL_BANK)
			math(EXPR FLASH_CFG_COUNT "${FLASH_CFG_COUNT} * 2")
		endif()
		set(_mode "sectors")
	elseif(_page_sz)
		math(EXPR FLASH_CFG_COUNT "${_kb} * 1024 / ${_page_sz}")
		math(EXPR _last "${FLASH_CFG_COUNT} - 1")
		foreach(_i RANGE 0 ${_last})
			math(EXPR _addr "0x08000000 + ${_i} * ${_page_sz}" OUTPUT_FORMAT HEXADECIMAL)
			string(APPEND FLASH_CFG_SECTORS_LIST
				"    { ${_addr}UL, ${_page_sz}UL }, // Sector ${_i}\n")
		endforeach()
		set(_mode "pages (${_page_sz} B each)")
	else()
		message(FATAL_ERROR
			"stm32_gen_flash_config: neither ${SERIES}_SECTOR_MAP nor ${SERIES}_PAGE_SIZE is defined")
	endif()

	set(FLASH_CFG_DEVICE    "${DEVICE_NAME}")
	set(FLASH_CFG_FLASH_STR "${FLASH_STR}")
	configure_file("${TEMPLATE}" "${OUT_FILE}" @ONLY)
	message(STATUS "Flash config generated: ${OUT_FILE}  (${FLASH_CFG_COUNT} ${_mode}, dual_bank=${STM32_DUAL_BANK})")
endfunction()

# stm32_gen_irq_handlers(VECTOR_FILE TEMPLATE OUT_FILE)
#   Text-scrapes the vector table: IRQ_TABLE_SIZE from the entry count, and one
#   `extern "C" void X_IRQHandler(){ IRQ_Registry::Dispatch(X_IRQn); }` per
#   *_IRQHandler found. Depends on vector_<device>.c keeping its formatting.
function(stm32_gen_irq_handlers VECTOR_FILE TEMPLATE OUT_FILE)
	file(STRINGS "${VECTOR_FILE}" _all_entries REGEX "\\(uint32_t\\)")
	list(LENGTH _all_entries _total)
	math(EXPR IRQ_TABLE_SIZE "${_total} - 16")

	if(STM32_SERIES_UC MATCHES "STM32G0")
		set(IRQ_MAX_SHARED 3)   # DMA1_Ch4_5_DMAMUX1_OVR
	else()
		set(IRQ_MAX_SHARED 2)   # timer pairs on F4/F7; safe default elsewhere
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

# ---- pick the flash-config template, fetch templates, generate -------------
set(_dl  "${CMAKE_CURRENT_BINARY_DIR}/dl")         # downloaded .h.in templates
set(_gen "${CMAKE_CURRENT_BINARY_DIR}/generated")  # generated headers

# Families with single/dual variants: F4, F7, G4, H7. F1 has an XL variant
# (>512K). Everything else: one template, no suffix.
k_to_int("${STM32_FLASH}" _flash_kb)
if("${STM32_CORE}" MATCHES "^STM32(F4|F7|G4|H7)$")
	if(STM32_DUAL_BANK)
		set(_flash_tmpl "${STM32_CORE}_flash_config_dual.h.in")
	else()
		set(_flash_tmpl "${STM32_CORE}_flash_config_single.h.in")
	endif()
elseif(STM32_CORE STREQUAL "STM32F1" AND _flash_kb GREATER 512)
	set(_flash_tmpl "STM32F1_flash_config_xl.h.in")
else()
	set(_flash_tmpl "${STM32_CORE}_flash_config.h.in")
endif()

download_one("${_flash_tmpl}"           "${_dl}" "cmake/${_flash_tmpl}")
download_one("irq_registry_config.h.in" "${_dl}" "cmake/irq_registry_config.h.in")

stm32_gen_flash_config("${STM32_CORE}" "${STM32_FLASH}" "${STM32_DEVICE_UC}"
	"${_dl}/${_flash_tmpl}" "${_gen}/flash_config.h")
stm32_gen_irq_handlers("${STM32_VECTOR_FILE}"
	"${_dl}/irq_registry_config.h.in" "${_gen}/irq_registry_config.h")

target_include_directories(stm32_drivers PUBLIC "${_gen}")

# ############################################################################
# Sources
# ############################################################################

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
#   If NAME is in STM32_DRIVERS_REQUESTED its SOURCES/INCLUDE are added.
#   The set of NAMEs declared here IS the set of valid module names - the
#   typo check below derives its list from these calls, nothing to sync.
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
# (DMA_Sx is a flat class now) and have never compiled.

# ---- reject a typo in the requested list --------------------------------
foreach(_req ${STM32_DRIVERS_REQUESTED})
	if(NOT "${_req}" IN_LIST _stm32_drivers_declared)
		message(FATAL_ERROR
			"stm32_drivers: unknown module '${_req}' in STM32_DRIVERS_REQUESTED "
			"(via stm32_project(DRIVERS ...)). Known: ${_stm32_drivers_declared}")
	endif()
endforeach()
