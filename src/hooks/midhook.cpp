#include "hooks/midhook.h"
#include <windows.h>

namespace {
struct Instruction {
    size_t input = 0, output = 0, size = 0, emitted = 0, rel = 0;
    int relSize = 0;
    uint8_t op = 0, condition = 0;
    bool ret = false;
};

void Dword(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        p[i] = uint8_t(v >> (i * 8));
    }
}

uint32_t ReadDword(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// Deliberate whitelist, not a general disassembler. No prefixes, LOOP/JCXZ,
// far transfers, environment-save/EIP-observing instructions, or unknown groups.
bool Decode(const uint8_t* p, size_t n, Instruction& d) {
    if (!n) {
        return false;
    }

    size_t pos = 1, immediate = 0;
    const uint8_t op = d.op = p[0];
    bool modrm = false;

    if (op == 0xe8 || op == 0xe9 || op == 0xeb || (op >= 0x70 && op <= 0x7f)) {
        d.rel = 1;
        d.relSize = op == 0xe8 || op == 0xe9 ? 4 : 1;
        d.condition = op & 15;
        pos += d.relSize;
        d.emitted = op >= 0x70 && op <= 0x7f ? 6 : 5;
    } else if (op == 0x0f) {
        if (n < 2) {
            return false;
        }

        const uint8_t second = p[pos++];

        if (second >= 0x80 && second <= 0x8f) {
            d.rel = 2;
            d.relSize = 4;
            d.condition = second & 15;
            pos += 4;
            d.emitted = 6;
        } else if (second == 0xb6 || second == 0xb7 || second == 0xbe || second == 0xbf ||
                   (second >= 0x90 && second <= 0x9f)) {
            modrm = true;
        } else {
            return false;
        }
    } else if ((op >= 0x40 && op <= 0x5f) || op == 0x90) {
        // INC/DEC/PUSH/POP registers, NOP
    } else if (op >= 0xb8 && op <= 0xbf) {
        immediate = 4;
    } else if (op >= 0xb0 && op <= 0xb7) {
        immediate = 1;
    } else if (op == 0x68 || op == 0xa1 || op == 0xa3) {
        immediate = 4;
    } else if (op == 0x6a || op == 0xa8) {
        immediate = 1;
    } else if (op == 0xa9) {
        immediate = 4;
    } else if (op <= 0x3b && (op & 7) <= 3) {
        modrm = true;  // ALU r/m,reg
    } else if (op <= 0x3d && (op & 7) == 4) {
        immediate = 1;
    } else if (op <= 0x3d && (op & 7) == 5) {
        immediate = 4;
    } else if (op == 0x84 || op == 0x85 || op == 0x86 || op == 0x87 || (op >= 0x88 && op <= 0x8b) || op == 0x8d) {
        modrm = true;
    } else if (op == 0xd9) {
        modrm = true;  // x87 D9 memory forms, filtered below (CMUV 00518BC0 stores with FST)
    } else if (op == 0xc3) {
        d.ret = true;  // RET: position independent; only valid as the LAST stolen instruction
    } else if (op == 0x80 || op == 0x81 || op == 0x83 || op == 0xc6 || op == 0xc7) {
        modrm = true;
        immediate = op == 0x81 || op == 0xc7 ? 4 : 1;
    } else {
        return false;
    }

    if (modrm) {
        if (pos >= n) {
            return false;
        }

        const uint8_t m = p[pos++], mod = m >> 6, rm = m & 7;
        if ((op == 0xc6 || op == 0xc7) && (m & 0x38)) {
            return false;
        }
        if (op == 0x8d && mod == 3) {
            return false;
        }

        // D9 /r: memory FLD(0) FST(2) FSTP(3) FLDCW(5) FNSTCW(7). Register forms (mod 3: FXCH, FNOP, FCHS,
        // FLD1...), FLDENV/FNSTENV(4,6) and the reserved /1 stay rejected.
        if (op == 0xd9) {
            const uint8_t r = (m >> 3) & 7;
            if (mod == 3 || r == 1 || r == 4 || r == 6) {
                return false;
            }
        }

        if (op == 0x0f && p[1] >= 0x90 && p[1] <= 0x9f && (m & 0x38)) {
            return false;
        }

        if (mod != 3 && rm == 4) {
            if (pos >= n) {
                return false;
            }

            const uint8_t sib = p[pos++];

            if (mod == 0 && (sib & 7) == 5) {
                pos += 4;
            }
        }

        if (mod == 0 && rm == 5) {
            pos += 4;
        }
        if (mod == 1) {
            ++pos;
        }
        if (mod == 2) {
            pos += 4;
        }
    }

    pos += immediate;
    if (pos > n || pos > 15) {
        return false;
    }

    d.size = pos;
    if (!d.emitted) {
        d.emitted = pos;
    }

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

extern "C" size_t __cdecl Rs7Mid_Build(uint8_t* out, size_t capacity, const uint8_t* source, uintptr_t sourceVa,
                                       size_t length, uintptr_t destVa, uintptr_t callback) {
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

    out[0] = 0x68;
    Dword(out + 1, uint32_t(destVa + entry));
    out[5] = 0x68;
    Dword(out + 6, uint32_t(callback));
    out[10] = 0xe9;
    Dword(out + 11, uint32_t(uintptr_t(&Rs7Mid_Thunk) - (destVa + entry)));
    // A replayed RET leaves the thunk code for good: no jump back (it would be dead bytes).
    if (endsInRet) {
        return entry + replay;
    }

    out[entry + replay] = 0xe9;
    Dword(out + entry + replay + 1, uint32_t(sourceVa + length - (destVa + entry + replay + 5)));

    return entry + replay + 5;
}

// Entry stack: callback, replay, original game stack. The temporary context is
// separate from PUSHAD so restoring ESP never depends on POPAD's ignored ESP slot.
// FXSAVE is supported by the game's required SSE CPU. C callbacks run with DF=0.
extern "C" __declspec(naked) void Rs7Mid_Thunk() {
    __asm {
        pushfd
        pushad
        mov esi, esp
        sub esp, 44
        mov edi, esp
        mov ecx, 11
        cld
        rep movsd
        mov ebp, esp
        add dword ptr [ebp+12], 12
        sub esp, 528
        and esp, -16
        mov ebx, esp
        fxsave [ebx]
        fninit
        push ebp
        call dword ptr [ebp+36]
        fxrstor [ebx]
        cld
        mov esi, ebp
        mov edi, [ebp+12]
        sub edi, 40
        mov edx, edi
        mov eax, [ebp+40]
        mov ecx, 9
        cmp edi, esi
        jbe forward_copy
        add esi, 32
        add edi, 32
        std
    forward_copy:
        rep movsd
        cld
        mov [edx+36], eax
        mov esp, edx
        popad
        popfd
        ret
    }
}

Rs7Mid::Hook::~Hook() {
    if (code_) {
        VirtualFree(code_, 0, MEM_RELEASE);
    }
}

bool Rs7Mid::Hook::Prepare(uintptr_t site, const uint8_t* bytes, size_t length, Callback callback) {
    if (code_) {
        return false;
    }

    auto* code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 256, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!code) {
        return false;
    }

    const size_t n = Rs7Mid_Build(code, 256, bytes, site, length, uintptr_t(code), uintptr_t(callback));
    DWORD old;

    if (!n || !VirtualProtect(code, 256, PAGE_EXECUTE_READ, &old)) {
        VirtualFree(code, 0, MEM_RELEASE);
        return false;
    }

    FlushInstructionCache(GetCurrentProcess(), code, n);
    code_ = code;

    return true;
}
