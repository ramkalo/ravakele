/**
 * Pitch shift: every note the voices play, moved by a whole number of semitones, up or down, while it is on.
 *
 *   semitones() = On ? (Direction == Up ? +1 : −1) × Semitones : 0        (Semitones 0..PITCH_SHIFT_MAX, whole)
 *
 * On is what the pitch shift key holds (the player's `pitch.hold` action, Shift by default): off by default, so the
 * shift sounds only while the player holds the key.
 *
 * The host adds semitones() to each note before its glide (dsp::Portamento): a new note glides to its shifted pitch,
 * and a change retargets the notes already sounding, so the shift moves at the portamento's speed (at once with no
 * portamento). Pure arithmetic.
 */
#pragma once

#include "../util/math.hpp"

namespace dsp {

constexpr double PITCH_SHIFT_MAX = 12;
constexpr int PITCH_SHIFT_DOWN = 0;
constexpr int PITCH_SHIFT_UP = 1;
constexpr const char* PITCH_SHIFT_DIRECTION_LABELS[] = {"Down", "Up"};
constexpr int PITCH_SHIFT_DIRECTION_DEFAULT = PITCH_SHIFT_UP;

class PitchShift {
 public:
  /** semitones 0..12 (rounded to whole), direction 0 Down, 1 Up (rounded), on 0 off, 1 on (rounded). */
  void setParams(double semitonesParam, double directionParam, double onParam) noexcept {
    amount = clamp(roundHalfUp(semitonesParam), 0, PITCH_SHIFT_MAX);
    direction = static_cast<int>(clamp(roundHalfUp(directionParam), 0, 1));
    on = roundHalfUp(onParam) >= 1;
  }

  /** The shift in semitones: 0 while off, else +amount Up, −amount Down. */
  double semitones() const noexcept { return !on ? 0 : direction == PITCH_SHIFT_UP ? amount : -amount; }

 private:
  double amount = 0;
  int direction = PITCH_SHIFT_DIRECTION_DEFAULT;
  bool on = false;
};

}  // namespace dsp
