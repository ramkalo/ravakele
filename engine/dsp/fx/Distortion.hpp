/**
 * A hard tube distortion, plainly digital: three hard-knee stages in a row, each driven by the same gain, between a
 * tone filter before them and one after, run at four times the sample rate.
 *
 * Drive  — the gain into each stage, unity up to STAGE_MAX_DB (so STAGES times that in all). There is no makeup gain:
 *          the output gets louder as Drive rises until the stages reach CEILING, and from there only denser.
 * Color  — the tone, bipolar: −1 Dark (the lows whole into the stages, the top rolled off after them), 0 the centre,
 *          +1 Bright (the lows cut before the stages, the top open). Every TONES field is interpolated between the
 *          three anchors. The curve does not depend on it.
 * Bias   — 0 clean and static; up to 1 the first stage's operating point is pushed toward its low rail (BIAS_MAX), and
 *          it sags further with the playing (SAG times an envelope of the driven input).
 * Mix    — wet/dry.
 *
 * Signal path per channel: pre filter (a low shelf: lows kept at `keep`, above `preHz` passed whole) → 4× up →
 * stage 1 (clip(g·x + b(t)) − clip(b(t))) → high-pass → stage 2 → high-pass → stage 3 → 4× down → post low-pass
 * (Butterworth, `postHz`) → DC block → mix with the latency-matched dry.
 *
 * Drive, Bias and Mix glide per sample and Color per block, so a moved control does not step.
 *
 * The pure curve functions are exported so the UI can draw the same curve.
 */
#pragma once

#include <algorithm>
#include <cmath>

#include "../util/blockSmoother.hpp"
#include "../util/math.hpp"
#include "../util/svf.hpp"

namespace dsp {

namespace distortion {

/**
 * One tone: the pre filter is a one-pole low shelf (the lows below `preHz` scaled by `keep`, everything above passed
 * whole), the post filter a Butterworth low-pass at `postHz`.
 */
struct Tone {
  double preHz;
  double keep;
  double postHz;
};

/** The three anchors: Dark (Color −1), the centre (0), Bright (+1). */
constexpr Tone TONES[3] = {
    {150, 1.0, 3000},
    {150, 0.7, 9000},
    {700, 0.2, 18000},
};

/** How many stages run in a row. */
constexpr int STAGES = 3;
/** The gain into each stage at full Drive, in dB. */
constexpr double STAGE_MAX_DB = 18;
/** A stage's upper rail: the most the wet signal reaches before the filters after it. */
constexpr double CEILING = 0.7;
/** A stage's lower rail, as a share of CEILING: the asymmetry, so even harmonics. */
constexpr double RAIL = 0.7;
/** The corner of the high-pass between two stages. */
constexpr double STAGE_HP_HZ = 10;
/** How far full Bias pushes the first stage's operating point toward its low rail, as a share of CEILING. */
constexpr double BIAS_MAX = 0.6;
/** How far the envelope (0..1) pushes it further at full Bias, as a share of CEILING. */
constexpr double SAG = 0.4;
/** The sag envelope's attack and release, in ms. */
constexpr double SAG_ATTACK_MS = 3;
constexpr double SAG_RELEASE_MS = 90;
/** The DC blocker's corner. */
constexpr double DC_HZ = 10;
/** The time constants Drive, Bias and Mix (per sample) and Color (per block) glide with, in ms. */
constexpr double PARAM_GLIDE_MS = 10;
constexpr double TONE_GLIDE_MS = 20;

/** The tone at a Color (−1..1): each field between its two anchors, the corner frequencies geometrically. */
inline Tone tone(double color) noexcept {
  const double c = clamp(color, -1, 1);
  const Tone& lo = c <= 0 ? TONES[0] : TONES[1];
  const Tone& hi = c <= 0 ? TONES[1] : TONES[2];
  const double t = c <= 0 ? c + 1 : c;
  return {lo.preHz * std::pow(hi.preHz / lo.preHz, t), lo.keep + (hi.keep - lo.keep) * t, lo.postHz * std::pow(hi.postHz / lo.postHz, t)};
}

/** The gain a Drive setting (0..1) puts into each stage: unity to STAGE_MAX_DB. */
inline double stageGain(double drive) noexcept {
  return std::pow(10.0, (clamp(drive, 0, 1) * STAGE_MAX_DB) / 20);
}

/** The hard knee on unit rails: v / (1 + v⁴)^¼, unity slope at zero. */
inline double unit(double v) noexcept {
  const double v2 = v * v;
  return v / std::sqrt(std::sqrt(1 + v2 * v2));
}

/** One stage's clipper: rail CEILING above and RAIL · CEILING below, unity slope at zero. */
inline double clip(double x) noexcept {
  return x >= 0 ? CEILING * unit(x / CEILING) : RAIL * CEILING * unit(x / (RAIL * CEILING));
}

/** The first stage's resting operating point at a Bias (0..1), before any sag. */
inline double operatingPoint(double bias) noexcept { return -CEILING * BIAS_MAX * clamp(bias, 0, 1); }

/**
 * The static curve at a Drive and Bias (0..1): the stages in a row at the stage gain g, the first around its operating
 * point b so it rests at zero, without the high-passes between them.
 */
inline double curveAt(double x, double g, double b) noexcept {
  double y = clip(g * x + b) - clip(b);
  for (int s = 1; s < STAGES; s++) y = clip(g * y);
  return y;
}

inline double curve(double x, double drive, double bias) noexcept {
  return curveAt(x, stageGain(drive), operatingPoint(bias));
}

/* ---- 4× oversampling: two cascaded 2× stages, each a Blackman-windowed half-band FIR up and down ---- */

/** The outer stage (base ↔ 2×) and the inner stage (2× ↔ 4×); (INNER_TAPS − 1) / 2 must be even. */
constexpr int OUTER_TAPS = 47;
constexpr int INNER_TAPS = 25;
/** Latency of the whole up + down chain, in samples at the base rate. */
constexpr int OS_LATENCY = (OUTER_TAPS - 1) / 2 + (INNER_TAPS - 1) / 4;
/** How many times the base rate the stages run at. */
constexpr int OVERSAMPLE = 4;

/** A Blackman-windowed half-band low-pass, normalised to unity DC gain. */
template <int N>
struct Halfband {
  double h[N] = {};
  Halfband() noexcept {
    const double mid = (N - 1) / 2.0;
    double sum = 0;
    for (int i = 0; i < N; i++) {
      const double t = i - mid;
      const double sinc = t == 0 ? 1 : std::sin(PI * 0.5 * t) / (PI * 0.5 * t);
      const double w = 0.42 - 0.5 * std::cos((2 * PI * i) / (N - 1)) + 0.08 * std::cos((4 * PI * i) / (N - 1));
      h[i] = sinc * w;
      sum += h[i];
    }
    for (int i = 0; i < N; i++) h[i] /= sum;
  }
};

/** One 2× upsampler: one sample in, two out (zero-stuffed through the half-band, two polyphase branches). */
template <int N>
struct Up2 {
  double hist[N] = {};
  int pos = 0;
  void reset() noexcept {
    std::fill(std::begin(hist), std::end(hist), 0.0);
    pos = 0;
  }
  void tick(const Halfband<N>& f, double x, double& y0, double& y1) noexcept {
    hist[pos] = x;
    double u0 = 0;
    double u1 = 0;
    for (int k = 0, p = pos; 2 * k < N; k++, p = p == 0 ? N - 1 : p - 1) {
      u0 += f.h[2 * k] * hist[p];
      if (2 * k + 1 < N) u1 += f.h[2 * k + 1] * hist[p];
    }
    pos = pos + 1 == N ? 0 : pos + 1;
    y0 = 2 * u0;
    y1 = 2 * u1;
  }
};

/** One 2× decimator: two samples in, one out (filtered after the first of the pair). */
template <int N>
struct Down2 {
  double hist[N] = {};
  int pos = 0;
  void reset() noexcept {
    std::fill(std::begin(hist), std::end(hist), 0.0);
    pos = 0;
  }
  double tick(const Halfband<N>& f, double x0, double x1) noexcept {
    hist[pos] = x0;
    double y = 0;
    for (int k = 0, p = pos; k < N; k++, p = p == 0 ? N - 1 : p - 1) y += f.h[k] * hist[p];
    pos = pos + 1 == N ? 0 : pos + 1;
    hist[pos] = x1;
    pos = pos + 1 == N ? 0 : pos + 1;
    return y;
  }
};

}  // namespace distortion

class Distortion {
 public:
  void prepare(double sampleRate) {
    using namespace distortion;
    this->sampleRate = sampleRate;
    dcR = 1 - (2 * PI * DC_HZ) / sampleRate;
    stageHp = 1 - std::exp((-2 * PI * STAGE_HP_HZ) / (OVERSAMPLE * sampleRate));
    sagAttack = 1 - std::exp(-1000 / (SAG_ATTACK_MS * sampleRate));
    sagRelease = 1 - std::exp(-1000 / (SAG_RELEASE_MS * sampleRate));
    gainS.prepare(sampleRate, PARAM_GLIDE_MS);
    pointS.prepare(sampleRate, PARAM_GLIDE_MS);
    sagS.prepare(sampleRate, PARAM_GLIDE_MS);
    mixS.prepare(sampleRate, PARAM_GLIDE_MS);
    colorS.prepare(sampleRate);
    colorS.setTime(TONE_GLIDE_MS);
    reset();
  }

  /** Clears every state; the next block starts on the params as set, with no glide. */
  void reset() noexcept {
    for (Channel& c : ch) c.reset();
    settled = false;
  }

  /** drive 0..1, color −1..1, bias 0..1, mix 0..1. */
  void setParams(double drive, double color, double bias, double mix = 1) noexcept {
    using namespace distortion;
    this->color = clamp(color, -1, 1);
    this->mix = clamp(mix, 0, 1);
    gain = stageGain(drive);
    point = operatingPoint(bias);
    sagDepth = CEILING * SAG * clamp(bias, 0, 1);
  }

  void process(float* left, float* right, int offset, int n) DSP_NONBLOCKING {
    if (!settled) {
      gainS.reset(gain);
      pointS.reset(point);
      sagS.reset(sagDepth);
      mixS.reset(mix);
      colorS.reset(color);
      settled = true;
    }
    setTone(colorS.advance(color, n));
    Channel& cl = ch[0];
    Channel& cr = ch[1];
    for (int s = 0; s < n; s++) {
      const int i = offset + s;
      const double g = gainS.step(gain);
      const double p = pointS.step(point);
      const double sag = sagS.step(sagDepth);
      const double m = mixS.step(mix);
      left[i] = static_cast<float>(tick(cl, left[i], g, p, sag, m));
      right[i] = static_cast<float>(tick(cr, right[i], g, p, sag, m));
    }
  }

 private:
  struct Channel {
    double preS = 0;
    distortion::Up2<distortion::OUTER_TAPS> upOuter;
    distortion::Up2<distortion::INNER_TAPS> upInner;
    distortion::Down2<distortion::INNER_TAPS> downInner;
    distortion::Down2<distortion::OUTER_TAPS> downOuter;
    double env = 0;
    double hp[distortion::STAGES - 1] = {};
    svf::Stage post;
    double dcX = 0;
    double dcY = 0;
    double dry[distortion::OS_LATENCY] = {};
    int dryPos = 0;

    void reset() noexcept {
      preS = 0;
      upOuter.reset();
      upInner.reset();
      downInner.reset();
      downOuter.reset();
      env = 0;
      std::fill(std::begin(hp), std::end(hp), 0.0);
      post.reset();
      dcX = dcY = 0;
      std::fill(std::begin(dry), std::end(dry), 0.0);
      dryPos = 0;
    }
  };

  distortion::Halfband<distortion::OUTER_TAPS> outer;
  distortion::Halfband<distortion::INNER_TAPS> inner;
  double sampleRate = 48000;
  // The targets the params set, and the glides toward them.
  double color = 0;
  double mix = 1;
  double gain = distortion::stageGain(0.25);
  double point = 0;
  double sagDepth = 0;
  Smoother gainS, pointS, sagS, mixS;
  BlockSmoother colorS;
  bool settled = false;
  // The tone's filters at the gliding Color.
  double keep = 1;
  double preG = 0;
  svf::Coefficients postC;
  double dcR = 0.9987;
  double stageHp = 0.001;
  double sagAttack = 0.007;
  double sagRelease = 0.0002;
  Channel ch[2];

  void setTone(double c) noexcept {
    const distortion::Tone t = distortion::tone(c);
    keep = t.keep;
    const double g = std::tan(PI * std::min(t.preHz, 0.45 * sampleRate) / sampleRate);
    preG = g / (1 + g);
    postC = svf::coefficients(std::tan(PI * std::min(t.postHz, 0.45 * sampleRate) / sampleRate));
  }

  /** The stages on one oversampled sample: the first around its operating point, a high-pass before each later one. */
  double shape(Channel& c, double x, double g, double b, double rest) const noexcept {
    double y = distortion::clip(g * x + b) - rest;
    for (double& low : c.hp) {
      low += stageHp * (y - low);
      y = distortion::clip(g * (y - low));
    }
    return y;
  }

  double tick(Channel& c, double x, double g, double point, double sag, double mix) noexcept {
    using namespace distortion;
    // Pre shelf: a TPT one-pole split; the lows kept at `keep`, the highs whole.
    const double vlp = (x - c.preS) * preG;
    const double low = vlp + c.preS;
    c.preS = low + vlp;
    const double pre = (x - low) + keep * low;
    // Sag: the driven input's envelope (0..1) pushes the operating point toward the low rail.
    const double level = std::min(std::fabs(g * pre) / CEILING, 1.0);
    c.env += (level > c.env ? sagAttack : sagRelease) * (level - c.env);
    const double b = point - sag * c.env;
    const double rest = clip(b);
    // Up 4×, the stages, down.
    double a0, a1, q0, q1, q2, q3;
    c.upOuter.tick(outer, pre, a0, a1);
    c.upInner.tick(inner, a0, q0, q1);
    c.upInner.tick(inner, a1, q2, q3);
    const double s0 = shape(c, q0, g, b, rest);
    const double s1 = shape(c, q1, g, b, rest);
    const double s2 = shape(c, q2, g, b, rest);
    const double s3 = shape(c, q3, g, b, rest);
    const double d0 = c.downInner.tick(inner, s0, s1);
    const double d1 = c.downInner.tick(inner, s2, s3);
    const double y = c.downOuter.tick(outer, d0, d1);
    // Tone, then DC blocker (the stages are asymmetric and the operating point is offset).
    const double toned = c.post.low(y, postC);
    const double blocked = toned - c.dcX + dcR * c.dcY;
    c.dcX = toned;
    c.dcY = blocked;
    // Dry, delayed to line up with the wet.
    const double d = c.dry[c.dryPos];
    c.dry[c.dryPos] = x;
    c.dryPos = c.dryPos + 1 == OS_LATENCY ? 0 : c.dryPos + 1;
    return d * (1 - mix) + blocked * mix;
  }
};

}  // namespace dsp
