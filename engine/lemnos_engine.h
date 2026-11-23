/**
 * The engine's C interface: what any host (the browser's AudioWorklet, a CLAP plugin, a standalone app) calls. One
 * engine is one synth built from a Lemnos project: its blocks and arrows compiled into Engine.hpp by Lemnos's engine
 * generator (src/engine/generate.js), on the fixed runtime in runtime.hpp.
 *
 * Parameters are addressed `DSP###.<param>` (the block's address and the param's id) and set in their manifest units.
 * lemnos_prepare runs off the audio thread; everything else may run on it, except the state calls.
 */
#ifndef LEMNOS_ENGINE_H
#define LEMNOS_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lemnos_engine lemnos_engine;

lemnos_engine* lemnos_create(void);
void lemnos_destroy(lemnos_engine* engine);
/** Sizes every buffer: process calls may then pass any number of samples. */
void lemnos_prepare(lemnos_engine* engine, double sample_rate, int max_block);

int lemnos_param_count(void);
/** The param at index: its address, range and default. */
const char* lemnos_param_address(int index);
double lemnos_param_min(int index);
double lemnos_param_max(int index);
double lemnos_param_default(int index);
/** The index of an address, or -1. */
int lemnos_param_index(const char* address);
void lemnos_set_param(lemnos_engine* engine, int index, double value);
double lemnos_get_param(const lemnos_engine* engine, int index);

void lemnos_note_on(lemnos_engine* engine, int note, double velocity);
void lemnos_note_off(lemnos_engine* engine, int note);
/** A note on or off on a MIDI channel (1..16), so a Duck (mod.duck) can take it; lemnos_note_on/off are channel 1. */
void lemnos_note_on_channel(lemnos_engine* engine, int channel, int note, double velocity);
void lemnos_note_off_channel(lemnos_engine* engine, int channel, int note);
void lemnos_all_notes_off(lemnos_engine* engine);
void lemnos_pitch_bend(lemnos_engine* engine, double semitones);
/**
 * One MIDI message: note on/off, pitch bend (±2 semitones), channel and poly pressure, the mod wheel (CC 1, on
 * note.midi-input's Mod wheel channel), all notes off (CC 120, 123). Others are ignored.
 */
void lemnos_midi(lemnos_engine* engine, const unsigned char* bytes, int length);
/** A mouse scroll of `travel` px, up positive: every Mod wheel (mod.wheel) moves by its share of its range. */
void lemnos_wheel(lemnos_engine* engine, double travel);

/** Renders n samples of stereo into left and right (overwriting). */
void lemnos_process(lemnos_engine* engine, float* left, float* right, int n);

/** The last n samples of the output (left and right summed to mono), oldest first, for a scope; n ≤ 4096. */
void lemnos_scope(const lemnos_engine* engine, float* out, int n);

/**
 * The KFOs and SOLOKFOs (motion.kfo). Each is a player addressed `KFO.<DSP###>` (a KFO linked to a block),
 * `KFO.<PAGE###.C###.K###>` (a free one) or `SOLO.<handle>` (a handle's SOLOKFO); the page sets their play, slot and
 * recording as params and sends each take here. A take is `count` points (x, y pairs in 0…1, 50 a second, at most
 * 1000) into slot 1…3; fewer than two empty the slot. Call it on the audio thread, or while nothing is processing.
 */
int lemnos_motion_count(void);
const char* lemnos_motion_address(int player);
/** The index of a player's address, or -1. */
int lemnos_motion_index(const char* address);
void lemnos_set_motion(lemnos_engine* engine, int player, int slot, const float* xy, int count);

/**
 * What the engine plays now, for the page to draw: lemnos_watch_count values, each named by lemnos_watch_address —
 * every KFO player's `<player>.x`, `.y`, `.playing`, then every handle's `HANDLE.<handle>.x`, `.y`, `.driven` (1 while a
 * player moves it), then what the pads show: `LFO.<block>.phase`, `ENV.<block>.stage` and `.progress`, `SEQ.<block>.step`
 * (−1 when nothing runs; src/shared/pad-watch.js). lemnos_watch writes at most capacity of them and returns how many it
 * wrote.
 */
int lemnos_watch_count(void);
const char* lemnos_watch_address(int index);
int lemnos_watch(const lemnos_engine* engine, float* out, int capacity);

/**
 * What the filter pads, the waterfall graphs, the oscilloscopes and the tuners draw: lemnos_spectrum_count signals, each named by
 * lemnos_spectrum_address — `FILTER.<block>`, the output of a filter a filter pad shows, `WATERFALL.<block>`, the
 * Thru output of a Waterfall block (util.waterfall) a waterfall graph shows, `SCOPE.<block>`, the Thru output of a
 * Scope block (util.scope) an oscilloscope shows, or `TUNER.<block>`, the Thru output of a Tuner block (util.tuner) a tuner
 * shows; every sounding voice's summed (left and right as their mean). Only
 * blocks a pad, graph, scope or tuner shows are kept (src/shared/pad-watch.js). lemnos_spectrum writes the
 * last n samples (n ≤ 4096) of signal `index`, oldest first, for the display to take its spectrum of.
 */
int lemnos_spectrum_count(void);
const char* lemnos_spectrum_address(int index);
void lemnos_spectrum(const lemnos_engine* engine, int index, float* out, int n);

/** Every param as JSON, {"DSP001.cutoff": 1200, …}; writes at most capacity bytes and returns the full length. */
int lemnos_get_state(const lemnos_engine* engine, char* out, int capacity);
/** Sets every param the JSON names (unknown addresses are skipped); returns how many were set, or -1 if it is not JSON. */
int lemnos_set_state(lemnos_engine* engine, const char* json);

#ifdef __cplusplus
}
#endif

#endif
