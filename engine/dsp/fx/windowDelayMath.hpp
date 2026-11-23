/**
 * The window delay's shared arithmetic: how far its time reaches, which
 * division of the sequencer's beat a synced time snaps to, and where on the
 * stereo field the ping and the pong land. The engine sets its lines and pans
 * from these, and the UI (library/ui/delay-quad-pad.js) draws the window from a
 * JavaScript copy held to this file by a parity test, so the picture is the delay.
 */
#pragma once

#include <cmath>
#include <limits>

#include "../util/math.hpp"
#include "../util/pan.hpp"

namespace dsp {

/** The shortest and longest echo time, in seconds. */
constexpr double DELAY_MIN_S = 0.001;
constexpr double DELAY_MAX_S = 2;
/** The most of an echo that feeds back: below unity, so a tail always ends. */
constexpr double FEEDBACK_MAX = 0.95;
/** How long the pans take to glide to a new window or side (one-pole time constant, ms). */
constexpr double PAN_GLIDE_MS = 20;

/** Which side the first echo (the ping) lands on. */
constexpr int SIDE_LEFT = 0;
constexpr int SIDE_RIGHT = 1;
constexpr const char* SIDE_LABELS[] = {"Left first", "Right first"};

struct DelayDivision {
  double beats;
  const char* label;
};

/** The divisions of a beat a synced time may be, in beats, shortest first, with their names. */
constexpr DelayDivision DELAY_DIVISIONS[] = {
    {1.0 / 8, "1/32"}, {1.0 / 6, "1/16T"}, {1.0 / 4, "1/16"}, {1.0 / 3, "1/8T"}, {3.0 / 8, "1/16·"},
    {1.0 / 2, "1/8"},  {3.0 / 4, "1/8·"},  {1, "1/4"},        {3.0 / 2, "1/4·"}, {2, "1/2"},
};
constexpr int DELAY_DIVISION_COUNT = sizeof(DELAY_DIVISIONS) / sizeof(DELAY_DIVISIONS[0]);

/** The division nearest a time at a tempo, by ratio, as an index into DELAY_DIVISIONS. */
inline int syncedDivision(double seconds, double bpm) noexcept {
  const double beat = 60 / (bpm > 1 ? bpm : 1);
  const double want = std::log(seconds > 1e-4 ? seconds : 1e-4);
  int best = 0;
  double nearest = std::numeric_limits<double>::infinity();
  for (int i = 0; i < DELAY_DIVISION_COUNT; i++) {
    const double d = std::fabs(std::log(DELAY_DIVISIONS[i].beats * beat) - want);
    if (d < nearest) { nearest = d; best = i; }
  }
  return best;
}

/** A time snapped to the nearest division of the beat at a tempo, in seconds. */
inline double syncedSeconds(double seconds, double bpm) noexcept {
  return DELAY_DIVISIONS[syncedDivision(seconds, bpm)].beats * (60 / (bpm > 1 ? bpm : 1));
}

/** The echo time in seconds: free, or with Sync on snapped to the beat; always inside DELAY_MIN_S..DELAY_MAX_S. */
inline double echoSeconds(double time, double sync, double bpm) noexcept {
  return clamp(sync >= 0.5 ? syncedSeconds(time, bpm) : time, DELAY_MIN_S, DELAY_MAX_S);
}

struct WindowEdges {
  double left, right, ping, pong;
};

/**
 * The pan window's two edges and which is the ping. Positions run −1 (hard
 * left) … +1 (hard right); center is −1..1 and width 0..1 of the whole L…R
 * span, so the edges are center ∓ width (width 1 reaches from −1 to +1 around
 * 0), each clamped to the field on its own. Side Left: the ping lands on the
 * left edge and the pong on the right; Side Right the other way round.
 */
inline WindowEdges windowEdges(double center, double width, double side) noexcept {
  const double c = clamp(center, -1, 1);
  const double half = clamp(width, 0, 1);
  const double left = clamp(c - half, -1, 1);
  const double right = clamp(c + half, -1, 1);
  return side >= 0.5 ? WindowEdges{left, right, right, left} : WindowEdges{left, right, left, right};
}

}  // namespace dsp
