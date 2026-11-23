/**
 * motion.kfo in the engine: every KFO and SOLOKFO of a project played from tables the engine generator bakes
 * (src/shared/kfo-tables.js), moving the handles they drive and setting the parameters those handles' paths set.
 *
 * - A **player** (MotionPlayer.hpp) is a KFO — linked to a motion.kfo block, whose period, direction, depth, phase and
 *   play params set how it plays (kfoTick), or free, playing its motion at its own length — or a handle's SOLOKFO.
 *   The page sets its play, slot and recording params (levels: a change plays, stops or selects) and sends its takes
 *   (setTake); a linked KFO also starts or stops when its block's play param changes (or starts at once when it is
 *   on from the start), unless a take is under way.
 * - A **target** is a handle a player may drive, with its own on and rotation params (a KFO's target list in mod
 *   mode); a SOLOKFO has one, its own handle, turned by the player's rotation param.
 * - A **handle** knows where it is (the page's HANDLE.<handle>.x/y params, or where a player last put it) and its
 *   **entries**: each path its point moves, read (HandleReading; a zone path through its ZoneSpec in `zones`) and
 *   landed on a parameter (SlotFeel).
 *
 * Each block, every playing player advances, turns its point by each target's rotation, scales it by depth around
 * where the handle rested when playback started (kfoDepthPoint; the rest is taken the first block depth is below 1),
 * and drives the handle: each entry's value goes to `apply(param, value)`. A parameter driven this block is **owned**:
 * the host's own setParam for it only keeps the value underneath (owns()), and when no player drives it any more its
 * kept value is applied again. Players run in table order, KFOs before SOLOKFOs, so the last one to drive a handle wins.
 *
 * The page draws what the engine plays from watch(): each player's point and whether it plays, and each handle's point
 * and whether a player drove it.
 */
#pragma once

#include <algorithm>
#include <vector>

#include "HandleFeel.hpp"
#include "MotionPlayer.hpp"

namespace dsp {

enum class KfoPlayerKind : int { Linked = 0, Free = 1, Solo = 2 };

/** One player. Param indices are into the engine's params; −1 when it has none. */
struct KfoPlayerSpec {
  int kind;       // KfoPlayerKind
  int family;     // MotionFamily
  double period;  // the built-in motion's period (slot 3), seconds
  int blockPeriod, blockDirection, blockDepth, blockPhase, blockPlay;  // a linked KFO's block params
  int play, slot, recording, rotation;  // the page's levels; rotation: a SOLOKFO's
  int firstTarget, targetCount;
};

struct KfoTargetSpec {
  int handle;
  int on;        // −1: always on
  int rotation;  // −1: the player's
};

struct KfoHandleSpec {
  int x, y;  // HANDLE.<handle>.x / .y
  int firstEntry, entryCount;
};

struct KfoEntrySpec {
  int param;
  HandleReading reading;
  SlotFeel feel;
};

struct KfoTables {
  const KfoPlayerSpec* players;
  int playerCount;
  const KfoTargetSpec* targets;
  int targetCount;
  const KfoHandleSpec* handles;
  int handleCount;
  const KfoEntrySpec* entries;
  int entryCount;
  int paramCount;
  const ZoneSpec* zones = nullptr;  // a Zone reading's c0 indexes these
  int zoneCount = 0;
};

/** What watch() reports per player and per handle: x, y, and playing (a player) or driven (a handle). */
constexpr int KFO_WATCH_FIELDS = 3;

class KfoHost {
 public:
  /** Allocates every player and every per-handle and per-param list. */
  void prepare(const KfoTables& tables, double sampleRate) {
    t = &tables;
    this->sampleRate = sampleRate > 0 ? sampleRate : 48000;
    players.assign(static_cast<std::size_t>(tables.playerCount), MotionPlayer());
    for (int p = 0; p < tables.playerCount; p++) players[p].prepare(static_cast<MotionFamily>(tables.players[p].family), tables.players[p].period);
    lastPlay.assign(tables.playerCount, -1);
    lastSlot.assign(tables.playerCount, -1);
    lastBlockPlay.assign(tables.playerCount, 0);  // a block's play starts a KFO when on; off at the start stops nothing
    wasPlaying.assign(tables.playerCount, 0);
    last.assign(tables.playerCount, MotionPoint{});
    rest.assign(tables.targetCount, MotionPoint{});
    restValid.assign(tables.targetCount, 0);
    point.assign(tables.handleCount, MotionPoint{});
    driven.assign(tables.handleCount, 0);
    wasDriven.assign(tables.handleCount, 0);
    owned.assign(tables.paramCount, 0);
    ownedNow.assign(tables.paramCount, 0);
  }

  /** Whether a player drives `param` now: setParam then only keeps its value for when none does. */
  bool owns(int param) const noexcept { return param >= 0 && param < static_cast<int>(owned.size()) && owned[param]; }

  /** A take into player `player`'s slot 1…3 (MotionPlayer::setTake). */
  void setTake(int player, int slot, const float* xy, int count) noexcept {
    if (player >= 0 && player < static_cast<int>(players.size())) players[player].setTake(slot, xy, count);
  }

  /** Advances every player by n samples, given the engine's param values, and applies what they drive. */
  template <class Apply>
  void process(int n, const double* values, Apply&& apply) noexcept {
    if (!t) return;
    const double dt = n / sampleRate;
    std::fill(ownedNow.begin(), ownedNow.end(), 0);
    std::swap(driven, wasDriven);
    std::fill(driven.begin(), driven.end(), 0);
    for (int p = 0; p < t->playerCount; p++) {
      const KfoPlayerSpec& spec = t->players[p];
      MotionPlayer& player = players[p];
      const bool recording = at(values, spec.recording) >= 0.5;
      const int slot = static_cast<int>(roundHalfUp(at(values, spec.slot, BUILTIN_SLOT)));
      if (slot != lastSlot[p]) { player.select(slot); lastSlot[p] = slot; }
      const int play = at(values, spec.play) >= 0.5 ? 1 : 0;
      if (play != lastPlay[p]) { player.setPlaying(play); lastPlay[p] = play; }
      const bool linked = spec.kind == static_cast<int>(KfoPlayerKind::Linked);
      KfoSettings settings;
      if (linked) {
        settings = kfoSettings(at(values, spec.blockPeriod, MOTION_PERIOD_DEFAULT), at(values, spec.blockDirection), at(values, spec.blockDepth, 1), at(values, spec.blockPhase), at(values, spec.blockPlay));
        const int blockPlay = settings.play ? 1 : 0;
        if (blockPlay != lastBlockPlay[p]) { lastBlockPlay[p] = blockPlay; if (!recording) player.setPlaying(settings.play); }
      }
      if (recording) { wasPlaying[p] = 0; continue; }
      if (player.isPlaying() && !wasPlaying[p]) for (int k = 0; k < spec.targetCount; k++) restValid[spec.firstTarget + k] = 0;
      wasPlaying[p] = player.isPlaying() ? 1 : 0;
      MotionPoint moved;
      if (!(linked ? player.kfoTick(dt, settings, moved) : player.tick(dt, moved))) continue;
      last[p] = moved;
      const double depth = linked ? settings.depth : 1;
      const MotionFamily family = static_cast<MotionFamily>(spec.family);
      for (int k = 0; k < spec.targetCount; k++) {
        const int target = spec.firstTarget + k;
        const KfoTargetSpec& ts = t->targets[target];
        if (ts.on >= 0 && at(values, ts.on) < 0.5) continue;
        MotionPoint q = rotateMotionPoint(family, moved, at(values, ts.rotation >= 0 ? ts.rotation : spec.rotation));
        if (depth < 1) {
          if (!restValid[target]) { rest[target] = handlePoint(ts.handle, values); restValid[target] = 1; }
          q = kfoDepthPoint(q, rest[target], depth);
        }
        drive(ts.handle, q, apply);
      }
    }
    for (int i = 0; i < t->paramCount; i++) if (owned[i] && !ownedNow[i]) apply(i, values[i]);
    std::swap(owned, ownedNow);
  }

  /** How many values watch() writes: KFO_WATCH_FIELDS per player, then per handle. */
  int watchCount() const noexcept { return t ? KFO_WATCH_FIELDS * (t->playerCount + t->handleCount) : 0; }

  /** Each player's x, y and playing (1/0), then each handle's x, y and driven (1/0); returns how many it wrote. */
  int watch(float* out, int capacity) const noexcept {
    if (!t || !out) return 0;
    int k = 0;
    const auto put = [&](double v) { if (k < capacity) out[k] = static_cast<float>(v); k++; };
    for (int p = 0; p < t->playerCount; p++) { put(last[p].x); put(last[p].y); put(players[p].isPlaying() ? 1 : 0); }
    for (int h = 0; h < t->handleCount; h++) { put(point[h].x); put(point[h].y); put(driven[h] || wasDriven[h] ? 1 : 0); }
    return std::min(k, capacity);
  }

 private:
  static double at(const double* values, int index, double fallback = 0) noexcept { return index >= 0 ? values[index] : fallback; }

  /** Where handle h is: where a player put it last block or this one, else where the page says. */
  MotionPoint handlePoint(int h, const double* values) const noexcept {
    if (driven[h] || wasDriven[h]) return point[h];
    const KfoHandleSpec& spec = t->handles[h];
    return {at(values, spec.x, 0.5), at(values, spec.y, 0.5)};
  }

  /** A path's value at q: a zone path through its zone table, any other through handleReadingAt. */
  double readingAt(const HandleReading& r, MotionPoint q) const noexcept {
    if (r.kind != static_cast<int>(ReadingKind::Zone)) return handleReadingAt(r, q.x, q.y);
    const int z = static_cast<int>(r.c[0]);
    return z >= 0 && z < t->zoneCount ? zoneReadingAt(t->zones[z], q.x, q.y) : 0;
  }

  template <class Apply>
  void drive(int h, MotionPoint q, Apply& apply) noexcept {
    point[h] = q;
    driven[h] = 1;
    const KfoHandleSpec& spec = t->handles[h];
    for (int e = spec.firstEntry; e < spec.firstEntry + spec.entryCount; e++) {
      const KfoEntrySpec& entry = t->entries[e];
      apply(entry.param, slotValue(entry.feel, readingAt(entry.reading, q)));
      ownedNow[entry.param] = 1;
    }
  }

  const KfoTables* t = nullptr;
  double sampleRate = 48000;
  std::vector<MotionPlayer> players;
  std::vector<int> lastPlay, lastSlot, lastBlockPlay;
  std::vector<char> wasPlaying;
  std::vector<MotionPoint> last, rest, point;
  std::vector<char> restValid, driven, wasDriven, owned, ownedNow;
};

}  // namespace dsp
