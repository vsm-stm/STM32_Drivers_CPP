/**
 * @file RingQueue.hpp
 * @brief Fixed-storage FIFO with N-1 usable elements and no heap allocation.
 *
 * One slot distinguishes full from empty: RingQueue<int,4> stores three items.
 * push/pop/size are O(1). Power-of-two N wraps by masking; otherwise an increment
 * and comparison wrap the index, without run-time division.
 *
 * @code
 *   RingQueue<int, 4> queue;
 *   bool stored = queue.push(42);
 *   int value;
 *   if (queue.pop(value)) {
 *       // Use value; 42 if the push succeeded.
 *   }
 * @endcode
 * This is a single-context container, not an interrupt/main SPSC primitive.
 */
#ifndef RING_QUEUE_HPP
#define RING_QUEUE_HPP

#include <cstddef>

// Single-context FIFO with usable capacity N-1. Concurrent access, including
// interrupt/main-loop access, must be serialized by the caller.
// T must be default-constructible and copy-assignable. All slots exist for the
// queue's lifetime; pop() does not destroy the stored value or release its
// resources. A slot's value is replaced when that slot is reused.
template<typename T, size_t N>
class RingQueue {
	static_assert(N > 1, "Queue size must be > 1");

	// Compile-time check: is N a power of two?
	static constexpr bool is_power_of_two = (N & (N - 1)) == 0;

private:
	T buffer[N];
	size_t head = 0;
	size_t tail = 0;

	inline size_t next_index(size_t index) const {
		if constexpr (is_power_of_two) {
			return (index + 1) & (N - 1); // Fast wrap
		} else {
			size_t next = index + 1;
			return (next >= N) ? 0 : next; // General case
		}
	}

public:
	/**
	 * @brief Assign item into the next available slot.
	 * @return false if full; content/order then remain unchanged.
	 */
	inline bool push(const T& item) {
		size_t next = next_index(head);
		if (next == tail) return false; // full
		buffer[head] = item;
		head = next;
		return true;
	}

	/**
	 * @brief Copy and remove the oldest item.
	 * @return false if empty, leaving item_out unchanged.
	 * Removal advances an index; it does not destroy/reset the stored slot.
	 */
	inline bool pop(T& item_out) {
		if (tail == head) return false; // empty
		item_out = buffer[tail];
		tail = next_index(tail);
		return true;
	}

	inline bool empty() const { return head == tail; }
	inline bool full() const { return next_index(head) == tail; }
	/// Current logical item count, 0..N-1.
	inline size_t size() const {
		if (head >= tail) return head - tail;
		return N - tail + head; // wrap case
	}
};


#endif // RING_QUEUE_HPP
