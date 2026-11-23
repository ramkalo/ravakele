/**
 * Key tracking: the key a note was played on as a modulation source (the mod mode source "KEY"), set at note-on and held
 * for the note. It follows the key, not the pitch: glide and pitch bend never move it.
 *
 * Two separate things:
 *   The curve   the mod amount 0..1 for each key across the whole keyboard: Min is its height at MIDI note 0, Max at
 *               127, and Bend bows it between them with the response curve (util/responseCurve.hpp):
 *                 u = responseCurve(key / 127, Bend, Min, Max)
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

constexpr double KEYTRACK_HIGHEST_KEY = 127;
constexpr double KEYTRACK_MIN_DEFAULT = 0;
constexpr double KEYTRACK_MAX_DEFAULT = 1;
constexpr double KEYTRACK_BEND_DEFAULT = 0;
constexpr double KEYTRACK_WINDOW_DEFAULT = 0.5;

/** The curve's value for a key (MIDI note number): Min at key 0, Max at 127, bowed by Bend. */
inline double keytrackCurve(double key, double min, double max, double bend) noexcept {
  return responseCurve(key / KEYTRACK_HIGHEST_KEY, bend, min, max);
}

class Keytrack {
 public:
  /** Min and Max (the curve's ends) 0..1, Bend −1..1, Window 0..1. */
  void setParams(double minValue, double maxValue, double bendValue, double windowValue) noexcept {
    min = clamp(minValue, 0, 1);
    max = clamp(maxValue, 0, 1);
    bend = clamp(bendValue, -1, 1);
    window = clamp(windowValue, 0, 1);
  }

  /** A note starts on `key` (MIDI note number); its value holds until the next. */
  void noteOn(double noteKey) noexcept { key = noteKey; }

  /** The curve's value for the held key, 0..1. */
  double curve() const noexcept { return keytrackCurve(key, min, max, bend); }

  /** The output now, −1..1: the curve's value placed by the window. */
  double value() const noexcept { return lfoOutput(curve(), 1, window); }

 private:
  double min = KEYTRACK_MIN_DEFAULT;
  double max = KEYTRACK_MAX_DEFAULT;
  double bend = KEYTRACK_BEND_DEFAULT;
  double window = KEYTRACK_WINDOW_DEFAULT;
  double key = 60;
};

}  // namespace dsp
