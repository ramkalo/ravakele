/**
 * mod.velocity as a uniform module (Module.hpp): the velocity a note was played at, through the curve and the Polarity
 * Window, −1..1, a Mod mode source. It takes the velocity at each note-on and holds it for the note.
 */
#pragma once

#include <algorithm>

#include "../mod/Velocity.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class ModVelocity : public Wrapper<ModVelocity, 4> {
 public:
  static constexpr const char* ID = "mod.velocity";
  enum Param : int { Min, Max, Bend, Window };
  static constexpr ParamInfo PARAMS[] = {
      {"min", VELOCITY_MIN_DEFAULT}, {"max", VELOCITY_MAX_DEFAULT}, {"bend", VELOCITY_BEND_DEFAULT}, {"window", VELOCITY_WINDOW_DEFAULT}};
  enum Port : int { Out, PORT_COUNT };
  static constexpr PortInfo PORTS[] = {{"out", Direction::Out, Kind::Control, Rate::Sample, 1}};

  void prepare(double /*sampleRate*/, int /*maxBlock*/) { apply(); }

  void reset() noexcept {
    if (takeDirty()) apply();
  }

  void noteOn(const Note& note) noexcept { source.noteOn(note.velocity); }

  /** The value this voice holds now, −1..1. */
  double current() const noexcept { return source.value(); }

  void process(const Io& io, int offset, int n) DSP_NONBLOCKING {
    if (takeDirty()) apply();
    if (float* out = io.channel(Out, 0)) std::fill(out + offset, out + offset + n, static_cast<float>(current()));
  }

 private:
  void apply() noexcept { source.setParams(values[Min], values[Max], values[Bend], values[Window]); }

  VelocitySource source;
};

}  // namespace adapter
}  // namespace dsp
