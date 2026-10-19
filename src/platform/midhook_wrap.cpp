// The Wrap placement's entry (ARCHITECTURE.md section 15 "Wrap placement"): the callee runs as a subroutine on copies
// of its stack arguments between an optional before and an after callback, so both see the caller's own arguments.
#include "platform/midhook.h"
#include "platform/midhook_emit.h"
#include <initializer_list>

namespace {
// A bounded byte writer: a write past the capacity only sets `failed`.
struct Writer {
    uint8_t* out;
    size_t capacity;
    uintptr_t va;
    size_t n = 0;
    bool failed = false;

    bool Room(size_t k) {
        failed = failed || n + k > capacity;

        return !failed;
    }

    void Bytes(std::initializer_list<uint8_t> bytes) {
        if (!Room(bytes.size())) {
            return;
        }

        for (const uint8_t b : bytes) {
            out[n++] = b;
        }
    }

    void Dword(uint32_t v) {
        if (Room(4)) {
            Rs7MidEmit::Dword(out + n, v);
            n += 4;
        }
    }

    uintptr_t Here() const { return va + n; }

    // lea esp, [esp + delta]: moves ESP without touching the flags (nothing for 0).
    void LeaEsp(int32_t delta) {
        if (delta >= -128 && delta <= 127) {
            if (delta) {
                Bytes({0x8d, 0x64, 0x24, uint8_t(delta)});
            }

            return;
        }

        Bytes({0x8d, 0xa4, 0x24});
        Dword(uint32_t(delta));
    }

    // push dword [esp + disp]
    void PushStackDword(uint32_t disp) {
        if (disp <= 127) {
            Bytes({0xff, 0x74, 0x24, uint8_t(disp)});
            return;
        }

        Bytes({0xff, 0xb4, 0x24});
        Dword(disp);
    }

    // call / jmp rel32 (op E8 / E9)
    void Rel32(uint8_t op, uintptr_t target) {
        Bytes({op});
        Dword(uint32_t(target - (Here() + 4)));
    }

    // push next; push callback; jmp thunk: the callback runs with `jump` = the byte after the 15-byte entry.
    void Callback(uintptr_t callback, void (*thunk)()) {
        if (callback && Room(15)) {
            Rs7MidEmit::Entry(out + n, Here(), Here() + 15, callback, thunk);
            n += 15;
        }
    }
};
}  // namespace

extern "C" size_t __cdecl Rs7Mid_BuildWrap(uint8_t* out, size_t capacity, uintptr_t destVa, uintptr_t callee,
                                           const uint8_t* source, uintptr_t sourceVa, size_t length, uintptr_t before,
                                           uintptr_t after, uint32_t args, uint32_t popBytes, uint32_t locals,
                                           int mode) {
    const bool layoutOk = args <= Rs7Mid::kWrapMaxArgs && locals <= Rs7Mid::kWrapMaxLocals && locals % 4 == 0 &&
                          (popBytes == 0 || popBytes == 4 * args);
    void (*const thunk)() = Rs7MidEmit::ThunkFor(mode);
    if (!out || !layoutOk || (!before && !after) || (!callee && !source) || !thunk) {
        return 0;
    }

    Writer w{out, capacity, destVa};

    w.Bytes({0x52, 0x51, 0x50, 0x6a, 0x00});  // push edx; push ecx; push eax; push 0 (Rs7Mid::WrapFrame)
    w.LeaEsp(-int32_t(locals));
    w.Callback(before, thunk);

    // The copies, last argument first: every push moves ESP by 4, so the same displacement reaches the next one.
    for (uint32_t i = 0; i < args; ++i) {
        w.PushStackDword(locals + 16 + 4 * args);
    }

    const size_t call = w.n;

    w.Rel32(0xe8, callee);  // a function entry's replay is patched in below

    if (!popBytes) {
        w.LeaEsp(int32_t(4 * args));
    }

    w.Callback(after, thunk);
    w.LeaEsp(int32_t(locals + 16));

    if (popBytes) {
        w.Bytes({0xc2, uint8_t(popBytes), uint8_t(popBytes >> 8)});
    } else {
        w.Bytes({0xc3});
    }

    if (w.failed || callee) {
        return w.failed ? 0 : w.n;
    }

    // A function entry: the callee is the relocated prologue and a jump to the rest of the function.
    const uintptr_t replay = w.Here();
    const size_t moved =
        capacity < w.n + 5 ? 0 : Rs7Mid_Relocate(out + w.n, capacity - w.n - 5, source, sourceVa, length, replay);
    if (!moved) {
        return 0;
    }

    w.n += moved;
    w.Rel32(0xe9, sourceVa + length);
    Rs7MidEmit::Dword(out + call + 1, uint32_t(replay - (destVa + call + 5)));

    return w.failed ? 0 : w.n;
}
