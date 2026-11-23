/**
 * The room reverb: a sound source and a listener at fixed spots in a rectangular room, heard as early reflections off
 * the walls, floor and ceiling (image sources up to the second order) and then a diffuse tail (an 8-line feedback
 * delay network fed by those reflections).
 *
 *   - Size is the room in meters: its smallest dimension (the ceiling height); the room is ROOM_SHAPE times it. The
 *     source and listener sit at fixed fractions of the room, close together near its middle, so the first reflection
 *     comes about 2.6 ms per meter after the direct sound (the dry signal is the direct sound) and the later ones
 *     spread out in proportion. Every reflection's delay, and every tail line's length (fixed ratios of the room's
 *     mean free path), scales with Size and glides when it moves. Size does not set how long the tail lasts.
 *   - Length is the tail's T60. Every tail line's loop gain is set from it so the tail loses 60 dB in that time
 *     whatever the line's length, so the tail time does not depend on Size.
 *   - Color decides which side of a split loses energy faster. The split is the log-midpoint between the Crossover
 *     and 20 kHz (√(crossover × 20 kHz)), so it moves with the Crossover. At 0 both sides decay over Length; at −1 the
 *     lows decay over Length and the highs over Length / COLOR_RATIO; at +1 the highs over Length and the lows over
 *     Length / COLOR_RATIO. A first-order shelf at the split carries it: in every tail line's loop, and on every
 *     reflection for the time it travelled, so the reflections fade along the same curve as the tail.
 *   - Crossover: L + R summed to mono goes through two cascaded Butterworth high-pass stages (24 dB/oct) at it before
 *     anything reverberates. The dry signal is untouched.
 *   - Mix: dry · (1 − mix) + wet · mix · WET_GAIN, the same law as Reverb.
 *
 * Reflections pan by where they come from (constant power, by their direction across the room's width); the tail's
 * lines reach left and right with orthogonal signs. The tail's level is normalised for its loop gains, so a longer
 * tail is spread out rather than made louder in sum: a fixed function of the settings, never of what is playing.
 *
 * Every line is allocated in prepare() for the largest room and never resized; setParams is block-rate and does
 * nothing for a setting that did not change.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "../util/math.hpp"
#include "../util/svf.hpp"

namespace dsp {

class RoomReverb {
 public:
  static constexpr double SIZE_MIN_M = 1;
  static constexpr double SIZE_MAX_M = 69;
  static constexpr double LENGTH_MIN_S = 0.1;
  static constexpr double LENGTH_MAX_S = 10;
  static constexpr double CROSSOVER_MIN_HZ = 80;
  static constexpr double CROSSOVER_MAX_HZ = 2000;
  /** The top of the band the Color split is the log-midpoint of, with the Crossover at the bottom. */
  static constexpr double SPLIT_TOP_HZ = 20000;
  /** How much shorter the damped side's T60 is at |Color| = 1. */
  static constexpr double COLOR_RATIO = 4;
  /** The speed of sound, m/s. */
  static constexpr double SPEED_OF_SOUND = 343;
  /** The room's length, width and height as multiples of Size (the height). */
  static constexpr double ROOM_SHAPE[3] = {1.6, 1.3, 1.0};
  /** The source and the listener, as fractions of the room's length, width and height. */
  static constexpr double SOURCE[3] = {0.40, 0.40, 0.46};
  static constexpr double LISTENER[3] = {0.43, 0.43, 0.50};
  /** Image sources of the first and second order: 6 + 18. */
  static constexpr int TAPS = 24;
  static constexpr int LINES = 8;

  RoomReverb() { buildTaps(); }

  void prepare(double sampleRate) {
    this->sampleRate = sampleRate;
    double longest = 0;
    for (const Tap& t : taps) longest = std::max(longest, t.secondsPerMeter);
    erLine.assign(static_cast<std::size_t>(std::ceil(longest * SIZE_MAX_M * sampleRate) + 4), 0.0f);
    const double lineMost = tailSeconds(SIZE_MAX_M) * LINE_RATIO[LINES - 1];
    for (auto& line : tail) line.assign(static_cast<std::size_t>(std::ceil(lineMost * sampleRate) + 4), 0.0f);
    lastSize = lastLength = lastColor = lastCrossover = -1;
    fresh = true;
    reset();
  }

  void reset() noexcept {
    std::fill(erLine.begin(), erLine.end(), 0.0f);
    for (auto& line : tail) std::fill(line.begin(), line.end(), 0.0f);
    erW = 0;
    std::fill(std::begin(tailW), std::end(tailW), 0);
    std::fill(std::begin(tapLow), std::end(tapLow), 0.0f);
    std::fill(std::begin(lineLow), std::end(lineLow), 0.0f);
    for (auto& s : highPass) s.reset();
    // Land the lines on their targets: nothing to glide from after a reset.
    std::copy(std::begin(tapTarget), std::end(tapTarget), std::begin(tapLen));
    std::copy(std::begin(lineTarget), std::end(lineTarget), std::begin(lineLen));
  }

  /** The settings, block-rate, in their own units: Size in m, Length in s, Color −1..1, Crossover in Hz. */
  void setParams(double size, double length, double color, double crossover) noexcept {
    const bool sizeChanged = size != lastSize;
    if (sizeChanged) {
      lastSize = size;
      const double s = clamp(size, SIZE_MIN_M, SIZE_MAX_M);
      for (int i = 0; i < TAPS; i++) tapTarget[i] = static_cast<float>(clampLen(std::round(taps[i].secondsPerMeter * s * sampleRate), erLine.size()));
      const double base = tailSeconds(s) * sampleRate;
      for (int i = 0; i < LINES; i++) lineTarget[i] = static_cast<float>(clampLen(std::round(base * LINE_RATIO[i]), tail[0].size()));
    }
    const bool crossoverChanged = crossover != lastCrossover;
    if (crossoverChanged) {
      lastCrossover = crossover;
      const double fc = std::min(clamp(crossover, CROSSOVER_MIN_HZ, CROSSOVER_MAX_HZ), 0.45 * sampleRate);
      highCoef = svf::coefficients(std::tan((PI * fc) / sampleRate));
      const double split = std::min(splitHz(fc), 0.45 * sampleRate);
      splitCoef = 1 - std::exp((-2 * PI * split) / sampleRate);
    }
    if (sizeChanged || length != lastLength || color != lastColor) {
      lastLength = length;
      lastColor = color;
      const double l = clamp(length, LENGTH_MIN_S, LENGTH_MAX_S);
      const double c = clamp(color, -1, 1);
      const double t60Low = l * std::pow(COLOR_RATIO, -std::max(c, 0.0));
      const double t60High = l * std::pow(COLOR_RATIO, std::min(c, 0.0));
      for (int i = 0; i < TAPS; i++) {
        const double seconds = tapTarget[i] / sampleRate;
        tapGainLow[i] = static_cast<float>(taps[i].gain * loss(seconds, t60Low));
        tapGainHigh[i] = static_cast<float>(taps[i].gain * loss(seconds, t60High));
      }
      double energy = 0;
      for (int i = 0; i < LINES; i++) {
        const double seconds = lineTarget[i] / sampleRate;
        lineGainLow[i] = static_cast<float>(loss(seconds, t60Low));
        lineGainHigh[i] = static_cast<float>(loss(seconds, t60High));
        energy += 0.5 * (static_cast<double>(lineGainLow[i]) * lineGainLow[i] + static_cast<double>(lineGainHigh[i]) * lineGainHigh[i]);
      }
      tailNorm = std::sqrt(1 - energy / LINES);
    }
    if (fresh) {
      fresh = false;
      std::copy(std::begin(tapTarget), std::end(tapTarget), std::begin(tapLen));
      std::copy(std::begin(lineTarget), std::end(lineTarget), std::begin(lineLen));
    }
  }

  /** In-place, stereo. `mix` is 0 (dry) to 1 (wet). */
  void process(float* left, float* right, int offset, int n, double mix) DSP_NONBLOCKING {
    if (erLine.empty()) return;
    const double wet = clamp(mix, 0, 1) * WET_GAIN;
    const double dry = 1 - clamp(mix, 0, 1);
    const double a = splitCoef;
    const double outScale = (tailNorm * TAIL_GAIN) / std::sqrt(static_cast<double>(LINES));
    const int erSize = static_cast<int>(erLine.size());
    const int lineSize = static_cast<int>(tail[0].size());

    glide(tapLen, tapTarget, tapStep, TAPS, n);
    glide(lineLen, lineTarget, lineStep, LINES, n);

    for (int s = 0; s < n; s++) {
      const double xl = left[offset + s];
      const double xr = right[offset + s];

      // Only what is above the crossover feeds the room.
      const double feed = highPass[1].high(highPass[0].high(0.5 * (xl + xr), highCoef), highCoef);
      erLine[erW] = static_cast<float>(feed);

      // Early reflections: one tap per image source, each with its spreading loss and the shelf for its travel.
      double erL = 0, erR = 0;
      for (int i = 0; i < TAPS; i++) {
        const double len = static_cast<double>(tapLen[i]) + tapStep[i];
        tapLen[i] = static_cast<float>(len);
        const double y = read(erLine.data(), erSize, erW, len);
        const double low = tapLow[i] + a * (y - tapLow[i]);
        tapLow[i] = static_cast<float>(low);
        const double r = tapGainHigh[i] * y + (static_cast<double>(tapGainLow[i]) - tapGainHigh[i]) * low;
        erL += r * taps[i].left;
        erR += r * taps[i].right;
      }
      erW = erW + 1 >= erSize ? 0 : erW + 1;
      erL *= ER_GAIN;
      erR *= ER_GAIN;

      // The tail: eight lines through a Householder matrix, each damped by its shelf, fed by the reflections.
      const double inject = 0.5 * (erL + erR) * TAIL_INPUT;
      double y[LINES], z[LINES];
      double sum = 0, tailL = 0, tailR = 0;
      for (int i = 0; i < LINES; i++) {
        const double len = static_cast<double>(lineLen[i]) + lineStep[i];
        lineLen[i] = static_cast<float>(len);
        y[i] = read(tail[i].data(), lineSize, tailW[i], len);
        const double low = lineLow[i] + a * (y[i] - lineLow[i]);
        lineLow[i] = static_cast<float>(low);
        z[i] = lineGainHigh[i] * y[i] + (static_cast<double>(lineGainLow[i]) - lineGainHigh[i]) * low;
        sum += z[i];
        tailL += y[i] * SIGN_LEFT[i];
        tailR += y[i] * SIGN_RIGHT[i];
      }
      const double h = (2.0 / LINES) * sum;
      for (int i = 0; i < LINES; i++) {
        const int w = tailW[i];
        tail[i][w] = static_cast<float>(z[i] - h + inject * SIGN_RIGHT[i]);
        tailW[i] = w + 1 >= lineSize ? 0 : w + 1;
      }

      left[offset + s] = static_cast<float>(xl * dry + (erL + tailL * outScale) * wet);
      right[offset + s] = static_cast<float>(xr * dry + (erR + tailR * outScale) * wet);
    }

    settle(tapLen, tapTarget, tapStep, TAPS);
    settle(lineLen, lineTarget, lineStep, LINES);
  }

  /** The Color split for a crossover: the log-midpoint between it and SPLIT_TOP_HZ. */
  static double splitHz(double crossover) noexcept { return std::sqrt(crossover * SPLIT_TOP_HZ); }

  /** The first reflection's delay after the direct sound, in seconds, for a room of `size` meters. */
  double firstReflectionSeconds(double size) const noexcept {
    double first = taps[0].secondsPerMeter;
    for (const Tap& t : taps) first = std::min(first, t.secondsPerMeter);
    return first * size;
  }

 private:
  /** The reflections' level into the wet signal, and the tail's input and output: with WET_GAIN, a full-wet room sits near the dry level. */
  static constexpr double ER_GAIN = 0.25;
  static constexpr double TAIL_INPUT = 0.5;
  static constexpr double TAIL_GAIN = 1.5;
  static constexpr double WET_GAIN = 0.6;
  /** The shortest base length of the tail's lines, so a tiny room's tail does not ring. */
  static constexpr double TAIL_MIN_S = 0.005;
  /** The tail's line lengths as multiples of the room's mean free path; no two in a simple ratio. */
  static constexpr double LINE_RATIO[LINES] = {1.000, 1.131, 1.263, 1.412, 1.567, 1.733, 1.911, 2.097};
  /** Orthogonal sign rows: which way each line reaches the left and right outputs (the right's also feeds it). */
  static constexpr double SIGN_LEFT[LINES] = {1, -1, 1, -1, 1, -1, 1, -1};
  static constexpr double SIGN_RIGHT[LINES] = {1, 1, -1, -1, 1, 1, -1, -1};
  /** The most a line's length may move per sample: a quarter — a swoop, not a click. */
  static constexpr double SLEW = 0.25;

  /** One image source at Size 1 m: its delay after the direct sound per meter of Size, its level against the first reflection, its pan. */
  struct Tap {
    double secondsPerMeter = 0;
    double gain = 0;
    double left = 0;
    double right = 0;
  };

  double sampleRate = 48000;
  Tap taps[TAPS];

  std::vector<float> erLine;
  std::int32_t erW = 0;
  float tapLen[TAPS] = {};
  float tapTarget[TAPS] = {};
  float tapStep[TAPS] = {};
  float tapGainLow[TAPS] = {};
  float tapGainHigh[TAPS] = {};
  float tapLow[TAPS] = {};

  std::vector<float> tail[LINES];
  std::int32_t tailW[LINES] = {};
  float lineLen[LINES] = {};
  float lineTarget[LINES] = {};
  float lineStep[LINES] = {};
  float lineGainLow[LINES] = {};
  float lineGainHigh[LINES] = {};
  float lineLow[LINES] = {};
  double tailNorm = 1;

  svf::Stage highPass[2];
  svf::Coefficients highCoef;
  double splitCoef = 0;

  double lastSize = -1;
  double lastLength = -1;
  double lastColor = -1;
  double lastCrossover = -1;
  /** True until the first settings arrive: the lines then land on their lengths rather than gliding up from nothing. */
  bool fresh = true;

  /** The image sources of order 1 and 2 in a 1 m room: per axis, mirror p ∈ {0, 1} and room m, with |m − p| + |m| walls. */
  void buildTaps() noexcept {
    double src[3], lis[3];
    for (int k = 0; k < 3; k++) {
      src[k] = SOURCE[k] * ROOM_SHAPE[k];
      lis[k] = LISTENER[k] * ROOM_SHAPE[k];
    }
    const double direct = distance(src, lis);
    int count = 0;
    double first = 1e9;
    double dist[TAPS] = {};
    double across[TAPS] = {};
    for (int p = 0; p < 8; p++) {
      const int px = p & 1, py = (p >> 1) & 1, pz = (p >> 2) & 1;
      for (int mx = -2; mx <= 2; mx++)
        for (int my = -2; my <= 2; my++)
          for (int mz = -2; mz <= 2; mz++) {
            const int order = std::abs(mx - px) + std::abs(mx) + std::abs(my - py) + std::abs(my) + std::abs(mz - pz) + std::abs(mz);
            if (order < 1 || order > 2 || count >= TAPS) continue;
            const double image[3] = {(1 - 2 * px) * src[0] + 2 * mx * ROOM_SHAPE[0], (1 - 2 * py) * src[1] + 2 * my * ROOM_SHAPE[1],
                                     (1 - 2 * pz) * src[2] + 2 * mz * ROOM_SHAPE[2]};
            dist[count] = distance(image, lis);
            across[count] = (image[1] - lis[1]) / dist[count];
            first = std::min(first, dist[count]);
            count++;
          }
    }
    for (int i = 0; i < count; i++) {
      const double angle = (clamp(across[i], -1, 1) + 1) * (PI / 4);
      taps[i] = {(dist[i] - direct) / SPEED_OF_SOUND, first / dist[i], std::cos(angle), std::sin(angle)};
    }
  }

  static double distance(const double* a, const double* b) noexcept {
    const double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  }

  /** The tail's base line length for a room: its mean free path 4V/S over the speed of sound, at least TAIL_MIN_S. */
  static double tailSeconds(double size) noexcept {
    const double l = ROOM_SHAPE[0] * size, w = ROOM_SHAPE[1] * size, h = ROOM_SHAPE[2] * size;
    const double path = (4 * l * w * h) / (2 * (l * w + l * h + w * h));
    return std::max(path / SPEED_OF_SOUND, TAIL_MIN_S);
  }

  /** The gain that loses 60 dB over t60 seconds, for `seconds` of travel. */
  static double loss(double seconds, double t60) noexcept { return std::pow(10.0, (-3 * seconds) / t60); }

  static void glide(float* len, const float* target, float* step, int count, int n) noexcept {
    for (int k = 0; k < count; k++) {
      const double d = (static_cast<double>(target[k]) - len[k]) / n;
      step[k] = static_cast<float>(d > SLEW ? SLEW : d < -SLEW ? -SLEW : d);
    }
  }

  static void settle(float* len, const float* target, const float* step, int count) noexcept {
    for (int k = 0; k < count; k++) {
      const double s = step[k];
      if (s > -SLEW && s < SLEW) len[k] = target[k];
    }
  }

  /**
   * A length the read can take: at least one sample, and three short of the line. Targets are whole samples, so a
   * settled line reads without interpolation (linear interpolation would damp the highs on every pass of the tail);
   * only a gliding line reads between samples.
   */
  static double clampLen(double len, std::size_t lineLength) noexcept {
    const double most = static_cast<double>(lineLength) - 3;
    return len < 1 ? 1 : len > most ? most : len;
  }

  /** The line `len` samples behind the write index `w`, linearly interpolated. */
  static double read(const float* line, int length, int w, double len) noexcept {
    double pos = w - len;
    if (pos < 0) pos += length;
    const int i0 = static_cast<int>(pos);
    const double frac = pos - i0;
    const int i1 = i0 + 1 >= length ? 0 : i0 + 1;
    return line[i0] + (static_cast<double>(line[i1]) - line[i0]) * frac;
  }
};

}  // namespace dsp
