// The audio thread's voice pool: renders one input buffer of synthesized guitar with the onsets of the scheduled
// notes. Values in, values out (tests/vguit).
#include "features/vguit/vguit.h"
#include <cmath>
#include <cstring>
#include <utility>

namespace rs7vg {

void Player::ReleaseAll() {
    for (Voice& v : voice_) {
        v.Release();
    }
}

void Player::Start(const Event& e, float gain) {
    // Mono guitarist: a new onset damps the voices of earlier notes (not those of the same instant).
    for (int v = 0; v < kVoices; ++v) {
        if (voice_[v].Active() && voiceTime_[v] < e.time - 0.005f) {
            voice_[v].Release();
        }
    }

    const float hold = (e.sustain > 0 ? e.sustain : 0.0f) + 0.30f;
    const float level = gain / (e.voices > 1 ? std::sqrt(float(e.voices)) : 1.0f);

    for (int j = 0; j < e.voices; ++j) {
        int slotV = -1;

        for (int v = 0; v < kVoices; ++v) {
            if (!voice_[v].Active()) {
                slotV = v;
                break;
            }
        }

        if (slotV < 0) {  // steal the oldest
            int64_t age = -1;

            for (int v = 0; v < kVoices; ++v) {
                if (voice_[v].Age() > age) {
                    age = voice_[v].Age();
                    slotV = v;
                }
            }
        }

        voice_[slotV].Start(e.midi[j], level, seed_ = seed_ * 1664525u + 1013904223u, e.glide);
        voice_[slotV].SetHold(static_cast<int64_t>(hold * kRate));
        voiceTime_[slotV] = e.time;
        ++started_;
    }
}

void Player::Render(float* mix, int n, Onset* onsets, int count, const Event* events, float gain) {
    // Sort by offset (few entries).
    for (int a = 1; a < count; ++a) {
        for (int b = a; b > 0 && onsets[b].at < onsets[b - 1].at; --b) {
            std::swap(onsets[b], onsets[b - 1]);
        }
    }

    std::memset(mix, 0, sizeof(float) * n);
    int pos = 0;

    for (int k = 0; k <= count; ++k) {
        const int seg = k < count ? onsets[k].at : n;

        if (seg > pos) {
            for (Voice& v : voice_) {
                v.Render(mix + pos, seg - pos);
            }

            pos = seg;
        }

        if (k == count) {
            break;
        }

        Start(events[onsets[k].ev], gain);
    }
}

}  // namespace rs7vg
