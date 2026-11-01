/**
 * @file moving_average.hpp
 * @brief Fixed-window rolling sum with selectable startup and no heap.
 *
 * The average is sum / N; division is left to the caller. By default, the
 * first sample fills the whole window. Pass false to the constructor to keep
 * missing samples at zero instead; the divisor remains N. Integer division
 * truncates towards zero. Each instance owns its history; concurrent access
 * must be serialized by the caller.
 *
 * @code
 *   Filters::MovingAverage<4> filter;
 *   filter.add_new_sample(100);
 *   auto average = filter.sum / 4; // 100: all four slots contain the first sample.
 * @endcode
 */
#pragma once

#include <cstdint>

namespace Filters {

/**
 * @brief Ring buffer and rolling sum of the last N samples.
 * @tparam N Positive window size; storage is allocated inside the object.
 * @tparam Sample Input and ring-buffer element type, int32_t by default.
 * @tparam Sum Accumulator type, int64_t by default. Must represent every sample
 *             and intermediate sum without overflow.
 * Public fields expose the state; preserve sum == the sum of array and index < N.
 */
template<std::uint32_t N, typename Sample = std::int32_t, typename Sum = std::int64_t>
struct MovingAverage {
	Sample array[N]{};      ///< Ring buffer; missing samples initially equal zero.
	Sum sum{};             ///< Sum of all N slots; divide by N to obtain the average.
	std::uint32_t index{};  ///< Oldest slot, replaced by the next sample.
	bool initialized{};    ///< False only while waiting to fill from the first sample.

	/// Use first-sample startup when no constructor argument is supplied.
	constexpr MovingAverage() = default;

	/**
	 * @brief Choose whether the first sample fills the complete window.
	 * @param from_first_sample true (default): fill from the first sample;
	 *                          false: start at zero.
	 * The choice sets the initialization flag; no separate mode is stored.
	 */
	constexpr explicit MovingAverage(bool from_first_sample)
		: initialized(!from_first_sample)
	{}

	/**
	 * @brief Store a sample and update sum; O(1) after optional initial filling.
	 * @param sample New input in the same units as previous samples.
	 * First-sample startup fills N slots and their sum once, in O(N) work.
	 * No division is performed; read sum after the call. The caller ensures
	 * the accumulator and intermediate arithmetic do not overflow.
	 */
	void add_new_sample(Sample sample)
	{
		if (!initialized) {
			sum = 0;
			for (auto& stored : array) {
				stored = sample;
				sum += sample;
			}
			initialized = true;
			return;
		}

		sum -= array[index];
		array[index] = sample;
		sum += sample;

		if (++index == N) index = 0;
	}
};

} // namespace Filters
