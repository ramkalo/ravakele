/** noise.coloured-noise as a uniform module (Module.hpp): noise from brown to violet. */
#pragma once

#include "../osc/ColouredNoise.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class NoiseColouredNoise : public Wrapper<NoiseColouredNoise, 2> {
 public:
  static constexpr const char* ID = "noise.coloured-noise";
  enum Param : int { Colour, Level };
  static constexpr ParamInfo PARAMS[] = {{"colour", 2}, {"level", 0}};
  enum Port : int { Out, PORT_COUNT };
  static constexpr PortInfo PORTS[] = {{"out", Direction::Out, Kind::Audio, Rate::Sample, 1}};

  void prepare(double sampleRate, int /*maxBlock*/) {
    noise.prepare(sampleRate);
    apply();
  }

  void reset() noexcept {
    if (takeDirty()) apply();
    noise.reset();
  }

  void process(const Io& io, int offset, int n) DSP_NONBLOCKING {
    float* out = io.channel(Out, 0);
    if (!out) return;
    if (takeDirty()) apply();
    noise.process(out, offset, n);
  }

 private:
  void apply() noexcept { noise.setParams(values[Colour], values[Level]); }

  ColouredNoise noise;
};

}  // namespace adapter
}  // namespace dsp
