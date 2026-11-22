#include "features/pitch/pitch.h"
#include "features/pitch/pitch_internal.h"
#include "features/pitch/state.h"
#include "model/instrument.h"
#include "model/tuning.h"
#include "platform/log.h"
#include <intrin.h>

extern "C" {
uintptr_t g_rsxr_profile_apply = 0;  // pitch.h: TS8PA / TS8PZ (native tuner)
uintptr_t g_rsxr_native_pitch = 0;
uintptr_t g_rsxr_fn_msize = 0;
const int32_t g_rsxr_pitch_gbase[8] = {rsxr::kGuitarOpenMidi[0], rsxr::kGuitarOpenMidi[1], rsxr::kGuitarOpenMidi[2],
                                       rsxr::kGuitarOpenMidi[3], rsxr::kGuitarOpenMidi[4], rsxr::kGuitarOpenMidi[5],
                                       rsxr::kGuitarOpenMidi[6], rsxr::kGuitarOpenMidi[7]};
// Every bass string, the extended low B / high C included (rsxrbass::ExtraBase gates those to an extended-bass song).
const int32_t g_rsxr_pitch_bbase[6] = {rsxr::kBassOpenMidi[0], rsxr::kBassOpenMidi[1], rsxr::kBassOpenMidi[2],
                                       rsxr::kBassOpenMidi[3], rsxr::kBassOpenMidi[4], rsxr::kBassOpenMidi[5]};
}

rsxrp::State rsxrp::g_state;

namespace {
using rsxrp::g_state;
using rsxrp::UncheckedField;

using MsizeFn = size_t(__cdecl*)(void*);

// Whether the CRT block of `p` holds at least `size` bytes (false without _msize: stock behaviour).
bool Holds(uint8_t* p, uint32_t size) {
    return p && g_rsxr_fn_msize && reinterpret_cast<MsizeFn>(g_rsxr_fn_msize)(p) >= size;
}

bool Bass(uint8_t* det) {
    return rsxr::IsBassKind(UncheckedField<int>(det, rsxr::kDetInstrument));
}

// The registry of tailed allocations: every object whose tail the DLL wrote (PM0, PR0, PM1, S3, PT1: only after Holds
// saw the native block size). RsxrPitch_Tailed reads it instead of _msize, so the hot readers (PC1-PC4 in light mode,
// the publication) call no CRT function and execute no x87/SSE instruction. One dword per object: its address, bit 0
// set for a detector (both are heap blocks, 8-aligned). Writers take a spin lock (cold paths, possibly two threads);
// readers acquire the published keys without it. Every writer that finds a stock-size block drops a stale entry at that
// address. Full (rsxrp::kTailedMax, 64 x the measured peak): the least recently written entry goes, a dead object's (a
// live object is rewritten at every tuning change), logged once.
using rsxrp::kTailedMax;

static_assert(std::atomic<uintptr_t>::is_always_lock_free);

uintptr_t TailedKey(const uint8_t* obj, uint32_t kind) {
    return reinterpret_cast<uintptr_t>(obj) | (kind ? 1u : 0u);
}

void LockTailed() {
    while (_InterlockedExchange(&g_state.tailedLock, 1)) {
    }
}

void UnlockTailed() {
    _InterlockedExchange(&g_state.tailedLock, 0);
}

// Writes the magic that marks a valid tail and records the object.
void MarkTailed(uint8_t* obj, uint32_t kind) {
    UncheckedField<uint32_t>(obj, kind ? rsxrp::kDetMagic : rsxrp::kMatMagic) = rsxrp::kMagic;
    const uintptr_t key = TailedKey(obj, kind);

    LockTailed();
    unsigned slot = 0;

    for (unsigned i = 0; i < kTailedMax; ++i) {
        if (g_state.tailed[i].load(std::memory_order_relaxed) == key) {
            slot = i;
            break;
        }

        if (g_state.tailedAge[i] < g_state.tailedAge[slot]) {
            slot = i;
        }
    }

    const uintptr_t previous = g_state.tailed[slot].load(std::memory_order_relaxed);
    const bool evicted = previous && previous != key;
    const bool logEviction = evicted && !g_state.tailedEvictLogged;

    g_state.tailedEvictLogged = g_state.tailedEvictLogged || evicted;
    g_state.tailed[slot].store(key, std::memory_order_release);
    g_state.tailedAge[slot] = ++g_state.tailedClock;
    UnlockTailed();

    if (logEviction) {  // full-mode writers only (PM0 PR0 PM1 S3 PT1)
        RsxrLog("pitch8: the registry of tailed allocations is full (%u), the least recently written entry goes",
                kTailedMax);
    }
}

// A new or reset object in a block without the tail (allocated before PA1 / PA2 were written): a stale entry of a
// freed tailed object at the same address goes.
void ForgetTailed(const uint8_t* obj, uint32_t kind) {
    const uintptr_t key = TailedKey(obj, kind);

    LockTailed();

    for (unsigned i = 0; i < kTailedMax; ++i) {
        if (g_state.tailed[i].load(std::memory_order_relaxed) == key) {
            g_state.tailed[i].store(0, std::memory_order_release);
            g_state.tailedAge[i] = 0;
        }
    }

    UnlockTailed();
}

bool Remembered(const uint8_t* obj, uint32_t kind) {
    const uintptr_t key = TailedKey(obj, kind);

#pragma loop(no_vector)
    for (unsigned i = 0; i < kTailedMax; ++i) {
        if (g_state.tailed[i].load(std::memory_order_acquire) == key) {
            return true;
        }
    }

    return false;
}

void Put8(uint8_t* dst, const int16_t t[8]) {  // plain stores: no CRT call (emulator tests)
    for (int i = 0; i < 8; ++i) {
        reinterpret_cast<int16_t*>(dst)[i] = t[i];
    }
}

void WriteDet(uint8_t* det, const int16_t t[8]) {
    const bool bass = Bass(det);
    int16_t open[8];

    rsxrp::FillOpen(t, bass, open);
    Put8(det + rsxrp::kDetTuning8, t);
    Put8(det + rsxrp::kDetOpen8, open);
    // Stock 6 / bass 4; the 7/8 a song widened the detector to (S4, RsxrSong_WidenDetector) survives the
    // SetTuning that S4 runs after it.
    const unsigned song = bass ? 0 : RsxrPitch_SongCount(det);

    UncheckedField<uint32_t>(det, rsxrp::kDetCount) = song ? song : bass ? 4 : 6;
    MarkTailed(det, 1);
}
}  // namespace

namespace {
// The song's 7 / 8-entry tuning per object (the last SetTuning with more than six entries: S4), kept
// through a six-entry profile re-apply (Play_Tuning_ApplyToDetection, g_rsxr_profile_apply).
// Value: the entry count kept (7 or 8). A full table replaces its entries in turn.
// t = the new vector's tuning8 (n entries). Returns the entry count the tail keeps.
size_t KeepSong(const uint8_t* obj, const int16_t* tail, int16_t t[8], size_t n) {
    const size_t* kept = g_state.song.Find(obj);

    if (n > 6) {  // a song with extra strings: remembered (the tail holds at most eight)
        const size_t keep = n > 8 ? 8 : n;

        g_state.song.Put(obj, keep);
        return keep;
    }

    if (g_rsxr_profile_apply && kept && RsxrPitch_KeepSongTuning()) {  // the profile's six strings
        for (size_t s = 6; s < *kept; ++s) {
            t[s] = tail[s];
        }

        return *kept;
    }

    if (kept && !g_rsxr_profile_apply) {
        g_state.song.Erase(obj);  // a six-string song replaces it
    }

    return n;
}
}  // namespace

extern "C" void __cdecl RsxrPitch_SetSongCount(const uint8_t* det, unsigned count) {
    if (!det) {
        return;
    }

    if (count) {
        g_state.songCount.Put(det, count);
    } else {
        g_state.songCount.Erase(det);
    }
}

extern "C" unsigned __cdecl RsxrPitch_SongCount(const uint8_t* det) {
    const unsigned* count = g_state.songCount.Find(det);

    return count ? *count : 0;
}

namespace {
// The floor Play_Detector_Reset wrote per detector (PR0), so a lowered floor goes back to it, also to another mod's
// patched floor. A detector seen first after its reset (PR0 not written) records the floor it has then: the DLL only
// writes floors after recording them. Game thread only.
// Value: the reset floor. A full table replaces its entries in turn (round robin), so a live detector's record can go.
int ResetFloorOf(uint8_t* det, bool reset) {
    int* r = g_state.floor.Find(det);
    const int current = UncheckedField<int>(det, rsxr::kDetFloor);

    if (r) {
        if (reset) {
            *r = current;
        }

        return *r;
    }

    // Not recorded outside a reset: the record may have been evicted after the DLL lowered this floor, so the current
    // floor is not taken below the stock one.
    return g_state.floor.Put(det, reset || current >= rsxrp::kStockFloor ? current : rsxrp::kStockFloor);
}
}  // namespace

extern "C" void __cdecl RsxrPitch_DetFloor(uint8_t* det, int reset) {
    if (!det) {
        return;
    }

    const int resetFloor = ResetFloorOf(det, reset != 0);
    int& floor = UncheckedField<int>(det, rsxr::kDetFloor);
    const int lowest = UncheckedField<int>(det, rsxr::kDetLowestOpen);
    const int want = rsxrp::Floor(resetFloor, lowest);

    if (floor == want) {
        return;
    }

    RsxrPitch_FloorLog(det, floor, want, lowest, resetFloor);
    floor = want;
}

extern "C" __declspec(noinline) void __cdecl RsxrPitch_FloorLog(uint8_t* det, int floor, int want, int lowest,
                                                                int reset) {
    // +DCCh: the rate the lag of a pitch is computed with, +DD4h: the lag buffer's length (0x4DF4D0 clamps the lag to
    // length - 4).
    RsxrLog("pitch: detector %p floor %d -> %d (lowest open %d, reset floor %d; lag rate %d, buffer %d)", det, floor,
            want, lowest, reset, int(UncheckedField<float>(det, 0xDCC)), UncheckedField<int>(det, 0xDD4));
}

extern "C" void __cdecl RsxrPitch_DetReset(uint8_t* det) {
    g_state.song.Erase(det);  // a new / reset detector has no song yet
    RsxrPitch_SetSongCount(det, 0);

    if (!Holds(det, rsxrp::kDetSize)) {
        ForgetTailed(det, 1);
        return;
    }

    const int16_t zero[8] = {};

    WriteDet(det, zero);  // the stock reset zeroes tuning[6] first and derives openMidi from it
}

extern "C" void __cdecl RsxrPitch_DetTuned(uint8_t* det, const RsxrInt16Vec* v) {
    if (!Holds(det, rsxrp::kDetSize)) {
        ForgetTailed(det, 1);
        return;
    }

    size_t n = static_cast<size_t>(v->end - v->begin);
    int16_t t[8];

    rsxrp::FillTuning(v->begin, n, t);
    if (UncheckedField<uint32_t>(det, rsxrp::kDetMagic) == rsxrp::kMagic) {
        n = KeepSong(det, reinterpret_cast<const int16_t*>(det + rsxrp::kDetTuning8), t, n);
    } else {
        n = KeepSong(det, t, t, n);
    }
    WriteDet(det, t);
    int& lowest = UncheckedField<int>(det, rsxr::kDetLowestOpen);

    lowest = rsxrp::Lowest(lowest, t, n, Bass(det));
}

void RsxrPitch_ClearTailed() {
    LockTailed();

    for (unsigned i = 0; i < kTailedMax; ++i) {
        g_state.tailed[i].store(0, std::memory_order_release);
        g_state.tailedAge[i] = 0;
    }

    g_state.tailedEvictLogged = false;
    UnlockTailed();
}

extern "C" int __cdecl RsxrPitch_Tailed(uint8_t* obj, uint32_t kind) {
    const uint32_t magic = kind ? rsxrp::kDetMagic : rsxrp::kMatMagic;

    return obj && Remembered(obj, kind) && UncheckedField<uint32_t>(obj, magic) == rsxrp::kMagic ? 1 : 0;
}

extern "C" void __cdecl RsxrPitch_MatNew(uint8_t* m) {
    g_state.song.Erase(m);

    if (!Holds(m, rsxrp::kMatSize)) {
        ForgetTailed(m, 0);
        return;
    }

    const int16_t zero[8] = {};

    Put8(m + rsxrp::kMatTuning8, zero);  // the stock ctor zeroes tuning[6] (+44h)
    MarkTailed(m, 0);
}

extern "C" void __cdecl RsxrPitch_MatTuned(uint8_t* m, const RsxrInt16Vec* v) {
    if (!Holds(m, rsxrp::kMatSize)) {
        ForgetTailed(m, 0);
        return;
    }

    int16_t t[8];
    const size_t n = static_cast<size_t>(v->end - v->begin);

    rsxrp::FillTuning(v->begin, n, t);
    if (UncheckedField<uint32_t>(m, rsxrp::kMatMagic) == rsxrp::kMagic) {
        KeepSong(m, reinterpret_cast<const int16_t*>(m + rsxrp::kMatTuning8), t, n);
    } else {
        KeepSong(m, t, t, n);
    }
    Put8(m + rsxrp::kMatTuning8, t);
    MarkTailed(m, 0);
}
