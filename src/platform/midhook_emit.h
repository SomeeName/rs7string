#pragma once
// Byte emitters shared by the mid-hook entry builders (midhook.cpp, midhook_wrap.cpp). Not part of the hook API.
#include <cstddef>
#include <cstdint>

extern "C" void Rs7Mid_Thunk();
extern "C" void Rs7Mid_ThunkLight();
extern "C" void Rs7Mid_ThunkSse();

namespace Rs7MidEmit {
inline void Dword(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        p[i] = uint8_t(v >> (i * 8));
    }
}

// The thunk of a builder's mode argument (0 full, 1 light, 2 sse: Rs7Mid::Mode); nullptr for any other value.
inline void (*ThunkFor(int mode))() {
    return mode == 0 ? &Rs7Mid_Thunk : mode == 1 ? &Rs7Mid_ThunkLight : mode == 2 ? &Rs7Mid_ThunkSse : nullptr;
}

// The 15-byte entry every mid-hook starts with: push continuation; push callback; jmp thunk.
inline void Entry(uint8_t* out, uintptr_t va, uintptr_t continuation, uintptr_t callback, void (*thunk)()) {
    out[0] = 0x68;
    Dword(out + 1, uint32_t(continuation));
    out[5] = 0x68;
    Dword(out + 6, uint32_t(callback));
    out[10] = 0xe9;
    Dword(out + 11, uint32_t(uintptr_t(thunk) - (va + 15)));
}
}  // namespace Rs7MidEmit
