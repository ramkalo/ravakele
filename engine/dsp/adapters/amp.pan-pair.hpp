/** amp.pan-pair as a uniform module (Module.hpp): two stereo pathways in and out, each balanced by its own pan; an unwired output goes to scratch. */
#pragma once

#include <vector>

#include "../amp/PanPair.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class AmpPanPair : public Wrapper<AmpPanPair, 2> {
 public:
  static constexpr const char* ID = "amp.pan-pair";
  enum Param : int { PanA, PanB };
  static constexpr ParamInfo PARAMS[] = {{"panA", -0.5}, {"panB", 0.5}};
  enum Port : int { AIn, BIn, AOut, BOut, PORT_COUNT };
  static constexpr PortInfo PORTS[] = {
      {"a-in", Direction::In, Kind::Audio, Rate::Sample, 2},
      {"b-in", Direction::In, Kind::Audio, Rate::Sample, 2},
      {"a-out", Direction::Out, Kind::Audio, Rate::Sample, 2},
      {"b-out", Direction::Out, Kind::Audio, Rate::Sample, 2},
  };

  void prepare(double sampleRate, int maxBlock) {
    block = maxBlock;
    // Two channels of scratch per output, so one pathway plays with the other's output unwired.
    scratch.assign(static_cast<std::size_t>(4 * maxBlock), 0.0f);
    pair.prepare(sampleRate);
    apply();
    pair.reset();
  }

  void reset() noexcept {
    if (takeDirty()) apply();
    pair.reset();
  }

  void process(const Io& io, int offset, int n) DSP_NONBLOCKING {
    if (!io.wired(AOut) && !io.wired(BOut)) return;
    if (takeDirty()) apply();
    float* aLeft = out(io, AOut, 0);
    float* aRight = out(io, AOut, 1);
    float* bLeft = out(io, BOut, 0);
    float* bRight = out(io, BOut, 1);
    copyIn(io.channel(AIn, 0), aLeft, offset, n);
    copyIn(io.channel(AIn, 1), aRight, offset, n);
    copyIn(io.channel(BIn, 0), bLeft, offset, n);
    copyIn(io.channel(BIn, 1), bRight, offset, n);
    pair.process(aLeft, aRight, bLeft, bRight, offset, n);
  }

 private:
  void apply() noexcept { pair.setParams(values[PanA], values[PanB]); }

  /** An output channel: the wired buffer, else this one's scratch. */
  float* out(const Io& io, int port, int ch) noexcept {
    float* wired = io.channel(port, ch);
    return wired ? wired : scratch.data() + (2 * (port - AOut) + ch) * block;
  }

  PanPair pair;
  std::vector<float> scratch;
  int block = 0;
};

}  // namespace adapter
}  // namespace dsp
