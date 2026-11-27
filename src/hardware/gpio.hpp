/**
 * @file hardware/gpio.hpp
 * @brief Select GPIO clocking and capabilities at compilation.
 *
 * F4/F7/G0/L0/G4 share the MODER GPIO register organization. Only the
 * clock-enable register and AF range are selected here; no hardware object,
 * operation table or run-time family selection is needed.
 * Included after the selected CMSIS device header by gpio.hpp.
 */
#ifndef STM32_GPIO_HARDWARE_HPP_
#define STM32_GPIO_HARDWARE_HPP_

namespace gpio_hw {

#if defined(STM32F4) || defined(STM32F7)
__STATIC_FORCEINLINE volatile uint32_t& ClockEnableRegister() noexcept { return RCC->AHB1ENR; }
inline constexpr uint32_t port_a_clock = RCC_AHB1ENR_GPIOAEN;
inline constexpr uint32_t af_count = 16;
#elif defined(STM32G0)
__STATIC_FORCEINLINE volatile uint32_t& ClockEnableRegister() noexcept { return RCC->IOPENR; }
inline constexpr uint32_t port_a_clock = RCC_IOPENR_GPIOAEN;
inline constexpr uint32_t af_count = 16;
#elif defined(STM32L0)
__STATIC_FORCEINLINE volatile uint32_t& ClockEnableRegister() noexcept { return RCC->IOPENR; }
inline constexpr uint32_t port_a_clock = RCC_IOPENR_IOPAEN;
inline constexpr uint32_t af_count = 8;
#elif defined(STM32G4)
__STATIC_FORCEINLINE volatile uint32_t& ClockEnableRegister() noexcept { return RCC->AHB2ENR; }
inline constexpr uint32_t port_a_clock = RCC_AHB2ENR_GPIOAEN;
inline constexpr uint32_t af_count = 16;
#else
#error "GPIO supports STM32F4, STM32F7, STM32G0, STM32L0 and STM32G4 only"
#endif

/// Port presence follows CMSIS, not package bonding or an AF routing table.
constexpr bool IsPortAvailable(uintptr_t port_base) noexcept
{
	switch (port_base) {
	case GPIOA_BASE:
	case GPIOB_BASE:
#if defined(GPIOC_BASE)
	case GPIOC_BASE:
#endif
#if defined(GPIOD_BASE)
	case GPIOD_BASE:
#endif
#if defined(GPIOE_BASE)
	case GPIOE_BASE:
#endif
#if defined(GPIOF_BASE)
	case GPIOF_BASE:
#endif
#if defined(GPIOG_BASE)
	case GPIOG_BASE:
#endif
#if defined(GPIOH_BASE)
	case GPIOH_BASE:
#endif
#if defined(GPIOI_BASE)
	case GPIOI_BASE:
#endif
#if defined(GPIOJ_BASE)
	case GPIOJ_BASE:
#endif
#if defined(GPIOK_BASE)
	case GPIOK_BASE:
#endif
		return true;
	default:
		return false;
	}
}

} // namespace gpio_hw

#endif // STM32_GPIO_HARDWARE_HPP_
