#include "platform/midhook.h"
#include "platform/midhook_emit.h"
#include <windows.h>
#include <array>
#include <cstring>

namespace {
struct Instruction {
    size_t input = 0, output = 0, size = 0, emitted = 0, rel = 0;
    int relSize = 0;
    uint8_t op = 0, condition = 0;
    bool ret = false;
};

using Rs7MidEmit::Dword;

uint32_t ReadDword(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// Operand form of a whitelisted opcode. regs: the ModRM.reg values (bit mask) the form allows; 0 = rejected.
// ImmV is an operand-sized immediate (2 bytes after a 66 prefix), Moffs a 4-byte address (never resized).
enum Flag : uint8_t { Modrm = 1, Imm8 = 2, ImmV = 4, Moffs = 8, MemOnly = 16, No66 = 32 };

struct Form {
    uint8_t flags = 0, regs = 0;
};

constexpr Form Op(uint8_t flags = 0, uint8_t regs = 0xff) {
    return {flags, regs};
}

// One-byte opcodes. Relative branches and RET are DecodeTransfer's; everything not listed is rejected.
constexpr std::array<Form, 256> OneByteForms() {
    std::array<Form, 256> t{};

    for (int op = 0; op <= 0x3d; ++op) {
        const int column = op & 7;  // ALU r/m,reg (0-3), AL,imm8 (4), eAX,imm (5)

        if (column <= 5) {
            t[op] = column <= 3 ? Op(Modrm) : Op(column == 4 ? Imm8 : ImmV);
        }
    }

    for (int op = 0x40; op <= 0x5f; ++op) {
        t[op] = Op();  // INC/DEC/PUSH/POP registers
    }

    for (int op = 0xb0; op <= 0xbf; ++op) {
        t[op] = Op(op < 0xb8 ? Imm8 : ImmV);  // MOV reg,imm
    }

    for (int op = 0x84; op <= 0x8b; ++op) {
        t[op] = Op(Modrm);  // TEST, XCHG, MOV r/m
    }

    t[0x8d] = Op(Modrm | MemOnly);         // LEA
    t[0x90] = Op();                        // NOP
    t[0x68] = Op(ImmV);                    // PUSH imm
    t[0x6a] = Op(Imm8);                    // PUSH imm8
    t[0x69] = Op(Modrm | ImmV);            // IMUL r,r/m,imm
    t[0x6b] = Op(Modrm | Imm8);            // IMUL r,r/m,imm8
    t[0x80] = t[0x83] = Op(Modrm | Imm8);  // ALU r/m,imm8
    t[0x81] = Op(Modrm | ImmV);            // ALU r/m,imm
    t[0xa1] = t[0xa3] = Op(Moffs);         // MOV eAX,[moffs] / [moffs],eAX
    t[0xa8] = Op(Imm8);                    // TEST AL,imm8
    t[0xa9] = Op(ImmV);                    // TEST eAX,imm
    t[0xc6] = Op(Modrm | Imm8, 1);         // MOV r/m8,imm8 (/0)
    t[0xc7] = Op(Modrm | ImmV, 1);         // MOV r/m,imm (/0)
    t[0xf6] = Op(Modrm | Imm8, 1);         // TEST r/m8,imm8 (/0)
    t[0xf7] = Op(Modrm | ImmV, 1);         // TEST r/m,imm (/0)
    t[0xfe] = Op(Modrm, 3);                // INC/DEC r/m8 (/0 /1)
    // x87 D9 memory forms FLD(0) FST(2) FSTP(3) FLDCW(5) FNSTCW(7) (CMUV 00518BC0 stores with FST). Register forms,
    // FLDENV/FNSTENV(4,6) and the reserved /1 stay rejected.
    t[0xd9] = Op(Modrm | MemOnly | No66, 0xad);
    // x87 D8 memory form FCOMP m32 (/3; W 005B2DDE compares with a constant): a memory operand is position
    // independent in 32-bit code. The other D8 forms stay rejected.
    t[0xd8] = Op(Modrm | MemOnly | No66, 0x08);

    return t;
}

constexpr std::array<Form, 256> kOneByte = OneByteForms();

// 0F xx: MOVZX/MOVSX, SETcc (/0, byte operand) and IMUL r,r/m. Jcc rel32 is DecodeTransfer's.
constexpr Form TwoByteForm(uint8_t op) {
    if (op == 0xb6 || op == 0xb7 || op == 0xbe || op == 0xbf || op == 0xaf) {
        return Op(Modrm);
    }

    return op >= 0x90 && op <= 0x9f ? Op(Modrm | No66, 1) : Form{};
}

// Relative CALL/JMP/Jcc and RET, without prefixes; false when the bytes are none of them (or cut).
bool DecodeTransfer(const uint8_t* p, size_t n, Instruction& d) {
    const uint8_t op = p[0];
    const bool shortJcc = op >= 0x70 && op <= 0x7f;
    size_t rel = 1, relSize = 1, emitted = shortJcc ? 6 : 5;

    if (op == 0xc3 || op == 0xc2) {
        if (op == 0xc2 && n < 3) {
            return false;
        }

        d.op = op;
        d.ret = true;  // RET / RET imm16: position independent; only valid as the LAST stolen instruction
        d.size = d.emitted = op == 0xc2 ? 3 : 1;
        return true;
    }

    if (op == 0xe8 || op == 0xe9) {
        relSize = 4;
    } else if (op == 0x0f && n >= 2 && (p[1] & 0xf0) == 0x80) {
        rel = 2;
        relSize = 4;
        emitted = 6;
    } else if (op != 0xeb && !shortJcc) {
        return false;
    }

    if (rel + relSize > n) {
        return false;
    }

    d.op = op;
    d.rel = rel;
    d.relSize = int(relSize);
    d.condition = p[rel - 1] & 15;
    d.size = rel + relSize;
    d.emitted = emitted;

    return true;
}

// Length of ModRM + SIB + displacement, 0 when the form forbids this ModRM or the bytes are cut.
size_t ModrmLength(const uint8_t* p, size_t n, Form form) {
    if (!n) {
        return 0;
    }

    const uint8_t m = p[0], mod = m >> 6, rm = m & 7;
    size_t length = 1;

    if (!((form.regs >> ((m >> 3) & 7)) & 1) || (mod == 3 && (form.flags & MemOnly))) {
        return 0;
    }

    if (mod != 3 && rm == 4) {
        if (n < 2) {
            return 0;
        }

        length += (mod == 0 && (p[1] & 7) == 5) ? 5 : 1;  // SIB, disp32 without base
    }

    if (mod == 0 && rm == 5) {
        length += 4;  // absolute disp32: no RIP-relative form in 32-bit code
    }

    return length + (mod == 1 ? 1 : mod == 2 ? 4 : 0);
}

// Deliberate whitelist, not a general disassembler: the forms above, optionally behind one operand-size prefix 66
// (not on transfers, x87 or SETcc). No other prefix, LOOP/JCXZ, far or indirect transfers, environment-save or
// EIP-observing instructions.
bool Decode(const uint8_t* p, size_t n, Instruction& d) {
    if (!n) {
        return false;
    }
    if (DecodeTransfer(p, n, d)) {
        return true;
    }

    const bool osz = p[0] == 0x66;
    size_t pos = osz ? 1 : 0;

    if (pos >= n) {
        return false;
    }

    const uint8_t op = p[pos++];
    Form form = kOneByte[op];

    if (op == 0x0f) {
        if (pos >= n) {
            return false;
        }

        form = TwoByteForm(p[pos++]);
    }

    if (!form.regs || (osz && (form.flags & No66))) {
        return false;
    }

    if (form.flags & Modrm) {
        const size_t length = ModrmLength(p + pos, n - pos, form);
        if (!length) {
            return false;
        }

        pos += length;
    }

    pos += (form.flags & Imm8) ? 1 : (form.flags & ImmV) ? (osz ? 2 : 4) : (form.flags & Moffs) ? 4 : 0;
    if (pos > n || pos > 15) {
        return false;
    }

    d.op = op;
    d.size = d.emitted = pos;

    return true;
}
}  // namespace

// endsInRet: the last stolen instruction is a RET, so control never falls through to sourceVa+length.
static size_t RelocateImpl(uint8_t* out, size_t capacity, const uint8_t* source, uintptr_t sourceVa, size_t length,
                           uintptr_t destVa, bool* endsInRet) {
    if (endsInRet) {
        *endsInRet = false;
    }
    if (!out || !source || length < 5 || length > 16) {
        return 0;
    }

    Instruction insns[16];
    size_t count = 0, input = 0, output = 0;

    while (input < length) {
        auto& d = insns[count++];

        d.input = input;
        d.output = output;
        if (!Decode(source + input, length - input, d)) {
            return 0;
        }
        if (d.ret && input + d.size != length) {
            return 0;  // code after a RET in the span is dead / not ours
        }

        input += d.size;
        output += d.emitted;
    }

    if (output > capacity) {
        return 0;
    }

    if (endsInRet) {
        *endsInRet = insns[count - 1].ret;
    }

    for (size_t i = 0; i < count; ++i) {
        const auto& d = insns[i];
        const auto* p = source + d.input;
        auto* q = out + d.output;

        if (!d.relSize) {
            for (size_t j = 0; j < d.size; ++j) {
                q[j] = p[j];
            }

            continue;
        }

        const int32_t rel = d.relSize == 1 ? int8_t(p[d.rel]) : int32_t(ReadDword(p + d.rel));
        uintptr_t target = sourceVa + d.input + d.size + rel;
        // Internal CALLs can materialize EIP through a non-adjacent POP too.
        // This small relocator cannot prove their return-address use safe.
        if (d.op == 0xe8 &&
            (target == sourceVa + d.input + d.size || (target >= sourceVa && target < sourceVa + length))) {
            return 0;
        }

        if (target >= sourceVa && target < sourceVa + length) {
            bool found = false;

            for (size_t j = 0; j < count; ++j) {
                if (target == sourceVa + insns[j].input) {
                    target = destVa + insns[j].output;
                    found = true;
                    break;
                }
            }

            if (!found) {
                return 0;  // branch to middle of stolen instruction
            }
        }

        const bool jcc = d.op == 0x0f || (d.op >= 0x70 && d.op <= 0x7f);

        if (jcc) {
            q[0] = 0x0f;
            q[1] = uint8_t(0x80 | d.condition);
        } else {
            q[0] = d.op == 0xe8 ? 0xe8 : 0xe9;
        }

        Dword(q + (jcc ? 2 : 1), uint32_t(target - (destVa + d.output + d.emitted)));
    }

    return output;
}

extern "C" size_t __cdecl Rs7Mid_Relocate(uint8_t* out, size_t capacity, const uint8_t* source, uintptr_t sourceVa,
                                          size_t length, uintptr_t destVa) {
    return RelocateImpl(out, capacity, source, sourceVa, length, destVa, nullptr);
}

using Rs7MidEmit::Entry;

static size_t BuildImpl(uint8_t* out, size_t capacity, const uint8_t* source, uintptr_t sourceVa, size_t length,
                        uintptr_t destVa, uintptr_t callback, void (*thunk)()) {
    constexpr size_t entry = 15;
    if (!out || !callback || capacity < entry + 5) {
        return 0;
    }

    bool endsInRet = false;
    const size_t replay =
        RelocateImpl(out + entry, capacity - entry - 5, source, sourceVa, length, destVa + entry, &endsInRet);
    if (!replay) {
        return 0;
    }

    Entry(out, destVa, destVa + entry, callback, thunk);
    // A replayed RET leaves the thunk code for good: no jump back (it would be dead bytes).
    if (endsInRet) {
        return entry + replay;
    }

    out[entry + replay] = 0xe9;
    Dword(out + entry + replay + 1, uint32_t(sourceVa + length - (destVa + entry + replay + 5)));

    return entry + replay + 5;
}

extern "C" size_t __cdecl Rs7Mid_Build(uint8_t* out, size_t capacity, const uint8_t* source, uintptr_t sourceVa,
                                       size_t length, uintptr_t destVa, uintptr_t callback) {
    return BuildImpl(out, capacity, source, sourceVa, length, destVa, callback, &Rs7Mid_Thunk);
}

extern "C" size_t __cdecl Rs7Mid_BuildLight(uint8_t* out, size_t capacity, const uint8_t* source, uintptr_t sourceVa,
                                            size_t length, uintptr_t destVa, uintptr_t callback) {
    return BuildImpl(out, capacity, source, sourceVa, length, destVa, callback, &Rs7Mid_ThunkLight);
}

extern "C" size_t __cdecl Rs7Mid_BuildSse(uint8_t* out, size_t capacity, const uint8_t* source, uintptr_t sourceVa,
                                          size_t length, uintptr_t destVa, uintptr_t callback) {
    return BuildImpl(out, capacity, source, sourceVa, length, destVa, callback, &Rs7Mid_ThunkSse);
}

// Call site entry (ARCHITECTURE.md section 15, "Call sites"). Before: the game's `call` lands on the entry, the
// continuation is the callee. After: the entry calls the callee itself and continues at the site's return address.
extern "C" size_t __cdecl Rs7Mid_BuildCall(uint8_t* out, size_t capacity, uintptr_t sourceVa, uintptr_t callee,
                                           uintptr_t destVa, uintptr_t callback, int mode, int after) {
    const size_t call = after ? 9 : 0;
    void (*const thunk)() = Rs7MidEmit::ThunkFor(mode);
    if (!out || !callee || !callback || !thunk || capacity < call + 15) {
        return 0;
    }

    // After: drop the game's return address (site + 5, pushed again as the continuation) without touching the flags,
    // then call the callee with the game's arguments directly above its return address.
    if (after) {
        const uint8_t dropReturn[] = {0x8d, 0x64, 0x24, 0x04};  // lea esp, [esp+4]

        for (size_t i = 0; i < sizeof dropReturn; ++i) {
            out[i] = dropReturn[i];
        }

        out[4] = 0xe8;
        Dword(out + 5, uint32_t(callee - (destVa + 9)));
    }

    Entry(out + call, destVa + call, after ? sourceVa + 5 : callee, callback, thunk);

    return call + 15;
}

// Call-in entry (ARCHITECTURE.md section 15, "Call-in entries"): the call entry with no callee, its continuation the
// `ret` / `ret N` right after it.
extern "C" size_t __cdecl Rs7Mid_BuildCallIn(uint8_t* out, size_t capacity, uintptr_t destVa, uintptr_t callback,
                                             int mode, uint32_t argBytes) {
    const size_t ret = argBytes ? 3 : 1;
    if (capacity < 15 + ret || argBytes > 0xfffc || argBytes % 4 ||
        !Rs7Mid_BuildCall(out, capacity, 0, destVa + 15, destVa, callback, mode, 0)) {
        return 0;
    }

    out[15] = argBytes ? 0xc2 : 0xc3;

    if (argBytes) {
        out[16] = uint8_t(argBytes);
        out[17] = uint8_t(argBytes >> 8);
    }

    return 15 + ret;
}

// Rs7Mid_Thunk, Rs7Mid_ThunkLight and Rs7Mid_ThunkSse, the shared thunks every entry above jumps to, are in
// midhook_thunks.asm.

Rs7Mid::Hook::~Hook() {
    if (code_) {
        VirtualFree(code_, 0, MEM_RELEASE);
    }
}

template <class Build>
bool Rs7Mid::Hook::Emit(Build build) {
    if (code_) {
        return false;
    }

    auto* code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 256, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!code) {
        return false;
    }

    const size_t n = build(code);
    DWORD old;

    if (!n || !VirtualProtect(code, 256, PAGE_EXECUTE_READ, &old)) {
        VirtualFree(code, 0, MEM_RELEASE);
        return false;
    }

    FlushInstructionCache(GetCurrentProcess(), code, n);
    code_ = code;

    return true;
}

bool Rs7Mid::Hook::Prepare(uintptr_t site, const uint8_t* bytes, size_t length, Callback callback, Mode mode) {
    const auto build = mode == Mode::Light ? &Rs7Mid_BuildLight : mode == Mode::Sse ? &Rs7Mid_BuildSse : &Rs7Mid_Build;

    return Emit(
        [&](uint8_t* code) { return build(code, 256, bytes, site, length, uintptr_t(code), uintptr_t(callback)); });
}

bool Rs7Mid::Hook::PrepareCall(uintptr_t site, const uint8_t* bytes, size_t length, Callback callback, Mode mode,
                               Call placement) {
    if (!bytes || length != 5 || bytes[0] != 0xe8) {
        return false;
    }

    const uintptr_t callee = site + 5 + int32_t(ReadDword(bytes + 1));

    return Emit([&](uint8_t* code) {
        return Rs7Mid_BuildCall(code, 256, site, callee, uintptr_t(code), uintptr_t(callback), int(mode),
                                placement == Call::After);
    });
}

bool Rs7Mid::Hook::PrepareCallIn(Callback callback, Mode mode, uint16_t argBytes) {
    return Emit([&](uint8_t* code) {
        return Rs7Mid_BuildCallIn(code, 256, uintptr_t(code), uintptr_t(callback), int(mode), argBytes);
    });
}

uintptr_t Rs7Mid::CallIn::Entry() {
    if (!hook_.Entry() && !hook_.PrepareCallIn(callback_, mode_, argBytes_)) {
        return 0;
    }

    return hook_.Entry();
}

bool Rs7Mid::Hook::PrepareWrap(uintptr_t site, const uint8_t* bytes, size_t length, bool callSite, const Wrap& wrap,
                               Callback before, Callback after, Mode mode) {
    if (!bytes || (callSite && (length != 5 || bytes[0] != 0xe8))) {
        return false;
    }

    const uintptr_t callee = callSite ? site + 5 + int32_t(ReadDword(bytes + 1)) : 0;

    return Emit([&](uint8_t* code) {
        return Rs7Mid_BuildWrap(code, 256, uintptr_t(code), callee, callSite ? nullptr : bytes, site, length,
                                uintptr_t(before), uintptr_t(after), wrap.args, wrap.PopBytes(), wrap.locals,
                                int(mode));
    });
}

// ---- x87 stack of the FXSAVE image (Context::fpu, full mode) ----
// Image layout (Intel SDM vol. 1, FXSAVE): FSW at +2 (TOP in bits 11-13, C1 bit 9), the abridged tag byte at +4 (bit
// i = physical register i valid), ST(i) in stack order at +32 + 16 * i (80 bits each).
namespace {
constexpr size_t kFsw = 2, kTag = 4, kSt = 32, kStride = 16;

// The FXSAVE image of a full-mode context; nullptr in light mode (0) and sse mode (a tagged SseResult pointer).
const uint8_t* Image(uint32_t fpu) {
    return fpu && !(fpu & 1) ? reinterpret_cast<const uint8_t*>(uintptr_t(fpu)) : nullptr;
}

unsigned Top(const uint8_t* image) {
    return (unsigned(image[kFsw]) | unsigned(image[kFsw + 1]) << 8) >> 11 & 7;
}

bool Valid(const uint8_t* image, unsigned st) {
    return (image[kTag] >> ((Top(image) + st) & 7)) & 1;
}

unsigned LeadingZeros(uint64_t v) {
    unsigned n = 0;

    for (; n < 64 && !(v >> 63); ++n) {
        v <<= 1;
    }

    return n;
}

// Exact double -> 80-bit extended (as `fld qword`: a signalling NaN comes back quiet). Integer only.
void ToExtended(double value, uint8_t* extended) {
    uint64_t bits;

    std::memcpy(&bits, &value, sizeof bits);
    const uint32_t sign = uint32_t(bits >> 63) << 15, exponent = uint32_t(bits >> 52) & 0x7ff;
    const uint64_t fraction = bits & ((1ull << 52) - 1);
    uint64_t mantissa = 0;
    uint32_t biased = 0;

    if (exponent == 0x7ff) {
        biased = 0x7fff;
        mantissa = 1ull << 63 | fraction << 11 | (fraction ? 1ull << 62 : 0);
    } else if (exponent) {
        biased = exponent - 1023 + 16383;
        mantissa = 1ull << 63 | fraction << 11;
    } else if (fraction) {  // denormal: normalised, extended has the range
        const unsigned shift = LeadingZeros(fraction << 11);

        biased = 16383 - 1022 - shift;
        mantissa = fraction << 11 << shift;
    }

    std::memcpy(extended, &mantissa, 8);
    const uint16_t top = uint16_t(sign | biased);

    std::memcpy(extended + 8, &top, 2);
}

// 80-bit extended -> double, rounded to nearest even (as `fstp qword` with the FNINIT control word). Integer only.
double FromExtended(const uint8_t* extended) {
    uint64_t mantissa;
    uint16_t top;

    std::memcpy(&mantissa, extended, 8);
    std::memcpy(&top, extended + 8, 2);
    const uint64_t sign = uint64_t(top >> 15) << 63, infinity = 0x7ffull << 52;
    const int biased = top & 0x7fff;
    uint64_t bits = sign;

    if (biased == 0x7fff) {  // infinity, or a NaN: quiet, top 51 payload bits kept
        bits |= infinity | (mantissa << 1 ? 1ull << 51 | (mantissa >> 11 & ((1ull << 52) - 1)) : 0);
    } else if (biased && !(mantissa >> 63)) {
        bits = 0xfff8000000000000ull;  // unnormal: the x87 refuses it (invalid) and stores the default NaN
    } else if (mantissa) {
        const unsigned lz = LeadingZeros(mantissa);
        int exponent = (biased ? biased : 1) - 16383 - int(lz);  // of the leading one
        const uint64_t m = mantissa << lz;
        // 53 bits of a normal result; a denormal one keeps fewer (shift up to 64 + 1 rounds to zero).
        const int shift = exponent >= -1022 ? 11 : 11 + (-1022 - exponent);
        uint64_t fraction = 0;
        uint64_t rest = m, half = 1ull << 63;

        if (shift < 64) {
            fraction = m >> shift;
            rest = m & ((1ull << shift) - 1);
            half = 1ull << (shift - 1);
        } else if (shift > 64) {
            rest = 1, half = 2;  // below half the smallest denormal: zero (rest != 0 never ties)
        }

        if (rest > half || (rest == half && (fraction & 1))) {
            ++fraction;
        }

        if (exponent >= -1022) {
            if (fraction >> 53) {
                fraction >>= 1;
                ++exponent;
            }

            bits |= exponent > 1023 ? infinity : uint64_t(exponent + 1023) << 52 | (fraction & ((1ull << 52) - 1));
        } else {
            bits |= fraction;  // a carry into bit 52 is the smallest normal, as encoded
        }
    }

    double value;

    std::memcpy(&value, &bits, sizeof value);

    return value;
}
}  // namespace

unsigned Rs7Mid::Context::FpuDepth() const {
    unsigned depth = 0;

    for (unsigned tags = Image(fpu) ? Image(fpu)[kTag] : 0u; tags; tags &= tags - 1) {
        ++depth;
    }

    return depth;
}

bool Rs7Mid::Context::HasSt0() const {
    return Image(fpu) && Valid(Image(fpu), 0);
}

double Rs7Mid::Context::St0() const {
    if (!HasSt0()) {
        const uint8_t quietNan[10] = {0, 0, 0, 0, 0, 0, 0, 0xc0, 0xff, 0x7f};

        return FromExtended(quietNan);
    }

    return FromExtended(reinterpret_cast<const uint8_t*>(fpu) + kSt);
}

bool Rs7Mid::Context::SetSt0(double value) {
    if (!HasSt0()) {
        return false;
    }

    ToExtended(value, reinterpret_cast<uint8_t*>(fpu) + kSt);

    return true;
}

bool Rs7Mid::Context::PushSt0(double value) {
    auto* image = const_cast<uint8_t*>(Image(fpu));
    if (!image || Valid(image, 7)) {
        return false;  // no image, or ST7 is in use: `fld` would overflow the stack
    }

    for (size_t i = kSt + 7 * kStride; i-- > kSt;) {
        image[i + kStride] = image[i];  // ST(i) -> ST(i + 1)
    }

    const unsigned top = (Top(image) + 7) & 7;
    const unsigned fsw = (unsigned(image[kFsw]) | unsigned(image[kFsw + 1]) << 8) & ~0x3a00u;  // TOP and C1

    image[kFsw] = uint8_t(fsw);
    image[kFsw + 1] = uint8_t((fsw | top << 11) >> 8);
    image[kTag] = uint8_t(image[kTag] | 1u << top);
    ToExtended(value, image + kSt);

    return true;
}

namespace {
// The magnitude of a supported 80-bit value as an ordered key (exponent, mantissa): zeros, denormals, normals and
// infinity. false for a NaN or an unnormal (the x87 compares those unordered).
bool Magnitude(const uint8_t* extended, uint16_t& exponent, uint64_t& mantissa, bool& negative) {
    uint16_t top;

    std::memcpy(&mantissa, extended, 8);
    std::memcpy(&top, extended + 8, 2);
    exponent = top & 0x7fff;
    negative = (top >> 15) != 0;

    if (exponent == 0x7fff) {
        return mantissa == 1ull << 63;  // infinity; a NaN or a pseudo-infinity / pseudo-NaN (no integer bit) is not
    }

    if (exponent && !(mantissa >> 63)) {
        return false;  // unnormal
    }

    if (!exponent) {
        exponent = 1;  // a denormal has the smallest normal's exponent and no integer bit
    }

    if (!mantissa) {
        exponent = 0, negative = false;  // +0 == -0
    }

    return true;
}
}  // namespace

int Rs7Mid::Context::CompareSt0(float rhs) const {
    if (!HasSt0()) {
        return 2;
    }

    uint8_t right[10];

    ToExtended(double(rhs), right);  // exact, as `fcom m32` widens its operand
    uint16_t le, re;
    uint64_t lm, rm;
    bool ln, rn;

    if (!Magnitude(Image(fpu) + kSt, le, lm, ln) || !Magnitude(right, re, rm, rn)) {
        return 2;
    }

    if (ln != rn) {
        return ln ? -1 : 1;
    }

    const int magnitude = le != re ? (le < re ? -1 : 1) : lm != rm ? (lm < rm ? -1 : 1) : 0;

    return ln ? -magnitude : magnitude;
}
