/**
 * Band-limited wavetables.
 *
 * One table per octave ("mip"), each containing only the harmonics that stay
 * below Nyquist for that octave, so a saw played high sounds like a saw rather
 * than like a bag of aliases.
 *
 * Only saw and triangle need tables. Sine never aliases, so it gets a single
 * plain table; and the pulse wave is built from two saw reads, which means pulse
 * width can be swept continuously and stays band-limited for free.
 *
 * Built once per sample rate (wavetableBank caches it, so every oscillator at
 * that rate shares one) using a precomputed sine table rather than std::sin per
 * sample — roughly two million lookups instead of two million transcendental
 * calls, which keeps startup imperceptible.
 */
#pragma once

#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "../util/math.hpp"

namespace dsp {

constexpr int TABLE_SIZE = 2048;
constexpr int TABLE_MASK = TABLE_SIZE - 1;
/** Octave bands from ~20 Hz upward. */
constexpr int NUM_MIPS = 11;

namespace wavetable_detail {

constexpr double BASE_F0 = 20;
/** Stride between mips, including one guard sample for interpolation. */
constexpr int STRIDE = TABLE_SIZE + 1;

inline void normalize(std::vector<float>& out, int at, double peak) {
  const double g = peak > 1e-9 ? 1 / peak : 1;
  for (int i = 0; i < TABLE_SIZE; i++) out[at + i] = static_cast<float>(out[at + i] * g);
  out[at + TABLE_SIZE] = out[at];  // guard sample closes the loop
}

inline void buildSaw(std::vector<float>& out, int at, const std::vector<float>& sin, int maxH) {
  double peak = 0;
  for (int i = 0; i < TABLE_SIZE; i++) {
    double v = 0;
    for (int h = 1; h <= maxH; h++) v += sin[(h * i) & TABLE_MASK] / static_cast<double>(h);
    out[at + i] = static_cast<float>(v);
    const double a = v < 0 ? -v : v;
    if (a > peak) peak = a;
  }
  normalize(out, at, peak);
}

inline void buildTri(std::vector<float>& out, int at, const std::vector<float>& sin, int maxH) {
  double peak = 0;
  for (int i = 0; i < TABLE_SIZE; i++) {
    double v = 0;
    double sign = 1;
    for (int h = 1; h <= maxH; h += 2) {
      v += (sign * sin[(h * i) & TABLE_MASK]) / (static_cast<double>(h) * h);
      sign = -sign;
    }
    out[at + i] = static_cast<float>(v);
    const double a = v < 0 ? -v : v;
    if (a > peak) peak = a;
  }
  normalize(out, at, peak);
}

}  // namespace wavetable_detail

class WavetableBank {
 public:
  std::vector<float> saw = std::vector<float>(wavetable_detail::STRIDE * NUM_MIPS);
  std::vector<float> tri = std::vector<float>(wavetable_detail::STRIDE * NUM_MIPS);
  std::vector<float> sine = std::vector<float>(wavetable_detail::STRIDE);

  void prepare(double sampleRate) {
    using namespace wavetable_detail;
    this->sampleRate = sampleRate;
    std::vector<float> sin(TABLE_SIZE);
    for (int i = 0; i < TABLE_SIZE; i++) sin[i] = static_cast<float>(std::sin((2 * PI * i) / TABLE_SIZE));

    for (int i = 0; i < TABLE_SIZE; i++) sine[i] = sin[i];
    sine[TABLE_SIZE] = sine[0];

    const double nyquist = sampleRate * 0.5;
    for (int m = 0; m < NUM_MIPS; m++) {
      // Harmonics must stay clean up to the top of the octave this mip covers.
      const double topF0 = BASE_F0 * std::pow(2.0, m + 1);
      int maxH = static_cast<int>(std::floor(nyquist / topF0));
      if (maxH < 1) maxH = 1;
      buildSaw(saw, m * STRIDE, sin, maxH);
      buildTri(tri, m * STRIDE, sin, maxH);
    }
  }

  /** Mip index for a fundamental frequency. */
  int mipFor(double freqHz) const noexcept {
    if (!(freqHz > wavetable_detail::BASE_F0)) return 0;
    const double m = std::floor(std::log2(freqHz / wavetable_detail::BASE_F0));
    return m < 0 ? 0 : m >= NUM_MIPS ? NUM_MIPS - 1 : static_cast<int>(m);
  }

  int mipOffset(int mip) const noexcept { return mip * wavetable_detail::STRIDE; }

  double getSampleRate() const noexcept { return sampleRate; }

 private:
  double sampleRate = 48000;
};

/** The bank for a sample rate, built on first use and shared after that. Call from prepare(), never the audio path. */
inline const WavetableBank& wavetableBank(double sampleRate) {
  static std::mutex lock;
  static std::map<double, std::unique_ptr<WavetableBank>> banks;
  std::lock_guard<std::mutex> guard(lock);
  std::unique_ptr<WavetableBank>& bank = banks[sampleRate];
  if (!bank) {
    bank = std::make_unique<WavetableBank>();
    bank->prepare(sampleRate);
  }
  return *bank;
}

/** Linear-interpolated read. `phase` is 0..1. */
inline double readTable(const std::vector<float>& table, int offset, double phase) noexcept {
  const double x = phase * TABLE_SIZE;
  const int i = static_cast<int>(x);
  const double f = x - i;
  const double a = table[offset + (i & TABLE_MASK)];
  const double b = table[offset + ((i + 1) & TABLE_MASK)];
  return a + (b - a) * f;
}

}  // namespace dsp
