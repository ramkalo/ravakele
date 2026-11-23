/**
 * Envelope curve tables.
 *
 * A table's bend runs -1 (fast start, slow finish) through 0 (linear) to +1
 * (slow start, fast finish); the envelope bakes the attack's toward +1 and the
 * decay's and the release's toward -1, so every stage curve is exponential. Evaluating that shape per sample would mean an exp()
 * per sample per envelope; instead each distinct curve setting is baked into a
 * small table once, when the control moves.
 *
 * Each envelope owns its three tables (CurveTables) and rebakes one only when
 * that stage's curve setting changes.
 */
#pragma once

#include <cmath>
#include <limits>

namespace dsp {

/** Samples per table; 128 intervals plus the endpoint. */
constexpr int CURVE_RES = 129;

/** Attack, Decay and Release each carry an independent curve. */
constexpr int CURVE_STAGES = 3;
constexpr int CURVE_ATTACK = 0;
constexpr int CURVE_DECAY = 1;
constexpr int CURVE_RELEASE = 2;

/** An envelope's three tables, laid out flat as [stage][sample], each remembering the curve it was baked for. */
class CurveTables {
 public:
  float tables[CURVE_STAGES * CURVE_RES] = {};

  CurveTables() noexcept {
    for (int g = 0; g < CURVE_STAGES; g++) baked[g] = std::numeric_limits<float>::quiet_NaN();
    for (int g = 0; g < CURVE_STAGES; g++) update(g, 0);
  }

  int offset(int stage) const noexcept { return stage * CURVE_RES; }

  /** Rebake one table if its curve (−1..1) changed. No allocation. */
  void update(int stage, double curve) noexcept {
    if (baked[stage] == curve) return;
    baked[stage] = static_cast<float>(curve);
    const int at = offset(stage);
    float* t = tables;
    const double k = curve * BEND;
    if (k > -1e-4 && k < 1e-4) {
      for (int i = 0; i < CURVE_RES; i++) t[at + i] = static_cast<float>(static_cast<double>(i) / (CURVE_RES - 1));
      return;
    }
    const double denom = std::exp(k) - 1;
    for (int i = 0; i < CURVE_RES; i++) {
      t[at + i] = static_cast<float>((std::exp(k * (static_cast<double>(i) / (CURVE_RES - 1))) - 1) / denom);
    }
  }

 private:
  /** How far the control bends the shape. Higher is more extreme. */
  static constexpr double BEND = 6;
  float baked[CURVE_STAGES] = {};
};

/** Linear-interpolated table read. `p` is progress through the stage, 0..1. */
inline double curveAt(const float* tables, int offset, double p) noexcept {
  const double x = p <= 0 ? 0 : p >= 1 ? CURVE_RES - 1 : p * (CURVE_RES - 1);
  const int i = static_cast<int>(x);
  const double f = x - i;
  const double a = tables[offset + i];
  return i >= CURVE_RES - 1 ? a : a + (tables[offset + i + 1] - a) * f;
}

}  // namespace dsp
