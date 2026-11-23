/**
 * The reverb: a pre-delay, a bank of parallel comb filters with damping in
 * their feedback, a chain of allpasses to diffuse their sum, and a tone tilt
 * on the wet signal — per channel, the right channel's lines offset a
 * little to open the stereo image. Freeverb's shape, tuned by mode
 * (reverbModes.hpp) and by the settings:
 *
 *   - Time is the tail's T60: every comb's feedback is set so it loses 60 dB
 *     in that time whatever its length, so the tail decays at one rate.
 *   - Curve bends the decay with a second comb bank at the same lengths but
 *     half the time. Positive subtracts it — the first echoes cancel
 *     and the tail swells in (a bloom); negative adds it — the tail falls
 *     fast, then what remains lingers at the tail's own rate. At 0 the
 *     second bank is skipped and cleared, so it is bit-for-bit absent.
 *   - Size scales the mode's delays. A line's length glides to its target,
 *     read at a fractional tap, so turning Size (or switching mode) swoops
 *     the tail's pitch rather than clicking.
 *   - Damping is the one-pole in each comb's feedback; Tilt a first-order
 *     tilt about 1 kHz on the wet signal, unity there.
 *
 * The wet level is normalised so a mode with fewer combs and a longer or
 * shorter tail lands near the same level — a fixed function of the settings,
 * never of what is playing.
 *
 * Every line is allocated in prepare() for the longest mode at the largest
 * size and never resized; the setters are block-rate and do nothing when
 * nothing changed.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "../util/math.hpp"
#include "reverbModes.hpp"

namespace dsp {

class Reverb {
 public:
  void prepare(double sampleRate) {
    this->sampleRate = sampleRate;
    const double ms = sampleRate / 1000;
    const double spreadSamples = std::ceil(reverbMaxSpreadMs() * ms);
    const std::size_t combLength = static_cast<std::size_t>(std::ceil(reverbMaxCombMs() * REVERB_SIZE_MAX * ms) + spreadSamples + 2);
    const std::size_t apLength = static_cast<std::size_t>(std::ceil(reverbMaxApMs() * REVERB_SIZE_MAX * ms) + spreadSamples + 2);
    const std::size_t preLength = static_cast<std::size_t>(std::ceil(PREDELAY_MAX_S * sampleRate) + 2);
    for (auto& b : combA) b.assign(combLength, 0.0f);
    for (auto& b : combB) b.assign(combLength, 0.0f);
    for (auto& b : ap) b.assign(apLength, 0.0f);
    for (auto& b : pre) b.assign(preLength, 0.0f);
    mode = -1;
    lastTime = -1;
    lastCurve = 2;
    lastSize = -1;
    lastDamping = -1;
    lastPredelay = -1;
    lastTilt = 2;
    fresh = true;
    reset();
  }

  void reset() noexcept {
    for (auto& b : combA) std::fill(b.begin(), b.end(), 0.0f);
    for (auto& b : combB) std::fill(b.begin(), b.end(), 0.0f);
    for (auto& b : ap) std::fill(b.begin(), b.end(), 0.0f);
    for (auto& b : pre) std::fill(b.begin(), b.end(), 0.0f);
    std::fill(std::begin(combW), std::end(combW), 0);
    std::fill(std::begin(apW), std::end(apW), 0);
    std::fill(std::begin(preW), std::end(preW), 0);
    std::fill(std::begin(storeA), std::end(storeA), 0.0f);
    std::fill(std::begin(storeB), std::end(storeB), 0.0f);
    std::fill(std::begin(tiltLp), std::end(tiltLp), 0.0f);
    // Land the lines on their targets: nothing to glide from after a reset.
    std::copy(std::begin(combTarget), std::end(combTarget), std::begin(combLen));
    std::copy(std::begin(apTarget), std::end(apTarget), std::begin(apLen));
    std::copy(std::begin(preTarget), std::end(preTarget), std::begin(preLen));
  }

  /** The mode's tuning: which lines run and at what delays. Lines that stop running are cleared. */
  void setMode(double modeValue) noexcept {
    const int m = modeValue < 0 ? 0 : modeValue >= REVERB_MODE_COUNT ? REVERB_MODE_COUNT - 1 : static_cast<int>(modeValue);
    if (m == mode) return;
    mode = m;
    const ReverbTuning& t = reverbModes()[m];
    const int newCombs = t.combCount;
    const int newAps = t.apCount;
    for (int ch = 0; ch < 2; ch++) {
      for (int i = newCombs; i < combs; i++) clearComb(ch * MAX_COMBS + i);
      for (int i = newAps; i < aps; i++) {
        std::vector<float>& line = ap[ch * MAX_ALLPASS + i];
        std::fill(line.begin(), line.end(), 0.0f);
        apW[ch * MAX_ALLPASS + i] = 0;
      }
    }
    combs = newCombs;
    aps = newAps;
    apFeedback = t.apFeedback;
    spread = (t.spreadMs * sampleRate) / 1000;
    // The lengths and gains follow the tuning: make the setters look again.
    lastSize = -1;
    lastTime = -1;
    norm = normFor();
  }

  /** The settings, block-rate. Each is looked at only when it changed. */
  void setParams(double time, double curve, double size, double damping, double predelay, double tilt) noexcept {
    if (mode < 0) setMode(0);
    const bool sizeChanged = size != lastSize;
    if (sizeChanged) {
      lastSize = size;
      const ReverbTuning& t = reverbModes()[mode];
      const double f = sizeFactor(size) * sampleRate / 1000;
      for (int ch = 0; ch < 2; ch++) {
        const double offset = ch == 1 ? spread : 0;
        for (int i = 0; i < combs; i++) combTarget[ch * MAX_COMBS + i] = static_cast<float>(clampLen(t.combMs[i] * f + offset, combA[0].size()));
        for (int i = 0; i < aps; i++) apTarget[ch * MAX_ALLPASS + i] = static_cast<float>(clampLen(t.apMs[i] * f + offset, ap[0].size()));
      }
    }
    if (sizeChanged || time != lastTime) {
      lastTime = time;
      const double t60 = time < 0.01 ? 0.01 : time;
      for (int k = 0; k < 2 * MAX_COMBS; k++) {
        const double len = combTarget[k];
        gainA[k] = static_cast<float>(combGain(len, t60, sampleRate));
        gainB[k] = static_cast<float>(combGain(len, t60 * CURVE_RATIO, sampleRate));
      }
      norm = normFor();
    }
    if (curve != lastCurve) {
      lastCurve = curve;
      const double c = curve < -1 ? -1 : curve > 1 ? 1 : curve;
      // Positive subtracts the fast bank (a bloom), negative adds it (a linger).
      weightB = -c;
      const bool use = c != 0;
      if (!use && useB) {
        // Cleared on the way to 0, so a stale tail does not come back with the curve.
        for (auto& b : combB) std::fill(b.begin(), b.end(), 0.0f);
        std::fill(std::begin(storeB), std::end(storeB), 0.0f);
      }
      useB = use;
    }
    if (damping != lastDamping) {
      lastDamping = damping;
      const double d = damping < 0 ? 0 : damping > 1 ? 1 : damping;
      damp = d * DAMPING_MAX;
    }
    if (predelay != lastPredelay) {
      lastPredelay = predelay;
      const double p = predelay < 0 ? 0 : predelay > PREDELAY_MAX_S ? PREDELAY_MAX_S : predelay;
      const double len = p * sampleRate;
      const double most = static_cast<double>(pre[0].size()) - 2;
      preTarget[0] = static_cast<float>(len > most ? most : len);
      preTarget[1] = preTarget[0];
    }
    if (tilt != lastTilt) {
      lastTilt = tilt;
      const double t = tilt < -1 ? -1 : tilt > 1 ? 1 : tilt;
      tilting = t != 0;
      gainLow = std::pow(10.0, (-t * TILT_DB) / 20);
      gainHigh = 1 / gainLow;
      // A first-order tilt, unity at TILT_HZ: gH·(s + wc·gL) / (s + wc·gH),
      // which is gH·x + (gL − gH)·lowpass(x) with the pole at TILT_HZ·gH.
      tiltCoef = 1 - std::exp((-2 * PI * TILT_HZ * gainHigh) / sampleRate);
    }
    if (fresh) {
      fresh = false;
      std::copy(std::begin(combTarget), std::end(combTarget), std::begin(combLen));
      std::copy(std::begin(apTarget), std::end(apTarget), std::begin(apLen));
      std::copy(std::begin(preTarget), std::end(preTarget), std::begin(preLen));
    }
  }

  /** In-place, stereo. `mix` is 0 (dry) to 1 (wet). */
  void process(float* left, float* right, int offset, int n, double mix) DSP_NONBLOCKING {
    if (mode < 0) setMode(0);
    const double wet = mix * WET_GAIN;
    const double dry = 1 - mix;
    const double keep = 1 - damp;
    const double fb = apFeedback;
    const double wB = weightB;
    const double tc = tiltCoef;
    const double gL = gainLow;
    const double gH = gainHigh;

    glide(combLen, combTarget, combStep, 2 * MAX_COMBS, n);
    glide(apLen, apTarget, apStep, 2 * MAX_ALLPASS, n);
    glide(preLen, preTarget, preStep, 2, n);

    for (int ch = 0; ch < 2; ch++) {
      float* buf = ch == 0 ? left : right;
      const int cBase = ch * MAX_COMBS;
      const int aBase = ch * MAX_ALLPASS;
      float* preLine = pre[ch].data();
      const int preSize = static_cast<int>(pre[ch].size());
      int pw = preW[ch];
      double pl = preLen[ch];
      const double ps = preStep[ch];
      double lp = tiltLp[ch];

      for (int s = 0; s < n; s++) {
        const double x = buf[offset + s];

        // The pre-delay: written first, so a length of 0 reads the sample itself.
        preLine[pw] = static_cast<float>(x);
        pl += ps;
        const double input = tap(preLine, preSize, pw, pl) * INPUT_GAIN;
        if (++pw >= preSize) pw = 0;

        double acc = 0;
        for (int i = 0; i < combs; i++) {
          const int k = cBase + i;
          float* lineA = combA[k].data();
          const int size = static_cast<int>(combA[k].size());
          const int w = combW[k];
          const double len = static_cast<double>(combLen[k]) + combStep[k];
          combLen[k] = static_cast<float>(len);
          const double yA = tap(lineA, size, w, len);
          const double sA = yA * keep + storeA[k] * damp;
          storeA[k] = static_cast<float>(sA);
          lineA[w] = static_cast<float>(input + sA * gainA[k]);
          acc += yA;
          if (useB) {
            float* lineB = combB[k].data();
            const double yB = tap(lineB, size, w, len);
            const double sB = yB * keep + storeB[k] * damp;
            storeB[k] = static_cast<float>(sB);
            lineB[w] = static_cast<float>(input + sB * gainB[k]);
            acc += yB * wB;
          }
          combW[k] = w + 1 >= size ? 0 : w + 1;
        }
        acc *= norm;

        for (int i = 0; i < aps; i++) {
          const int k = aBase + i;
          float* line = ap[k].data();
          const int size = static_cast<int>(ap[k].size());
          const int w = apW[k];
          const double len = static_cast<double>(apLen[k]) + apStep[k];
          apLen[k] = static_cast<float>(len);
          // The Schroeder allpass proper — unity gain at any feedback, so
          // the feedback sets only how the mode disperses, never its level.
          const double y = tap(line, size, w, len);
          const double v = acc + y * fb;
          line[w] = static_cast<float>(v);
          apW[k] = w + 1 >= size ? 0 : w + 1;
          acc = y - v * fb;
        }

        if (tilting) {
          lp += tc * (acc - lp);
          acc = gH * acc + (gL - gH) * lp;
        }

        buf[offset + s] = static_cast<float>(x * dry + acc * wet);
      }

      preW[ch] = pw;
      preLen[ch] = static_cast<float>(pl);
      tiltLp[ch] = static_cast<float>(lp);
    }

    settle(combLen, combTarget, combStep, 2 * MAX_COMBS);
    settle(apLen, apTarget, apStep, 2 * MAX_ALLPASS);
    settle(preLen, preTarget, preStep, 2);
  }

 private:
  /** Scales the input into the combs; with WET_GAIN, sets the wet path's level: a full-wet Hall holds a note about 7 dB under the dry. */
  static constexpr double INPUT_GAIN = 0.05;
  static constexpr double WET_GAIN = 2;
  /** The comb feedback the level is normalised against (Freeverb's room size). */
  static constexpr double REF_GAIN = 0.84;
  /** The most a line's length may move per sample: a quarter — a swoop, not a click. */
  static constexpr double SLEW = 0.25;

  double sampleRate = 48000;

  /* Lines, per channel then per index: [ch * MAX + i]. Banks A and B share a length and a write index. */
  std::vector<float> combA[2 * MAX_COMBS];
  std::vector<float> combB[2 * MAX_COMBS];
  std::int32_t combW[2 * MAX_COMBS] = {};
  float combLen[2 * MAX_COMBS] = {};
  float combTarget[2 * MAX_COMBS] = {};
  float combStep[2 * MAX_COMBS] = {};
  float gainA[2 * MAX_COMBS] = {};
  float gainB[2 * MAX_COMBS] = {};
  float storeA[2 * MAX_COMBS] = {};
  float storeB[2 * MAX_COMBS] = {};

  std::vector<float> ap[2 * MAX_ALLPASS];
  std::int32_t apW[2 * MAX_ALLPASS] = {};
  float apLen[2 * MAX_ALLPASS] = {};
  float apTarget[2 * MAX_ALLPASS] = {};
  float apStep[2 * MAX_ALLPASS] = {};

  std::vector<float> pre[2];
  std::int32_t preW[2] = {};
  float preLen[2] = {};
  float preTarget[2] = {};
  float preStep[2] = {};

  float tiltLp[2] = {};
  double tiltCoef = 0;
  double gainLow = 1;
  double gainHigh = 1;
  bool tilting = false;

  /* The mode's tuning. */
  int mode = -1;
  int combs = 0;
  int aps = 0;
  double apFeedback = 0.5;
  double spread = 0;

  /* The settings, as the loop reads them. */
  double damp = 0;
  double weightB = 0;
  bool useB = false;
  double norm = 1;

  /* What the setters were last given, so they do nothing when nothing changed. */
  double lastTime = -1;
  double lastCurve = 2;
  double lastSize = -1;
  double lastDamping = -1;
  double lastPredelay = -1;
  double lastTilt = 2;
  /** True until the first settings arrive: the lines then land on their lengths rather than gliding up from nothing. */
  bool fresh = true;

  /** Per-sample steps that carry each length toward its target over the block, no faster than SLEW. */
  static void glide(float* len, const float* target, float* step, int count, int n) noexcept {
    for (int k = 0; k < count; k++) {
      const double d = (static_cast<double>(target[k]) - len[k]) / n;
      step[k] = static_cast<float>(d > SLEW ? SLEW : d < -SLEW ? -SLEW : d);
    }
  }

  /** After the block, a length that was not slewed has arrived: land it exactly. */
  static void settle(float* len, const float* target, const float* step, int count) noexcept {
    for (int k = 0; k < count; k++) {
      const double s = step[k];
      if (s > -SLEW && s < SLEW) len[k] = target[k];
    }
  }

  void clearComb(int k) noexcept {
    std::fill(combA[k].begin(), combA[k].end(), 0.0f);
    std::fill(combB[k].begin(), combB[k].end(), 0.0f);
    combW[k] = 0;
    storeA[k] = 0;
    storeB[k] = 0;
  }

  /** A length the fractional tap can read: at least one sample, and two short of the line. */
  static double clampLen(double len, std::size_t lineLength) noexcept {
    const double most = static_cast<double>(lineLength) - 2;
    return len < 1 ? 1 : len > most ? most : len;
  }

  /**
   * The comb sum's scaling: every mode to the level of eight combs, and the
   * tail's energy held where Freeverb's fixed feedback put it — a longer
   * tail is spread out, not made louder in sum.
   */
  double normFor() const noexcept {
    if (combs == 0) return 0;
    double g = 0;
    for (int i = 0; i < combs; i++) g += gainA[i];
    g /= combs;
    const double energy = std::sqrt((1 - g) / (1 - REF_GAIN));
    return (static_cast<double>(MAX_COMBS) / combs) * energy;
  }

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
