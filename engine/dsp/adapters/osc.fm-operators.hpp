/**
 * osc.fm-operators as a uniform module (Module.hpp): four FM operators played at the Pitch input. The Envelopes input
 * has one channel per operator; an unwired one leaves that operator at full level. It plays as up to UNISON_MAX
 * detuned copies summed into its output (setUnison, UnisonCopies.hpp; one copy unless voice.unison says more), every
 * copy reading the same envelopes.
 */
#pragma once

#include <algorithm>

#include "../osc/FmOperators.hpp"
#include "Module.hpp"
#include "UnisonCopies.hpp"

namespace dsp {
namespace adapter {

class OscFmOperators : public Wrapper<OscFmOperators, 38> {
 public:
  static constexpr const char* ID = "osc.fm-operators";
  /** Algorithm, Index, then 5 per operator (Op1Shape + 5·op + field), then Carrier1 + op, then the 12 free depths. */
  enum Param : int { Algorithm, Index, Op1Shape, Carrier1 = Op1Shape + 5 * OPERATORS, Mod1to2 = Carrier1 + OPERATORS };
  enum OperatorField : int { OpShape, OpSemitones, OpCents, OpLevel, OpFeedback };
  static constexpr ParamInfo PARAMS[] = {
      {"algorithm", 0},    {"index", 1},       {"op1Shape", 0},     {"op1Semitones", 0}, {"op1Cents", 0},     {"op1Level", 1},
      {"op1Feedback", 0},  {"op2Shape", 0},    {"op2Semitones", 0}, {"op2Cents", 0},     {"op2Level", 1},     {"op2Feedback", 0},
      {"op3Shape", 0},     {"op3Semitones", 0}, {"op3Cents", 0},    {"op3Level", 1},     {"op3Feedback", 0},  {"op4Shape", 0},
      {"op4Semitones", 0}, {"op4Cents", 0},    {"op4Level", 1},     {"op4Feedback", 0},  {"carrier1", 1},     {"carrier2", 0},
      {"carrier3", 0},     {"carrier4", 0},    {"mod1to2", 0},      {"mod1to3", 0},      {"mod1to4", 0},      {"mod2to1", 0},
      {"mod2to3", 0},      {"mod2to4", 0},     {"mod3to1", 0},      {"mod3to2", 0},      {"mod3to4", 0},      {"mod4to1", 0},
      {"mod4to2", 0},      {"mod4to3", 0}};
  enum Port : int { Pitch, Envelopes, Out, PORT_COUNT };
  static constexpr PortInfo PORTS[] = {
      {"pitch", Direction::In, Kind::Control, Rate::Block, 1},
      {"envelopes", Direction::In, Kind::Control, Rate::Sample, OPERATORS},
      {"out", Direction::Out, Kind::Audio, Rate::Sample, 1},
  };

  void prepare(double sampleRate, int maxBlock) {
    for (FmOperators& fm : fms) fm.prepare(sampleRate, maxBlock);
    unison.prepare(maxBlock);
    apply();
  }

  void reset() noexcept {
    if (takeDirty()) apply();
    for (int i = 0; i < UNISON_MAX; i++) {
      fms[i].reset();
      fms[i].noteOn(unison.startPhase(i));
    }
  }

  /** Pitch: the played pitch in Hz. */
  void setInput(int port, double value) noexcept {
    if (port != Pitch) return;
    pitchHz = value;
    for (int i = 0; i < UNISON_MAX; i++) fms[i].setPitch(unison.pitch(i, pitchHz));
  }

  void noteOn(const Note& /*note*/) noexcept {
    for (int i = 0; i < UNISON_MAX; i++) fms[i].noteOn(unison.startPhase(i));
  }

  /** The unison copies (voice.unison): how many, each one's cents, and their sum's gain. */
  void setUnison(int count, const double* cents, double gain) noexcept {
    unison.set(count, cents, gain);
    for (int i = 0; i < UNISON_MAX; i++) fms[i].setPitch(unison.pitch(i, pitchHz));
  }

  void process(const Io& io, int offset, int n) DSP_NONBLOCKING {
    float* out = io.channel(Out, 0);
    if (!out) return;
    if (takeDirty()) apply();
    unison.play(out, offset, n, [&](int i, float* buffer, int at, int count) {
      // FmOperators reads each operator's envelope from 0 for the block, not from offset.
      FmOperators& fm = fms[i];
      float* env = fm.envBuffer();
      for (int op = 0; op < OPERATORS; op++) {
        float* dst = env + fm.envOffset(op);
        if (const float* src = io.channel(Envelopes, op)) std::copy(src + offset, src + offset + count, dst);
        else std::fill(dst, dst + count, 1.0f);
      }
      fm.process(buffer, at, count);
    });
  }

 private:
  void apply() noexcept {
    for (FmOperators& fm : fms) {
      fm.setParams(values[Algorithm], values[Index]);
      for (int op = 0; op < OPERATORS; op++) {
        const double* v = values + Op1Shape + 5 * op;
        fm.setOperator(op, v[OpShape], v[OpSemitones], v[OpCents], v[OpLevel], v[OpFeedback]);
        fm.setCarrier(op, values[Carrier1 + op]);
      }
      // The free depths in manifest order: from each operator to every other, in ascending order.
      int i = Mod1to2;
      for (int from = 0; from < OPERATORS; from++)
        for (int to = 0; to < OPERATORS; to++)
          if (from != to) fm.setDepth(from, to, values[i++]);
    }
  }

  FmOperators fms[UNISON_MAX];  // copy 0 is the operators themselves; the rest play only with unison
  UnisonCopies unison;
  double pitchHz = 440;
};

}  // namespace adapter
}  // namespace dsp
