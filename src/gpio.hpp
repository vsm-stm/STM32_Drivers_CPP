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

	uintptr_t _port_base{};

public:
	uint8_t  pin{};
	uint8_t  af{};
	uint32_t periph_base{};

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

	PIN() = default;
	PIN(PIN const&)            = default;
	PIN(PIN&&)                 = default;
	PIN& operator=(PIN const&) = default;
	PIN& operator=(PIN&&)      = default;

	// Constexpr constructor — for compile-time pin tables in SPI/UART/etc.
	constexpr PIN(uintptr_t port_base, uint8_t p, uint8_t a = 0, uint32_t periph = 0) noexcept
		: _port_base(port_base), pin(p), af(a), periph_base(periph) {}

	// Runtime constructor — for direct GPIO_TypeDef* usage
	PIN(GPIO_TypeDef* port, uint8_t p, uint8_t a = 0) noexcept
		: _port_base(reinterpret_cast<uintptr_t>(port)), pin(p), af(a)
	{
		if (p >= 16) { __BKPT(0); while(1); }
	}

	GPIO_TypeDef* PORT() const noexcept
	{
		return reinterpret_cast<GPIO_TypeDef*>(_port_base);
	}

	constexpr bool IsValid() const noexcept { return _port_base != 0; }

	PIN& operator=(bool b) noexcept { SetLevel(b); return *this; }
	explicit operator bool() const noexcept { return GetLevel(); }

	// SetUp using stored af, explicit speed
	SysInitStatus SetUp(TYPE type, OUTPUT_SPEED speed)
	{
		return SetUp(type, speed, af);
	}

	// SetUp using stored af, default speed
	SysInitStatus SetUp(TYPE type = TYPE::INPUT_NO_Pull)
	{
		return SetUp(type, OUTPUT_SPEED::Low, af);
	}

	// SetUp with explicit af override (for PinArray and legacy callers)
	SysInitStatus SetUp(TYPE type, OUTPUT_SPEED speed, uint8_t af_override);

	// Convenience: explicit af, low speed
	SysInitStatus SetUp(TYPE type, uint8_t af_override)
	{
		return SetUp(type, OUTPUT_SPEED::Low, af_override);
	}

	// ---- level access ----

	inline bool GetLevel() const noexcept
	{
		return (PORT()->IDR >> pin) & 0x1u;
	}

	inline void SetLevel(bool lvl) noexcept
	{
		PORT()->BSRR = 1u << (pin + 16 * static_cast<uint8_t>(!lvl));
	}

	inline void SetLevel(LVL lvl) noexcept
	{
		SetLevel(lvl == LVL::HIGH);
	}

	inline void TogglePin() noexcept
	{
		PORT()->ODR ^= (0x1u << pin);
	}

#if defined(STM32F4)
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

	inline void TogglePin_BB() noexcept
	{
		BIT_BB(&PORT()->ODR, pin) = !BIT_BB(&PORT()->ODR, pin);
	}
#endif

	~PIN() = default;
};

// ---------------------------------------------------------------------------

template<size_t N>
class PinArray
{
	static_assert(N > 0,   "PinArray: must have at least 1 pin");
	static_assert(N <= 32, "PinArray: maximum 32 pins allowed");

private:
	PIN pins[N];

	template<size_t... I>
	SysInitStatus SetUpAllImpl(std::index_sequence<I...>,
							   PIN::TYPE         type,
							   PIN::OUTPUT_SPEED  speed,
							   uint8_t            af)
	{
		SysInitStatus result = SysInitStatus::InitOK;
		((pins[I].SetUp(type, speed, af) != SysInitStatus::InitOK
			  ? (result = SysInitStatus::NotInit, void())
			  : void()), ...);
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
	{
		for (size_t i = 0; i < N; ++i)
			pins[i] = _pins[i];
	}

	SysInitStatus SetUpAll(PIN::TYPE type, uint8_t af)
	{
		return SetUpAllImpl(std::make_index_sequence<N>{}, type, PIN::OUTPUT_SPEED::Low, af);
	}

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

	PIN&       operator[](size_t i)       { return pins[i]; }
	const PIN& operator[](size_t i) const { return pins[i]; }
};

#endif // GPIO_H_
