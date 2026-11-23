/**
 * mod.aftertouch as a uniform module (Module.hpp): how hard the voice's key is pressed, through the curve, Smoothing and
 * the Polarity Window, −1..1, a Mod mode source. It starts each note at no pressure and takes every pressure message
 * its Type accepts (runtime.hpp hands a voice channel pressure, and poly pressure for its own key).
 */
#pragma once

#include <algorithm>

#include "../mod/Aftertouch.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class ModAftertouch : public Wrapper<ModAftertouch, 6> {
 public:
  static constexpr const char* ID = "mod.aftertouch";
  enum Param : int { Type, Min, Max, Bend, Smoothing, Window };
  static constexpr ParamInfo PARAMS[] = {{"type", AFTERTOUCH_TYPE_DEFAULT},       {"min", AFTERTOUCH_MIN_DEFAULT},
                                         {"max", AFTERTOUCH_MAX_DEFAULT},         {"bend", AFTERTOUCH_BEND_DEFAULT},
                                         {"smoothing", AFTERTOUCH_SMOOTHING_DEFAULT}, {"window", AFTERTOUCH_WINDOW_DEFAULT}};
  enum Port : int { Out, PORT_COUNT };
  static constexpr PortInfo PORTS[] = {{"out", Direction::Out, Kind::Control, Rate::Sample, 1}};

  void prepare(double sampleRate, int /*maxBlock*/) {
    apply();
    source.prepare(sampleRate);
  }

  void reset() noexcept {
    if (takeDirty()) apply();
    source.noteOn();
  }

  void noteOn(const Note& /*note*/) noexcept {
    if (takeDirty()) apply();
    source.noteOn();
  }

  /** A pressure message (0..127), channel (poly false) or for this voice's key (poly true); taken if its Type accepts it. */
  void pressure(bool poly, double raw) noexcept {
    if (takeDirty()) apply();  // Type and the curve as set now, not at the next block
    if (source.accepts(poly)) source.press(raw);
  }

  void process(const Io& io, int offset, int n) DSP_NONBLOCKING {
    if (takeDirty()) apply();
    const float value = static_cast<float>(source.advance(n));
    if (float* out = io.channel(Out, 0)) std::fill(out + offset, out + offset + n, value);
  }

 private:
  void apply() noexcept { source.setParams(values[Type], values[Min], values[Max], values[Bend], values[Smoothing], values[Window]); }

  AftertouchSource source;
};

}  // namespace adapter
}  // namespace dsp
