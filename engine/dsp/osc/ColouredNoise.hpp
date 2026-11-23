/**
 * Coloured noise.
 *
 * Colour — 0..4, continuous: Brown (−6 dB/oct) → Pink (−3) → White → Blue (+3) → Violet (+6). Every colour's filter
 *          runs on every sample and the output is a crossfade of the two either side of the setting, so a sweep is
 *          smooth and never switches. (Red is Brown by another name.)
 * Level  — a plain gain on the output, 0 (silent) to 1, each colour's own calibrated loudness.
 *
 * All colours derive from one seeded white source (util/random.hpp), so renders are reproducible and the spectra are
 * testable.
 */
#pragma once

#include <algorithm>
#include <cmath>

#include "../util/math.hpp"
#include "../util/random.hpp"

namespace dsp {

namespace NoiseColour {
enum : int { Brown = 0, Pink = 1, White = 2, Blue = 3, Violet = 4 };
}
constexpr const char* NOISE_COLOUR_LABELS[] = {"Brown", "Pink", "White", "Blue", "Violet"};

class ColouredNoise {
 public:
  /** The colours need no sample rate; `seed` picks the white source's sequence. */
  void prepare(double /*sampleRate*/, double seed = 1) {
    this->seed = seed;
    reset();
  }

  /** Back to the start of the seed's sequence, filters at rest. */
  void reset() noexcept {
    rng.reset(seed);
    p0 = 0; p1 = 0; p2 = 0;
    brown = 0; lastWhite = 0; lastBlue = 0;
    std::fill(std::begin(out), std::end(out), 0.0f);
  }

  /** colour 0..4 (a fraction crossfades two neighbours), level 0..1. Block-rate. */
  void setParams(double colour, double level) noexcept {
    if (!(colour > 0)) colour = 0;
    else if (colour > LAST_COLOUR) colour = LAST_COLOUR;
    colourA = static_cast<int>(std::floor(colour));
    colourB = colourA < LAST_COLOUR ? colourA + 1 : colourA;
    mix = colour - colourA;
    this->level = clamp(level, 0, 1);
  }

  /** Writes n samples from offset (overwriting). */
  void process(float* output, int offset, int n) DSP_NONBLOCKING {
    for (int i = 0; i < n; i++) output[offset + i] = static_cast<float>(tick());
  }

  double tick() noexcept {
    const double w = rng.next();
    float* o = out;

    p0 = 0.99765 * p0 + w * 0.0990460;
    p1 = 0.96300 * p1 + w * 0.2965164;
    p2 = 0.57000 * p2 + w * 1.0526913;
    o[NoiseColour::Pink] = static_cast<float>((p0 + p1 + p2 + w * 0.1848) * 0.3);

    brown = (brown + 0.02 * w) / 1.02;
    o[NoiseColour::Brown] = static_cast<float>(brown * 3.5);

    const double b = w - lastWhite;
    lastWhite = w;
    o[NoiseColour::Blue] = static_cast<float>(b * 0.5);
    o[NoiseColour::Violet] = static_cast<float>((b - lastBlue) * 0.25);
    lastBlue = b;

    o[NoiseColour::White] = static_cast<float>(w);

    const double a = o[colourA];
    return (mix > 0 ? a + (o[colourB] - a) * mix : a) * level;
  }

 private:
  static constexpr int LAST_COLOUR = NoiseColour::Violet;

  Rng rng{1};
  double seed = 1;
  /** The two colours being blended and how far from the first to the second. */
  int colourA = NoiseColour::White;
  int colourB = NoiseColour::White;
  double mix = 0;
  double level = 1;

  // Pink: Paul Kellet's economy three-pole approximation of -3 dB/oct.
  double p0 = 0;
  double p1 = 0;
  double p2 = 0;
  // Brown: leaky integrator.
  double brown = 0;
  // Blue/Violet: successive differences of white.
  double lastWhite = 0;
  double lastBlue = 0;

  /** This sample's value of every colour, indexed by NoiseColour. */
  float out[LAST_COLOUR + 1] = {};
};

}  // namespace dsp
