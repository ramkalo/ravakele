/**
 * Thru: its stereo input, unchanged, at its output. It shapes nothing: it is the block a display watches (the
 * Waterfall, util.waterfall; the Scope, util.scope; the Tuner, util.tuner), so a host records what was wired into it by recording its output.
 *
 * No state and no allocation.
 */
#pragma once

#include <algorithm>

#include "math.hpp"

namespace dsp {

class Thru {
 public:
  /** Copies one channel from `offset` for `n` samples; a null input (nothing wired) gives silence. */
  void process(const float* in, float* out, int offset, int n) const DSP_NONBLOCKING {
    if (in == out) return;
    if (in) std::copy(in + offset, in + offset + n, out + offset);
    else std::fill(out + offset, out + offset + n, 0.0f);
  }
};

}  // namespace dsp
