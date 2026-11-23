/**
 * Random: a stepped random modulation source (sample and hold).
 *
 * Speed     — 0.01..50 Hz (randomMath.hpp): how many new values it draws a second.
 * Slew      — 0..1, a share of the step: the output glides to each new value in a straight line over that share, then
 *             holds (randomSlewed). 0 is hard steps; 1 arrives exactly as the next value is drawn.
 * Placement — 0..1, where the window sits across the room its Width leaves (randomOutput).
 * Width     — 0.5..1, the window's width: 0.5 is 50 units, the narrowest; 1 is 100, the widest.
 *
 * The output is −1..1 (−100..+100). The clock runs free: no note touches it. Whether a host keeps one Random per voice
 * or one for the instrument is the host's choice; seed(n) picks its sequence, so two of them given different seeds
 * draw different values and the same seed repeats exactly.
 *
 * No allocation after construction; setParams() is block-rate. Placement and Width glide over RANDOM_GLIDE_MS, so
 * moving them never steps the output; a speed change keeps the phase continuous.
 */
#pragma once

#include <cmath>

#include "../util/math.hpp"
#include "../util/random.hpp"
#include "randomMath.hpp"

namespace dsp {

class Random {
 public:
  void prepare(double sampleRate) {
    this->sampleRate = sampleRate;
    glide = 1 - std::exp(-1 / (sampleRate * RANDOM_GLIDE_MS * 0.001));
    inc = speed / sampleRate;
    reset();
  }

  /** Picks the sequence and starts it over. */
  void seed(double value) noexcept {
    seedValue = value;
    reset();
  }

  /** Back to the start of the seed's sequence, holding its first value. */
  void reset() noexcept {
    // Neighbouring seeds (1, 2, 3: a block's voices) are spread over the generator's range and run in, so their first
    // values are as different as any others.
    rng.reset(static_cast<double>(wrapToUint32(seedValue) * 2654435761u));
    for (int i = 0; i < 4; i++) rng.next();
    phase = 0;
    to = draw();
    from = to;
    placementNow = placement;
    widthNow = width;
  }

  /** Speed in Hz, Slew 0..1, Placement 0..1, Width 0.5..1. Block-rate. */
  void setParams(double speed, double slew, double placement, double width) noexcept {
    this->speed = clamp(speed, RANDOM_SPEED_MIN, RANDOM_SPEED_MAX);
    inc = this->speed / sampleRate;
    this->slew = clamp(slew, 0, 1);
    this->placement = clamp(placement, 0, 1);
    this->width = clamp(width, RANDOM_WIDTH_MIN, RANDOM_WIDTH_MAX);
  }

  /** Where in its step the clock is, 0..1. */
  double currentPhase() const noexcept { return phase; }

  /** The current output, −1..1. */
  double value() const noexcept { return randomOutput(randomSlewed(from, to, phase, slew), placementNow, widthNow); }

  /** Per-sample output, n samples from offset (overwriting). */
  void process(float* out, int offset, int n) DSP_NONBLOCKING {
    for (int i = 0; i < n; i++) {
      placementNow += (placement - placementNow) * glide;
      widthNow += (width - widthNow) * glide;
      out[offset + i] = static_cast<float>(value());
      step(1);
    }
  }

  /**
   * Advance n samples without evaluating each one and return the value landed on: for a block-rate modulation source.
   * The glides land where n steps of them would.
   */
  double advance(int n) DSP_NONBLOCKING {
    const double k = 1 - std::pow(1 - glide, n);
    placementNow += (placement - placementNow) * k;
    widthNow += (width - widthNow) * k;
    step(n);
    return value();
  }

 private:
  double sampleRate = 48000;
  double seedValue = 1;
  Rng rng{1};

  double speed = 1;
  double inc = 1.0 / 48000;
  double phase = 0;
  double slew = 0;
  double from = 0.5;
  double to = 0.5;

  double placement = 0.5;
  double width = 1;
  double placementNow = 0.5;
  double widthNow = 1;
  double glide = 1;

  /** A new value, 0..1. */
  double draw() noexcept { return (rng.next() + 1) * 0.5; }

  /** Each time the clock wraps, the value it reached becomes where the next glide starts, and a new one is drawn. */
  void step(int n) noexcept {
    phase += inc * n;
    while (phase >= 1) {
      phase -= 1;
      from = to;
      to = draw();
    }
  }
};

}  // namespace dsp
