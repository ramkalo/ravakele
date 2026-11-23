/**
 * The unison copies an oscillator wrapper plays (voice.unison): how many, each one's detune in cents, and the gain their
 * sum is played at. The engine works these out once (dsp::Unison) and hands them to every voice (Voice::setUnison),
 * which hands them to each wrapper that takes unison (its manifest's "unison": true).
 *
 * The wrapper plays copy 0 into its output and copies 1..count−1 into a scratch buffer added on top, then scales the
 * sum by gain. With one copy (unison off) it plays exactly as it did without unison. A copy that starts from a fixed
 * phase starts at startPhase(i): copy 0 at 0, copy i at the fractional part of i × the golden ratio, so a note begins
 * neither as one loud oscillator (every copy at 0) nor cancelled (copies evenly round the cycle sum to nothing).
 * Nothing here allocates after prepare().
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "../note/Unison.hpp"

namespace dsp {
namespace adapter {

class UnisonCopies {
 public:
  void prepare(int maxBlock) { scratch.assign(static_cast<std::size_t>(maxBlock > 0 ? maxBlock : 1), 0.0f); }

  /** count 1..UNISON_MAX; cents[i] for each copy; gain for their sum. */
  void set(int count, const double* cents, double gain) noexcept {
    copies = std::clamp(count, 1, UNISON_MAX);
    for (int i = 0; i < UNISON_MAX; i++) ratio[i] = i < copies && cents ? std::pow(2.0, cents[i] / 1200.0) : 1.0;
    sumGain = copies > 1 ? static_cast<float>(gain) : 1.0f;
  }

  int count() const noexcept { return copies; }

  /** Copy i's pitch from the voice's: pitchHz × 2^(cents(i)/1200). */
  double pitch(int i, double pitchHz) const noexcept { return pitchHz * ratio[i]; }

  /** Where copy i starts when it starts from a fixed phase, in cycles: frac(i × golden ratio). */
  static double startPhase(int i) noexcept {
    const double p = i * 0.6180339887498949;
    return p - std::floor(p);
  }

  /**
   * Plays the copies into out from offset for n samples: render(i, buffer, at, n) writes copy i (overwriting) into
   * buffer from at. Copy 0 goes straight into out; the rest are summed onto it, and the sum is scaled by the gain.
   */
  template <class Render>
  void play(float* out, int offset, int n, Render&& render) {
    render(0, out, offset, n);
    if (copies <= 1) return;
    for (int i = 1; i < copies; i++) {
      render(i, scratch.data(), 0, n);
      for (int s = 0; s < n; s++) out[offset + s] += scratch[s];
    }
    for (int s = 0; s < n; s++) out[offset + s] *= sumGain;
  }

 private:
  int copies = 1;
  double ratio[UNISON_MAX] = {1, 1, 1, 1, 1, 1, 1, 1};
  float sumGain = 1.0f;
  std::vector<float> scratch;
};

}  // namespace adapter
}  // namespace dsp
