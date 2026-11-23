/**
 * LFO: a low-frequency modulation source.
 *
 * Rate      — 0.01..20 Hz (lfoMath.hpp); with Sync on, one cycle per the nearest division of the beat at Tempo.
 * Shape     — 0..1 through Saw up, Triangle, Sine, Triangle, Saw down, Square and a narrow Pulse (lfoWave).
 * Level     — 0..1, a depth: scales the window's swing toward 0 (lfoOutput).
 * Window    — 0..1, where the 100-unit window sits: −100..0, through −50..+50, to 0..+100 (lfoOutput).
 * Attack    — 0..20 s: after noteOn() the swing fades in from the window's center in a straight line (lfoOutput).
 * Retrigger — on: noteOn() also restarts the cycle from its start. Off: the phase runs free.
 *
 * The output is −1..1 (−100..+100). Whether a host keeps one LFO per voice or one for the instrument is the host's
 * choice: noteOn() is all it needs to call. Before the first noteOn() the LFO runs at full level.
 *
 * No allocation after construction; setParams() is block-rate. Level and the window glide over LFO_GLIDE_MS, so moving
 * them never steps the output; a rate change keeps the phase continuous.
 */
#pragma once

#include <cmath>

#include "../util/math.hpp"
#include "lfoMath.hpp"

namespace dsp {

class Lfo {
 public:
  void prepare(double sampleRate) {
    this->sampleRate = sampleRate;
    glide = 1 - std::exp(-1 / (sampleRate * LFO_GLIDE_MS * 0.001));
    reset();
  }

  void reset() noexcept {
    phase = 0;
    fading = false;
    attackPos = 0;
    levelNow = level;
    windowNow = window;
  }

  /** Rate in Hz, Shape 0..1, Level 0..1, Attack in seconds, Window 0..1, Retrigger and Sync on at ≥ 0.5, Tempo in BPM. Block-rate. */
  void setParams(double rate, double shape, double level, double attack, double window, double retrigger = 0,
                 double sync = 0, double tempo = 120) noexcept {
    inc = lfoCycleHz(rate, sync, tempo) / sampleRate;
    this->shape = clamp(shape, 0, 1);
    this->level = clamp(level, 0, 1);
    this->window = clamp(window, 0, 1);
    const double seconds = clamp(attack, LFO_ATTACK_MIN, LFO_ATTACK_MAX);
    const double samples = roundHalfUp(seconds * sampleRate);
    attackSamples = samples > 1 ? samples : 1;
    this->retrigger = retrigger >= 0.5;
  }

  /** A note starts: the output fades in over Attack; with Retrigger on the cycle restarts too. */
  void noteOn() noexcept {
    fading = true;
    attackPos = 0;
    if (retrigger) phase = 0;
  }

  /** Where in its cycle the LFO is, 0..1. */
  double currentPhase() const noexcept { return phase; }

  /** The current output, −1..1. */
  double value() const noexcept { return lfoOutput(lfoWave(shape, phase), levelNow, windowNow, gain()); }

  /** Per-sample output, n samples from offset (overwriting). */
  void process(float* out, int offset, int n) DSP_NONBLOCKING {
    for (int i = 0; i < n; i++) {
      levelNow += (level - levelNow) * glide;
      windowNow += (window - windowNow) * glide;
      out[offset + i] = static_cast<float>(value());
      step(1);
    }
  }

  /**
   * Advance n samples without evaluating each one and return the value landed on: for a block-rate modulation source,
   * whose targets interpolate between block edges. The glides land where n steps of them would.
   */
  double advance(int n) DSP_NONBLOCKING {
    const double k = 1 - std::pow(1 - glide, n);
    levelNow += (level - levelNow) * k;
    windowNow += (window - windowNow) * k;
    step(n);
    return value();
  }

 private:
  double sampleRate = 48000;

  double phase = 0;
  double inc = 1.0 / 48000;
  double shape = 2.0 / 6;
  bool retrigger = false;

  double level = 1;
  double window = 0.5;
  double levelNow = 1;
  double windowNow = 0.5;
  double glide = 1;

  double attackSamples = 1;
  double attackPos = 0;
  bool fading = false;

  void step(int n) noexcept {
    phase += inc * n;
    phase -= std::floor(phase);
    if (fading) {
      attackPos += n;
      if (attackPos >= attackSamples) fading = false;
    }
  }

  /** The fade-in, 0..1: a straight line over Attack after a note-on, else 1. */
  double gain() const noexcept { return fading ? attackPos / attackSamples : 1; }
};

}  // namespace dsp
