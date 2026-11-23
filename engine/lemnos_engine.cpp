/**
 * The C interface (lemnos_engine.h) over the generated engine: Engine.hpp, which Lemnos writes for each project, on
 * the runtime in runtime.hpp. Compile this one file with -I pointing at Engine.hpp's folder, this folder, and the DSP
 * library's src.
 */
#include "lemnos_engine.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "Engine.hpp"
#include "runtime.hpp"

using Engine = lemnos::Synth<lemnos::generated::Shared, lemnos::generated::VoiceRoutes, lemnos::generated::Voice, lemnos::generated::Global,
                             lemnos::generated::Notes, lemnos::generated::PARAM_COUNT, lemnos::generated::BUS_CHANNELS,
                             lemnos::generated::TAP_CHANNELS, lemnos::generated::PAD_WATCH_COUNT, lemnos::generated::SPECTRUM_COUNT>;

struct lemnos_engine {
  Engine synth{lemnos::generated::PARAMS, lemnos::generated::PARAM_STAGE, lemnos::AllocationParams{lemnos::generated::POLYPHONY_PARAM, lemnos::generated::PRIORITY_PARAM, lemnos::generated::TRIGGER_PARAM},
               lemnos::PortamentoParams{lemnos::generated::PORTAMENTO_TIME_PARAM, lemnos::generated::PORTAMENTO_CURVE_PARAM,
                                        lemnos::generated::PORTAMENTO_TIME_MODE_PARAM, lemnos::generated::PORTAMENTO_GLIDE_MODE_PARAM},
               lemnos::PitchShiftParams{lemnos::generated::PITCH_SHIFT_SEMITONES_PARAM, lemnos::generated::PITCH_SHIFT_DIRECTION_PARAM, lemnos::generated::PITCH_SHIFT_ON_PARAM},
               lemnos::TuningParams{lemnos::generated::TUNING_A4_PARAM, lemnos::generated::TUNING_TRANSPOSE_PARAM, lemnos::generated::TUNING_FINE_PARAM},
               lemnos::UnisonParams{lemnos::generated::UNISON_COUNT_PARAM, lemnos::generated::UNISON_DETUNE_PARAM, lemnos::generated::UNISON_CURVE_PARAM},
               lemnos::MpeParams{lemnos::generated::MPE_ENABLED_PARAM, lemnos::generated::MPE_ZONE_PARAM, lemnos::generated::MPE_MEMBERS_PARAM,
                                 lemnos::generated::MPE_NOTE_BEND_PARAM, lemnos::generated::MPE_MASTER_BEND_PARAM, lemnos::generated::MPE_GLIDE_PARAM},
               lemnos::MidiInputParams{lemnos::generated::MOD_WHEEL_CHANNEL_PARAM},
               lemnos::generated::KFO};
};

namespace {

bool validIndex(int index) { return index >= 0 && index < lemnos::generated::PARAM_COUNT; }

void skipSpace(const char*& p) {
  while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
}

/** A JSON string at p (after its opening quote), with simple escapes; false if it does not end. */
bool readString(const char*& p, std::string& out) {
  out.clear();
  for (; *p && *p != '"'; p++) {
    if (*p == '\\' && p[1]) p++;
    out += *p;
  }
  if (*p != '"') return false;
  p++;
  return true;
}

}  // namespace

extern "C" {

lemnos_engine* lemnos_create(void) { return new lemnos_engine(); }
void lemnos_destroy(lemnos_engine* engine) { delete engine; }
void lemnos_prepare(lemnos_engine* engine, double sample_rate, int max_block) { engine->synth.prepare(sample_rate, max_block); }

int lemnos_param_count(void) { return lemnos::generated::PARAM_COUNT; }
const char* lemnos_param_address(int index) { return validIndex(index) ? lemnos::generated::PARAMS[index].address : ""; }
double lemnos_param_min(int index) { return validIndex(index) ? lemnos::generated::PARAMS[index].min : 0; }
double lemnos_param_max(int index) { return validIndex(index) ? lemnos::generated::PARAMS[index].max : 0; }
double lemnos_param_default(int index) { return validIndex(index) ? lemnos::generated::PARAMS[index].value : 0; }

int lemnos_param_index(const char* address) {
  if (!address) return -1;
  for (int i = 0; i < lemnos::generated::PARAM_COUNT; i++)
    if (std::strcmp(lemnos::generated::PARAMS[i].address, address) == 0) return i;
  return -1;
}

void lemnos_set_param(lemnos_engine* engine, int index, double value) { engine->synth.setParam(index, value); }
double lemnos_get_param(const lemnos_engine* engine, int index) { return engine->synth.param(index); }

void lemnos_note_on(lemnos_engine* engine, int note, double velocity) { engine->synth.noteOn(note, velocity); }
void lemnos_note_off(lemnos_engine* engine, int note) { engine->synth.noteOff(note); }
void lemnos_note_on_channel(lemnos_engine* engine, int channel, int note, double velocity) { engine->synth.noteOnFrom(channel, note, velocity); }
void lemnos_note_off_channel(lemnos_engine* engine, int channel, int note) { engine->synth.noteOffFrom(channel, note); }
void lemnos_all_notes_off(lemnos_engine* engine) { engine->synth.allNotesOff(); }
void lemnos_pitch_bend(lemnos_engine* engine, double semitones) { engine->synth.pitchBend(semitones); }
void lemnos_midi(lemnos_engine* engine, const unsigned char* bytes, int length) { engine->synth.midi(bytes, length); }
void lemnos_wheel(lemnos_engine* engine, double travel) { engine->synth.wheel(travel); }

void lemnos_process(lemnos_engine* engine, float* left, float* right, int n) { engine->synth.process(left, right, n); }
void lemnos_scope(const lemnos_engine* engine, float* out, int n) { engine->synth.scope(out, n); }

int lemnos_motion_count(void) { return lemnos::generated::KFO_PLAYER_COUNT; }
const char* lemnos_motion_address(int player) { return player >= 0 && player < lemnos::generated::KFO_PLAYER_COUNT ? lemnos::generated::KFO_PLAYER_ADDRESSES[player] : ""; }
int lemnos_motion_index(const char* address) {
  if (!address) return -1;
  for (int i = 0; i < lemnos::generated::KFO_PLAYER_COUNT; i++)
    if (std::strcmp(lemnos::generated::KFO_PLAYER_ADDRESSES[i], address) == 0) return i;
  return -1;
}
void lemnos_set_motion(lemnos_engine* engine, int player, int slot, const float* xy, int count) { engine->synth.setMotion(player, slot, xy, count); }

int lemnos_watch_count(void) { return lemnos::generated::WATCH_COUNT; }
const char* lemnos_watch_address(int index) { return index >= 0 && index < lemnos::generated::WATCH_COUNT ? lemnos::generated::WATCH[index] : ""; }
int lemnos_watch(const lemnos_engine* engine, float* out, int capacity) { return engine->synth.watch(out, capacity); }

int lemnos_spectrum_count(void) { return lemnos::generated::SPECTRUM_COUNT; }
const char* lemnos_spectrum_address(int index) { return index >= 0 && index < lemnos::generated::SPECTRUM_COUNT ? lemnos::generated::SPECTRA[index] : ""; }
void lemnos_spectrum(const lemnos_engine* engine, int index, float* out, int n) { engine->synth.spectrum(index, out, n); }

int lemnos_get_state(const lemnos_engine* engine, char* out, int capacity) {
  std::string json = "{";
  char number[40];
  for (int i = 0; i < lemnos::generated::PARAM_COUNT; i++) {
    std::snprintf(number, sizeof number, "%.17g", engine->synth.param(i));
    json += (i ? ",\"" : "\"") + std::string(lemnos::generated::PARAMS[i].address) + "\":" + number;
  }
  json += "}";
  if (out && capacity > 0) {
    const std::size_t n = std::min(json.size(), static_cast<std::size_t>(capacity - 1));
    std::memcpy(out, json.data(), n);
    out[n] = '\0';
  }
  return static_cast<int>(json.size());
}

int lemnos_set_state(lemnos_engine* engine, const char* json) {
  if (!json) return -1;
  const char* p = json;
  skipSpace(p);
  if (*p++ != '{') return -1;
  int set = 0;
  std::string key;
  for (;;) {
    skipSpace(p);
    if (*p == '}') return set;
    if (*p++ != '"' || !readString(p, key)) return -1;
    skipSpace(p);
    if (*p++ != ':') return -1;
    char* end = nullptr;
    const double value = std::strtod(p, &end);
    if (end == p) return -1;
    p = end;
    const int index = lemnos_param_index(key.c_str());
    if (index >= 0) {
      engine->synth.setParam(index, value);
      set++;
    }
    skipSpace(p);
    if (*p == ',') p++;
    else if (*p != '}') return -1;
  }
}

}  // extern "C"
