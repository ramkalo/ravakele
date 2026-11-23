/**
 * The fixed part of every engine: what the generated Engine.hpp builds on. The generator writes the project's three
 * stages, as the designer drew them on the Architecture chart: Shared (the blocks that run once, before the voices:
 * what depends on no note), Voice (the blocks that run once per sounding note) with VoiceRoutes (their routing, held
 * once for every voice), Global (the blocks that run once, after the voices are summed: effects and what follows
 * them); and Notes (the note sources that play the voices: step sequencers) and the param table. Synth here runs them:
 * voice allocation, chunking a host's block into maxBlock pieces and again at every note a source fires, pitch (each
 * voice's glide, dsp::Portamento, from the note it was given to its new one, the note moved by dsp::PitchShift (a
 * change to the shift glides each sounding note there at the portamento's speed), then by dsp::Tuning's Transpose and Fine, plus its own MPE bend, turned to Hz against dsp::Tuning's A4), unison (one
 * dsp::Unison handing every voice its copies), MPE (dsp::Mpe: a member channel's messages reach only the voice playing
 * its note), Shared's outputs (one buffer every voice
 * and Global read), the buses that sum each voice's audio into Global, and the taps that carry a voice's control
 * output into Global from the newest sounding voice, and the mod wheel (a mouse scroll, Synth::wheel, and MIDI CC 1 on
 * note.midi-input's Mod wheel channel, both handed to Shared's and Global's Mod wheels, mod.wheel). Before each block the KFOs and SOLOKFOs play (dsp::KfoHost on the
 * generated KFO tables): a parameter one of them drives is theirs while they do, and setParam then only keeps its
 * value for when they stop.
 *
 * Generated code relies on: ParamSpec, VoiceNote, NOTE_IDLE, sumInto/sumScaled/mulInto/copyChannels, MixGains/mixGains/mixInto,
 * and on Synth calling
 *   Shared:      prepare(sr, maxBlock), setParam(index, value), noteOn(VoiceNote), noteOff(), process(n), outputs()
 *                (its buffers, read as `shared`), duck(channel, note) (offers a note-on to its Ducks, mod.duck),
 *                wheel(travel) and modWheel(raw) (a mouse scroll in px, up positive, and a CC 1 value 0..127, to its
 *                Mod wheels, mod.wheel)
 *   VoiceRoutes: prepare(sr), setParam(index, value)  (held once; each voice reads it)
 *   Voice:       prepare(sr, maxBlock), setParam(index, value), noteOn(VoiceNote), noteOff(), finished(), level(),
 *                pressure(poly, raw) (channel pressure, and poly pressure for its key, or its MPE channel's pressure),
 *                setUnison(count, cents, gain) (voice.unison's copies, for each oscillator that takes unison),
 *                setSeed(voice) (its number, for each seeded block: mod.random draws its own sequence in every voice),
 *                process(n, pitchHz, routes, shared, buses)  (adds its audio into the buses), tap(taps, n)
 *   Global:      prepare(sr, maxBlock), setParam(index, value), noteOn(VoiceNote), noteOff(), duck(channel, note),
 *                wheel(travel), modWheel(raw),
 *                process(n, shared, taps, buses, left, right)
 *   Shared and Global also have finished() (their envelopes done). Voice's finished() is its envelopes the designer
 *   set to end the note (Ends the note, the block's endsVoice) done, and Voice OWN_ENVELOPE says it has one: a voice
 *   without one is kept after its key while no key is held and Shared's or Global's envelopes still sound.
 *   Shared's and Global's noteOn/noteOff play their envelopes, one for the whole instrument: noteOn on the first key
 *   and, with Voice allocation's Trigger on Retrigger, on every new key (the newest key's note and velocity); noteOff
 *   when the last key is let go. Each note-on is first offered with its channel (1..16; a note with none counts as 1) to
 *   Shared's and Global's duck(channel, note): a note a Duck takes fires its dip and plays no voice, and its note-off
 *   is dropped. MPE member channels' notes are not offered.
 *   Notes:       prepare(sr), setParam(index, value), next(maxSamples, sink) (fires what is due into the sink and says
 *                how many samples, 1..maxSamples, may render before the next note), rendered(n)
 *   and each of Shared, Voice, Global and Notes watch(out): writes what its blocks report for the pads (an LFO's phase,
 *   a Random's value, an envelope's stage, a sequencer's step) into their places among PadWatch values. A voice block reports from the
 *   newest sounding voice. Each of Shared, Voice and Global also has spectrum(out, n): adds the output of each filter a
 *   filter pad shows, of each Waterfall block a waterfall graph shows, of each Scope block an oscilloscope shows and of
 *   each Tuner block a tuner shows,
 *   into its place among the Spectra buffers
 *   (every sounding voice adds its own, so they sum).
 *   A param reaches only the classes its PARAM_STAGE bits name (STAGE_*).
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "dsp/motion/KfoHost.hpp"
#include "dsp/note/MidiInput.hpp"
#include "dsp/note/Mpe.hpp"
#include "dsp/note/NoteSink.hpp"
#include "dsp/note/PitchShift.hpp"
#include "dsp/note/Portamento.hpp"
#include "dsp/note/Tuning.hpp"
#include "dsp/note/Unison.hpp"
#include "dsp/note/VoiceAllocator.hpp"
#include "dsp/util/pan.hpp"

namespace lemnos {

struct ParamSpec {
  const char* address;
  double min;
  double max;
  double value;  // its default
};

/** A note starting on a voice. */
struct VoiceNote {
  double note;
  double velocity;
  bool sounding;  // the voice was already sounding (a stolen voice, or one voice changing key on Retrigger)
};

/** The engine params that set the allocator (voice.allocation's Voices, Priority, Trigger); -1 where there is none. */
struct AllocationParams {
  int polyphony = -1;
  int priority = -1;
  int trigger = -1;
};

/** The engine params that set every voice's glide (note.portamento's Time, Curve, Time mode, Glide mode); -1 where there is none. */
struct PortamentoParams {
  int time = -1;
  int curve = -1;
  int timeMode = -1;
  int glideMode = -1;
};

/** The engine params that set the pitch shift (note.pitch-shift's Semitones, Direction, On); -1 where there is none. */
struct PitchShiftParams {
  int semitones = -1;
  int direction = -1;
  int on = -1;
};

/** The engine params that set the tuning (voice.tuning's A4, Transpose, Fine); -1 where there is none. */
struct TuningParams {
  int a4 = -1;
  int transpose = -1;
  int fine = -1;
};

/** The engine params that set the unison copies (voice.unison's Unison, Detune, Detune curve); -1 where there is none. */
struct UnisonParams {
  int count = -1;
  int detune = -1;
  int curve = -1;
};

/** The engine params that set MPE (note.mpe's MPE on, Zone, Member channels, Per-note bend, Master bend, Glide); -1 where there is none. */
struct MpeParams {
  int enabled = -1;
  int zone = -1;
  int members = -1;
  int noteBend = -1;
  int masterBend = -1;
  int glide = -1;
};

/** The engine param that sets which channel the mod wheel (CC 1) is taken from (note.midi-input's Mod wheel channel); -1 where there is none. */
struct MidiInputParams {
  int modWheelChannel = -1;
};

/** dst += src, from 0 for n samples. */
inline void sumInto(float* dst, const float* src, int n) noexcept {
  for (int i = 0; i < n; i++) dst[i] += src[i];
}

/** dst += src × gain, from 0 for n samples: a channel's share of a mean (the spectra). */
inline void sumScaled(float* dst, const float* src, float gain, int n) noexcept {
  for (int i = 0; i < n; i++) dst[i] += src[i] * gain;
}

/** dst *= src, from 0 for n samples: control signals into one input multiply (two gains compose). */
inline void mulInto(float* dst, const float* src, int n) noexcept {
  for (int i = 0; i < n; i++) dst[i] *= src[i];
}

/** A mix arrow's two gains: its level times a constant-power pan (a mono arrow, width 1) or a balance (stereo, 2). */
struct MixGains {
  float l, r;
};
inline MixGains mixGains(double level, double pan, int width) noexcept {
  const double lv = level < 0 ? 0 : level > 1 ? 1 : level;
  const dsp::PanGains g = width == 1 ? dsp::panGains(pan) : dsp::balanceGains(pan);
  return {static_cast<float>(lv * g.l), static_cast<float>(lv * g.r)};
}

/** dst += src · gain, the gain ramping from `from` to `to` across the n samples, so a moved level or pan never clicks. */
inline void mixInto(float* dst, const float* src, float from, float to, int n) noexcept {
  const float step = n > 0 ? (to - from) / static_cast<float>(n) : 0.0f;
  for (int i = 0; i < n; i++) dst[i] += src[i] * (from + step * static_cast<float>(i + 1));
}

/** PARAM_STAGE bits: the classes a param reaches. */
constexpr unsigned char STAGE_SHARED = 1, STAGE_VOICE = 2, STAGE_ROUTES = 4, STAGE_GLOBAL = 8, STAGE_NOTES = 16;

/** What a note source's advance() is asked for when nothing caps it: far enough that only its own next note does. */
constexpr int NOTE_IDLE = 1 << 30;

/** One output channel's peak over n samples. */
inline double peak(const float* buf, int n) noexcept {
  double p = 0;
  for (int i = 0; i < n; i++) p = std::max(p, static_cast<double>(std::fabs(buf[i])));
  return p;
}

template <class Shared, class VoiceRoutes, class Voice, class Global, class Notes, int ParamCount, int BusChannels, int TapChannels, int PadWatch, int Spectra>
class Synth {
 public:
  static constexpr int SCOPE = 4096;
  /** Samples kept of each spectrum (the newest; lemnos_spectrum reads up to this many). */
  static constexpr int SPECTRUM_RING = 4096;

  Synth(const ParamSpec* params, const unsigned char* paramStages, AllocationParams allocation, PortamentoParams portamento,
        PitchShiftParams pitchShift, TuningParams tuning, UnisonParams unison, MpeParams mpe, MidiInputParams midiInput, const dsp::KfoTables& kfo)
      : params(params), paramStages(paramStages), allocation(allocation), portamento(portamento), pitchShift(pitchShift), tuningParams(tuning), unison(unison),
        mpeParams(mpe), midiInputParams(midiInput), kfo(kfo) {
    for (int i = 0; i < ParamCount; i++) values[i] = params[i].value;
  }

  void prepare(double sampleRate, int maxBlock) {
    this->maxBlock = maxBlock > 0 ? maxBlock : 128;
    busData.assign(static_cast<std::size_t>(BusChannels > 0 ? BusChannels : 1) * this->maxBlock, 0.0f);
    for (int c = 0; c < BusChannels; c++) buses[c] = busData.data() + c * this->maxBlock;
    tapData.assign(static_cast<std::size_t>(TapChannels > 0 ? TapChannels : 1) * this->maxBlock, 0.0f);
    for (int c = 0; c < TapChannels; c++) taps[c] = tapData.data() + c * this->maxBlock;
    std::fill(std::begin(tapHeld), std::end(tapHeld), 0.0f);
    spectrumData.assign(static_cast<std::size_t>(Spectra > 0 ? Spectra : 1) * this->maxBlock, 0.0f);
    for (int k = 0; k < Spectra; k++) spectrumNow[k] = spectrumData.data() + k * this->maxBlock;
    spectrumRing.assign(static_cast<std::size_t>(Spectra) * SPECTRUM_RING, 0.0f);
    spectrumAt = 0;
    allocator.prepare(sampleRate);
    shared.prepare(sampleRate, this->maxBlock);
    routes.prepare(sampleRate);
    for (Voice& voice : voices) voice.prepare(sampleRate, this->maxBlock);
    for (int v = 0; v < dsp::MAX_VOICES; v++) voices[v].setSeed(v);
    for (dsp::Portamento& glide : glides) glide.prepare(sampleRate);
    glideMemory.reset();
    for (dsp::MpeNote& note : mpeNotes) note.prepare(sampleRate);
    std::fill(std::begin(voiceChannel), std::end(voiceChannel), 0);
    std::fill(std::begin(keyChannel), std::end(keyChannel), 0);
    global.prepare(sampleRate, this->maxBlock);
    notes.prepare(sampleRate);
    kfoHost.prepare(kfo, sampleRate);
    for (int i = 0; i < ParamCount; i++) setParam(i, values[i]);
    std::fill(std::begin(active), std::end(active), false);
  }

  /** Sets a param, kept inside its own range (PARAMS' min and max); a value that is not finite is ignored. */
  void setParam(int index, double value) noexcept {
    if (index < 0 || index >= ParamCount || !std::isfinite(value)) return;
    value = std::clamp(value, std::min(params[index].min, params[index].max), std::max(params[index].min, params[index].max));
    values[index] = value;
    if (!kfoHost.owns(index)) apply(index, value);
  }

  /** A take into a KFO's or SOLOKFO's slot (dsp::KfoHost::setTake). */
  void setMotion(int player, int slot, const float* xy, int count) noexcept { kfoHost.setTake(player, slot, xy, count); }

  /**
   * What the engine reports now: the KFOs' (dsp::KfoHost::watch), then the PadWatch values the pads show (−1 where
   * nothing runs: a voice block with no voice sounding). Returns how many it wrote.
   */
  int watch(float* out, int capacity) const noexcept {
    if (!out || capacity <= 0) return 0;
    const int k = kfoHost.watch(out, capacity);
    float pads[PadWatch > 0 ? PadWatch : 1];
    std::fill(pads, pads + (PadWatch > 0 ? PadWatch : 1), -1.0f);
    const int voice = sounding();
    if (voice >= 0) voices[voice].watch(pads);
    shared.watch(pads);
    global.watch(pads);
    notes.watch(pads);
    const int n = std::min(PadWatch, capacity - k);
    for (int i = 0; i < n; i++) out[k + i] = pads[i];
    return k + std::max(0, n);
  }

  /** Sets a param where it acts: the stage that owns it (every voice only for a voice block's own), the note sources, the allocator, the glides, the pitch shift, the tuning, the unison, MPE. */
  void apply(int index, double value) noexcept {
    const unsigned char stage = paramStages[index];
    if (stage & STAGE_SHARED) shared.setParam(index, value);
    if (stage & STAGE_ROUTES) routes.setParam(index, value);
    if (stage & STAGE_VOICE) for (Voice& voice : voices) voice.setParam(index, value);
    if (stage & STAGE_GLOBAL) global.setParam(index, value);
    if (stage & STAGE_NOTES) notes.setParam(index, value);
    if (index == allocation.polyphony || index == allocation.priority || index == allocation.trigger) {
      allocator.setParams(allocationValue(allocation.polyphony, 8), allocationValue(allocation.priority, dsp::PRIORITY_LAST),
                          allocationValue(allocation.trigger, dsp::TRIGGER_RETRIGGER));
    }
    const int glideIndex[] = {portamento.time, portamento.curve, portamento.timeMode, portamento.glideMode};
    for (int k = 0; k < 4; k++) {
      if (index != glideIndex[k]) continue;
      glideSettings[k] = value;
      for (dsp::Portamento& glide : glides) glide.setParams(glideSettings[0], glideSettings[1], glideSettings[2], glideSettings[3]);
    }
    const int shiftIndex[] = {pitchShift.semitones, pitchShift.direction, pitchShift.on};
    for (int k = 0; k < 3; k++) {
      if (index != shiftIndex[k]) continue;
      shiftSettings[k] = value;
      const double was = shift.semitones();
      shift.setParams(shiftSettings[0], shiftSettings[1], shiftSettings[2]);
      // The shift rides the glide: a change moves each sounding note at the portamento's speed.
      if (shift.semitones() != was) {
        for (int v = 0; v < dsp::MAX_VOICES; v++) if (active[v]) glides[v].retarget(voiceKey[v] + shift.semitones(), glideMemory);
      }
    }
    const int tuningIndex[] = {tuningParams.a4, tuningParams.transpose, tuningParams.fine};
    for (int k = 0; k < 3; k++) {
      if (index != tuningIndex[k]) continue;
      tuningSettings[k] = value;
      tuning.setParams(tuningSettings[0], tuningSettings[1], tuningSettings[2]);
    }
    const int unisonIndex[] = {unison.count, unison.detune, unison.curve};
    for (int k = 0; k < 3; k++) {
      if (index != unisonIndex[k]) continue;
      unisonSettings[k] = value;
      applyUnison();
    }
    const int mpeIndex[] = {mpeParams.enabled, mpeParams.zone, mpeParams.members, mpeParams.noteBend, mpeParams.masterBend, mpeParams.glide};
    for (int k = 0; k < 6; k++) {
      if (index != mpeIndex[k]) continue;
      mpeSettings[k] = value;
      mpe.setZone(mpeSettings[0], mpeSettings[1], mpeSettings[2], mpeSettings[3], mpeSettings[4]);
      mpe.setNote(mpeSettings[5], 0);
    }
    if (index == midiInputParams.modWheelChannel) midiInput.setRouting(value, dsp::MIDI_OMNI);
  }

  double param(int index) const noexcept { return index >= 0 && index < ParamCount ? values[index] : 0; }

  /** A note-on with no channel (the computer keyboard, Hold note, a sequencer, lemnos_note_on): channel 1. */
  void noteOn(int note, double velocity) noexcept { noteOnFrom(1, note, velocity); }

  void noteOff(int note) noexcept { noteOffFrom(1, note); }

  /** A note-on on `channel` (1..16): a Duck that takes it fires its dip and no voice plays; any other note plays. */
  void noteOnFrom(int channel, int note, double velocity) noexcept {
    if (velocity <= 0) return noteOffFrom(channel, note);
    if (note >= 0 && note < dsp::NOTE_KEYS && channel >= 1 && channel <= 16) {
      const bool sharedTook = shared.duck(channel, note), globalTook = global.duck(channel, note);  // both, always
      if (sharedTook || globalTook) {
        ducked[channel - 1][note] = true;
        return;
      }
    }
    playNote(note, velocity);
  }

  /** A note-off on `channel`: dropped for a note a Duck took, else the note is let go. */
  void noteOffFrom(int channel, int note) noexcept {
    if (note >= 0 && note < dsp::NOTE_KEYS && channel >= 1 && channel <= 16 && ducked[channel - 1][note]) {
      ducked[channel - 1][note] = false;
      return;
    }
    releaseNote(note);
  }

  void playNote(int note, double velocity) noexcept {
    if (velocity <= 0) return releaseNote(note);
    velocity = std::clamp(velocity, 0.0, 1.0);
    if (note >= 0 && note < dsp::NOTE_KEYS) {
      velocities[note] = velocity;
      keyChannel[note] = incomingChannel;  // its MPE member channel, or 0 (a note from anywhere else)
    }
    play(allocator.noteOn(note), velocity, true);
    keyOn(note, velocity);
  }

  void releaseNote(int note) noexcept {
    keyOff(note);
    glideMemory.noteOff();
    dsp::NoteChange changes[dsp::MAX_VOICES];
    const int n = allocator.noteOff(note, changes, dsp::MAX_VOICES);
    for (int i = 0; i < n; i++) {
      const int to = changes[i].note;
      play(changes[i], to >= 0 && to < dsp::NOTE_KEYS ? velocities[to] : 1.0, false);
    }
  }

  void allNotesOff() noexcept {
    std::int32_t slots[dsp::MAX_VOICES];
    const int n = allocator.allNotesOff(slots, dsp::MAX_VOICES);
    for (int i = 0; i < n; i++) voices[slots[i]].noteOff();
    glideMemory.held = 0;
    std::fill(std::begin(keyDown), std::end(keyDown), false);
    for (auto& keys : ducked) std::fill(std::begin(keys), std::end(keys), false);
    if (keysHeld > 0) { shared.noteOff(); global.noteOff(); }
    keysHeld = 0;
  }

  void pitchBend(double semitones) noexcept { bend = semitones; }

  /**
   * A MIDI message. With MPE on, a message on a member channel belongs to the note played on it (memberMidi) and the
   * manager channel's pitch bend bends every note by Master bend; everything else, and everything with MPE off, is
   * read as ordinary MIDI below.
   */
  void midi(const unsigned char* bytes, int length) noexcept {
    if (!bytes || length < 1) return;
    const int status = bytes[0] & 0xF0, channel = (bytes[0] & 0x0F) + 1;
    if (mpe.isMember(channel) && memberMidi(status, channel, bytes, length)) return;
    if (mpe.isManager(channel) && status == 0xE0 && length >= 3) return pitchBend(mpe.masterBendSemitones(bytes[2] << 7 | bytes[1]));
    if (status == 0x90 && length >= 3) noteOnFrom(channel, bytes[1], bytes[2] / 127.0);
    else if (status == 0x80 && length >= 3) noteOffFrom(channel, bytes[1]);
    else if (status == 0xE0 && length >= 3) pitchBend(((bytes[2] << 7 | bytes[1]) - 8192) / 8192.0 * 2.0);
    else if (status == 0xB0 && length >= 3 && bytes[1] == dsp::MIDI_CC_MOD_WHEEL) { if (midiInput.accepts(dsp::MIDI_CC_MOD_WHEEL, channel)) modWheel(bytes[2]); }
    else if (status == 0xB0 && length >= 3 && (bytes[1] == 120 || bytes[1] == 123)) allNotesOff();
    else if (status == 0xD0 && length >= 2) channelPressure(bytes[1]);
    else if (status == 0xA0 && length >= 3) polyPressure(bytes[1], bytes[2]);
  }

  /** A mouse scroll of `travel` px (up positive): every Mod wheel (mod.wheel) in Shared and Global takes it. */
  void wheel(double travel) noexcept {
    shared.wheel(travel);
    global.wheel(travel);
  }

  /** A MIDI mod wheel value (CC 1, 0..127), already on the Mod wheel channel: every Mod wheel takes it. */
  void modWheel(double raw) noexcept {
    shared.modWheel(raw);
    global.modWheel(raw);
  }

  /** Channel pressure (0..127): every sounding voice takes it (an Aftertouch block whose Type accepts it). */
  void channelPressure(double raw) noexcept {
    for (int v = 0; v < dsp::MAX_VOICES; v++) if (active[v]) voices[v].pressure(false, raw);
  }

  /** Poly key pressure (0..127) for one key: the voices playing that key take it. */
  void polyPressure(int note, double raw) noexcept {
    for (int v = 0; v < dsp::MAX_VOICES; v++) if (active[v] && voiceKey[v] == note) voices[v].pressure(true, raw);
  }

  /**
   * A message on an MPE member channel: its note-on and note-off play the note and remember the channel; its pitch bend
   * goes to the MpeNote of the voice playing that channel's note, its channel pressure to that voice as poly pressure.
   * Slide (CC 74) is not built. Returns false for what it leaves to ordinary MIDI.
   */
  bool memberMidi(int status, int channel, const unsigned char* bytes, int length) noexcept {
    if ((status == 0x90 || status == 0x80) && length >= 3) {
      incomingChannel = channel;
      if (status == 0x90) playNote(bytes[1], bytes[2] / 127.0);
      else releaseNote(bytes[1]);
      incomingChannel = 0;
      return true;
    }
    if (status == 0xE0 && length >= 3) {
      const double raw = bytes[2] << 7 | bytes[1];
      for (int v = 0; v < dsp::MAX_VOICES; v++) if (active[v] && voiceChannel[v] == channel) mpeNotes[v].bend(raw);
      return true;
    }
    if (status == 0xD0 && length >= 2) {
      for (int v = 0; v < dsp::MAX_VOICES; v++) if (active[v] && voiceChannel[v] == channel) voices[v].pressure(true, bytes[1]);
      return true;
    }
    return false;
  }

  /** Renders n samples in pieces of at most maxBlock, cut again wherever a note source fires, so its notes land on their sample. */
  void process(float* left, float* right, int n) noexcept {
    for (int done = 0; done < n;) {
      const int chunk = std::clamp(notes.next(std::min(maxBlock, n - done), sink), 1, std::min(maxBlock, n - done));
      block(left + done, right + done, chunk);
      notes.rendered(chunk);
      done += chunk;
    }
  }

  void scope(float* out, int n) const noexcept {
    n = std::clamp(n, 0, SCOPE);
    for (int i = 0; i < n; i++) out[i] = scopeRing[(scopeAt + SCOPE - n + i) % SCOPE];
  }

  /** The last n samples (n ≤ SPECTRUM_RING) of spectrum `index`, oldest first: a filter pad's filter's output, every voice's summed. */
  void spectrum(int index, float* out, int n) const noexcept {
    n = std::clamp(n, 0, SPECTRUM_RING);
    if (index < 0 || index >= Spectra) { std::fill(out, out + n, 0.0f); return; }
    const float* ring = spectrumRing.data() + static_cast<std::size_t>(index) * SPECTRUM_RING;
    for (int i = 0; i < n; i++) out[i] = ring[(spectrumAt + SPECTRUM_RING - n + i) % SPECTRUM_RING];
  }

  const ParamSpec* paramSpecs() const noexcept { return params; }

 private:
  void block(float* left, float* right, int n) noexcept {
    kfoHost.process(n, values, [this](int index, double value) { apply(index, value); });
    shared.process(n);
    float* const* sharedOut = shared.outputs();
    if (Spectra > 0) {
      for (int k = 0; k < Spectra; k++) std::fill(spectrumNow[k], spectrumNow[k] + n, 0.0f);
      shared.spectrum(spectrumNow, n);
    }
    for (int c = 0; c < BusChannels; c++) std::fill(buses[c], buses[c] + n, 0.0f);
    for (int v = 0; v < dsp::MAX_VOICES; v++) {
      if (!active[v]) continue;
      Voice& voice = voices[v];
      mpeNotes[v].advance(n);
      const double ownBend = mpe.on() ? mpeNotes[v].bendSemitones() : 0;  // its note's own MPE bend
      voice.process(n, tuning.hz(tuning.pitch(glides[v].advance(n)) + bend + ownBend), routes, sharedOut, buses);
      if (Spectra > 0) voice.spectrum(spectrumNow, n);
      allocator.setLevel(v, voice.level());
      // A voice ends once its key is up and the envelopes set to end the note have finished. A voice with none rides
      // Shared's and Global's: once the last key is up, it is kept until they finish.
      if (allocator.isReleased(v) && voice.finished() && (Voice::OWN_ENVELOPE || keysHeld > 0 || (shared.finished() && global.finished()))) {
        allocator.finished(v);
        active[v] = false;
      }
      // A slot the allocator retired (polyphony lowered) stops too.
      if (!allocator.isActive(v)) active[v] = false;
    }
    // The taps: what the newest sounding voice's control outputs say; with none sounding, their last value held.
    if (TapChannels > 0) {
      const int voice = sounding();
      if (voice >= 0) voices[voice].tap(taps, n);
      for (int c = 0; c < TapChannels; c++) {
        if (voice >= 0) tapHeld[c] = taps[c][n - 1];
        else std::fill(taps[c], taps[c] + n, tapHeld[c]);
      }
    }
    global.process(n, sharedOut, taps, buses, left, right);
    if (Spectra > 0) {
      global.spectrum(spectrumNow, n);
      for (int k = 0; k < Spectra; k++) {
        float* ring = spectrumRing.data() + static_cast<std::size_t>(k) * SPECTRUM_RING;
        for (int i = 0; i < n; i++) ring[(spectrumAt + i) % SPECTRUM_RING] = spectrumNow[k][i];
      }
      spectrumAt = (spectrumAt + n) % SPECTRUM_RING;
    }
    for (int i = 0; i < n; i++) {
      scopeRing[scopeAt] = 0.5f * (left[i] + right[i]);
      scopeAt = (scopeAt + 1) % SCOPE;
    }
  }

  /**
   * Does what the allocator says to a slot. A Start or a Move sets the slot's glide to the new note plus the pitch shift
   * (block() plays the glide's pitch) and starts its MPE expression afresh on the new note's channel. `keyDown` is false for a change a note-off made (a mono voice falling back to a key still held):
   * that is no key going down, so the glide memory's held count is put back.
   *
   * Every new note is numbered for the round robins (mod.voice-values, Voice::setStep): a Start, and a Move a key
   * going down made (a mono voice changing key, legato or not). A voice falling back to a key still held keeps its number.
   */
  void play(const dsp::NoteChange& change, double velocity, bool keyDown) noexcept {
    const int slot = change.slot;
    switch (change.action) {
      case dsp::NoteAction::Start:
        if (keyDown) voices[slot].setStep(noteCount++);
        glide(slot, change.note, keyDown);
        expression(slot, change.note);
        voiceKey[slot] = change.note;
        voices[slot].noteOn(VoiceNote{static_cast<double>(change.note), velocity, active[slot]});
        active[slot] = true;
        newest = slot;
        break;
      case dsp::NoteAction::Move:
        if (keyDown) voices[slot].setStep(noteCount++);
        glide(slot, change.note, keyDown);
        expression(slot, change.note);
        voiceKey[slot] = change.note;
        newest = slot;
        break;
      case dsp::NoteAction::Release: voices[slot].noteOff(); break;
      case dsp::NoteAction::None: break;
    }
  }

  void glide(int slot, int note, bool keyDown) noexcept {
    glides[slot].noteOn(note + shift.semitones(), glideMemory);
    if (!keyDown) glideMemory.noteOff();
  }

  /** A slot's MPE expression on a new note: the note's channel (0: not an MPE note), its bend centred. */
  void expression(int slot, int note) noexcept {
    voiceChannel[slot] = note >= 0 && note < dsp::NOTE_KEYS ? keyChannel[note] : 0;
    mpeNotes[slot].noteOn(mpe);
  }

  /** Hands every voice the unison copies as the settings now say. */
  void applyUnison() noexcept {
    unisonModel.setParams(unisonSettings[0], unisonSettings[1], unisonSettings[2]);
    double cents[dsp::UNISON_MAX] = {};
    for (int i = 0; i < unisonModel.count(); i++) cents[i] = unisonModel.cents(i);
    for (Voice& voice : voices) voice.setUnison(unisonModel.count(), cents, unisonModel.gain());
  }

  /**
   * The envelopes in Shared and Global, one for the whole instrument, follow the keys: the first key opens them, a new
   * key while others are held opens them again only with Trigger on Retrigger, and the last key let go releases them.
   */
  void keyOn(int note, double velocity) noexcept {
    if (note < 0 || note >= dsp::NOTE_KEYS || keyDown[note]) return;
    keyDown[note] = true;
    const bool open = keysHeld++ > 0;
    if (open && allocator.getTrigger() == dsp::TRIGGER_LEGATO) return;
    const VoiceNote key{static_cast<double>(note), velocity, open};
    shared.noteOn(key);
    global.noteOn(key);
  }

  void keyOff(int note) noexcept {
    if (note < 0 || note >= dsp::NOTE_KEYS || !keyDown[note]) return;
    keyDown[note] = false;
    if (--keysHeld > 0) return;
    shared.noteOff();
    global.noteOff();
  }

  /** The newest voice still sounding (the one the latest note started on, else the first sounding), or −1. */
  int sounding() const noexcept {
    if (newest >= 0 && active[newest]) return newest;
    for (int v = 0; v < dsp::MAX_VOICES; v++) if (active[v]) return v;
    return -1;
  }

  double allocationValue(int index, double fallback) const noexcept { return index >= 0 && index < ParamCount ? values[index] : fallback; }

  /** Where the note sources' notes go: the voices, as a keyboard's would. */
  struct Sink final : dsp::NoteSink {
    explicit Sink(Synth* synth) : synth(synth) {}
    void noteOn(int note, double velocity) noexcept override { synth->noteOn(note, velocity); }
    void noteOff(int note) noexcept override { synth->noteOff(note); }
    Synth* synth;
  };

  const ParamSpec* params;
  const unsigned char* paramStages;  // per param, the classes it reaches (STAGE_*)
  AllocationParams allocation;
  PortamentoParams portamento;
  PitchShiftParams pitchShift;
  TuningParams tuningParams;
  UnisonParams unison;
  MpeParams mpeParams;
  MidiInputParams midiInputParams;
  const dsp::KfoTables& kfo;
  dsp::KfoHost kfoHost;
  double values[ParamCount > 0 ? ParamCount : 1] = {};
  int maxBlock = 128;
  double bend = 0;
  dsp::VoiceAllocator allocator;
  Voice voices[dsp::MAX_VOICES];
  dsp::Portamento glides[dsp::MAX_VOICES];  // each voice's pitch: its note, or a glide to it
  dsp::GlideMemory glideMemory;              // what the glides share: the last note, and how many keys are down
  double glideSettings[4] = {dsp::PORTAMENTO_TIME_DEFAULT, dsp::PORTAMENTO_CURVE_DEFAULT, dsp::PORTAMENTO_TIME_MODE_DEFAULT,
                             dsp::PORTAMENTO_GLIDE_MODE_DEFAULT};  // Time, Curve, Time mode, Glide mode, as last set
  dsp::PitchShift shift;                     // every voice's note moved by whole semitones, through its glide (note.pitch-shift)
  double shiftSettings[3] = {0, dsp::PITCH_SHIFT_DIRECTION_DEFAULT, 0};  // Semitones, Direction, On, as last set
  dsp::Tuning tuning;                        // where every voice's pitch sits: Transpose, Fine and A4 (voice.tuning)
  double tuningSettings[3] = {dsp::TUNING_A4_DEFAULT, 0, 0};  // A4, Transpose, Fine, as last set
  dsp::Unison unisonModel;                   // the copies every voice's oscillators play
  double unisonSettings[3] = {1, 15, 0};     // Unison, Detune, Detune curve, as last set (voice.unison's defaults)
  dsp::Mpe mpe;                              // MPE's zone, channels and bend ranges (off until note.mpe turns it on)
  dsp::MpeNote mpeNotes[dsp::MAX_VOICES];    // each voice's own bend, from its note's channel
  double mpeSettings[6] = {0, dsp::MPE_LOWER, dsp::MPE_MEMBERS_MAX, dsp::MPE_NOTE_BEND_DEFAULT, dsp::MPE_MASTER_BEND_DEFAULT, 0};  // MPE on … Glide, as last set
  dsp::MidiInput midiInput;                  // which channel the mod wheel (CC 1) is taken from (Omni until note.midi-input sets it)
  int voiceChannel[dsp::MAX_VOICES] = {};    // the MPE member channel each voice's note came on, 0 for none
  int keyChannel[dsp::NOTE_KEYS] = {};       // the MPE member channel each key was last played on, 0 for none
  int incomingChannel = 0;                   // the member channel of the note-on or note-off being played now, 0 for none
  bool active[dsp::MAX_VOICES] = {};
  int voiceKey[dsp::MAX_VOICES] = {};  // the key each voice plays now, for poly pressure
  double velocities[dsp::NOTE_KEYS] = {};  // each key's last velocity, for a voice that falls back to it
  int newest = -1;  // the voice the latest note started on
  bool keyDown[dsp::NOTE_KEYS] = {};  // the keys held, for the envelopes in Shared and Global
  bool ducked[16][dsp::NOTE_KEYS] = {};  // per channel, the keys held that a Duck took (they play no voice)
  int keysHeld = 0;
  long long noteCount = 0;  // the notes played so far: the next one's number for the round robins (Voice::setStep)
  Shared shared;
  VoiceRoutes routes;  // the voices' routing, held once
  Global global;
  Notes notes;
  Sink sink{this};
  std::vector<float> busData;
  float* buses[BusChannels > 0 ? BusChannels : 1] = {};
  std::vector<float> tapData;
  float* taps[TapChannels > 0 ? TapChannels : 1] = {};
  float tapHeld[TapChannels > 0 ? TapChannels : 1] = {};  // each tap's last value, held while no voice sounds
  float scopeRing[SCOPE] = {};
  int scopeAt = 0;
  std::vector<float> spectrumData;                    // this block's spectra, before they go into the rings
  float* spectrumNow[Spectra > 0 ? Spectra : 1] = {};
  std::vector<float> spectrumRing;                    // each spectrum's newest SPECTRUM_RING samples
  int spectrumAt = 0;
};

}  // namespace lemnos
