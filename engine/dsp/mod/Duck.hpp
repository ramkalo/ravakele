/**
 * Duck: a one-shot dip fired by trigger notes, as a modulation source (the mod mode source "Duck"), for sidechain-style
 * ducking of anything a route reaches (a level, a filter's cutoff, an envelope's sustain).
 *
 *   The trigger  a note-on on Channel (1..16) whose note is between Lowest note and Highest note (either order) fires
 *                it; runtime.hpp then plays no voice for that note. Note-offs do nothing: the dip always runs out.
 *   The dip      0..1, from the value it has now (0 at rest) up to Depth over Attack ms, bowed by Attack bend; Depth
 *                for Hold ms; then Depth down to 0 over Release ms, bowed by Release bend. Both bends are the response
 *                curve (util/responseCurve.hpp):
 *                  attack  value = from + (Depth − from) × responseCurve(t / Attack, Attack bend, 0, 1)
 *                  release value = responseCurve(t / Release, Release bend, Depth, 0)
 *                A new trigger mid-dip starts the attack again from the value it has, so it never jumps.
 *
 * The value is unipolar 0..1; a Mod mode route with Negative polarity pulls its target down by it (mod-mode.js gives
 * a Duck route Negative by default). Shared or Global owns one per Duck block: prepare(sampleRate), setParams on param
 * changes, trigger() when accepts(channel, note), and advance(n) once per block for the value. Nothing allocates.
 */
#pragma once

#include <algorithm>

#include "../util/math.hpp"
#include "../util/responseCurve.hpp"

namespace dsp {

constexpr int DUCK_IDLE = 0;
constexpr int DUCK_ATTACK = 1;
constexpr int DUCK_HOLD = 2;
constexpr int DUCK_RELEASE = 3;

constexpr double DUCK_CHANNEL_DEFAULT = 10;
constexpr double DUCK_LOWEST_DEFAULT = 36;
constexpr double DUCK_HIGHEST_DEFAULT = 36;
constexpr double DUCK_DEPTH_DEFAULT = 1;
constexpr double DUCK_ATTACK_MAX = 500;
constexpr double DUCK_ATTACK_DEFAULT = 5;
constexpr double DUCK_ATTACK_BEND_DEFAULT = 0;
constexpr double DUCK_HOLD_MAX = 1000;
constexpr double DUCK_HOLD_DEFAULT = 50;
constexpr double DUCK_RELEASE_MIN = 1;
constexpr double DUCK_RELEASE_MAX = 2000;
constexpr double DUCK_RELEASE_DEFAULT = 250;
constexpr double DUCK_RELEASE_BEND_DEFAULT = 0;

class Duck {
 public:
  void prepare(double sampleRate) noexcept {
    sr = sampleRate > 0 ? sampleRate : 48000;
    reset();
  }

  /** At rest: no dip. */
  void reset() noexcept {
    stageNow = DUCK_IDLE;
    elapsed = 0;
    value = 0;
    from = 0;
  }

  /** Channel 1..16 and the notes 0..127 (rounded), Depth 0..1, times in ms, bends −1..1. */
  void setParams(double channelValue, double lowestValue, double highestValue, double depthValue, double attackMs, double attackBendValue,
                 double holdMs, double releaseMs, double releaseBendValue) noexcept {
    channel = static_cast<int>(clamp(roundHalfUp(channelValue), 1, 16));
    const int a = static_cast<int>(clamp(roundHalfUp(lowestValue), 0, 127));
    const int b = static_cast<int>(clamp(roundHalfUp(highestValue), 0, 127));
    lowest = std::min(a, b);
    highest = std::max(a, b);
    depth = clamp(depthValue, 0, 1);
    attack = clamp(attackMs, 0, DUCK_ATTACK_MAX);
    attackBend = clamp(attackBendValue, -1, 1);
    hold = clamp(holdMs, 0, DUCK_HOLD_MAX);
    release = clamp(releaseMs, DUCK_RELEASE_MIN, DUCK_RELEASE_MAX);
    releaseBend = clamp(releaseBendValue, -1, 1);
  }

  /** Whether a note-on on `noteChannel` (1..16) for `note` fires this dip. */
  bool accepts(int noteChannel, int note) const noexcept { return noteChannel == channel && note >= lowest && note <= highest; }

  /** Starts the dip again, its attack from the value it has now. */
  void trigger() noexcept {
    from = value;
    stageNow = DUCK_ATTACK;
    elapsed = 0;
    settle();
  }

  /** Steps a block of n samples and returns the value at its end, 0..1. */
  double advance(int n) DSP_NONBLOCKING {
    if (stageNow == DUCK_IDLE) return value = 0;
    elapsed += n * 1000.0 / sr;
    settle();
    return value;
  }

  /** The value now, 0..1. */
  double current() const noexcept { return value; }

  /** Which part of the dip it is in: DUCK_IDLE, DUCK_ATTACK, DUCK_HOLD or DUCK_RELEASE. */
  int stage() const noexcept { return stageNow; }

  /** How far through that part, 0..1 (0 at rest). */
  double progress() const noexcept {
    const double length = stageNow == DUCK_ATTACK ? attack : stageNow == DUCK_HOLD ? hold : stageNow == DUCK_RELEASE ? release : 0;
    return length > 0 ? clamp(elapsed / length, 0, 1) : 0;
  }

 private:
  /** Moves past every part `elapsed` has run through (a 0 ms part is passed at once), then sets the value. */
  void settle() noexcept {
    if (stageNow == DUCK_ATTACK && elapsed >= attack) {
      elapsed -= attack;
      stageNow = DUCK_HOLD;
    }
    if (stageNow == DUCK_HOLD && elapsed >= hold) {
      elapsed -= hold;
      stageNow = DUCK_RELEASE;
    }
    if (stageNow == DUCK_RELEASE && elapsed >= release) {
      stageNow = DUCK_IDLE;
      elapsed = 0;
    }
    if (stageNow == DUCK_ATTACK) value = from + (depth - from) * responseCurve(elapsed / attack, attackBend, 0, 1);
    else if (stageNow == DUCK_HOLD) value = depth;
    else if (stageNow == DUCK_RELEASE) value = responseCurve(elapsed / release, releaseBend, depth, 0);
    else value = 0;
  }

  double sr = 48000;
  int channel = static_cast<int>(DUCK_CHANNEL_DEFAULT);
  int lowest = static_cast<int>(DUCK_LOWEST_DEFAULT);
  int highest = static_cast<int>(DUCK_HIGHEST_DEFAULT);
  double depth = DUCK_DEPTH_DEFAULT;
  double attack = DUCK_ATTACK_DEFAULT;
  double attackBend = DUCK_ATTACK_BEND_DEFAULT;
  double hold = DUCK_HOLD_DEFAULT;
  double release = DUCK_RELEASE_DEFAULT;
  double releaseBend = DUCK_RELEASE_BEND_DEFAULT;
  int stageNow = DUCK_IDLE;
  double elapsed = 0;  // ms into the current part
  double value = 0;
  double from = 0;  // where the attack started
};

}  // namespace dsp
