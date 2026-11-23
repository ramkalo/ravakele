/**
 * MIDI input settings: how incoming notes and controllers are read before anything plays them.
 *
 * Mod wheel channel, Pedal channel — the MIDI channel the mod wheel (CC 1) and the sustain pedal (CC 64) are taken
 *   from: Omni (any channel) or 1..16. A controller on another channel is ignored.
 *
 * Pure arithmetic; nothing allocates.
 */
#pragma once

#include "../util/math.hpp"

namespace dsp {

/** A channel setting: 0 is Omni, 1..16 a channel. */
constexpr int MIDI_OMNI = 0;
constexpr const char* MIDI_CHANNEL_LABELS[] = {"Omni", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"};

constexpr int MIDI_CC_MOD_WHEEL = 1;
constexpr int MIDI_CC_SUSTAIN = 64;

/** Whether a channel setting (0 Omni, 1..16) takes a message on `channel` (1..16). */
inline bool channelAccepts(int setting, int channel) noexcept { return setting == MIDI_OMNI || setting == channel; }

class MidiInput {
 public:
  /** Mod wheel and pedal channels 0 (Omni)..16; each rounded. */
  void setRouting(double modWheelChannel, double pedalChannel) noexcept {
    modWheel = static_cast<int>(clamp(roundHalfUp(modWheelChannel), 0, 16));
    pedal = static_cast<int>(clamp(roundHalfUp(pedalChannel), 0, 16));
  }

  /** Whether a control change `cc` on `channel` (1..16) is taken: the mod wheel and sustain on their channels, anything else always. */
  bool accepts(int cc, int channel) const noexcept {
    if (cc == MIDI_CC_MOD_WHEEL) return channelAccepts(modWheel, channel);
    if (cc == MIDI_CC_SUSTAIN) return channelAccepts(pedal, channel);
    return true;
  }

 private:
  int modWheel = MIDI_OMNI;
  int pedal = MIDI_OMNI;
};

}  // namespace dsp
