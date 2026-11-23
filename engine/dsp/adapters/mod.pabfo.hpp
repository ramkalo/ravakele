/**
 * mod.pabfo as a uniform module (Module.hpp): the key-tracked bass oscillator, −1..1, a Mod mode source. It keeps time
 * even with its output unwired.
 */
#pragma once

#include "../mod/Pabfo.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class ModPabfo : public Wrapper<ModPabfo, 2> {
 public:
  static constexpr const char* ID = "mod.pabfo";
  enum Param : int { Ratio, Window };
  static constexpr ParamInfo PARAMS[] = {{"ratio", PABFO_RATIO_DEFAULT}, {"window", PABFO_WINDOW_DEFAULT}};
  enum Port : int { Pitch, Out, PORT_COUNT };
  static constexpr PortInfo PORTS[] = {
      {"pitch", Direction::In, Kind::Control, Rate::Block, 1},
      {"out", Direction::Out, Kind::Control, Rate::Sample, 1},
  };

  void prepare(double sampleRate, int /*maxBlock*/) {
    pabfo.prepare(sampleRate);
    apply();
  }

  void reset() noexcept {
    if (takeDirty()) apply();
    pabfo.reset();
  }

  /** Pitch: the voice's pitch in Hz. */
  void setInput(int port, double value) noexcept {
    if (port == Pitch) pabfo.setPitch(value);
  }

  void noteOn(const Note& /*note*/) noexcept { pabfo.noteOn(); }

  /** The PABFO's phase, 0..1. */
  double phase() const noexcept { return pabfo.currentPhase(); }

  void process(const Io& io, int offset, int n) DSP_NONBLOCKING {
    if (takeDirty()) apply();
    if (float* out = io.channel(Out, 0)) pabfo.process(out, offset, n);
    else pabfo.advance(n);
  }

 private:
  void apply() noexcept { pabfo.setParams(values[Ratio], values[Window]); }

  Pabfo pabfo;
};

}  // namespace adapter
}  // namespace dsp
