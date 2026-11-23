/**
 * A two-tap stereo chorus. The input, summed to mono, goes into one delay line, read by two taps around
 * CHORUS_CENTRE_S. One LFO sways both taps the opposite way (tap 2 is half a cycle behind tap 1): Depth sets the swing,
 * CHORUS_SWING_MIN_S … CHORUS_SWING_MAX_S peak to peak; Speed its rate; Shape its wave, sine or triangle. A second,
 * slow sine (CHORUS_WANDER_RATE_HZ) swings Speed by up to ±Wander Hz, the rate held to CHORUS_RATE_FLOOR_HZ …
 * CHORUS_RATE_CEIL_HZ. Width pans the taps apart, tap 1 left and tap 2 right with constant power (util/pan.hpp): at 0 both
 * sit in the middle. No feedback. Mix is the wet/dry, passed to process().
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "../util/math.hpp"
#include "../util/pan.hpp"

namespace dsp {

/** The delay the taps sway around, and the swing peak to peak at Depth 0 and 1, in seconds. */
constexpr double CHORUS_CENTRE_S = 0.025;
constexpr double CHORUS_SWING_MIN_S = 0.003;
constexpr double CHORUS_SWING_MAX_S = 0.006;
/** Speed's range, Hz, on a log scale. */
constexpr double CHORUS_SPEED_MIN_HZ = 0.7;
constexpr double CHORUS_SPEED_MAX_HZ = 7;
/** The most Wander swings Speed, either way, and how fast it swings it, Hz. */
constexpr double CHORUS_WANDER_MAX_HZ = 0.5;
constexpr double CHORUS_WANDER_RATE_HZ = 0.1;
/** The rate the LFO may run at once Wander has swung Speed, Hz. */
constexpr double CHORUS_RATE_FLOOR_HZ = 0.2;
constexpr double CHORUS_RATE_CEIL_HZ = 7.5;
/** How fast a change of Depth or Width glides in, seconds (a one-pole, so a jump does not click). */
constexpr double CHORUS_GLIDE_S = 0.01;

/** The LFO's waves: Shape's two values. */
enum ChorusShape : int { ChorusSine = 0, ChorusTriangle = 1 };

class Chorus {
 public:
  void prepare(double sampleRate) {
    this->sampleRate = sampleRate;
    const std::size_t length = static_cast<std::size_t>(std::ceil((CHORUS_CENTRE_S + CHORUS_SWING_MAX_S / 2) * sampleRate)) + 4;
    line.assign(length, 0.0f);
    centre = CHORUS_CENTRE_S * sampleRate;
    glide = 1 - std::exp(-1 / (CHORUS_GLIDE_S * sampleRate));
    wanderInc = CHORUS_WANDER_RATE_HZ / sampleRate;
    setParams(CHORUS_SPEED_MIN_HZ, 0.5, 1, ChorusSine, 0);
    reset();
  }

  void reset() noexcept {
    std::fill(line.begin(), line.end(), 0.0f);
    w = 0;
    phase = 0;
    wanderPhase = 0;
    reach = reachTarget;
    width = widthTarget;
  }

  /**
   * speed in Hz (CHORUS_SPEED_MIN_HZ … MAX_HZ), depth 0..1, width 0..1, shape ChorusSine or ChorusTriangle, wander in Hz
   * (0 … CHORUS_WANDER_MAX_HZ). Each is clamped to its range.
   */
  void setParams(double speed, double depth, double widthValue, int shapeValue, double wander) noexcept {
    speedHz = clamp(speed, CHORUS_SPEED_MIN_HZ, CHORUS_SPEED_MAX_HZ);
    const double swing = CHORUS_SWING_MIN_S + (CHORUS_SWING_MAX_S - CHORUS_SWING_MIN_S) * clamp(depth, 0, 1);
    reachTarget = swing / 2 * sampleRate;
    widthTarget = clamp(widthValue, 0, 1);
    shape = shapeValue == ChorusTriangle ? ChorusTriangle : ChorusSine;
    wanderHz = clamp(wander, 0, CHORUS_WANDER_MAX_HZ);
  }

  void process(float* left, float* right, int offset, int n, double mix) DSP_NONBLOCKING {
    const double dry = 1 - mix;
    const double wet = mix * WET_GAIN;
    float* buf = line.data();
    const int length = static_cast<int>(line.size());
    int wi = w;
    double ph = phase, wph = wanderPhase, r = reach, wd = width;
    for (int s = 0; s < n; s++) {
      const double xL = left[offset + s];
      const double xR = right[offset + s];
      buf[wi] = static_cast<float>((xL + xR) * 0.5);
      r += (reachTarget - r) * glide;
      wd += (widthTarget - wd) * glide;
      wph += wanderInc;
      if (wph >= 1) wph -= 1;
      const double rate = clamp(speedHz + wanderHz * std::sin(2 * PI * wph), CHORUS_RATE_FLOOR_HZ, CHORUS_RATE_CEIL_HZ);
      ph += rate / sampleRate;
      if (ph >= 1) ph -= 1;
      const double l = wave(ph, shape);
      const double y1 = tap(buf, length, wi, centre + r * l);
      const double y2 = tap(buf, length, wi, centre - r * l);
      const PanGains g1 = panGains(-wd), g2 = panGains(wd);
      wi = wi + 1 >= length ? 0 : wi + 1;
      left[offset + s] = static_cast<float>(xL * dry + (y1 * g1.l + y2 * g2.l) * wet);
      right[offset + s] = static_cast<float>(xR * dry + (y1 * g1.r + y2 * g2.r) * wet);
    }
    w = wi;
    phase = ph;
    wanderPhase = wph;
    reach = r;
    width = wd;
  }

  /** The LFO's rate now, Hz (Speed swung by Wander): for tests. */
  double rateHz() const noexcept { return clamp(speedHz + wanderHz * std::sin(2 * PI * wanderPhase), CHORUS_RATE_FLOOR_HZ, CHORUS_RATE_CEIL_HZ); }

  /** The wave at a phase (0..1 of the cycle), −1..1: a sine, or a triangle rising through 0 at phase 0 like the sine. */
  static double wave(double phase, int shape) noexcept {
    if (shape != ChorusTriangle) return std::sin(2 * PI * phase);
    const double p = phase - std::floor(phase);
    return p < 0.25 ? 4 * p : p < 0.75 ? 2 - 4 * p : 4 * p - 4;
  }

 private:
  /** Two taps, each panned with constant power: 1/√2 keeps the wet as loud as one tap at the middle. */
  static constexpr double WET_GAIN = 0.7071067811865476;

  double sampleRate = 48000;
  std::vector<float> line;
  int w = 0;
  double phase = 0, wanderPhase = 0, wanderInc = 0;
  double centre = 0, glide = 1;
  double speedHz = CHORUS_SPEED_MIN_HZ, wanderHz = 0;
  double reach = 0, reachTarget = 0, width = 1, widthTarget = 1;
  int shape = ChorusSine;

  /** The line `len` samples behind the write index `w`, linearly interpolated. */
  static double tap(const float* line, int length, int w, double len) noexcept {
    double pos = w - len;
    if (pos < 0) pos += length;
    const int i0 = static_cast<int>(pos);
    const double frac = pos - i0;
    const int i1 = i0 + 1 >= length ? 0 : i0 + 1;
    return line[i0] + (static_cast<double>(line[i1]) - line[i0]) * frac;
  }
};

}  // namespace dsp
