/**
 * Ravakele as a CLAP instrument, on the engine's C interface (engine/lemnos_engine.h). Every engine param is a CLAP
 * param (its id is the engine's index, its name the param's address); notes come as CLAP notes or MIDI; the output
 * is one stereo port; the plugin's state is the engine's preset JSON. Events are applied at their sample.
 *
 * Written against CLAP 1.x (github.com/free-audio/clap). Lemnos could not compile it (it does not ship the CLAP
 * headers): see BUILD.md.
 */
#include <clap/clap.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "lemnos_engine.h"

namespace {

struct Plugin {
  clap_plugin_t plugin;
  const clap_host_t* host;
  lemnos_engine* engine;
};

const char* const FEATURES[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};

const clap_plugin_descriptor_t DESCRIPTOR = {
    CLAP_VERSION_INIT, "lemnos.ravakele", "Ravakele", "Lemnos export", "", "", "", "1.0.0", "A synth exported from Lemnos.", FEATURES,
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

// ---- params ----

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(lemnos_param_count()); }

bool paramsInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
  if (static_cast<int>(index) >= lemnos_param_count()) return false;
  std::memset(info, 0, sizeof *info);
  info->id = index;
  info->flags = CLAP_PARAM_IS_AUTOMATABLE;
  std::snprintf(info->name, sizeof info->name, "%s", lemnos_param_address(static_cast<int>(index)));
  info->min_value = lemnos_param_min(static_cast<int>(index));
  info->max_value = lemnos_param_max(static_cast<int>(index));
  info->default_value = lemnos_param_default(static_cast<int>(index));
  return true;
}

bool paramsValue(const clap_plugin_t* plugin, clap_id id, double* value) {
  if (static_cast<int>(id) >= lemnos_param_count()) return false;
  *value = lemnos_get_param(self(plugin)->engine, static_cast<int>(id));
  return true;
}

bool paramsToText(const clap_plugin_t*, clap_id, double value, char* out, uint32_t size) {
  std::snprintf(out, size, "%.4g", value);
  return true;
}

bool paramsFromText(const clap_plugin_t*, clap_id, const char* text, double* value) {
  char* end = nullptr;
  *value = std::strtod(text, &end);
  return end != text;
}

void applyEvent(Plugin* p, const clap_event_header_t* event) {
  if (event->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
  switch (event->type) {
    case CLAP_EVENT_NOTE_ON: { auto* e = reinterpret_cast<const clap_event_note_t*>(event); lemnos_note_on_channel(p->engine, e->channel + 1, e->key, e->velocity); break; }
    case CLAP_EVENT_NOTE_OFF: { auto* e = reinterpret_cast<const clap_event_note_t*>(event); lemnos_note_off_channel(p->engine, e->channel + 1, e->key); break; }
    case CLAP_EVENT_NOTE_CHOKE: lemnos_all_notes_off(p->engine); break;
    case CLAP_EVENT_PARAM_VALUE: { auto* e = reinterpret_cast<const clap_event_param_value_t*>(event); lemnos_set_param(p->engine, static_cast<int>(e->param_id), e->value); break; }
    case CLAP_EVENT_MIDI: { auto* e = reinterpret_cast<const clap_event_midi_t*>(event); lemnos_midi(p->engine, e->data, 3); break; }
    default: break;
  }
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) {
  for (uint32_t i = 0, n = in->size(in); i < n; i++) applyEvent(self(plugin), in->get(in, i));
}

const clap_plugin_params_t PARAMS = {paramsCount, paramsInfo, paramsValue, paramsToText, paramsFromText, paramsFlush};

// ---- ports ----

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 0 : 1; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
  if (isInput || index != 0) return false;
  std::memset(info, 0, sizeof *info);
  info->id = 0;
  std::snprintf(info->name, sizeof info->name, "Out");
  info->channel_count = 2;
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = CLAP_INVALID_ID;
  return true;
}

const clap_plugin_audio_ports_t AUDIO_PORTS = {audioPortsCount, audioPortsGet};

uint32_t notePortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 1 : 0; }

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_note_port_info_t* info) {
  if (!isInput || index != 0) return false;
  std::memset(info, 0, sizeof *info);
  info->id = 0;
  info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
  info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
  std::snprintf(info->name, sizeof info->name, "Notes");
  return true;
}

const clap_plugin_note_ports_t NOTE_PORTS = {notePortsCount, notePortsGet};

// ---- state: the engine's preset JSON ----

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream) {
  const int length = lemnos_get_state(self(plugin)->engine, nullptr, 0);
  std::string json(static_cast<size_t>(length) + 1, '\0');
  lemnos_get_state(self(plugin)->engine, json.data(), length + 1);
  for (int done = 0; done < length;) {
    const int64_t wrote = stream->write(stream, json.data() + done, static_cast<uint64_t>(length - done));
    if (wrote <= 0) return false;
    done += static_cast<int>(wrote);
  }
  return true;
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream) {
  std::string json;
  char buffer[4096];
  for (int64_t read; (read = stream->read(stream, buffer, sizeof buffer)) > 0;) json.append(buffer, static_cast<size_t>(read));
  return lemnos_set_state(self(plugin)->engine, json.c_str()) >= 0;
}

const clap_plugin_state_t STATE = {stateSave, stateLoad};

// ---- the plugin ----

bool init(const clap_plugin_t* plugin) { self(plugin)->engine = lemnos_create(); return true; }
void destroy(const clap_plugin_t* plugin) { lemnos_destroy(self(plugin)->engine); delete self(plugin); }
bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t maxFrames) {
  lemnos_prepare(self(plugin)->engine, sampleRate, static_cast<int>(maxFrames ? maxFrames : 512));
  return true;
}
void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin) { lemnos_all_notes_off(self(plugin)->engine); }

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc) {
  Plugin* p = self(plugin);
  float* left = proc->audio_outputs[0].data32[0];
  float* right = proc->audio_outputs[0].data32[1];
  const uint32_t frames = proc->frames_count, events = proc->in_events->size(proc->in_events);
  uint32_t done = 0;
  for (uint32_t i = 0; i < events; i++) {
    const clap_event_header_t* event = proc->in_events->get(proc->in_events, i);
    const uint32_t at = event->time < frames ? event->time : frames;
    if (at > done) { lemnos_process(p->engine, left + done, right + done, static_cast<int>(at - done)); done = at; }
    applyEvent(p, event);
  }
  if (frames > done) lemnos_process(p->engine, left + done, right + done, static_cast<int>(frames - done));
  return CLAP_PROCESS_CONTINUE;
}

const void* getExtension(const clap_plugin_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &PARAMS;
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &AUDIO_PORTS;
  if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return &NOTE_PORTS;
  if (!std::strcmp(id, CLAP_EXT_STATE)) return &STATE;
  return nullptr;
}

void onMainThread(const clap_plugin_t*) {}

// ---- the factory and entry ----

uint32_t factoryCount(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* factoryDescriptor(const clap_plugin_factory_t*, uint32_t index) { return index == 0 ? &DESCRIPTOR : nullptr; }

const clap_plugin_t* factoryCreate(const clap_plugin_factory_t*, const clap_host_t* host, const char* pluginId) {
  if (!clap_version_is_compatible(host->clap_version) || std::strcmp(pluginId, DESCRIPTOR.id)) return nullptr;
  Plugin* p = new Plugin();
  p->host = host;
  p->plugin = {&DESCRIPTOR, p, init, destroy, activate, deactivate, startProcessing, stopProcessing, reset, process, getExtension, onMainThread};
  return &p->plugin;
}

const clap_plugin_factory_t FACTORY = {factoryCount, factoryDescriptor, factoryCreate};

bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryFactory(const char* id) { return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? nullptr : &FACTORY; }

}  // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, entryInit, entryDeinit, entryFactory};
