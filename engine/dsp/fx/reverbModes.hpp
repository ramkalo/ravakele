/**
 * The reverb's four characters as tunings of one machine (Reverb.hpp): a bank
 * of parallel combs into a chain of allpasses. A mode says how many combs
 * run and at what delays, how many diffuser stages and how long, the
 * diffuser's feedback, and how far the right channel's lines are offset.
 * Indexed by ReverbMode (below).
 *
 * Delays are in milliseconds at any sample rate; Size scales them
 * REVERB_SIZE_MIN..REVERB_SIZE_MAX, and the lines are allocated for the longest mode at
 * REVERB_SIZE_MAX so nothing is ever resized.
 */
#pragma once

#include <array>
#include <cmath>

#include "../util/math.hpp"

namespace dsp {

constexpr int MAX_COMBS = 8;
constexpr int MAX_ALLPASS = 8;

struct ReverbTuning {
  double combMs[MAX_COMBS];
  int combCount;
  double apMs[MAX_ALLPASS];
  int apCount;
  double apFeedback;
  double spreadMs;
};

/** Size 0 halves the mode's delays, size 1 makes them half as long again. */
constexpr double REVERB_SIZE_MIN = 0.5;
constexpr double REVERB_SIZE_MAX = 1.5;

/**
 * The curve's second bank decays this much faster than the tail (its T60
 * times this): at a half the bloom swells over a tenth of the tail's time
 * and sits 6 dB under it thereafter.
 */
constexpr double CURVE_RATIO = 0.5;

/** The pre-delay's reach, in seconds. */
constexpr double PREDELAY_MAX_S = 0.25;

/** The tilt's reach at either extreme, in dB, either side of TILT_HZ. */
constexpr double TILT_DB = 12;
constexpr double TILT_HZ = 1000;

/** The most the damping one-pole may close: at 1 the loop would pass nothing but DC. */
constexpr double DAMPING_MAX = 0.95;

/** The modes, in order. */
namespace ReverbMode {
enum : int { Spring = 0, Plate = 1, Hall = 2, Room = 3 };
}
constexpr const char* REVERB_MODE_LABELS[] = {"Spring", "Plate", "Hall", "Room"};
constexpr int REVERB_MODE_COUNT = 4;

namespace reverb_detail {

/** Freeverb's eight combs and four allpasses, in ms — the Room and the Hall are these scaled. */
constexpr double FV_COMBS[] = {25.31, 26.94, 28.96, 30.75, 32.24, 33.81, 35.31, 36.67};
constexpr double FV_APS[] = {12.61, 10.0, 7.73, 5.10};

inline double scaled(double ms, double by) noexcept { return roundHalfUp(ms * by * 100) / 100; }

inline std::array<ReverbTuning, REVERB_MODE_COUNT> build() noexcept {
  std::array<ReverbTuning, REVERB_MODE_COUNT> modes{};
  // Spring: three sparse combs — the boing — into a chain of near-identical
  // short allpasses fed back hard, which is what disperses a transient into
  // a spring's chirp.
  modes[0] = {{31.7, 36.1, 40.3}, 3, {2.0, 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7}, 8, 0.72, 0};
  // Plate: short, dense combs and a long diffuser, so the tail is thick at once.
  modes[1] = {{15.1, 17.3, 19.9, 21.7, 23.9, 26.3, 28.1, 30.7}, 8, {1.3, 1.9, 2.7, 3.5, 4.3, 5.3, 6.1, 7.1}, 8, 0.65, 1.0};
  // Hall: Freeverb's tuning stretched — long combs, the diffuser with them.
  modes[2] = {{}, 8, {}, 4, 0.5, 0.6};
  for (int i = 0; i < 8; i++) modes[2].combMs[i] = scaled(FV_COMBS[i], 1.6);
  for (int i = 0; i < 4; i++) modes[2].apMs[i] = scaled(FV_APS[i], 1.5);
  // Room: Freeverb's tuning shrunk.
  modes[3] = {{}, 8, {}, 4, 0.5, 0.5};
  for (int i = 0; i < 8; i++) modes[3].combMs[i] = scaled(FV_COMBS[i], 0.7);
  for (int i = 0; i < 4; i++) modes[3].apMs[i] = FV_APS[i];
  return modes;
}

}  // namespace reverb_detail

/** Indexed by ReverbMode: Spring, Plate, Hall, Room. Built once, on first use. */
inline const std::array<ReverbTuning, REVERB_MODE_COUNT>& reverbModes() noexcept {
  static const std::array<ReverbTuning, REVERB_MODE_COUNT> modes = reverb_detail::build();
  return modes;
}

/** The longest comb any mode asks for, in ms, before Size and spread. */
inline double reverbMaxCombMs() noexcept {
  double most = 0;
  for (const ReverbTuning& t : reverbModes()) for (int i = 0; i < t.combCount; i++) most = t.combMs[i] > most ? t.combMs[i] : most;
  return most;
}
/** The longest allpass any mode asks for, in ms, before Size and spread. */
inline double reverbMaxApMs() noexcept {
  double most = 0;
  for (const ReverbTuning& t : reverbModes()) for (int i = 0; i < t.apCount; i++) most = t.apMs[i] > most ? t.apMs[i] : most;
  return most;
}
/** The widest right-channel offset any mode asks for, in ms. */
inline double reverbMaxSpreadMs() noexcept {
  double most = 0;
  for (const ReverbTuning& t : reverbModes()) most = t.spreadMs > most ? t.spreadMs : most;
  return most;
}

inline double sizeFactor(double size) noexcept {
  const double s = size < 0 ? 0 : size > 1 ? 1 : size;
  return REVERB_SIZE_MIN + s * (REVERB_SIZE_MAX - REVERB_SIZE_MIN);
}

/** A delay length a mode runs at a size, in ms — what the engine sets and the UI draws. */
inline double scaledMs(double ms, double size) noexcept { return ms * sizeFactor(size); }

/**
 * A comb's feedback for a tail of `t60` seconds at a delay of `samples`:
 * every comb loses 60 dB in the same time whatever its length, which is
 * what Time means.
 */
inline double combGain(double samples, double t60, double sampleRate) noexcept {
  return std::pow(10.0, (-3 * samples) / (t60 * sampleRate));
}

}  // namespace dsp
