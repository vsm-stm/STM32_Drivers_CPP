#pragma once

// Included after the CMSIS and generated Flash geometry headers.
namespace flash_hw {

// Reset an enabled Flash accelerator cache while it is disabled.
inline void reset_cache(uint32_t enable, uint32_t reset)
{
	if (FLASH->ACR & enable) {
		FLASH->ACR &= ~enable;
		FLASH->ACR |= reset;
		FLASH->ACR &= ~reset;
		FLASH->ACR |= enable;
	}
}

#if defined(RCC_APB2ENR_SYSCFGEN)
inline bool syscfg_bank_swapped(volatile uint32_t& mapping, uint32_t mask)
{
	const bool clock_enabled = (RCC->APB2ENR & RCC_APB2ENR_SYSCFGEN) != 0U;
	if (!clock_enabled) {
		RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
		(void)RCC->APB2ENR;
	}
	const bool swapped = (mapping & mask) != 0U;
	if (!clock_enabled) RCC->APB2ENR &= ~RCC_APB2ENR_SYSCFGEN;
	return swapped;
}
#endif

#ifdef FLASH_CR_MER2
inline constexpr uint32_t second_bank_erase = FLASH_CR_MER2;
#else
inline constexpr uint32_t second_bank_erase = 0U;
#endif

} // namespace flash_hw
