/**
 * Tuning: where the keyboard sits in pitch. Twelve-tone equal temperament around a reference A4.
 *
 *   pitch(note) = note + Transpose + Fine / 100            (semitones, a MIDI note number)
 *   hz(pitch)   = A4 × 2^((pitch − 69) / 12)
 *
 * A4 — the reference pitch, TUNING_A4_MIN..TUNING_A4_MAX Hz (440).
 * Transpose — whole semitones, ±TUNING_TRANSPOSE_MAX.
 * Fine — cents, ±100.
 *
 * The host takes pitch() of each voice's note (after portamento, before bend and pitch modulation, which add in
 * semitones) and hz() of the sum for its oscillators. Pure arithmetic.
 */
#pragma once

#include <cmath>

#include "../util/math.hpp"

namespace dsp {

constexpr double TUNING_A4_MIN = 415;
constexpr double TUNING_A4_MAX = 466;
constexpr double TUNING_A4_DEFAULT = 440;
constexpr double TUNING_TRANSPOSE_MAX = 24;
constexpr double TUNING_FINE_MAX = 100;

class Tuning {
 public:
  /** a4 Hz, transpose semitones (rounded), fine cents. */
  void setParams(double a4Hz, double transposeSemitones, double fineCents) noexcept {
    a4 = clamp(a4Hz, TUNING_A4_MIN, TUNING_A4_MAX);
    transpose = clamp(roundHalfUp(transposeSemitones), -TUNING_TRANSPOSE_MAX, TUNING_TRANSPOSE_MAX);
    fine = clamp(fineCents, -TUNING_FINE_MAX, TUNING_FINE_MAX);
  }

  /** A note (or a fractional pitch) moved by Transpose and Fine, in semitones. */
  double pitch(double note) const noexcept { return note + transpose + fine / 100; }

  /** A pitch in semitones as Hz against A4 (no Transpose or Fine: apply pitch() first). */
  double hz(double semitones) const noexcept { return a4 * std::pow(2.0, (semitones - 69) / 12); }

  /** A note's frequency with Transpose and Fine. */
  double noteToHz(double note) const noexcept { return hz(pitch(note)); }

 private:
  double a4 = TUNING_A4_DEFAULT;
  double transpose = 0;
  double fine = 0;
};

}  // namespace dsp
