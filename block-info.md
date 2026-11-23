<!--
Info text for mod mode's MORE page: what the Info card shows for the block the More settings card is on.

One entry per DSP part, under a heading that is the part's id (library/parts/dsp: env.envelope, filter.morph, mod.lfo):

## env.envelope

A paragraph.

Another paragraph, after a blank line.

A part with no entry here shows no Info card. Text in comments like this one is left out.
-->
## env.envelope

Envelope - Envelopes are modules that send control signals to the settings on the other modules of a synth. Common destinations for envelope modulation include the amp and the filter cutoff. The different stages for envelopes often include an Attack stage, a Decay stage, a Sustain stage, and a Release stage. Some envelopes will also include a Delay stage or even a Hold stage. Many synths will label the envelope control section with EG, for Envelope Generator, or they will use the abbreviation of stages that are available, such as ADSR, for Attack-Decay-Sustain-Release.

Attack - The attack stage of an envelope determines how long the control signal takes to reach the maximum amount of modulation after a trigger event occurs.

Decay - The decay stage of an envelope occurs immediately after the attack stage. It determines how long the signal takes to reach the Sustain stage level of modulation.

Sustain - The Sustain stage of an envelope determines what level of modulation is applied while the trigger is active. It occurs directly after the Decay stage.

Release - The Release stage of an envelope determines how long the signal takes to reach the minimum amount of modulation after the trigger is released.

## mod.lfo

Low Frequency Oscillator - Low frequency oscillators function almost exactly like normal oscillators except they typically oscillate at frequencies well below human hearing. For example some oscillators will complete their cycle once every ten minutes, some may even have settings to one cycle per several hours. The point of an LFO is to make some other setting on a synth, such as the filter cutoff, be dynamic, allowing more interesting and complex sounds to be created.

## mod.aftertouch

Aftertouch - This is a feature found on some synths that allows for the keyboard to function as more than a simple trigger. The synth will track how hard or soft the player is pressing the keys "after" the trigger occurs but before you let go of the key. Lets do an example: 1) First assign the aftertouch control to the filter cutoff 2) Next play a note but don't let go of the the key. 3) Pressing harder and softer on that key without letting go will cause the filter cutoff to wobble up and down based on the pressure you put on the key. Polyphonic Aftertouch tracks the pressure placed on each key being used to trigger a voice on a polyphonic synthesizer.

## mod.velocity

Velocity - Generally refers to the amount of pressure applied to a key when it is pressed. On a piano, playing with high velocity results in louder sound, while low velocity results in quieter sound. Adjusting the velocity sensitivity of a keyboard will adjust how much pressure it takes to play.

## mod.wheel

Mod Wheel - The mod wheel is a controller built into some synthesizers and MIDI controllers. It can usually be assigned to control different settings but is often defaulted to control the filter cutoff setting. Some synths use a ribbon controller instead of a mod wheel, even if they still label that controller as "Mod Wheel".

## filter.morph

Filter - Part of the audio signal chain that reduces, removes, amplifies, or otherwise shapes the frequencies of a signal. Filters are one of the key components found in almost every synthesizer, alongside oscillators, envelopes, and LFOs. Filters, and their cousin the equalizer, are used extensively through out the world of music making, not just in synthesizers.

Cutoff -  This setting controls the point at which a filter starts affecting the frequencies of an audio signal. For example, a low pass filter with a cutoff setting of 400Hz will generally leave the frequencies below that number alone, while the frequencies above 400Hz would have their amplitudes reduced. In essence, the cutoff functions a little like a volume knob for certain frequency bands. The amount of reduction that occurs past the cutoff point is determined by the filter slope.

Resonance — This controls the amount of frequency boost that occurs at the cutoff value for a filter. It can also control the peak of a bandpass filter or depth of a notch filter. For example, if the cutoff value on a low pass filter is set to 400Hz and the resonance is set fairly high then the result is: 1) frequencies above 400Hz are reduced 2) frequencies around 400Hz are amplified and 3) frequencies below 400Hz are left a

## note.portamento

Glide - The glide setting on a synth controls how quickly the next note in a sequence will reach its desired pitch. With a low glide value, the note will have the desired pitch very fast. When the glide is set higher an audible pitch slide effect can be heard. Some synths will use the word Portamento or even glissando, though those terms technically mean something slightly different.

## osc.wavetable

Wave Table - Some synthesizers use wave tables as their oscillators rather than one of the standard wave shapes like Sine, Triangle, Saw, or Square. Wave tables are essentially large lists of complex waves that give sound designers more choice. We can compare this to giving a painter a large pallet of different paint colors to choose from, instead of just providing them with Red, green, and blue. A skilled painter can get by with the basics by mixing them together, but having a large pallet of pre-mixed paints is often more enjoyable. One of the benefits of wavetable synthesis is the ability to have the wave shift and change while the synth is being played. This results in sounds with lots of interesting textures.

## osc.fm-operators

Frequency Modulation (FM) - This technique creates complex waveforms by using a carrier wave and a modulator wave. Distinct from just playing two waves from two oscillators at the same time, the modulator wave modifies and shapes the carrier wave. See Level 0-00 (link) to learn about FM synthesis.

## fx.window-delay

Delay (FX) - For the envelope stage, see Delay (Envelope). Delays are sound effects take in an audio signal and then repeat it one or more times. Most Delays will have options for increasing the number of times the audio is repeated and where in the stereo field the repetitions come from.

## fx.room-reverb

Reverb - This is an effect that takes in an audio signal and processes it so that it sounds as if you are listening to it in a physical space rather than delivered directly to your ears. The types of reverb you will likely come across are Room, Hall, Plate and Spring. Room and hall reverbs are pretty self explanatory, they make the audio signal sound like you are listening to it while inside of a room or hall. Plate and Spring reverbs get their name from a physical device that is mimic the reverb effect created by physical spaces by playing audio through a very large metal plate or metal spring.

## fx.reverb-space

Reverb - This is an effect that takes in an audio signal and processes it so that it sounds as if you are listening to it in a physical space rather than delivered directly to your ears. The types of reverb you will likely come across are Room, Hall, Plate and Spring. Room and hall reverbs are pretty self explanatory, they make the audio signal sound like you are listening to it while inside of a room or hall. Plate and Spring reverbs get their name from a physical device that is mimic the reverb effect created by physical spaces by playing audio through a very large metal plate or metal spring.

## clock.tempo

Tempo - The speed at which a musical piece is played. Typically given in BPM (Beats Per Minute).

Clock - This is the mechanism that allows electronic instruments and devices to keep a consistent tempo and stay synchronized with each other.

## amp.vca

Amplifier - An electrical component that increases the amplitude, or volume, of a sound. Synths need an amplifier circuit in order to produce sound. Most synths allow the amp to be modulated, often by an envelope or LFO.

## note.midi-input

MIDI - MIDI is the musical notation language used by electronic instruments. Rather than transmitting audio, it transmits instructions. For example, if you connect a MIDI controller to a synthesizer and press the A key, it sends an instruction to the synthesizer telling it to produce a frequency of 440Hz. MIDI is capable of sending much more data than just notes, however that is beyond the scope of this document. MIDI has 16 channels that it can send these instructions across, it is important to ensure that the device sending the signal and the device receiving that signal are both on the same channel.

## seq.step-grid

Sequencer - This is any device that sends out a series of sequential voltage or midi data to a synthesizer or drum machine. Some sequencers can only be used to modify pitch but others can be used to modify other parameters on a synth. Many synths have built in sequencers that can be programmed allowing the synth to modify and play itself.

## voice.tuning

Tuning - Tuning is the act of adjusting an oscillator, resonator, string, or tube until produces a target frequency.

Tune - When two oscillators are producing the same frequency, or pitch, they are in tune. In contemporary western music, 440Hz is set as the standard pitch to which all instruments have their "A" note tuned to.

## voice.allocation

Polyphonic - Instruments like piano, guitar, organ, or a choir of human singers are capable of producing multiple unrelated notes simultaneously. Part of the reason pianos are so popular is that they can produce chord progressions and melodies simultaneously. Many synths are also polyphonic, though they are often limited to only 4 or 8 simultaneous notes. This is called voice count. Many digital pianos have voice counts well above 64, meaning the pianist can have 64 different notes going simultaneously. Because this calculation includes the notes that are still ringing out after being played, a good digital piano should have at least 128 voices to produce the most natural sound. Since so many polyphonic synths are limited to only 8 voices it can be difficult to get them to adequately replicate the feel of pianos and organs while playing complex songs. Do not confuse the terms Polyphonic and Multitimbral.

Monophonic - Many instruments, like most instances of flute or a single human singer for example, are monophonic. Without special techniques like beatboxing, they can only make one note at a time. Many synths are also monophonic. While many monophic synths, like the minimoog, have 3 oscillators which can be tuned to different frequnecies, those oscillators are triggered at the same time and are working together to produce a single sound. Monophonic instruments are not inherently worse than polyphonic instruments. Monophonic synths are perfectly adapted to playing bass lines, melodies, or simple pad sounds.

## util.scope

Oscilloscope - An oscilloscope draws the shape of a sound wave as it moves: time runs along it and the wave swings from side to side as the signal rises and falls. A smooth curve is a pure tone; a jagged one has many harmonics.

## util.waterfall

Waterfall - A waterfall graph shows which frequencies are in a sound, and how that changes over time. Low frequencies are on the left and high frequencies on the right. The newest moment is at the top and older moments move down. The color of each point shows how loud that frequency was.

## util.tuner

Tuner - A tuner listens to a sound and tells you which note it is closest to. It also shows how far the sound is from that note in cents: a cent is one hundredth of a semitone, so +50 cents is halfway to the next note up and -50 cents is halfway to the next note down. A minus number means the sound is flat (a little low) and a plus number means it is sharp (a little high). The number in Hz is how many times the wave repeats each second. Try changing an oscillator's Semitone or Detune and watch the note and the cents move.
