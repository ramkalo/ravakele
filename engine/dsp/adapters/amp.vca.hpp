/** amp.vca as a uniform module (Module.hpp): stereo audio in and out, times Gain times its control signal, balanced by Pan. */
#pragma once

#include "../amp/Vca.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class AmpVca : public Wrapper<AmpVca, 3> {
 public:
  static constexpr const char* ID = "amp.vca";
  enum Param : int { Gain, Response, Pan };
  static constexpr ParamInfo PARAMS[] = {{"gain", 0.8}, {"response", 0.5}, {"pan", 0}};
  enum Port : int { AudioIn, Cv, AudioOut, PORT_COUNT };
  static constexpr PortInfo PORTS[] = {
      {"audio-in", Direction::In, Kind::Audio, Rate::Sample, 2},
      {"cv", Direction::In, Kind::Control, Rate::Sample, 1},
      {"audio-out", Direction::Out, Kind::Audio, Rate::Sample, 2},
  };

  void prepare(double sampleRate, int /*maxBlock*/) {
    vca.prepare(sampleRate);
    apply();
    vca.reset();
  }

  void reset() noexcept {
    if (takeDirty()) apply();
    vca.reset();
  }

  void process(const Io& io, int offset, int n) DSP_NONBLOCKING {
    float* left = io.channel(AudioOut, 0);
    float* right = io.channel(AudioOut, 1);
    if (!left || !right) return;
    if (takeDirty()) apply();
    copyIn(io.channel(AudioIn, 0), left, offset, n);
    copyIn(io.channel(AudioIn, 1), right, offset, n);
    vca.process(left, right, io.channel(Cv, 0), offset, n);
  }

 private:
  void apply() noexcept { vca.setParams(values[Gain], values[Response], values[Pan]); }

  Vca vca;
};

}  // namespace adapter
}  // namespace dsp
