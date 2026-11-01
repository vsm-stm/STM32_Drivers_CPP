/**
 * @file exponential_moving_average.hpp
 * @brief Exponential smoothing with one output value and no sample buffer.
 *
 * By default the first sample becomes the output immediately. Pass false as
 * the second constructor argument to start smoothing from zero instead.
 * Samples update value += alpha * (sample - value). Input conversion happens before
 * subtraction, so integer sample differences are not evaluated as integers.
 * Each instance owns its state; concurrent access must be serialized.
 *
 * @code
 *   Filters::ExponentialMovingAverage<> filter{0.25f};
 *   filter.add_new_sample(100); // value = 100.0f; no initial rise from zero.
 *   filter.add_new_sample(200); // value = 125.0f.
 * @endcode
 */
#pragma once

#include <cstdint>
#include <type_traits>

namespace Filters {

/**
 * @brief Previous output and smoothing coefficient for one signal.
 * @tparam Sample Input type, int32_t by default; convertible to Value.
 * @tparam Value Floating-point calculation/output type, float by default.
 * All input/state values and intermediate results must be finite. Integer
 * inputs outside the exact range of Value are rounded during conversion.
 */
template<typename Sample = std::int32_t, typename Value = float>
struct ExponentialMovingAverage {
	static_assert(std::is_floating_point<Value>::value,
				  "ExponentialMovingAverage: Value must be floating-point");

	Value alpha;        ///< Smoothing coefficient, 0 < alpha <= 1; not clamped.
	Value value{};      ///< Current output; zero before the first sample.
	bool initialized{}; ///< False only while waiting to initialize from the first sample.

	/**
	 * @brief Set the coefficient and choose the initial output behavior.
	 * @param coefficient Finite value in (0, 1]; smaller means stronger smoothing.
	 * @param from_first_sample true (default): initialize from the first sample;
	 *                          false: apply smoothing from an initial zero output.
	 * The caller guarantees the range. No hardware or dynamic memory is used.
	 */
	constexpr explicit ExponentialMovingAverage(Value coefficient, bool from_first_sample = true)
		: alpha(coefficient), initialized(!from_first_sample)
	{}

	/**
	 * @brief Convert one sample and update value in constant work.
	 * @param sample New input in the same units as previous samples.
	 * First-sample startup assigns the first input directly. Zero startup applies
	 * alpha from the first call. Later calls use the previous filtered output.
	 */
	void add_new_sample(Sample sample)
	{
		const Value input = static_cast<Value>(sample);
		if (!initialized) {
			value = input;
			initialized = true;
			return;
		}
		value += alpha * (input - value);
	}
};

} // namespace Filters
