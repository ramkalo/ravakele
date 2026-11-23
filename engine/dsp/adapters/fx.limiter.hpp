/** fx.limiter as a uniform module (Module.hpp): stereo in and out, processed in place, LIMITER_LOOKAHEAD_MS late. */
#pragma once

#include "../fx/Limiter.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class FxLimiter : public Wrapper<FxLimiter, 3> {
 public:
  static constexpr const char* ID = "fx.limiter";
  enum Param : int { Input, Ceiling, Release };
  static constexpr ParamInfo PARAMS[] = {{"input", 0}, {"ceiling", -0.3}, {"release", 100}};
  enum Port : int { AudioIn, AudioOut, PORT_COUNT };
  static constexpr PortInfo PORTS[] = {
      {"audio-in", Direction::In, Kind::Audio, Rate::Sample, 2},
      {"audio-out", Direction::Out, Kind::Audio, Rate::Sample, 2},
  };

  void prepare(double sampleRate, int /*maxBlock*/) {
    apply();
    fx.prepare(sampleRate);
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
  void apply() noexcept { fx.setParams(values[Input], values[Ceiling], values[Release]); }

  Limiter fx;
};

}  // namespace adapter
}  // namespace dsp
