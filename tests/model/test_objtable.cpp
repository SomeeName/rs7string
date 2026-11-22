// model/objtable.h: the per-object side table the features keep their state in (pitch, songend, string7, frets, tuner).
#include "model/objtable.h"
#include "test.h"

namespace {
using Table = rsxr::ObjTable<const int*, int, 4>;

int Live(const Table& t) {
    int n = 0;

    for (const Table::Entry& e : t) {
        n += e.key ? 1 : 0;
    }

    return n;
}
}  // namespace

// Catches: a lookup that matches a free entry, or an insert that adds a second entry for a known key.
TEST(model_objtable_find_put_replace) {
    Table t;
    int a = 0, b = 0;

    CHECK(!t.Find(&a));
    CHECK(!t.Find(nullptr));  // never a free entry
    CHECK_EQ(t.Put(&a, 1), 1);
    CHECK_EQ(t.Put(&b, 2), 2);
    CHECK_EQ(t.Put(&a, 3), 3);  // replaced in place
    CHECK_EQ(*t.Find(&a), 3);
    CHECK_EQ(*t.Find(&b), 2);
    CHECK_EQ(Live(t), 2);
    *t.Find(&b) = 4;
    CHECK_EQ(*t.Find(&b), 4);
}

// Catches: Put(Key{}) writing into a free entry (or evicting one) although Key{} marks a free entry.
TEST(model_objtable_put_free_key_rejected) {
    Table t;
    int k[4] = {};

    CHECK_EQ(t.Put(nullptr, 7), 7);  // returned, not stored
    CHECK_EQ(Live(t), 0);

    for (const Table::Entry& e : t) {
        CHECK_EQ(e.value, 0);
    }

    for (int i = 0; i < 4; ++i) {
        t.Put(&k[i], i + 1);
    }

    t.Put(nullptr, 9);  // a full table evicts nothing for Key{}

    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(*t.Find(&k[i]), i + 1);
    }
}

// Catches: an erase that leaves the value behind, or a freed entry not reused before an eviction.
TEST(model_objtable_erase_and_clear) {
    Table t;
    int k[5] = {};

    for (int i = 0; i < 4; ++i) {
        t.Put(&k[i], i + 10);
    }

    t.Erase(&k[1]);
    t.Erase(nullptr);  // no effect
    CHECK(!t.Find(&k[1]));
    CHECK_EQ(Live(t), 3);
    t.Put(&k[4], 14);  // takes the freed entry: nothing evicted
    CHECK_EQ(*t.Find(&k[0]), 10);
    CHECK_EQ(*t.Find(&k[2]), 12);
    CHECK_EQ(*t.Find(&k[3]), 13);
    CHECK_EQ(*t.Find(&k[4]), 14);
    t.Clear();
    CHECK_EQ(Live(t), 0);

    for (const Table::Entry& e : t) {
        CHECK_EQ(e.value, 0);
    }
}

// Catches: a full table that always evicts entry 0 (co-op: the second player's insert evicted the first player's).
TEST(model_objtable_full_table_evicts_in_turn) {
    Table t;
    int k[8] = {};

    for (int i = 0; i < 4; ++i) {
        t.Put(&k[i], i);
    }

    t.Put(&k[4], 4);  // evicts k[0]
    t.Put(&k[5], 5);  // evicts k[1], not the newest k[4]
    CHECK(!t.Find(&k[0]));
    CHECK(!t.Find(&k[1]));
    CHECK_EQ(*t.Find(&k[4]), 4);
    CHECK_EQ(*t.Find(&k[5]), 5);
    t.Put(&k[5], 50);  // a known key never evicts
    CHECK_EQ(*t.Find(&k[2]), 2);
    CHECK_EQ(*t.Find(&k[3]), 3);
    t.Put(&k[6], 6);  // evicts k[2]
    t.Put(&k[7], 7);  // evicts k[3]
    t.Put(&k[0], 0);  // wraps: evicts k[4]
    CHECK(!t.Find(&k[4]));
    CHECK_EQ(*t.Find(&k[5]), 50);
    CHECK_EQ(*t.Find(&k[6]), 6);
    CHECK_EQ(*t.Find(&k[7]), 7);
    CHECK_EQ(*t.Find(&k[0]), 0);
    CHECK_EQ(Live(t), 4);
}
