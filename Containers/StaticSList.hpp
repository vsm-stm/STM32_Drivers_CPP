#ifndef STATIC_SLIST_HPP
#define STATIC_SLIST_HPP

#include <cstddef>
#include <cstdint>

template<typename T, size_t N>
class StaticSList {
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
		// формируем список свободных
		for (int i = 0; i < (int)N - 1; i++) {
			pool[i].next = i + 1;
		}
		pool[N - 1].next = -1;
	}

	// доступ к началу
	T* front() {
		if (head == -1) return nullptr;
		return &pool[head].value;
	}

	// переход к следующему
	T* next(const T* cur) {
		const Node* n = reinterpret_cast<const Node*>(
			reinterpret_cast<const char*>(cur) - offsetof(Node, value));
		int idx = (int)(n - pool);
		if (idx < 0 || idx >= (int)N) return nullptr;
		int next_idx = pool[idx].next;
		if (next_idx == -1) return nullptr;
		return &pool[next_idx].value;
	}

	// вставка в конец
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

	// удаление по условию
	template<typename Pred>
	bool erase_if(Pred&& pred) {
		int prev = -1;
		int idx = head;
		while (idx != -1) {
			if (pool[idx].used && pred(pool[idx].value)) {
				int next_idx = pool[idx].next;

				if (prev == -1) head = next_idx;
				else pool[prev].next = next_idx;

				if (idx == tail) tail = prev;

				// вернуть в свободный список
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
