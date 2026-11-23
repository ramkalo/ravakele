/**
 * Where a note block's notes go: a voice allocator, or the next note block. The arpeggiator and the step sequencer
 * call it as their clock fires.
 */
#pragma once

namespace dsp {

class NoteSink {
 public:
  virtual ~NoteSink() = default;
  virtual void noteOn(int note, double velocity) noexcept = 0;
  virtual void noteOff(int note) noexcept = 0;
};

}  // namespace dsp
