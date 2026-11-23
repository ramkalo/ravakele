/**
 * Placing a signal on the stereo field. Positions run −1 (hard left) … +1 (hard right). A mono signal is panned with
 * constant power (both sides at cos/sin, so it sounds as loud anywhere); a stereo signal is balanced (the far side
 * turned down, the near side kept). The window delay pans its echoes with panGains; the engine's mix arrows
 * (library/engine/runtime.hpp mixGains) use both.
 */
#pragma once

#include <cmath>

#include "math.hpp"

namespace dsp {

struct PanGains {
  double l, r;
};

/** A constant-power pan of a position −1 (left) … +1 (right): the left and right gains. */
inline PanGains panGains(double position) noexcept {
  const double a = (clamp(position, -1, 1) + 1) * (PI / 4);
  return {std::cos(a), std::sin(a)};
}

/** A balance of a stereo pair at a position −1 … +1: centre keeps both at 1, a side turns the other down to 0. */
inline PanGains balanceGains(double position) noexcept {
  const double p = clamp(position, -1, 1);
  return {p > 0 ? 1 - p : 1, p < 0 ? 1 + p : 1};
}

}  // namespace dsp
