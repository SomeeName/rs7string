#include "core/pitch8.h"

extern "C" {
uintptr_t g_rs7_profile_apply = 0;  // core/pitch8.h: TS8PA / TS8PZ ([neck] native_tuner)
uintptr_t g_rs7_native_pitch = 0;
uintptr_t g_rs7_fn_msize = 0;
const int32_t g_rs7_pitch_gbase[8] = {40, 45, 50, 55, 59, 64, 35, 30};
const int32_t g_rs7_pitch_bbase[4] = {28, 33, 38, 43};
}

namespace rs7p {
void FillTuning(const int16_t* v, size_t n, int16_t out[8]) {
    for (size_t i = 0; i < 8; ++i) {
        out[i] = i < n ? v[i] : int16_t(0);
    }
}

void FillOpen(const int16_t t[8], bool bass, int16_t out[8]) {
    for (int i = 0; i < 8; ++i) {
        const int base = bass ? (i < 4 ? g_rs7_pitch_bbase[i] : 0) : g_rs7_pitch_gbase[i];

        out[i] = static_cast<int16_t>(static_cast<uint16_t>(static_cast<uint16_t>(t[i]) + base));
    }
}

int Lowest(int stock, const int16_t t[8], size_t n, bool bass) {
    if (bass) {
        return stock;
    }

    const size_t end = n < 8 ? n : 8;
    int low = stock;

    for (size_t s = 6; s < end; ++s) {
        const int v = g_rs7_pitch_gbase[s] + t[s];

        if (v < low) {
            low = v;
        }
    }

    return low;
}
}  // namespace rs7p

namespace {
template <class T>
T& At(uint8_t* base, uint32_t off) {
    return *reinterpret_cast<T*>(base + off);
}

using MsizeFn = size_t(__cdecl*)(void*);

// Whether the CRT block of `p` holds at least `size` bytes (false without _msize: stock behaviour).
bool Holds(uint8_t* p, uint32_t size) {
    return p && g_rs7_fn_msize && reinterpret_cast<MsizeFn>(g_rs7_fn_msize)(p) >= size;
}

bool Bass(uint8_t* det) {
    return At<int>(det, rs7::kDetInstrument) == 2;
}

void Put8(uint8_t* dst, const int16_t t[8]) {  // plain stores: no CRT call (emulator tests)
    for (int i = 0; i < 8; ++i) {
        reinterpret_cast<int16_t*>(dst)[i] = t[i];
    }
}

void WriteDet(uint8_t* det, const int16_t t[8]) {
    int16_t open[8];

    rs7p::FillOpen(t, Bass(det), open);
    Put8(det + rs7p::kDetTuning8, t);
    Put8(det + rs7p::kDetOpen8, open);
    // Stock 6 / bass 4; the 7/8 a song widened the detector to (S4, Rs7Song_WidenDetector) survives the
    // SetTuning that S4 runs after it.
    const unsigned song = Bass(det) ? 0 : Rs7Pitch_SongCount(det);

    At<uint32_t>(det, rs7p::kDetCount) = song ? song : Bass(det) ? 4 : 6;
    At<uint32_t>(det, rs7p::kDetMagic) = rs7p::kMagic;
}
}  // namespace

namespace {
// The song's 7 / 8-entry tuning per object (the last SetTuning with more than six entries: S4), kept
// through a six-entry profile re-apply (Play_Tuning_ApplyToDetection, g_rs7_profile_apply).
struct SongTuning {
    const uint8_t* obj;
    size_t n;
};

SongTuning g_song[4]{};

SongTuning* SongOf(const uint8_t* obj) {
    for (SongTuning& r : g_song) {
        if (r.obj == obj) {
            return &r;
        }
    }

    return nullptr;
}

// t = the new vector's tuning8 (n entries). Returns the entry count the tail keeps.
size_t KeepSong(const uint8_t* obj, const int16_t* tail, int16_t t[8], size_t n) {
    SongTuning* r = SongOf(obj);

    if (n > 6) {  // a song with extra strings: remembered
        if (!r) {
            r = SongOf(nullptr);
        }
        if (!r) {
            r = &g_song[0];
        }
        *r = SongTuning{obj, n > 8 ? 8 : n};
        return n;
    }

    if (g_rs7_profile_apply && r && Rs7Pitch_KeepSongTuning()) {  // the profile's six strings
        for (size_t s = 6; s < r->n; ++s) {
            t[s] = tail[s];
        }

        return r->n;
    }

    if (r && !g_rs7_profile_apply) {
        *r = SongTuning{};  // a six-string song replaces it
    }

    return n;
}
}  // namespace

namespace {
// The count a 7/8-string song widened a detector to (S4). Cleared with the detector (PR0: constructed / reset)
// and by the next S4 of that detector; game thread only.
struct SongCountRec {
    const uint8_t* det;
    unsigned count;
};

SongCountRec g_songCount[4]{};
}  // namespace

extern "C" void __cdecl Rs7Pitch_SetSongCount(const uint8_t* det, unsigned count) {
    if (!det) {
        return;
    }

    for (SongCountRec& r : g_songCount) {
        if (r.det == det) {
            r = count ? SongCountRec{det, count} : SongCountRec{};
            return;
        }
    }

    if (!count) {
        return;
    }

    for (SongCountRec& r : g_songCount) {
        if (!r.det) {
            r = SongCountRec{det, count};
            return;
        }
    }

    g_songCount[0] = SongCountRec{det, count};  // full: the oldest entry is a freed detector's
}

extern "C" unsigned __cdecl Rs7Pitch_SongCount(const uint8_t* det) {
    for (const SongCountRec& r : g_songCount) {
        if (det && r.det == det) {
            return r.count;
        }
    }

    return 0;
}

extern "C" void __cdecl Rs7Pitch_DetReset(uint8_t* det) {
    if (SongTuning* r = SongOf(det)) {
        *r = SongTuning{};  // a new / reset detector has no song yet
    }
    Rs7Pitch_SetSongCount(det, 0);
    if (!Holds(det, rs7p::kDetSize)) {
        return;
    }

    const int16_t zero[8] = {};

    WriteDet(det, zero);  // the stock reset zeroes tuning[6] first and derives openMidi from it
}

extern "C" void __cdecl Rs7Pitch_DetTuned(uint8_t* det, const Rs7Int16Vec* v) {
    if (!Holds(det, rs7p::kDetSize)) {
        return;
    }

    size_t n = static_cast<size_t>(v->end - v->begin);
    int16_t t[8];

    rs7p::FillTuning(v->begin, n, t);
    if (At<uint32_t>(det, rs7p::kDetMagic) == rs7p::kMagic) {
        n = KeepSong(det, reinterpret_cast<const int16_t*>(det + rs7p::kDetTuning8), t, n);
    } else {
        n = KeepSong(det, t, t, n);
    }
    WriteDet(det, t);
    int& lowest = At<int>(det, rs7::kDetLowestOpen);

    lowest = rs7p::Lowest(lowest, t, n, Bass(det));
}

extern "C" int __cdecl Rs7Pitch_Tailed(uint8_t* obj, uint32_t kind) {
    const uint32_t size = kind ? rs7p::kDetSize : rs7p::kMatSize;
    const uint32_t magic = kind ? rs7p::kDetMagic : rs7p::kMatMagic;

    return Holds(obj, size) && At<uint32_t>(obj, magic) == rs7p::kMagic ? 1 : 0;
}

extern "C" void __cdecl Rs7Pitch_MatNew(uint8_t* m) {
    if (SongTuning* r = SongOf(m)) {
        *r = SongTuning{};
    }
    if (!Holds(m, rs7p::kMatSize)) {
        return;
    }

    const int16_t zero[8] = {};

    Put8(m + rs7p::kMatTuning8, zero);  // the stock ctor zeroes tuning[6] (+44h)
    At<uint32_t>(m, rs7p::kMatMagic) = rs7p::kMagic;
}

extern "C" void __cdecl Rs7Pitch_MatTuned(uint8_t* m, const Rs7Int16Vec* v) {
    if (!Holds(m, rs7p::kMatSize)) {
        return;
    }

    int16_t t[8];
    const size_t n = static_cast<size_t>(v->end - v->begin);

    rs7p::FillTuning(v->begin, n, t);
    if (At<uint32_t>(m, rs7p::kMatMagic) == rs7p::kMagic) {
        KeepSong(m, reinterpret_cast<const int16_t*>(m + rs7p::kMatTuning8), t, n);
    } else {
        KeepSong(m, t, t, n);
    }
    Put8(m + rs7p::kMatTuning8, t);
    At<uint32_t>(m, rs7p::kMatMagic) = rs7p::kMagic;
}
