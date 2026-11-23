/**
 * A brick-wall limiter: a lookahead peak limiter, stereo-linked, that never lets a sample past the Ceiling.
 * The signal is delayed by the lookahead (LIMITER_LOOKAHEAD_MS) while the gain is worked out ahead of it:
 *
 *   1. each sample's target gain: ceiling / peak (both channels' larger, after Input), or 1 under the ceiling;
 *   2. hold: the lowest target over the last lookahead + 1 samples (a monotonic queue, O(1) a sample);
 *   3. release: a rising gain eases back up (one pole, Release); a falling one follows at once;
 *   4. attack: the mean of the last lookahead gains, so the gain glides down and reaches each peak's target as that
 *      peak leaves the delay.
 *
 * Every gain in the mean is at or under the target of the sample leaving the delay, so the output never passes the
 * ceiling; a last clamp at the ceiling covers rounding. It catches sample peaks, not peaks between samples.
 */
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "../util/math.hpp"

namespace dsp {

/** How far ahead the limiter looks, and so how late its output is. Fixed, so the delay never moves while playing. */
constexpr double LIMITER_LOOKAHEAD_MS = 1.5;

/** dB to an amplitude. */
inline double limiterDbToGain(double db) noexcept { return std::pow(10.0, db / 20); }

class Limiter {
 public:
  /** Off the audio thread: sizes the delay and the windows for this sample rate. */
  void prepare(double sampleRate) {
    sr = sampleRate;
    look = static_cast<int>(std::lround(LIMITER_LOOKAHEAD_MS * 0.001 * sampleRate));
    if (look < 1) look = 1;
    delayL.assign(look + 1, 0.0f);
    delayR.assign(look + 1, 0.0f);
    queueValue.assign(look + 1, 1.0);
    queueAt.assign(look + 1, 0);
    attack.assign(look, 1.0);
    inputGain.prepare(sampleRate, 10);
    setParams(inputDb, ceilingDb, releaseMs);
    inputGain.reset(inputTarget);
    reset();
  }

  /** Clears the delay and lets the gain back to unity. */
  void reset() noexcept {
    for (float& v : delayL) v = 0;
    for (float& v : delayR) v = 0;
    for (double& v : attack) v = 1;
    write = 0;
    attackAt = 0;
    attackSum = look;
    queueHead = queueCount = 0;
    now = 0;
    released = 1;
    inputGain.reset(inputTarget);
  }

  /** Input in dB (gain into the limiter), Ceiling in dBFS, Release in ms. */
  void setParams(double input, double ceiling, double release) noexcept {
    inputDb = input;
    ceilingDb = ceiling;
    releaseMs = release;
    inputTarget = limiterDbToGain(clamp(input, 0, 24));
    ceilingGain = limiterDbToGain(clamp(ceiling, -12, 0));
    const double ms = clamp(release, 10, 1000);
    releaseCoeff = sr > 0 ? 1 - std::exp(-1 / (sr * ms * 0.001)) : 1;
  }

  /** How many samples late the output is. */
  int latency() const noexcept { return look; }

  void process(float* left, float* right, int offset, int n) DSP_NONBLOCKING {
    if (look < 1 || delayL.empty()) return;
    const int size = look + 1;
    const double ceil = ceilingGain;
    for (int s = 0; s < n; s++) {
      const double g = inputGain.step(inputTarget);
      double xL = left[offset + s], xR = right[offset + s];
      if (!std::isfinite(xL)) xL = 0;
      if (!std::isfinite(xR)) xR = 0;
      xL *= g;
      xR *= g;

      // 1. the target gain for this sample
      const double peak = std::fmax(std::fabs(xL), std::fabs(xR));
      const double target = peak > ceil ? ceil / peak : 1;

      // 2. hold: the lowest target over the last look + 1 samples
      while (queueCount > 0 && queueAt[queueHead] + size <= now) {
        queueHead = (queueHead + 1) % size;
        queueCount--;
      }
      while (queueCount > 0 && queueValue[(queueHead + queueCount - 1) % size] >= target) queueCount--;
      queueValue[(queueHead + queueCount) % size] = target;
      queueAt[(queueHead + queueCount) % size] = now;
      queueCount++;
      const double held = queueValue[queueHead];

      // 3. release
      released = held < released ? held : released + (held - released) * releaseCoeff;

      // 4. attack: the mean of the last look gains
      attackSum += released - attack[attackAt];
      attack[attackAt] = released;
      if (++attackAt == look) {
        attackAt = 0;
        double sum = 0;
        for (double v : attack) sum += v;
        attackSum = sum;  // re-summed once a lap, so rounding never builds up
      }
      const double gain = attackSum / look;

      // the delayed sample out, at the gain worked out for it
      const int read = (write + 1) % size;
      delayL[write] = static_cast<float>(xL);
      delayR[write] = static_cast<float>(xR);
      left[offset + s] = static_cast<float>(clamp(delayL[read] * gain, -ceil, ceil));
      right[offset + s] = static_cast<float>(clamp(delayR[read] * gain, -ceil, ceil));
      write = read;
      now++;
    }
  }

 private:
  double sr = 0;
  int look = 0;
  double inputDb = 0, ceilingDb = -0.3, releaseMs = 100;
  double inputTarget = 1, ceilingGain = 1, releaseCoeff = 1;
  Smoother inputGain;

  std::vector<float> delayL, delayR;  // look + 1 samples: what is written now is read look samples later
  int write = 0;

  std::vector<double> queueValue;  // the hold's monotonic queue, look + 1 slots
  std::vector<std::int64_t> queueAt;
  int queueHead = 0, queueCount = 0;
  std::int64_t now = 0;

  double released = 1;

  std::vector<double> attack;  // the last look released gains
  int attackAt = 0;
  double attackSum = 0;
};

}  // namespace dsp
