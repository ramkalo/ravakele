/**
 * The modulation routing: the engine side of mod mode, computing exactly its math (library/ui/mod-mode.js).
 *
 * A *target* is one block parameter being modulated, with its feel range (min..max) and its **sum** (on, polarity
 * ±1, level 0..1). It has up to MAX_ROUTES_PER_TARGET **routes**, each one source → this target with on/off, a
 * polarity (positive, negative) and a level 0..1. A source is an LFO (its value −1..1), an envelope (0..1), a voice
 * value (0..1, mod/VoiceValues.hpp: the host processes a target routed from one per voice), a PABFO (−1..1, as an
 * LFO; mod/Pabfo.hpp), a key (−1..1 through its Polarity Window, as an LFO; mod/Keytrack.hpp), a velocity (the same; mod/Velocity.hpp) or an aftertouch (the same; mod/Aftertouch.hpp). A PABFO runs at 50..100 Hz, too fast for block-rate routing: while any live route takes one
 * (fastRouted()), the host runs process() every PABFO_SLICE samples instead of once per block.
 *
 *   mapped       = positive s, negative −s (an LFO's, a PABFO's, a key's, a velocity's and an aftertouch's s is −1..1, an envelope's and a voice value's 0..1)
 *   contribution = on × mapped × level
 *   sum          = on × (±1) × level × Σ contributions, clamped to −1..1
 *   value        = clamp(base + sum × (max − min), min, max); on a log feel (a frequency), in octaves:
 *                  clamp(base × (max / min)^sum, min, max), so a sum of 1 still sweeps the whole range
 *
 * Routes and sums are preset state: the host sets them from the patch (setTarget, setRoute). process() is block-rate
 * and const: it reads every source's current value and every target's base value (what its control sets) and writes
 * each target's modulated value, keeping nothing, so one ModRouting serves every voice (each passes its own sources
 * and outputs). No allocation after construction.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "../util/math.hpp"

namespace dsp {

/** Every parameter a control drives is a target (the engine compiles a route slot for each, src/engine/generate.js). */
constexpr int MAX_TARGETS = 256;
/** Mod mode's 8 route slots per parameter (mod-mode.js MAX_ROUTES_PER_PARAM) plus fixed routes from modulation arrows. */
constexpr int MAX_ROUTES_PER_TARGET = 16;

namespace SourceKind {
enum : int { Lfo = 0, Env = 1, Voice = 2, Pabfo = 3, Key = 4, Velocity = 5, Aftertouch = 6, Duck = 7, Wheel = 8, Rnd = 9 };
}
namespace RoutePolarity {
enum : int { Positive = 0, Negative = 1 };
}

/** A source's value mapped by a route's polarity (mod-mode.js polarityValue). */
inline double polarityValue(int kind, int polarity, double value) noexcept {
  const bool bipolar = kind == SourceKind::Lfo || kind == SourceKind::Pabfo || kind == SourceKind::Key || kind == SourceKind::Velocity || kind == SourceKind::Aftertouch || kind == SourceKind::Wheel || kind == SourceKind::Rnd;
  const double s = bipolar ? clamp(value, -1, 1) : clamp(value, 0, 1);
  return polarity == RoutePolarity::Negative ? -s : s;
}

class ModRouting {
 public:
  ModRouting() noexcept {
    std::fill(std::begin(hi), std::end(hi), 1.0f);
    std::fill(std::begin(sumOn), std::end(sumOn), std::uint8_t{1});
    std::fill(std::begin(sumSign), std::end(sumSign), 1.0f);
    std::fill(std::begin(sumLevel), std::end(sumLevel), 1.0f);
  }

  void prepare(double /*sampleRate*/) { reset(); }

  /** No targets and no routes. */
  void reset() noexcept {
    targets = 0;
    std::fill(std::begin(routes), std::end(routes), std::uint8_t{0});
  }

  /** How many targets process() reads and writes, 0..MAX_TARGETS. */
  void setTargetCount(double n) noexcept { targets = static_cast<int>(clamp(roundHalfUp(n), 0, MAX_TARGETS)); }

  /** Target `t`'s feel range and its sum: on 0/1, polarity +1 or −1, level 0..1; log 1 for a log feel (its curve). */
  void setTarget(int t, double min, double max, double on = 1, double polarity = 1, double level = 1, double log = 0) noexcept {
    lo[t] = static_cast<float>(std::min(min, max));
    hi[t] = static_cast<float>(std::max(min, max));
    logFeel[t] = log >= 0.5 && lo[t] > 0 ? 1 : 0;
    sumOn[t] = on >= 0.5 ? 1 : 0;
    sumSign[t] = polarity < 0 ? -1.0f : 1.0f;
    sumLevel[t] = static_cast<float>(clamp(level, 0, 1));
  }

  /** Target `t` has `n` routes, 0..MAX_ROUTES_PER_TARGET. */
  void setRouteCount(int t, double n) noexcept { routes[t] = static_cast<std::uint8_t>(clamp(roundHalfUp(n), 0, MAX_ROUTES_PER_TARGET)); }

  /** Route `r` of target `t`: from source index `source`, its kind (SourceKind), on 0/1, polarity (RoutePolarity), level 0..1. */
  void setRoute(int t, int r, int source, int kind, double on, double polarity, double level) noexcept {
    const int i = t * MAX_ROUTES_PER_TARGET + r;
    routeSource[i] = source;
    routeKind[i] = static_cast<std::uint8_t>(kind == SourceKind::Env || kind == SourceKind::Voice || kind == SourceKind::Pabfo || kind == SourceKind::Key || kind == SourceKind::Velocity || kind == SourceKind::Aftertouch || kind == SourceKind::Duck || kind == SourceKind::Wheel || kind == SourceKind::Rnd ? kind : SourceKind::Lfo);
    routeOn[i] = on >= 0.5 ? 1 : 0;
    routePolarity[i] = static_cast<std::uint8_t>(clamp(roundHalfUp(polarity), 0, 2));
    routeLevel[i] = static_cast<float>(clamp(level, 0, 1));
  }

  /** Whether any live route (on, with a level, of a live sum) takes a PABFO: the host then routes every PABFO_SLICE samples. */
  bool fastRouted() const noexcept {
    for (int t = 0; t < targets; t++) {
      if (sumOn[t] == 0 || sumLevel[t] == 0) continue;
      const int first = t * MAX_ROUTES_PER_TARGET;
      for (int r = 0; r < routes[t]; r++) {
        const int i = first + r;
        if (routeOn[i] != 0 && routeLevel[i] > 0 && routeKind[i] == SourceKind::Pabfo) return true;
      }
    }
    return false;
  }

  /**
   * One block (or slice): `sources[i]` (i < sourceCount) is source i's value now, `base[t]` target t's base value; writes each
   * target's modulated value into `out[t]`, and its sum, −1..1, into `sums[t]` when `sums` is given.
   */
  void process(const float* sources, int sourceCount, const float* base, float* out, float* sums = nullptr) const DSP_NONBLOCKING {
    for (int t = 0; t < targets; t++) {
      double total = 0;
      const int first = t * MAX_ROUTES_PER_TARGET;
      for (int r = 0; r < routes[t]; r++) {
        const int i = first + r;
        if (routeOn[i] == 0) continue;
        const int s = routeSource[i];
        const double value = s >= 0 && s < sourceCount ? sources[s] : 0;
        total += polarityValue(routeKind[i], routePolarity[i], value) * routeLevel[i];
      }
      const double sum = sumOn[t] == 0 ? 0 : clamp(sumSign[t] * sumLevel[t] * total, -1, 1);
      if (sums) sums[t] = static_cast<float>(sum);
      const double l = lo[t];
      const double h = hi[t];
      out[t] = static_cast<float>(logFeel[t] != 0 ? clamp(base[t] * std::pow(h / l, sum), l, h) : clamp(base[t] + sum * (h - l), l, h));
    }
  }

 private:
  int targets = 0;
  float lo[MAX_TARGETS] = {};
  float hi[MAX_TARGETS] = {};
  std::uint8_t logFeel[MAX_TARGETS] = {};
  std::uint8_t sumOn[MAX_TARGETS] = {};
  float sumSign[MAX_TARGETS] = {};
  float sumLevel[MAX_TARGETS] = {};

  std::uint8_t routes[MAX_TARGETS] = {};
  std::uint8_t routeOn[MAX_TARGETS * MAX_ROUTES_PER_TARGET] = {};
  std::int32_t routeSource[MAX_TARGETS * MAX_ROUTES_PER_TARGET] = {};
  std::uint8_t routeKind[MAX_TARGETS * MAX_ROUTES_PER_TARGET] = {};
  std::uint8_t routePolarity[MAX_TARGETS * MAX_ROUTES_PER_TARGET] = {};
  float routeLevel[MAX_TARGETS * MAX_ROUTES_PER_TARGET] = {};
};

}  // namespace dsp
