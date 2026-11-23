/** fx.warp-chorus as a uniform module (Module.hpp): stereo in and out, processed in place with Mix. */
#pragma once

#include "../fx/WarpChorus.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class FxWarpChorus : public Wrapper<FxWarpChorus, 6> {
 public:
  static constexpr const char* ID = "fx.warp-chorus";
  enum Param : int { Wander, Depth, Slip, Coupling, Spread, Mix };
  static constexpr ParamInfo PARAMS[] = {{"wander", 0.5}, {"depth", 0.5}, {"slip", 0.2}, {"coupling", 0.25}, {"spread", 0.7}, {"mix", 0.3}};
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
  void apply() noexcept { fx.setParams(values[Wander], values[Depth], values[Slip], values[Coupling], values[Spread]); }

  WarpChorus fx;
};

}  // namespace adapter
}  // namespace dsp
