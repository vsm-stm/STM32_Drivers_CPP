#ifndef GPIO_H_
#define GPIO_H_

#include "system.hpp"
#include <cstddef>
#include <utility>
#include <cassert>

/**
 * @brief Class representing a GPIO pin with configurable parameters.
 */
class PIN
{
private:
	/**
	 * @brief Bit positions within the TYPE enum byte.
	 *   [1:0] = MODE
	 *   [3:2] = PULL
	 *   [4]   = OUTPUT_TYPE
	 */
	static constexpr uint8_t mode_pos        = 0;
	static constexpr uint8_t pull_pos        = 2;
	static constexpr uint8_t output_type_pos = 4;

	/**
	 * @brief Pin mode — matches MODER register 2-bit encoding.
	 */
	enum class MODE : uint8_t
	{
		INPUT  = 0b00,  ///< Input mode
		OUTPUT = 0b01,  ///< Output mode  (FIX: was 0b1, same value but misleading alignment)
		AF     = 0b10,  ///< Alternate function mode
		ANALOG = 0b11   ///< Analog mode
	};

	/**
	 * @brief Pull-up / pull-down selection — raw 2-bit values (NOT pre-shifted).
	 */
	enum class PULL : uint8_t
	{
		NO_Pull  = 0b00,
		PullUP   = 0b01,
		PullDown = 0b10
	};

	/**
	 * @brief Output driver type — raw 1-bit value (NOT pre-shifted).
	 */
	enum class OUTPUT_TYPE : uint8_t
	{
		PushPull  = 0b0,
		OpenDrain = 0b1
	};

	void Reset() { SetUp(TYPE::INPUT_NO_Pull); }

public:
	GPIO_TypeDef* PORT{};
	uint8_t       pin{};

	/**
	 * @brief Combined pin configuration packed into one byte.
	 *
	 *  Bit layout:
	 *   [1:0]  MODE        (0=IN, 1=OUT, 2=AF, 3=ANALOG)
	 *   [3:2]  PULL        (0=none, 1=up, 2=down)
	 *   [4]    OUTPUT_TYPE (0=PP, 1=OD)
	 */
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

	PIN& operator=(bool b) noexcept { SetLevel(b); return *this; }
	explicit operator bool() const noexcept { return GetLevel(); }

	/**
	 * @param port  GPIO port pointer.
	 * @param pn    Pin number, must be in [0..15].
	 */
	explicit PIN(GPIO_TypeDef* port, uint8_t pn)
		: PORT(port), pin(pn)
	{
		assert(pn < 16 && "PIN: pin number must be in [0..15]");
	}

	SysInitStatus SetUp(TYPE type, uint8_t af)
	{
		return SetUp(type, OUTPUT_SPEED::Low, af);
	}

	SysInitStatus SetUp(TYPE type          = TYPE::INPUT_NO_Pull,
						OUTPUT_SPEED speed = OUTPUT_SPEED::Low,
						uint8_t af         = 0);

	// ---- level access ----

	inline bool GetLevel() const noexcept
	{
		// FIX: was ((PORT->IDR & (0x1 << pin)) >> pin) — equivalent but less clear
		return (PORT->IDR >> pin) & 0x1u;
	}

	inline void SetLevel(bool lvl) noexcept
	{
		PORT->BSRR = (lvl ? GPIO_BSRR_BS0 : GPIO_BSRR_BR0) << pin;
	}

	inline void SetLevel(LVL lvl) noexcept
	{
		SetLevel(lvl == LVL::HIGH);
	}

	inline void TogglePin() noexcept
	{
		PORT->ODR ^= (0x1u << pin);
	}

#if defined(STM32F4)
	inline bool GetLevel_BB() const noexcept
	{
		return static_cast<bool>(BIT_BB(&PORT->IDR, pin));
	}

	inline void SetLevel_BB(bool lvl) noexcept
	{
		BIT_BB(&PORT->ODR, pin) = static_cast<uint32_t>(lvl);
	}

	inline void SetLevel_BB(LVL lvl) noexcept
	{
		SetLevel_BB(lvl == LVL::HIGH);
	}

	/**
	 * FIX: XOR через bit-band alias некорректен — ячейка содержит 0 или 1,
	 *      запись любого ненулевого значения устанавливает бит.
	 *      Правильный toggle — read → invert → write.
	 */
	inline void TogglePin_BB() noexcept
	{
		BIT_BB(&PORT->ODR, pin) = !BIT_BB(&PORT->ODR, pin);
	}
#endif

	~PIN() = default;
};

// ---------------------------------------------------------------------------

/**
 * @brief Owning array of PIN objects with bulk configure / read / write operations.
 *
 * @tparam N  Number of pins (1..32).
 *
 * FIX: removed private inheritance from PIN — PinArray IS NOT a PIN.
 */
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
		// FIX: was using short-circuit &&, which stopped on first error.
		//      Now ALL pins are configured; first failure is remembered.
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
