/**
 * mod.keytrack as a uniform module (Module.hpp): the key a note was played on, across the whole keyboard through the curve and the Polarity
 * Window, −1..1, a Mod mode source. It takes the key at each note-on and holds it for the note.
 */
#pragma once

#include <algorithm>

#include "../mod/Keytrack.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class ModKeytrack : public Wrapper<ModKeytrack, 4> {
 public:
  static constexpr const char* ID = "mod.keytrack";
  enum Param : int { Min, Max, Bend, Window };
  static constexpr ParamInfo PARAMS[] = {
      {"min", KEYTRACK_MIN_DEFAULT}, {"max", KEYTRACK_MAX_DEFAULT}, {"bend", KEYTRACK_BEND_DEFAULT}, {"window", KEYTRACK_WINDOW_DEFAULT}};
  enum Port : int { Out, PORT_COUNT };
  static constexpr PortInfo PORTS[] = {{"out", Direction::Out, Kind::Control, Rate::Sample, 1}};

  void prepare(double /*sampleRate*/, int /*maxBlock*/) { apply(); }

  void reset() noexcept {
    if (takeDirty()) apply();
  }

  void noteOn(const Note& note) noexcept { keytrack.noteOn(note.note); }

  /** The value this voice holds now, −1..1. */
  double current() const noexcept { return keytrack.value(); }

  void process(const Io& io, int offset, int n) DSP_NONBLOCKING {
    if (takeDirty()) apply();
    if (float* out = io.channel(Out, 0)) std::fill(out + offset, out + offset + n, static_cast<float>(current()));
  }

 private:
  void apply() noexcept { keytrack.setParams(values[Min], values[Max], values[Bend], values[Window]); }

  Keytrack keytrack;
};

}  // namespace adapter
}  // namespace dsp
