/**
 * util.waterfall as a uniform module (Module.hpp): stereo audio in, the same audio at Thru. It has no params. The
 * engine keeps the newest samples of its Thru output for the waterfall graph that shows it (src/shared/pad-watch.js).
 */
#pragma once

#include <array>

#include "../util/Thru.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class UtilWaterfall : public Wrapper<UtilWaterfall, 0> {
 public:
  static constexpr const char* ID = "util.waterfall";
  static constexpr std::array<ParamInfo, 0> PARAMS = {};
  enum Port : int { AudioIn, ThruOut, PORT_COUNT };
  static constexpr PortInfo PORTS[] = {
      {"audio-in", Direction::In, Kind::Audio, Rate::Sample, 2},
      {"thru", Direction::Out, Kind::Audio, Rate::Sample, 2},
  };

  void prepare(double /*sampleRate*/, int /*maxBlock*/) {}
  void reset() noexcept {}

  void process(const Io& io, int offset, int n) DSP_NONBLOCKING {
    for (int ch = 0; ch < 2; ch++)
      if (float* out = io.channel(ThruOut, ch)) thru.process(io.channel(AudioIn, ch), out, offset, n);
  }

 private:
  Thru thru;
};

}  // namespace adapter
}  // namespace dsp
