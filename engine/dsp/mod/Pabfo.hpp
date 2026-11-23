/**
 * The PABFO: a psychoacoustic bass frequency oscillator (sketch SK032).
 *
 * A mod source like the LFO, but its rate is never set by hand: it listens to the voice's note and runs at the note's
 * frequency times a ratio, folded by whole octaves into one bass octave, PABFO_BAND_LOW..PABFO_BAND_HIGH (50..100 Hz).
 * Routed onto a cutoff or the amp, the sidebands it adds sit a deep subharmonic below the note, so a high note gains a
 * false deep fundamental a small speaker (a phone, a laptop) cannot play itself.
 *
 * Ratio (note : PABFO), in button order bottom → top — 4:1 a quarter of the note's frequency, 2:1 a half, 3:2 two
 * thirds, 1:1 the note — then folded: an A 440 at 1:1 runs at 55 Hz, at 3:2 at 73.3 Hz; a 30 Hz note at 4:1 at 60 Hz.
 *
 * Window — 0..1, where the 100-unit window sits on −100..+100, as the LFO's (lfoOutput at level 1): 0 is −100..0,
 * 0.5 is −50..+50, 1 is 0..+100. The output is window − 0.5 + (u − 0.5) with u = 0.5 − 0.5·cos(2πφ), so at phase 0
 * (each note-on) it sits at the window's low edge and a note starts from where the swing begins. There is no level:
 * each Mod mode route's level is the depth.
 *
 * The rate follows the pitch the host sets each block (glide and bend included); a ratio change keeps the phase
 * continuous. Its routes are applied every PABFO_SLICE samples by the engine (src/engine/generate.js), since 50..100 Hz
 * is too fast for block-rate routing. No allocation after construction.
 */
#pragma once

#include <cmath>

#include "../util/math.hpp"
#include "lfoMath.hpp"

namespace dsp {

/** The ratios, in button order (bottom → top), as note : PABFO. */
constexpr const char* PABFO_RATIO_LABELS[] = {"4:1", "2:1", "3:2", "1:1"};
/** What each ratio multiplies the note's frequency by, before the fold. */
constexpr double PABFO_RATIO_MULTIPLIERS[] = {1.0 / 4, 1.0 / 2, 2.0 / 3, 1};
constexpr int PABFO_RATIO_COUNT = 4;
constexpr int PABFO_RATIO_DEFAULT = 1;
constexpr double PABFO_WINDOW_DEFAULT = 0.5;

/** The bass octave every rate is folded into: [low, high). */
constexpr double PABFO_BAND_LOW = 50;
constexpr double PABFO_BAND_HIGH = 100;

/** How many samples apart the engine applies a PABFO's routes (src/engine/generate.js PABFO_SLICE). */
constexpr int PABFO_SLICE = 16;

/** A ratio param (0..3, rounded) as its index. */
inline int pabfoRatioOf(double v) noexcept { return static_cast<int>(clamp(roundHalfUp(v), 0, PABFO_RATIO_COUNT - 1)); }

/** The PABFO's rate in Hz for a note at `pitchHz` and a ratio param: note × ratio, folded by octaves into the band. */
inline double pabfoRateHz(double pitchHz, double ratio) noexcept {
  if (!(pitchHz > 0)) return 0;
  double hz = pitchHz * PABFO_RATIO_MULTIPLIERS[pabfoRatioOf(ratio)];
  while (hz >= PABFO_BAND_HIGH) hz *= 0.5;
  while (hz < PABFO_BAND_LOW) hz *= 2;
  return hz;
}

class Pabfo {
 public:
  void prepare(double sampleRate) {
    this->sampleRate = sampleRate;
    glide = 1 - std::exp(-1 / (sampleRate * LFO_GLIDE_MS * 0.001));
    reset();
  }

  /** Back to rest: phase 0, the window where the params put it. */
  void reset() noexcept {
    phase = 0;
    windowNow = window;
  }

  /** A note-on restarts the phase so every hit is the same. */
  void noteOn() noexcept { phase = 0; }

  /** The voice's current pitch in Hz (glide and bend included), each block. */
  void setPitch(double hz) noexcept { pitchHz = hz > 0 ? hz : 0; }

  /** Ratio 0..3 (PABFO_RATIO_LABELS), Window 0..1. Block-rate. */
  void setParams(double ratio, double window) noexcept {
    this->ratio = pabfoRatioOf(ratio);
    this->window = clamp(window, 0, 1);
  }

  /** The rate it is running at now, Hz. */
  double rateHz() const noexcept { return pabfoRateHz(pitchHz, ratio); }

  /** Where in its cycle it is, 0..1. */
  double currentPhase() const noexcept { return phase; }

  /** The current output, −1..1. */
  double value() const noexcept { return windowNow - 0.5 - 0.5 * std::cos(2 * PI * phase); }

  /** Per-sample output, n samples from offset (overwriting). */
  void process(float* out, int offset, int n) DSP_NONBLOCKING {
    const double inc = rateHz() / sampleRate;
    for (int i = 0; i < n; i++) {
      windowNow += (window - windowNow) * glide;
      out[offset + i] = static_cast<float>(value());
      phase += inc;
      phase -= std::floor(phase);
    }
  }

  /** Advance n samples without evaluating each one and return the value landed on (the output unwired). */
  double advance(int n) DSP_NONBLOCKING {
    windowNow += (window - windowNow) * (1 - std::pow(1 - glide, n));
    phase += (rateHz() / sampleRate) * n;
    phase -= std::floor(phase);
    return value();
  }

 private:
  double sampleRate = 48000;
  double phase = 0;
  double pitchHz = 0;
  int ratio = PABFO_RATIO_DEFAULT;
  double window = PABFO_WINDOW_DEFAULT;
  double windowNow = PABFO_WINDOW_DEFAULT;
  double glide = 1;
};

}  // namespace dsp
