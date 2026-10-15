#pragma once
#include <cstddef>
#include <cstdint>

// 7-string state per game object, keyed by the address of the object's 6-entry tuning array
// (detector + 0x1340, matcher + 0x44). Thread-safe.
struct Rs7Entry {
    bool active;  // 7-string mode for this object
    int16_t t6;   // tuning offset of string 6 relative to B1
};

void Rs7Store_Set(const void* key, Rs7Entry e);
bool Rs7Store_Get(const void* key, Rs7Entry* out);  // false if the key was never set (or set inactive)
bool Rs7Store_AnyActive();                          // some key is in 7-string mode
// Empties the store. Also constructs the map on first use: Rs7_Startup calls it before patching
// so the construction (which allocates) never happens inside a game frame.
void Rs7Store_ClearAll();

// Leak audit: active entries kept at most (older ones are dead sessions); the number of entries now.
constexpr size_t kRs7StoreMax = 64;
int Rs7Store_Count();
