#ifndef STATIC_MAP_HPP
#define STATIC_MAP_HPP

#include <cstddef> 

template <typename Key, typename Value, size_t N>
class StaticMap {
public:
	struct Entry {
		Key key;
		Value value;
		bool used = false;
	};

	bool insert(const Key& k, const Value& v) {
		for (auto& e : entries) {
			if (!e.used) {
				e.key = k;
				e.value = v;
				e.used = true;
				return true;
			}
		}
		return false;
	}

	Value* find(const Key& k) {
		for (auto& e : entries) {
			if (e.used && e.key == k) {
				return &e.value;
			}
		}
		return nullptr;
	}

	bool erase(const Key& k) {
		auto entry = find(k);
		if (entry) {
			entry->used = false;
			return true;
		}
		return false;
	}

	void clear() {
		for (auto& e : entries) e.used = false;
	}

	size_t size() const {
		size_t cnt = 0;
		for (auto& e : entries) if (e.used) cnt++;
		return cnt;
	}

private:
	Entry entries[N];
};

#endif // STATIC_MAP_HPP