/**
 * The saturation, as simple as it comes: the signal driven into a tanh,
 * Drive the gain into it (unity to DRIVE_MAX), Mix the wet/dry (passed to process()).
 * Stateless, so nothing to prepare or reset beyond the shape.
 */
#pragma once

#include <cmath>

#include "../util/math.hpp"

namespace dsp {

/** The gain at full drive, as an amplitude: 16×, +24 dB. */
constexpr double DRIVE_MAX = 16;

/** The gain a drive setting puts into the curve. */
inline double driveGain(double drive) noexcept {
  const double d = drive < 0 ? 0 : drive > 1 ? 1 : drive;
  return 1 + d * (DRIVE_MAX - 1);
}

/** The curve itself, so the UI can draw the same one. */
inline double saturate(double x, double gain) noexcept { return std::tanh(x * gain); }

class Saturator {
 public:
  void prepare(double /*sampleRate*/) { gain = 1; }

  void reset() noexcept {}

  void setParams(double drive) noexcept { gain = driveGain(drive); }

  void process(float* left, float* right, int offset, int n, double mix) DSP_NONBLOCKING {
    const double wet = mix;
    const double dry = 1 - mix;
    const double g = gain;
    for (int s = 0; s < n; s++) {
      const double xL = left[offset + s];
      const double xR = right[offset + s];
      left[offset + s] = static_cast<float>(xL * dry + std::tanh(xL * g) * wet);
      right[offset + s] = static_cast<float>(xR * dry + std::tanh(xR * g) * wet);
    }
  }

 private:
  double gain = 1;
};

}  // namespace dsp
