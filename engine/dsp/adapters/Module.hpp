/**
 * The one shape every signal module takes, so generated engine code drives any block the same way. Each module in
 * library/parts/dsp that carries signals has a wrapper in this folder, named by its part id (amp.vca.hpp → AmpVca).
 * A wrapper declares, in its manifest's order:
 *
 *   ID              the part id
 *   PARAMS[]        every declared param, {id, default}; setParam(index, …) takes these indexes
 *   PORTS[]         the manifest's signal ports (all but `params` and the event ports), {id, direction, kind, rate,
 *                   channels}; io and setInput take these indexes
 *
 * and implements
 *
 *   prepare(sampleRate, maxBlock)   off the audio thread; offset + n never exceeds maxBlock in process()
 *   reset()                         jumps to the params set so far and clears the state
 *   setParam(index, value)          in the manifest's units; applied at the next process()
 *   setInput(port, value)           a block-rate input port's value (a pitch in Hz, an envelope's level, a tempo)
 *   noteOn(note) / noteOff()        the event ports
 *   process(io, offset, n)          reads its input ports, writes its output ports, from offset for n samples
 *   finished()                      false while it needs its voice kept (an envelope still sounding)
 *
 * io.ports[port][channel] is a sample-rate buffer, or null when the port is not wired. Inputs are only read; an
 * unwired audio input is silence. An unwired output is not computed, except by a source that keeps time (an envelope,
 * an LFO), which still advances.
 */
#pragma once

#include <algorithm>

#include "../util/math.hpp"

namespace dsp {
namespace adapter {

enum class Direction { In, Out };
enum class Kind { Audio, Control, Clock };
enum class Rate { Sample, Block };

struct PortInfo {
  const char* id;
  Direction direction;
  Kind kind;
  Rate rate;
  int channels;
};

struct ParamInfo {
  const char* id;
  double value;
};

/** A note starting, as every wrapper sees it. */
struct Note {
  double note = 60;       // MIDI note number, fractional while gliding
  double velocity = 1;    // 0..1
  bool sounding = false;  // the voice was already sounding (legato, or a stolen voice)
};

/** The buffers of one process() call: ports[port][channel], null for an unwired port. */
struct Io {
  float* const* const* ports;

  float* channel(int port, int ch) const noexcept { return ports[port] ? ports[port][ch] : nullptr; }
  bool wired(int port) const noexcept { return ports[port] != nullptr; }
};

/** Copies an input channel into the output it is processed in place in; an unwired input gives silence. */
inline void copyIn(const float* in, float* out, int offset, int n) noexcept {
  if (in == out) return;
  if (in) std::copy(in + offset, in + offset + n, out + offset);
  else std::fill(out + offset, out + offset + n, 0.0f);
}

/** What every wrapper shares: its param values, starting at the manifest defaults, and the no-op defaults. */
template <class Self, int ParamCount>
class Wrapper {
 public:
  static constexpr int PARAM_COUNT = ParamCount;

  Wrapper() noexcept {
    for (int i = 0; i < ParamCount; i++) values[i] = Self::PARAMS[i].value;
  }

  void setParam(int index, double value) noexcept {
    if (index < 0 || index >= ParamCount || values[index] == value) return;
    values[index] = value;
    dirty = true;
  }

  double param(int index) const noexcept { return values[index]; }

  void setInput(int /*port*/, double /*value*/) noexcept {}
  void noteOn(const Note& /*note*/) noexcept {}
  void noteOff() noexcept {}
  bool finished() const noexcept { return true; }

 protected:
  /** True once after any param changed, so a wrapper pushes its params to the module only then. */
  bool takeDirty() noexcept {
    const bool was = dirty;
    dirty = false;
    return was;
  }

  double values[ParamCount > 0 ? ParamCount : 1] = {};
  bool dirty = true;
};

}  // namespace adapter
}  // namespace dsp
