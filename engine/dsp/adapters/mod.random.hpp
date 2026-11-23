/** mod.random as a uniform module (Module.hpp): the stepped random source, −1..1, which keeps time even with its output unwired; the engine hands it its seed. */
#pragma once

#include "../mod/Random.hpp"
#include "Module.hpp"

namespace dsp {
namespace adapter {

class ModRandom : public Wrapper<ModRandom, 4> {
 public:
  static constexpr const char* ID = "mod.random";
  enum Param : int { Speed, Slew, Placement, Width };
  static constexpr ParamInfo PARAMS[] = {{"speed", 1}, {"slew", 0}, {"placement", 0.5}, {"width", 1}};
  enum Port : int { Out, PORT_COUNT };
  static constexpr PortInfo PORTS[] = {
      {"out", Direction::Out, Kind::Control, Rate::Sample, 1},
  };

  void prepare(double sampleRate, int /*maxBlock*/) {
    apply();
    random.prepare(sampleRate);
  }

  void reset() noexcept {
    if (takeDirty()) apply();
    random.reset();
  }

  /** Its sequence (a part marked "seeded": the engine gives every block, and every voice's copy of it, its own). */
  void seed(double value) noexcept { random.seed(value); }

  /** The output now as 0..1 (−100 is 0, +100 is 1), for a pad that shows it. */
  double value() const noexcept { return (random.value() + 1) * 0.5; }

  void process(const Io& io, int offset, int n) DSP_NONBLOCKING {
    if (takeDirty()) apply();
    if (float* out = io.channel(Out, 0)) random.process(out, offset, n);
    else random.advance(n);
  }

 private:
  void apply() noexcept { random.setParams(values[Speed], values[Slew], values[Placement], values[Width]); }

  Random random;
};

}  // namespace adapter
}  // namespace dsp
