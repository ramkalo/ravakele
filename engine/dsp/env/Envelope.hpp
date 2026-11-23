/**
 * Six-stage envelope: Delay, Attack, Hold, Decay, Sustain, Release.
 *
 * Delay, Attack, Hold, Decay, Release — stage times in seconds, STAGE_MIN..STAGE_MAX.
 * Sustain     — the plateau's level, 0..1. A change while a note sounds glides there over SUSTAIN_SMOOTH_MS, so a
 *               moved sustain never steps the level it shapes.
 * Attack, Decay and Release curves — 0 (linear) to 1 (the most exponential); every stage bows below its straight
 *               line: the attack rises slowly, then steeply; the decay and the release fall fast, then tail off. Each
 *               is baked into its own small table when it moves (envelopeCurves.hpp).
 * Sustain curve — 0..1: as it rises the sustain plateau wobbles more, a triangle from 1 Hz to 20 Hz and from nothing
 *               to ±10% of full level. Attack still covers the whole 0..1; only the plateau moves.
 * Intensity   — scales the whole output, 0..1.
 *
 * An ADSR is this envelope with Delay and Hold at STAGE_MIN.
 *
 * No allocation after construction; setParams() is block-rate and rebakes a curve table only when that curve changed.
 */
#pragma once

#include <cmath>

#include "../util/math.hpp"
#include "envelopeCurves.hpp"

namespace dsp {

/** The shortest and longest stage, in seconds. */
constexpr double STAGE_MIN = 0.001;
constexpr double STAGE_MAX = 20;

/** The glide of the plateau to a moved Sustain, in ms (a one-pole time constant). */
constexpr double SUSTAIN_SMOOTH_MS = 20;

/** Sustain wobble at curve = 1, as a fraction of full level. */
constexpr double SUSTAIN_WIGGLE_DEPTH = 0.1;
/** Sustain wobble rate at curve = 0 and at curve = 1, in Hz; exponential between. */
constexpr double SUSTAIN_WIGGLE_HZ_MIN = 1;
constexpr double SUSTAIN_WIGGLE_HZ_MAX = 20;

namespace EgStage {
enum : int {
  Idle = 0,
  Delay = 1,
  Attack = 2,
  Hold = 3,
  Decay = 4,
  Sustain = 5,
  Release = 6,
};
}

class Envelope {
 public:
  void prepare(double sampleRate) {
    this->sampleRate = sampleRate;
    reset();
  }

  void reset() noexcept {
    stage = EgStage::Idle;
    pos = 0;
    len = 1;
    releaseFrom = 0;
    attackFrom = 0;
    wigPhase = 0;
    plateau = sustain;
  }

  /**
   * Stage times in seconds, sustain 0..1, the three stage curves 0..1, the sustain curve 0..1 and intensity 0..1.
   * Block-rate. A stage's new time applies from its next start.
   */
  void setParams(double delay, double attack, double hold, double decay, double sustain, double release,
                 double attackCurve = 0, double decayCurve = 0, double releaseCurve = 0, double sustainCurve = 0,
                 double intensity = 1) noexcept {
    const double sr = sampleRate;
    lenDelay = toSamples(clamp(delay, STAGE_MIN, STAGE_MAX), sr);
    lenAttack = toSamples(clamp(attack, STAGE_MIN, STAGE_MAX), sr);
    lenHold = toSamples(clamp(hold, STAGE_MIN, STAGE_MAX), sr);
    lenDecay = toSamples(clamp(decay, STAGE_MIN, STAGE_MAX), sr);
    lenRelease = toSamples(clamp(release, STAGE_MIN, STAGE_MAX), sr);
    this->sustain = clamp(sustain, 0, 1);
    plateauKeep = std::exp(-1 / (sr * SUSTAIN_SMOOTH_MS * 0.001));
    this->intensity = clamp(intensity, 0, 1);
    // Exponential only: the attack's table bends to a slow start (positive), the decay's and the release's to a fast
    // start (negative), so each falls fast and tails off.
    curveTables.update(CURVE_ATTACK, clamp(attackCurve, 0, 1));
    curveTables.update(CURVE_DECAY, -clamp(decayCurve, 0, 1));
    curveTables.update(CURVE_RELEASE, -clamp(releaseCurve, 0, 1));
    const double c = clamp(sustainCurve, 0, 1);
    wigDepth = SUSTAIN_WIGGLE_DEPTH * c;
    wigInc = SUSTAIN_WIGGLE_HZ_MIN * std::pow(SUSTAIN_WIGGLE_HZ_MAX / SUSTAIN_WIGGLE_HZ_MIN, c) / sr;
  }

  /** Starts from Delay. A retrigger while it still sounds holds and attacks from the current level, so it never steps. */
  void noteOn() noexcept {
    // A silent envelope starts on the Sustain set now; one still sounding keeps gliding to it.
    if (stage == EgStage::Idle) plateau = sustain;
    attackFrom = stage == EgStage::Idle ? 0 : level();
    stage = EgStage::Delay;
    pos = 0;
    len = lenDelay;
    wigPhase = 0;
  }

  void noteOff() noexcept {
    if (stage == EgStage::Idle || stage == EgStage::Release) return;
    // Release from where the plateau actually is, wobble included, so it never steps.
    releaseFrom = level();
    stage = EgStage::Release;
    pos = 0;
    len = lenRelease;
  }

  bool isActive() const noexcept { return stage != EgStage::Idle; }

  /** True once the envelope has finished releasing and output is silent. */
  bool isFinished() const noexcept { return stage == EgStage::Idle; }

  int currentStage() const noexcept { return stage; }

  /** How far through the current stage, 0..1: 0 while idle or sustaining (they have no length). */
  double stageProgress() const noexcept {
    if (stage == EgStage::Idle || stage == EgStage::Sustain) return 0;
    const double p = len > 0 ? pos / len : 0;
    return p < 0 ? 0 : p > 1 ? 1 : p;
  }

  /** Current output, including the sustain wobble and intensity. */
  double value() const noexcept { return shaped(); }

  /**
   * Per-sample output, n samples from offset (overwriting). Used where the envelope shapes a signal sample by sample,
   * an amp envelope; a modulation source can call advance() instead.
   */
  void process(float* out, int offset, int n) DSP_NONBLOCKING {
    for (int i = 0; i < n; i++) {
      out[offset + i] = static_cast<float>(shaped());
      step();
    }
  }

  /**
   * Advance without evaluating every sample, and return the value landed on.
   *
   * Modulation targets receive a start/end pair per block and interpolate between
   * them, so the curve only needs evaluating at the block edges. The state machine
   * still advances sample-accurately — it is only counters.
   */
  double advance(int n) DSP_NONBLOCKING {
    double left = n;
    while (left > 0 && stage != EgStage::Idle) {
      if (stage == EgStage::Sustain) {
        pos += left;
        left = 0;
        break;
      }
      const double remain = len - pos;
      const double take = left < remain ? left : remain;
      pos += take;
      left -= take;
      if (pos >= len) nextStage();
    }
    wigPhase += wigInc * n;
    wigPhase -= std::floor(wigPhase);
    if (plateau != sustain && n > 0) plateau += (sustain - plateau) * (1 - std::pow(plateauKeep, n));
    return shaped();
  }

 private:
  static constexpr double MIN_STAGE_SAMPLES = 1;

  double sampleRate = 48000;

  int stage = EgStage::Idle;
  double pos = 0;
  double len = 1;

  double lenDelay = 1;
  double lenAttack = 1;
  double lenHold = 1;
  double lenDecay = 1;
  double lenRelease = 1;

  double sustain = 0.7;
  double plateau = 0.7;      // where the plateau is now: glides to sustain
  double plateauKeep = 0;    // how much of the gap to sustain one sample keeps
  double intensity = 1;
  double releaseFrom = 0;
  double attackFrom = 0;

  CurveTables curveTables;
  const float* curves = curveTables.tables;
  static constexpr int curveBase = 0;

  double wigDepth = 0;
  double wigInc = 0;
  double wigPhase = 0;

  static double toSamples(double seconds, double sampleRate) noexcept {
    const double n = roundHalfUp(seconds * sampleRate);
    return n < MIN_STAGE_SAMPLES ? MIN_STAGE_SAMPLES : n;
  }

  void step() noexcept {
    if (stage == EgStage::Idle) return;
    plateau += (sustain - plateau) * (1 - plateauKeep);
    wigPhase += wigInc;
    if (wigPhase >= 1) wigPhase -= 1;
    if (stage == EgStage::Sustain) {
      pos++;
      return;
    }
    pos++;
    if (pos >= len) nextStage();
  }

  void nextStage() noexcept {
    pos = 0;
    switch (stage) {
      case EgStage::Delay:
        stage = EgStage::Attack;
        len = lenAttack;
        break;
      case EgStage::Attack:
        stage = EgStage::Hold;
        len = lenHold;
        break;
      case EgStage::Hold:
        stage = EgStage::Decay;
        len = lenDecay;
        break;
      case EgStage::Decay:
        stage = EgStage::Sustain;
        len = MIN_STAGE_SAMPLES;
        // The wobble always starts from the same place on the plateau.
        wigPhase = 0;
        break;
      case EgStage::Release:
        stage = EgStage::Idle;
        len = MIN_STAGE_SAMPLES;
        break;
      default:
        stage = EgStage::Idle;
        len = MIN_STAGE_SAMPLES;
        break;
    }
  }

  /** Envelope level before the sustain wobble and intensity. */
  double rawValue() const noexcept {
    const double p = len > 0 ? pos / len : 0;
    switch (stage) {
      case EgStage::Idle:
        return 0;
      case EgStage::Delay:
        return attackFrom;
      case EgStage::Attack:
        return attackFrom + (1 - attackFrom) * curveAt(curves, curveBase + CURVE_ATTACK * CURVE_RES, p);
      case EgStage::Hold:
        return 1;
      case EgStage::Decay:
        return 1 + (plateau - 1) * curveAt(curves, curveBase + CURVE_DECAY * CURVE_RES, p);
      case EgStage::Sustain:
        return plateau;
      case EgStage::Release:
        return releaseFrom * (1 - curveAt(curves, curveBase + CURVE_RELEASE * CURVE_RES, p));
      default:
        return 0;
    }
  }

  /** Raw level plus the sustain wobble, clamped to 0..1 — before intensity. */
  double level() const noexcept {
    double v = rawValue();
    if (stage == EgStage::Sustain && wigDepth > 0) {
      // Triangle, bipolar: -1 at phase 0 and 1, +1 at phase 0.5.
      const double tri = 1 - 4 * std::fabs(wigPhase - 0.5);
      v += wigDepth * tri;
    }
    return v < 0 ? 0 : v > 1 ? 1 : v;
  }

  /** Output: level scaled by intensity. */
  double shaped() const noexcept { return level() * intensity; }
};

}  // namespace dsp
