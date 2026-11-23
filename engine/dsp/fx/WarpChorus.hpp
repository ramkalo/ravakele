/**
 * The warp chorus: four copies of the input, each read back through its own
 * broken clock, panned across the field and mixed with the dry signal.
 *
 * A delay tap whose length moves is a time warp (how fast the length moves is
 * the pitch shift), so each copy is one tap on a shared line and its clock is
 * what moves the tap. The chorus (Chorus.hpp) moves its taps with a sine, the
 * steadiest clock there is; these clocks are unstable in four ways:
 *
 *   Drift     — each clock's rate takes a random walk, held near zero by a
 *               damped spring on its offset (Wander: the spring's natural
 *               frequency, how fast it drifts; Depth: how far, up to
 *               WARP_REACH_S either way), so a clock holds a pitch offset for
 *               a while and is always pulled home, never out of the line.
 *   Stick-slip— now and then a clock sticks: its tap falls behind for ~30 ms
 *               (a pitch dip), then the spring catches it up (Slip: how often
 *               and how hard).
 *   Coupling  — each clock pulls toward the others' rate (Coupling: 0 four
 *               independent clocks, 1 four clocks moving as one).
 *   Spread    — each clock is broken differently (its own wander, depth and
 *               slip, from fixed tables), rests at its own delay and sits at
 *               its own pan; Spread scales all of that from identical and
 *               centred (0) to the tables (1).
 *
 * Mix is the wet/dry, passed to process(); the wet copies are summed at
 * 1/√4, so warping copies sit at about the dry level (still, centred ones at
 * √2 of it). The random source is seeded, so a render repeats exactly.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "../util/math.hpp"
#include "../util/pan.hpp"
#include "../util/random.hpp"

namespace dsp {

/** How many copies, and their delays in seconds: the rest, how far it wanders either way, how far apart they rest at full Spread. */
constexpr int WARP_CLOCKS = 4;
constexpr double WARP_CENTRE_S = 0.012;
constexpr double WARP_REACH_S = 0.008;
constexpr double WARP_REST_SPREAD_S = 0.003;
/**
 * How each clock is broken at full Spread: its wander, depth and slip against the setting, and its place (its rest
 * and its pan). The pad draws the same tables (library/ui/warp-chorus-pad.js; a parity test holds them together).
 */
constexpr double WARP_PLACE[WARP_CLOCKS] = {-1, -1.0 / 3, 1.0 / 3, 1};
constexpr double WARP_WANDER_SPREAD[WARP_CLOCKS] = {1, 1.31, 0.77, 1.62};
constexpr double WARP_DEPTH_SPREAD[WARP_CLOCKS] = {1, 0.85, 1.2, 0.7};
constexpr double WARP_SLIP_SPREAD[WARP_CLOCKS] = {0.6, 1, 1.5, 0.8};

class WarpChorus {
 public:
  void prepare(double sampleRate) {
    this->sampleRate = sampleRate;
    const std::size_t length = static_cast<std::size_t>(std::ceil((WARP_CENTRE_S + WARP_REST_SPREAD_S + WARP_REACH_S) * sampleRate)) + 8;
    line.assign(length, 0.0f);
    reach = WARP_REACH_S * sampleRate;
    slipRelease = std::exp(-1 / (SLIP_RELEASE_S * sampleRate));
    // Between the coherent and the incoherent sum: four still, centred copies add to √2 the dry signal, four warping
    // copies of a broad signal to about its level.
    wetGain = 1 / std::sqrt(static_cast<double>(WARP_CLOCKS));
    reset();
  }

  void reset() noexcept {
    std::fill(line.begin(), line.end(), 0.0f);
    w = 0;
    meanRate = 0;
    for (int i = 0; i < WARP_CLOCKS; i++) {
      Clock& c = clocks[i];
      c.offset = 0;
      c.rate = 0;
      c.slip = 0;
      c.rng.reset(SEED + i * 2654435769.0);
    }
  }

  /** wander in Hz, the rest 0..1. Block-rate. */
  void setParams(double wander, double depth, double slip, double coupling, double spread) noexcept {
    const double hz = wander < 0.05 ? 0.05 : wander;
    const double d = clamp(depth, 0, 1);
    const double s = clamp(slip, 0, 1);
    const double sp = clamp(spread, 0, 1);
    couple = clamp(coupling, 0, 1) * COUPLE_MAX;
    for (int i = 0; i < WARP_CLOCKS; i++) {
      Clock& c = clocks[i];
      const double omega = (2 * PI * hz * lerp(1, WARP_WANDER_SPREAD[i], sp)) / sampleRate;
      c.k = omega * omega;
      c.damp = 2 * ZETA * omega;
      // The spring's stationary offset has variance sigma² / (3 · 2 · damp · k) (the noise is uniform in [-1, 1),
      // variance 1/3); sigma is set so its 3σ wander at full Depth is the reach.
      c.sigma = (d * lerp(1, WARP_DEPTH_SPREAD[i], sp) * reach / 3) * std::sqrt(6 * c.damp * c.k);
      // v > threshold starts a stall: up to SLIP_RATE_MAX a second at full Slip.
      c.slipThreshold = 1 - (2 * s * lerp(1, WARP_SLIP_SPREAD[i], sp) * SLIP_RATE_MAX) / sampleRate;
      c.slipDepth = s * SLIP_DEPTH;
      c.rest = (WARP_CENTRE_S + sp * WARP_PLACE[i] * WARP_REST_SPREAD_S) * sampleRate;
      c.pan = panGains(sp * WARP_PLACE[i]);
    }
  }

  /** In place, stereo. `mix` is 0 (dry) to 1 (wet). */
  void process(float* left, float* right, int offset, int n, double mix) DSP_NONBLOCKING {
    const double wet = mix * wetGain;
    const double dry = 1 - mix;
    float* buf = line.data();
    const int length = static_cast<int>(line.size());
    const double lim = reach;
    const double release = slipRelease;
    const double cpl = couple;
    int wi = w;
    double mean = meanRate;
    for (int s = 0; s < n; s++) {
      const double xL = left[offset + s];
      const double xR = right[offset + s];
      buf[wi] = static_cast<float>((xL + xR) * 0.5);
      double sum = 0, yL = 0, yR = 0;
      for (int i = 0; i < WARP_CLOCKS; i++) {
        Clock& c = clocks[i];
        const double u = c.rng.next();
        const double v = c.rng.next();
        if (v > c.slipThreshold) c.slip = c.slipDepth;  // the clock sticks…
        c.slip *= release;                              // …and lets go; the spring catches it up
        c.rate += c.sigma * u - c.k * c.offset - c.damp * c.rate + cpl * (mean - c.rate);
        c.offset += c.rate + c.slip;
        if (c.offset > lim) {
          c.offset = lim;
          c.rate = 0;
          c.slip = 0;
        } else if (c.offset < -lim) {
          c.offset = -lim;
          c.rate = 0;
          c.slip = 0;
        }
        sum += c.rate;
        const double y = tap(buf, length, wi, c.rest + c.offset);
        yL += y * c.pan.l;
        yR += y * c.pan.r;
      }
      mean = sum / WARP_CLOCKS;
      wi = wi + 1 >= length ? 0 : wi + 1;
      left[offset + s] = static_cast<float>(xL * dry + yL * wet);
      right[offset + s] = static_cast<float>(xR * dry + yR * wet);
    }
    w = wi;
    meanRate = mean;
  }

  /** Clock i's tap, in seconds behind its resting delay (negative: ahead). For the tests. */
  double clockOffset(int i) const noexcept { return clocks[i].offset / sampleRate; }

 private:
  /** The spring's damping ratio: pulled home without ringing. */
  static constexpr double ZETA = 0.8;
  /** Stalls a second at full Slip, how far behind a stall pushes per sample at its start, and how long it holds. */
  static constexpr double SLIP_RATE_MAX = 6;
  static constexpr double SLIP_DEPTH = 0.04;
  static constexpr double SLIP_RELEASE_S = 0.03;
  /** How hard a clock is pulled toward the others' rate per sample at full Coupling. */
  static constexpr double COUPLE_MAX = 0.002;
  static constexpr double SEED = 0x57a2b;

  struct Clock {
    double offset = 0;  // samples behind its rest
    double rate = 0;    // samples per sample the offset grows: −rate is the pitch ratio less one
    double slip = 0;    // the stall's push, decaying
    Rng rng{1};
    double k = 0, damp = 0, sigma = 0, slipThreshold = 1, slipDepth = 0, rest = 0;
    PanGains pan{1, 1};
  };

  double sampleRate = 48000;
  std::vector<float> line;
  int w = 0;
  double reach = 0;
  double slipRelease = 0;
  double wetGain = 0;
  double couple = 0;
  double meanRate = 0;
  Clock clocks[WARP_CLOCKS];

  static double lerp(double a, double b, double t) noexcept { return a + (b - a) * t; }

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
