/**
 * MPE (MIDI Polyphonic Expression): each note on its own channel, so pitch bend, slide (CC 74) and pressure belong to
 * that note alone.
 *
 * Zone and channels — MPE on, a Lower zone (manager channel 1, members 2 upward) or an Upper zone (manager channel 16,
 *   members 15 downward), and how many member channels (1..15). A note on a member channel is an MPE note; messages on
 *   the manager channel apply to every note in the zone. With MPE off nothing is a member and the input is ordinary.
 * Bend ranges — a member channel's pitch bend sweeps ±Per-note bend semitones (48 by default, the MPE default); the
 *   manager channel's ±Master bend (2).
 * Slide (CC 74) — through the response curve (util/responseCurve.hpp) onto Slide min..max. Absolute: x = CC / 127.
 *   Relative: x = 0.5 + (CC − the note's first CC) / 254, so a note starts in the middle wherever the finger lands.
 * Pressure — channel pressure on a member channel through the response curve onto Pressure min..max, smoothed by
 *   Pressure smoothing ms.
 * Glide — per-note pitch bend is smoothed by Glide ms, so a stepped controller slides.
 * Release velocity — whether the note-off's velocity reaches the voice (for a release that follows how fast a key
 *   lifts).
 *
 * The engine owns one Mpe (the settings and the channel arithmetic); each voice owns one MpeNote, started with
 * noteOn(mpe), fed bend(), slide() and pressure() from its channel, and advanced once per block. Nothing allocates.
 */
#pragma once

#include "../util/blockSmoother.hpp"
#include "../util/math.hpp"
#include "../util/responseCurve.hpp"

namespace dsp {

constexpr int MPE_LOWER = 0;
constexpr int MPE_UPPER = 1;
constexpr const char* MPE_ZONE_LABELS[] = {"Lower", "Upper"};
constexpr int MPE_SLIDE_ABSOLUTE = 0;
constexpr int MPE_SLIDE_RELATIVE = 1;
constexpr const char* MPE_SLIDE_MODE_LABELS[] = {"Absolute", "Relative"};
constexpr int MPE_MEMBERS_MAX = 15;
constexpr double MPE_BEND_MAX = 96;
constexpr double MPE_NOTE_BEND_DEFAULT = 48;
constexpr double MPE_MASTER_BEND_DEFAULT = 2;
constexpr double MPE_SMOOTHING_MAX = 500;

/** A response curve's three settings. */
struct MpeCurve {
  double bend = 0;
  double min = 0;
  double max = 1;
  double at(double x) const noexcept { return responseCurve(x, bend, min, max); }
};

class Mpe {
 public:
  /** on 0/1, zone 0..1, members 1..15 (each rounded); bend ranges in semitones. */
  void setZone(double on, double zoneValue, double members, double noteBendRange, double masterBendRange) noexcept {
    enabled = on >= 0.5;
    zone = static_cast<int>(clamp(roundHalfUp(zoneValue), 0, 1));
    memberCount = static_cast<int>(clamp(roundHalfUp(members), 1, MPE_MEMBERS_MAX));
    noteBend = clamp(noteBendRange, 0, MPE_BEND_MAX);
    masterBend = clamp(masterBendRange, 0, MPE_BEND_MAX);
  }

  /** Slide's curve and mode (0 Absolute, 1 Relative). */
  void setSlide(double bend, double min, double max, double mode) noexcept {
    slideCurve = {clamp(bend, -1, 1), clamp(min, 0, 1), clamp(max, 0, 1)};
    slideMode = static_cast<int>(clamp(roundHalfUp(mode), 0, 1));
  }

  /** Pressure's curve and smoothing ms. */
  void setPressure(double bend, double min, double max, double smoothingMs) noexcept {
    pressureCurve = {clamp(bend, -1, 1), clamp(min, 0, 1), clamp(max, 0, 1)};
    pressureMs = clamp(smoothingMs, 0, MPE_SMOOTHING_MAX);
  }

  /** Glide ms for per-note bend, and whether note-off velocity is used (0/1). */
  void setNote(double glide, double releaseVelocity) noexcept {
    glideMs = clamp(glide, 0, MPE_SMOOTHING_MAX);
    useRelease = releaseVelocity >= 0.5;
  }

  bool on() const noexcept { return enabled; }
  int members() const noexcept { return memberCount; }
  /** The zone's manager channel: 1 (Lower) or 16 (Upper). */
  int managerChannel() const noexcept { return zone == MPE_UPPER ? 16 : 1; }
  bool isManager(int channel) const noexcept { return enabled && channel == managerChannel(); }
  /** Whether a channel (1..16) is one of the zone's member channels. */
  bool isMember(int channel) const noexcept {
    if (!enabled) return false;
    return zone == MPE_UPPER ? channel <= 15 && channel >= 16 - memberCount : channel >= 2 && channel <= 1 + memberCount;
  }

  /** A 14-bit pitch bend (0..16383, centre 8192) on a member channel, in semitones. */
  double noteBendSemitones(double raw) const noexcept { return bendOf(raw) * noteBend; }
  /** A 14-bit pitch bend on the manager channel, in semitones. */
  double masterBendSemitones(double raw) const noexcept { return bendOf(raw) * masterBend; }

  /** A slide CC (0..127) for a note whose first slide CC was `first`, on Slide's curve. */
  double slide(double raw, double first) const noexcept {
    const double x = slideMode == MPE_SLIDE_RELATIVE ? 0.5 + (raw - first) / 254 : raw / 127;
    return slideCurve.at(x);
  }

  /** A pressure (0..127) on Pressure's curve, before smoothing. */
  double pressure(double raw) const noexcept { return pressureCurve.at(raw / 127); }

  double pressureSmoothingMs() const noexcept { return pressureMs; }
  double glideTimeMs() const noexcept { return glideMs; }
  bool releaseVelocity() const noexcept { return useRelease; }

 private:
  static double bendOf(double raw) noexcept { return clamp((raw - 8192) / 8192, -1, 1); }

  bool enabled = false;
  int zone = MPE_LOWER;
  int memberCount = MPE_MEMBERS_MAX;
  double noteBend = MPE_NOTE_BEND_DEFAULT;
  double masterBend = MPE_MASTER_BEND_DEFAULT;
  MpeCurve slideCurve;
  int slideMode = MPE_SLIDE_ABSOLUTE;
  MpeCurve pressureCurve;
  double pressureMs = 20;
  double glideMs = 0;
  bool useRelease = false;
};

/** One voice's MPE expression: its bend (glided), slide and pressure (smoothed). */
class MpeNote {
 public:
  void prepare(double sampleRate) noexcept {
    bendGlide.prepare(sampleRate);
    pressureGlide.prepare(sampleRate);
  }

  /** A new note: bend centred, no pressure, slide waiting for its first CC (at the middle until then in Relative). */
  void noteOn(const Mpe& mpe) noexcept {
    settings = &mpe;
    bendGlide.setTime(mpe.glideTimeMs());
    pressureGlide.setTime(mpe.pressureSmoothingMs());
    bendTarget = 0;
    bendGlide.reset(0);
    pressureTarget = mpe.pressure(0);
    pressureGlide.reset(pressureTarget);
    firstSlide = -1;
    slideValue = mpe.slide(64, 64);
  }

  /** The note channel's 14-bit pitch bend. */
  void bend(double raw) noexcept {
    if (settings) bendTarget = settings->noteBendSemitones(raw);
  }

  /** The note channel's CC 74. */
  void slide(double raw) noexcept {
    if (!settings) return;
    if (firstSlide < 0) firstSlide = raw;
    slideValue = settings->slide(raw, firstSlide);
  }

  /** The note channel's channel pressure. */
  void pressure(double raw) noexcept {
    if (settings) pressureTarget = settings->pressure(raw);
  }

  /** Steps a block of n samples. */
  void advance(int n) DSP_NONBLOCKING {
    bendGlide.advance(bendTarget, n);
    pressureGlide.advance(pressureTarget, n);
  }

  /** The note's own bend in semitones (glided). */
  double bendSemitones() const noexcept { return bendGlide.value; }
  double slideAmount() const noexcept { return slideValue; }
  double pressureAmount() const noexcept { return pressureGlide.value; }

 private:
  const Mpe* settings = nullptr;
  BlockSmoother bendGlide;
  BlockSmoother pressureGlide;
  double bendTarget = 0;
  double pressureTarget = 0;
  double firstSlide = -1;
  double slideValue = 0;
};

}  // namespace dsp
