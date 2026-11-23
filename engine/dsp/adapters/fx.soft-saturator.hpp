/** fx.soft-saturator as a uniform module (Module.hpp): stereo in and out, processed in place with Mix. */
#pragma once

#include "../fx/Saturator.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class FxSoftSaturator : public Wrapper<FxSoftSaturator, 2> {
 public:
  static constexpr const char* ID = "fx.soft-saturator";
  enum Param : int { Drive, Mix };
  static constexpr ParamInfo PARAMS[] = {{"drive", 0.3}, {"mix", 0.25}};
  enum Port : int { AudioIn, AudioOut, PORT_COUNT };
  static constexpr PortInfo PORTS[] = {
      {"audio-in", Direction::In, Kind::Audio, Rate::Sample, 2},
      {"audio-out", Direction::Out, Kind::Audio, Rate::Sample, 2},
  };

  void prepare(double sampleRate, int /*maxBlock*/) {
    fx.prepare(sampleRate);
    apply();
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
  void apply() noexcept { fx.setParams(values[Drive]); }

  Saturator fx;
};

}  // namespace adapter
}  // namespace dsp
