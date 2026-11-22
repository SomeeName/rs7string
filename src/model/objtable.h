#pragma once
#include <cstddef>

// A small fixed table of per-object state keyed by an object address (Key{} = a free entry): find, insert or replace,
// erase one, clear all. A full table replaces its entries in turn (round robin). The turn only advances on an eviction,
// so an entry that refilled a freed slot can be the next one evicted. No heap and no locks: each caller keeps its own
// threading rule.

namespace rsxr {
template <class Key, class Value, size_t N>
class ObjTable {
public:
    struct Entry {
        Key key;
        Value value;
    };

    // The value stored for `key`, or nullptr (also for Key{}).
    Value* Find(Key key) {
        if (key == Key{}) {
            return nullptr;
        }

        for (Entry& e : entries_) {
            if (e.key == key) {
                return &e.value;
            }
        }

        return nullptr;
    }

    const Value* Find(Key key) const { return const_cast<ObjTable*>(this)->Find(key); }

    // Stores `value` for `key` in its own entry, else the first free one, else the next one in turn. Key{} (the free
    // marker) is not stored: the value goes to a scratch slot outside the table.
    Value& Put(Key key, const Value& value) {
        if (key == Key{}) {
            scratch_ = value;
            return scratch_;
        }

        Entry* slot = nullptr;

        for (Entry& e : entries_) {
            if (e.key == key) {
                slot = &e;
                break;
            }
        }

        if (!slot) {
            for (Entry& e : entries_) {
                if (e.key == Key{}) {
                    slot = &e;
                    break;
                }
            }
        }

        if (!slot) {
            slot = &entries_[next_];
            next_ = (next_ + 1) % N;
        }

        slot->key = key;
        slot->value = value;

        return slot->value;
    }

    // Frees the entry of `key` (its value is reset to Value{}); nothing for an unknown key or Key{}.
    void Erase(Key key) {
        if (key == Key{}) {
            return;
        }

        for (Entry& e : entries_) {
            if (e.key == key) {
                e = Entry{};
            }
        }
    }

    // Frees every entry. The eviction turn carries on where it was.
    void Clear() {
        for (Entry& e : entries_) {
            e = Entry{};
        }
    }

    // Every entry, free ones included (key == Key{}).
    Entry* begin() { return entries_; }

    Entry* end() { return entries_ + N; }

    const Entry* begin() const { return entries_; }

    const Entry* end() const { return entries_ + N; }

private:
    Entry entries_[N] = {};
    size_t next_ = 0;
    Value scratch_{};  // Put(Key{})'s result
};
}  // namespace rsxr
