/**
 * Four FM operators and their routing.
 *
 * Each operator is a WavetableOscillator (any shape) tuned from the played pitch by its own semitones and cents, with
 * a level and a self-feedback amount. The eight fixed algorithms and free routing are the same machinery: a 4×4 matrix
 * of phase-modulation depths plus a carrier mask. Selecting an algorithm writes into that matrix, every edge at the FM
 * index; free routing (FREE_ROUTING) uses the matrix and carriers set directly (setDepth, setCarrier).
 *
 * Evaluation order is a topological sort computed in compile(), which runs from setParams() and the routing setters
 * when the routing changes, never per sample. Free routing can describe cycles, so any edge that runs backward in the
 * resulting order reads the modulator's *previous* sample instead — the same one-sample delay that classic operator
 * feedback uses. Every possible routing is therefore valid and stable.
 *
 * An operator's envelope and level scale its output before it is used, so a modulator's envelope shapes the timbre it
 * imposes. The host writes each operator's envelope into envBuffer() at envOffset(op) before process(); it is all 1
 * (no envelope) until written.
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "../util/math.hpp"
#include "WavetableOscillator.hpp"
#include "fmAlgorithms.hpp"

namespace dsp {

class FmOperators {
 public:
  FmOperators() {
    for (int i = 0; i < OPERATORS; i++) gain[i] = 1;
  }

  void prepare(double sampleRate, int maxBlock = 128) {
    this->sampleRate = sampleRate;
    this->maxBlock = maxBlock;
    env.assign(static_cast<std::size_t>(OPERATORS) * maxBlock, 1.0f);
    for (int i = 0; i < OPERATORS; i++) oscs[i].prepare(sampleRate);
    applyRouting();
    reset();
  }

  void reset() noexcept {
    for (int i = 0; i < OPERATORS; i++) oscs[i].reset();
    std::fill(std::begin(out), std::end(out), 0.0f);
    std::fill(std::begin(lastOut), std::end(lastOut), 0.0f);
    fresh = true;
  }

  /** A note starts: every operator from phase `startPhase` (cycles; 0 unless a unison copy). */
  void noteOn(double startPhase = 0) noexcept {
    for (int i = 0; i < OPERATORS; i++) oscs[i].setPhase(startPhase);
  }

  /** The played pitch in Hz, each block. */
  void setPitch(double hz) noexcept { pitchHz = hz > 0 ? hz : 0; }

  /** algorithm 0..FREE_ROUTING, index 0..FM_MAX_INDEX (radians on every algorithm edge). Block-rate. */
  void setParams(double algorithm, double index) noexcept {
    const int a = static_cast<int>(clamp(roundHalfUp(algorithm), 0, FREE_ROUTING));
    const double i = clamp(index, 0, FM_MAX_INDEX);
    if (a == this->algorithm && i == this->index) return;
    this->algorithm = a;
    this->index = i;
    applyRouting();
  }

  /** Operator `op`'s shape 0..4, semitones, cents, level 0..1 and self-feedback 0..1. Block-rate. */
  void setOperator(int op, double shape, double semitones, double cents, double level, double feedback) noexcept {
    oscs[op].setShape(shape);
    tune[op] = static_cast<float>(clamp(roundHalfUp(semitones), -2 * SEMITONE_RANGE, 2 * SEMITONE_RANGE) + clamp(cents, -CENTS_RANGE, CENTS_RANGE) * 0.01);
    gain[op] = static_cast<float>(clamp(level, 0, 1));
    selfFb[op] = static_cast<float>(clamp(feedback, 0, 1) * FEEDBACK_SCALE);
  }

  /** Free routing: how deep `from` modulates `to`, 0..FM_MAX_INDEX radians. */
  void setDepth(int from, int to, double radians) noexcept {
    freeDepth[from * OPERATORS + to] = from == to ? 0.0f : static_cast<float>(clamp(radians, 0, FM_MAX_INDEX));
    if (algorithm == FREE_ROUTING) applyRouting();
  }

  /** Free routing: whether operator `i` reaches the output. */
  void setCarrier(int i, double on) noexcept {
    freeCarriers[i] = on >= 0.5 ? 1.0f : 0.0f;
    if (algorithm == FREE_ROUTING) applyRouting();
  }

  double getDepth(int from, int to) const noexcept { return depth[from * OPERATORS + to]; }

  bool isCarrier(int i) const noexcept { return carriers[i] != 0; }

  /** Where the host writes each operator's envelope for the next block: operator `op` from envOffset(op). */
  float* envBuffer() noexcept { return env.data(); }

  int envOffset(int op) const noexcept { return op * maxBlock; }

  /** Evaluation order, for tests and diagrams. */
  const std::int32_t* evaluationOrder() const noexcept { return order; }

  bool isDelayedEdge(int from, int to) const noexcept { return delayed[from * OPERATORS + to] == 1; }

  /** Writes n ≤ maxBlock samples of the carriers' sum from offset (overwriting). */
  void process(float* output, int offset, int n) DSP_NONBLOCKING {
    const double inv = n > 0 ? 1.0 / n : 0;
    for (int i = 0; i < OPERATORS; i++) {
      inc[i] = static_cast<float>(transpose(pitchHz, tune[i]) / sampleRate);
      if (fresh) lastInc[i] = inc[i];
      oscs[i].selectMip((inc[i] > lastInc[i] ? inc[i] : lastInc[i]) * sampleRate);
    }
    fresh = false;

    for (int s = 0; s < n; s++) {
      const double t = (s + 1) * inv;
      double sum = 0;
      for (int p = 0; p < OPERATORS; p++) {
        const int op = order[p];
        double pm = static_cast<double>(selfFb[op]) * lastOut[op];
        for (int m = 0; m < OPERATORS; m++) {
          const double d = depth[m * OPERATORS + op];
          if (d == 0 || m == op) continue;
          pm += d * (delayed[m * OPERATORS + op] == 1 ? lastOut[m] : out[m]);
        }
        const double opInc = lastInc[op] + (static_cast<double>(inc[op]) - lastInc[op]) * t;
        out[op] = static_cast<float>(oscs[op].tick(opInc, pm * INV_TWO_PI, 0.5) * env[op * maxBlock + s] * gain[op]);
      }
      for (int op = 0; op < OPERATORS; op++) {
        lastOut[op] = out[op];
        sum += static_cast<double>(out[op]) * carriers[op];
      }
      output[offset + s] = static_cast<float>(sum);
    }
    std::copy(std::begin(inc), std::end(inc), std::begin(lastInc));
  }

 private:
  /** Radians of phase deviation at full self-feedback. */
  static constexpr double FEEDBACK_SCALE = 4;
  static constexpr double INV_TWO_PI = 1 / (2 * PI);

  WavetableOscillator oscs[OPERATORS];
  double sampleRate = 48000;
  int maxBlock = 128;

  /** Phase-modulation depth in radians, [modulator * OPERATORS + carrier]. */
  float depth[OPERATORS * OPERATORS] = {};
  float carriers[OPERATORS] = {};
  float selfFb[OPERATORS] = {};
  /** Free routing's own matrix and carriers, kept while an algorithm is selected. */
  float freeDepth[OPERATORS * OPERATORS] = {};
  float freeCarriers[OPERATORS] = {};
  int algorithm = 0;
  double index = 1;

  /** Evaluation order, and which edges must read the previous sample. */
  std::int32_t order[OPERATORS] = {};
  std::uint8_t delayed[OPERATORS * OPERATORS] = {};
  std::int32_t inDegree[OPERATORS] = {};
  std::int32_t position[OPERATORS] = {};

  float out[OPERATORS] = {};
  float lastOut[OPERATORS] = {};

  double pitchHz = 440;
  float tune[OPERATORS] = {};
  float gain[OPERATORS] = {};
  float lastInc[OPERATORS] = {};
  float inc[OPERATORS] = {};
  bool fresh = true;

  /** Each operator's envelope, OPERATORS × maxBlock. */
  std::vector<float> env;

  /** The matrix and carriers in force: the selected algorithm's, or free routing's own. */
  void applyRouting() noexcept {
    if (algorithm == FREE_ROUTING) {
      std::copy(std::begin(freeDepth), std::end(freeDepth), std::begin(depth));
      std::copy(std::begin(freeCarriers), std::end(freeCarriers), std::begin(carriers));
    } else {
      applyAlgorithm(algorithm, index, depth, carriers);
    }
    compile();
  }

  /** Recompute evaluation order; called when the routing changes, never per sample. */
  void compile() noexcept {
    const int n = OPERATORS;
    std::int32_t* deg = inDegree;
    std::fill(deg, deg + n, 0);

    for (int from = 0; from < n; from++) {
      for (int to = 0; to < n; to++) {
        if (from != to && depth[from * n + to] != 0) deg[to]++;
      }
    }

    // Kahn's algorithm. Anything still holding a non-zero in-degree at the end is
    // part of a cycle and gets appended in index order.
    int head = 0;
    int tail = 0;
    std::int32_t* visited = position;  // reused as a scratch flag before being filled
    std::fill(visited, visited + n, 0);
    for (int i = 0; i < n; i++) {
      if (deg[i] == 0) { order[tail++] = i; visited[i] = 1; }
    }
    while (head < tail) {
      const int node = order[head++];
      for (int to = 0; to < n; to++) {
        if (to == node || depth[node * n + to] == 0) continue;
        if (--deg[to] == 0 && visited[to] == 0) {
          order[tail++] = to;
          visited[to] = 1;
        }
      }
    }
    for (int i = 0; i < n; i++) {
      if (visited[i] == 0) { order[tail++] = i; visited[i] = 1; }
    }

    for (int p = 0; p < n; p++) position[order[p]] = p;

    // An edge that does not run strictly forward in the final order cannot read a
    // current-sample value, so it reads the previous one.
    for (int from = 0; from < n; from++) {
      for (int to = 0; to < n; to++) {
        delayed[from * n + to] = position[from] < position[to] ? 0 : 1;
      }
    }
  }
};

}  // namespace dsp
