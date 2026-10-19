// Mid-hook cost breakdown (ARCHITECTURE.md section 15, "Cost"): the production thunks against ablated copies of the
// full thunk, a direct call and the stock code, on a dummy `mov eax,7; ret` site. Prints numbers, checks only results.
#include "platform/midhook.h"
#include "test.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
void __cdecl BenchNoop(Rs7Mid::Context&) {}

// The full thunk without FNINIT (FXSAVE / FXRSTOR kept).
__declspec(naked) void ThunkNoFninit() {
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

// The full thunk without any floating-point save (no FXSAVE / FNINIT / FXRSTOR, no aligned save area).
__declspec(naked) void ThunkNoFx() {
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
        push ebp
        call dword ptr [ebp+36]
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

// Lower bound: PUSHFD / PUSHAD, call, POPAD / POPFD (no context copy, no writable ESP / jump).
__declspec(naked) void ThunkMin() {
    __asm {
        pushfd
        pushad
        push esp
        call dword ptr [esp+40]
        add esp, 4
        popad
        popfd
        add esp, 4
        ret
    }
}

void Retarget(uint8_t* entry, void (*thunk)()) {
    const auto rel = int32_t(uintptr_t(thunk) - (uintptr_t(entry) + 15));

    std::memcpy(entry + 11, &rel, 4);
}

double MedianNs(int (*fn)(), int calls) {
    LARGE_INTEGER freq, start, end;
    double samples[7];
    volatile int sink = 0;

    QueryPerformanceFrequency(&freq);

    for (double& sample : samples) {
        for (int i = 0; i < 1000; ++i) {
            sink = fn();
        }

        QueryPerformanceCounter(&start);

        for (int i = 0; i < calls; ++i) {
            sink = fn();
        }

        QueryPerformanceCounter(&end);
        sample = double(end.QuadPart - start.QuadPart) * 1e9 / double(freq.QuadPart) / calls;
    }

    std::sort(samples, samples + 7);

    return sink == 7 ? samples[3] : -1.0;
}
}  // namespace

TEST(midhook_cost_breakdown) {
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));

    CHECK(page != nullptr);
    if (!page) {
        return;
    }

    const uint8_t stock[] = {0xb8, 7, 0, 0, 0, 0xc3};

    std::memcpy(page, stock, sizeof stock);
    // Direct call: call BenchNoop; mov eax,7; ret.
    page[16] = 0xe8;
    const auto rel = int32_t(uintptr_t(&BenchNoop) - uintptr_t(page + 21));

    std::memcpy(page + 17, &rel, 4);
    std::memcpy(page + 21, stock, sizeof stock);

    struct Variant {
        const char* name;
        void (*thunk)();
    };

    const Variant variants[] = {{"full", &Rs7Mid_Thunk},
                                {"full - fninit", &ThunkNoFninit},
                                {"full - fxsave/fninit/fxrstor", &ThunkNoFx},
                                {"light (production)", &Rs7Mid_ThunkLight},
                                {"sse (production)", &Rs7Mid_ThunkSse},
                                {"pushfd/pushad/call only", &ThunkMin}};
    constexpr int calls = 200000;
    const double stockNs = MedianNs(reinterpret_cast<int (*)()>(page), calls);
    const double directNs = MedianNs(reinterpret_cast<int (*)()>(page + 16), calls);

    std::printf("mid-hook cost (median of 7 x %d, ns/hit): stock %.1f, direct call %.1f\n", calls, stockNs, directNs);
    CHECK(stockNs > 0 && directNs > 0);

    for (size_t i = 0; i < sizeof variants / sizeof variants[0]; ++i) {
        uint8_t* entry = page + 256 + i * 256;

        CHECK(Rs7Mid_Build(entry, 256, page, uintptr_t(page), 5, uintptr_t(entry), uintptr_t(&BenchNoop)) > 0);
        Retarget(entry, variants[i].thunk);
        const double ns = MedianNs(reinterpret_cast<int (*)()>(entry), calls);

        CHECK(ns > 0);
        std::printf("  %-32s %6.1f ns (+%.1f over stock)\n", variants[i].name, ns, ns - stockNs);
    }

    VirtualFree(page, 0, MEM_RELEASE);
}
