#include <windows.h>
#include <cstdint>
#include <initializer_list>
#include <cstdio>
#include <cstring>
#include <limits>
const char* kKey = "Rs7String6";
#include "readkey.inc"
struct Node { void** vt; uint8_t bytes[16]; };
struct Object { void** vt; Node* child; };
static const char* expectedKey = kKey;
void* __fastcall Find(void* self, void*, const char* key) {
    return std::strcmp(key, expectedKey) ? nullptr : static_cast<Object*>(self)->child;
}
uint8_t* __fastcall Number(void* self, void*) { return static_cast<Node*>(self)->bytes; }
uint8_t* __fastcall NotNumber(void*, void*) { return nullptr; }
int main() {
    void* ovt[6] = {}, *nvt[21] = {};
    ovt[5] = reinterpret_cast<void*>(&Find); nvt[20] = reinterpret_cast<void*>(&Number);
    Node node = {nvt, {}}; Object obj = {ovt, &node}; void* holder = &obj;
    for (double value : {-24.999, -24.0, -0.75, 0.0, 0.75, 24.0, 24.999}) {
        std::memcpy(node.bytes + 8, &value, 8); int out = 999; double raw = 999;
        if (!ReadKey(&holder, &out, &raw) || out != int(value) || raw != value) return 1;
    }
    for (double value : {-25.0, 25.0, -1e100, 1e100, std::numeric_limits<double>::infinity(),
                         -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        std::memcpy(node.bytes + 8, &value, 8); int out = 999; double raw = 999;
        if (ReadKey(&holder, &out, &raw) || out != 999 || raw != 999) return 2;
    }
    int out = 999; double raw = 999;
    if (ReadKey(nullptr, &out, &raw)) return 3;
    void* missing = nullptr;
    if (ReadKey(&missing, &out, &raw)) return 4;
    obj.child = nullptr; if (ReadKey(&holder, &out, &raw)) return 5; obj.child = &node;
    nvt[20] = reinterpret_cast<void*>(&NotNumber); if (ReadKey(&holder, &out, &raw)) return 6;
    nvt[20] = reinterpret_cast<void*>(&Number);
    void* bad = reinterpret_cast<void*>(1); if (ReadKey(&bad, &out, &raw)) return 7;
    expectedKey = "Rs7String7"; double value = -2; std::memcpy(node.bytes + 8, &value, 8);
    if (ReadKey(&holder, &out, &raw) || !ReadKey(&holder, &out, &raw, expectedKey) || out != -2) return 8;
    std::puts("PASS: synthetic manifest vtables cover missing/malformed/nonfinite/range/fractional keys");
}
