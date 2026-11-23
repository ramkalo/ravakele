/**
 * One topology-preserving-transform state-variable filter stage at Butterworth Q (1/√2), the building block of a
 * Linkwitz-Riley 24 dB/oct crossover: two stages cascaded per band, the low band's low-pass outputs and the high band's
 * high-pass outputs, and low + high is an allpass (flat magnitude, each band −6 dB at the crossover). The stage stays
 * stable while its coefficients move, so a crossover can glide or jump. Coefficients come from `coefficients(g)` with
 * g = tan(π · fc / sampleRate). Used wherever a crossover splits a signal (fx.crossover-split, fx.room-reverb, fx.rotary-speaker).
 */
#pragma once

namespace dsp {
namespace svf {

/** 1 / Q for a Butterworth stage. */
constexpr double BUTTERWORTH_K = 1.41421356237309504880;

/** A stage's coefficients for one prewarped cutoff g. */
struct Coefficients {
  double a1 = 0;
  double a2 = 0;
  double a3 = 0;
};

inline Coefficients coefficients(double g) noexcept {
  const double a1 = 1 / (1 + g * (g + BUTTERWORTH_K));
  const double a2 = g * a1;
  return {a1, a2, g * a2};
}

/** One stage: its two integrator states and its last band-pass output. */
struct Stage {
  double s1 = 0;
  double s2 = 0;
  double band = 0;

  void reset() noexcept { s1 = s2 = band = 0; }

  /** One tick: the low-pass output; the band-pass output is kept on the stage. */
  double low(double x, const Coefficients& c) noexcept {
    const double v3 = x - s2;
    const double v1 = c.a1 * s1 + c.a2 * v3;
    const double v2 = s2 + c.a2 * s1 + c.a3 * v3;
    s1 = 2 * v1 - s1;
    s2 = 2 * v2 - s2;
    band = v1;
    return v2;
  }

  /** One tick: the high-pass output, x − k·band − low. */
  double high(double x, const Coefficients& c) noexcept {
    const double lp = low(x, c);
    return x - BUTTERWORTH_K * band - lp;
  }
};

}  // namespace svf
}  // namespace dsp
