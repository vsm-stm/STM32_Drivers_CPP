#ifndef STATIC_MAP_HPP
#define STATIC_MAP_HPP

#include <cstddef>

// Fixed-capacity map with unique keys: insert() of an existing key replaces
// its value. Concurrent access must be serialized by the caller.
// Key and Value must be default-constructible and copy-assignable; keys must
// support equality comparison. All slots exist for the map's lifetime;
// erase()/clear() do not destroy keys or values or release their resources.
// A pointer returned by find() must no longer be used after erasing that key
// or clearing the map, even if a later insertion reuses the same address.
template <typename Key, typename Value, size_t N>
class StaticMap {
	static_assert(N > 0, "StaticMap: N must be > 0");
public:
	struct Entry {
		Key key;
		Value value;
		bool used = false;
	};

	// Direct access to capacity() slots; read key/value only in used slots.
	// The caller must preserve unique keys among used entries and the number
	// of used entries. Raw changes are not checked; changing used does not
	// update size(). Use insert()/erase()/clear() to change occupancy.
	Entry* raw_entries() { return entries; }
	const Entry* raw_entries() const { return entries; }
	size_t capacity() const { return N; }

private:
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

public :
	/** @return false only when k is new and the map is full. */
	bool insert(const Key &k, const Value &v) {
		if (auto existing = find_entry(k)) {
			existing->value = v;
			return true;
		}
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
		auto entry = find_entry(k);
		return entry ? &entry->value : nullptr;
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

};

#endif // STATIC_MAP_HPP
