/**
 * The amp: a voice's audio times Gain times its control signal (an envelope, usually), so one envelope shapes
 * everything that is mixed into it.
 *
 * Gain     — the level, 0..1, glided over GAIN_SMOOTH_MS so a jump does not click.
 * Response — how the control signal maps to gain: 0 linear, 1 exponential (vcaCurve), blended between.
 * Pan      — −1 (left) … +1 (right), a balance (balanceGains in util/pan.hpp): the middle keeps both sides at 1, a side
 *            turns the other down to 0. So 0, the default, changes nothing, and a mono source (L = R) is panned with
 *            its middle at full level. Glided over GAIN_SMOOTH_MS. The stereo process() only.
 *
 * No allocation after construction.
 */
#pragma once

#include <cmath>

#include "../util/math.hpp"
#include "../util/pan.hpp"

namespace dsp {

/** The glide on Gain, in ms. */
constexpr double GAIN_SMOOTH_MS = 5;
/** The bend of the exponential response: e^k is its range, about 60 dB. */
constexpr double VCA_EXP_K = 6.9;

/** The gain a control value 0..1 gives at a response 0..1; 0 → 0 and 1 → 1 at every response. */
inline double vcaCurve(double cv, double response) noexcept {
  const double c = clamp(cv, 0, 1);
  const double r = clamp(response, 0, 1);
  const double bent = (std::exp(VCA_EXP_K * c) - 1) / (std::exp(VCA_EXP_K) - 1);
  return c + (bent - c) * r;
}

class Vca {
 public:
  void prepare(double sampleRate) {
    smoother.prepare(sampleRate, GAIN_SMOOTH_MS);
    panSmoother.prepare(sampleRate, GAIN_SMOOTH_MS);
    reset();
  }

  /** Jumps the level and the pan to their targets, with no glide. */
  void reset() noexcept {
    smoother.reset(target());
    panSmoother.reset(pan);
  }

  /** Gain 0..1, Response 0..1, Pan −1..1. Block-rate. */
  void setParams(double gain, double response = 0, double pan = 0) noexcept {
    this->gain = clamp(gain, 0, 1);
    this->response = clamp(response, 0, 1);
    this->pan = clamp(pan, -1, 1);
  }

  /**
   * Scales io in place from offset for n samples by Gain and vcaCurve(cv). With cv null (nothing
   * wired) the control signal is 1.
   */
  void process(float* io, const float* cv, int offset, int n) DSP_NONBLOCKING {
    const double t = target();
    const double r = response;
    for (int s = 0; s < n; s++) {
      const double level = smoother.step(t);
      const double c = cv ? vcaCurve(cv[offset + s], r) : 1;
      io[offset + s] = static_cast<float>(io[offset + s] * level * c);
    }
  }

  /** The stereo process(): left and right in place, both scaled by the one gain each sample, then balanced by Pan. */
  void process(float* left, float* right, const float* cv, int offset, int n) DSP_NONBLOCKING {
    const double t = target();
    const double r = response;
    for (int s = 0; s < n; s++) {
      const double gainNow = smoother.step(t) * (cv ? vcaCurve(cv[offset + s], r) : 1);
      const PanGains side = balanceGains(panSmoother.step(pan));
      left[offset + s] = static_cast<float>(left[offset + s] * gainNow * side.l);
      right[offset + s] = static_cast<float>(right[offset + s] * gainNow * side.r);
    }
  }

 private:
  double target() const noexcept { return gain; }

  Smoother smoother;
  Smoother panSmoother;
  double gain = 1;
  double response = 0;
  double pan = 0;
};

}  // namespace dsp
