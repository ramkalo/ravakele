/**
 * The pan pair: two separate pathways, A and B, each placed on the stereo field on its own.
 *
 * Pan A — pathway A's position, −1 (left) … +1 (right).
 * Pan B — pathway B's, the same.
 *
 * Each pathway is balanced (balanceGains in util/pan.hpp, the VCA's Pan law): the middle keeps both sides at 1, a side
 * turns the other down to 0. So 0 leaves a pathway as it is, and a mono source (L = R) is panned with its middle at
 * full level. Each pan glides over PAN_PAIR_SMOOTH_MS so a jump does not click. The Pan Pad drives the two from its
 * window's two lines. No allocation after construction.
 */
#pragma once

#include "../util/math.hpp"
#include "../util/pan.hpp"

namespace dsp {

/** The glide on each pan, in ms. */
constexpr double PAN_PAIR_SMOOTH_MS = 5;

class PanPair {
 public:
  void prepare(double sampleRate) {
    smoothA.prepare(sampleRate, PAN_PAIR_SMOOTH_MS);
    smoothB.prepare(sampleRate, PAN_PAIR_SMOOTH_MS);
    reset();
  }

  /** Jumps both pans to their targets, with no glide. */
  void reset() noexcept {
    smoothA.reset(panA);
    smoothB.reset(panB);
  }

  /** Pan A and Pan B, each −1..1. Block-rate. */
  void setParams(double panA, double panB) noexcept {
    this->panA = clamp(panA, -1, 1);
    this->panB = clamp(panB, -1, 1);
  }

  /** Balances pathway A (aLeft, aRight) by Pan A and pathway B (bLeft, bRight) by Pan B, in place, from offset for n samples. */
  void process(float* aLeft, float* aRight, float* bLeft, float* bRight, int offset, int n) DSP_NONBLOCKING {
    for (int s = 0; s < n; s++) {
      const PanGains a = balanceGains(smoothA.step(panA));
      const PanGains b = balanceGains(smoothB.step(panB));
      const int i = offset + s;
      aLeft[i] = static_cast<float>(aLeft[i] * a.l);
      aRight[i] = static_cast<float>(aRight[i] * a.r);
      bLeft[i] = static_cast<float>(bLeft[i] * b.l);
      bRight[i] = static_cast<float>(bRight[i] * b.r);
    }
  }

 private:
  Smoother smoothA;
  Smoother smoothB;
  double panA = 0;
  double panB = 0;
};

}  // namespace dsp
