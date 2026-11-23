/**
 * The main filters' continuous shape: one number that morphs
 *
 *   LP 8p → LP 4p → LP 2p → LP Shelf → BP → HP Shelf → HP 2p → HP 4p
 *   → HP 8p → Notch
 *
 * Whole numbers are the stops (FILTER_SHAPE_LABELS); between two the
 * response is a blend of both. Every stop and every blend is one setting of
 * the same machine: a cascade of up to four SVF stages, of which the output
 * is a weighted sum of three *taps* — a response (lowpass, highpass, bandpass
 * or notch) read at a stage. So
 *
 *   - between two orders (LP 8p → LP 4p) the taps are the lowpass after
 *     stage 4 and after stage 2, cross-faded, with every stage's damping
 *     blended between the two orders' Butterworth tables — exact Butterworth
 *     at either end, and the resonance sits on the last stage of whichever
 *     order is nearer;
 *   - the middle of the range is one stage: its lowpass or highpass, its
 *     notch and its bandpass. Lowpass alone is LP 2p, highpass alone is
 *     HP 2p, and a shelf lets the notch through at SHELF. The notch is
 *     lowpass + highpass, so LP Shelf is lp + SHELF·hp — the stop band sits
 *     exactly SHELF down. Each shelf cross-fades with the bandpass between
 *     them, which alone is BP;
 *   - HP 8p → Notch cross-fades the highpass after stage 4 with the notch of
 *     stage 1.
 *
 * `morphInto` fills a MorphState without allocating, so the audio path can
 * call it every block; the UI draws the response with a JavaScript copy of
 * this function, held to it by a parity test, so the picture is the filter.
 */
#pragma once

#include <cmath>

namespace dsp {

constexpr int MAX_STAGES = 4;

/** The stops, in order: shape 0..SHAPE_MAX. */
constexpr const char* FILTER_SHAPE_LABELS[] = {"LP 8p", "LP 4p", "LP 2p", "LP Shelf", "BP", "HP Shelf", "HP 2p", "HP 4p", "HP 8p", "Notch"};

/** A response read at a stage — or the stage's own input, dry. */
enum class Tap : int { Lp = 0, Hp = 1, Bp = 2, Notch = 3, Dry = 4 };

/**
 * Damping (k = 1/Q) per stage for a Butterworth cascade of the given order.
 * Indexed by stage count, then by stage. Cascading identical stages would
 * drag the -3 dB point down as the order rose; these keep cutoff meaning the
 * same frequency at every slope.
 */
constexpr double BUTTERWORTH_K[5][4] = {
    {},
    {1.4142136},
    {1.8477590, 0.7653669},
    {1.9318517, 1.4142136, 0.5176381},
    {1.9615706, 1.6629392, 1.1111404, 0.3901806},
};

/** Damping of the resonant stage at full resonance — close to self-oscillation. */
constexpr double K_AT_FULL_RES = 0.03;

/** How far a shelf holds its stop band down, as an amplitude: -12 dB. */
constexpr double SHELF = 0.25118864;

/** The last stop, and so the shape parameter's range. */
constexpr int SHAPE_MAX = 9;

/** The stops as numbers. */
namespace FilterShape {
enum : int { Lp8 = 0, Lp4 = 1, Lp2 = 2, LpShelf = 3, Bp = 4, HpShelf = 5, Hp2 = 6, Hp4 = 7, Hp8 = 8, Notch = 9 };
}

struct MorphState {
  /** Stages to run, 1..MAX_STAGES. */
  int stages = 1;
  /** The response each stage feeds the next. */
  Tap chain = Tap::Lp;
  Tap typeA = Tap::Lp;
  int stageA = 0;
  double wA = 1;
  Tap typeB = Tap::Lp;
  int stageB = 0;
  double wB = 0;
  Tap typeC = Tap::Lp;
  int stageC = 0;
  double wC = 0;
  /** Damping per stage, resonance applied. */
  float k[MAX_STAGES] = {};
};

namespace morph_detail {

/** Stage `s`'s damping in an order-`n` Butterworth cascade, the last stage resonant. */
inline double kFor(int n, int s, double resonance) noexcept {
  const double k = BUTTERWORTH_K[n][s];
  return s == n - 1 ? k + (K_AT_FULL_RES - k) * resonance : k;
}

/**
 * A cross-fade between two orders of one response (or of two responses,
 * HP 8p → Notch): `nA` stages read by tap A, `nB` by tap B, every stage's
 * damping blended so each end is exactly its own Butterworth. Tap C is silent.
 */
inline void blend(MorphState& out, Tap chain, Tap typeA, int nA, Tap typeB, int nB, double t, double resonance) noexcept {
  out.chain = chain;
  out.stages = nA > nB ? nA : nB;
  out.typeA = typeA; out.stageA = nA - 1; out.wA = 1 - t;
  out.typeB = typeB; out.stageB = nB - 1; out.wB = t;
  out.typeC = Tap::Lp; out.stageC = 0; out.wC = 0;
  for (int s = 0; s < out.stages; s++) {
    const double kA = s < nA ? kFor(nA, s, resonance) : kFor(nB, s, resonance);
    const double kB = s < nB ? kFor(nB, s, resonance) : kFor(nA, s, resonance);
    out.k[s] = static_cast<float>(kA + (kB - kA) * t);
  }
}

/**
 * One stage: its response (tap A) with `notch` of its notch let through (tap B) — a shelf — cross-faded with its
 * bandpass (tap C), `band` of it. The three taps stand through the whole middle of the range, so only their weights move.
 */
inline void shelfBand(MorphState& out, Tap type, double notch, double band, double resonance) noexcept {
  out.chain = type;
  out.stages = 1;
  out.typeA = type; out.stageA = 0; out.wA = (1 - band) * (1 - notch);
  out.typeB = Tap::Notch; out.stageB = 0; out.wB = (1 - band) * notch;
  out.typeC = Tap::Bp; out.stageC = 0; out.wC = band;
  out.k[0] = static_cast<float>(kFor(1, 0, resonance));
}

}  // namespace morph_detail

/** The setting for a shape (0..SHAPE_MAX, fractions between the stops) at a resonance (0..1). */
inline void morphInto(double shape, double resonance, MorphState& out) noexcept {
  using namespace morph_detail;
  const double r = resonance < 0 ? 0 : resonance > 1 ? 1 : resonance;
  const double s = shape < 0 ? 0 : shape > SHAPE_MAX ? SHAPE_MAX : shape;
  int i = static_cast<int>(std::floor(s));
  if (i == SHAPE_MAX) i = SHAPE_MAX - 1;
  const double t = s - i;
  switch (i) {
    case 0: blend(out, Tap::Lp, Tap::Lp, 4, Tap::Lp, 2, t, r); return;
    case 1: blend(out, Tap::Lp, Tap::Lp, 2, Tap::Lp, 1, t, r); return;
    // LP 2p → LP Shelf: the stop band rises from nothing to the shelf.
    case 2: shelfBand(out, Tap::Lp, t * SHELF, 0, r); return;
    // LP Shelf → BP: the shelf cross-fades with the bandpass.
    case 3: shelfBand(out, Tap::Lp, SHELF, t, r); return;
    // BP → HP Shelf: the bandpass cross-fades with the highpass shelf.
    case 4: shelfBand(out, Tap::Hp, SHELF, 1 - t, r); return;
    // HP Shelf → HP 2p: the stop band falls away to nothing.
    case 5: shelfBand(out, Tap::Hp, SHELF * (1 - t), 0, r); return;
    case 6: blend(out, Tap::Hp, Tap::Hp, 1, Tap::Hp, 2, t, r); return;
    case 7: blend(out, Tap::Hp, Tap::Hp, 2, Tap::Hp, 4, t, r); return;
    default: blend(out, Tap::Hp, Tap::Hp, 4, Tap::Notch, 1, t, r); return;
  }
}

}  // namespace dsp
