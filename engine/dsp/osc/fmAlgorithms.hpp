/**
 * FM operator topologies.
 *
 * The eight classic four-operator algorithms are *data*, not code paths — edge
 * lists that get written into the same N×N depth matrix that free-routing mode
 * exposes directly. One evaluation engine (FmOperators.hpp) serves both modes.
 *
 * Operators are zero-indexed here and presented as 1..N in the UI.
 */
#pragma once

#include <algorithm>

namespace dsp {

/** Operators in the module. */
constexpr int OPERATORS = 4;
/** The largest FM index (phase deviation in radians), for the algorithms' depth and every free-routing edge. */
constexpr double FM_MAX_INDEX = 8;

struct Algorithm {
  const char* name;
  /** [modulator, carrier] pairs. */
  int edges[OPERATORS][2];
  int edgeCount;
  /** Operators that reach the output. */
  int carriers[OPERATORS];
  int carrierCount;
};

/**
 * Running from a full serial stack (deep, metallic FM) through to fully additive
 * (four independent oscillators, i.e. no FM at all).
 *
 *   1 Stack           4→3→2→1
 *   2 Stack + Direct  4→1, 3→2→1
 *   3 Twin Branch     4→3→1, 2→1
 *   4 Double Mod      4→2, 3→2, 2→1
 *   5 Chain + Carrier 4→3→2, with 1 clean
 *   6 One to Three    4→1, 4→2, 4→3
 *   7 Pair + Two      3→2, with 1 and 4 clean
 *   8 Additive        no modulation
 */
constexpr Algorithm ALGORITHMS[] = {
    {"Stack", {{3, 2}, {2, 1}, {1, 0}}, 3, {0}, 1},
    {"Stack + Direct", {{2, 1}, {1, 0}, {3, 0}}, 3, {0}, 1},
    {"Twin Branch", {{3, 2}, {2, 0}, {1, 0}}, 3, {0}, 1},
    {"Double Mod", {{3, 1}, {2, 1}, {1, 0}}, 3, {0}, 1},
    {"Chain + Carrier", {{3, 2}, {2, 1}}, 2, {0, 1}, 2},
    {"One to Three", {{3, 0}, {3, 1}, {3, 2}}, 3, {0, 1, 2}, 3},
    {"Pair + Two", {{2, 1}}, 1, {0, 1, 3}, 3},
    {"Additive", {}, 0, {0, 1, 2, 3}, 4},
};
constexpr int ALGORITHM_COUNT = sizeof(ALGORITHMS) / sizeof(ALGORITHMS[0]);

/** Number of operators the fixed algorithms are defined over. */
constexpr int ALGORITHM_ARITY = OPERATORS;

/** Selecting this instead of an algorithm exposes the raw N×N matrix. */
constexpr int FREE_ROUTING = ALGORITHM_COUNT;

constexpr const char* ALGORITHM_LABELS[] = {"Stack", "Stack + Direct", "Twin Branch", "Double Mod", "Chain + Carrier",
                                            "One to Three", "Pair + Two", "Additive", "Free"};

/**
 * Write an algorithm into a depth matrix and carrier mask.
 *
 * @param index     the FM index every edge of the algorithm gets
 * @param depth     OPERATORS×OPERATORS matrix, row = modulator, col = carrier
 * @param carriers  per-operator output gain, 1 for carriers and 0 otherwise
 */
inline void applyAlgorithm(int algoIndex, double index, float* depth, float* carriers) noexcept {
  std::fill(depth, depth + OPERATORS * OPERATORS, 0.0f);
  std::fill(carriers, carriers + OPERATORS, 0.0f);

  if (algoIndex < 0 || algoIndex >= ALGORITHM_COUNT) return;
  const Algorithm& algo = ALGORITHMS[algoIndex];

  for (int i = 0; i < algo.edgeCount; i++) depth[algo.edges[i][0] * OPERATORS + algo.edges[i][1]] = static_cast<float>(index);
  for (int i = 0; i < algo.carrierCount; i++) carriers[algo.carriers[i]] = 1;
}

}  // namespace dsp
