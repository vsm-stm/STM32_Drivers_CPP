/**
 * @file dac.hpp
 * @brief Direct 12-bit DAC output for STM32F4/F7/G0.
 *
 * One object binds one DAC channel for its lifetime. SetUp() configures PA4
 * (CH1) or PA5 (CH2) as analog, selects direct writes with the output buffer
 * enabled, writes zero and enables the channel. The other channel is preserved.
 * Initialize System/RCC first; the selected device and package must expose
 * the DAC output. F410 has CH1 only.
 *
 * SetValue() writes a 12-bit right-aligned value: NotInit before SetUp()/after
 * DeInit(), Error for values above 4095 without changing the output, otherwise OK.
 * OK confirms the register write; analog startup/settling time depends on the
 * device and load and is not waited for by this driver.
 *
 * Keep one live object per hardware channel. Copy/move are forbidden. Call
 * DeInit() explicitly before destruction; the destructor does not change hardware.
 * Serialize calls affecting the same channel and shared configuration registers.
 *
 * @code
 *   #include "dac.hpp"
 *
 *   constinit DAC_Module output(DAC_Module::_1::instance, DAC_Module::CHANNEL::CH1); // PA4
 *   // After System/RCC initialization:
 *   if (output.SetUp() == SysInitStatus::InitOK) {
 *       SysStatus written = output.SetValue(2048);
 *       // Check written == SysStatus::OK; allow the analog output to settle.
 *   }
 *   // Later, when the channel is no longer needed:
 *   output.DeInit();
 * @endcode
 */
#ifndef DAC_HPP_
#define DAC_HPP_

#include "system.hpp"
#include "gpio.hpp"

// Change here and rebuild this driver and its consumers.
#ifndef DAC_USE_ATOMIC
#define DAC_USE_ATOMIC 0
#endif

#if DAC_USE_ATOMIC
#define DAC_SET_BIT ATOMIC_SET_BIT
#define DAC_CLEAR_BIT ATOMIC_CLEAR_BIT
#define DAC_MODIFY_REG ATOMIC_MODIFY_REG
#else
#define DAC_SET_BIT SET_BIT
#define DAC_CLEAR_BIT CLEAR_BIT
#define DAC_MODIFY_REG MODIFY_REG
#endif

#if !defined(STM32F4) && !defined(STM32F7) && !defined(STM32G0)
#error "DAC_Module supports STM32F4/F7/G0 only."
#elif !defined(DAC1)
#error "DAC_Module: the selected STM32 device has no DAC."
#else

class DAC_Module
{
public:
	/// CH2 is rejected during constinit initialization on CH1-only devices.
	enum class CHANNEL
	{
		CH1 = 0,
		CH2
	};

	struct Instance {
		uintptr_t periph, clk_reg;
		uint32_t clk_bit;
		bool has_ch2;
	};
	struct _1 {
		static constexpr Instance instance {
#ifdef DAC1_BASE
			DAC1_BASE,
#else
			DAC_BASE,
#endif
#if defined(STM32G0)
			RCC_BASE + offsetof(RCC_TypeDef, APBENR1), RCC_APBENR1_DAC1EN,
#else
			RCC_BASE + offsetof(RCC_TypeDef, APB1ENR), RCC_APB1ENR_DACEN,
#endif
#if defined(STM32F4) && !defined(DAC_CHANNEL2_SUPPORT)
			false
#else
			true
#endif
		};
	};

	/**
	 * @brief Bind an instance/channel without accessing hardware.
	 * @param instance DAC_Module::_1::instance.
	 * @param channel CH1 on PA4, or CH2 on PA5 when supported.
	 * Use constinit: the channel is checked during constant initialization.
	 */
	constexpr explicit DAC_Module(const Instance& instance, CHANNEL channel)
		: _instance(instance), ch(channel),
		  out_pin(channel == CHANNEL::CH1 ? PIN(GPIOA_BASE, 4) : PIN(GPIOA_BASE, 5))
	{
		if ((channel != CHANNEL::CH1 && channel != CHANNEL::CH2)
			|| (channel == CHANNEL::CH2 && !instance.has_ch2))
			__builtin_unreachable();
	}
	DAC_Module() = delete;
	DAC_Module(const DAC_Module&) = delete;
	DAC_Module(DAC_Module&&) = delete;
	DAC_Module& operator=(const DAC_Module&) = delete;
	DAC_Module& operator=(DAC_Module&&) = delete;
	~DAC_Module() = default;

	/**
	 * @brief Configure buffered, direct 12-bit output, initially zero.
	 * @return InitError for repeated setup;
	 *         InitOK after GPIO/DAC configuration.
	 * Preserves the other channel. No trigger, waveform, DMA or IRQ is enabled.
	 * Reconfiguration requires DeInit(); analog startup is not waited for.
	 */
	SysInitStatus SetUp();

	/**
	 * @brief Write one 12-bit right-aligned output code.
	 * @return NotInit without successful setup; Error for data > 4095; OK on write.
	 * Error leaves the previous output code unchanged. No clamping is performed.
	 */
	SysStatus SetValue(uint32_t data)
	{
		if (!data_out) return SysStatus::NotInit;
		if (data > 0xFFFU) return SysStatus::Error;
		*data_out = data;
		return SysStatus::OK;
	}

	/**
	 * @brief Disable only this channel and invalidate the data register pointer.
	 * Repeated calls are harmless. Keeps the other channel, DAC/GPIO clocks and
	 * GPIO configuration; the pin remains analog. The disabled output voltage
	 * must not be assumed to retain its previous value.
	 */
	void DeInit();

private:
	volatile uint32_t* data_out = nullptr; ///< Non-null only after successful setup.

protected:
	const Instance& _instance;
	DAC_TypeDef* DACx = nullptr;
	const CHANNEL ch;
	PIN out_pin;
};

#endif // Supported family with DAC.
#endif // DAC_HPP_
