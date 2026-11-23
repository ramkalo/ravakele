/** Every uniform module (Module.hpp), and Modules, the list of them, for code that walks them all. */
#pragma once

#include "amp.mixer.hpp"
#include "amp.pan-pair.hpp"
#include "amp.vca.hpp"
#include "env.envelope.hpp"
#include "filter.morph.hpp"
#include "fx.chorus-ensemble.hpp"
#include "fx.crossover-split.hpp"
#include "fx.distortion.hpp"
#include "fx.limiter.hpp"
#include "fx.reverb-space.hpp"
#include "fx.room-reverb.hpp"
#include "fx.rotary-speaker.hpp"
#include "fx.soft-saturator.hpp"
#include "fx.warp-chorus.hpp"
#include "fx.window-delay.hpp"
#include "mod.aftertouch.hpp"
#include "mod.duck.hpp"
#include "mod.keytrack.hpp"
#include "mod.lfo.hpp"
#include "mod.pabfo.hpp"
#include "mod.random.hpp"
#include "mod.velocity.hpp"
#include "mod.voice-values.hpp"
#include "mod.wheel.hpp"
#include "noise.coloured-noise.hpp"
#include "osc.fm-operators.hpp"
#include "osc.wavetable.hpp"
#include "util.scope.hpp"
#include "util.tuner.hpp"
#include "util.waterfall.hpp"

namespace dsp {
namespace adapter {

template <class... M>
struct ModuleList {};

using Modules = ModuleList<AmpMixer, AmpPanPair, AmpVca, EnvEnvelope, FilterMorph, FxChorusEnsemble, FxCrossoverSplit, FxDistortion, FxLimiter,
                           FxReverbSpace, FxRoomReverb, FxRotarySpeaker, FxSoftSaturator, FxWarpChorus, FxWindowDelay, ModAftertouch, ModDuck, ModKeytrack, ModLfo, ModPabfo, ModRandom, ModVelocity, ModVoiceValues, ModWheel, NoiseColouredNoise, OscFmOperators, OscWavetable, UtilScope, UtilTuner, UtilWaterfall>;

}  // namespace adapter
}  // namespace dsp
