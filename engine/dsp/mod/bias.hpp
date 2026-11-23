/**
 * Bias: one parameter's value fanned out to up to 32 others, each varied.
 *
 * A bias block takes one block parameter as its input and sets up to
 * BIAS_MAX_OUTPUTS other parameters — the same parameter of the same module
 * (four envelopes' attacks, six oscillators' fine tunes) — in an order the
 * designer chose. Output r sends the input's base value plus its own offset:
 *
 *   value_r = clamp(input + offset_r + addition_r, output range)
 *
 * where addition_r is what a component on that output adds on top (its value
 * minus the parameter's default; zero when none drives it). Example: an aux
 * envelope's attack is 44 ms; a ramp with spread 1 sends 45, 46, 47 to three
 * other envelopes' attacks. The aux envelope still runs normally.
 *
 * The pattern sets how the offsets vary along the order; spread is the step,
 * in the input parameter's own unit, and may be negative:
 *
 *   - ramp: spread · (r + 1), shaped — output 1 moves by one step and output n
 *     by n steps; exponential takes small steps first, log big steps first;
 *   - alternate: +1, −1, +2, −2… steps (sign flips each output, the size grows
 *     every second output), shaped by size;
 *   - center-out: the middle output keeps the input; the others step above
 *     (after the middle) and below (before it), shaped by distance;
 *   - random: a fixed offset per output within ±spread, from the seed (the same
 *     seed always gives the same offsets), shaped by size.
 *
 * Spread, shape, pattern, and seed are the block's parameters: components may
 * drive them and modulation sources may modulate them. The input is read at its
 * base value (its handle), not its modulated value.
 *
 * Pure functions; biasOffsets writes into a caller-owned array, so nothing is
 * allocated in the audio path. src/shared/bias.js (the editor) mirrors this for the
 * editor's preview (a parity test keeps them equal).
 */
#pragma once

#include <cmath>
#include <cstdint>

#include "../util/random.hpp"

namespace dsp {

/** The shape and pattern parameters' stops, in order (their stepped values 0, 1, 2…). */
enum class BiasShape : int { Linear = 0, Exponential = 1, Log = 2 };
enum class BiasPattern : int { Ramp = 0, Alternate = 1, Center = 2, Random = 3 };
constexpr const char* BIAS_SHAPES[] = {"linear", "exponential", "log"};
constexpr const char* BIAS_PATTERNS[] = {"ramp", "alternate", "center", "random"};
/** A bias sets at most this many outputs. */
constexpr int BIAS_MAX_OUTPUTS = 32;
/** Steepness of the exponential and log shapes: the last step is 2^BIAS_CURVE_K times the first. */
constexpr double BIAS_CURVE_K = 3;

/** The share of the full reach a size `w` (0..1) receives: straight, exponential, or log. */
inline double biasShare(double w, BiasShape shape) noexcept {
  const double norm = std::pow(2.0, BIAS_CURVE_K) - 1;
  const double x = w < 0 ? 0 : w > 1 ? 1 : w;
  if (shape == BiasShape::Exponential) return (std::pow(2.0, BIAS_CURVE_K * x) - 1) / norm;
  if (shape == BiasShape::Log) return 1 - (std::pow(2.0, BIAS_CURVE_K * (1 - x)) - 1) / norm;
  return x;
}

/** A repeatable value in −1..1 for output `rank` under `seed` (an integer hash; no generator state). */
inline double biasRandom(int rank, double seed) noexcept {
  std::uint32_t h = ((wrapToUint32(seed) ^ 0x9e3779b9u) * 0x85ebca6bu) ^ (static_cast<std::uint32_t>(rank + 1) * 0xc2b2ae35u);
  h = (h ^ (h >> 16)) * 0x7feb352du;
  h = (h ^ (h >> 15)) * 0x846ca68bu;
  h ^= h >> 16;
  return (static_cast<double>(h) / 0xffffffffu) * 2 - 1;
}

namespace bias_detail {

inline double sign(double v) noexcept { return v > 0 ? 1 : v < 0 ? -1 : 0; }

/**
 * Steps of size `k` (1…`reach`) shaped: `k` steps on a straight shape; the same reach at the far end on the others.
 * `spread · reach · share(k / reach)`.
 */
inline double shaped(double k, double reach, double spread, BiasShape shape) noexcept {
  return reach <= 0 ? 0 : spread * reach * biasShare(k / reach, shape);
}

}  // namespace bias_detail

/** The offset output `rank` (0-based) of `count` adds to the input, in the input parameter's unit. */
inline double biasOffset(int rank, int count, BiasPattern pattern, BiasShape shape, double spread, double seed = 0) noexcept {
  using namespace bias_detail;
  if (count <= 0 || spread == 0) return 0;
  if (pattern == BiasPattern::Alternate) {
    const double k = rank / 2 + 1, reach = std::ceil(count / 2.0);
    return (rank % 2 == 0 ? 1 : -1) * shaped(k, reach, spread, shape);
  }
  if (pattern == BiasPattern::Center) {
    const double middle = (count - 1) / 2.0, distance = rank - middle, reach = std::ceil(middle);
    if (distance == 0 || reach == 0) return 0;
    // An even count has no middle output: the two nearest sit half a step out, the rest whole steps beyond.
    return sign(distance) * shaped(std::fabs(distance), reach, spread, shape);
  }
  if (pattern == BiasPattern::Random) {
    const double u = biasRandom(rank, seed);
    return sign(u) * spread * biasShare(std::fabs(u), shape);
  }
  return shaped(rank + 1, count, spread, shape);
}

/** Every output's offset, written into `out[0..count)`. */
inline void biasOffsets(float* out, int count, BiasPattern pattern, BiasShape shape, double spread, double seed = 0) noexcept {
  for (int rank = 0; rank < count; rank++) out[rank] = static_cast<float>(biasOffset(rank, count, pattern, shape, spread, seed));
}

/** Output `rank`'s value: input + offset + what a component on it adds, clamped to the output's range. */
inline double biasOutput(double input, double offset, double addition, double min, double max) noexcept {
  const double value = input + offset + addition;
  return value < min ? min : value > max ? max : value;
}

}  // namespace dsp
