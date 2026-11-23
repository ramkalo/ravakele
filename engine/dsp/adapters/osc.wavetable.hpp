/**
 * osc.wavetable as a uniform module (Module.hpp): the wavetable oscillator, played at its Pitch input, as up to
 * UNISON_MAX detuned copies summed into its output (setUnison, UnisonCopies.hpp; one copy unless voice.unison says more).
 */
#pragma once

#include "../osc/WavetableOscillator.hpp"
#include "Module.hpp"
#include "UnisonCopies.hpp"

namespace dsp {
namespace adapter {

class OscWavetable : public Wrapper<OscWavetable, 7> {
 public:
  static constexpr const char* ID = "osc.wavetable";
  enum Param : int { Shape, Width, Octave, Semitones, Cents, Level, PhaseReset };
  static constexpr ParamInfo PARAMS[] = {{"shape", 1},     {"width", 0.5}, {"octave", 0},    {"semitones", 0},
                                         {"cents", 0},     {"level", 1},   {"phaseReset", 2}};
  enum Port : int { Pitch, Out, PORT_COUNT };
  static constexpr PortInfo PORTS[] = {
      {"pitch", Direction::In, Kind::Control, Rate::Block, 1},
      {"out", Direction::Out, Kind::Audio, Rate::Sample, 1},
  };

  void prepare(double sampleRate, int maxBlock) {
    for (WavetableOscillator& osc : oscs) osc.prepare(sampleRate);
    unison.prepare(maxBlock);
    apply();
  }

  void reset() noexcept {
    if (takeDirty()) apply();
    for (int i = 0; i < UNISON_MAX; i++) {
      oscs[i].reset();
      oscs[i].setPhase(unison.startPhase(i));
    }
  }

  /** Pitch: the played pitch in Hz. */
  void setInput(int port, double value) noexcept {
    if (port != Pitch) return;
    pitchHz = value;
    for (int i = 0; i < UNISON_MAX; i++) oscs[i].setPitch(unison.pitch(i, pitchHz));
  }

  void noteOn(const Note& note) noexcept {
    for (int i = 0; i < UNISON_MAX; i++) oscs[i].noteOn(note.sounding, unison.startPhase(i));
  }

  /** The unison copies (voice.unison): how many, each one's cents, and their sum's gain. */
  void setUnison(int count, const double* cents, double gain) noexcept {
    unison.set(count, cents, gain);
    for (int i = 0; i < UNISON_MAX; i++) oscs[i].setPitch(unison.pitch(i, pitchHz));
  }

  void process(const Io& io, int offset, int n) DSP_NONBLOCKING {
    float* out = io.channel(Out, 0);
    if (!out) return;
    if (takeDirty()) apply();
    unison.play(out, offset, n, [this](int i, float* buffer, int at, int count) { oscs[i].process(buffer, at, count); });
  }

 private:
  void apply() noexcept {
    for (WavetableOscillator& osc : oscs)
      osc.setParams(values[Shape], values[Octave], values[Semitones], values[Cents], values[Width], values[Level], values[PhaseReset]);
  }

  WavetableOscillator oscs[UNISON_MAX];  // copy 0 is the oscillator itself; the rest play only with unison
  UnisonCopies unison;
  double pitchHz = 440;  // the oscillator's own until the Pitch input sets it
};

}  // namespace adapter
}  // namespace dsp
