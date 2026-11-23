/**
 * The window delay: a ping-pong delay whose two sides are a movable,
 * resizable window on the stereo field rather than hard left and right.
 *
 * The input, summed to mono, enters line A. Line A's echo lands on the ping
 * edge of the window and feeds line B; line B's echo lands on the pong edge
 * and feeds line A back. So the first echo is on the ping edge after Time,
 * the second on the pong edge after twice Time, and so on, every pass
 * Feedback quieter.
 *
 * Time     — DELAY_MIN_S..DELAY_MAX_S seconds, free; with Sync on it snaps
 *            to the nearest division of the sequencer's beat.
 * Feedback — how much of an echo returns, up to FEEDBACK_MAX.
 * Center   — −1..1, where the window sits on the field.
 * Width    — 0..1, how much of the field it spans (1: from −1 to +1 around
 *            0); each edge is clamped to the field on its own.
 * Side     — which edge the first echo lands on: Left or Right.
 * Mix      — wet/dry, passed to process().
 *
 * The lines are allocated in prepare() for DELAY_MAX_S and never resized.
 * The read tap glides to a new time so a change swoops rather than clicks,
 * and the four pan gains glide over PAN_GLIDE_MS, so moving the window or
 * flipping the side sweeps. The setters are block-rate and look only at what
 * changed.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "../util/math.hpp"
#include "windowDelayMath.hpp"

namespace dsp {

class WindowDelay {
 public:
  void prepare(double sampleRate) {
    this->sampleRate = sampleRate;
    const std::size_t length = static_cast<std::size_t>(std::ceil(DELAY_MAX_S * sampleRate)) + 4;
    lineA.assign(length, 0.0f);
    lineB.assign(length, 0.0f);
    panCoef = 1 - std::exp(-1 / ((PAN_GLIDE_MS / 1000) * sampleRate));
    lastTime = -1;
    lastSync = -1;
    lastTempo = -1;
    lastFeedback = -1;
    lastCenter = std::numeric_limits<double>::quiet_NaN();
    lastWidth = std::numeric_limits<double>::quiet_NaN();
    lastSide = -1;
    fresh = true;
    reset();
  }

  void reset() noexcept {
    std::fill(lineA.begin(), lineA.end(), 0.0f);
    std::fill(lineB.begin(), lineB.end(), 0.0f);
    w = 0;
    len = target;
    std::copy(std::begin(panTarget), std::end(panTarget), std::begin(pan));
  }

  /** The settings, block-rate. `tempo` is the host's BPM, for Sync. */
  void setParams(double time, double sync, double feedback, double center, double width, double side, double tempo) noexcept {
    const int synced = sync >= 0.5 ? 1 : 0;
    if (time != lastTime || synced != lastSync || (synced == 1 && tempo != lastTempo)) {
      lastTime = time;
      lastSync = synced;
      lastTempo = tempo;
      const double most = static_cast<double>(lineA.size()) - 4;
      const double samples = echoSeconds(time, synced, tempo) * sampleRate;
      target = samples < 1 ? 1 : samples > most ? most : samples;
    }
    if (feedback != lastFeedback) {
      lastFeedback = feedback;
      const double f = feedback < 0 ? 0 : feedback > 1 ? 1 : feedback;
      gain = f * FEEDBACK_MAX;
    }
    const int s = side >= 0.5 ? 1 : 0;
    if (center != lastCenter || width != lastWidth || s != lastSide) {
      lastCenter = center;
      lastWidth = width;
      lastSide = s;
      const WindowEdges edges = windowEdges(center, width, s);
      const PanGains a = panGains(edges.ping);
      const PanGains b = panGains(edges.pong);
      panTarget[0] = static_cast<float>(a.l);
      panTarget[1] = static_cast<float>(a.r);
      panTarget[2] = static_cast<float>(b.l);
      panTarget[3] = static_cast<float>(b.r);
    }
    if (fresh) {
      fresh = false;
      len = target;
      std::copy(std::begin(panTarget), std::end(panTarget), std::begin(pan));
    }
  }

  /** In-place, stereo. `mix` is 0 (dry) to 1 (wet). */
  void process(float* left, float* right, int offset, int n, double mix) DSP_NONBLOCKING {
    const double wet = mix;
    const double dry = 1 - mix;
    const double g = gain;
    float* a = lineA.data();
    float* b = lineB.data();
    const int size = static_cast<int>(lineA.size());
    const double k = panCoef;
    const float* tp = panTarget;
    double pingL = pan[0];
    double pingR = pan[1];
    double pongL = pan[2];
    double pongR = pan[3];
    int wi = w;
    double l = len;
    const double d = (target - l) / n;
    const double step = d > SLEW ? SLEW : d < -SLEW ? -SLEW : d;

    for (int i = 0; i < n; i++) {
      const double xL = left[offset + i];
      const double xR = right[offset + i];
      l += step;
      pingL += k * (tp[0] - pingL);
      pingR += k * (tp[1] - pingR);
      pongL += k * (tp[2] - pongL);
      pongR += k * (tp[3] - pongR);

      const double yA = tap(a, size, wi, l);
      const double yB = tap(b, size, wi, l);
      // The input enters A; A's echo feeds B, and B's feeds A back.
      a[wi] = static_cast<float>((xL + xR) * 0.5 + yB * g);
      b[wi] = static_cast<float>(yA * g);
      wi = wi + 1 >= size ? 0 : wi + 1;

      left[offset + i] = static_cast<float>(xL * dry + (yA * pingL + yB * pongL) * wet);
      right[offset + i] = static_cast<float>(xR * dry + (yA * pingR + yB * pongR) * wet);
    }

    w = wi;
    len = step > -SLEW && step < SLEW ? target : l;
    pan[0] = static_cast<float>(pingL);
    pan[1] = static_cast<float>(pingR);
    pan[2] = static_cast<float>(pongL);
    pan[3] = static_cast<float>(pongR);
  }

 private:
  /** The most a line's length may move per sample: half — a swoop, not a click. */
  static constexpr double SLEW = 0.5;

  double sampleRate = 48000;
  std::vector<float> lineA;
  std::vector<float> lineB;
  int w = 0;
  double len = 1;
  double target = 1;
  double gain = 0;
  bool fresh = true;

  /** The pan gains now and their targets: ping L/R, pong L/R. */
  float pan[4] = {};
  float panTarget[4] = {};
  double panCoef = 0;

  double lastTime = -1;
  int lastSync = -1;
  double lastTempo = -1;
  double lastFeedback = -1;
  double lastCenter = std::numeric_limits<double>::quiet_NaN();
  double lastWidth = std::numeric_limits<double>::quiet_NaN();
  int lastSide = -1;

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
