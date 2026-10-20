/**
 * @file gpio.hpp
 * @brief Fixed GPIO pin binding, configuration, level access and pin groups.
 *
 * Pin number, port and constructor AF never change after construction.
 * GPIOx_BASE construction is consteval and validates port/pin/AF at compilation;
 * the retained GPIO_TypeDef* constructor is convenient for CMSIS pointers and
 * relies on caller-supplied valid pin/AF. No run-time argument guards are added.
 *
 * @code
 *   PIN led(GPIOA, 5); // CMSIS pointer constructor, immutable binding.
 *   led.SetUp(PIN::TYPE::OUTPUT_PushPull, PIN::OUTPUT_SPEED::Low, PIN::LVL::HIGH);
 *   led.SetLevel(false);
 *   led.TogglePin();
 *
 *   constexpr PIN tx(GPIOA_BASE, 9, 7, USART1_BASE); // Compile-time checked form.
 *   // Peripheral drivers use the stored AF; they check periph_base ownership.
 * @endcode
 * GPIO and PinArray operations have no internal locks. SetLevel uses BSRR;
 * TogglePin uses ODR read/modify/write and must not race with another port writer.
 */
#ifndef GPIO_H_
#define GPIO_H_

#include "system.hpp"
#include <cstddef>
#include <utility>

#if defined(STM32F4) or defined(STM32F7)
	#define RCC_GPIO_EN_REG		RCC->AHB1ENR
	#define RCC_GPIOA_EN		RCC_AHB1ENR_GPIOAEN
#elif defined(STM32G0)
	#define RCC_GPIO_EN_REG		RCC->IOPENR
	#define RCC_GPIOA_EN		RCC_IOPENR_GPIOAEN
#endif

/**
 * @brief One immutable pin binding; operations configure/read/write its hardware.
 * PIN{} is an absent optional signal: test IsValid(), do not configure/access it.
 * The destructor does not reset GPIO or disable its shared port clock.
 */
class PIN
{
private:
	static constexpr uint8_t mode_pos        = 0;
	static constexpr uint8_t pull_pos        = 2;
	static constexpr uint8_t output_type_pos = 4;

	enum class MODE : uint8_t
	{
		INPUT  = 0b00,
		OUTPUT = 0b01,
		AF     = 0b10,
		ANALOG = 0b11
	};

	enum class PULL : uint8_t
	{
		NO_Pull  = 0b00,
		PullUP   = 0b01,
		PullDown = 0b10
	};

	enum class OUTPUT_TYPE : uint8_t
	{
		PushPull  = 0b0,
		OpenDrain = 0b1
	};

	void Reset() { SetUp(TYPE::INPUT_NO_Pull); }

	const uintptr_t _port_base{};
	const uint8_t pin{};
	const uint8_t af{};

public:
	// Read-only binding metadata used by USART/SPI/I2C constructors.
	const uint32_t periph_base{};

	/// Packed mode/pull/output-type choices consumed by SetUp(), not raw MODER values.
	enum class TYPE : uint8_t
	{
		INPUT_NO_Pull   = (static_cast<uint8_t>(MODE::INPUT)  << mode_pos),
		INPUT_PullUp    = (static_cast<uint8_t>(MODE::INPUT)  << mode_pos) | (static_cast<uint8_t>(PULL::PullUP)   << pull_pos),
		INPUT_PullDown  = (static_cast<uint8_t>(MODE::INPUT)  << mode_pos) | (static_cast<uint8_t>(PULL::PullDown) << pull_pos),

		OUTPUT_PushPull = (static_cast<uint8_t>(MODE::OUTPUT) << mode_pos) | (static_cast<uint8_t>(OUTPUT_TYPE::PushPull)  << output_type_pos),
		OUTPUT_OD       = (static_cast<uint8_t>(MODE::OUTPUT) << mode_pos) | (static_cast<uint8_t>(OUTPUT_TYPE::OpenDrain) << output_type_pos),
		OUTPUT_OD_PulUp = (static_cast<uint8_t>(MODE::OUTPUT) << mode_pos) | (static_cast<uint8_t>(OUTPUT_TYPE::OpenDrain) << output_type_pos) | (static_cast<uint8_t>(PULL::PullUP) << pull_pos),

		AF_PushPull     = (static_cast<uint8_t>(MODE::AF)     << mode_pos) | (static_cast<uint8_t>(OUTPUT_TYPE::PushPull)  << output_type_pos),
		AF_OD           = (static_cast<uint8_t>(MODE::AF)     << mode_pos) | (static_cast<uint8_t>(OUTPUT_TYPE::OpenDrain) << output_type_pos),
		AF_OD_PulUp     = (static_cast<uint8_t>(MODE::AF)     << mode_pos) | (static_cast<uint8_t>(OUTPUT_TYPE::OpenDrain) << output_type_pos) | (static_cast<uint8_t>(PULL::PullUP) << pull_pos),

		ANALOG          = (static_cast<uint8_t>(MODE::ANALOG) << mode_pos)
	};

	/// GPIO output slew-rate selection; availability/electrical limits are device-specific.
	enum class OUTPUT_SPEED : uint8_t
	{
		Low    = 0b00,
		Medium = 0b01,
		Fast   = 0b10,
		High   = 0b11
	};

	enum class LVL
	{
		LOW,
		HIGH
	};

	// An absent pin, for optional peripheral signals. Do not access or configure it.
	constexpr PIN() = default;
	PIN(PIN const&)            = default;
	PIN(PIN&&)                 = default;
	PIN& operator=(PIN const&) = delete;
	PIN& operator=(PIN&&)      = delete;

	// Use GPIOx_BASE: parameters and checks are evaluated only at compile time.
	consteval PIN(uintptr_t port_base, uint32_t p, uint32_t a = 0, uint32_t periph = 0) noexcept
		: _port_base(port_base), pin(static_cast<uint8_t>(p)),
		  af(static_cast<uint8_t>(a)), periph_base(periph)
	{
		// Reaching a non-constant expression rejects an invalid definition at build time.
		if (p >= 16) System::DebugTrap("PIN: pin number must be in 0..15");
		if (a >= 16) System::DebugTrap("PIN: alternate function must be in 0..15");
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
			break;
		default:
			System::DebugTrap("PIN: GPIO port is not available on this device");
		}
	}

	/**
	 * @brief Bind a direct CMSIS GPIO pointer; retained for ordinary GPIOx syntax.
	 * @param port Existing GPIO port pointer on the selected device.
	 * @param p Pin number 0..15, provided correctly by the caller.
	 * @param a Stored alternate function 0..15, default 0.
	 * No run-time validation; this overload is not a compile-time pointer checker.
	 */
	PIN(GPIO_TypeDef* port, uint8_t p, uint8_t a = 0) noexcept
		: _port_base(reinterpret_cast<uintptr_t>(port)), pin(p), af(a) {}

	/// Recover the bound CMSIS port pointer. Does not enable its clock.
	GPIO_TypeDef* PORT() const noexcept
	{
		return reinterpret_cast<GPIO_TypeDef*>(_port_base);
	}

	/// Read the fixed pin number, e.g. for routing its external EXTI line.
	constexpr uint8_t GetNumber() const noexcept { return pin; }

	/// Whether a port binding exists; not a pin/package/AF-route capability test.
	constexpr bool IsValid() const noexcept { return _port_base != 0; }

	PIN& operator=(bool b) noexcept { SetLevel(b); return *this; }
	explicit operator bool() const noexcept { return GetLevel(); }

	/**
	 * @brief Configure this pin using its constructor AF.
	 * @param type Packed input/output/AF/analog choice.
	 * @param speed Slew rate for output/AF, default Low.
	 * @param initial_level Output latch level set before enabling OUTPUT, default LOW.
	 * @return InitOK after register programming; arguments are a caller contract.
	 * Enables the shared GPIO clock. Other modes do not change the output latch;
	 * ANALOG clears pull/output/AF fields rather than enabling a digital output.
	 * Configuration registers are each written once; MODER is written last,
	 * without temporarily selecting INPUT. This is not an atomic multi-register update.
	 * @pre Valid bound pin and enum arguments; serialize GPIO configuration.
	 */
	SysInitStatus SetUp(TYPE type = TYPE::INPUT_NO_Pull, OUTPUT_SPEED speed = OUTPUT_SPEED::Low,
						LVL initial_level = LVL::LOW)
	{
		return SetUp(type, speed, af, initial_level);
	}

	/**
	 * @brief Full configuration with an explicit hardware AF override.
	 * @param af_override AF field 0..15; does not change the stored constructor AF.
	 * Caller supplies a valid route/value; the GPIO layer does not validate
	 * whether that AF connects to a particular peripheral on the selected pin.
	 */
	SysInitStatus SetUp(TYPE type, OUTPUT_SPEED speed, uint8_t af_override,
						LVL initial_level = LVL::LOW);

	/// Convenience overload: explicit AF override, Low speed and default LOW output level.
	SysInitStatus SetUp(TYPE type, uint8_t af_override, LVL initial_level = LVL::LOW)
	{
		return SetUp(type, OUTPUT_SPEED::Low, af_override, initial_level);
	}

	// ---- level access ----

	/// Read IDR (actual input level), not the ODR output latch.
	inline bool GetLevel() const noexcept
	{
		return (PORT()->IDR >> pin) & 0x1u;
	}

	/// Write one BSRR set/reset bit, leaving unrelated output bits unchanged.
	inline void SetLevel(bool lvl) noexcept
	{
		PORT()->BSRR = 1u << (pin + 16 * static_cast<uint8_t>(!lvl));
	}

	inline void SetLevel(LVL lvl) noexcept
	{
		SetLevel(lvl == LVL::HIGH);
	}

	/**
	 * @brief Toggle the ODR bit using a whole-register read/modify/write.
	 * Caller prevents competing writes to this port during the operation.
	 */
	inline void TogglePin() noexcept
	{
		// ODR read-modify-write: do not modify this port from IRQ during the toggle.
		PORT()->ODR ^= (0x1u << pin);
	}

#if defined(STM32F4)
	/// F4 peripheral bit-band read of IDR; no corresponding M7/G0 API.
	inline bool GetLevel_BB() const noexcept
	{
		return static_cast<bool>(BIT_BB(&PORT()->IDR, pin));
	}

	inline void SetLevel_BB(bool lvl) noexcept
	{
		BIT_BB(&PORT()->ODR, pin) = static_cast<uint32_t>(lvl);
	}

	inline void SetLevel_BB(LVL lvl) noexcept
	{
		SetLevel_BB(lvl == LVL::HIGH);
	}

	/// F4 bit-band read then write of ODR; the combined toggle is not atomic.
	inline void TogglePin_BB() noexcept
	{
		BIT_BB(&PORT()->ODR, pin) = !BIT_BB(&PORT()->ODR, pin);
	}
#endif

	~PIN() = default;
};

// ---------------------------------------------------------------------------

/**
 * @brief Copy N fixed pin bindings into a group, N=1..32.
 * Group bit i corresponds to constructor pin i, even across different ports.
 * Operations visit pins individually; a group write is not one simultaneous
 * hardware transaction. SetUpAll uses its explicit common AF (default 0).
 *
 * @code
 *   PIN signals[] = { PIN(GPIOA, 5), PIN(GPIOB, 0) };
 *   PinArray<2> group(signals);
 *   group.SetUpAll(PIN::TYPE::OUTPUT_PushPull);
 *   group.SetLevelAll(0b01); // First HIGH, second LOW.
 *   uint32_t inputs = group.GetLevelAll();
 * @endcode
 */
template<size_t N>
class PinArray
{
	static_assert(N > 0,   "PinArray: must have at least 1 pin");
	static_assert(N <= 32, "PinArray: maximum 32 pins allowed");

private:
	PIN pins[N];

	template<size_t... I>
	constexpr PinArray(const PIN (&values)[N], std::index_sequence<I...>)
		: pins{values[I]...} {}

	template<size_t... I>
	SysInitStatus SetUpAllImpl(std::index_sequence<I...>,
							   PIN::TYPE         type,
							   PIN::OUTPUT_SPEED  speed,
							   uint8_t            af)
	{
		SysInitStatus result = SysInitStatus::InitOK;
		([&] {
			const SysInitStatus current = pins[I].SetUp(type, speed, af);
			if (result == SysInitStatus::InitOK) result = current;
		}(), ...);
		return result;
	}

	template<size_t... I>
	uint32_t GetLevelImpl(std::index_sequence<I...>) const noexcept
	{
		return ((static_cast<uint32_t>(pins[I].GetLevel()) << I) | ...);
	}

	template<size_t... I>
	void SetLevelImpl(uint32_t value, std::index_sequence<I...>) noexcept
	{
		(pins[I].SetLevel(static_cast<bool>((value >> I) & 0x1u)), ...);
	}

	template<size_t... I>
	void ToggleAllImpl(std::index_sequence<I...>) noexcept
	{
		(pins[I].TogglePin(), ...);
	}

public:
	constexpr explicit PinArray(const PIN (&_pins)[N])
		: PinArray(_pins, std::make_index_sequence<N>{}) {}

	/// Configure all pins with a common AF and Low speed; returns the first non-InitOK result.
	SysInitStatus SetUpAll(PIN::TYPE type, uint8_t af)
	{
		return SetUpAllImpl(std::make_index_sequence<N>{}, type, PIN::OUTPUT_SPEED::Low, af);
	}

	/// Configure every pin, retaining the first error while still visiting the remaining pins.
	SysInitStatus SetUpAll(PIN::TYPE         type  = PIN::TYPE::INPUT_NO_Pull,
						   PIN::OUTPUT_SPEED  speed = PIN::OUTPUT_SPEED::Low,
						   uint8_t            af    = 0)
	{
		return SetUpAllImpl(std::make_index_sequence<N>{}, type, speed, af);
	}

	uint32_t GetLevelAll() const noexcept
	{
		return GetLevelImpl(std::make_index_sequence<N>{});
	}

	void SetLevelAll(uint32_t value) noexcept
	{
		SetLevelImpl(value, std::make_index_sequence<N>{});
	}

	void ToggleAll() noexcept
	{
		ToggleAllImpl(std::make_index_sequence<N>{});
	}

	PinArray& operator=(uint32_t value) noexcept { SetLevelAll(value); return *this; }

	/// Unchecked access: i < N. Binding cannot be reassigned; GPIO level/configuration can.
	PIN&       operator[](size_t i)       { return pins[i]; }
	const PIN& operator[](size_t i) const { return pins[i]; }
};

#endif // GPIO_H_
