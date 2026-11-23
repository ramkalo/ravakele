/**
 * The wavetable oscillator: a phase accumulator reading band-limited tables.
 *
 * Shape     — 0..4, continuous. Whole numbers are the pure shapes (OSC_SHAPE_LABELS); a fraction is a crossfade between
 *             its two neighbours, so the wave morphs Sine → Triangle → Saw → Square rather than switching. A blend of
 *             two band-limited tables is still band-limited. Past Square there is nothing to crossfade to: the square
 *             narrows toward PULSE_WIDTH, so the last stretch is one shape getting thinner.
 * Width     — the square's duty, 0.01..0.99 (0.5 is a true square).
 * Octave, Semitones, Cents — the tuning from the played pitch (setPitch).
 * Level     — the output gain, 0..1.
 * Phase reset — on, noteOn() starts every note from the same point, so attacks are consistent.
 *
 * The pulse is two saw reads offset by the width and subtracted, which keeps it band-limited at any width and lets the
 * width be swept continuously. tick() takes a phase-modulation offset so the same oscillator serves as an FM operator
 * (FmOperators.hpp) without a separate code path.
 *
 * setParams() and setPitch() are block-rate; process() glides the frequency from the last block's to this one across
 * the block, and smooths the level.
 */
#pragma once

#include <cmath>
#include <limits>

#include "../util/math.hpp"
#include "wavetable.hpp"

namespace dsp {

namespace OscShape {
enum : int {
  Sin = 0,
  Tri = 1,
  Saw = 2,
  Square = 3,
  /** A narrow pulse: between Square and here the width narrows from the square's to PULSE_WIDTH. */
  Pulse = 4,
};
}
constexpr const char* OSC_SHAPE_LABELS[] = {"Sine", "Triangle", "Saw", "Square", "Pulse"};
/** The Pulse stop's width, as a fraction of the cycle; the square is 0.5. */
constexpr double PULSE_WIDTH = 0.125;

/** When a note starts the phase over: never, every note, or only a note that starts a silent voice. */
enum class PhaseReset { Free = 0, Reset = 1, FromSilence = 2 };

/** Octave reaches two octaves either way (in whole octaves); semitones two octaves either way; cents half a semitone. */
constexpr double OCTAVE_RANGE = 2;
constexpr double SEMITONE_RANGE = 24;
constexpr double CENTS_RANGE = 50;

/** A frequency `semitones` above (or below) `hz`. */
inline double transpose(double hz, double semitones) noexcept { return hz * std::pow(2.0, semitones / 12); }

class WavetableOscillator {
 public:
  void prepare(double sampleRate) {
    this->sampleRate = sampleRate;
    bank = &wavetableBank(sampleRate);
    level.prepare(sampleRate);
    reset();
  }

  void reset() noexcept {
    phase = 0;
    lastInc = std::numeric_limits<double>::quiet_NaN();
    level.reset(levelTarget);
  }

  /**
   * A note starts. Phase reset Reset starts every note from phase 0; From silence only a note that starts a silent voice
   * (`sounding` false: the amp envelope had finished), so a legato or retriggered note in a mono voice carries on
   * without a step; Free never resets. A reset starts from `startPhase` (cycles, 0..1; 0 unless a unison copy).
   */
  void noteOn(bool sounding = false, double startPhase = 0) noexcept {
    if (phaseReset == PhaseReset::Reset || (phaseReset == PhaseReset::FromSilence && !sounding)) setPhase(startPhase);
  }

  /** The played pitch in Hz (glide, bend and any pitch modulation included), each block. */
  void setPitch(double hz) noexcept { pitchHz = hz > 0 ? hz : 0; }

  /** shape 0..4, octave (whole octaves)/semitones/cents of tuning, width 0.01..0.99, level 0..1, phaseReset 0..2 (Free, Reset, From silence). Block-rate. */
  void setParams(double shape, double octave = 0, double semitones = 0, double cents = 0, double width = 0.5,
                 double level = 1, double phaseReset = 2) noexcept {
    setShape(shape);
    tune = clamp(roundHalfUp(octave), -OCTAVE_RANGE, OCTAVE_RANGE) * 12 + clamp(roundHalfUp(semitones), -SEMITONE_RANGE, SEMITONE_RANGE)
        + clamp(cents, -CENTS_RANGE, CENTS_RANGE) * 0.01;
    this->width = clamp(width, 0.01, 0.99);
    levelTarget = clamp(level, 0, 1);
    this->phaseReset = static_cast<PhaseReset>(static_cast<int>(clamp(roundHalfUp(phaseReset), 0, 2)));
  }

  /** The frequency it plays now: the pitch with the tuning applied. */
  double frequency() const noexcept { return transpose(pitchHz, tune); }

  /** Writes n samples from offset (overwriting). */
  void process(float* out, int offset, int n) DSP_NONBLOCKING {
    const double inc = frequency() / sampleRate;
    // The first block after prepare() or reset() starts on its setting instead of gliding to it.
    if (std::isnan(lastInc)) level.reset(levelTarget);
    const double from = std::isnan(lastInc) ? inc : lastInc;
    selectMip((from > inc ? from : inc) * sampleRate);
    const double step = n > 0 ? (inc - from) / n : 0;
    double current = from;
    for (int i = 0; i < n; i++) {
      current += step;
      out[offset + i] = static_cast<float>(tick(current, 0, width) * level.step(levelTarget));
    }
    lastInc = inc;
  }

  /** Start from phase 0 now. */
  void resetPhase() noexcept { phase = 0; }

  /** Start from phase p (cycles, wrapped into 0..1) now. */
  void setPhase(double p) noexcept { phase = p - std::floor(p); }

  /**
   * A whole shape, or a fraction between two (see above). Past Square there
   * is nothing to crossfade to: the square itself narrows toward the Pulse
   * stop's width, so the last stretch of the axis is one shape getting thinner.
   */
  void setShape(double shape) noexcept {
    if (!(shape > 0)) shape = 0;
    else if (shape > OscShape::Pulse) shape = OscShape::Pulse;
    if (shape >= OscShape::Square) {
      shapeA = OscShape::Square;
      shapeB = OscShape::Square;
      mix = 0;
      narrow = shape - OscShape::Square;
      return;
    }
    shapeA = static_cast<int>(std::floor(shape));
    shapeB = shapeA + 1;
    mix = shape - shapeA;
    narrow = 0;
  }

  /** Pick the mip band once per block from the frequency about to be played. */
  void selectMip(double freqHz) noexcept { mipOffset = bank->mipOffset(bank->mipFor(freqHz)); }

  /**
   * @param inc  phase increment per sample (frequency / sample rate)
   * @param pm   phase-modulation offset in cycles
   * @param pwm  pulse width, 0..1, used by the Square shape only — and, past
   *             Square, narrowed toward PULSE_WIDTH by how far along the shape is
   */
  double tick(double inc, double pm, double pwm) noexcept {
    double p = phase + pm;
    p -= std::floor(p);

    const double w = pwm + (PULSE_WIDTH - pwm) * narrow;
    double out = sample(shapeA, p, w);
    if (mix > 0) out += (sample(shapeB, p, w) - out) * mix;

    phase += inc;
    if (phase >= 1) phase -= 1;
    else if (phase < 0) phase += 1;
    return out;
  }

 private:
  const WavetableBank* bank = nullptr;
  double sampleRate = 48000;

  double phase = 0;
  /** The two whole shapes being blended and how far from the first to the second. */
  int shapeA = OscShape::Tri;
  int shapeB = OscShape::Saw;
  double mix = 0;
  /** Past Square, how far the pulse has narrowed toward PULSE_WIDTH: 0 at Square, 1 at Pulse. */
  double narrow = 0;
  int mipOffset = 0;

  double pitchHz = 440;
  double tune = 0;
  double width = 0.5;
  double levelTarget = 1;
  PhaseReset phaseReset = PhaseReset::FromSilence;
  Smoother level;
  /** The increment the last block ended on; NaN until the first block, which starts on its own. */
  double lastInc = std::numeric_limits<double>::quiet_NaN();

  /** One tonal shape at phase p. */
  double sample(int shape, double p, double pwm) const noexcept {
    switch (shape) {
      case OscShape::Tri:
        return readTable(bank->tri, mipOffset, p);
      case OscShape::Saw:
        return readTable(bank->saw, mipOffset, p);
      case OscShape::Square: {
        // P(x) = s(x) - s(x-w), with s the band-limited saw — which falls, so
        // the difference is -2w for most of the cycle and 2 - 2w for a width
        // of w: a zero-mean pulse with no DC term to remove (±1 at w = ½; a
        // narrower pulse is quieter, as it should be). This polarity (rather
        // than the mirror image) keeps the pulse's fundamental in phase with
        // the saw's, so the Saw → Square morph fills in rather than
        // cancelling halfway.
        double q = p - pwm;
        q -= std::floor(q);
        return readTable(bank->saw, mipOffset, p) - readTable(bank->saw, mipOffset, q);
      }
      default:
        return readTable(bank->sine, 0, p);
    }
  }
};

}  // namespace dsp
