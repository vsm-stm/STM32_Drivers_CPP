/**
 * @file StaticDList.hpp
 * @brief Fixed-capacity doubly linked list addressed by live-slot indices.
 *
 * push_back/erase/move_to_tail/navigation are O(1); construction is O(N).
 * A live index stays stable when moving a node. Erasing invalidates that index
 * and its references, even if later insertion reuses the same physical slot.
 *
 * @code
 *   StaticDList<int, 8> list;
 *   StaticDList<int, 8>::index_type saved;
 *   if (list.push_back(7, &saved)) list.move_to_tail(saved);
 *   for (auto i = list.head(); i != decltype(list)::npos; ) {
 *       if (list.ref(i) == 7) i = list.erase_and_next(i);
 *       else i = list.next(i);
 *   }
 * @endcode
 */
#ifndef STATIC_DLIST_HPP
#define STATIC_DLIST_HPP

#include <cstddef>
#include <cstdint>

// Fixed-capacity list. Concurrent access must be serialized by the caller.
// T must be default-constructible and copy-assignable. All slot values exist
// for the list's lifetime; erase does not destroy a value or release its
// resources. Erasing an element invalidates its index and references;
// a later insertion may reuse that index for a different element.
template<typename T, size_t N>
class StaticDList {
public:
	static_assert(N > 0, "N > 0");
	using index_type = size_t;
	/// No-element sentinel, including empty head/tail and unsuccessful navigation.
	static constexpr index_type npos = static_cast<index_type>(-1);

private:
	struct Node {
		T          value;
		index_type next;
		index_type prev;
		bool       used;
	};

	Node        nodes[N];
	index_type  head_idx;
	index_type  tail_idx;
	index_type  free_head;
	size_t      sz;

	index_type alloc_node() {
		if (free_head == npos) return npos;
		index_type i = free_head;
		free_head = nodes[i].next;
		nodes[i].used = true;
		nodes[i].next = npos;
		nodes[i].prev = npos;
		return i;
	}
	void free_node(index_type i) {
		nodes[i].used = false;
		nodes[i].next = free_head;   // onto the free stack
		nodes[i].prev = npos;
		free_head = i;
	}
	void unlink(index_type i) {
		index_type p = nodes[i].prev;
		index_type n = nodes[i].next;
		if (p != npos) nodes[p].next = n; else head_idx = n;
		if (n != npos) nodes[n].prev = p; else tail_idx = p;
		nodes[i].next = nodes[i].prev = npos;
	}

public:
	StaticDList() : head_idx(npos), tail_idx(npos), free_head(0), sz(0) {
		for (index_type i = 0; i < N; ++i) {
			nodes[i].used = false;
			nodes[i].next = (i + 1 < N) ? (i + 1) : npos; // free nodes as a stack
			nodes[i].prev = npos;
		}
	}

	// Logical size/order, independent of physical slot numbers.
	// head()/tail() return npos if empty; ref(i) requires valid(i).
	bool        empty() const { return sz == 0; }
	bool        full()  const { return sz == N; }
	size_t      size()  const { return sz; }
	index_type  head()  const { return head_idx; }
	index_type  tail()  const { return tail_idx; }
	// Out-of-range and unused indices return npos.
	index_type  next(index_type i) const { return valid(i) ? nodes[i].next : npos; }
	index_type  prev(index_type i) const { return valid(i) ? nodes[i].prev : npos; }
		  T&    ref (index_type i)       { return nodes[i].value; }   // precondition: valid(i)
	const T&    ref (index_type i) const { return nodes[i].value; } // precondition: valid(i)
	bool        valid(index_type i) const { return (i < N) && nodes[i].used; }

	/**
	 * @brief Copy to a free slot and append in logical order.
	 * @param out_idx Optional output receiving the live index on success only.
	 * @return false if full; no list mutation/out_idx write then.
	 */
	bool push_back(const T& v, index_type* out_idx = nullptr) {
		index_type i = alloc_node();
		if (i == npos) return false;
		nodes[i].value = v;
		nodes[i].next  = npos;
		nodes[i].prev  = tail_idx;
		if (tail_idx != npos) nodes[tail_idx].next = i;
		tail_idx = i;
		if (head_idx == npos) head_idx = i;
		++sz;
		if (out_idx) *out_idx = i;
		return true;
	}

	/// Relink a live node at the tail in O(1); invalid/already-tail index is a no-op.
	void move_to_tail(index_type i) {
		if (!valid(i) || i == tail_idx) return;
		unlink(i);
		nodes[i].prev = tail_idx;
		nodes[i].next = npos;
		if (tail_idx != npos) nodes[tail_idx].next = i;
		tail_idx = i;
		if (head_idx == npos) head_idx = i;
	}

	/// Unlink/free a live node without destroying its value; false for invalid/unused i.
	bool erase(index_type i) {
		if (!valid(i)) return false;
		unlink(i);
		free_node(i);
		--sz;
		return true;
	}

	/**
	 * @brief Remove i and return its saved successor, or npos.
	 * Use as the iterator step when erasing; next(i) after erase returns npos
	 * for the unused index, not its old successor.
	 */
	index_type erase_and_next(index_type i) {
		if (!valid(i)) return npos;
		index_type n = nodes[i].next; // save next BEFORE unlinking
		unlink(i);
		free_node(i);
		--sz;
		return n; // safe to continue with n
	}
};

#endif // STATIC_DLIST_HPP
