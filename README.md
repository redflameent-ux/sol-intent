# Sol Intent: Sonic Alchemy Sampler

A playable, expandable library for recorded sound-healing instruments: singing bowls, Koshi chimes,
frame drums and whatever gets recorded next. VST3 + stand-alone on Windows, AU + VST3 + stand-alone on Mac.

## What it does
- **Plays the real recordings.** On its own key a recording plays back exactly as recorded. Other keys
  repitch it the natural way (no time-stretching), so low notes ring longer and high notes speed up.
- **Multisampling.** Each recording has a root note, a key range and a velocity range. Several takes of the
  same note take turns (round robin). Soft and hard hits can be separate recordings.
- **Articulations.** Strikes, swells, rim hits, rolls... each is its own set of recordings. Switch with the
  buttons above the pads or with keyswitches (violet keys, starting at C0 by default).
- **Let ring.** Recordings ring out fully after you let go of the key. Turn it off to use the Release time.
- **Two layers (A and B)** with their own level, tune and pan, e.g. a crystal bowl under a Koshi chime.
- **Shaping:** Attack / Decay / Sustain / Release, a slow Motion LFO (pitch, volume, tone or pan; the mod wheel
  adds more), Tone, Width, Space + Room reverb, Touch (velocity), pitch bend up to +/-24 semitones.
- **MIDI learn:** right-click any knob, choose *Learn MIDI control*, move a knob or fader on the controller.
- **Presets** save every knob and both layers (`Documents/Sol Intent/Presets`).
- **4x4 pads** (higher on a pad = harder hit) and an on-screen keyboard; teal dots mark real recordings.

## Adding instruments (no rebuilding)
Easiest: press **+ New instrument**, then drag WAV files, or a whole folder, onto the plugin.
Folders inside a dropped folder become articulations. On the **MAP** screen click a recording, play its
note on the controller and press **Root = last key**. Everything saves to the instrument's folder.

By hand: the library is `Documents/Sol Intent/Library`. Each folder in it is an instrument; put note names in
the file names (`C3.wav`, `Bowl F#2 rr2.wav`, `G3_v1.wav`). Sub-folders are articulations. A folder with no
recordings of its own is a pack. Press **Rescan**. C3 = middle C (MIDI 60), like Logic and Ableton.

## Installing
Download from this repo's **Releases** page.
**Mac:** `Sol-Intent-Mac.pkg`, right-click it and choose **Open** (not signed with an Apple developer certificate yet).
**Windows:** `Sol-Intent-Setup.exe`, then rescan plugins in your DAW.

## Building
Every push to `main` builds the Windows installer and the Mac AU/VST3 package (GitHub Actions) and checks
the AU with Apple's validator. `tools/OfflineTest.cpp` holds the automated checks (`-DSOL_BUILD_TESTS=ON`).
