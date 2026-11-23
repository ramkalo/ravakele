/** fx.distortion as a uniform module (Module.hpp): stereo in and out, in place. */
#pragma once

#include "../fx/Distortion.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class FxDistortion : public Wrapper<FxDistortion, 4> {
 public:
  static constexpr const char* ID = "fx.distortion";
  enum Param : int { Drive, Color, Bias, Mix };
  static constexpr ParamInfo PARAMS[] = {{"drive", 0.25}, {"color", 0}, {"bias", 0}, {"mix", 1}};
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
    fx.process(left, right, offset, n);
  }

 private:
  void apply() noexcept { fx.setParams(values[Drive], values[Color], values[Bias], values[Mix]); }

  Distortion fx;
};

}  // namespace adapter
}  // namespace dsp
