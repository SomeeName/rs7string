#pragma once
#include <cstdint>

// Virtual guitarist synth (vguit.h): plucked-string voices for the game's 48 kHz mono float input
// buffer. Pure code (no Win32, no game memory): unit-tested in tests/test_vgsynth.cpp.
//
// A voice is a Karplus-Strong string with a fractional, time-varying delay (3rd-order Lagrange
// read), so its pitch can glide (bends, slides, vibrato) without re-plucking:
//   y[n] = x[n] + rho * (y[n-L] + y[n-L-1]) / 2,   L = fs / f(t) - 0.5 (the average adds half a sample)
//   x    = one period of the plucked-string spectrum |sin(k pi beta)| / k (beta = 0.13, random phases,
//          up to 5 kHz) plus 5 % pick noise
//   rho  = per-period loss chosen so the fundamental decays by kDecayDbPerSec at any pitch
// Output passes a DC blocker, an attack (1 ms) and a release ramp (kReleaseSec) once the voice is
// let go. The fundamental is present (low B 61.7 Hz: L = 777.5 samples, the 4096-sample line
// holds down to 11.7 Hz).
namespace rs7vg {

constexpr int kRate = 48000;
constexpr int kLine = 4096;             // delay line (power of two)
constexpr float kDecayDbPerSec = 6.0f;  // fundamental decay while held
constexpr float kReleaseSec = 0.03f;    // fade after Release()
constexpr float kAttackSec = 0.001f;
constexpr double kBeta = 0.13;         // pluck position (fraction of the string from the bridge)
constexpr double kExciteMaxHz = 5000;  // highest harmonic of the excitation
constexpr float kPickNoise = 0.05f;    // pick noise relative to the harmonic peak

// MIDI note -> Hz (A4 = 69 = 440 Hz); fractional MIDI allowed.
double MidiToHz(double midi);

// Pitch offset (semitones) of a voice at `t` seconds after its onset: bend curve, slide and
// vibrato. All fields optional (count 0 / slide 0 / vibrato 0 = a plain note).
struct Glide {
    int bendCount = 0;                   // points of the bend curve
    float bendT[32] = {};                // seconds after the onset, ascending
    float bendStep[32] = {};             // semitones (SNG BendValue.step: 1 = half step, 2 = whole step)
    float slideSemis = 0;                // pitched/unpitched slide target - start, semitones
    float slideStart = 0, slideEnd = 0;  // seconds after the onset (the slide's span)
    float vibratoSemis = 0;              // vibrato depth (0 = off)
    float vibratoHz = 5.5f;
};

// The offset in semitones at `t` (piecewise-linear bend from (0, 0) through the points, holding the
// last; linear slide over [slideStart, slideEnd]; sine vibrato).
double GlideAt(const Glide& g, double t);

class Voice {
public:
    // Plucks the string at `midi` with peak level ~`level` (0..1). `seed` makes the noise repeatable.
    void Start(double midi, float level, uint32_t seed, const Glide& glide);
    // Starts the release ramp (the voice ends kReleaseSec later). Idempotent.
    void Release();

    // Releases by itself once Age() reaches `samples` (call after Start; < 0 = hold until Release).
    void SetHold(int64_t samples) { hold_ = samples; }

    // Adds `n` samples to `out` (mix). Does nothing once the voice has ended.
    void Render(float* out, int n);

    bool Active() const { return active_; }

    // Samples since Start (the onset is sample 0).
    int64_t Age() const { return age_; }

    double Midi() const { return midi_; }

private:
    float line_[kLine] = {};
    int w_ = 0;  // next write index
    int64_t age_ = 0;
    double midi_ = 0;
    float level_ = 0;
    float rho_ = 1;
    float excite_[kLine] = {};  // one period of excitation, fed in over the first `exciteLen_` samples
    int exciteLen_ = 0;
    float dcX_ = 0, dcY_ = 0;
    bool active_ = false;
    bool released_ = false;
    int64_t releaseAt_ = 0;
    int64_t hold_ = -1;
    Glide glide_;
};

// Peak-normalised RMS / pitch helpers for the tests and the in-game self-check log.
double Rms(const float* x, int n);
// Fundamental frequency by normalised autocorrelation over lags fs/fmax..fs/fmin (0 = none found).
double EstimateHz(const float* x, int n, double fmin, double fmax);

}  // namespace rs7vg
