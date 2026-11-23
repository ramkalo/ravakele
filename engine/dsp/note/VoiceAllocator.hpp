/**
 * Voice allocation and stealing, and how one voice follows the keys held.
 *
 * The allocator keeps one slot per voice the host owns (up to MAX_VOICES) and says which slot each note plays on;
 * the host renders the voices. Polyphony only limits how many slots are used, so changing it never allocates.
 *
 * Stealing prefers, in order: a free slot, the quietest slot already releasing, then the oldest. Taking a releasing
 * voice first means a stolen note is one the player has already let go of. "Quietest" is the level the host reports
 * for each voice (setLevel), so the allocator never looks at audio itself.
 *
 * At one voice the allocator keeps the keys held (MIDI notes 0..127; another note plays as at more voices) and the
 * voice plays one of them, chosen by Priority. Letting go of the key it plays moves it to the next one held, by the
 * same Priority; letting go of the last releases it. Trigger says how the voice changes key while another is held:
 * Retrigger starts it again (its envelopes attack from where they are), Legato only moves its pitch. A key pressed
 * with nothing held always starts the voice.
 *
 * Polyphony — 1..MAX_VOICES slots in use; a slot that falls outside a new limit is freed.
 * Priority  — at one voice, which held key plays: Last (the latest pressed), Low, High.
 * Trigger   — at one voice, a change of key while another is held: Retrigger or Legato.
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

#include "../util/math.hpp"

namespace dsp {

constexpr int MAX_VOICES = 16;
constexpr int NOTE_KEYS = 128;

constexpr int PRIORITY_LAST = 0;
constexpr int PRIORITY_LOW = 1;
constexpr int PRIORITY_HIGH = 2;
constexpr const char* PRIORITY_LABELS[] = {"Last", "Low", "High"};

constexpr int TRIGGER_RETRIGGER = 0;
constexpr int TRIGGER_LEGATO = 1;
constexpr const char* TRIGGER_LABELS[] = {"Retrigger", "Legato"};

/** What the host does to a slot. */
enum class NoteAction : std::uint8_t {
  None,     // nothing changes (a key held that does not play, or one let go that was not playing)
  Start,    // the voice (re)starts on `note`
  Move,     // the voice plays `note` from now on without starting again (Legato)
  Release,  // the voice gets its note-off
};

struct NoteChange {
  int slot = -1;
  int note = -1;
  NoteAction action = NoteAction::None;
};

class VoiceAllocator {
 public:
  VoiceAllocator() noexcept { std::fill(std::begin(notes), std::end(notes), -1); }

  void prepare(double /*sampleRate*/) { reset(); }

  /** Every slot free, no key held. */
  void reset() noexcept {
    std::fill(std::begin(active), std::end(active), std::uint8_t{0});
    std::fill(std::begin(released), std::end(released), std::uint8_t{0});
    std::fill(std::begin(notes), std::end(notes), -1);
    std::fill(std::begin(levels), std::end(levels), 0.0f);
    clearHeld();
  }

  /** polyphony 1..MAX_VOICES; priority PRIORITY_*; trigger TRIGGER_*. */
  void setParams(double polyphonyValue, double priorityValue = PRIORITY_LAST, double triggerValue = TRIGGER_RETRIGGER) noexcept {
    const int p = static_cast<int>(clamp(roundHalfUp(polyphonyValue), 1, MAX_VOICES));
    // Retire any slot that falls outside the new limit.
    for (int i = p; i < MAX_VOICES; i++) free(i);
    polyphony = p;
    priority = static_cast<int>(clamp(roundHalfUp(priorityValue), PRIORITY_LAST, PRIORITY_HIGH));
    trigger = static_cast<int>(clamp(roundHalfUp(triggerValue), TRIGGER_RETRIGGER, TRIGGER_LEGATO));
  }

  int getPolyphony() const noexcept { return polyphony; }
  int getPriority() const noexcept { return priority; }
  int getTrigger() const noexcept { return trigger; }

  /** A key goes down: what happens to which slot. */
  NoteChange noteOn(int note) noexcept {
    const bool key = isKey(note);
    if (key) {
      held[note] = 1;
      pressedAt[note] = ++pressCounter;
    }
    if (polyphony > 1 || !key) return start(pickVoice(), note);

    if (choose() != note) return {};  // held, but another key wins
    if (playingHeldKey() && trigger == TRIGGER_LEGATO) return move(note);
    return start(0, note);
  }

  /**
   * A key comes up: the changes, written into `out` (from 0, at most `capacity`); returns how many. At more voices
   * every held slot playing it is released; at one, the voice moves to the next key held or is released.
   */
  int noteOff(int note, NoteChange* out, int capacity) noexcept {
    const bool key = isKey(note);
    if (key) held[note] = 0;
    if (polyphony > 1 || !key) {
      int n = 0;
      for (int i = 0; i < polyphony; i++) {
        if (active[i] && !released[i] && notes[i] == note) {
          released[i] = 1;
          if (n < capacity) out[n++] = NoteChange{i, note, NoteAction::Release};
        }
      }
      return n;
    }

    if (!active[0] || released[0] || notes[0] != note || capacity < 1) return 0;
    const int next = choose();
    if (next < 0) {
      released[0] = 1;
      out[0] = NoteChange{0, note, NoteAction::Release};
    } else {
      out[0] = trigger == TRIGGER_LEGATO ? move(next) : start(0, next);
    }
    return 1;
  }

  /** Every held slot is released, written into `out` (at most `capacity`), and no key is held; returns how many. */
  int allNotesOff(std::int32_t* out, int capacity) noexcept {
    clearHeld();
    int n = 0;
    for (int i = 0; i < polyphony; i++) {
      if (active[i] && !released[i]) {
        released[i] = 1;
        if (n < capacity) out[n++] = i;
      }
    }
    return n;
  }

  /** The host reports a voice's current level (e.g. its amp envelope), for choosing the quietest to steal. */
  void setLevel(int i, double level) noexcept { levels[i] = static_cast<float>(level); }

  /** The host reports that a voice has finished (its release ended): the slot is free. */
  void finished(int i) noexcept { free(i); }

  bool isActive(int i) const noexcept { return active[i] == 1; }

  bool isReleased(int i) const noexcept { return released[i] == 1; }

  /** The note slot `i` is playing, or -1. */
  int noteOf(int i) const noexcept { return notes[i]; }

  /** Whether key `note` is held down. */
  bool isHeld(int note) const noexcept { return isKey(note) && held[note] == 1; }

  int activeCount() const noexcept {
    int n = 0;
    for (int i = 0; i < polyphony; i++) if (active[i]) n++;
    return n;
  }

 private:
  int polyphony = 8;
  int priority = PRIORITY_LAST;
  int trigger = TRIGGER_RETRIGGER;
  double ageCounter = 0;
  double pressCounter = 0;
  std::uint8_t active[MAX_VOICES] = {};
  std::uint8_t released[MAX_VOICES] = {};
  std::int32_t notes[MAX_VOICES] = {};
  double ages[MAX_VOICES] = {};
  float levels[MAX_VOICES] = {};
  std::uint8_t held[NOTE_KEYS] = {};
  double pressedAt[NOTE_KEYS] = {};

  static bool isKey(int note) noexcept { return note >= 0 && note < NOTE_KEYS; }

  void clearHeld() noexcept {
    std::fill(std::begin(held), std::end(held), std::uint8_t{0});
    std::fill(std::begin(pressedAt), std::end(pressedAt), 0.0);
  }

  void free(int i) noexcept {
    active[i] = 0;
    released[i] = 0;
    notes[i] = -1;
    levels[i] = 0;
  }

  NoteChange start(int i, int note) noexcept {
    active[i] = 1;
    released[i] = 0;
    notes[i] = note;
    ages[i] = ++ageCounter;
    return NoteChange{i, note, NoteAction::Start};
  }

  NoteChange move(int note) noexcept {
    notes[0] = note;
    return NoteChange{0, note, NoteAction::Move};
  }

  /** Slot 0 sounds a key that is still held (so a change of key is legato). */
  bool playingHeldKey() const noexcept { return active[0] && !released[0] && isHeld(notes[0]); }

  /** The held key Priority picks, or -1 when none is held. */
  int choose() const noexcept {
    int best = -1;
    for (int k = 0; k < NOTE_KEYS; k++) {
      if (!held[k]) continue;
      if (best < 0) { best = k; if (priority == PRIORITY_LOW) return k; continue; }
      if (priority == PRIORITY_HIGH || (priority == PRIORITY_LAST && pressedAt[k] > pressedAt[best])) best = k;
    }
    return best;
  }

  int pickVoice() const noexcept {
    for (int i = 0; i < polyphony; i++) if (!active[i]) return i;

    int quietest = -1;
    double quietestLevel = std::numeric_limits<double>::infinity();
    for (int i = 0; i < polyphony; i++) {
      if (!released[i]) continue;
      if (levels[i] < quietestLevel) { quietestLevel = levels[i]; quietest = i; }
    }
    if (quietest >= 0) return quietest;

    int oldest = 0;
    for (int i = 1; i < polyphony; i++) if (ages[i] < ages[oldest]) oldest = i;
    return oldest;
  }
};

}  // namespace dsp
