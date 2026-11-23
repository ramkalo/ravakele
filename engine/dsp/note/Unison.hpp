/**
 * Unison: each voice's oscillators play as up to UNISON_MAX copies, spread in pitch, summed in mono.
 *
 * Copy i of n sits at a position p from −1 (the first) to +1 (the last), evenly; a single copy sits at 0. Its pitch
 * offset is p's sign times the response curve (util/responseCurve.hpp) of |p|, from 0 to Detune cents, so Detune is
 * how far the outermost copies sit from the note and Detune curve says how the inner ones crowd toward the centre
 * (+1) or out toward the edges (−1).
 *
 * Unison — copies per voice, 1..UNISON_MAX (1 is off).
 * Detune — the outermost copies' offset, 0..UNISON_DETUNE_MAX cents.
 * Detune curve — −1..1: the response curve's bend over the copies' positions.
 *
 * The host plays count() copies of each oscillator, copy i at cents(i) from the voice's pitch, and sums them at gain()
 * (1 / sqrt(count())) to keep the level even. The copies are not panned: an oscillator's output is mono. Pure
 * arithmetic; nothing allocates.
 */
#pragma once

#include <cmath>

#include "../util/math.hpp"
#include "../util/responseCurve.hpp"

namespace dsp {

constexpr int UNISON_MAX = 8;
constexpr double UNISON_DETUNE_MAX = 100;

class Unison {
 public:
  /** unison 1..UNISON_MAX (rounded), detune 0..UNISON_DETUNE_MAX cents, curve −1..1. */
  void setParams(double unison, double detuneCents, double detuneCurve) noexcept {
    copies = static_cast<int>(clamp(roundHalfUp(unison), 1, UNISON_MAX));
    detune = clamp(detuneCents, 0, UNISON_DETUNE_MAX);
    curve = clamp(detuneCurve, -1, 1);
  }

  /** How many copies each voice plays. */
  int count() const noexcept { return copies; }

  /** Copy i's position, −1 (first) … +1 (last); 0 for a single copy. */
  double position(int i) const noexcept {
    if (copies <= 1) return 0;
    const int at = static_cast<int>(clamp(i, 0, copies - 1));
    return -1 + 2.0 * at / (copies - 1);
  }

  /** Copy i's pitch offset in cents. */
  double cents(int i) const noexcept {
    const double p = position(i);
    const double size = responseCurve(std::fabs(p), curve, 0, detune);
    return p < 0 ? -size : size;
  }

  /** The gain each copy is played at so the voice's level stays even: 1 / sqrt(count()). */
  double gain() const noexcept { return 1 / std::sqrt(static_cast<double>(copies)); }

 private:
  int copies = 1;
  double detune = 15;
  double curve = 0;
};

}  // namespace dsp
