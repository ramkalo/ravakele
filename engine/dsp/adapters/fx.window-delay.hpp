/** fx.window-delay as a uniform module (Module.hpp): stereo in and out, in place with Mix; its Tempo input is the synth's tempo. */
#pragma once

#include "../clock/Tempo.hpp"
#include "../fx/WindowDelay.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class FxWindowDelay : public Wrapper<FxWindowDelay, 7> {
 public:
  static constexpr const char* ID = "fx.window-delay";
  enum Param : int { Time, Sync, Feedback, Center, Width, Side, Mix };
  static constexpr ParamInfo PARAMS[] = {{"time", 0.375}, {"sync", 0}, {"feedback", 0.4}, {"center", 0},
                                         {"width", 1},    {"side", 0}, {"mix", 0.35}};
  enum Port : int { AudioIn, AudioOut, TempoIn, PORT_COUNT };
  static constexpr PortInfo PORTS[] = {
      {"audio-in", Direction::In, Kind::Audio, Rate::Sample, 2},
      {"audio-out", Direction::Out, Kind::Audio, Rate::Sample, 2},
      {"tempo", Direction::In, Kind::Clock, Rate::Block, 1},
  };

  void prepare(double sampleRate, int /*maxBlock*/) {
    fx.prepare(sampleRate);
    apply();
  }

  void reset() noexcept {
    if (takeDirty()) apply();
    fx.reset();
  }

  /** Tempo: the synth's BPM (clock.tempo). */
  void setInput(int port, double value) noexcept {
    if (port != TempoIn || value == tempo) return;
    tempo = value;
    dirty = true;
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
  void apply() noexcept {
    const double side = values[Center] < 0 ? 0 : 1;  // Side follows Center (the manifest's `follows`); values[Side] is never read
    fx.setParams(values[Time], values[Sync], values[Feedback], values[Center], values[Width], side, tempo);
  }

  WindowDelay fx;
  double tempo = TEMPO_DEFAULT_BPM;
};

}  // namespace adapter
}  // namespace dsp
