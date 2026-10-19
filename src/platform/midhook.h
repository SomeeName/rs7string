#pragma once
#include <cstddef>
#include <cstdint>

extern "C" {
void __cdecl Rs7Mid_Fldcw(uint32_t fcw);  // midhook_thunks.asm: fldcw (Rs7Mid::GameFcw)
}

namespace Rs7Mid {
// PUSHAD order. ESP is the site's original ESP (not PUSHAD's saved ESP).
// jump defaults to the relocated replay (a call site: Call); assigning another address skips replay.
// fpu is thunk-owned (read-only for callbacks): in full mode the 512-byte FXSAVE image of the game's floating-point
// state that the thunk restores after the callback (the St0 accessors below edit it), 0 in light mode, the SseResult
// block with bit 0 set in sse mode (LoadSt0 below). Callbacks must not throw.
struct Context {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax, eflags;
    uint32_t fpu, jump;

    // The game's x87 stack at the site (full mode only; light and sse mode have no image: FpuDepth 0, every edit
    // fails).
    // Values go through double: a read rounds the 80-bit ST0 to double (round to nearest), a write is exact; a
    // signalling NaN comes back quiet. The edits keep TOP and the tag word consistent, so FXRSTOR hands the game a
    // valid stack.
    unsigned FpuDepth() const;                // valid x87 registers (0..8)
    bool HasSt0() const;                      // ST0 holds a value (the stack is not empty)
    double St0() const;                       // ST0, a quiet NaN when !HasSt0()
    [[nodiscard]] bool SetSt0(double value);  // replace ST0 (depth unchanged); false when !HasSt0()
    [[nodiscard]] bool PushSt0(
        double value);  // what `fld` does: TOP - 1, the value in ST0, C1 = 0; false when all 8 are valid
    // What `fcom` of ST0 with `rhs` finds (exact, no rounding of the 80-bit ST0): -1 below, 0 equal, 1 above, 2
    // unordered (a NaN or an unsupported operand, or no ST0).
    int CompareSt0(float rhs) const;

    // Emulate x86 CMP lhs,rhs (operand width 8, 16 or 32 bits) without losing non-arithmetic flags
    // (including DF). Useful when replacing a stub whose own comparisons remain observable.
    void Compare(uint32_t lhs, uint32_t rhs, unsigned bits) {
        const uint32_t sign = 1u << (bits - 1);
        const uint32_t mask = bits == 32 ? 0xffffffffu : (1u << bits) - 1;

        lhs &= mask;
        rhs &= mask;
        const uint32_t result = (lhs - rhs) & mask;
        const uint32_t arithmetic = (lhs < rhs ? 1u : 0u) | ParityFlag(result) | ((lhs ^ rhs ^ result) & 0x10) |
                                    (result == 0 ? 0x40u : 0u) | (result & sign ? 0x80u : 0u) |
                                    (((lhs ^ rhs) & (lhs ^ result)) & sign ? 0x800u : 0u);

        eflags = (eflags & ~0x8d5u) | arithmetic;
    }

    void Compare32(uint32_t lhs, uint32_t rhs) { Compare(lhs, rhs, 32); }

    // Emulate x86 TEST lhs,rhs (8, 16 or 32 bits): CF, OF and AF cleared, PF/ZF/SF of lhs & rhs, other flags kept.
    void Test(uint32_t lhs, uint32_t rhs, unsigned bits) {
        const uint32_t mask = bits == 32 ? 0xffffffffu : (1u << bits) - 1;
        const uint32_t result = lhs & rhs & mask;
        const uint32_t arithmetic =
            ParityFlag(result) | (result == 0 ? 0x40u : 0u) | (result & (1u << (bits - 1)) ? 0x80u : 0u);

        eflags = (eflags & ~0x8d5u) | arithmetic;
    }

    // Emulate x86 ADD lhs,rhs (32 bits): returns the sum and sets its arithmetic flags, other flags kept. For a
    // callback that skips a displaced `add` whose flags stay observable.
    uint32_t Add32(uint32_t lhs, uint32_t rhs) {
        const uint32_t result = lhs + rhs;
        const uint32_t arithmetic = (result < lhs ? 1u : 0u) | ParityFlag(result) | ((lhs ^ rhs ^ result) & 0x10) |
                                    (result == 0 ? 0x40u : 0u) | (result & 0x80000000u ? 0x80u : 0u) |
                                    ((lhs ^ result) & (rhs ^ result) & 0x80000000u ? 0x800u : 0u);

        eflags = (eflags & ~0x8d5u) | arithmetic;

        return result;
    }

    // PF (0x4) of a result: set when its low byte has an even number of one bits.
    static uint32_t ParityFlag(uint32_t result) {
        uint32_t parity = result & 0xff;

        parity ^= parity >> 4;
        parity ^= parity >> 2;
        parity ^= parity >> 1;

        return parity & 1 ? 0u : 4u;
    }
};

using Callback = void(__cdecl*)(Context&);

// Full: the thunk saves and restores x87/MMX/SSE/MXCSR (FXSAVE/FXRSTOR) and runs the callback after FNINIT; any C++
// works, and Context::St0 / SetSt0 / PushSt0 read and edit the saved x87 stack. Light: no floating-point save (about 25
// ns instead of 150 ns per hit, ARCHITECTURE.md section 15), for hot sites. A light callback, and everything it calls,
// must execute no x87, MMX or SSE instruction: no float/double, no CRT math, no memcpy/memset or struct copies the
// compiler may vectorise (/arch:SSE2 emits MOVUPS/MOVQ for those). tools/tests/test_midhook_emu.py checks every
// callback listed in LIGHT_CALLBACKS for such instructions. Sse: the light thunk plus XMM0-7 and MXCSR (the callback
// runs with MXCSR 1F80h), for hot callbacks with float math. SSE is allowed, x87 and MMX are not (the game's x87
// state is live): no function returning float/double that is not inlined (the x86 ABI returns those in ST0), no CRT
// math; an x87 result goes through LoadSt0. SSE_CALLBACKS lists them for the same check.
enum class Mode : uint8_t { Full, Light, Sse };

// Sse mode: the block Context::fpu points at (bit 0 set). The thunk loads `value` with `fld dword` after the
// callback when `load` is nonzero.
struct SseResult {
    float value;
    uint32_t load;
};

// Sse mode: what a displaced `fld dword [..]` of `value` does (ST0 = value, TOP - 1), done by the thunk after the
// callback. One load per hit; false in another mode. An empty slot is not checked: like the game's own `fld`, a push
// onto a full stack is a stack fault (masked: the indefinite NaN).
[[nodiscard]] inline bool LoadSt0(Context& c, float value) {
    if (!(c.fpu & 1)) {
        return false;
    }

    auto* result = reinterpret_cast<SseResult*>(uintptr_t(c.fpu & ~1u));

    result->value = value;
    result->load = 1;

    return true;
}

// The dword at a game address (a stack slot or a field), as the replaced stubs' `push dword ptr [..]` read it.
inline uint32_t Dword(uint32_t address) {
    return *reinterpret_cast<const uint32_t*>(address);
}

// The byte at a game address (a `movzx r,byte [..]` / `cmp byte [..]` operand).
inline uint8_t Byte(uint32_t address) {
    return *reinterpret_cast<const uint8_t*>(address);
}

// A register or dword value as the game object pointer it holds (no read).
template <class T = uint8_t>
inline T* Ptr(uint32_t value) {
    return reinterpret_cast<T*>(uintptr_t(value));
}

// What `push value` does to the saved context (the thunk writes the edited stack back). For callbacks that skip the
// replay (`jump = resume`) because they push a computed value instead of the stock immediate / register.
inline void Push(Context& c, uint32_t value) {
    c.esp -= 4;
    *reinterpret_cast<uint32_t*>(c.esp) = value;
}

// Call sites (PrepareCall): Before runs the callback at the callee's entry (`jump` = the callee, `esp` at the game's
// return address); After runs the callee first, from the entry, and the callback on its result (`jump` = site + 5).
// Wrap (PrepareWrap) runs the callee on copies of its arguments between an optional before and an after callback
// that both see the original arguments (WrapFrame below).
enum class Call : uint8_t { Before, After, Wrap };

// What `ret N` does (N = argBytes): continue at the return address on the stack and pop it and N bytes. For a callback
// that does a displaced epilogue's work itself.
inline void Ret(Context& c, uint32_t argBytes = 0) {
    c.jump = Dword(c.esp);
    c.esp += 4 + argBytes;
}

// Before placement: skip the callee as if it had returned `value` in EAX; `argBytes` is a `ret N` callee's N.
inline void Return(Context& c, uint32_t value, uint32_t argBytes = 0) {
    c.eax = value;
    Ret(c, argBytes);
}

// Who removes a wrapped callee's stack arguments: the callee's `ret 4 * args` (stdcall; a thiscall callee cleans up the
// same way, ECX is a register) or the caller (cdecl).
enum class Conv : uint8_t { Stdcall, Cdecl };

// A Wrap site's frame, declared once per site (its hook row and its callbacks use the same constant): the stack
// argument dwords the entry copies for the callee, their cleanup and the bytes of scratch below the frame.
struct Wrap {
    uint8_t args = 0;
    Conv conv = Conv::Stdcall;
    uint16_t locals = 0;  // a multiple of 4, at most kWrapMaxLocals

    constexpr uint32_t PopBytes() const { return conv == Conv::Cdecl ? 0u : 4u * args; }
};

constexpr unsigned kWrapMaxArgs = 16, kWrapMaxLocals = 256;

// What both Wrap callbacks find at Context::esp + Wrap::locals (ARCHITECTURE.md section 15 "Wrap placement"): `user`
// (0 at the call, free for the before callback to hand a value to the after callback), the registers the game called
// with, its return address and above it the caller's own argument slots (the callee ran on copies).
struct WrapFrame {
    uint32_t user, eax, ecx, edx, ret;

    uint32_t& Arg(unsigned i) { return *reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(&ret) + 4 + 4 * i); }
};

inline WrapFrame& Frame(const Context& c, const Wrap& w) {
    return *Ptr<WrapFrame>(c.esp + w.locals);
}

// The site's scratch (Wrap::locals bytes, uninitialised): storage for arguments a before callback builds.
inline uint8_t* Locals(const Context& c) {
    return Ptr(c.esp);
}

// Before callback of a Wrap site: skip the callee as if it had returned `value` in EAX (frame, return address and the
// callee's cleanup popped, continue at the game's return address).
inline void WrapReturn(Context& c, const Wrap& w, uint32_t value) {
    c.eax = value;
    c.esp += w.locals + 16;
    Ret(c, w.PopBytes());
}

static_assert(sizeof(WrapFrame) == 20, "wrap entry ABI");
static_assert(sizeof(Context) == 44 && offsetof(Context, jump) == 40, "thunk ABI");
static_assert(offsetof(Context, esp) == 12 && offsetof(Context, eflags) == 32 && offsetof(Context, fpu) == 36,
              "thunk register/flags ABI");

// The x87 control word the full thunk runs callbacks with (FNINIT: 64-bit precision, round to nearest, all masked).
constexpr uint16_t kThunkFcw = 0x37F;

// A full-mode callback that calls game code: for the scope, the game's own x87 control word (precision, rounding) from
// the saved image (FXSAVE +0), then the thunk's again. Light / Sse mode (no image): nothing. Game code must not run
// under the thunk's FCW (the D3D9 device sets 24-bit precision; results would differ from stock).
class GameFcw {
public:
    explicit GameFcw(const Context& c) : saved_(c.fpu && !(c.fpu & 1)) {
        if (saved_) {
            Rs7Mid_Fldcw(*reinterpret_cast<const uint16_t*>(uintptr_t(c.fpu)));
        }
    }

    ~GameFcw() {
        if (saved_) {
            Rs7Mid_Fldcw(kThunkFcw);
        }
    }

    GameFcw(const GameFcw&) = delete;
    GameFcw& operator=(const GameFcw&) = delete;

private:
    bool saved_;
};

// Owns executable storage, not a live patch. Prepare before writing any site.
// Destroy only after restoring the site AND quiescing every thread in the hook.
class Hook {
public:
    Hook() = default;
    ~Hook();
    Hook(const Hook&) = delete;
    Hook& operator=(const Hook&) = delete;
    bool Prepare(uintptr_t site, const uint8_t* bytes, size_t length, Callback callback, Mode mode = Mode::Full);
    // A `call rel32` site: bytes = its five original bytes (E8 rel32, the callee is decoded from them).
    bool PrepareCall(uintptr_t site, const uint8_t* bytes, size_t length, Callback callback, Mode mode = Mode::Full,
                     Call placement = Call::Before);
    // A Wrap site: a `call rel32` (callSite, bytes = its five bytes) or the entry of a function (bytes = whole
    // instructions of its prologue, replayed, then the rest of the function). At least one callback.
    bool PrepareWrap(uintptr_t site, const uint8_t* bytes, size_t length, bool callSite, const Wrap& wrap,
                     Callback before, Callback after, Mode mode = Mode::Full);

    // A call-in entry (CallIn below): no site, the continuation is `ret argBytes`.
    bool PrepareCallIn(Callback callback, Mode mode, uint16_t argBytes);

    uintptr_t Entry() const { return reinterpret_cast<uintptr_t>(code_); }

private:
    template <class Build>
    bool Emit(Build build);  // allocates, builds (Build(code) -> bytes, 0 = rejected) and seals code_

    void* code_ = nullptr;
};

// A call written into relocated game code (a function copy, platform/fncopy.h): `call Entry()` runs the callback with
// the copy's registers and flags, `esp` at the return address, `jump` = a `ret argBytes` after the entry. A callback
// that leaves `jump` returns to the copy with the edited Context; one that assigns it continues there instead (a tail
// jump into a game function with the copy's stack). Built on the first Entry() (startup, before any copy runs).
class CallIn {
public:
    constexpr CallIn(Callback callback, Mode mode, uint16_t argBytes = 0)
        : callback_(callback), mode_(mode), argBytes_(argBytes) {}

    CallIn(const CallIn&) = delete;
    CallIn& operator=(const CallIn&) = delete;
    uintptr_t Entry();  // 0 when the entry cannot be built

private:
    Callback callback_;
    Mode mode_;
    uint16_t argBytes_;
    Hook hook_;
};
}  // namespace Rs7Mid

// Pure bounded emitters, also executed directly by the emulator tests. Return
// bytes emitted or 0 on rejection. Source span must be whole instructions, 5..16
// bytes. Relocate output may be partially written on failure; never execute it.
extern "C" __declspec(dllexport) size_t __cdecl Rs7Mid_Relocate(uint8_t* out, size_t capacity, const uint8_t* source,
                                                                uintptr_t sourceVa, size_t length, uintptr_t destVa);
extern "C" __declspec(dllexport) size_t __cdecl Rs7Mid_Build(uint8_t* out, size_t capacity, const uint8_t* source,
                                                             uintptr_t sourceVa, size_t length, uintptr_t destVa,
                                                             uintptr_t callback);
// Same contract as Rs7Mid_Build, entry jumps to the light thunk (Mode::Light).
extern "C" __declspec(dllexport) size_t __cdecl Rs7Mid_BuildLight(uint8_t* out, size_t capacity, const uint8_t* source,
                                                                  uintptr_t sourceVa, size_t length, uintptr_t destVa,
                                                                  uintptr_t callback);
// Same contract as Rs7Mid_Build, entry jumps to the SSE thunk (Mode::Sse).
extern "C" __declspec(dllexport) size_t __cdecl Rs7Mid_BuildSse(uint8_t* out, size_t capacity, const uint8_t* source,
                                                                uintptr_t sourceVa, size_t length, uintptr_t destVa,
                                                                uintptr_t callback);
// The entry of a call site (no relocation): mode is the thunk (0 full, 1 light, 2 sse: Rs7Mid::Mode), after != 0 the
// After placement.
// sourceVa is the call instruction, callee its original target. Returns bytes emitted (15 / 24) or 0.
extern "C" __declspec(dllexport) size_t __cdecl Rs7Mid_BuildCall(uint8_t* out, size_t capacity, uintptr_t sourceVa,
                                                                 uintptr_t callee, uintptr_t destVa, uintptr_t callback,
                                                                 int mode, int after);
// A call-in entry (Rs7Mid::CallIn; mode: an Rs7Mid::Mode): the 15-byte entry with a `ret` (argBytes 0) or `ret
// argBytes` continuation after it. Returns bytes emitted (16 / 18) or 0 (no callback, capacity, argBytes not a multiple
// of 4 below 10000h).
extern "C" __declspec(dllexport) size_t __cdecl Rs7Mid_BuildCallIn(uint8_t* out, size_t capacity, uintptr_t destVa,
                                                                   uintptr_t callback, int mode, uint32_t argBytes);
// The entry of a Wrap site: callee != 0 is a call site's callee; callee == 0 relocates `length` bytes at `source`
// (sourceVa, a function entry) as the callee. popBytes is 0 or 4 * args; mode as in Rs7Mid_BuildCall. Returns
// bytes emitted or 0 (bad layout, no callback, rejected prologue, capacity).
extern "C" __declspec(dllexport) size_t __cdecl Rs7Mid_BuildWrap(uint8_t* out, size_t capacity, uintptr_t destVa,
                                                                 uintptr_t callee, const uint8_t* source,
                                                                 uintptr_t sourceVa, size_t length, uintptr_t before,
                                                                 uintptr_t after, uint32_t args, uint32_t popBytes,
                                                                 uint32_t locals, int mode);
// The shared thunks (midhook_thunks.asm, exported there): never called from C++, only jumped to by an entry.
extern "C" void Rs7Mid_Thunk();
extern "C" void Rs7Mid_ThunkLight();
extern "C" void Rs7Mid_ThunkSse();
