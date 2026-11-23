/**
 * A two-band crossover splitter with a switchable low route.
 *
 * The stereo input is split at one crossover by a Linkwitz-Riley 24 dB/oct pair: the lows (below it) and the highs
 * (above it) leave on separate stereo outputs, and low + high sum back to the input's magnitude (an allpass).
 * The highs always leave on the high output — meant for effects (delay, distortion). The lows leave on ONE of two
 * stereo outputs, picked by Route:
 *
 * Route A (0) — the bypass: the low output A, meant to go straight to the mix, untouched.
 * Route B (1) — the FX route: the low output B, meant to go through an effect (e.g. fx.distortion) before the mix.
 *
 * The unused low output is written with zeros, so a host can always mix all three. A route change crossfades the
 * two low outputs over ROUTE_FADE_MS (equal gain: the two carry the same signal), and the crossover glides over
 * GLIDE_MS on a log scale, so neither clicks nor zips.
 *
 * Each band is two cascaded Butterworth (Q = 1/√2) topology-preserving state-variable stages per channel (util/svf.hpp;
 * they stay stable while the crossover moves). The pure constants and crossoverHz() are exported so the UI reads the
 * same range.
 */
#pragma once

#include <cmath>

#include "../util/math.hpp"
#include "../util/svf.hpp"

namespace dsp {

/** The crossover's range, Hz: the bottom of the pad (0) to its top (1), on a log scale. */
constexpr double CROSSOVER_MIN_HZ = 40;
constexpr double CROSSOVER_MAX_HZ = 1000;
constexpr double CROSSOVER_DEFAULT_HZ = 150;
/** How long a route change crossfades the two low outputs, in ms. */
constexpr double ROUTE_FADE_MS = 10;
/** How long the crossover takes to glide to a new setting (one-pole time constant, ms). */
constexpr double GLIDE_MS = 20;

constexpr int ROUTE_A = 0;
constexpr int ROUTE_B = 1;
constexpr const char* ROUTE_LABELS[] = {"A (bypass)", "B (FX)"};

/** A 0..1 position as a crossover in Hz: CROSSOVER_MIN_HZ · (MAX / MIN)^t. */
inline double crossoverHz(double t) noexcept {
  return CROSSOVER_MIN_HZ * std::pow(CROSSOVER_MAX_HZ / CROSSOVER_MIN_HZ, clamp(t, 0, 1));
}

/** A crossover in Hz as its 0..1 position (the inverse of crossoverHz). */
inline double crossoverPosition(double hz) noexcept {
  return std::log(clamp(hz, CROSSOVER_MIN_HZ, CROSSOVER_MAX_HZ) / CROSSOVER_MIN_HZ) / std::log(CROSSOVER_MAX_HZ / CROSSOVER_MIN_HZ);
}

/** The route param (0..1) as a route: below 0.5 is A, from 0.5 up is B. */
inline int routeOf(double v) noexcept { return v >= 0.5 ? ROUTE_B : ROUTE_A; }

struct BandGains {
  double low;
  double high;
};

/**
 * The exact magnitude of each band at a frequency for a crossover (bilinear Butterworth, squared): for drawing and tests.
 * Returns linear gains { low, high }; low + high (complex) is an allpass.
 */
inline BandGains bandGains(double hz, double crossover, double sampleRate = 48000) noexcept {
  const double g = std::tan((PI * clamp(crossover, CROSSOVER_MIN_HZ, CROSSOVER_MAX_HZ)) / sampleRate);
  const double w = std::tan((PI * hz) / sampleRate) / g;  // prewarped, normalised analog frequency
  const double butter = 1 / std::sqrt(1 + std::pow(w, 4));
  const double hb = w * w * butter;
  return {butter * butter, hb * hb};
}

class CrossoverSplit {
 public:
  void prepare(double sampleRate) {
    this->sampleRate = sampleRate;
    glide = 1 - std::exp(-1 / (sampleRate * GLIDE_MS * 0.001));
    const double fadeSamples = sampleRate * ROUTE_FADE_MS * 0.001;
    fadeStep = 1 / (fadeSamples > 1 ? fadeSamples : 1);
    reset();
  }

  /** Clears the filters and jumps the crossover and the route to their settings. */
  void reset() noexcept {
    for (Channel& c : ch) c.reset();
    currentLog = targetLog;
    fade = route;
  }

  /** crossover in Hz (clamped to CROSSOVER_MIN_HZ..CROSSOVER_MAX_HZ), route 0 A (bypass) / 1 B (FX). */
  void setParams(double crossover, double route) noexcept {
    targetLog = std::log(clamp(crossover, CROSSOVER_MIN_HZ, CROSSOVER_MAX_HZ));
    this->route = routeOf(route);
  }

  /** The crossover the filters are at right now (mid-glide), Hz. */
  double crossover() const noexcept { return std::exp(currentLog); }

  /**
   * Splits n samples of inL/inR from offset into the high pair and the two low pairs (A, B), all written from offset.
   * The input buffers are only read, so they may be any of the outputs.
   */
  void process(const float* inL, const float* inR, float* highL, float* highR, float* lowAL, float* lowAR,
               float* lowBL, float* lowBR, int offset, int n) DSP_NONBLOCKING {
    Channel& cl = ch[0];
    Channel& cr = ch[1];
    const double target = route;
    for (int s = 0; s < n; s++) {
      currentLog += (targetLog - currentLog) * glide;
      const svf::Coefficients c = svf::coefficients(std::tan((PI * std::exp(currentLog)) / sampleRate));
      if (fade != target) {
        fade = target > fade ? std::fmin(1, fade + fadeStep) : std::fmax(0, fade - fadeStep);
      }
      const double b = fade;
      const double a = 1 - b;
      const int i = offset + s;
      const double xl = inL[i];
      const double xr = inR[i];
      const double ll = low(cl, xl, c);
      const double lr = low(cr, xr, c);
      highL[i] = static_cast<float>(high(cl, xl, c));
      highR[i] = static_cast<float>(high(cr, xr, c));
      lowAL[i] = static_cast<float>(ll * a);
      lowAR[i] = static_cast<float>(lr * a);
      lowBL[i] = static_cast<float>(ll * b);
      lowBR[i] = static_cast<float>(lr * b);
    }
  }

 private:
  /** One channel: two low-pass stages and two high-pass stages, each run on the stage before it. */
  struct Channel {
    svf::Stage lp[2];
    svf::Stage hp[2];
    void reset() noexcept {
      for (svf::Stage& s : lp) s.reset();
      for (svf::Stage& s : hp) s.reset();
    }
  };

  double sampleRate = 48000;
  double targetLog = std::log(CROSSOVER_DEFAULT_HZ);
  double currentLog = std::log(CROSSOVER_DEFAULT_HZ);
  double glide = 1;
  int route = ROUTE_A;
  /** How far the lows have crossed to output B: 0 all on A, 1 all on B. */
  double fade = 0;
  double fadeStep = 1;
  Channel ch[2];

  /** Two Butterworth low-pass stages: the LR4 low band. */
  static double low(Channel& ch, double x, const svf::Coefficients& c) noexcept { return ch.lp[1].low(ch.lp[0].low(x, c), c); }

  /** Two Butterworth high-pass stages: the LR4 high band. */
  static double high(Channel& ch, double x, const svf::Coefficients& c) noexcept { return ch.hp[1].high(ch.hp[0].high(x, c), c); }
};

}  // namespace dsp
