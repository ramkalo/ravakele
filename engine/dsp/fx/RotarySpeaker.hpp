/**
 * The rotary speaker: a two-rotor cabinet whose rotation path can be made wider or narrower.
 *
 * The input is summed to mono (a cabinet has one input) and split at one crossover by a Linkwitz-Riley 24 dB/oct pair
 * (util/svf.hpp, as fx.crossover-split): the highs feed the horn (the high rotor), the lows the drum (the low rotor).
 * Each rotor turns at its own Rate and moves its band three ways at once, as a speaker carried round a circle does:
 *
 *   Pan      — where the speaker is across the field: Width · sin θ (constant power, util/pan.hpp).
 *   Doppler  — how near it is: its delay is ROTARY_BASE_S − Width · reach · cos θ (nearer is sooner), so a turning
 *              rotor bends the pitch up as it comes round and down as it goes.
 *   Tremolo  — which way it faces: its gain is 1 − Width · am · (1 − cos θ) / 2, loudest facing the listener.
 *
 * Width is the diameter of the rotation path, 0..1: at 0 the rotor sits still in the middle (no pan, no Doppler, no
 * tremolo) and the two bands sum back to the input (the crossover is an allpass), ROTARY_BASE_S late; at 1 it swings
 * the whole field with its full Doppler and tremolo.
 *
 * Each rotor has inertia: its speed (rate times direction) eases toward its target over a time constant, the horn's
 * HORN_INERTIA_S (settles in about 1 s) and the drum's DRUM_INERTIA_S (about 4 s), so moving Rate spins a rotor up or
 * down as a motor does. Both turn the same way (θ rising: they swing right first); Flip reverses a rotor, which slows
 * it through standstill and spins it up the other way. The angle is an accumulator, so nothing jumps.
 *
 * Mix is the wet/dry, passed to process(). The crossover glides over ROTARY_GLIDE_MS on a log scale.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "../util/math.hpp"
#include "../util/pan.hpp"
#include "../util/svf.hpp"

namespace dsp {

/** Each rotor's Rate, Hz: the pad's left (0) to its right (1) on a log scale. */
constexpr double ROTARY_RATE_MIN_HZ = 0.1;
constexpr double ROTARY_RATE_MAX_HZ = 10;
/** The crossover between the drum (below) and the horn (above), Hz, on a log scale. */
constexpr double ROTARY_CROSSOVER_MIN_HZ = 200;
constexpr double ROTARY_CROSSOVER_MAX_HZ = 2000;
constexpr double ROTARY_CROSSOVER_DEFAULT_HZ = 800;
/** How long the crossover takes to glide to a new setting (one-pole time constant, ms). */
constexpr double ROTARY_GLIDE_MS = 20;
/** The rotors' resting delay, seconds: room for the Doppler either way. */
constexpr double ROTARY_BASE_S = 0.001;
/** How far each rotor's delay swings either way at full Width, seconds (a horn of ~15 cm, a drum of ~7 cm, over c). */
constexpr double HORN_REACH_S = 0.0004;
constexpr double DRUM_REACH_S = 0.0002;
/** How deep each rotor's tremolo is at full Width (the gain facing away is 1 − am). */
constexpr double HORN_AM = 0.5;
constexpr double DRUM_AM = 0.3;
/** Each rotor's inertia: the time constant its speed eases with, seconds (three of them settle it to 95%). */
constexpr double HORN_INERTIA_S = 1.0 / 3;
constexpr double DRUM_INERTIA_S = 4.0 / 3;

/** A 0..1 position as a Rate in Hz: ROTARY_RATE_MIN_HZ · (MAX / MIN)^t. */
inline double rotaryRateHz(double t) noexcept {
  return ROTARY_RATE_MIN_HZ * std::pow(ROTARY_RATE_MAX_HZ / ROTARY_RATE_MIN_HZ, clamp(t, 0, 1));
}

/** A 0..1 position as a crossover in Hz: ROTARY_CROSSOVER_MIN_HZ · (MAX / MIN)^t. */
inline double rotaryCrossoverHz(double t) noexcept {
  return ROTARY_CROSSOVER_MIN_HZ * std::pow(ROTARY_CROSSOVER_MAX_HZ / ROTARY_CROSSOVER_MIN_HZ, clamp(t, 0, 1));
}

class RotarySpeaker {
 public:
  /** The two rotors, by index. */
  static constexpr int HORN = 0;
  static constexpr int DRUM = 1;

  void prepare(double sampleRate) {
    this->sampleRate = sampleRate;
    const std::size_t length =
        static_cast<std::size_t>(std::ceil((ROTARY_BASE_S + std::max(HORN_REACH_S, DRUM_REACH_S)) * sampleRate)) + 8;
    for (Rotor& r : rotors) r.line.assign(length, 0.0f);
    rotors[HORN].reach = HORN_REACH_S * sampleRate;
    rotors[DRUM].reach = DRUM_REACH_S * sampleRate;
    rotors[HORN].am = HORN_AM;
    rotors[DRUM].am = DRUM_AM;
    rotors[HORN].inertia = 1 - std::exp(-1 / (HORN_INERTIA_S * sampleRate));
    rotors[DRUM].inertia = 1 - std::exp(-1 / (DRUM_INERTIA_S * sampleRate));
    base = ROTARY_BASE_S * sampleRate;
    glide = 1 - std::exp(-1 / (sampleRate * ROTARY_GLIDE_MS * 0.001));
    reset();
  }

  /** Clears the lines and filters, puts both rotors at the front, and jumps their speeds and the crossover to their settings. */
  void reset() noexcept {
    for (Rotor& r : rotors) {
      std::fill(r.line.begin(), r.line.end(), 0.0f);
      r.angle = 0;
      r.speed = r.target;
    }
    w = 0;
    for (svf::Stage& s : lp) s.reset();
    for (svf::Stage& s : hp) s.reset();
    currentLog = targetLog;
    coeffs = svf::coefficients(std::tan((PI * std::exp(currentLog)) / sampleRate));
  }

  /**
   * Rates in Hz (clamped to ROTARY_RATE_MIN_HZ..MAX), widths 0..1, crossover in Hz (clamped to its range), flips 0/1
   * (from 0.5 up the rotor turns backwards). Block-rate.
   */
  void setParams(double highRate, double highWidth, double lowRate, double lowWidth, double crossover, double flipLow,
                 double flipHigh) noexcept {
    set(rotors[HORN], highRate, highWidth, flipHigh);
    set(rotors[DRUM], lowRate, lowWidth, flipLow);
    targetLog = std::log(clamp(crossover, ROTARY_CROSSOVER_MIN_HZ, ROTARY_CROSSOVER_MAX_HZ));
  }

  /** In place, stereo. `mix` is 0 (dry) to 1 (wet). */
  void process(float* left, float* right, int offset, int n, double mix) DSP_NONBLOCKING {
    const double wet = clamp(mix, 0, 1) * SQRT2;  // a centred rotor's constant-power gains (√½ each side) back to unity
    const double dry = 1 - clamp(mix, 0, 1);
    const int length = static_cast<int>(rotors[0].line.size());
    const double step = 2 * PI / sampleRate;
    int wi = w;
    for (int s = 0; s < n; s++) {
      if (currentLog != targetLog) {
        currentLog += (targetLog - currentLog) * glide;
        if (std::fabs(targetLog - currentLog) < 1e-9) currentLog = targetLog;
        coeffs = svf::coefficients(std::tan((PI * std::exp(currentLog)) / sampleRate));
      }
      const int i = offset + s;
      const double xL = left[i];
      const double xR = right[i];
      const double x = (xL + xR) * 0.5;
      rotors[DRUM].line[wi] = static_cast<float>(lp[1].low(lp[0].low(x, coeffs), coeffs));
      rotors[HORN].line[wi] = static_cast<float>(hp[1].high(hp[0].high(x, coeffs), coeffs));
      double yL = 0, yR = 0;
      for (Rotor& r : rotors) {
        r.speed += (r.target - r.speed) * r.inertia;
        r.angle += r.speed * step;
        if (r.angle >= 2 * PI) r.angle -= 2 * PI;
        else if (r.angle < 0) r.angle += 2 * PI;
        const double sn = std::sin(r.angle);
        const double cs = std::cos(r.angle);
        const double y = tap(r.line.data(), length, wi, base - r.width * r.reach * cs) * (1 - r.width * r.am * (1 - cs) * 0.5);
        const PanGains g = panGains(r.width * sn);
        yL += y * g.l;
        yR += y * g.r;
      }
      wi = wi + 1 >= length ? 0 : wi + 1;
      left[i] = static_cast<float>(xL * dry + yL * wet);
      right[i] = static_cast<float>(xR * dry + yR * wet);
    }
    w = wi;
  }

  /** Rotor r's speed right now, Hz, signed (negative: turning backwards). For the tests. */
  double speed(int r) const noexcept { return rotors[r].speed; }

  /** Rotor r's angle right now, radians, 0 facing the listener, rising toward the right. For the tests. */
  double angle(int r) const noexcept { return rotors[r].angle; }

 private:
  static constexpr double SQRT2 = 1.41421356237309504880;

  struct Rotor {
    std::vector<float> line;
    double angle = 0;   // radians
    double speed = 0;   // Hz, signed
    double target = 0;  // Hz, signed: rate times direction
    double width = 0;
    double reach = 0;  // samples
    double am = 0;
    double inertia = 1;
  };

  double sampleRate = 48000;
  Rotor rotors[2];
  int w = 0;
  double base = 0;
  double glide = 1;
  double targetLog = std::log(ROTARY_CROSSOVER_DEFAULT_HZ);
  double currentLog = std::log(ROTARY_CROSSOVER_DEFAULT_HZ);
  svf::Coefficients coeffs;
  svf::Stage lp[2];
  svf::Stage hp[2];

  static void set(Rotor& r, double rate, double width, double flip) noexcept {
    r.target = clamp(rate, ROTARY_RATE_MIN_HZ, ROTARY_RATE_MAX_HZ) * (flip >= 0.5 ? -1 : 1);
    r.width = clamp(width, 0, 1);
  }

  /** The line `len` samples behind the write index `w`, 4-point Hermite interpolated. */
  static double tap(const float* line, int length, int w, double len) noexcept {
    double pos = w - len;
    if (pos < 0) pos += length;
    const int i1 = static_cast<int>(pos);
    const double f = pos - i1;
    const int i0 = i1 == 0 ? length - 1 : i1 - 1;
    const int i2 = i1 + 1 >= length ? i1 + 1 - length : i1 + 1;
    const int i3 = i2 + 1 >= length ? i2 + 1 - length : i2 + 1;
    const double y0 = line[i0], y1 = line[i1], y2 = line[i2], y3 = line[i3];
    const double c1 = 0.5 * (y2 - y0);
    const double c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3;
    const double c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
  }
};

}  // namespace dsp
