/**
 * Aftertouch: how hard a held key is pressed, as a modulation source (the mod mode source "AT"), the way Velocity is
 * (mod/Velocity.hpp) but moving while the note is held. Several can run at once, each with its own curve and window,
 * so each target can answer pressure its own way.
 *
 *   Type        which messages it listens to: Channel (channel pressure, one value for every voice), Poly (polyphonic
 *               key pressure, each note its own) or Both (whichever arrives).
 *   The curve   the mod amount 0..1 for each pressure, as velocity's: Min is its height with no pressure, Max at full
 *               pressure, and Bend bows it between them (util/responseCurve.hpp):
 *                 u = responseCurve(pressure / 127, Bend, Min, Max)
 *   Smoothing   a one-pole glide of u toward the latest pressure (util/blockSmoother.hpp), 0..AFTERTOUCH_SMOOTHING_MAX
 *               ms, so a coarse controller does not step.
 *   The window  how the smoothed u is read as modulation, the LFO's Polarity Window (lfoMath.hpp lfoOutput at level 1):
 *                 out = window − 0.5 + (u − 0.5), so 0 is −1..0, 0.5 is −0.5..+0.5, 1 is 0..+1.
 *
 * Each voice owns one per Aftertouch block. Call prepare(sampleRate), setParams on param changes, noteOn() at each note
 * start (no pressure yet), press(raw) for each pressure message it accepts, and advance(n) once per block for the
 * value. Nothing allocates.
 */
#pragma once

#include "../util/blockSmoother.hpp"
#include "../util/math.hpp"
#include "../util/responseCurve.hpp"
#include "lfoMath.hpp"

namespace dsp {

constexpr int AFTERTOUCH_CHANNEL = 0;
constexpr int AFTERTOUCH_POLY = 1;
constexpr int AFTERTOUCH_BOTH = 2;
constexpr const char* AFTERTOUCH_TYPE_LABELS[] = {"Channel", "Poly", "Both"};
constexpr double AFTERTOUCH_TYPE_DEFAULT = AFTERTOUCH_CHANNEL;
constexpr double AFTERTOUCH_MIN_DEFAULT = 0;
constexpr double AFTERTOUCH_MAX_DEFAULT = 1;
constexpr double AFTERTOUCH_BEND_DEFAULT = 0;
constexpr double AFTERTOUCH_SMOOTHING_MAX = 500;
constexpr double AFTERTOUCH_SMOOTHING_DEFAULT = 20;
constexpr double AFTERTOUCH_WINDOW_DEFAULT = 0.5;

class AftertouchSource {
 public:
  void prepare(double sampleRate) noexcept {
    smoother.prepare(sampleRate);
    smoother.reset(target);
  }

  /** type 0..2 (rounded), Min and Max (the curve's ends) 0..1, Bend −1..1, Smoothing ms, Window 0..1. */
  void setParams(double typeValue, double minValue, double maxValue, double bendValue, double smoothingMs, double windowValue) noexcept {
    type = static_cast<int>(clamp(roundHalfUp(typeValue), 0, 2));
    min = clamp(minValue, 0, 1);
    max = clamp(maxValue, 0, 1);
    bend = clamp(bendValue, -1, 1);
    window = clamp(windowValue, 0, 1);
    smoother.setTime(clamp(smoothingMs, 0, AFTERTOUCH_SMOOTHING_MAX));
    target = responseCurve(pressure, bend, min, max);
  }

  /** Whether it listens to channel pressure (poly false) or poly key pressure (poly true). */
  bool accepts(bool poly) const noexcept { return type == AFTERTOUCH_BOTH || (type == AFTERTOUCH_POLY) == poly; }

  /** A pressure message (0..127): the curve's value for it becomes the target the value glides to. */
  void press(double raw) noexcept {
    pressure = clamp(raw / 127, 0, 1);
    target = responseCurve(pressure, bend, min, max);
  }

  /** A new note: no pressure yet, so the value starts at the curve's Min at once. */
  void noteOn() noexcept {
    pressure = 0;
    target = responseCurve(0, bend, min, max);
    smoother.reset(target);
  }

  /** Steps a block of n samples and returns the output, −1..1: the smoothed curve value placed by the window. */
  double advance(int n) DSP_NONBLOCKING { return lfoOutput(smoother.advance(target, n), 1, window); }

  /** The curve's value for the latest pressure, 0..1, before smoothing. */
  double curve() const noexcept { return target; }

 private:
  BlockSmoother smoother;
  int type = AFTERTOUCH_CHANNEL;
  double min = AFTERTOUCH_MIN_DEFAULT;
  double max = AFTERTOUCH_MAX_DEFAULT;
  double bend = AFTERTOUCH_BEND_DEFAULT;
  double window = AFTERTOUCH_WINDOW_DEFAULT;
  double pressure = 0;
  double target = 0;
};

}  // namespace dsp
