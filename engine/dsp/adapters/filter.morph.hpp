/** filter.morph as a uniform module (Module.hpp): the morphing filter, mono in and out. */
#pragma once

#include "../filter/MorphFilter.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class FilterMorph : public Wrapper<FilterMorph, 4> {
 public:
  static constexpr const char* ID = "filter.morph";
  enum Param : int { Shape, Cutoff, Resonance, Level };
  static constexpr ParamInfo PARAMS[] = {{"shape", 2}, {"cutoff", 20000}, {"resonance", 0.1}, {"level", 1}};
  enum Port : int { In, Out, PORT_COUNT };
  static constexpr PortInfo PORTS[] = {
      {"in", Direction::In, Kind::Audio, Rate::Sample, 1},
      {"out", Direction::Out, Kind::Audio, Rate::Sample, 1},
  };

  void prepare(double sampleRate, int /*maxBlock*/) {
    filter.prepare(sampleRate);
    apply();
    filter.reset();
  }

  void reset() noexcept {
    if (takeDirty()) apply();
    filter.reset();
  }

  void process(const Io& io, int offset, int n) DSP_NONBLOCKING {
    float* out = io.channel(Out, 0);
    if (!out) return;
    if (takeDirty()) apply();
    copyIn(io.channel(In, 0), out, offset, n);
    filter.process(out, offset, n);
  }

 private:
  void apply() noexcept { filter.setParams(values[Shape], values[Cutoff], values[Resonance], values[Level]); }

  MorphFilter filter;
};

}  // namespace adapter
}  // namespace dsp
