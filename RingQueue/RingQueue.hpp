#ifndef RING_QUEUE_HPP
#define RING_QUEUE_HPP

#include <cstddef> 

template<typename T, size_t N>
class RingQueue {
	static_assert(N > 1, "Queue size must be > 1");

	// Compile-time check: is N a power of two?
	static constexpr bool is_power_of_two = (N & (N - 1)) == 0;

private:
	volatile T buffer[N];
	volatile size_t head = 0;
	volatile size_t tail = 0;

	inline size_t next_index(size_t index) const {
		if constexpr (is_power_of_two) {
			return (index + 1) & (N - 1); // Fast wrap
		} else {
			size_t next = index + 1;
			return (next >= N) ? 0 : next; // General case
		}
	}

public:
	inline bool push(const T& item) {
		size_t next = next_index(head);
		if (next == tail) return false; // full
		buffer[head] = item;
		head = next;
		return true;
	}

	inline bool pop(T& item_out) {
		if (tail == head) return false; // empty
		item_out = buffer[tail];
		tail = next_index(tail);
		return true;
	}

	inline bool empty() const { return head == tail; };
	inline bool full() const { return next_index(head) == tail; };
};


#endif // RING_QUEUE_HPP
