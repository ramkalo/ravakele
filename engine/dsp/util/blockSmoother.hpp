/**
 * A one-pole glide toward a target, stepped a block at a time: n samples of Smoother (math.hpp) in one call, for a
 * control read once per block (aftertouch, MPE pressure and per-note bend). Time 0 lands on the target at once.
 *
 *   per sample  c = 1 − e^{−1 / (sampleRate × timeMs / 1000)}
 *   per block   value += (target − value) × (1 − (1 − c)^n)
 */
#pragma once

#include <cmath>

#include "math.hpp"

namespace dsp {

class BlockSmoother {
 public:
  double value = 0;

  void prepare(double sampleRate) noexcept {
    rate = sampleRate > 0 ? sampleRate : 48000;
    setTime(timeMs);
  }

  /** The time constant in milliseconds; 0 or less is no smoothing. */
  void setTime(double ms) noexcept {
    timeMs = ms;
    keep = ms > 0 ? std::exp(-1 / (rate * ms * 0.001)) : 0;
  }

  void reset(double v) noexcept { value = v; }

  /** Steps n samples toward `target` and returns the value reached. */
  double advance(double target, int n) DSP_NONBLOCKING {
    if (keep <= 0 || n <= 0) {
      if (keep <= 0) value = target;
      return value;
    }
    value += (target - value) * (1 - std::pow(keep, n));
    return value;
  }

 private:
  double rate = 48000;
  double timeMs = 0;
  double keep = 0;  // 1 − c: how much of the gap one sample keeps
};

}  // namespace dsp
