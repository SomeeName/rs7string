#include "features/vguit/synth.h"
#include <cmath>

namespace rs7vg {
namespace {
constexpr double kPi = 3.14159265358979323846;

uint32_t Xorshift(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;

    return s;
}
}  // namespace

double MidiToHz(double midi) {
    return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
}

double GlideAt(const Glide& g, double t) {
    double semis = 0;

    if (g.bendCount > 0) {
        // Piecewise linear from (0, 0) through the points; the last value holds.
        double t0 = 0, s0 = 0, b = 0;
        bool done = false;

        for (int i = 0; i < g.bendCount && i < 32; ++i) {
            const double t1 = g.bendT[i], s1 = g.bendStep[i];

            if (t < t1) {
                b = t1 > t0 ? s0 + (s1 - s0) * (t - t0) / (t1 - t0) : s1;
                done = true;
                break;
            }

            t0 = t1;
            s0 = s1;
        }

        semis += done ? b : s0;
    }

    if (g.slideSemis != 0 && t > g.slideStart) {
        const double span = g.slideEnd - g.slideStart;
        const double k = span > 0 ? (t - g.slideStart) / span : 1.0;

        semis += g.slideSemis * (k > 1 ? 1 : k);
    }

    if (g.vibratoSemis != 0) {
        semis += g.vibratoSemis * std::sin(2 * kPi * g.vibratoHz * t);
    }

    return semis;
}

void Voice::Start(double midi, float level, uint32_t seed, const Glide& glide) {
    for (float& v : line_) {
        v = 0;
    }

    w_ = 0;
    age_ = 0;
    midi_ = midi;
    level_ = level;
    glide_ = glide;
    dcX_ = dcY_ = 0;
    released_ = false;
    releaseAt_ = 0;
    hold_ = -1;
    const double hz = MidiToHz(midi);

    // Per-period loss for kDecayDbPerSec at this pitch (the 2-tap average adds its own HF loss).
    rho_ = static_cast<float>(std::pow(10.0, -kDecayDbPerSec / 20.0 / hz));
    // One period of the plucked-string spectrum: harmonic k has amplitude |sin(k pi beta)| / k
    // (pluck kBeta from the bridge, -6 dB/octave), a random phase, up to kExciteMaxHz; plus a little
    // pick noise. A single period of plain noise would give every harmonic a random level: the
    // first in-game run (low notes detected late, the detector's early candidates were harmonics
    // 10-13 of the low B) had exactly that.
    int len = static_cast<int>(kRate / hz + 0.5);

    if (len < 4) {
        len = 4;
    }
    if (len > kLine - 8) {
        len = kLine - 8;
    }
    uint32_t s = seed ? seed : 0x9E3779B9u;

    for (int i = 0; i < len; ++i) {
        excite_[i] = 0;
    }

    const int harmonics = static_cast<int>(kExciteMaxHz / hz);

    for (int k = 1; k <= harmonics; ++k) {
        const double amp = std::fabs(std::sin(k * kPi * kBeta)) / k;
        const double w = 2 * kPi * k / len;
        const double phase = 2 * kPi * static_cast<double>(Xorshift(s) & 0xFFFF) / 65536.0;
        // Recursive oscillator: sin(phase + i w) without a sin() per sample.
        double s0 = std::sin(phase - w), s1 = std::sin(phase);
        const double c2 = 2 * std::cos(w);

        for (int i = 0; i < len; ++i) {
            excite_[i] += static_cast<float>(amp * s1);
            const double s2 = c2 * s1 - s0;

            s0 = s1;
            s1 = s2;
        }
    }

    float peakH = 0;

    for (int i = 0; i < len; ++i) {
        peakH = std::fabs(excite_[i]) > peakH ? std::fabs(excite_[i]) : peakH;
    }

    for (int i = 0; i < len; ++i) {
        excite_[i] += kPickNoise * peakH * (static_cast<float>(Xorshift(s) & 0xFFFF) / 32767.5f - 1.0f);
    }

    double mean = 0;

    for (int i = 0; i < len; ++i) {
        mean += excite_[i];
    }

    mean /= len;
    float peak = 0;

    for (int i = 0; i < len; ++i) {
        excite_[i] -= static_cast<float>(mean);
        peak = std::fabs(excite_[i]) > peak ? std::fabs(excite_[i]) : peak;
    }

    const float norm = peak > 0 ? 1.0f / peak : 0.0f;

    for (int i = 0; i < len; ++i) {
        excite_[i] *= norm;
    }

    exciteLen_ = len;
    active_ = true;
}

void Voice::Release() {
    if (!active_ || released_) {
        return;
    }

    released_ = true;
    releaseAt_ = age_;
}

void Voice::Render(float* out, int n) {
    if (!active_) {
        return;
    }

    constexpr int mask = kLine - 1;
    const float attack = kAttackSec * kRate, release = kReleaseSec * kRate;
    double delay = 0;

    for (int i = 0; i < n; ++i) {
        if ((age_ & 15) == 0 || i == 0) {  // pitch update every 16 samples (0.33 ms)
            const double t = static_cast<double>(age_) / kRate;

            delay = kRate / MidiToHz(midi_ + GlideAt(glide_, t)) - 0.5;
            if (delay < 3) {
                delay = 3;
            }
            if (delay > kLine - 8) {
                delay = kLine - 8;
            }
        }

        // y[n-D] and y[n-D-1] by 3rd-order Lagrange interpolation.
        float taps[2];

        for (int k = 0; k < 2; ++k) {
            const double p = static_cast<double>(w_) - (delay + k);
            const double fl = std::floor(p);
            const float d = static_cast<float>(p - fl);
            const int i0 = static_cast<int>(fl);
            const float xm1 = line_[(i0 - 1) & mask], x0 = line_[i0 & mask], x1 = line_[(i0 + 1) & mask],
                        x2 = line_[(i0 + 2) & mask];
            const float cm1 = -d * (d - 1) * (d - 2) / 6, c0 = (d + 1) * (d - 1) * (d - 2) / 2,
                        c1 = -(d + 1) * d * (d - 2) / 2, c2 = (d + 1) * d * (d - 1) / 6;

            taps[k] = cm1 * xm1 + c0 * x0 + c1 * x1 + c2 * x2;
        }

        float y = rho_ * 0.5f * (taps[0] + taps[1]);

        if (age_ < exciteLen_) {
            y += excite_[age_];
        }
        line_[w_ & mask] = y;
        ++w_;
        // DC blocker, then the envelope.
        const float o = y - dcX_ + 0.995f * dcY_;

        dcX_ = y;
        dcY_ = o;

        if (!released_ && hold_ >= 0 && age_ >= hold_) {
            released_ = true;
            releaseAt_ = age_;
        }

        float env = age_ < attack ? static_cast<float>(age_) / attack : 1.0f;

        if (released_) {
            const float r = 1.0f - static_cast<float>(age_ - releaseAt_) / release;

            if (r <= 0) {
                active_ = false;
                ++age_;
                return;
            }

            env *= r;
        }

        out[i] += o * level_ * env;
        ++age_;
    }
}

double Rms(const float* x, int n) {
    double s = 0;

    for (int i = 0; i < n; ++i) {
        s += double(x[i]) * x[i];
    }

    return n > 0 ? std::sqrt(s / n) : 0.0;
}

double EstimateHz(const float* x, int n, double fmin, double fmax) {
    const int lo = static_cast<int>(kRate / fmax), hi = static_cast<int>(kRate / fmin) + 1;
    if (hi * 2 > n || lo < 2) {
        return 0;
    }

    const int len = n - hi;
    double best = 0;
    static double r[kLine + 2];
    if (hi > kLine) {
        return 0;
    }

    for (int lag = lo; lag <= hi; ++lag) {
        double xy = 0, xx = 0, yy = 0;

        for (int i = 0; i < len; ++i) {
            xy += double(x[i]) * x[i + lag];
            xx += double(x[i]) * x[i];
            yy += double(x[i + lag]) * x[i + lag];
        }

        r[lag] = xx > 0 && yy > 0 ? xy / std::sqrt(xx * yy) : 0;
        if (r[lag] > best) {
            best = r[lag];
        }
    }

    if (best < 0.5) {
        return 0;
    }

    // The first local maximum within 90 % of the best (the period, not a multiple of it).
    for (int lag = lo + 1; lag < hi; ++lag) {
        if (r[lag] >= 0.9 * best && r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1]) {
            const double a = r[lag - 1], b = r[lag], c = r[lag + 1];
            const double den = a - 2 * b + c;
            const double off = den != 0 ? 0.5 * (a - c) / den : 0;

            return kRate / (lag + off);
        }
    }

    return 0;
}

}  // namespace rs7vg
