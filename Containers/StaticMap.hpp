#ifndef STATIC_MAP_HPP
#define STATIC_MAP_HPP

#include <cstddef> 
#include <type_traits>

template <typename Key, typename Value, size_t N>
class StaticMap {
private:
	struct Entry {
		Key key;
		Value value;
		bool used = false;
	};

	Entry entries[N];

	Entry* find_entry(const Key& k) {
	for (auto& e : entries) {
		if (e.used && e.key == k) {
			return &e;
		}
	}
	return nullptr;
}

	size_t used_count = 0;

public:
	bool insert(const Key &k, const Value &v) {
		for (auto& e : entries) {
			if (!e.used) {
				e.key = k;
				e.value = v;
				e.used = true;
				++used_count;
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
		auto entry = find_entry(k);
		if (entry) {
			entry->used = false;
			--used_count;
			return true;
		}
		return false;
	}

	void clear() {
		for (auto& e : entries) e.used = false;
		used_count = 0;
	}

	size_t size() const {
		return used_count;
	}
	
	template <typename F>
	void for_each_value(F&& fn) {
		for (auto& e : entries) {
			if (e.used) fn(e.value);
		}
	}

	template <typename F>
	void for_each_entry(F&& fn) {
		for (auto& e : entries) {
			if (e.used) fn(*this, e.key, e.value);
		}
	}
};

#endif // STATIC_MAP_HPP