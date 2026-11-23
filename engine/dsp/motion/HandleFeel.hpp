/**
 * What a handle's point sets: how a path reads the point (HandleReading), and how a slot of the path lands on its
 * block parameter (SlotFeel). The same arithmetic as the page (library/ui/interaction.js derivedOutputValue and
 * targets.js slotValue); the engine generator bakes each handle's readings and feels from the project
 * (src/shared/kfo-tables.js), and src/shared/kfo-parity.test.js holds this copy to the page's.
 *
 * A reading's kind:
 * - Affine: clamp(c0 + c1·x + c2·y, 0, 1) — a sub-pad's own axis, a pad's position or blend;
 * - Step: 1 when c0 + c1·x + c2·y ≥ .5, else 0 — a pad's state;
 * - Measure: the point mapped through the pad's corners (c0…c7, c8 of them: 3 or 4), then read by projection c9:
 *   0 along the segment c10,c11 → c12,c13; on a triangle 1 corner, 2 centroid, 3 parallel, 4 ratio, 5 centred-ratio,
 *   for corner c14;
 * - Zone: the pad zone the point is in (zones.js zoneAt), from the zone table c0 (ZoneSpec, read by zoneReadingAt):
 *   (i + .5) / n for zone i of n, 0 in none.
 */
#pragma once

#include <algorithm>
#include <cmath>

#include "../util/math.hpp"

namespace dsp {

enum class ReadingKind : int { Affine = 0, Step = 1, Measure = 2, Zone = 3 };

struct HandleReading {
  int kind;
  double c[15];
};

namespace feel_detail {

struct Point {
  double x, y;
};

inline double unit(double v) noexcept { return clamp(v, 0, 1); }

/** The barycentric weights of p over the triangle a, b, c; false for a degenerate triangle. */
inline bool barycentric(Point p, Point a, Point b, Point c, double w[3]) noexcept {
  const double den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
  if (std::fabs(den) < 1e-12) return false;
  w[0] = ((b.y - c.y) * (p.x - c.x) + (c.x - b.x) * (p.y - c.y)) / den;
  w[1] = ((c.y - a.y) * (p.x - c.x) + (a.x - c.x) * (p.y - c.y)) / den;
  w[2] = 1 - w[0] - w[1];
  return true;
}

}  // namespace feel_detail

/** A path's 0…1 value when its handle is at (x, y) in the handle's own square. */
inline double handleReadingAt(const HandleReading& r, double x, double y) noexcept {
  using namespace feel_detail;
  const double* c = r.c;
  const double linear = c[0] + c[1] * x + c[2] * y;
  if (r.kind == static_cast<int>(ReadingKind::Affine)) return unit(linear);
  if (r.kind == static_cast<int>(ReadingKind::Step)) return linear >= 0.5 ? 1 : 0;
  const bool triangle = c[8] == 3;
  const Point p = triangle
      ? Point{c[0] * (1 - y) + c[4] * (1 - x) * y + c[2] * x * y, c[1] * (1 - y) + c[5] * (1 - x) * y + c[3] * x * y}
      : Point{c[0] * (1 - x) * (1 - y) + c[2] * x * (1 - y) + c[4] * x * y + c[6] * (1 - x) * y,
              c[1] * (1 - x) * (1 - y) + c[3] * x * (1 - y) + c[5] * x * y + c[7] * (1 - x) * y};
  const int projection = static_cast<int>(c[9]);
  if (projection == 0) {
    const double dx = c[12] - c[10], dy = c[13] - c[11], length = dx * dx + dy * dy;
    return unit(((p.x - c[10]) * dx + (p.y - c[11]) * dy) / (length != 0 ? length : 1));
  }
  const int i = ((static_cast<int>(roundHalfUp(c[14])) % 3) + 3) % 3;
  const Point corners[3] = {{c[0], c[1]}, {c[2], c[3]}, {c[4], c[5]}};
  if (projection == 1) {
    const Point a = corners[i], b = corners[(i + 1) % 3], o = corners[(i + 2) % 3];
    const double ex = b.x - a.x, ey = b.y - a.y, dx = p.x - o.x, dy = p.y - o.y, den = ex * dy - ey * dx;
    if (std::fabs(den) < 1e-12) return 0.5;
    return unit(((o.x - a.x) * dy - (o.y - a.y) * dx) / den);
  }
  double w[3];
  if (!barycentric(p, corners[0], corners[1], corners[2], w)) return 0;
  if (projection == 2) return unit(w[i] - std::min({w[0], w[1], w[2]}));
  if (projection == 3) return unit(w[i]);
  const double positive[3] = {std::max(0.0, w[0]), std::max(0.0, w[1]), std::max(0.0, w[2])};
  if (projection == 4) {
    const double largest = std::max({positive[0], positive[1], positive[2]});
    return largest > 0 ? unit(positive[i] / largest) : 0;
  }
  const double smallest = std::min({positive[0], positive[1], positive[2]});
  const double d[3] = {positive[0] - smallest, positive[1] - smallest, positive[2] - smallest};
  const double largest = std::max({d[0], d[1], d[2]}), total = d[0] + d[1] + d[2];
  return largest > 1e-12 ? unit(d[i] * total / largest) : 0;
}

/** A path a pad's zones are cut from: read like any path, then split into regions at its ascending bounds. */
struct ZoneTerm {
  HandleReading reading;
  const double* bounds;
  int boundCount;
};

/**
 * A zone path's zones (zones.js): its terms, and per zone per term the run of the term's regions it takes, lo then hi
 * (`runs`, 2 · termCount per zone; lo −1: any region). A point is in the first zone whose runs all hold.
 */
struct ZoneSpec {
  const ZoneTerm* terms;
  int termCount;
  const int* runs;
  int zoneCount;
};

/** A zone path's 0…1 value when its handle is at (x, y): (i + .5) / n in zone i of n, 0 in none (zones.js zoneReading). */
inline double zoneReadingAt(const ZoneSpec& z, double x, double y) noexcept {
  constexpr int MAX_TERMS = 16;
  int region[MAX_TERMS];
  const int terms = std::min(z.termCount, MAX_TERMS);
  for (int k = 0; k < terms; k++) {
    const ZoneTerm& term = z.terms[k];
    const double v = clamp(handleReadingAt(term.reading, x, y), 0, 1);
    int r = 0;
    for (int i = 0; i < term.boundCount; i++) if (v >= term.bounds[i]) r++;
    region[k] = r;
  }
  for (int zone = 0; zone < z.zoneCount; zone++) {
    const int* run = z.runs + 2 * z.termCount * zone;
    bool in = true;
    for (int k = 0; k < terms && in; k++) if (run[2 * k] >= 0 && (region[k] < run[2 * k] || region[k] > run[2 * k + 1])) in = false;
    if (in) return (zone + 0.5) / z.zoneCount;
  }
  return 0;
}

enum class FeelCurve : int { Linear = 0, Log = 1, Exponential = 2, Cube = 3, SoftLog = 4 };
/** The soft log curve's bend (geometry.js SOFT_LOG_A): lo + (hi − lo)·ln(1 + a·p)/ln(1 + a), half the travel at 65 % of the range. */
constexpr double SOFT_LOG_A = 2.5917954291711642;
enum class FeelShape : int { Linear = 0, Center = 1, Ends = 2 };

/** How a slot lands its path's 0…1 on a parameter: shaped and inverted, across min…max on a curve, stepped; or, with values, the value of the region the path is in. */
struct SlotFeel {
  double min, max;
  int curve, shape, invert;
  double step;
  const double* bounds;  // ascending region bounds, `boundCount` of them
  int boundCount;
  const double* values;  // one per region; none: min…max
  int valueCount;
};

/** Where the path sits along the slot's travel: shaped (center peak or dip), then inverted. */
inline double slotPosition(const SlotFeel& f, double t) noexcept {
  double position = clamp(t, 0, 1);
  if (f.shape == static_cast<int>(FeelShape::Center)) position = 1 - std::fabs(2 * position - 1);
  else if (f.shape == static_cast<int>(FeelShape::Ends)) position = std::fabs(2 * position - 1);
  return f.invert ? 1 - position : position;
}

/** The parameter's value for the path at t (targets.js slotValue). */
inline double slotValue(const SlotFeel& f, double t) noexcept {
  if (f.valueCount > 0) {
    const double v = clamp(t, 0, 1);
    int region = 0;
    for (int i = 0; i < f.boundCount; i++) if (v >= f.bounds[i]) region++;
    return f.values[std::min(f.valueCount - 1, region)];
  }
  const double lo = f.min, hi = f.max, position = clamp(slotPosition(f, t), 0, 1);
  double value;
  if (f.curve == static_cast<int>(FeelCurve::Log) && lo > 0 && hi > 0) value = lo * std::pow(hi / lo, position);
  else if (f.curve == static_cast<int>(FeelCurve::Exponential)) value = lo + (hi - lo) * position * position;
  else if (f.curve == static_cast<int>(FeelCurve::Cube)) value = lo + (hi - lo) * position * position * position;
  else if (f.curve == static_cast<int>(FeelCurve::SoftLog)) value = lo + (hi - lo) * std::log1p(SOFT_LOG_A * position) / std::log1p(SOFT_LOG_A);
  else value = lo + (hi - lo) * position;
  const double step = std::fabs(f.step);
  if (step > 0) value = lo + roundHalfUp((value - lo) / step) * step;
  return clamp(value, std::min(lo, hi), std::max(lo, hi));
}

}  // namespace dsp
