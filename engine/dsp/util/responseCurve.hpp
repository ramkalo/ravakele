/**
 * The response curve: one input 0..1 bent by a Bend and landed on an output range. Key tracking, velocity, aftertouch,
 * the mod wheel (their Min and Max the curve's two ends), MPE slide and pressure, unison detune and per-voice values all
 * use it, and the UI draws it from the same formula
 * (library/ui/controls.js responseCurve; src/shared/response-curve-parity.test.js holds the two together).
 *
 *   shaped = x                                   at bend 0 (linear)
 *          = (e^{k·x} − 1) / (e^k − 1)           otherwise, k = RESPONSE_CURVE_BEND × bend
 *   out    = min + (max − min) × shaped
 *
 * Bend −1 rises fast and levels off (log-like), +1 starts slow and rises late (exponential); the envelopes' and
 * portamento's bend (env/envelopeCurves.hpp, note/Portamento.hpp) is the same formula. min above max runs the range
 * downward.
 */
#pragma once

#include <cmath>

#include "math.hpp"

namespace dsp {

/** How far Bend ±1 bends the curve; identical to the envelopes' and portamento's. */
constexpr double RESPONSE_CURVE_BEND = 6;

/** x (0..1, clamped) bent by `bend` (−1..1, clamped), landed on min..max. */
inline double responseCurve(double x, double bend, double min, double max) noexcept {
  const double p = clamp(x, 0, 1);
  const double k = clamp(bend, -1, 1) * RESPONSE_CURVE_BEND;
  const double shaped = (k > -1e-4 && k < 1e-4) ? p : (std::exp(k * p) - 1) / (std::exp(k) - 1);
  return min + (max - min) * shaped;
}

}  // namespace dsp
