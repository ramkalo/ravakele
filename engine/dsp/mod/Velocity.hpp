/**
 * Velocity: the velocity a note was played at as a modulation source (the mod mode source "VEL"), set at note-on and
 * held for the note. Several can run at once, each with its own curve and window, so each target can answer velocity
 * its own way.
 *
 * Two separate things:
 *   The curve   the mod amount 0..1 for each velocity: Min is its height at the softest note, Max at the hardest, and
 *               Bend bows it between them with the response curve (util/responseCurve.hpp):
 *                 u = responseCurve(velocity, Bend, Min, Max)
 *   The window  how that 0..1 is read as modulation, the LFO's Polarity Window (lfoMath.hpp lfoOutput at level 1):
 *                 out = window − 0.5 + (u − 0.5), so 0 is −1..0, 0.5 is −0.5..+0.5, 1 is 0..+1.
 *
 * Pure state and arithmetic; nothing allocates.
 */
#pragma once

#include "../util/math.hpp"
#include "../util/responseCurve.hpp"
#include "lfoMath.hpp"

namespace dsp {

constexpr double VELOCITY_MIN_DEFAULT = 0;
constexpr double VELOCITY_MAX_DEFAULT = 1;
constexpr double VELOCITY_BEND_DEFAULT = 0;
constexpr double VELOCITY_WINDOW_DEFAULT = 0.5;

/** The curve's value 0..1 for a velocity 0..1. */
inline double velocityCurve(double velocity, double min, double max, double bend) noexcept { return responseCurve(velocity, bend, min, max); }

class VelocitySource {
 public:
  /** Min and Max (the curve's ends) 0..1, Bend −1..1, Window 0..1. */
  void setParams(double minValue, double maxValue, double bendValue, double windowValue) noexcept {
    min = clamp(minValue, 0, 1);
    max = clamp(maxValue, 0, 1);
    bend = clamp(bendValue, -1, 1);
    window = clamp(windowValue, 0, 1);
  }

  /** A note starts at `noteVelocity` (0..1); its value holds until the next. */
  void noteOn(double noteVelocity) noexcept { velocity = clamp(noteVelocity, 0, 1); }

  /** The curve's value for the held velocity, 0..1. */
  double curve() const noexcept { return velocityCurve(velocity, min, max, bend); }

  /** The output now, −1..1: the curve's value placed by the window. */
  double value() const noexcept { return lfoOutput(curve(), 1, window); }

 private:
  double min = VELOCITY_MIN_DEFAULT;
  double max = VELOCITY_MAX_DEFAULT;
  double bend = VELOCITY_BEND_DEFAULT;
  double window = VELOCITY_WINDOW_DEFAULT;
  double velocity = 1;
};

}  // namespace dsp
