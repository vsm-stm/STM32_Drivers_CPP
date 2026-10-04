#ifndef STATIC_DLIST_HPP
#define STATIC_DLIST_HPP

#include <cstddef>
#include <cstdint>

template<typename T, size_t N>
class StaticDList {
public:
	static_assert(N > 0, "N > 0");
	using index_type = size_t;
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

	// basic API
	bool        empty() const { return sz == 0; }
	bool        full()  const { return sz == N; }
	size_t      size()  const { return sz; }
	index_type  head()  const { return head_idx; }
	index_type  tail()  const { return tail_idx; }
	index_type  next(index_type i) const { return (i < N) ? nodes[i].next : npos; }
	index_type  prev(index_type i) const { return (i < N) ? nodes[i].prev : npos; }
		  T&    ref (index_type i)       { return nodes[i].value; }
	const T&    ref (index_type i) const { return nodes[i].value; }
	bool        valid(index_type i) const { return (i < N) && nodes[i].used; }

	// append at the tail
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

	// move node i to the tail (O(1))
	void move_to_tail(index_type i) {
		if (i == npos || !nodes[i].used || i == tail_idx) return;
		unlink(i);
		nodes[i].prev = tail_idx;
		nodes[i].next = npos;
		if (tail_idx != npos) nodes[tail_idx].next = i;
		tail_idx = i;
		if (head_idx == npos) head_idx = i;
	}

	// ordinary removal (unlink + free)
	bool erase(index_type i) {
		if (i == npos || !nodes[i].used) return false;
		unlink(i);
		free_node(i);
		--sz;
		return true;
	}

	// CONVENIENT FOR ITERATION: returns the index of the node AFTER erasing i.
	// Guarantees a correct step in an external for loop.
	index_type erase_and_next(index_type i) {
		if (i == npos || !nodes[i].used) return npos;
		index_type n = nodes[i].next; // save next BEFORE unlinking
		unlink(i);
		free_node(i);
		--sz;
		return n; // safe to continue with n
	}
};

#endif // STATIC_DLIST_HPP
