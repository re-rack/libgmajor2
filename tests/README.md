# How the tests work

The library is checked against the original firmware, not against expectations
written by hand. The G-Major 2's DSP firmware runs in an emulator (the xemu
DSP56300 core and the harness in `reverse_engineering/harness/`), the recorder
stores what it computes, and `make check` requires the library to reproduce it
exactly: every coefficient word and every output sample. A difference of one LSB
fails.

## Record once, check offline

```
firmware --emulator--> gm2_record --> tests/fixtures/<name>.bin
                                             |
            library --> gm2_check <----------+
```

- `gm2_record` needs the firmware, which the record targets extract from TC's
  updater (`reverse_engineering/README.md`), and the emulator library; `gm2_check`
  needs neither, so anyone can verify the library from a plain clone.
- Recording is deterministic. A fixture changes only when what it describes
  changes (an effect's knob table or test script, the input signal, the fixture
  format), never because `src/` changed.
- Inputs are generated (`gm2_signal.c`), never stored. The fixtures hold only the
  firmware's answers.

## What a fixture proves

Each effect's fixture has three parts, besides the knob table it was recorded
with, which must match the effect's.

| Part | Contents | What it proves |
|---|---|---|
| Setup words | the coefficient words the firmware's setup stages, for each knob on its own at every position (or 32 spread positions for wide knobs), and for four multi-knob presets walked one knob at a time | the setup math, including its clamps and the ways knobs interact |
| Level words | the input, wet and dry words the firmware's block code stages at the knob table's defaults | the level words each effect builds from its own Mix and Out Level knobs are the device's |
| Audio | one long scripted run on the generated signal, with every output sample | the audio kernel, the parameter glides and ramps, and the control tick |

The audio script moves each knob through its minimum, 33 %, 66 %, maximum and
default, then drives every knob from an active setting (Tempo at Ignore so Speed
and Delay act, switches flipped), and turns each knob ten times 64 frames apart,
so every turn lands while the previous one still glides. Segments are offset so
the turns land on each frame of the firmware's four-frame chunk. Runs are 97k to
780k frames long.

The input signal cycles through full-scale noise, held notes, noise bursts, quiet
noise, a 20 Hz to 20 kHz sweep and full-scale clicks, which open and close gates
and detectors and drive filters and limiters to their rails. A clean note
change, 110 Hz to 300 Hz, exercises the pitch tracker's lock and handover.

## How the recorder drives the firmware

The recorder changes knobs the way the device does: through the firmware's own
parameter edit and callbacks, not by writing results into memory. After every
four frames it runs exactly the parameter jobs the firmware has queued, and fails
if they differ from what the effect's adapter expects. An adapter never copies
firmware state into place and never takes a value from the library, so a
fixture cannot agree with the library by construction.

## How the checker compares

- **Setup:** every recorded knob vector's words, plus knobs one step outside
  their range, which must clamp to the end values. Configs are filled with junk
  before setup, so a word the setup forgets to write shows up.
- **Audio:** every run is replayed through all four entry points: split,
  interleaved, per frame, and float, in place (Q23 to float is exact, so its
  output converts back for an exact compare). Buffer sizes are random, and the
  interleaved and per-frame paths get junk in the upper input bytes, which the
  library must ignore.
- **Shared pieces:** `swf.bin` holds the firmware's software-float routines on
  edge and random operands, and `tables.bin` every lookup table as the firmware
  holds it. The float converter is checked on every float pattern
  (`make check-convert`).
- **Packaging:** `make check-install` installs the library into a staging
  directory and builds a C and a C++ host from `gm2.h` and `libgm2.a` alone.
- **Host additions:** the tempo rule, LFO alignment and the context have no
  firmware counterpart, so the `host` item checks them against their definitions.

## Limits

- Everything is recorded at 48 kHz. Other accepted rates (below 48 kHz) run the
  same code paths but are not compared with the firmware.
- Each fixture is one scripted run. Behaviour that needs a longer run, or a knob
  combination the script never reaches, is not covered.
- Internal state is not compared, only outputs and setup words.
- The control tick and job schedule are the port's fixed timing. The recorder
  uses the same schedule, so the fixtures check the arithmetic, not the device's
  real background timing.

## Adding an effect

Describe it once: a knob table and native adapter in `tests/effect_<name>.c`, a
firmware adapter in its block's `tests/firmware_<block>.c`, and one line in
`tests/gm2_effects.h`. Then `make record-fixture FIXTURE=<name>`, `make check`,
and `make coverage`, and confirm that a deliberately broken constant makes the
check fail.
