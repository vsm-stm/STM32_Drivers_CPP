/**
 * @file median3.hpp
 * @brief Median of three recent samples with fixed storage and no heap.
 *
 * By default the first sample fills all three slots and becomes the output.
 * Pass false to the constructor to keep the initial zero-filled history.
 * Later calls replace the oldest slot and select the middle value using three
 * comparisons. Only local copies are reordered; history stays in arrival order.
 * Each instance owns its history; concurrent access must be serialized.
 *
 * @code
 *   Filters::Median3<> filter;
 *   filter.add_new_sample(100); // value = 100; all slots start at 100.
 *   filter.add_new_sample(900); // value = 100; reject an isolated spike.
 *   filter.add_new_sample(102); // value = 102.
 * @endcode
 */
#pragma once

#include <cstdint>

namespace Filters {

/**
 * @brief Ring buffer and median output for a three-sample window.
 * @tparam Sample Input/output type, int32_t by default. Must support copying
 *                and ordered comparison; floating-point inputs must be finite.
 * Reset the complete history with filter = {}. Public state must remain
 * consistent: index < 3, and value corresponds to the stored window.
 */
template<typename Sample = std::int32_t>
struct Median3 {
	Sample array[3]{};     ///< Ring buffer; zero or first-sample startup is selected at construction.
	Sample value{};        ///< Current median; zero before the first sample.
	std::uint32_t index{}; ///< Oldest slot, replaced by the next sample.
	bool initialized{};   ///< False only while waiting to fill from the first sample.

	/// Use first-sample startup when no constructor argument is supplied.
	constexpr Median3() = default;

	/**
	 * @brief Choose whether the first sample fills the complete window.
	 * @param from_first_sample true: fill from the first sample; false: start at zero.
	 * The choice sets the initialization flag; no separate mode is stored.
	 */
	constexpr explicit Median3(bool from_first_sample)
		: initialized(!from_first_sample)
	{}

	/**
	 * @brief Store one sample and update value in constant work.
	 * @param sample New input in the same units as previous samples.
	 * First-sample startup fills the window once. Zero startup replaces one
	 * zero-filled slot from the first call; subsequent calls replace one slot.
	 * No arithmetic on sample values or allocation; the ring is not reordered.
	 */
	void add_new_sample(Sample sample)
	{
		if (!initialized) {
			array[0] = array[1] = array[2] = value = sample;
			initialized = true;
			return;
		}

		array[index] = sample;
		if (++index == 3) index = 0;

		Sample a = array[0];
		Sample b = array[1];
		Sample c = array[2];
		if (b < a) { Sample temp = a; a = b; b = temp; }
		if (c < b) b = c;
		if (b < a) b = a;
		value = b;
	}
};

} // namespace Filters
