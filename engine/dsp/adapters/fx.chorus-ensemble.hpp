/** fx.chorus-ensemble as a uniform module (Module.hpp): stereo in and out, processed in place with Mix; two taps swayed by one LFO, panned apart by Width. */
#pragma once

#include <cmath>

#include "../fx/Chorus.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class FxChorusEnsemble : public Wrapper<FxChorusEnsemble, 6> {
 public:
  static constexpr const char* ID = "fx.chorus-ensemble";
  enum Param : int { Mix, SpeedHz, Depth, Width, Shape, WanderHz };
  static constexpr ParamInfo PARAMS[] = {{"mix", 0.25}, {"speed", 0.8}, {"depth", 0.5}, {"width", 1}, {"shape", 0}, {"wander", 0}};
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
  void apply() noexcept { fx.setParams(values[SpeedHz], values[Depth], values[Width], static_cast<int>(std::lround(values[Shape])), values[WanderHz]); }

  Chorus fx;
};

}  // namespace adapter
}  // namespace dsp
