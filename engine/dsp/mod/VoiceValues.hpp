/**
 * Voice values: a round robin of up to sixteen values 0..1, a modulation source that differs from note to note (the mod
 * mode source "VOICE"). Each new note takes the next value in turn, whatever voice plays it, so it works the same at
 * one voice as at sixteen: a filter a little brighter on every note, a pan spread across a run.
 *
 * Steps — how many values the round robin cycles through (1..16) before it wraps.
 * Mode — how the values are set:
 *   Manual  each step's own value, v1…v16 (the first Steps of them).
 *   Curve   the response curve (util/responseCurve.hpp) across the steps: step i (0-based) is
 *           responseCurve(i / (Steps − 1), Bend, Start, End), so step 1 is Start and the last End (Start at one step).
 *
 * value(n) is the value of the n-th note (0-based, any n ≥ 0): step n mod Steps. The host counts the notes.
 * Pure state and arithmetic; nothing allocates.
 */
#pragma once

#include "../note/VoiceAllocator.hpp"  // MAX_VOICES
#include "../util/math.hpp"
#include "../util/responseCurve.hpp"

namespace dsp {

constexpr int VOICE_VALUES_MANUAL = 0;
constexpr int VOICE_VALUES_CURVE = 1;
constexpr const char* VOICE_VALUES_MODE_LABELS[] = {"Manual", "Curve"};

/** The value slot s starts with in Manual mode: an even ramp, the same as Curve's defaults. */
constexpr double voiceValueDefault(int slot) noexcept { return static_cast<double>(slot) / (MAX_VOICES - 1); }

class VoiceValues {
 public:
  VoiceValues() noexcept {
    for (int s = 0; s < MAX_VOICES; s++) manual[s] = voiceValueDefault(s);
  }

  /** 0 Manual, 1 Curve (rounded). */
  void setMode(double value) noexcept { mode = static_cast<int>(clamp(roundHalfUp(value), 0, 1)); }

  /** How many values the round robin cycles through, 1..MAX_VOICES (rounded). */
  void setSteps(double value) noexcept { steps = static_cast<int>(clamp(roundHalfUp(value), 1, MAX_VOICES)); }

  /** Step s's own value (Manual), 0..1. */
  void setValue(int slot, double value) noexcept {
    if (slot >= 0 && slot < MAX_VOICES) manual[slot] = clamp(value, 0, 1);
  }

  /** The curve: Start (step 1), End (the last step), each 0..1, and its Bend −1..1. */
  void setCurve(double startValue, double endValue, double bendValue) noexcept {
    start = clamp(startValue, 0, 1);
    end = clamp(endValue, 0, 1);
    bend = clamp(bendValue, -1, 1);
  }

  int currentMode() const noexcept { return mode; }
  int currentSteps() const noexcept { return steps; }

  /** The n-th note's value now (n ≥ 0; step n mod Steps), 0..1. */
  double value(long long note) const noexcept {
    const int s = static_cast<int>((note < 0 ? 0 : note) % steps);
    if (mode == VOICE_VALUES_CURVE) return steps < 2 ? start : responseCurve(static_cast<double>(s) / (steps - 1), bend, start, end);
    return manual[s];
  }

 private:
  int mode = VOICE_VALUES_MANUAL;
  int steps = MAX_VOICES;
  double manual[MAX_VOICES] = {};
  double start = 0;
  double end = 1;
  double bend = 0;
};

}  // namespace dsp
