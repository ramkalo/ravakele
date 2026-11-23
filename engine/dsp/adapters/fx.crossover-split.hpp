/** fx.crossover-split as a uniform module (Module.hpp): stereo in, three stereo outputs; unwired ones go to scratch. */
#pragma once

#include <vector>

#include "../fx/CrossoverSplit.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class FxCrossoverSplit : public Wrapper<FxCrossoverSplit, 2> {
 public:
  static constexpr const char* ID = "fx.crossover-split";
  enum Param : int { Crossover, Route };
  static constexpr ParamInfo PARAMS[] = {{"crossover", 150}, {"route", 0}};
  enum Port : int { AudioIn, HighOut, LowAOut, LowBOut, PORT_COUNT };
  static constexpr PortInfo PORTS[] = {
      {"audio-in", Direction::In, Kind::Audio, Rate::Sample, 2},
      {"high-out", Direction::Out, Kind::Audio, Rate::Sample, 2},
      {"low-a-out", Direction::Out, Kind::Audio, Rate::Sample, 2},
      {"low-b-out", Direction::Out, Kind::Audio, Rate::Sample, 2},
  };

  void prepare(double sampleRate, int maxBlock) {
    block = maxBlock;
    // Two channels of scratch per output, and two of silence for an unwired input.
    scratch.assign(static_cast<std::size_t>(8 * maxBlock), 0.0f);
    fx.prepare(sampleRate);
    apply();
  }

  void reset() noexcept {
    if (takeDirty()) apply();
    fx.reset();
  }

  void process(const Io& io, int offset, int n) DSP_NONBLOCKING {
    if (!io.wired(HighOut) && !io.wired(LowAOut) && !io.wired(LowBOut)) return;
    if (takeDirty()) apply();
    float* silence = scratch.data() + 6 * block;
    std::fill(silence + offset, silence + offset + n, 0.0f);
    std::fill(silence + block + offset, silence + block + offset + n, 0.0f);
    const float* inL = io.channel(AudioIn, 0);
    const float* inR = io.channel(AudioIn, 1);
    fx.process(inL ? inL : silence, inR ? inR : silence + block, out(io, HighOut, 0), out(io, HighOut, 1), out(io, LowAOut, 0),
               out(io, LowAOut, 1), out(io, LowBOut, 0), out(io, LowBOut, 1), offset, n);
  }

 private:
  void apply() noexcept { fx.setParams(values[Crossover], values[Route]); }

  float* out(const Io& io, int port, int ch) noexcept {
    float* wired = io.channel(port, ch);
    return wired ? wired : scratch.data() + (2 * (port - HighOut) + ch) * block;
  }

  CrossoverSplit fx;
  std::vector<float> scratch;
  int block = 0;
};

}  // namespace adapter
}  // namespace dsp
