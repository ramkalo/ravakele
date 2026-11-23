/**
 * The arpeggiator: a note-engine module that plays the notes being held one
 * at a time, in a pattern, on a clock.
 *
 * Notes come in (noteOn / noteOff from the keyboard, MIDI, or another note
 * block) and are kept in pitch order. While any are held and Run is on, the
 * arpeggiator steps through them at `division` notes per beat of `tempo`:
 *
 *   - up: lowest to highest, then the same an octave up, for `octaves` octaves;
 *   - down: the same, highest first;
 *   - up-down: up then back down, without repeating the top and bottom notes;
 *   - random: any of them, from the seeded generator (the same seed and the
 *     same held notes always give the same run).
 *
 * Each step's note sounds for `gate` of the step. Like the sequencer, the
 * clock is sample-accurate: the host asks how far it may render before the
 * next event (advance), renders that far, and calls again. Letting go of every
 * note releases what is sounding and rewinds, so the next chord starts the
 * pattern from its first step.
 *
 * Its parameters are the manifest's (note.arpeggiator): run, mode, division,
 * octaves, gate, tempo, seed. The host passes their current values each call.
 * Nothing allocates after construction.
 */
#pragma once

#include <cmath>
#include <cstdint>

#include "../util/math.hpp"
#include "../util/random.hpp"
#include "NoteSink.hpp"

namespace dsp {

enum class ArpMode : int { Up = 0, Down = 1, UpDown = 2, Random = 3 };
/** The mode parameter's stops, in order. */
constexpr const char* ARP_MODES[] = {"up", "down", "upDown", "random"};
/** The division parameter's stops: notes per beat (1/4, 1/8, 1/8 triplet, 1/16, 1/16 triplet, 1/32). */
constexpr double ARP_DIVISIONS[] = {1, 2, 3, 4, 6, 8};
constexpr const char* ARP_DIVISION_LABELS[] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
/** At most this many notes are held at once; more are ignored until one is let go. */
constexpr int ARP_MAX_HELD = 16;
constexpr int ARP_MAX_OCTAVES = 4;

/** The arpeggiator's parameter values for one call. */
struct ArpSettings {
  double run;
  /** Index into ARP_MODES. */
  double mode;
  /** Index into ARP_DIVISIONS. */
  double division;
  double octaves;
  /** Share of the step the note sounds, 0.05..1. */
  double gate;
  /** Beats per minute. */
  double tempo;
  double seed;
};

/** A number as JavaScript's `x | 0` makes it: truncated and wrapped to a signed 32-bit integer. */
inline int toInt32(double x) noexcept { return static_cast<std::int32_t>(wrapToUint32(x)); }

/**
 * The pattern position `step` of `count` held notes over `octaves` octaves as an index into the expanded run
 * (held note index + octave · count), or −1 when nothing is held. Random is not a fixed pattern; see Arpeggiator.
 */
inline int arpIndex(ArpMode mode, int step, int count, int octaves) noexcept {
  const int length = count * octaves;
  if (length <= 0) return -1;
  if (mode == ArpMode::Down) return length - 1 - (step % length);
  if (mode == ArpMode::UpDown) {
    if (length == 1) return 0;
    const int period = 2 * length - 2, at = step % period;
    return at < length ? at : period - at;
  }
  return step % length;
}

class Arpeggiator {
 public:
  void prepare(double sampleRate) {
    this->sampleRate = sampleRate;
    reset();
  }

  void reset() noexcept {
    count = 0;
    running = false;
    step = 0;
    untilNextStep = 0;
    untilGateOff = 0;
    sounding = -1;
    rng.reset(seed);
  }

  /** How many notes are held. */
  int heldCount() const noexcept { return count; }

  /** The note sounding now, or −1. */
  int current() const noexcept { return sounding; }

  /** A note is held: kept in pitch order (a repeat only updates its velocity). */
  void noteOn(int note, double vel) noexcept {
    for (int i = 0; i < count; i++) if (held[i] == note) { velocity[i] = static_cast<float>(vel); return; }
    if (count >= ARP_MAX_HELD) return;
    int at = count;
    while (at > 0 && held[at - 1] > note) {
      held[at] = held[at - 1];
      velocity[at] = velocity[at - 1];
      at--;
    }
    held[at] = note;
    velocity[at] = static_cast<float>(vel);
    count++;
  }

  /** A note is let go. */
  void noteOff(int note) noexcept {
    int at = -1;
    for (int i = 0; i < count; i++) if (held[i] == note) { at = i; break; }
    if (at < 0) return;
    for (int i = at; i < count - 1; i++) {
      held[i] = held[i + 1];
      velocity[i] = velocity[i + 1];
    }
    count--;
  }

  /**
   * Fire whatever is due now and say how many of the next `maxSamples` may be
   * rendered before something else is due.
   */
  int advance(int maxSamples, const ArpSettings& settings, NoteSink& sink) DSP_NONBLOCKING {
    const bool play = settings.run >= 0.5 && count > 0;
    if (!play) {
      if (running) stop(sink, settings);
      return maxSamples;
    }
    if (!running) {
      running = true;
      step = 0;
      seed = toInt32(settings.seed);
      rng.reset(seed);
      beginStep(settings, sink);
    }
    if (sounding >= 0 && untilGateOff <= 0) release(sink);
    if (untilNextStep <= 0) {
      step++;
      beginStep(settings, sink);
    }
    int n = untilNextStep;
    if (sounding >= 0 && untilGateOff < n) n = untilGateOff;
    if (n > maxSamples) n = maxSamples;
    untilNextStep -= n;
    if (sounding >= 0) untilGateOff -= n;
    return n;
  }

 private:
  double sampleRate = 48000;
  std::int32_t held[ARP_MAX_HELD] = {};
  float velocity[ARP_MAX_HELD] = {};
  int count = 0;
  bool running = false;
  int step = 0;
  int untilNextStep = 0;
  int untilGateOff = 0;
  /** The note sounding now, or −1. */
  int sounding = -1;
  Rng rng{1};
  double seed = 1;

  void stop(NoteSink& sink, const ArpSettings& settings) noexcept {
    if (sounding >= 0) release(sink);
    running = false;
    step = 0;
    untilNextStep = 0;
    untilGateOff = 0;
    seed = toInt32(settings.seed);
    rng.reset(seed);
  }

  /** The step comes round: size it from the clock, pick its note, strike it. */
  void beginStep(const ArpSettings& settings, NoteSink& sink) noexcept {
    const int divisionIndex = toInt32(settings.division);
    const double division = divisionIndex >= 0 && divisionIndex < 6 ? ARP_DIVISIONS[divisionIndex] : ARP_DIVISIONS[1];
    const double tempo = settings.tempo > 1 ? settings.tempo : 1;
    const double stepLength = roundHalfUp((60 / (tempo * division)) * sampleRate);
    const int length = static_cast<int>(stepLength > 1 ? stepLength : 1);
    untilNextStep = length;
    double gate = settings.gate;
    if (gate > 1) gate = 1;
    if (gate < 0.05) gate = 0.05;
    const double gateLength = roundHalfUp(length * gate);
    untilGateOff = static_cast<int>(gateLength > 1 ? gateLength : 1);

    int octaves = toInt32(settings.octaves);
    if (octaves < 1) octaves = 1;
    if (octaves > ARP_MAX_OCTAVES) octaves = ARP_MAX_OCTAVES;
    const int modeIndex = toInt32(settings.mode);
    const ArpMode mode = modeIndex >= 0 && modeIndex < 4 ? static_cast<ArpMode>(modeIndex) : ArpMode::Up;
    const int runLength = count * octaves;
    int index;
    if (mode == ArpMode::Random) {
      index = static_cast<int>(std::floor((rng.next() * 0.5 + 0.5) * runLength));
      if (index >= runLength) index = runLength - 1;
    } else {
      index = arpIndex(mode, step, count, octaves);
    }
    const int heldIndex = index % count, octave = (index - heldIndex) / count;
    sounding = held[heldIndex] + 12 * octave;
    sink.noteOn(sounding, velocity[heldIndex]);
  }

  void release(NoteSink& sink) noexcept {
    sink.noteOff(sounding);
    sounding = -1;
  }
};

}  // namespace dsp
