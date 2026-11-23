/**
 * Mod wheel: a wheel's position as a modulation source (the mod mode source "WHEEL"), one value for the whole
 * instrument. Two things move it: the mouse scroll wheel (scroll(travel), relative) and a MIDI mod wheel (CC 1,
 * control(raw), absolute). Several can run at once, each with its own curve and window.
 *
 *   On           off: it ignores both inputs, its position goes back to rest and it gives 0 (no modulation).
 *   Sensitivity  how far a scroll moves it: WHEEL_TRAVEL px of scroll covers the whole range at 1, half that at 2:
 *                  position += travel × Sensitivity / WHEEL_TRAVEL, clamped 0..1 (rest is 0)
 *   Return       0 holds the position where it was left; above 0 the position falls back to rest, a one-pole fall
 *                with that time, 0..WHEEL_RETURN_MAX ms, always acting (a scroll moves against it).
 *   The curve    the mod amount 0..1 for each position, as aftertouch's: Min is its height at rest, Max at the top,
 *                and Bend bows it between them (util/responseCurve.hpp):
 *                  u = responseCurve(position, Bend, Min, Max)
 *   Smoothing    a one-pole glide of u (util/blockSmoother.hpp), 0..WHEEL_SMOOTHING_MAX ms, so a notched wheel or a
 *                7-bit controller does not step.
 *   The window   how the smoothed u is read as modulation, the LFO's Polarity Window (lfoMath.hpp lfoOutput at level 1):
 *                  out = window − 0.5 + (u − 0.5), so 0 is −1..0, 0.5 is −0.5..+0.5, 1 is 0..+1.
 *
 * A host owns one per Mod wheel block. Call prepare(sampleRate), setParams on param changes, scroll(travel) for each
 * scroll (px, up positive), control(raw) for each CC 1 message, and advance(n) once per block for the value. Nothing
 * allocates.
 */
#pragma once

#include <cmath>

#include "../util/blockSmoother.hpp"
#include "../util/math.hpp"
#include "../util/responseCurve.hpp"
#include "lfoMath.hpp"

namespace dsp {

/** The scroll travel, px, that covers the whole range at Sensitivity 1. */
constexpr double WHEEL_TRAVEL = 1000;
constexpr double WHEEL_ON_DEFAULT = 1;
constexpr double WHEEL_SENSITIVITY_MIN = 0.1;
constexpr double WHEEL_SENSITIVITY_MAX = 4;
constexpr double WHEEL_SENSITIVITY_DEFAULT = 1;
constexpr double WHEEL_RETURN_MAX = 5000;
constexpr double WHEEL_RETURN_DEFAULT = 0;
constexpr double WHEEL_MIN_DEFAULT = 0;
constexpr double WHEEL_MAX_DEFAULT = 1;
constexpr double WHEEL_BEND_DEFAULT = 0;
constexpr double WHEEL_SMOOTHING_MAX = 500;
constexpr double WHEEL_SMOOTHING_DEFAULT = 20;
constexpr double WHEEL_WINDOW_DEFAULT = 0.5;

class WheelSource {
 public:
  void prepare(double sampleRate) noexcept {
    rate = sampleRate > 0 ? sampleRate : 48000;
    smoother.prepare(rate);
    smoother.reset(curve());
  }

  /** On 0/1, Sensitivity ×, Return ms, Min and Max (the curve's ends) 0..1, Bend −1..1, Smoothing ms, Window 0..1. */
  void setParams(double onValue, double sensitivityValue, double returnMs, double minValue, double maxValue, double bendValue,
                 double smoothingMs, double windowValue) noexcept {
    on = onValue >= 0.5;
    sensitivity = clamp(sensitivityValue, WHEEL_SENSITIVITY_MIN, WHEEL_SENSITIVITY_MAX);
    returnTime = clamp(returnMs, 0, WHEEL_RETURN_MAX);
    min = clamp(minValue, 0, 1);
    max = clamp(maxValue, 0, 1);
    bend = clamp(bendValue, -1, 1);
    window = clamp(windowValue, 0, 1);
    smoother.setTime(clamp(smoothingMs, 0, WHEEL_SMOOTHING_MAX));
    if (!on) position = 0;
  }

  /** A scroll of `travel` px (up positive) moves the position by its share of the range; ignored while off. */
  void scroll(double travel) noexcept {
    if (on) position = clamp(position + travel * sensitivity / WHEEL_TRAVEL, 0, 1);
  }

  /** A MIDI mod wheel message (CC 1, 0..127) sets the position; ignored while off. */
  void control(double raw) noexcept {
    if (on) position = clamp(raw / 127, 0, 1);
  }

  /** The wheel at rest, at once. */
  void reset() noexcept {
    position = 0;
    smoother.reset(curve());
  }

  /** Steps a block of n samples and returns the output, −1..1: the smoothed curve value placed by the window; 0 while off. */
  double advance(int n) DSP_NONBLOCKING {
    if (!on) {
      smoother.reset(curve());
      return 0;
    }
    if (returnTime > 0 && n > 0) position *= std::exp(-n / (rate * returnTime * 0.001));
    return lfoOutput(smoother.advance(curve(), n), 1, window);
  }

  /** Where the wheel is, 0..1. */
  double at() const noexcept { return position; }

  /** The curve's value for the position, 0..1, before smoothing. */
  double curve() const noexcept { return responseCurve(position, bend, min, max); }

 private:
  BlockSmoother smoother;
  double rate = 48000;
  bool on = WHEEL_ON_DEFAULT >= 0.5;
  double sensitivity = WHEEL_SENSITIVITY_DEFAULT;
  double returnTime = WHEEL_RETURN_DEFAULT;
  double min = WHEEL_MIN_DEFAULT;
  double max = WHEEL_MAX_DEFAULT;
  double bend = WHEEL_BEND_DEFAULT;
  double window = WHEEL_WINDOW_DEFAULT;
  double position = 0;
};

}  // namespace dsp
