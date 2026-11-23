/**
 * Small numeric helpers shared by the DSP modules, and DSP_NONBLOCKING, the mark every audio-path method carries.
 */
#pragma once

#include <cmath>

/**
 * Marks a method that runs on the audio path: noexcept, and [[clang::nonblocking]] where the compiler has it, so
 * Clang's RealtimeSanitizer (-fsanitize=realtime) flags any allocation, lock or system call reached from it.
 */
#if defined(__has_cpp_attribute)
#if __has_cpp_attribute(clang::nonblocking)
#define DSP_NONBLOCKING noexcept [[clang::nonblocking]]
#endif
#endif
#ifndef DSP_NONBLOCKING
#define DSP_NONBLOCKING noexcept
#endif

namespace dsp {

constexpr double PI = 3.14159265358979323846;

/** v limited to lo..hi; a NaN comes out as lo. */
inline double clamp(double v, double lo, double hi) noexcept {
  return v > lo ? (v < hi ? v : hi) : lo;
}

/** Math.round: halves round up (toward +∞), so -2.5 → -2. */
inline double roundHalfUp(double v) noexcept {
  return std::floor(v + 0.5);
}

/**
 * A one-pole glide toward a target, stepped once per sample: a control set per block reaches its new value smoothly
 * instead of stepping. `timeMs` is the time constant.
 */
class Smoother {
 public:
  double value = 0;

  void prepare(double sampleRate, double timeMs = 10) {
    coeff = 1 - std::exp(-1 / (sampleRate * timeMs * 0.001));
  }

  void reset(double v) noexcept { value = v; }

  double step(double target) noexcept {
    value += (target - value) * coeff;
    return value;
  }

 private:
  double coeff = 1;
};

}  // namespace dsp
