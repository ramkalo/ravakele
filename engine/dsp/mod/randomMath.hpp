/**
 * Random's shared arithmetic: how far Speed reaches, how Slew glides from one drawn value to the next, and how the
 * window (Placement and Width) places the value. The engine runs on these, and the UI (library/ui/random-pad.js) reads
 * its Speed and its window's edges from a JavaScript copy of the same arithmetic, held to this file by a parity test
 * (src/shared/random-pad-parity.test.js).
 */
#pragma once

#include <cmath>

#include "../util/math.hpp"

namespace dsp {

/** The slowest and fastest speed, in new values per second (exponential between). */
constexpr double RANDOM_SPEED_MIN = 0.01;
constexpr double RANDOM_SPEED_MAX = 50;
/** The narrowest and the widest window, as a share of 100 units: 50 to 100. */
constexpr double RANDOM_WIDTH_MIN = 0.5;
constexpr double RANDOM_WIDTH_MAX = 1;
/** How long the window's Placement and Width glide to a new value (one-pole time constant, ms). */
constexpr double RANDOM_GLIDE_MS = 10;

/** Speed in Hz at a 0..1 path value (the manifest's exp taper). */
inline double randomSpeedHz(double t) noexcept {
  return RANDOM_SPEED_MIN * std::pow(RANDOM_SPEED_MAX / RANDOM_SPEED_MIN, clamp(t, 0, 1));
}

/** Window Width at a 0..1 path value: RANDOM_WIDTH_MIN to RANDOM_WIDTH_MAX in a straight line. */
inline double randomWidthAt(double t) noexcept {
  return RANDOM_WIDTH_MIN + (RANDOM_WIDTH_MAX - RANDOM_WIDTH_MIN) * clamp(t, 0, 1);
}

/**
 * The value `phase` (0..1) into a step that left `from` for `to`, at a Slew (0..1, a share of the step): a straight
 * line over the first `slew` of the step, then held. Slew 0 is a hard step; Slew 1 arrives exactly as the next value
 * is drawn.
 */
inline double randomSlewed(double from, double to, double phase, double slew) noexcept {
  const double s = clamp(slew, 0, 1);
  if (s <= 0 || phase >= s) return to;
  return from + (to - from) * (phase / s);
}

/**
 * The output (−1..1, i.e. −100..+100) for a value `u` (0..1), a Window Placement (0..1) and a Window Width
 * (RANDOM_WIDTH_MIN..RANDOM_WIDTH_MAX: 0.5 is 50 units, the narrowest, 1 is 100, the widest). Placement slides the
 * window across the room left: its low edge is −1 + placement · (2 − width). At Width 1 that is −100..0 (placement 0),
 * −50..+50 (0.5), 0..+100 (1), the LFO's window at full Level; Width 0.5 at Placement 0.6 is −10..+40.
 */
inline double randomOutput(double u, double placement, double width) noexcept {
  const double w = clamp(width, RANDOM_WIDTH_MIN, RANDOM_WIDTH_MAX);
  return -1 + clamp(placement, 0, 1) * (2 - w) + w * clamp(u, 0, 1);
}

}  // namespace dsp
