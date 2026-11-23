/**
 * A motion player: what one KFO or SOLOKFO plays. A motion is points on a 50-per-second grid in the handle's own 0…1
 * square, looping from its last point back to its first; a player keeps three slots of them (a slot left empty plays
 * nothing, except slot 3, which plays the built-in motion: a circle for a quad, the incircle for a triangle) and plays
 * the selected one.
 *
 * - tick(dt): a SOLOKFO or a KFO with no block plays the motion at its own length.
 * - kfoTick(dt, settings): a KFO linked to a motion.kfo block plays one loop per `period` whatever the motion's
 *   length, read from `phase` on, forward, reverse, or ping-pong (forward then back, one period each way, turning at the
 *   last point).
 * - rotateMotionPoint turns a point by whole corners of its family; kfoDepthPoint scales it around a rest point.
 *
 * The page records the takes (from the finger) and sends each one here (setTake). Every buffer is allocated in
 * prepare(); nothing on the audio path allocates.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "../util/math.hpp"

namespace dsp {

constexpr int MOTION_SAMPLE_RATE = 50;
constexpr int MOTION_MAX_SECONDS = 20;
constexpr int MOTION_MAX_SAMPLES = MOTION_MAX_SECONDS * MOTION_SAMPLE_RATE;
constexpr int MOTION_SLOTS = 3;
/** The slot that plays the built-in motion until a take replaces it. */
constexpr int BUILTIN_SLOT = 3;
constexpr double MOTION_PERIOD_MIN = 0.5;
constexpr double MOTION_PERIOD_MAX = 20;
constexpr double MOTION_PERIOD_DEFAULT = 4;

enum class MotionFamily : int { Quad = 0, Triangle = 1 };
enum class KfoDirection : int { Forward = 0, Reverse = 1, PingPong = 2 };

struct MotionPoint {
  double x = 0.5;
  double y = 0.5;
};

/** A motion.kfo block's settings, from its params (kfoSettings clamps them). */
struct KfoSettings {
  double period = MOTION_PERIOD_DEFAULT;
  KfoDirection direction = KfoDirection::Forward;
  double depth = 1;
  double phase = 0;
  bool play = false;
};

/** The block's param values (period, direction, depth, phase, play) clamped to their ranges. */
inline KfoSettings kfoSettings(double period, double direction, double depth, double phase, double play) noexcept {
  KfoSettings s;
  s.period = clamp(std::isfinite(period) ? period : MOTION_PERIOD_DEFAULT, MOTION_PERIOD_MIN, MOTION_PERIOD_MAX);
  s.direction = static_cast<KfoDirection>(static_cast<int>(clamp(roundHalfUp(std::isfinite(direction) ? direction : 0), 0, 2)));
  s.depth = clamp(std::isfinite(depth) ? depth : 1, 0, 1);
  s.phase = clamp(std::isfinite(phase) ? phase : 0, 0, 1);
  s.play = std::isfinite(play) && play >= 0.5;
  return s;
}

constexpr double TRIANGLE_HEIGHT = 0.86602540378443864676;  // √3 / 2

/** A point of the reference triangle → the triangle's gesture (u, v): p = apex·(1 − v) + (left·(1 − u) + right·u)·v. */
inline MotionPoint triangleGesturePoint(double x, double y) noexcept {
  const double v = clamp(y / TRIANGLE_HEIGHT, 0, 1);
  return {v > 1e-9 ? clamp((x - 0.5 * (1 - v)) / v, 0, 1) : 0.5, v};
}

/** How many points the built-in motion has at `period` seconds. */
inline int builtinMotionCount(double period) noexcept {
  const double p = clamp(std::isfinite(period) ? period : MOTION_PERIOD_DEFAULT, MOTION_PERIOD_MIN, MOTION_PERIOD_MAX);
  return std::max(2, static_cast<int>(roundHalfUp(p * MOTION_SAMPLE_RATE)));
}

/** Point `index` of `count` of the built-in motion: a circle about the middle (quad), or the triangle's incircle × .8. */
inline MotionPoint builtinMotionPoint(MotionFamily family, int index, int count) noexcept {
  const double phase = static_cast<double>(index) / count, angle = phase * 2 * PI;
  if (family == MotionFamily::Triangle) {
    const double radius = 0.8 * TRIANGLE_HEIGHT / 3, cy = 2 * TRIANGLE_HEIGHT / 3;
    return triangleGesturePoint(0.5 + radius * std::sin(angle), cy - radius * std::cos(angle));
  }
  return {0.5 + 0.35 * std::sin(angle), 0.5 - 0.35 * std::cos(angle)};
}

/** Corner steps that bring a family's motion back to itself. */
inline int rotationSteps(MotionFamily family) noexcept { return family == MotionFamily::Triangle ? 3 : 4; }

/**
 * A point as a target turned by `rotation` corner steps sees it. Quad: one step is (x, y) → (1 − y, x). Triangle: the
 * corner weights (apex 1 − y, left y·(1 − x), right y·x) move one corner on, apex → right → left.
 */
inline MotionPoint rotateMotionPoint(MotionFamily family, MotionPoint point, double rotation) noexcept {
  const int n = rotationSteps(family), steps = ((static_cast<int>(roundHalfUp(std::isfinite(rotation) ? rotation : 0)) % n) + n) % n;
  double x = clamp(point.x, 0, 1), y = clamp(point.y, 0, 1);
  for (int step = 0; step < steps; step++) {
    if (family == MotionFamily::Triangle) {
      const double apex = 1 - y, left = y * (1 - x);
      y = clamp(1 - left, 0, 1);
      x = y > 1e-9 ? clamp(apex / y, 0, 1) : 0.5;
    } else {
      const double nx = 1 - y;
      y = x;
      x = nx;
    }
  }
  return {x, y};
}

/** A point scaled by `depth` around `rest` (where the handle sat when playback started); depth 1 leaves it. */
inline MotionPoint kfoDepthPoint(MotionPoint point, MotionPoint rest, double depth) noexcept {
  if (depth >= 1) return point;
  return {rest.x + (point.x - rest.x) * depth, rest.y + (point.y - rest.y) * depth};
}

/** The point of a motion `t` seconds in, looping; linear between points, and from the last back to the first. */
inline MotionPoint motionSampleAt(const float* xy, int count, double t) noexcept {
  if (count <= 0) return {};
  if (count == 1) return {xy[0], xy[1]};
  const double position = std::fmod(std::fmod(t * MOTION_SAMPLE_RATE, count) + count, count);
  const int index = static_cast<int>(std::floor(position)) % count, next = (index + 1) % count;
  const double fraction = position - std::floor(position);
  return {xy[2 * index] + (xy[2 * next] - xy[2 * index]) * fraction, xy[2 * index + 1] + (xy[2 * next + 1] - xy[2 * index + 1]) * fraction};
}

class MotionPlayer {
 public:
  /** Allocates the slots and lays out the built-in motion for `family` at `builtinPeriod` seconds. Stopped, slot 3. */
  void prepare(MotionFamily family, double builtinPeriod) {
    this->family = family;
    for (auto& slot : slots) slot.assign(2 * MOTION_MAX_SAMPLES, 0.0f);
    builtin.assign(2 * MOTION_MAX_SAMPLES, 0.0f);
    builtinCount = std::min(MOTION_MAX_SAMPLES, builtinMotionCount(builtinPeriod));
    for (int i = 0; i < builtinCount; i++) {
      const MotionPoint p = builtinMotionPoint(family, i, builtinCount);
      builtin[2 * i] = static_cast<float>(p.x);
      builtin[2 * i + 1] = static_cast<float>(p.y);
    }
    std::fill(std::begin(counts), std::end(counts), 0);
    selected = BUILTIN_SLOT;
    playing = false;
    t = 0;
    loop = 0;
  }

  /**
   * A take into slot 1…3: `count` points (x, y pairs in `xy`), at most MOTION_MAX_SAMPLES; fewer than two empty the
   * slot. The selected slot starts again from its beginning.
   */
  void setTake(int slot, const float* xy, int count) noexcept {
    if (slot < 1 || slot > MOTION_SLOTS || slots[slot - 1].empty()) return;
    count = std::clamp(count, 0, MOTION_MAX_SAMPLES);
    if (count < 2 || !xy) count = 0;
    for (int i = 0; i < 2 * count; i++) slots[slot - 1][i] = static_cast<float>(clamp(xy[i], 0, 1));
    counts[slot - 1] = count;
    if (slot == selected) t = 0;
    if (!filled(selected)) playing = false;
  }

  bool filled(int slot) const noexcept { return slot == BUILTIN_SLOT || (slot >= 1 && slot <= MOTION_SLOTS && counts[slot - 1] >= 2); }

  /** Selects slot 1…3: a new slot starts from its beginning; an empty one stops. */
  void select(int slot) noexcept {
    slot = std::clamp(slot, 1, MOTION_SLOTS);
    if (slot != selected) {
      selected = slot;
      t = 0;
    }
    if (!filled(slot)) playing = false;
  }

  int slot() const noexcept { return selected; }

  /** Plays or stops; an empty slot does not play. */
  void setPlaying(bool on) noexcept { playing = on && filled(selected); }
  bool isPlaying() const noexcept { return playing; }

  /** Advances by dt seconds at the motion's own length; false when not playing. */
  bool tick(double dt, MotionPoint& out) noexcept {
    int count = 0;
    const float* xy = motion(count);
    if (!playing || !xy) {
      playing = false;
      return false;
    }
    const double duration = static_cast<double>(count) / MOTION_SAMPLE_RATE;
    t = std::fmod(t + std::max(0.0, dt), duration);
    out = motionSampleAt(xy, count, t);
    return true;
  }

  /** Advances by dt seconds under a KFO block's settings: one loop per period, from phase on, in its direction. */
  bool kfoTick(double dt, const KfoSettings& s, MotionPoint& out) noexcept {
    int count = 0;
    const float* xy = motion(count);
    if (!playing || !xy) {
      playing = false;
      return false;
    }
    const bool pingPong = s.direction == KfoDirection::PingPong;
    const double span = pingPong ? 2 : 1;
    loop = std::fmod(std::fmod(loop + std::max(0.0, dt) / s.period, span) + span, span);
    const double at = std::fmod(loop + s.phase, span);
    const double u = s.direction == KfoDirection::Reverse ? 1 - at : at > 1 ? 2 - at : at;
    const double duration = static_cast<double>(count) / MOTION_SAMPLE_RATE;
    const double length = pingPong ? std::max(0.0, duration - 1.0 / MOTION_SAMPLE_RATE) : duration;
    out = motionSampleAt(xy, count, u * length);
    return true;
  }

  MotionFamily motionFamily() const noexcept { return family; }

 private:
  /** The selected slot's motion and its point count, or null when it is empty. */
  const float* motion(int& count) const noexcept {
    if (counts[selected - 1] >= 2) {
      count = counts[selected - 1];
      return slots[selected - 1].data();
    }
    if (selected == BUILTIN_SLOT && builtinCount >= 2) {
      count = builtinCount;
      return builtin.data();
    }
    count = 0;
    return nullptr;
  }

  MotionFamily family = MotionFamily::Quad;
  std::vector<float> slots[MOTION_SLOTS];
  int counts[MOTION_SLOTS] = {};
  std::vector<float> builtin;
  int builtinCount = 0;
  int selected = BUILTIN_SLOT;
  bool playing = false;
  double t = 0;
  double loop = 0;
};

}  // namespace dsp
