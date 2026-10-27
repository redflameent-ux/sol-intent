# Sol Intent: Sonic Alchemy Sampler, changelog

Newest changes first. Every build is on the [Releases page](../../releases/tag/latest).

## 0.12 (27 Oct 2026): Playback fixes from Cody's Logic tests
- **Fixed: notes played many times over.** A note that landed partway through an audio buffer, which is nearly every note coming from Logic, was being started up to 25 times at once. This caused the strange phasey artifacts, the random-sounding velocity, and the clicks once all the voices filled up. Each note now plays once.
- **Velocity is much stronger.** With Touch at 70%, a hit at velocity 64 is about 10 dB quieter than a full hit (before, only about 3 dB). At 100% the difference is about 15 dB, and at 0% every hit is the same.
- **New Choke switch** (next to Let ring). For drums: hitting the same key again damps the hit that's still ringing, like a hand on the drum head. Leave it off for bowls so they ring over each other.
- **No clicks when voices run out.** If so many hits are ringing that a voice has to be reused, the old sound now fades out in about 3 ms instead of stopping dead. Each layer also has more voices (64, up from 48).
- **Velocity layers:** if a hit's velocity falls in a gap between layers, it now plays the nearest layer instead of switching between all of them.
- **Cleaner high notes and high sample rates.** Recordings played faster than they were recorded, such as higher notes or a 96 kHz file in a 48 kHz session, no longer turn harsh or grainy.

## 0.11 (25 Oct 2026): Full name
- The full name, Sol Intent: Sonic Alchemy Sampler, now shows beside the logo, in the Windows installer and in the read-me.
- The background glow leans green instead of blue, with indigo kept as a small accent.
- In music programs the plugin is still listed as "Sol Intent", so it fits in the plugin menus and existing projects keep finding it.
- The logo no longer appears in the middle of the ring, so the flower pattern shows on its own. The logo stays in the top left.

## 0.10 (25 Oct 2026): Logo and new colours
- The Sol Intent logo now sits large in the top left of the plugin.
- When nothing is playing, the logo glows softly in the middle of the ring. It fades away on the first note and comes back after a moment of quiet.
- The sun from the logo is the app icon for the standalone version.
- New colours to match the logo: gold, emerald green and black, with indigo as an accent.
- The circle-of-fifths colours (ring display and ROLL lines) now run from green through gold, with indigo as an accent.

## 0.9 (25 Oct 2026): Longer rings
- The demo bowl now rings for about 22 seconds and fades out gently. Before, it cut off early.
- Wider envelope limits: Attack up to 20 s, Decay up to 60 s, Release up to 60 s.
- Recordings up to 10 minutes long can be loaded.
- A tiny fade at the very end of each recording, so a sound never ends with a click.

## 0.8 (25 Oct 2026): The ring display breathes
- Holding a note draws light into the circle, a little like breathing in. The longer you hold, the deeper the breath.
- When you let go, the light breathes back out in a bright wave of glitter.

## 0.7 (25 Oct 2026): ROLL page (MPE piano roll)
- New ROLL tab. Draw notes as lines that bend, so a bowl can glide from one pitch to another.
- Each line can also shape velocity, pressure (swell) and slide (pan).
- Lines glow and glitter in their circle-of-fifths colour, and shimmer while they ring.
- Export MIDI, or drag a pattern straight into Logic. The MPE bends stay on each note.
- Patterns save as .solroll files.

## 0.6 (25 Oct 2026): Assignable expression modulation
- Right-click any knob and choose "Modulate with" to send Motion 1, Motion 2, the mod wheel or aftertouch to it. It works the same way as the macros, and nothing is hard-wired.
- A gold arc on each knob shows how much it is being moved.
- The XY morph pad can orbit by itself (right-click, then Orbit).

## 0.5 (25 Oct 2026): Patch list
- The preset arrows change the sound settings but keep the instruments you have loaded. A setting lets you change this.
- All effects start switched off.
- On the MAP page, drag a sample block to move it, or drag its edges to resize it.
- Undo and Redo on the MAP page (Ctrl/Cmd+Z).

## 0.4 (25 Oct 2026): Circle of fifths
- The resonance ring is laid out on the circle of fifths. Each note glows in its own colour.

## 0.3 (24 Oct 2026): Shaping the sound
- Each layer has its own envelope (attack, decay, sustain, release), a let-ring/held switch and a key zone.
- Four macro knobs that you can link to any control and rename.
- An XY morph pad that blends between the four layers.
- An effects rack with EQ, Chorus, Echo and Space (reverb).

## 0.2 (24 Oct 2026): Four layers and drum mapping
- Four instrument layers (A–D) that can play together.
- Importing samples asks whether they are drums (one key each) or tuned (spread across the keyboard).
- Instruments can be removed. They go to the Trash, so nothing is lost.
- Sample names are shown on the pads.

## 0.1 (24 Oct 2026): First version
- Sampler for recorded sound-healing instruments. Drop in WAV files and play them.
- Natural pitch changes with no time-stretching: low notes ring longer and high notes play faster, like a real instrument.
- Multisampling: root note, key range, velocity layers and round robin.
- Articulations (strike, swell, rim, roll) chosen with keyswitches.
- MIDI learn, presets and pitch bend.
- Settings are saved and recalled with your project in Logic.
- VST3 for Windows and Mac, plus AU for Logic.
- Star-field look with amber, cyan and violet.
