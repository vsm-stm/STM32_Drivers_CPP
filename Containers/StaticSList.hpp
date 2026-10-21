/**
 * @file StaticSList.hpp
 * @brief Fixed-capacity singly linked list with pointer-based value iteration.
 *
 * push_back is O(1), erase_first_if is O(size). Storage/free chain are internal.
 * T is standard-layout because next() recovers Node from a live value pointer;
 * the static_assert rejects unsupported T at compilation.
 *
 * @code
 *   StaticSList<int, 8> list;
 *   list.push_back(10);
 *   list.push_back(20);
 *   for (int* value = list.front(); value; value = list.next(value)) {
 *       // Use *value without structurally changing the list during this traversal.
 *   }
 *   bool removed = list.erase_first_if([](int value) { return value == 10; });
 * @endcode
 */
#ifndef STATIC_SLIST_HPP
#define STATIC_SLIST_HPP

#include <cstddef>
#include <cstdint>
#include <type_traits>

// Fixed-capacity list. Concurrent access must be serialized by the caller.
// T must be standard-layout, default-constructible and copy-assignable.
// All slot values exist for the list's lifetime; erasing an element does not
// destroy its value or release its resources. An erased element's pointer
// must no longer be used, even if a later insertion reuses the same address.
template<typename T, size_t N>
class StaticSList {
	static_assert(N > 0, "StaticSList: N must be > 0");
	static_assert(std::is_standard_layout<T>::value,
		"StaticSList: T must be standard-layout for next()");

	struct Node {
		T value;
		int next = -1;
		bool used = false;
	};

	Node pool[N];
	int head = -1;
	int tail = -1;
	int free_head = 0;
	size_t used_count = 0;

public:
	StaticSList() {
		// build the free list
		for (int i = 0; i < (int)N - 1; i++) {
			pool[i].next = i + 1;
		}
		pool[N - 1].next = -1;
	}

	/// First live value, or nullptr if empty; returned pointer does not own storage.
	T* front() {
		if (head == -1) return nullptr;
		return &pool[head].value;
	}

	// cur must be nullptr or a live element of this list from front()/next().
	// Foreign pointers and pointers to erased elements are not accepted.
	T* next(const T* cur) {
		if (!cur) return nullptr;
		const Node* n = reinterpret_cast<const Node*>(
			reinterpret_cast<const char*>(cur) - offsetof(Node, value));
		int idx = (int)(n - pool);
		if (idx < 0 || idx >= (int)N || !pool[idx].used) return nullptr;
		int next_idx = pool[idx].next;
		if (next_idx == -1) return nullptr;
		return &pool[next_idx].value;
	}

	/// Copy into a free slot and append; false when full, with no list mutation.
	bool push_back(const T& val) {
		if (free_head == -1) return false;
		int idx = free_head;
		free_head = pool[idx].next;

		pool[idx].value = val;
		pool[idx].used = true;
		pool[idx].next = -1;

		if (tail == -1) {
			head = tail = idx;
		} else {
			pool[tail].next = idx;
			tail = idx;
		}
		++used_count;
		return true;
	}

	/**
	 * @brief Remove only the first predicate match in front-to-back order.
	 * @param pred Receives stored T; must not change the list structure.
	 * @return true after one removal, false if no match.
	 * Erased pointers become invalid; no value destructor/reset is invoked.
	 */
	template<typename Pred>
	bool erase_first_if(Pred&& pred) {
		int prev = -1;
		int idx = head;
		while (idx != -1) {
			if (pool[idx].used && pred(pool[idx].value)) {
				int next_idx = pool[idx].next;

				if (prev == -1) head = next_idx;
				else pool[prev].next = next_idx;

				if (idx == tail) tail = prev;

				// return it to the free list
				pool[idx].used = false;
				pool[idx].next = free_head;
				free_head = idx;
				--used_count;
				return true;
			}
			prev = idx;
			idx = pool[idx].next;
		}
		return false;
	}

	bool empty() const { return used_count == 0; }
	bool full() const { return used_count == N; }
	size_t size() const { return used_count; }
};

#endif // STATIC_SLIST_HPP
