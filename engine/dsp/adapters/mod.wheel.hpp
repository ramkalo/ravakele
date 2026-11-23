/**
 * mod.wheel as a uniform module (Module.hpp): the mod wheel's position through Sensitivity, Return, the curve,
 * Smoothing and the Polarity Window (mod/Wheel.hpp), −1..1, a Mod mode source. It lives in Shared or Global;
 * runtime.hpp hands it each mouse scroll through wheel() and each MIDI mod wheel message (CC 1) through modWheel().
 */
#pragma once

#include <algorithm>

#include "../mod/Wheel.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class ModWheel : public Wrapper<ModWheel, 8> {
 public:
  static constexpr const char* ID = "mod.wheel";
  enum Param : int { On, Sensitivity, Return, Min, Max, Bend, Smoothing, Window };
  static constexpr ParamInfo PARAMS[] = {{"on", WHEEL_ON_DEFAULT},         {"sensitivity", WHEEL_SENSITIVITY_DEFAULT},
                                         {"return", WHEEL_RETURN_DEFAULT}, {"min", WHEEL_MIN_DEFAULT},
                                         {"max", WHEEL_MAX_DEFAULT},       {"bend", WHEEL_BEND_DEFAULT},
                                         {"smoothing", WHEEL_SMOOTHING_DEFAULT}, {"window", WHEEL_WINDOW_DEFAULT}};
  enum Port : int { Out, PORT_COUNT };
  static constexpr PortInfo PORTS[] = {{"out", Direction::Out, Kind::Control, Rate::Sample, 1}};

  void prepare(double sampleRate, int /*maxBlock*/) {
    apply();
    source.prepare(sampleRate);
  }

  void reset() noexcept {
    if (takeDirty()) apply();
    source.reset();
  }

  /** A mouse scroll of `travel` px, up positive. */
  void wheel(double travel) noexcept {
    if (takeDirty()) apply();  // On and Sensitivity as set now, not at the next block
    source.scroll(travel);
  }

  /** A MIDI mod wheel message (CC 1, 0..127). */
  void modWheel(double raw) noexcept {
    if (takeDirty()) apply();
    source.control(raw);
  }

  void process(const Io& io, int offset, int n) DSP_NONBLOCKING {
    if (takeDirty()) apply();
    const float value = static_cast<float>(source.advance(n));
    if (float* out = io.channel(Out, 0)) std::fill(out + offset, out + offset + n, value);
  }

 private:
  void apply() noexcept {
    source.setParams(values[On], values[Sensitivity], values[Return], values[Min], values[Max], values[Bend], values[Smoothing], values[Window]);
  }

  WheelSource source;
};

}  // namespace adapter
}  // namespace dsp
