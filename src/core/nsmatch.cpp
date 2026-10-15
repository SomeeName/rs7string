#include "core/nsmatch.h"
#include <windows.h>
#include <cstring>

extern "C" __declspec(dllexport) uintptr_t g_rs7_fn_msort = 0, g_rs7_fn_mins1 = 0, g_rs7_fn_mins2 = 0,
                                           g_rs7_fn_mcmp = 0, g_rs7_matcher_table = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_match_misses = 2;

namespace {
volatile LONG g_stock6 = 0, g_portN = 0, g_portHits = 0;

// The candidate vector element of the stock matcher (12 bytes): MIDI bin, summed strength, a byte
// nobody reads.
struct Cand {
    int32_t bin;
    float s;
    uint32_t pad;
};

// The stock sort sequence of 0x4E7B30 (0x4E802D..0x4E8078) over v[0..127): std::sort's introsort loop
// 0x4E3EE0 (cdecl first, last, 0, depth 2*log2(127) = 12, comparator 0x4E1940 = descending strength),
// then the insertion sorts 0x4E86B0 (EDI = first, [esp+4] = first + 16) and 0x4E45A0 (cdecl first + 16,
// last). v[127] stays where it is, as in stock.
void GameSort(Cand* v, const rs7match::Game& g) {
    const uintptr_t s = g.sort, i1 = g.ins1, i2 = g.ins2, c = g.cmp;

    __asm {
        push ebx
        push edi
        mov edx, v
        lea ebx, [edx+5F4h]
        push c
        push 12
        push 0
        push ebx
        push edx
        call s
        add esp, 14h
        mov edi, v
        lea eax, [edi+0C0h]
        push eax
        call i1
        add esp, 4
        lea eax, [edi+0C0h]
        push ebx
        push eax
        call i2
        add esp, 8
        pop edi
        pop ebx
    }
}

// M * c > U exactly as stock evaluates it: FLD float M, FMUL double c (at the thread's x87 precision),
// compare with float U (TEST AH,41h: not below, not equal, ordered).
bool ScaledGreater(float m, double c, float u) {
    unsigned short sw;

    __asm {
        fld u
        fld m
        fmul c
        fcompp
        fnstsw ax
        mov sw, ax
    }

    return (sw & 0x4100) == 0;
}
}  // namespace

namespace rs7match {

bool OctaveDyad(const int32_t* in, int n) {
    int count = 0, first = 0;
    bool octave = false;

    for (int i = 0; i < n; ++i) {
        const int v = in[i];
        if (v <= 0) {
            continue;
        }

        ++count;
        if (first <= 0) {
            first = v;
        } else if (first + 12 == v) {
            octave = true;
        } else {
            return false;
        }
    }

    return octave && count == 2;
}

bool Compact6(const int32_t* in, int n, int32_t* out6) {
    int k = 0;

    for (int i = 0; i < 6; ++i) {
        out6[i] = -1;
    }

    for (int i = 0; i < n; ++i) {
        if (in[i] < 0) {
            continue;
        }
        if (k == 6) {
            return false;
        }

        out6[k++] = in[i];
    }

    return true;
}

int MatchN(const uint8_t* m, const int32_t* in, int n, int tol, int hist, const Game& g) {
    if (n > kMaxTargets) {
        n = kMaxTargets;
    }
    // Dedupe against the input (0x4E7B5B): a repeated value becomes -1.
    int32_t loc[kMaxTargets];

    for (int i = 0; i < n; ++i) {
        loc[i] = in[i];

        for (int j = 0; j < i; ++j) {
            if (in[i] == in[j]) {
                loc[i] = -1;
                break;
            }
        }
    }

    int active = 0;

    for (int i = 0; i < n; ++i) {
        active += loc[i] > -1;
    }

    // Three or fewer targets: the lowest one (< 65) matches on its fundamental only (special row).
    int32_t special[6] = {0, 0, 0, 0, 0, 0};
    int32_t low = 0x80;

    if (active <= 3) {
        for (int i = 0; i < n; ++i) {
            if (loc[i] > 0 && loc[i] < low) {
                low = loc[i];
            }
        }

        if (low < 0x41) {
            special[0] = low;
        } else {
            low = -1;
        }
    }

    const int32_t* row[kMaxTargets] = {};
    int32_t cnt[kMaxTargets];

    for (int i = 0; i < n; ++i) {
        cnt[i] = -1;

        if (loc[i] > -1) {
            row[i] = loc[i] == low
                         ? special
                         : reinterpret_cast<const int32_t*>(g.table + 24 * static_cast<intptr_t>(loc[i] - 22));
            cnt[i] = 0;
        }
    }

    // The detector frame `hist` frames back: summed peak strength per MIDI bin.
    float bins[128] = {};
    const uint8_t* input = *reinterpret_cast<uint8_t* const*>(m);
    const uint8_t* h = *reinterpret_cast<uint8_t* const*>(input + 4);

    if (*reinterpret_cast<const int32_t*>(h + 0xDC8) > 0) {
        int idx = *reinterpret_cast<const int32_t*>(h + 0xDBC);

        for (int k = hist; k > 0; --k) {
            if (--idx < 0) {
                idx = *reinterpret_cast<const int32_t*>(h + 0xDC0) - 1;
            }
        }

        uint8_t fr[2000];

        std::memcpy(fr, *reinterpret_cast<uint8_t* const*>(h + 0xDB8) + static_cast<intptr_t>(idx) * 2000, 2000);
        float level;

        std::memcpy(&level, fr + 0x700, 4);

        if (level > -55.0) {  // FCOMP double -55.0, TEST AH,41h: not taken when NaN
            int32_t peaks;

            std::memcpy(&peaks, fr + 0xBC, 4);

            for (int k = 0; k < peaks; ++k) {
                int32_t b;
                float s;

                std::memcpy(&b, fr + 0x1C + 8 * k, 4);
                std::memcpy(&s, fr + 0x20 + 8 * k, 4);
                if (b > -1 && b < 128) {
                    bins[b] = s + bins[b];
                }
            }
        }
    }

    Cand v[128];

    for (int b = 0; b < 128; ++b) {
        v[b] = Cand{b, bins[b], 0};
    }

    GameSort(v, g);
    float matched = 0.0f, unmatched = 0.0f;
    int fund = 0;

    for (int e = 0; e < 128; ++e) {
        const float s = v[e].s;
        if (!(0.0f < s) && !(s != s)) {
            break;  // FCOM 0.0 with s: continue while 0 < s or unordered
        }

        bool hit = false;

        for (int i = 0; i < n; ++i) {
            if (!row[i] && !(loc[i] > -1)) {
                continue;
            }

            for (int k = 0; k < 6; ++k) {
                if (row[i][k] == v[e].bin) {
                    if (k == 0) {
                        ++fund;
                    }
                    matched = s + matched;
                    ++cnt[i];
                    hit = true;
                }
            }
        }

        int found = 0;

        for (int i = 0; i < n; ++i) {
            found += cnt[i] > 0;
        }

        if (found == active) {
            break;
        }

        if (!hit) {
            unmatched = s + unmatched;
        }
    }

    int found = 0;

    for (int i = 0; i < n; ++i) {
        found += cnt[i] > 0;
    }

    const uint8_t* det = *reinterpret_cast<uint8_t* const*>(m + 4);
    if (det && *reinterpret_cast<const int32_t*>(det + 0x11EC) == 2 && OctaveDyad(in, n) && m[0x54] && found >= 1 &&
        ScaledGreater(matched, 0.30000001192092896, unmatched)) {
        return 1;
    }

    const bool less = unmatched < matched;
    if (active == 6) {
        return (found >= 6 - tol && less) ||
               (found == 5 - tol && ScaledGreater(matched, 0.20000000298023224, unmatched));
    }
    if (active == 5) {
        return (found >= 5 - tol && less) ||
               (found == 4 - tol && ScaledGreater(matched, 0.20000000298023224, unmatched));
    }
    // Seven or eight targets (only the port sees them): stock has no case for them; its branch below
    // would require every target. [chords] wide_misses (g.misses, default 2) lets up to that many
    // targets be missing, with the five/six-target rule: all found and less unmatched than matched
    // strength, or some missing (at most g.misses) and unmatched < 0.2 x matched.
    if (active >= 7 && g.misses > 0) {
        return (found >= active - tol && less) || (found < active - tol && found >= active - g.misses - tol &&
                                                   ScaledGreater(matched, 0.20000000298023224, unmatched));
    }

    return fund == active || (found >= active - tol && less);
}

}  // namespace rs7match

namespace {
rs7match::Game Bound() {
    rs7match::Game g;

    g.sort = g_rs7_fn_msort;
    g.ins1 = g_rs7_fn_mins1;
    g.ins2 = g_rs7_fn_mins2;
    g.cmp = g_rs7_fn_mcmp;
    g.table = g_rs7_matcher_table;
    g.misses = static_cast<int>(g_rs7_match_misses);

    return g;
}
}  // namespace

extern "C" int __cdecl Rs7Ns_MatchPort(const uint8_t* matcher, const int32_t* in, int n, int tol, int hist) {
    return rs7match::MatchN(matcher, in, n, tol, hist, Bound());
}

namespace {
// The port behind an SEH guard (a fault reads as "not matched"); kept out of Rs7Ns_DetectChord so the
// compaction path has no SEH frame (the emulator tests run it).
__declspec(noinline) int PortGuarded(const uint8_t* matcher, const int32_t* in8, int tol, int hist) {
    const rs7match::Game g = Bound();

    __try {
        return rs7match::MatchN(matcher, in8, rs7match::kMaxTargets, tol, hist, g);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
}  // namespace

extern "C" int __cdecl Rs7Ns_DetectChord(const uint8_t* matcher, const int32_t* in8, int tol, int hist, int32_t* out6) {
    if (rs7match::Compact6(in8, rs7match::kMaxTargets, out6)) {
        InterlockedIncrement(&g_stock6);
        return -1;
    }

    const int r = PortGuarded(matcher, in8, tol, hist);

    InterlockedIncrement(&g_portN);
    if (r) {
        InterlockedIncrement(&g_portHits);
    }

    return r;
}

Rs7MatchStats Rs7Ns_MatchStats(bool reset) {
    Rs7MatchStats s;

    s.stock6 = reset ? InterlockedExchange(&g_stock6, 0) : g_stock6;
    s.portN = reset ? InterlockedExchange(&g_portN, 0) : g_portN;
    s.portHits = reset ? InterlockedExchange(&g_portHits, 0) : g_portHits;

    return s;
}
