# Bass Enhancer for foobar2000

A bass enhancer DSP for **foobar2000 v2** (x86 and x64). It is a port of the **Calf Studio Gear Bass Enhancer**, the plugin EasyEffects uses on Linux. It adds two improved modes that avoid Calf's side effects.

- **Classic** mode is bit-exact with Calf and EasyEffects.
- **Phase-aligned** mode removes Calf's bass dip around 50 Hz.
- **Harmonics only** mode adds overtones without extra low-end energy.
- Settings apply live while music plays, with no clicks.
- **Listen** (hear only what the enhancer adds) and **Bypass** work like Calf / EasyEffects, with click-free 20 ms crossfades.

## Install
1. Download `foo_dsp_bassenhancer.fb2k-component` from [Releases](../../releases/latest).
2. In foobar2000, go to **Preferences > Components > Install...**, pick the file, then click **Apply** and restart.
3. Go to **Preferences > Playback > DSP Manager** and add **Bass Enhancer** to the active DSPs.
4. Double-click it to open the settings.

The package contains both the 32-bit and 64-bit builds. foobar2000 loads the right one.

## Modes
| Mode | What it does |
|---|---|
| **Phase-aligned** (default) | Calf's processing, but the original signal goes through matching all-pass filters so it adds in phase with the processed bass. The result is a smooth bass lift (about +8 dB below 60 Hz at Amount 0 dB, fading out by 200 Hz) plus harmonics, with no dip. |
| **Harmonics only** | Adds only overtones (2nd–5th harmonics) and keeps the bass itself at its original level (±1 dB). The overtone level follows the bass level, so quiet passages get enhanced too. This mode gives a fuller-sounding bass without extra low-end energy, which suits small speakers and headphones. |
| **Classic** | Identical to Calf / EasyEffects, verified sample by sample. Calf's processed bass partly cancels the original around half the Scope frequency (about −8 dB at 50–55 Hz with default settings). |

Bass level change on a −12 dBFS test tone with default settings (Amount 0 dB):

| Mode | 30 Hz | 40 Hz | 55 Hz | 80 Hz | 100 Hz | 200 Hz |
|---|---|---|---|---|---|---|
| Classic | +5.0 | +1.0 | −8.2 | +3.5 | +3.3 | −0.1 |
| Phase-aligned | +8.1 | +7.9 | +7.4 | +5.4 | +3.3 | +0.1 |
| Harmonics only | +1.1 | +0.9 | +0.5 | 0.0 | 0.0 | 0.0 |

## Controls
| Control | Range (default) | Meaning |
|---|---|---|
| Mode | (Phase-aligned) | See above. Switching mode doesn't change the sliders. Every slider works in every mode. |
| Amount | −36…+36 dB (0) | How much processed bass is added. −36 turns it off. |
| Harmonics | 0.1–10 (8.5) | Waveshaper drive, which sets the harmonic content |
| Blend | −10…+10 (0) | Balance between 3rd (odd) and 2nd (even) harmonics |
| Scope | 10–250 Hz (100) | The bass below this frequency gets processed |
| Floor | 10–120 Hz (20, off) | High-pass on the processed bass to remove sub-rumble |
| Input / Output | −36…+36 dB (0) | Gain before and after the effect |
| Listen | off | Calf's Listen: plays only the processed bass and harmonics the enhancer adds, without the original audio (Amount × Output). In Harmonics only mode you hear just the overtones. |
| Listen gain | −12…+12 dB (0) | Extra volume while listening, for both the processed and the original bass (0 dB = Calf's level) |
| Bypass | off | Calf's Bypass: plays the original, unprocessed audio. **With Listen on**, it plays the original bass only (the input below Scope), so toggling Bypass A/Bs the original bass against the processed bass. The effect keeps running, so switching back is seamless. |
| Reset | | Restores the defaults |

Settings are saved in the DSP chain preset.

## Tips
- Put it **before** the resampler in the DSP chain. It then runs at the file's native rate, which is cheaper.
- Start with Phase-aligned mode, Amount 0 to +2 dB, Scope 80–120 Hz, Floor on at 20–30 Hz, and Output 0 dB.
- The bass lift can push peaks over 0 dBFS. Put foobar2000's **Advanced Limiter** last in the chain instead of lowering Output. Lowering Output also lowers the mids and treble, so the bass sounds weaker by comparison.
- The effect depends on the input level. If ReplayGain or a preamp lowers the level, you may want a little more Amount (or Input).

## Building
Requirements:
- Visual Studio 2026 (MSVC v145)
- [foobar2000 SDK](https://www.foobar2000.org/SDK) 2026-09-17 in `..\SDK-2026-09-17`
- [WTL](https://sourceforge.net/projects/wtl/) in `..\wtl`
- 7-Zip, for packaging

```
build.bat [Release|Debug] [x64|Win32]   builds the SDK libraries and the component (log: build.log)
package.bat                             builds both platforms -> dist\foo_dsp_bassenhancer.fb2k-component
```

Source files:
- `src/bass_enhancer.h`: the DSP engine, standalone with no foobar2000 dependency
- `src/component.cpp`: the foobar2000 DSP and the settings dialog

## Changelog
- **1.4.0**: Listen and Bypass now work like Calf / EasyEffects. Listen plays only what the enhancer adds (instead of a low-passed bass solo), and Bypass always plays the original. Both crossfade in 20 ms, so toggling no longer clicks. Bypass with Listen on plays the original bass only, to A/B the bass. Listen gain defaults to 0 dB (presets with the old +3 dB default load as 0 dB). With both off the output is unchanged (bit-exact with 1.3.1).
- **1.3.1**: x86 (32-bit) build added to the package.
- **1.3.0**: Listen gain slider.
- **1.2.x**: Listen is now a bass-solo monitor (8th-order low-pass at 3× Scope) that works together with Bypass for A/B comparison. Bypass and Listen states are shown in the dialog and logged to the console.
- **1.1.0**: Bypass checkbox. Harmonics only mode is louder and works at any level.
- **1.0.0**: First release.

## License and credits
The DSP code is derived from [Calf Studio Gear](https://github.com/calf-studio-gear/calf): the Bass Enhancer by Markus Schmidt, the tape distortion core by Tom Szilagyi, and the framework by Krzysztof Foltman. Calf is licensed under the GNU LGPL 2.1 or later, and so is this component. See [LICENSE](LICENSE).
