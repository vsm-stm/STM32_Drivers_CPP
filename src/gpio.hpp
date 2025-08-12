#ifndef GPIO_H_
#define GPIO_H_

#include <system.hpp>

/**
 * @brief Class representing a GPIO pin with configurable parameters.
 */
class PIN
{
private:
	/**
	 * @brief Bit position for mode configuration in the setup.
	 */
	static const uint8_t mode_pos = 0;

	/**
	 * @brief Enumeration for pin modes.
	 */
	enum class MODE
	{
		INPUT =  (0b00 << mode_pos),	///< Input mode
		OUTPUT = (0b1 << mode_pos),		///< Output mode
		AF = 	 (0b10 << mode_pos),	///< Alternate function mode
		ANALOG = (0b11 << mode_pos)		///< Analog mode
	};

	/**
	 * @brief Bit position for pull configuration in the setup.
	 */
	static const uint8_t pull_pos = 2;

	/**
	 * @brief Enumeration for pull configurations.
	 */
	enum class PULL
	{
		NO_Pull = (0b0 << pull_pos), 	///< No pull-up/pull-down
		PullUP = (0b1 << pull_pos),  	///< Pull-up
		PullDown = (0b10 << pull_pos)	///< Pull-down
	};

	/**
	 * @brief Bit position for output type configuration in the setup.
	 */
	static const uint8_t output_type_pos = 4;

	/**
	 * @brief Enumeration for output types.
	 */
	enum class OUTPUT_TYPE
	{
		PushPull = (0b0 << output_type_pos),	///< Push-pull output type
		OpenDrain = (0b1 << output_type_pos)	///< Open-drain output type
	};

	/**
	 * @brief Reset the pin configuration to INPUT_NO_Pull.
	 */
	void Reset()
	{
		SetUp(TYPE::INPUT_NO_Pull);
	};

public:
	GPIO_TypeDef *PORT{};
	uint8_t pin{};

	/**
	 * @brief Enumeration representing different pin configurations.
	 */
	enum class TYPE
	{
		INPUT_NO_Pull =  	(static_cast<uint8_t>(MODE::INPUT) 	| static_cast<uint8_t>(PULL::NO_Pull)),		///< Input mode without pull-up/pull-down
		INPUT_PullUp =   	(static_cast<uint8_t>(MODE::INPUT) 	| static_cast<uint8_t>(PULL::PullUP)),		///< Input mode with pull-up
		INPUT_PullDown = 	(static_cast<uint8_t>(MODE::INPUT) 	| static_cast<uint8_t>(PULL::PullDown)),	///< Input mode with pull-down
		OUTPUT_PushPull =	(static_cast<uint8_t>(MODE::OUTPUT)	| static_cast<uint8_t>(OUTPUT_TYPE::PushPull)),		///< Output mode with push-pull configuration
		OUTPUT_OD =      	(static_cast<uint8_t>(MODE::OUTPUT)	| static_cast<uint8_t>(OUTPUT_TYPE::OpenDrain)),	///< Output mode with open-drain configuration
		OUTPUT_OD_PulUp =	(static_cast<uint8_t>(MODE::OUTPUT)	| static_cast<uint8_t>(OUTPUT_TYPE::OpenDrain) | static_cast<uint8_t>(PULL::PullUP)),	///< Output mode with open-drain configuration and pull-up
		AF_PushPull =    	(static_cast<uint8_t>(MODE::AF) 	| static_cast<uint8_t>(OUTPUT_TYPE::PushPull)),		///< Alternate function mode with push-pull configuration
		AF_OD =          	(static_cast<uint8_t>(MODE::AF) 	| static_cast<uint8_t>(OUTPUT_TYPE::OpenDrain)),	///< Alternate function mode with open-drain configuration
		AF_OD_PulUp =    	(static_cast<uint8_t>(MODE::AF) 	| static_cast<uint8_t>(OUTPUT_TYPE::OpenDrain) | static_cast<uint8_t>(PULL::PullUP)),	///< Alternate function mode with open-drain configuration and pull-up
		ANALOG =         	(static_cast<uint8_t>(MODE::ANALOG))      ///< Analog mode
	};

	enum class OUTPUT_SPEED
	{
		Low  = 0b00,
		Medium,
		Fast,
		High
	};

	/**
	 * @brief Enumeration representing different logic levels.
	 */
	enum class LVL
	{
		LOW,
		HIGH
	};

	PIN() = default;
	PIN(PIN const &) = default;
	PIN(PIN &&) = default;
	PIN &operator=(PIN const &) = default;
	PIN &operator=(PIN &&) = default;
	void operator=(int i) {
		SetLevel(i);
	};
	void operator=(bool b) {
		SetLevel(b);
	};

	/**
	 * @brief Constructor for the PIN class.
	 * @param port Pointer to the GPIO port.
	 * @param pn Pin number.
	 */
	explicit PIN(GPIO_TypeDef *port, uint8_t pn) :
		PORT(port),
		pin(pn)
	{
	}

	/**
	 * @brief Set up the pin with the specified type and alternate function.
	 * @param type The enumerate type of pin configuration.
	 * @param af The alternate function number.
	 * @return The status of the setup operation.
	 */
	SYS_StatusTypeDef SetUp(TYPE type, uint8_t af) {return SetUp(type, OUTPUT_SPEED::Low, af);};
	SYS_StatusTypeDef SetUp(TYPE type = TYPE::INPUT_NO_Pull, OUTPUT_SPEED speed = OUTPUT_SPEED::Low, uint8_t af = 0);

	/**
	 * @brief Get the logic level of the pin.
	 * @return The logic level of the pin.
	 */
	inline bool GetLevel()
	{
		return ((PORT->IDR & (0x1 << pin)) >> pin);
	};

	/**
	 * @brief Set the logic level of the pin.
	 * @param lvl_int The logic level as an integer.
	 */
	inline void SetLevel(bool lvl)
	{
		PORT->BSRR |= ((lvl == 0) ? GPIO_BSRR_BR0 : GPIO_BSRR_BS0) << pin;
	};

	/**
	 * @brief Set the logic level of the pin using the specified enum.
	 * @param lvl The logic level enum.
	 */
	inline void SetLevel(LVL lvl)
	{
		SetLevel(static_cast<bool>(lvl));
	};

	/**
	 * @brief Toggle the logic level of the pin.
	 */
	inline void TogglePin()
	{
		PORT->ODR ^= 0x1 << pin;
	};


#if defined(STM32F4)
	/**
	 * @brief Get the logic level of the pin using bit-banding.
	 * @return The logic level of the pin.
	 */
	inline bool GetLevel_BB()
	{
		return BIT_BB(&PORT->IDR, pin);
	};

	/**
	 * @brief Set the logic level of the pin using bit-banding.
	 * @param lvl_int The logic level as an integer.
	 */
	inline void SetLevel_BB(bool lvl)
	{
		BIT_BB(&PORT->ODR, pin) = static_cast<uint8_t>(lvl);
	}

	/**
	 * @brief Set the logic level of the pin using the specified enum and bit-banding.
	 * @param lvl The logic level enum.
	 */
	inline void SetLevel_BB(LVL lvl)
	{
		BIT_BB(&PORT->ODR, pin) = static_cast<uint8_t>(lvl);
	};

	/**
	 * @brief Toggle the logic level of the pin using bit-banding.
	 */
	inline void TogglePin_BB()
	{
		BIT_BB(&PORT->ODR, pin) ^= 1;
	};
#endif

	/**
	 * @brief Destructor for the PIN class.
	 */
	~PIN()
	{
		Reset();
	};
};

#include <cstddef> 

struct gpio_array_t {
	GPIO_TypeDef* port;
	uint32_t pin; 
};

template<size_t N>
class PinArray : public PIN
{
private:
	gpio_array_t gpio_array[N];

public:
	constexpr PinArray(const gpio_array_t (&_gpio_array)[N]) : gpio_array{} {
		for (size_t i = 0; i < N; ++i) {
			gpio_array[i] = _gpio_array[i];
		}
	}


};

// Deduction guide
template<size_t N>
PinArray(const gpio_array_t (&)[N]) -> PinArray<N>;





#endif
