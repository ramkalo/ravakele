/**
 * Portamento: a voice's pitch glides from where it was to the note it was given.
 *
 * One Portamento per voice; it owns the voice's base pitch, in semitones (MIDI note numbers, fractional while
 * gliding). The host adds pitch bend, pitch LFOs and tuning on top of pitch(), so they never glide.
 *
 * Time — how long a glide takes, in seconds. Time mode says how it is read:
 *   Constant time   every glide takes `time`, whatever the interval.
 *   Constant rate   `time` is per octave: a fifth takes 7/12 of it, two octaves twice it.
 * Time 0 is no glide: the pitch lands on the note.
 *
 * Curve — the glide's shape, −1 (fast start, slow finish) … 0 (linear) … +1 (slow start, fast finish), in semitones.
 * Progress p (0..1) is bent as (e^{k·p} − 1)/(e^k − 1) with k = 6·curve.
 *
 * Glide mode — where a glide starts. GlideMemory, one per engine and shared by every voice, remembers the last note
 * played and how many keys are down:
 *   Always      from the last note played (any voice); from this voice's current pitch if it played that note itself,
 *               so a mono retrigger mid-glide carries on from where it is.
 *   Legato      as Always, but only while another key is held; a detached note starts on its pitch.
 *   Per voice   from this voice's own current pitch, whatever it last played.
 * With nothing to start from (the first note), a note starts on its pitch.
 *
 * retarget() moves a sounding voice to a new pitch with no key going down (a pitch shift turning on or off): it glides
 * from where the pitch is now, at the same Time, Curve and Time mode, whatever the glide mode; time 0 lands at once.
 *
 * Time mode and glide mode are module settings; a portamento pad drives only time and curve.
 */
#pragma once

#include <cmath>
#include <optional>

#include "../util/math.hpp"

namespace dsp {

/** The longest a glide (or, at constant rate, an octave of one) may take, in seconds. */
constexpr double PORTAMENTO_TIME_MAX = 1;
constexpr double PORTAMENTO_TIME_DEFAULT = 0;
constexpr double PORTAMENTO_CURVE_DEFAULT = 0;
/** How far the curve bends a glide; identical to BEND in env/envelopeCurves.hpp. */
constexpr double PORTAMENTO_CURVE_BEND = 6;

constexpr int TIME_MODE_CONSTANT_TIME = 0;
constexpr int TIME_MODE_CONSTANT_RATE = 1;
constexpr const char* PORTAMENTO_TIME_MODE_LABELS[] = {"Constant time", "Constant rate"};
constexpr int PORTAMENTO_TIME_MODE_DEFAULT = TIME_MODE_CONSTANT_TIME;

constexpr int GLIDE_MODE_ALWAYS = 0;
constexpr int GLIDE_MODE_LEGATO = 1;
constexpr int GLIDE_MODE_PER_VOICE = 2;
constexpr const char* PORTAMENTO_GLIDE_MODE_LABELS[] = {"Always", "Legato", "Per voice"};
constexpr int PORTAMENTO_GLIDE_MODE_DEFAULT = GLIDE_MODE_ALWAYS;

/** A time mode param (0..1, rounded) as its index. */
inline int timeModeOf(double v) noexcept { return static_cast<int>(clamp(roundHalfUp(v), 0, 1)); }
/** A glide mode param (0..2, rounded) as its index. */
inline int glideModeOf(double v) noexcept { return static_cast<int>(clamp(roundHalfUp(v), 0, 2)); }

/** Progress through a glide (0..1) bent by `curve` (−1..+1): the envelope stages' formula. */
inline double portamentoCurve(double curve, double progress) noexcept {
  const double p = clamp(progress, 0, 1);
  const double k = clamp(curve, -1, 1) * PORTAMENTO_CURVE_BEND;
  if (k > -1e-4 && k < 1e-4) return p;
  return (std::exp(k * p) - 1) / (std::exp(k) - 1);
}

/** How long a glide over `semitones` takes, in seconds, for a time and time mode. */
inline double portamentoSeconds(double time, double timeMode, double semitones) noexcept {
  const double t = clamp(time, 0, PORTAMENTO_TIME_MAX);
  return timeModeOf(timeMode) == TIME_MODE_CONSTANT_RATE ? t * std::fabs(semitones) / 12 : t;
}

class Portamento;

/** What every voice's portamento shares: the last note played, which voice played it, and how many keys are down. */
struct GlideMemory {
  std::optional<double> lastNote;
  const Portamento* lastVoice = nullptr;
  int held = 0;

  /** A key went up (the host calls it on every note-off; Portamento::noteOn counts the key-downs). */
  void noteOff() noexcept { held = held - 1 > 0 ? held - 1 : 0; }

  void reset() noexcept {
    lastNote.reset();
    lastVoice = nullptr;
    held = 0;
  }
};

class Portamento {
 public:
  void prepare(double sampleRate) {
    this->sampleRate = sampleRate;
    reset();
  }

  /** Nothing played: the next note starts on its pitch. */
  void reset() noexcept {
    played = false;
    progress = 1;
    inc = 1;
  }

  void setParams(double time, double curve, double timeMode = PORTAMENTO_TIME_MODE_DEFAULT,
                 double glideMode = PORTAMENTO_GLIDE_MODE_DEFAULT) noexcept {
    this->time = clamp(time, 0, PORTAMENTO_TIME_MAX);
    this->curve = clamp(curve, -1, 1);
    this->timeMode = timeModeOf(timeMode);
    this->glideMode = glideModeOf(glideMode);
    // A glide under way keeps its progress and takes the new time for what is left.
    if (progress < 1) inc = stepFor(to - from);
  }

  /**
   * A note-on: picks where the glide starts by glide mode, then records the note (and the key-down) in `memory`.
   * Call it once per note-on, before the voice renders.
   */
  void noteOn(double note, GlideMemory& memory) noexcept {
    const std::optional<double> start = startFor(memory);
    memory.lastNote = note;
    memory.lastVoice = this;
    memory.held += 1;
    to = note;
    played = true;
    if (!start || *start == note) { from = note; progress = 1; inc = 1; return; }
    from = *start;
    inc = stepFor(note - *start);
    progress = inc >= 1 ? 1 : 0;
  }

  /**
   * A new pitch for the note already sounding (no key went down): glides there from where the pitch is now, at the
   * same time and curve; time 0 lands at once. If this voice played the last note, `memory` remembers the new pitch,
   * so the next glide starts from what was heard. Not a note-on: the held count is untouched.
   */
  void retarget(double note, GlideMemory& memory) noexcept {
    const double now = pitch();
    if (memory.lastVoice == this) memory.lastNote = note;
    to = note;
    if (time <= 0 || now == note) { from = note; progress = 1; inc = 1; return; }
    from = now;
    inc = stepFor(note - now);
    progress = inc >= 1 ? 1 : 0;
  }

  /** The pitch it is at now, in semitones. */
  double pitch() const noexcept { return progress >= 1 ? to : from + (to - from) * portamentoCurve(curve, progress); }

  /** Whether a glide is under way. */
  bool gliding() const noexcept { return progress < 1; }

  /** Steps n samples and returns the pitch landed on (semitones), for a host that sets pitch once a block. */
  double advance(int n) DSP_NONBLOCKING {
    if (progress < 1) progress = std::fmin(1, progress + inc * n);
    return pitch();
  }

  /** Writes n samples of pitch (semitones) from offset. */
  void render(float* out, int offset, int n) DSP_NONBLOCKING {
    for (int i = offset, end = offset + n; i < end; i++) {
      out[i] = static_cast<float>(pitch());
      if (progress < 1) progress = std::fmin(1, progress + inc);
    }
  }

 private:
  double sampleRate = 48000;
  double time = PORTAMENTO_TIME_DEFAULT;
  double curve = PORTAMENTO_CURVE_DEFAULT;
  int timeMode = PORTAMENTO_TIME_MODE_DEFAULT;
  int glideMode = PORTAMENTO_GLIDE_MODE_DEFAULT;
  /** Where the glide started and where it lands, in semitones; whether this voice has played anything. */
  double from = 60;
  double to = 60;
  bool played = false;
  /** Progress through the glide (0..1) and how far it moves per sample. */
  double progress = 1;
  double inc = 1;

  std::optional<double> startFor(const GlideMemory& memory) const noexcept {
    if (time <= 0) return std::nullopt;
    if (glideMode == GLIDE_MODE_PER_VOICE) return played ? std::optional<double>(pitch()) : std::nullopt;
    if (glideMode == GLIDE_MODE_LEGATO && memory.held <= 0) return std::nullopt;
    if (memory.lastVoice == this && played) return pitch();
    return memory.lastNote;
  }

  /** Progress per sample for a glide over `semitones`; 1 when it takes no time. */
  double stepFor(double semitones) const noexcept {
    const double samples = portamentoSeconds(time, timeMode, semitones) * sampleRate;
    return samples >= 1 ? 1 / samples : 1;
  }
};

}  // namespace dsp
