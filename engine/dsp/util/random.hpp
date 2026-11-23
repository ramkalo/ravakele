/**
 * A seeded random source (xorshift32), so a module's random choices repeat exactly for the same seed and its tests can
 * assert on them.
 */
#pragma once

#include <cmath>
#include <cstdint>

namespace dsp {

/** A number as a 32-bit integer the way JavaScript's `x | 0` makes one: truncated, then wrapped modulo 2^32. */
inline std::uint32_t wrapToUint32(double x) noexcept {
  if (!std::isfinite(x)) return 0;
  const double t = std::trunc(x);
  const double m = std::fmod(t, 4294967296.0);
  return static_cast<std::uint32_t>(static_cast<std::int64_t>(m < 0 ? m + 4294967296.0 : m));
}

class Rng {
 public:
  explicit Rng(double seed = 0x9e3779b9) noexcept { reset(seed); }

  void reset(double seed) noexcept {
    state = wrapToUint32(seed);
    if (state == 0) state = 1;
  }

  /** Uniform in [-1, 1). */
  double next() noexcept {
    std::uint32_t x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return static_cast<double>(static_cast<std::int32_t>(x)) / 2147483648.0;
  }

 private:
  std::uint32_t state = 1;
};

}  // namespace dsp
