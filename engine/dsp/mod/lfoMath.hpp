/**
 * The LFO's shared arithmetic: the wave a Shape value draws, how the Level and the Polarity Window place it, how far
 * Rate and Attack reach, and which division of the beat a synced rate snaps to. The engine runs on these, and the UI
 * (library/ui/lfo-pad.js) draws its wave from a JavaScript copy of the same arithmetic, held to this file by a parity
 * test (src/shared/lfo-pad-parity.test.js), so the picture is the LFO.
 */
#pragma once

#include <cmath>
#include <limits>

#include "../util/math.hpp"

namespace dsp {

/** The slowest and fastest rate, in Hz (exponential between). */
constexpr double LFO_RATE_MIN = 0.01;
constexpr double LFO_RATE_MAX = 20;
/** The shortest and longest fade-in after a note-on, in seconds (a cube taper between). At the shortest it is instant. */
constexpr double LFO_ATTACK_MIN = 0.001;
constexpr double LFO_ATTACK_MAX = 20;
/** The narrowest pulse at Shape 1, as a share of the cycle. */
constexpr double LFO_PULSE_MIN = 0.05;
/** How long Level and the window glide to a new value (one-pole time constant, ms). */
constexpr double LFO_GLIDE_MS = 10;

/** The waves along Shape 0..1, evenly spaced; each neighbouring pair crossfades. The last is the narrowest pulse. */
constexpr const char* LFO_SHAPE_ANCHORS[] = {"Saw up", "Triangle", "Sine", "Triangle", "Saw down", "Square", "Pulse"};

struct LfoDivision {
  double beats;
  const char* label;
};

/** The divisions of a beat a synced cycle may last, in beats, shortest first, with their names. */
constexpr LfoDivision LFO_DIVISIONS[] = {
    {1.0 / 8, "1/32"},   {1.0 / 6, "1/16T"}, {1.0 / 4, "1/16"}, {1.0 / 3, "1/8T"},  {3.0 / 8, "1/16·"},  {1.0 / 2, "1/8"},
    {2.0 / 3, "1/4T"},   {3.0 / 4, "1/8·"},  {1, "1/4"},        {4.0 / 3, "1/2T"},  {3.0 / 2, "1/4·"},   {2, "1/2"},
    {3, "1/2·"},         {4, "1 bar"},       {8, "2 bars"},     {16, "4 bars"},     {32, "8 bars"},
};
constexpr int LFO_DIVISION_COUNT = sizeof(LFO_DIVISIONS) / sizeof(LFO_DIVISIONS[0]);

namespace lfo_detail {

inline double wrap(double phase) noexcept { return phase - std::floor(phase); }

/** A 0..1 position on an exponential range. */
inline double expRange(double t, double lo, double hi) noexcept { return lo * std::pow(hi / lo, clamp(t, 0, 1)); }

/** A triangle peaking at `peak` (0..1 of the cycle): peak 1 is a rising saw, 0.5 a triangle, 0 a falling saw. 0..1. */
inline double skewed(double phase, double peak) noexcept {
  if (phase < peak) return phase / peak;
  return peak >= 1 ? 1 : (1 - phase) / (1 - peak);
}
inline double sine(double phase) noexcept { return 0.5 - 0.5 * std::cos(2 * PI * phase); }
inline double pulse(double phase, double width) noexcept { return phase < width ? 1 : 0; }

}  // namespace lfo_detail

/** Rate in Hz at a 0..1 path value (the manifest's exp taper). */
inline double lfoRateHz(double t) noexcept { return lfo_detail::expRange(t, LFO_RATE_MIN, LFO_RATE_MAX); }
/** Attack in seconds at a 0..1 path value (the manifest's cube taper: a tenth of the way is 20 ms, half is 2.5 s). */
inline double lfoAttackSeconds(double t) noexcept {
  const double c = clamp(t, 0, 1);
  return LFO_ATTACK_MIN + (LFO_ATTACK_MAX - LFO_ATTACK_MIN) * c * c * c;
}

/**
 * The wave at a Shape (0..1) and a phase (0..1 of the cycle), unipolar 0..1. Shape is cut into six even stretches
 * between LFO_SHAPE_ANCHORS: Saw up → Triangle and Triangle → Saw down move the peak of one skewed triangle;
 * Triangle ↔ Sine and Saw down → Square crossfade; Square → Pulse narrows the high part from half the cycle to
 * LFO_PULSE_MIN. Every wave starts its cycle at its low point except the falling saw and the pulses, which start high,
 * so neighbours line up.
 */
inline double lfoWave(double shape, double phase) noexcept {
  using namespace lfo_detail;
  const double s = clamp(shape, 0, 1) * 6, p = wrap(phase);
  const int seg = s >= 6 ? 5 : static_cast<int>(std::floor(s));
  const double t = s - seg;
  switch (seg) {
    case 0: return skewed(p, 1 - 0.5 * t);
    case 1: return skewed(p, 0.5) * (1 - t) + sine(p) * t;
    case 2: return sine(p) * (1 - t) + skewed(p, 0.5) * t;
    case 3: return skewed(p, 0.5 - 0.5 * t);
    case 4: return skewed(p, 0) * (1 - t) + pulse(p, 0.5) * t;
    default: return pulse(p, 0.5 + (LFO_PULSE_MIN - 0.5) * t);
  }
}

/**
 * The output (−1..1, i.e. −100..+100) for a unipolar wave value `u`, a Level (0..1) and a Polarity Window (0..1), faded
 * in by `gain` (0..1). The window is 100 units wide and slides from −100..0 (window 0) through −50..+50 (0.5) to 0..+100
 * (1); Level scales the whole swing toward 0, a depth: −70..+30 at Level 50% is −35..+15, and Level 0 is no
 * modulation. The fade grows the swing out of its center, so a note starts at level · (window − 0.5).
 */
inline double lfoOutput(double u, double level, double window, double gain = 1) noexcept {
  return clamp(level, 0, 1) * (clamp(window, 0, 1) - 0.5 + gain * (u - 0.5));
}

/** The division nearest a rate at a tempo, by ratio, as an index into LFO_DIVISIONS. */
inline int syncedLfoDivision(double hz, double bpm) noexcept {
  const double beat = 60 / (bpm > 1 ? bpm : 1);
  const double want = -std::log(hz > LFO_RATE_MIN ? hz : LFO_RATE_MIN);
  int best = 0;
  double nearest = std::numeric_limits<double>::infinity();
  for (int i = 0; i < LFO_DIVISION_COUNT; i++) {
    const double d = std::fabs(std::log(LFO_DIVISIONS[i].beats * beat) - want);
    if (d < nearest) { nearest = d; best = i; }
  }
  return best;
}

/** The rate in Hz: free, or with Sync on one cycle per the nearest division of the beat. */
inline double lfoCycleHz(double hz, double sync, double bpm) noexcept {
  const double free = clamp(hz, LFO_RATE_MIN, LFO_RATE_MAX);
  if (sync < 0.5) return free;
  return 1 / (LFO_DIVISIONS[syncedLfoDivision(free, bpm)].beats * (60 / (bpm > 1 ? bpm : 1)));
}

}  // namespace dsp
