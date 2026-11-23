/**
 * The morph filter: a cascade of up to four topology-preserving state-variable
 * stages whose one Shape control morphs
 *
 *   LP 8p → LP 4p → LP 2p → LP Shelf → BP → HP Shelf → HP 2p → HP 4p
 *   → HP 8p → Notch
 *
 * Shape     — 0..SHAPE_MAX; whole numbers are the stops, a fraction blends
 *             its two neighbours (filterMorphMath.hpp).
 * Cutoff    — 20 Hz..20 kHz.
 * Resonance — 0..1, on the last stage only, so there is one peak at cutoff
 *             whatever the order.
 * Level     — the output gain, 0..1.
 *
 * Stages are Butterworth-aligned rather than identical, so cutoff is the
 * −3 dB point at every order. What the cascade is at any moment is a
 * MorphState: how many stages run, what each feeds the next, and three taps (a
 * response read at a stage) whose weighted sum is the output.
 *
 * setParams() is block-rate. process() glides the coefficients and tap
 * weights from the last block's setting to this one across the block, so
 * sweeps don't zipper. When a block moves the taps to other stages (crossing
 * a stop) the weights snap rather than glide — the outgoing tap is at nothing
 * and the incoming at unity there, so the snap is silent — and stages that
 * stop running are cleared, so they resume from rest.
 */
#pragma once

#include <algorithm>
#include <cmath>

#include "../util/math.hpp"
#include "filterMorphMath.hpp"

namespace dsp {

constexpr double CUTOFF_MIN_HZ = 20;
constexpr double CUTOFF_MAX_HZ = 20000;

class MorphFilter {
 public:
  void prepare(double sampleRate) {
    this->sampleRate = sampleRate;
    level.prepare(sampleRate);
    reset();
  }

  /** Silence the stages; the next block jumps straight to the setting. */
  void reset() noexcept {
    std::fill(std::begin(ic1), std::end(ic1), 0.0f);
    std::fill(std::begin(ic2), std::end(ic2), 0.0f);
    level.reset(levelTarget);
    fresh = true;
  }

  /** shape 0..SHAPE_MAX, cutoff in Hz, resonance 0..1, level 0..1. Block-rate. */
  void setParams(double shape, double cutoffHz, double resonance, double level = 1) noexcept {
    this->shape = clamp(shape, 0, SHAPE_MAX);
    cutoff = clamp(cutoffHz, CUTOFF_MIN_HZ, CUTOFF_MAX_HZ);
    this->resonance = clamp(resonance, 0, 1);
    levelTarget = clamp(level, 0, 1);
  }

  /** Filters n samples of `buf` from `offset`, in place. */
  void process(float* buf, int offset, int n) DSP_NONBLOCKING {
    if (fresh) {
      fresh = false;
      jump();
      level.reset(levelTarget);
    } else {
      glide(n);
    }
    for (int i = 0; i < n; i++) buf[offset + i] = static_cast<float>(tick(buf[offset + i]) * level.step(levelTarget));
    flushDenormals();
  }

  /** One sample through the cascade at the current coefficients (no level). */
  double tick(double x) noexcept {
    double out = x;
    double tapA = 0;
    double tapB = 0;
    double tapC = 0;
    const int n = stages;
    for (int s = 0; s < n; s++) {
      a1[s] += da1[s];
      a2[s] += da2[s];
      a3[s] += da3[s];
      k[s] += dk[s];

      const double i1 = ic1[s];
      const double i2 = ic2[s];
      const double v3 = out - i2;
      const double v1 = a1[s] * i1 + a2[s] * v3;
      const double v2 = i2 + a2[s] * i1 + a3[s] * v3;
      ic1[s] = static_cast<float>(2 * v1 - i1);
      ic2[s] = static_cast<float>(2 * v2 - i2);

      // The stage's four responses: lowpass, unity-peak bandpass, highpass, notch.
      const double kv1 = k[s] * v1;
      if (s == stageA) tapA = pick(typeA, out, v2, kv1);
      if (s == stageB) tapB = pick(typeB, out, v2, kv1);
      if (s == stageC) tapC = pick(typeC, out, v2, kv1);
      out = pick(chain, out, v2, kv1);
    }
    wA += dwA;
    wB += dwB;
    wC += dwC;
    return wA * tapA + wB * tapB + wC * tapC;
  }

  /** Jump straight to the setting, with no glide. */
  void jump() noexcept {
    compute(a1, a2, a3, k);
    std::fill(std::begin(da1), std::end(da1), 0.0f);
    std::fill(std::begin(da2), std::end(da2), 0.0f);
    std::fill(std::begin(da3), std::end(da3), 0.0f);
    std::fill(std::begin(dk), std::end(dk), 0.0f);
    applyTaps(0);
  }

 private:
  /** Below this, filter state is inaudible and is flushed to zero. */
  static constexpr double DENORMAL = 1e-25;

  double sampleRate = 48000;

  double shape = 2;
  double cutoff = 1000;
  double resonance = 0;
  Smoother level;
  double levelTarget = 1;
  /** The first block after prepare() or reset() jumps to its setting instead of gliding. */
  bool fresh = true;

  MorphState morph;
  /* the taps in force, to notice a change */
  int stages = 1;
  Tap chain = Tap::Lp;
  Tap typeA = Tap::Lp;
  int stageA = 0;
  Tap typeB = Tap::Lp;
  int stageB = 0;
  Tap typeC = Tap::Lp;
  int stageC = 0;
  double wA = 1;
  double wB = 0;
  double wC = 0;
  double dwA = 0;
  double dwB = 0;
  double dwC = 0;

  float ic1[MAX_STAGES] = {};
  float ic2[MAX_STAGES] = {};

  // Per-stage coefficients, plus the per-sample delta that glides them.
  float a1[MAX_STAGES] = {};
  float a2[MAX_STAGES] = {};
  float a3[MAX_STAGES] = {};
  float k[MAX_STAGES] = {};
  float da1[MAX_STAGES] = {};
  float da2[MAX_STAGES] = {};
  float da3[MAX_STAGES] = {};
  float dk[MAX_STAGES] = {};

  /** A stage's response of the given kind from its input, its lowpass and k × bandpass. */
  static double pick(Tap tap, double x, double lp, double kbp) noexcept {
    switch (tap) {
      case Tap::Hp: return x - kbp - lp;
      case Tap::Bp: return kbp;
      case Tap::Notch: return x - kbp;
      case Tap::Dry: return x;
      default: return lp;
    }
  }

  /** Glide from the current coefficients to the setting over `n` samples. */
  void glide(int n) noexcept {
    const double inv = n > 0 ? 1.0 / n : 0;
    compute(da1, da2, da3, dk);
    for (int s = 0; s < MAX_STAGES; s++) {
      da1[s] = static_cast<float>((static_cast<double>(da1[s]) - a1[s]) * inv);
      da2[s] = static_cast<float>((static_cast<double>(da2[s]) - a2[s]) * inv);
      da3[s] = static_cast<float>((static_cast<double>(da3[s]) - a3[s]) * inv);
      dk[s] = static_cast<float>((static_cast<double>(dk[s]) - k[s]) * inv);
    }
    applyTaps(n);
  }

  /**
   * Zero out integrator state that has decayed into denormal range: inaudible, but it can cost a large multiple of
   * normal arithmetic on some CPUs. Once per block.
   */
  void flushDenormals() noexcept {
    for (int s = 0; s < MAX_STAGES; s++) {
      if (ic1[s] > -DENORMAL && ic1[s] < DENORMAL) ic1[s] = 0;
      if (ic2[s] > -DENORMAL && ic2[s] < DENORMAL) ic2[s] = 0;
    }
  }

  /** Writes the setting's coefficients into the supplied arrays and sets the morph. */
  void compute(float* oa1, float* oa2, float* oa3, float* kOut) noexcept {
    const double nyq = sampleRate * 0.5;
    double fc = cutoff;
    if (fc < 5) fc = 5;
    else if (fc > nyq * 0.49) fc = nyq * 0.49;

    const double g = std::tan((PI * fc) / sampleRate);
    MorphState& m = morph;
    morphInto(shape, resonance, m);

    for (int s = 0; s < m.stages; s++) {
      const double ks = m.k[s];
      kOut[s] = static_cast<float>(ks);
      const double d = 1 / (1 + g * (g + ks));
      oa1[s] = static_cast<float>(d);
      oa2[s] = static_cast<float>(g * d);
      oa3[s] = static_cast<float>(g * g * d);
    }
  }

  /**
   * Take the morph's taps and weights into force: gliding the weights over
   * `n` samples while the taps stand, snapping them when the taps move.
   */
  void applyTaps(int n) noexcept {
    const MorphState& m = morph;
    const bool moved = m.stages != stages || m.chain != chain
        || m.typeA != typeA || m.stageA != stageA
        || m.typeB != typeB || m.stageB != stageB
        || m.typeC != typeC || m.stageC != stageC;
    if (moved) {
      // Stages that stop running resume from rest when they come back.
      for (int s = m.stages; s < stages; s++) { ic1[s] = 0; ic2[s] = 0; }
      stages = m.stages;
      chain = m.chain;
      typeA = m.typeA; stageA = m.stageA;
      typeB = m.typeB; stageB = m.stageB;
      typeC = m.typeC; stageC = m.stageC;
    }
    if (moved || n <= 0) {
      wA = m.wA; wB = m.wB; wC = m.wC;
      dwA = 0; dwB = 0; dwC = 0;
    } else {
      const double inv = 1.0 / n;
      dwA = (m.wA - wA) * inv;
      dwB = (m.wB - wB) * inv;
      dwC = (m.wC - wC) * inv;
    }
  }
};

}  // namespace dsp
