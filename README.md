# Unofficial Rings — AU / VST3 port (work in progress)

A plugin wrapper around the Mutable Instruments **Rings** DSP, taken from the open-source release
(`pichenettes/eurorack`, `rings/dsp` + `stmlib/`, stmlib at the commit that repo pins). Not a Mutable Instruments product.
Don't contact Emilie Gillet or Mutable Instruments about it.

## Easiest: let GitHub build it
Push this folder to a GitHub repo. `.github/workflows/build.yml` builds the AU and VST3 on a macOS runner;
download `Rings-macOS` from the run's Artifacts (Actions tab > latest run). `INSTALL.txt` inside covers the install.
Builds are universal (arm64 + x86_64), minimum macOS 11.

## Build it yourself (macOS, for Logic Pro)

```
brew install cmake        # if you don't have it
cd rings-plugin
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```
First configure downloads JUCE 8.0.6. With `COPY_PLUGIN_AFTER_BUILD` on, the AU lands in
`~/Library/Audio/Plug-Ins/Components/` and the VST3 in `~/Library/Audio/Plug-Ins/VST3/`.
Then in Logic: Settings > Plug-in Manager > rescan, or run `killall -9 AudioComponentRegistrar`.
It shows up as "particledots: Unofficial Rings". The AU is declared as a *MIDI-controlled effect*
(`aumf`). In Logic this means two places to load it:

* **To play it from a MIDI keyboard:** create a **Software Instrument** track, click the **Instrument** slot, and choose
  *Audio Units > AU MIDI-controlled Effects > particledots: Unofficial Rings*. Leave Exciter on Internal and Strum on MIDI notes.
  (Confirmed working in Logic on the M4 Mac mini.)
* **To resonate another track's audio:** in the same setup, set Exciter to Audio input and pick the source track in the plug-in's
  Side Chain menu (top right of the plug-in window); set that source track's output to No Output to avoid doubling.
  The Side Chain route is untested. Loading it in an ordinary **Audio FX** slot also works as a plain effect
  (confirmed: another synth's audio resonated through it), but it gets no MIDI notes there.

An unsigned local build may need `xattr -dr com.apple.quarantine` on the bundle, or ad-hoc signing:
`codesign --force --deep -s - "<bundle>"`.

Headless DSP test (any OS): `cmake --build build --target engine_test && build/engine_test`

## Parameters

| Plugin | Module |
|---|---|
| Structure, Brightness, Damping, Position | the four main knobs, smoothed the way the firmware smooths its pots |
| Transpose | the FREQUENCY knob's job: semitones added to the pitch (rounded to whole semitones, as the firmware does). Range ±36, default 0 |
| FM | the FM input after its attenuverter, ±48 semitones (there is no CV in a plugin, so it is a plain knob) |
| Pitch Snap (0.2.0, my addition, not in the module) | **Off** (the sound is exactly 0.1.0), **Steps** or **Semitones**. Acts on Transpose + FM taken together. Steps are octaves and fifths across the controls' combined range: 0, 7, 12, 19, 24, 31, 36, 43, 48, 55, 60, 67, 72, 79, 84, both signs. Semitones rounds to whole notes. With it on, FM only matters when it tips the total over a step. The Transpose readout shows the offset that plays. Old projects load with it Off. |
| Polyphony | 1 / 2 / 4 voices |
| Mode | **Resonator** (the normal Rings) or **String synth** (the string-synth part that is also in the firmware) |
| Resonator Model | Modal resonator, Sympathetic strings, Modulated/inharmonic string, FM voice, Sympathetic strings (quantized), String + reverb. These are the six models in the firmware's `ResonatorModel` list; the first three are the standard ones, the last three are the firmware's extra ("bonus") models |
| String Synth FX | the firmware's six string-synth effects: Formant filter, Chorus, Reverb, Formant filter 2, Ensemble, Reverb 2 |
| Exciter | **Internal** (the module's own noise-burst exciter) or **Audio input** (your track's audio excites the resonator, through the firmware's noise gate) |
| Strum | what triggers a strum: **MIDI notes** (note-on = strum, note number = pitch), **Audio onset** (the firmware's onset detector listens to the audio input; uses the audio exciter), **Manual button** (the STRUM button; any click strums) |
| Mono Out | sums the two outputs. By default **left = ODD output, right = EVEN output**, as on the module |

Pitch: with MIDI notes, the sounding pitch is the MIDI note number plus Transpose. In the other two strum modes there is no note, so
the pitch is a fixed base note (MIDI 48) plus Transpose. That base note is my choice, not the module's: on hardware the equivalent
(nothing in V/OCT) is the bare FREQUENCY knob range, whose bottom is about 16 Hz.
Velocity is ignored (Rings has no velocity), and note-offs are ignored (Rings has no gate; strings just ring out and Damping decides how long).

## How it works / honest limits

* The module DSP is called exactly as the firmware does it: fixed 48 kHz, 24-sample blocks. A windowed-sinc
  resampler converts to and from the host rate (bypassed at 48 kHz). Reported latency is about 2 ms at 48 kHz.
* Not modelled: the CV inputs and per-channel attenuverters, calibration, normalisation detection (replaced by the Exciter and Strum
  menus above), the settings/UI/LED code, and the module's bypass.
* The audio input is the left and right channels averaged to mono (the hardware input is mono; the firmware reads one channel).
* MIDI note-ons are placed sample-accurately, but the DSP strums on 24-sample block boundaries (0.5 ms), as the module does.
* **Verified here:** the DSP wrapper (`engine_test`, 87 combinations at 44.1/48/96 kHz: all six models x 1/2/4 voices, audio exciter,
  audio-onset and manual strum, all six string-synth effects, knob extremes) produces finite, bounded, non-silent output; the VST3
  builds on Linux. The GitHub Actions macOS build (AU + VST3) passes. **Confirmed by the owner in Logic on the M4 Mac mini:** it loads,
  plays from MIDI notes in the Software Instrument slot, and the Structure/Damping etc. knobs behave like the module's.
  **Not verified:** loading on the Intel Mac (macOS 12.7), the VST3 in a host, the Side Chain route, and a careful A/B of how closely
  it sounds like the hardware. The automated tests check "not broken", not "sounds right".
* The model and effect names come from the firmware source's own enums and my reading of the code, not from listening.
* The UI is a basic custom panel (knobs, menus). It is functional rather than polished.

## Licensing

`third_party/` is MIT-licensed code by Emilie Gillet (copyright notices kept in the file headers; `third_party/stmlib/LICENSE`).
Keep those notices if you redistribute. The hardware design is CC-BY-SA; none of it is used here. The wrapper code in `src/` has
no licence assigned yet. JUCE is used under its own licence terms (AGPLv3 / commercial); check them before distributing builds.
