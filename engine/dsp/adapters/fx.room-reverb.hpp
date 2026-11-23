/** fx.room-reverb as a uniform module (Module.hpp): stereo in and out, processed in place with Mix. */
#pragma once

#include "../fx/RoomReverb.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class FxRoomReverb : public Wrapper<FxRoomReverb, 5> {
 public:
  static constexpr const char* ID = "fx.room-reverb";
  enum Param : int { Size, Length, Color, Crossover, Mix };
  static constexpr ParamInfo PARAMS[] = {{"size", 8}, {"length", 1.5}, {"color", 0}, {"crossover", 120}, {"mix", 0.25}};
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
  void apply() noexcept { fx.setParams(values[Size], values[Length], values[Color], values[Crossover]); }

  RoomReverb fx;
};

}  // namespace adapter
}  // namespace dsp
