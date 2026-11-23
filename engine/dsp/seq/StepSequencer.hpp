/**
 * The step sequencer: a bar cut into steps, each step a column of the grid,
 * each lit cell in the column a scale degree that fires when the step comes
 * round. The clock is sample-accurate: the host asks how far it may render
 * before the next event, renders that far, and lets the event fire — so a
 * step never waits for a block boundary.
 *
 * Nothing here decides anything. The grid is what the user painted, the
 * degrees are the tonic and scale the user chose, and a column with three
 * cells lit plays three notes. The settings are read at each step, so a
 * change lands at the next step rather than moving the one under way.
 *
 * Run      — on starts the bar from its first step at once; off releases
 *            whatever is sounding and rewinds.
 * Tempo    — 30..300 BPM; a bar is four beats.
 * Steps    — how many columns make a bar (SEQ_STEP_COUNTS).
 * Tonic, Scale, Octave — where the seven degrees (rows) land.
 * Gate     — how much of its step a note sounds, 0.05..1.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "../note/NoteSink.hpp"
#include "../util/math.hpp"

namespace dsp {

/** Rows of the grid: the scale's seven degrees. */
constexpr int SEQ_ROWS = 7;
/** Columns of the grid; Steps says how many are played. */
constexpr int MAX_SEQ_STEPS = 16;
constexpr int SEQ_STEP_COUNTS[] = {3, 4, 6, 8, 12, 16};
constexpr const char* TONIC_LABELS[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
constexpr const char* SCALE_LABELS[] = {"Major", "Nat Min", "Harm Min", "Mel Min"};
/** Each scale's seven degrees, in semitones above the tonic. */
constexpr int SCALE_INTERVALS[4][SEQ_ROWS] = {
    {0, 2, 4, 5, 7, 9, 11},
    {0, 2, 3, 5, 7, 8, 10},
    {0, 2, 3, 5, 7, 8, 11},
    {0, 2, 3, 5, 7, 9, 11},
};

class StepSequencer {
 public:
  StepSequencer() noexcept {
    for (int s = 0; s < MAX_SEQ_STEPS; s++) grid[(s % SEQ_ROWS) * MAX_SEQ_STEPS + s] = 1;
  }

  void prepare(double sampleRate) {
    this->sampleRate = sampleRate;
    reset();
  }

  /** Stopped and rewound, nothing sounding (no note-offs are sent: a panic). The settings and grid are kept. */
  void reset() noexcept {
    running = false;
    step = 0;
    untilGateOff = 0;
    untilNextStep = 0;
    gateOpen = false;
    std::fill(std::begin(sounding), std::end(sounding), -1);
  }

  /** run 0/1, tempo BPM, steps as an index into SEQ_STEP_COUNTS, tonic 0..11, scale 0..3, octave 1..7, gate 0.05..1. */
  void setParams(double run, double tempo, double steps, double tonic, double scale, double octave, double gate) noexcept {
    this->run = run >= 0.5;
    this->tempo = clamp(tempo, 30, 300);
    stepsIndex = static_cast<int>(clamp(roundHalfUp(steps), 0, 5));
    this->tonic = static_cast<int>(clamp(roundHalfUp(tonic), 0, 11));
    this->scale = static_cast<int>(clamp(roundHalfUp(scale), 0, 3));
    this->octave = static_cast<int>(clamp(roundHalfUp(octave), 1, 7));
    this->gate = clamp(gate, 0.05, 1);
  }

  /** Light or clear one cell: degree `row` (0..6) on column `step`. */
  void setCell(int row, int column, double on) noexcept {
    if (row < 0 || row >= SEQ_ROWS || column < 0 || column >= MAX_SEQ_STEPS) return;
    grid[row * MAX_SEQ_STEPS + column] = on >= 0.5 ? 1 : 0;
  }

  bool cell(int row, int column) const noexcept { return grid[row * MAX_SEQ_STEPS + column] == 1; }

  /** The column under the playhead, or -1 while stopped. */
  int currentStep() const noexcept { return running ? step : -1; }

  /**
   * Fire whatever is due now and say how many of the next `maxSamples` may be
   * rendered before something else is due. Called repeatedly across a block:
   * the host renders the returned count, then calls again from there.
   */
  int advance(int maxSamples, NoteSink& sink) DSP_NONBLOCKING {
    if (run != running) {
      if (run) start(sink);
      else stop(sink);
    }
    if (!running) return maxSamples;

    // Both countdowns may reach zero in the same sample: the gate closes
    // before the next step opens, so a full gate rearticulates rather than
    // holds — a repeated degree is heard twice.
    if (gateOpen && untilGateOff <= 0) closeGate(sink);
    if (untilNextStep <= 0) {
      step++;
      beginStep(sink);
    }

    int n = untilNextStep;
    if (gateOpen && untilGateOff < n) n = untilGateOff;
    if (n > maxSamples) n = maxSamples;
    untilNextStep -= n;
    if (gateOpen) untilGateOff -= n;
    return n;
  }

 private:
  /** Every step's note is struck at this velocity; the mixer sets the level. */
  static constexpr double STEP_VELOCITY = 0.8;
  /** A bar is four beats: at 120 BPM a bar is two seconds, and sixteen steps are eighths of a second. */
  static constexpr double BEATS_PER_BAR = 4;

  double sampleRate = 48000;
  bool running = false;
  /** The column being played. */
  int step = 0;
  /** Samples until the step's notes are released, and until the next step; both count down as the host renders. */
  int untilGateOff = 0;
  int untilNextStep = 0;
  bool gateOpen = false;
  /** The note sounding for each row, or -1: released by number, so a tonic change mid-step releases the right one. */
  std::int32_t sounding[SEQ_ROWS] = {};

  bool run = false;
  double tempo = 120;
  int stepsIndex = 1;
  int tonic = 0;
  int scale = 0;
  int octave = 4;
  double gate = 0.5;
  /** The grid, [row * MAX_SEQ_STEPS + step]; it starts as one cell per column walking up the degrees. */
  std::uint8_t grid[SEQ_ROWS * MAX_SEQ_STEPS] = {};

  void start(NoteSink& sink) noexcept {
    running = true;
    step = 0;
    beginStep(sink);
  }

  void stop(NoteSink& sink) noexcept {
    if (gateOpen) closeGate(sink);
    reset();
  }

  /** The step comes round: read the settings, size the step, strike the column. */
  void beginStep(NoteSink& sink) noexcept {
    const int steps = SEQ_STEP_COUNTS[stepsIndex];
    if (step >= steps) step = 0;

    const double stepSeconds = (BEATS_PER_BAR * 60) / (tempo * steps);
    const double stepLength = roundHalfUp(stepSeconds * sampleRate);
    const int length = static_cast<int>(stepLength > 1 ? stepLength : 1);
    untilNextStep = length;
    const double gateLength = roundHalfUp(length * gate);
    untilGateOff = static_cast<int>(gateLength > 1 ? gateLength : 1);

    const int* intervals = SCALE_INTERVALS[scale];
    // MIDI: C4 is 60, so octave 4 puts the tonic at 12 * (4 + 1) + its pitch class.
    const int base = 12 * (octave + 1) + tonic;

    gateOpen = true;
    for (int row = 0; row < SEQ_ROWS; row++) {
      if (grid[row * MAX_SEQ_STEPS + step] == 0) continue;
      const int note = base + intervals[row];
      sounding[row] = note;
      sink.noteOn(note, STEP_VELOCITY);
    }
  }

  void closeGate(NoteSink& sink) noexcept {
    gateOpen = false;
    for (int row = 0; row < SEQ_ROWS; row++) {
      const int note = sounding[row];
      if (note < 0) continue;
      sounding[row] = -1;
      sink.noteOff(note);
    }
  }
};

}  // namespace dsp
