/**
 * mod.duck as a uniform module (Module.hpp): the dip trigger notes fire (mod/Duck.hpp), 0..1, a Mod mode source. It
 * lives in Shared or Global; runtime.hpp offers it each note-on with its channel through trigger(), and a note it
 * takes plays no voice.
 */
#pragma once

#include <algorithm>

#include "../mod/Duck.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class ModDuck : public Wrapper<ModDuck, 9> {
 public:
  static constexpr const char* ID = "mod.duck";
  enum Param : int { Channel, Lowest, Highest, Depth, Attack, AttackBend, Hold, Release, ReleaseBend };
  static constexpr ParamInfo PARAMS[] = {{"channel", DUCK_CHANNEL_DEFAULT},        {"lowest", DUCK_LOWEST_DEFAULT},
                                         {"highest", DUCK_HIGHEST_DEFAULT},        {"depth", DUCK_DEPTH_DEFAULT},
                                         {"attack", DUCK_ATTACK_DEFAULT},          {"attackBend", DUCK_ATTACK_BEND_DEFAULT},
                                         {"hold", DUCK_HOLD_DEFAULT},              {"release", DUCK_RELEASE_DEFAULT},
                                         {"releaseBend", DUCK_RELEASE_BEND_DEFAULT}};
  enum Port : int { Out, PORT_COUNT };
  static constexpr PortInfo PORTS[] = {{"out", Direction::Out, Kind::Control, Rate::Sample, 1}};

  void prepare(double sampleRate, int /*maxBlock*/) {
    apply();
    duck.prepare(sampleRate);
  }

  void reset() noexcept {
    if (takeDirty()) apply();
    duck.reset();
  }

  /** A note-on (channel 1..16): fires the dip and returns true when its channel and note are this Duck's. */
  bool trigger(int channel, int note) noexcept {
    if (takeDirty()) apply();  // the channel and notes as set now, not at the next block
    if (!duck.accepts(channel, note)) return false;
    duck.trigger();
    return true;
  }

  /** Which part of the dip it is in (mod/Duck.hpp DUCK_IDLE..DUCK_RELEASE), for a pad that shows it. */
  double stage() const noexcept { return duck.stage(); }

  /** How far through that part, 0..1, for a pad that shows it. */
  double progress() const noexcept { return duck.progress(); }

  void process(const Io& io, int offset, int n) DSP_NONBLOCKING {
    if (takeDirty()) apply();
    const float value = static_cast<float>(duck.advance(n));
    if (float* out = io.channel(Out, 0)) std::fill(out + offset, out + offset + n, value);
  }

 private:
  void apply() noexcept {
    duck.setParams(values[Channel], values[Lowest], values[Highest], values[Depth], values[Attack], values[AttackBend], values[Hold],
                   values[Release], values[ReleaseBend]);
  }

  Duck duck;
};

}  // namespace adapter
}  // namespace dsp
