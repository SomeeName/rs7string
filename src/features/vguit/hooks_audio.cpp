// AUD1 (Wwise audio thread): maps the input buffer to song time, plays the scheduled onsets that fall into it and
// writes the synthesized guitar into the game's 48 kHz mono float input (replace or mix).
#include "features/vguit/hooks.h"
#include "platform/log.h"
#include <cmath>

namespace rs7vg {
namespace {

// One AkAudioBuffer as AUD1 sees it.
struct Buffer {
    void* dev;
    uint8_t* hdr;  // the AkAudioBuffer: +0 data, +0Ch uMaxFrames, +0Eh uValidFrames
    float* data;
    int n, valid;
    int inputId;
};

// The input's detector (DetectInput manager +10h input 0 / +14h otherwise, -> di, di+4 -> det).
bool Detector(int inputId, uintptr_t* di, uintptr_t* det) {
    uintptr_t mgr = 0;
    if (!g_rs7_vg_dimgr || !ReadAt(g_rs7_vg_dimgr, &mgr) || !mgr) {
        return false;
    }

    return ReadAt(mgr + (inputId == 0 ? 0x10 : 0x14), di) && *di && ReadAt(*di + 4, det) && *det;
}

// Song time at the first sample of this buffer: det+D08h + (DI+30h - det+D18h) (section 2), or the
// master clock while no detector frame exists.
bool BufferBase(int inputId, double* base, double* master, bool* mapped) {
    *mapped = false;
    float clock = 0;
    if (!MasterClock(&clock)) {
        return false;
    }

    *master = clock;
    *base = clock;
    uintptr_t di = 0, det = 0;
    if (inputId != 0 && inputId != 2) {
        return true;
    }
    if (!Detector(inputId, &di, &det)) {
        return true;
    }

    double d08 = 0, d18 = 0, di30 = 0;
    if (!ReadAt(det + 0xD08, &d08) || !ReadAt(det + 0xD18, &d18) || !ReadAt(di + 0x30, &di30)) {
        return true;
    }
    if (d08 == 0 && d18 == 0) {
        return true;
    }

    const double b = d08 + (di30 - d18);

    if (!(std::fabs(b - clock) < 0.5)) {
        ++g_vg.clockFallbacks;
        return true;
    }

    *base = b;
    *mapped = true;

    return true;
}

// [test] vguitar_trace=1: the detector's current frame (det+5E8h + frame offsets of section 4),
// every 4th buffer for 0.6 s after each onset. The worker writes the frame meanwhile
// (a torn line is possible, it is a diagnostic).
void TraceDetector(int inputId, double base, double rms) {
    uintptr_t di = 0, det = 0;
    if (!Detector(inputId, &di, &det)) {
        return;
    }

    uint8_t f[2000];
    if (!Rs7_SafeCopy(f, reinterpret_cast<const void*>(det + 0x5E8), sizeof f)) {
        return;
    }

    Rs7Log(
        "vguitar trace: base %.4f in-rms %.4f | frame t %.4f sclk %.4f serial %d onset %u | main %d | since %d %.2f | "
        "n %d c0 %d %.2f c1 %d %.2f c2 %d %.2f | lvl %.1f rel %.1f | fine %.2f smooth %.2f",
        base, rms, Get<double>(f, 0x720), Get<double>(f, 0x730), Get<int32_t>(f, 0x748), unsigned(f[0x7B5]),
        Get<int32_t>(f, 0x0C), Get<int32_t>(f, 0x14), double(Get<float>(f, 0x18)), Get<int32_t>(f, 0xBC),
        Get<int32_t>(f, 0x1C), double(Get<float>(f, 0x20)), Get<int32_t>(f, 0x24), double(Get<float>(f, 0x28)),
        Get<int32_t>(f, 0x2C), double(Get<float>(f, 0x30)), double(Get<float>(f, 0x700)), double(Get<float>(f, 0x750)),
        double(Get<float>(f, 0x7A8)), double(Get<float>(f, 0x7B0)));
}

// Reads the device and buffer header; false when the buffer is unreadable or not the expected format (logged once).
bool ReadBuffer(void* dev, void* akbuf, Buffer* b) {
    uint8_t d[0x28];
    uint8_t hdr[0x10];
    if (!Rs7_SafeCopy(d, dev, sizeof d) || !Rs7_SafeCopy(hdr, akbuf, sizeof hdr)) {
        return false;
    }

    b->dev = dev;
    b->hdr = static_cast<uint8_t*>(akbuf);
    b->data = Get<float*>(hdr, 0);
    b->n = Get<uint16_t>(hdr, 0xC);
    b->valid = Get<uint16_t>(hdr, 0xE);
    b->inputId = -1;
    const int slot = Get<int32_t>(d, 0xC);
    const int align = Get<uint16_t>(d, 0x24);

    if (g_rs7_vg_slotmap && slot >= 0 && slot < 10) {
        ReadAt(g_rs7_vg_slotmap + 4 * uintptr_t(slot), &b->inputId);
    }

    if (!g_vg.formatLogged) {
        g_vg.formatLogged = true;
        g_vg.badFormat = align != 4 || !b->data || b->n <= 0 || b->n > kMaxFrames;
        Rs7Log("vguitar: first input buffer: device %p slot %d input %d, uMaxFrames %d, uValidFrames %d, "
               "blockAlign %d, data %p%s",
               dev, slot, b->inputId, b->n, b->valid, align, static_cast<void*>(b->data),
               g_vg.badFormat ? " -> UNEXPECTED FORMAT, nothing written" : "");
    }

    return !g_vg.badFormat && b->data && b->n > 0 && b->n <= kMaxFrames;
}

// Whether a song of our session plays (else the real input passes through: menus, another player's input).
bool Playing(int inputId, DWORD now) {
    const bool active = g_vg.active && now - DWORD(g_vg.lastTickMs) < 1000;

    if (!active) {
        if (g_vg.active) {
            InterlockedExchange(&g_vg.active, 0);
        }
        return false;
    }

    return inputId == g_vg.sessionInput;
}

// The pending events whose onset falls into [base, base + n samples): marked started, copied to `local` (with
// voices only), `starts` their sample offsets. Late events are skipped, never played out of time.
int CollectOnsets(double base, int n, Onset* starts, Event* local) {
    int ns = 0;
    const double lead = g_vg.cfg.leadMs / 1000.0, end = base + double(n) / kRate;

    AcquireSRWLockExclusive(&g_vg.schedLock);

    for (int i = 0; i < kEvents && ns < 32; ++i) {
        Event& e = g_vg.ev[i];
        if (e.state != kPending) {
            continue;
        }

        const double onset = double(e.time) - lead;
        if (onset >= end) {
            continue;
        }

        if (onset < base - 0.05) {  // late (pause, seek, first buffer): never play it out of time
            e.state = kSkipped;
            continue;
        }

        int at = onset <= base ? 0 : static_cast<int>((onset - base) * kRate);

        if (at >= n) {
            at = n - 1;
        }
        e.state = kStarted;
        e.onset = base + double(at) / kRate;

        if (e.voices > 0) {
            starts[ns] = {at, ns};
            local[ns] = e;
            ++ns;
        }
    }

    ReleaseSRWLockExclusive(&g_vg.schedLock);

    return ns;
}

void WriteOutput(const Buffer& b, const float* mix) {
    if (g_vg.cfg.mix) {
        for (int i = 0; i < b.n; ++i) {
            b.data[i] += mix[i];
        }
    } else {
        std::memcpy(b.data, mix, sizeof(float) * b.n);
    }

    *reinterpret_cast<uint16_t*>(b.hdr + 0xE) = static_cast<uint16_t>(b.n);
}
}  // namespace

void InputImpl(void* dev, void* akbuf) {
    Buffer b;
    if (!ReadBuffer(dev, akbuf, &b)) {
        return;
    }

    if (InterlockedExchange(&g_vg.flush, 0)) {
        g_vg.player.ReleaseAll();
        g_vg.lastBase = -1;
    }

    const DWORD now = GetTickCount();
    double base = 0, master = 0;
    bool mapped = false;
    if (!Playing(b.inputId, now)) {
        return;
    }
    if (!BufferBase(b.inputId, &base, &master, &mapped)) {
        return;
    }

    if (g_vg.lastBase >= 0 && base < g_vg.lastBase - 0.25) {
        g_vg.player.ReleaseAll();
    }

    g_vg.lastBase = base;
    ++g_vg.buffers;
    Onset starts[32];
    Event local[32];
    const int ns = CollectOnsets(base, b.n, starts, local);
    float* mix = g_vg.mixBuf;

    g_vg.player.Render(mix, b.n, starts, ns, local, g_vg.cfg.gain);
    WriteOutput(b, mix);
    if (ns) {
        g_vg.traceUntil = base + 0.6;
    }
    if (g_vg.cfg.trace && base < g_vg.traceUntil && (g_vg.buffers % 4) == 0) {
        TraceDetector(b.inputId, base, Rms(mix, b.n));
    }

    if (now - g_vg.lastStatMs > 5000) {
        g_vg.lastStatMs = now;
        Rs7Log("vguitar: audio: %u buffers of %d (valid was %d), base %.4f %s master %.4f (diff %+.1f ms), "
               "%u voices started, %d clock fallbacks, rms %.4f",
               g_vg.buffers, b.n, b.valid, base, mapped ? "(detector map)" : "(master)", master,
               (base - master) * 1000.0, g_vg.player.Started(), g_vg.clockFallbacks, Rms(mix, b.n));
    }
}

}  // namespace rs7vg
