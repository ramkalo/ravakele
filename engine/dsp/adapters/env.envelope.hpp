/** env.envelope as a uniform module (Module.hpp): the DAHDSR envelope, holding its voice until its release ends. */
#pragma once

#include "../env/Envelope.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class EnvEnvelope : public Wrapper<EnvEnvelope, 11> {
 public:
  static constexpr const char* ID = "env.envelope";
  enum Param : int { Delay, Attack, Hold, Decay, Sustain, Release, AttackCurve, DecayCurve, ReleaseCurve, SustainCurve, Intensity };
  static constexpr ParamInfo PARAMS[] = {{"delay", 0.001},  {"attack", 0.005},  {"hold", 0.001},       {"decay", 0.25},
                                         {"sustain", 0.7},   {"release", 0.3},   {"attackCurve", 0},     {"decayCurve", 0},
                                         {"releaseCurve", 0}, {"sustainCurve", 0}, {"intensity", 1}};
  enum Port : int { Out, PORT_COUNT };
  static constexpr PortInfo PORTS[] = {{"out", Direction::Out, Kind::Control, Rate::Sample, 1}};

  void prepare(double sampleRate, int /*maxBlock*/) {
    env.prepare(sampleRate);
    apply();
  }

  void reset() noexcept {
    if (takeDirty()) apply();
    env.reset();
  }

  void noteOn(const Note& /*note*/) noexcept { env.noteOn(); }
  void noteOff() noexcept { env.noteOff(); }
  bool finished() const noexcept { return env.isFinished(); }

  /** The stage running (EgStage: 0 idle … 6 release) and how far through it, 0..1, for a pad that shows it. */
  int stage() const noexcept { return env.currentStage(); }
  double stageProgress() const noexcept { return env.stageProgress(); }

  void process(const Io& io, int offset, int n) DSP_NONBLOCKING {
    if (takeDirty()) apply();
    if (float* out = io.channel(Out, 0)) env.process(out, offset, n);
    else env.advance(n);
  }

 private:
  void apply() noexcept {
    env.setParams(values[Delay], values[Attack], values[Hold], values[Decay], values[Sustain], values[Release],
                  values[AttackCurve], values[DecayCurve], values[ReleaseCurve], values[SustainCurve], values[Intensity]);
  }

  Envelope env;
};

}  // namespace adapter
}  // namespace dsp
