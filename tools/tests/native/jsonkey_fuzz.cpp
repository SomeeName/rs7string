// The two manifest-key readers that walk the game's JSON vtables (rs7::JsonNumber, rs7::HasOffset in model/manifest.h) against
// synthetic objects: null/unreadable objects, vtables, slots and nodes, a number record running into a
// PAGE_NOACCESS page, and boundary / non-finite / fixed-seed random doubles. They must fail closed, never fault.
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
const char* kKey = "Rs7String6";
#include "model/manifest.h"  // rs7::JsonNumber, rs7::HasOffset (the tuner's and C-lite's readers)

struct Node { void** vt; uint8_t bytes[16]; };
struct Object { void** vt; void* child; };
static uint8_t* g_numberAt = nullptr;
void* __fastcall Find(void* self, void*, const char*) { return static_cast<Object*>(self)->child; }
uint8_t* __fastcall Number(void* self, void*) { return g_numberAt ? g_numberAt : static_cast<Node*>(self)->bytes; }

static uint8_t* GuardedEnd() {
    SYSTEM_INFO si; GetSystemInfo(&si);
    uint8_t* m = static_cast<uint8_t*>(VirtualAlloc(nullptr, 2 * si.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    DWORD old;
    return m && VirtualProtect(m + si.dwPageSize, si.dwPageSize, PAGE_NOACCESS, &old) ? m + si.dwPageSize : nullptr;
}

static int failures = 0;
static void Expect(const char* what, void* obj, bool numberOk, bool hasOk) {
    double out = 12345.0;
    const bool n = rs7::JsonNumber(obj, kKey, &out);
    const bool h = rs7::HasOffset(obj, kKey);
    if (n != numberOk || h != hasOk || (!n && out != 12345.0)) {
        std::printf("FAIL %s: JsonNumber %d (want %d), HasOffset %d (want %d)\n", what, n, numberOk, h, hasOk);
        ++failures;
    }
}

int main() {
    uint8_t* guard = GuardedEnd();
    if (!guard) return 90;
    void *ovt[6] = {}, *nvt[21] = {};
    ovt[5] = reinterpret_cast<void*>(&Find); nvt[20] = reinterpret_cast<void*>(&Number);
    Node node = {nvt, {}};
    Object obj = {ovt, &node};
    double v = 1.0; std::memcpy(node.bytes + 8, &v, 8);
    Expect("valid", &obj, true, true);

    // Structural failures: each pointer on the path null, unreadable or a guard-page address.
    Expect("null object", nullptr, false, false);
    Expect("object at a guard page", guard, false, false);
    Expect("object pointer 1", reinterpret_cast<void*>(1), false, false);
    Object badVt = {reinterpret_cast<void**>(guard), &node};
    Expect("vtable at a guard page", &badVt, false, false);
    ovt[5] = nullptr; Expect("null Find slot", &obj, false, false); ovt[5] = reinterpret_cast<void*>(&Find);
    ovt[5] = guard; Expect("Find slot points at a guard page", &obj, false, false); ovt[5] = reinterpret_cast<void*>(&Find);
    obj.child = nullptr; Expect("missing key", &obj, false, false);
    obj.child = guard; Expect("node at a guard page", &obj, false, false);
    obj.child = guard - 4; Expect("node vtable pointer straddles a guard page", &obj, false, false);
    obj.child = &node;
    nvt[20] = nullptr; Expect("null Number slot", &obj, false, false); nvt[20] = reinterpret_cast<void*>(&Number);
    for (int back = 0; back <= 16; ++back) {  // the number record (value at +8) running into the guard page
        g_numberAt = guard - back;
        if (back >= 16) std::memcpy(guard - back + 8, &v, 8);
        Expect(back >= 16 ? "number record just fits" : "number record crosses a guard page", &obj, back >= 16, back >= 16);
    }
    g_numberAt = nullptr;

    // Values: JsonNumber returns any number (callers range-check), HasOffset only (-25, 25).
    const double inf = std::numeric_limits<double>::infinity(), nan = std::numeric_limits<double>::quiet_NaN();
    const double values[] = {0.0, -0.0, 24.999999, -24.999999, 25.0, -25.0, std::nextafter(25.0, 0.0),
                             std::nextafter(-25.0, 0.0), 1e308, -1e308, inf, -inf, nan, -nan,
                             std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::max()};
    for (double d : values) {
        std::memcpy(node.bytes + 8, &d, 8);
        Expect("boundary value", &obj, true, d > -25.0 && d < 25.0);
    }
    uint32_t rng = 0x15c0ffeeu;
    for (int i = 0; i < 20000; ++i) {  // random bit patterns: NaN payloads, denormals, huge exponents
        uint32_t w[2];
        for (uint32_t& x : w) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; x = rng; }
        std::memcpy(node.bytes + 8, w, 8);
        double d; std::memcpy(&d, w, 8);
        double out = 0;
        if (!rs7::JsonNumber(&obj, kKey, &out) || std::memcmp(&out, w, 8) ||
            rs7::HasOffset(&obj, kKey) != (d > -25.0 && d < 25.0)) {
            std::printf("FAIL: random value %08x%08x\n", w[1], w[0]); return 2;
        }
    }
    if (failures) return 1;
    std::puts("PASS: JsonNumber/HasOffset fail closed on every bad pointer; values and NaN/inf handled");
}
