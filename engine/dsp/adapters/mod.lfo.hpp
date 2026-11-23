/** mod.lfo as a uniform module (Module.hpp): the LFO, −1..1, which keeps time even with its output unwired; its Tempo input is the synth's tempo. */
#pragma once

#include "../clock/Tempo.hpp"
#include "../mod/Lfo.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class ModLfo : public Wrapper<ModLfo, 7> {
 public:
  static constexpr const char* ID = "mod.lfo";
  enum Param : int { RateHz, Shape, Level, Window, Attack, Retrigger, Sync };
  static constexpr ParamInfo PARAMS[] = {{"rate", 1},   {"shape", 0.3333333333333333}, {"level", 1},     {"window", 0.5},
                                         {"attack", 0.001}, {"retrigger", 0},          {"sync", 0}};
  enum Port : int { Out, TempoIn, PORT_COUNT };
  static constexpr PortInfo PORTS[] = {
      {"out", Direction::Out, Kind::Control, Rate::Sample, 1},
      {"tempo", Direction::In, Kind::Clock, Rate::Block, 1},
  };

  void prepare(double sampleRate, int /*maxBlock*/) {
    lfo.prepare(sampleRate);
    apply();
  }

  void reset() noexcept {
    if (takeDirty()) apply();
    lfo.reset();
  }

  /** Tempo: the synth's BPM (clock.tempo). */
  void setInput(int port, double value) noexcept {
    if (port != TempoIn || value == tempo) return;
    tempo = value;
    dirty = true;
  }

  void noteOn(const Note& /*note*/) noexcept { lfo.noteOn(); }

  /** The LFO's phase, 0..1, for a pad that shows it. */
  double phase() const noexcept { return lfo.currentPhase(); }

  void process(const Io& io, int offset, int n) DSP_NONBLOCKING {
    if (takeDirty()) apply();
    if (float* out = io.channel(Out, 0)) lfo.process(out, offset, n);
    else lfo.advance(n);
  }

 private:
  void apply() noexcept {
    lfo.setParams(values[RateHz], values[Shape], values[Level], values[Attack], values[Window], values[Retrigger], values[Sync], tempo);
  }

  Lfo lfo;
  double tempo = TEMPO_DEFAULT_BPM;
};

}  // namespace adapter
}  // namespace dsp
