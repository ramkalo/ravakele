/**
 * mod.voice-values as a uniform module (Module.hpp): the round robin, 0..1, held for each note. The host numbers the
 * notes and tells the voice each one's number (setStep, from Voice::setStep); the out port carries value(that number)
 * until the next.
 */
#pragma once

#include "../mod/VoiceValues.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class ModVoiceValues : public Wrapper<ModVoiceValues, 21> {
 public:
  static constexpr const char* ID = "mod.voice-values";
  enum Param : int { Mode, V1, V16 = V1 + MAX_VOICES - 1, Start, End, Bend, Steps };
  static constexpr ParamInfo PARAMS[] = {
      {"mode", 0},       {"v1", 0},         {"v2", 0.0667},  {"v3", 0.1333}, {"v4", 0.2},    {"v5", 0.2667},
      {"v6", 0.3333},    {"v7", 0.4},       {"v8", 0.4667},  {"v9", 0.5333}, {"v10", 0.6},   {"v11", 0.6667},
      {"v12", 0.7333},   {"v13", 0.8},      {"v14", 0.8667}, {"v15", 0.9333}, {"v16", 1},     {"start", 0},
      {"end", 1},        {"bend", 0},       {"steps", 16}};
  enum Port : int { Value, PORT_COUNT };
  static constexpr PortInfo PORTS[] = {{"value", Direction::Out, Kind::Control, Rate::Sample, 1}};

  void prepare(double /*sampleRate*/, int /*maxBlock*/) { apply(); }

  void reset() noexcept {
    if (takeDirty()) apply();
    step = 0;
  }

  /** The note this voice now plays is the host's `n`-th (n ≥ 0); its value holds until the next. */
  void setStep(long long n) noexcept { step = n < 0 ? 0 : n; }

  /** The value this voice holds now, 0..1. */
  double current() const noexcept { return values_.value(step); }

  void process(const Io& io, int offset, int n) DSP_NONBLOCKING {
    if (takeDirty()) apply();
    if (float* out = io.channel(Value, 0)) std::fill(out + offset, out + offset + n, static_cast<float>(current()));
  }

 private:
  void apply() noexcept {
    values_.setMode(values[Mode]);
    for (int s = 0; s < MAX_VOICES; s++) values_.setValue(s, values[V1 + s]);
    values_.setCurve(values[Start], values[End], values[Bend]);
    values_.setSteps(values[Steps]);
  }

  VoiceValues values_;
  long long step = 0;
};

}  // namespace adapter
}  // namespace dsp
