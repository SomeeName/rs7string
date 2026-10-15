#include "core/vgsynth.h"
#include "test.h"
#include <cmath>
#include <vector>

using namespace rs7vg;

namespace {
Voice g_v;  // 32 KB: not on the stack

std::vector<float> RenderNote(double midi, float level, double seconds, const Glide& g = Glide{}, int64_t hold = -1) {
    std::vector<float> out(static_cast<size_t>(seconds * kRate), 0.0f);

    g_v.Start(midi, level, 1234, g);
    if (hold >= 0) {
        g_v.SetHold(hold);
    }

    for (size_t pos = 0; pos < out.size(); pos += 1024) {  // in Wwise-sized blocks
        const int n = static_cast<int>(out.size() - pos < 1024 ? out.size() - pos : 1024);

        g_v.Render(out.data() + pos, n);
    }

    return out;
}

double HzAt(const std::vector<float>& x, double t0, double len, double fmin, double fmax) {
    const size_t a = static_cast<size_t>(t0 * kRate);

    return EstimateHz(x.data() + a, static_cast<int>(len * kRate), fmin, fmax);
}

bool Near(double a, double b, double rel) {
    return std::fabs(a - b) <= rel * std::fabs(b);
}
}  // namespace

TEST(vgsynth_midi_to_hz) {
    CHECK(Near(MidiToHz(69), 440.0, 1e-12));
    CHECK(Near(MidiToHz(35), 61.7354, 1e-4));  // low B (B1)
    CHECK(Near(MidiToHz(40), 82.4069, 1e-4));  // low E
}

TEST(vgsynth_low_b_has_its_fundamental) {
    const auto x = RenderNote(35, 0.3f, 1.0);
    const double hz = HzAt(x, 0.2, 0.3, 40, 400);

    std::printf("  low B: %.3f Hz (61.735)\n", hz);
    CHECK(Near(hz, 61.7354, 0.005));
}

TEST(vgsynth_pitch_across_the_range) {
    for (int midi : {38, 40, 42, 45, 50, 55, 59, 64, 71, 76}) {
        const auto x = RenderNote(midi, 0.3f, 0.6);
        const double hz = HzAt(x, 0.1, 0.25, 40, 1200);

        if (!Near(hz, MidiToHz(midi), 0.005)) {
            std::printf("  midi %d: %.3f Hz, want %.3f\n", midi, hz, MidiToHz(midi));
        }
        CHECK(Near(hz, MidiToHz(midi), 0.005));
    }
}

TEST(vgsynth_level_is_moderate) {
    const auto x = RenderNote(35, 0.3f, 1.0);
    float peak = 0;

    for (float s : x) {
        peak = std::fabs(s) > peak ? std::fabs(s) : peak;
    }

    const double rms = Rms(x.data(), kRate / 2);
    const double db = 20 * std::log10(rms);

    std::printf("  low B at gain 0.3: peak %.3f, rms %.1f dBFS (first 0.5 s)\n", peak, db);
    CHECK(peak > 0.1f && peak < 0.6f);
    CHECK(db > -32 && db < -12);
}

TEST(vgsynth_hold_then_release_ends_the_voice) {
    const auto x = RenderNote(45, 0.3f, 0.5, Glide{}, static_cast<int64_t>(0.2 * kRate));

    CHECK(Rms(x.data() + static_cast<int>(0.1 * kRate), 2400) > 0.01);
    const int after = static_cast<int>((0.2 + kReleaseSec) * kRate) + 2;
    double tail = 0;

    for (size_t i = after; i < x.size(); ++i) {
        tail += std::fabs(x[i]);
    }

    CHECK(tail == 0.0);
    CHECK(!g_v.Active());
}

TEST(vgsynth_bend_glides_up_without_a_new_pluck) {
    Glide g;

    g.bendCount = 2;
    g.bendT[0] = 0.2f;
    g.bendStep[0] = 0.0f;
    g.bendT[1] = 0.4f;
    g.bendStep[1] = 2.0f;  // whole step: 40 -> 42
    const auto x = RenderNote(40, 0.3f, 1.0, g);
    const double before = HzAt(x, 0.05, 0.12, 40, 400), after = HzAt(x, 0.5, 0.25, 40, 400);

    std::printf("  bend: %.2f Hz -> %.2f Hz (%.2f -> %.2f)\n", before, after, MidiToHz(40), MidiToHz(42));
    CHECK(Near(before, MidiToHz(40), 0.006));
    CHECK(Near(after, MidiToHz(42), 0.006));
}

TEST(vgsynth_glide_curve) {
    Glide g;

    g.bendCount = 2;
    g.bendT[0] = 0.1f;
    g.bendStep[0] = 1.0f;
    g.bendT[1] = 0.3f;
    g.bendStep[1] = 0.0f;
    CHECK(std::fabs(GlideAt(g, 0.0)) < 1e-9);
    CHECK(std::fabs(GlideAt(g, 0.05) - 0.5) < 1e-6);  // (0,0) -> (0.1,1)
    CHECK(std::fabs(GlideAt(g, 0.2) - 0.5) < 1e-6);   // (0.1,1) -> (0.3,0)
    CHECK(std::fabs(GlideAt(g, 1.0)) < 1e-6);         // holds the last
    Glide s;

    s.slideSemis = 5;
    s.slideStart = 0;
    s.slideEnd = 1;
    CHECK(std::fabs(GlideAt(s, 0.5) - 2.5) < 1e-6);
    CHECK(std::fabs(GlideAt(s, 3.0) - 5.0) < 1e-6);
    Glide v;

    v.vibratoSemis = 0.3f;
    double mx = 0;

    for (int i = 0; i < 1000; ++i) {
        mx = std::fmax(mx, std::fabs(GlideAt(v, i / 1000.0)));
    }

    CHECK(mx > 0.29 && mx <= 0.3 + 1e-6);
}

TEST(vgsynth_is_repeatable) {
    const auto a = RenderNote(50, 0.3f, 0.1), b = RenderNote(50, 0.3f, 0.1);

    CHECK(a == b);
}

TEST(vgsynth_low_notes_have_a_falling_harmonic_spectrum) {
    // Regression for the first in-game run: the strongest partial of a low note must be one of the
    // first three harmonics (the detector locked onto harmonics 10-13 of the low B before).
    for (int midi : {35, 38, 40, 42}) {
        const auto x = RenderNote(midi, 0.3f, 0.3);
        const double f0 = MidiToHz(midi);
        double best = 0;
        int bestK = 0;

        for (int k = 1; k <= 16; ++k) {  // Goertzel-style single-bin DFT over 0.05..0.25 s
            double re = 0, im = 0;
            const int a = kRate / 20, n = kRate / 5;

            for (int i = 0; i < n; ++i) {
                const double ph = 2 * 3.14159265358979 * k * f0 * i / kRate;

                re += x[a + i] * std::cos(ph);
                im += x[a + i] * std::sin(ph);
            }

            const double mag = std::sqrt(re * re + im * im);

            if (mag > best) {
                best = mag;
                bestK = k;
            }
        }

        if (bestK > 3) {
            std::printf("  midi %d: strongest harmonic %d\n", midi, bestK);
        }
        CHECK(bestK >= 1 && bestK <= 3);
    }
}
