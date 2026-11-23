/** fx.rotary-speaker as a uniform module (Module.hpp): stereo in and out, processed in place with Mix. */
#pragma once

#include "../fx/RotarySpeaker.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class FxRotarySpeaker : public Wrapper<FxRotarySpeaker, 8> {
 public:
  static constexpr const char* ID = "fx.rotary-speaker";
  enum Param : int { Mix, HighRate, HighWidth, LowRate, LowWidth, Crossover, FlipLow, FlipHigh };
  static constexpr ParamInfo PARAMS[] = {{"mix", 0.8},      {"highRate", 0.8},  {"highWidth", 0.7}, {"lowRate", 0.7},
                                         {"lowWidth", 0.5}, {"crossover", 800}, {"flipLow", 0},     {"flipHigh", 0}};
  enum Port : int { AudioIn, AudioOut, PORT_COUNT };
  static constexpr PortInfo PORTS[] = {
      {"audio-in", Direction::In, Kind::Audio, Rate::Sample, 2},
      {"audio-out", Direction::Out, Kind::Audio, Rate::Sample, 2},
  };

  void prepare(double sampleRate, int /*maxBlock*/) {
    fx.prepare(sampleRate);
    apply();
    fx.reset();
  }

  void reset() noexcept {
    if (takeDirty()) apply();
    fx.reset();
  }

  void process(const Io& io, int offset, int n) DSP_NONBLOCKING {
    float* left = io.channel(AudioOut, 0);
    float* right = io.channel(AudioOut, 1);
    if (!left || !right) return;
    if (takeDirty()) apply();
    copyIn(io.channel(AudioIn, 0), left, offset, n);
    copyIn(io.channel(AudioIn, 1), right, offset, n);
    fx.process(left, right, offset, n, values[Mix]);
  }

 private:
  void apply() noexcept {
    fx.setParams(values[HighRate], values[HighWidth], values[LowRate], values[LowWidth], values[Crossover], values[FlipLow], values[FlipHigh]);
  }

  RotarySpeaker fx;
};

}  // namespace adapter
}  // namespace dsp
