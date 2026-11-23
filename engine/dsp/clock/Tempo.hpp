/**
 * The synth's tempo: the one BPM every tempo-synced block follows (a step sequencer's steps, a synced LFO's cycle, a
 * synced delay's time, an arpeggiator's division), and what it is set to follow.
 *
 * Tempo — beats per minute, TEMPO_MIN_BPM..TEMPO_MAX_BPM. A synth with no Tempo block runs at TEMPO_DEFAULT_BPM.
 * Clock source — what the tempo and the transport follow: Internal (the Tempo param), Host (the plugin host's
 *   transport) or MIDI (incoming MIDI clock, 24 per quarter note, with start, stop and continue).
 *
 * Pure arithmetic; nothing allocates.
 */
#pragma once

#include "../util/math.hpp"

namespace dsp {

constexpr double TEMPO_MIN_BPM = 30;
constexpr double TEMPO_MAX_BPM = 300;
constexpr double TEMPO_DEFAULT_BPM = 120;

constexpr int CLOCK_INTERNAL = 0;
constexpr int CLOCK_HOST = 1;
constexpr int CLOCK_MIDI = 2;
constexpr const char* CLOCK_SOURCE_LABELS[] = {"Internal", "Host", "MIDI"};

class Tempo {
 public:
  /** Tempo in BPM (clamped to its range) and clock source 0..2 (rounded). */
  void setParams(double tempo, double clockSource) noexcept {
    beats = clamp(tempo, TEMPO_MIN_BPM, TEMPO_MAX_BPM);
    clock = static_cast<int>(clamp(roundHalfUp(clockSource), 0, 2));
  }

  /** The BPM every synced block follows. */
  double bpm() const noexcept { return beats; }

  int clockSource() const noexcept { return clock; }

 private:
  double beats = TEMPO_DEFAULT_BPM;
  int clock = CLOCK_INTERNAL;
};

}  // namespace dsp
