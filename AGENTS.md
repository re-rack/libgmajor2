# G-Major 2 Firmware Reverse Engineering and Native Effects

## Project goal

This project has two connected goals:

1. Reverse engineer the G-Major 2 v1.02 DSP56362 firmware to recover its effect algorithms, parameter setup, arithmetic, and state behavior.
2. Reimplement every guitar effect as a reusable native C17 library, one effect at a time, rather than emulating the whole device.

The original firmware running in the xemu function-level harness is the reference oracle. Native implementations should be bit-exact by default, including coefficient generation, arithmetic order, rounding, saturation, wrapping, and persistent state. Static analysis and emulation support the native library; they are not the final deliverable.

## Verified facts

- `make ghidra-prepare` extracts the **G-Major 2 v1.02** firmware for a **DSP56362** (DSP56300 family) from TC Electronic's updater into `reverse_engineering/firmware/` as its DSP load records: 24-bit big-endian payloads for the bootstrap, application code, configuration/data and presets, plus the terminator and filler.
- There is **no AVR firmware**: the device's AVR bootloader receives the load stream and loads the DSP.
- One load record changes from data to code at `0x82AA2`, confirmed by disassembly; it is split there into `05a`/`05b`.

## Reference documentation

- `reverse_engineering/downloads/G_Major_II_US.pdf`, which `make ghidra-prepare` downloads, is the official 58-page English **G-Major 2 User's Manual** of 2009.
- Use it to connect firmware code and data to intended device behavior: signal flow, routing, presets and preset backup, global/MIDI/MOD menus, external control and modifiers, relays, effect names and parameters, MIDI implementation, and technical specifications.
- It is especially useful when annotating effect engines, parameter tables, preset data, UI strings, MIDI handlers, and hardware I/O behavior.
- Treat it as a behavioral reference, not proof of firmware layout, DSP instruction semantics, or peripheral implementation; verify low-level claims against the firmware and DSP56362 documentation.
- To search the text without generating another project artifact: `pdftotext -layout reverse_engineering/downloads/G_Major_II_US.pdf /tmp/gmajor2_manual.txt`.
- Page numbers here are the manual's printed numbers, two less than the PDF page: printed page 30 is PDF page 32.

## Layout

| Path | Purpose |
|---|---|
| `reverse_engineering/` | The reverse-engineering scripts and docs; `make ghidra-prepare` writes TC's downloads, the firmware, the disassembly, call graph, seeds and Ghidra project under it, all untracked (`reverse_engineering/README.md`) |
| `reverse_engineering/fetch_firmware.py` | Download TC's updater and manual and extract the load records |
| `reverse_engineering/build_workspace.py` | Disassemble the records and write the portable analysis artifacts |
| `reverse_engineering/build_ghidra.py` | Build and export the Ghidra project using PyGhidra only |
| `reverse_engineering/ghidra/GMajor2.gpr` | The Ghidra project `make ghidra-prepare` builds; bootstrap and application are separate programs |
| `reverse_engineering/harness/` | Emulator plumbing (`gm2_emu.{c,h}`): loads the firmware, applies its initializer, installs a rate and calls routines; the test recorder drives it |
| `src/` | Native C17 implementations of every effect, their parameter setup, shared family cores and designs, sample-rate and LFO helpers, parameter jobs, arithmetic, lookup tables and float <-> Q23 conversion. Each effect is `gm2_X.{h,c}` plus `gm2_X_setup.{h,c}`; see Code conventions. `gm2.h` is the one header a host includes |
| `tests/` | The test system: the recorder writes the firmware's answers to `tests/fixtures/<name>.bin` and `make check` checks the library against them offline (see Testing) |
| `Makefile` | Builds the library and the test programs into `build/` and runs every check and tool (see Testing) |
| `README.md` | The project's front page: what the library is, its effects, dependencies and make targets |
| `reverse_engineering/EFFECTS.md` | Verified firmware map and reusable findings for native effect implementations |

## Native effect progress

The checklist follows the effect algorithms in the user manual, with each algorithm's printed manual page in parentheses. **Complete** requires a native parameter-setup path, an audio kernel, the library's API with all four process entry points (see Library API) and a recorded fixture that passes `make check`. Identified addresses or partially understood code do not count as an implementation.

| Effect family | Algorithms (manual page) | Native status |
|---|---|---|
| Gate & EQ | Three-band EQ (30) | **Complete** |
| Gate & EQ | Noise Gate (30) | **Complete** |
| Filter | WahWah (31), TouchWah (31), Auto Wah (32), Resonance Filter (32), Touch Resonance Filter (33) | **Complete** |
| Filter | Tremolo (33), Panner (34) | **Complete** |
| Compressor | Compressor (35) | **Complete** |
| Pitch | Detune (36), Whammy (36), Octaver (37), Pitch Shifter (38), Intelligent Pitch Shifter (39) | **Complete** |
| Modulation | Classic Chorus (40), Advanced Chorus (40), Tri-Chorus Normal (41), Tri-Chorus Asymmetric (41) | **Complete** |
| Modulation | Classic Flanger (42), Advanced Flanger (43), Through Zero Flanger Negative (43), Through Zero Flanger Positive (43) | **Complete** |
| Modulation | Vibrato (44), Vintage Phaser (45), Smooth Phaser (45), Vintage Univibe (46), Modern Univibe (46) | **Complete** |
| Delay | Ping Pong (47), Dynamic (48), Dual (48), Reverse (48) | **Complete** |
| Reverb | Spring (49), Hall (49), Room (49), Plate (49) | **Complete** |

The EQ, noise gate and compressor are standalone modules. Effects that run one firmware routine share a family core and design: Tremolo/Panner use `gm2_lfo_gain_*`, the wahs `gm2_wah_*`, the resonance filters `gm2_res_*`, the pitch effects `gm2_pitch_*`, the phasers `gm2_phaser_*`, the univibes `gm2_univibe_*`, the choruses, flangers and Vibrato `gm2_moddelay_*`, and the delays `gm2_delay_*`. The Reverb is `gm2_reverb_*`; the Touch Res Filter and Dynamic Delay also run `gm2_gate_detector_*`. `reverse_engineering/EFFECTS.md` maps these modules to their firmware routines; `tests/fixtures/` holds their recordings.

Tuner behavior, routing, presets, MIDI, modifiers, relays, and hardware I/O are supporting firmware areas, not additional effect algorithms in this checklist.

## Float interface

Plugins exchange 32-bit float audio; the bit-exact kernels work on Q23, a signed 24-bit sample held in an `int32`. `src/gm2_convert.h` converts both ways: f32 to Q23 scales by 2^23, rounds to nearest even, turns NaN into 0 and saturates to `[-0x800000, 0x7FFFFF]`; Q23 to f32 reads bits 23..0 as a signed value and scales by 2^-23, which is exact.

The path is fixed when the sources are compiled, with no runtime dispatch and no global state: AVX-512, then AVX2, then SSE2 (the x86-64 baseline), then AArch64 NEON, then the RISC-V vector extension (V or Zve32f), else scalar. `-DGM2_NO_SIMD` forces the scalar path, `-march=x86-64-v3` or `-march=x86-64-v4` selects AVX2 or AVX-512, and `-march=rv64gcv` selects RVV; `gm2_convert_simd()` names the compiled path. The SSE2, AVX2 and scalar paths round in the current FP rounding mode, so the default round-to-nearest is assumed; FTZ and DAZ cannot change a result.

An effect's float wrapper takes planar left and right buffers, converts through a fixed stack chunk and may write each output into its input buffer, since hosts process in place. Nothing allocates.

## Library API

Every effect has the same interface, the instance first and the knob struct by const pointer; `src/gm2.h` states its contract once:

```c
int  gm2_X_setup_config(const gm2_context *context, const gm2_X_setup_raw *raw, gm2_X_config *config);
void gm2_X_state_init(gm2_X_state *state, const gm2_X_config *config);
void gm2_X_process_split(gm2_X_state *state, const gm2_X_config *config, const int32_t *in_l,
                         const int32_t *in_r, int32_t *out_l, int32_t *out_r, size_t frames);
void gm2_X_process_f32(gm2_X_state *state, const gm2_X_config *config, const float *in_l,
                       const float *in_r, float *out_l, float *out_r, size_t frames);
void gm2_X_process_interleaved(gm2_X_state *state, const gm2_X_config *config, const int32_t *in,
                               int32_t *out, size_t frames);
void gm2_X_process_frame(gm2_X_state *state, const gm2_X_config *config, const int32_t in[2],
                         int32_t out[2]);
void gm2_X_align(gm2_X_state *state, const gm2_X_config *config, double beats); /* LFO effects */
```

Setup forces every raw value into its descriptor window, as the firmware's accessor `P:0x0832CB` does, and returns 0, or -1 for a NULL pointer or a context without a rate. The EQ also returns -1 for a band at or above half the sample rate, a branch of the firmware's chain that is not ported. A knob change is implicit: the host reruns the setup and passes the new config, and the effect applies it from the first frame it processes after the change; there is no publish or apply call. No public function, parameter or constant exists for tests alone.

A host includes `gm2.h`, which collects every effect's headers inside `extern "C"`, and links `libgm2.a`. `make build` writes `build/default/libgm2.a`; `make install` copies it, every header into `include/gm2/` and a `gm2.pc` under `PREFIX` (default `/usr/local`; `DESTDIR` stages), so `pkg-config --cflags --libs gm2` gives the flags. The library is compiled with `-fPIC` and hidden visibility, so it links into a plugin's shared object without exporting its symbols. The conversion path follows the build's flags: `make build BUILD=build/v3 CFLAGS='-O2 -march=x86-64-v3'` builds an AVX2 library and `make install BUILD=build/v3` installs it. Every header stays valid C++17, because C++ hosts read the inline helpers too: `restrict` and `_Static_assert` stay out of what C++ reads.

## Code conventions

Every module in `src/` follows one pattern, so that all effects read as one codebase.

**Modules and names**

- An effect X is four files: `gm2_X.h` (config, state, the process entry points, align), `gm2_X.c`, `gm2_X_setup.h` (the raw struct and `gm2_X_setup_config`) and `gm2_X_setup.c`; `gm2.h` includes the setup header.
- A family's shared audio routine is `gm2_<family>_core.{h,c}`: its config and state, `gm2_<family>_core_state_init`, `gm2_<family>_core_process_frame` and, with an LFO, `gm2_<family>_core_align`. A core has no buffer entry points; its state_init and process_frame take only what the routine needs, directly from the effect's coefficients where no separate core config is useful.
- A family's shared setup math is `gm2_<family>_design.{h,c}`: functions over raw values, taking `const gm2_rate_words *rate` first where they depend on the rate. They validate nothing and cannot fail.
- Everything a header declares starts with the header's module name: `gm2_<module>_...`, `GM2_<MODULE>_...`. Only `gm2_base.h` declares library-wide names (`gm2_context`, `GM2_CHANNELS`, `GM2_CHUNK_FRAMES`).
- Inside a `.c` file: static functions are `<module>_<name>` without `gm2_` (`gate_gain`, `lfo_gain_level`); constants are enum constants `<MODULE>_<NAME>` without `GM2_`; software-float constants are `static const gm2_swf k_<name> = GM2_SWF_CONST(0x......u, 0x..u);` with the value in a trailing comment (the exact ones every setup shares, `gm2_swf_one` and its neighbours, are in `gm2_swf.h`); other constant data is `static const ... k_<name>`. A file that clamps raw knob values (a `_setup.c`, a `_design.c`, `gm2_tick.c`) names its descriptor windows `<FIELD>_MIN` and `<FIELD>_MAX` in one enum.

**Types**

- An effect whose config and state are exactly its core's aliases them (`typedef gm2_<family>_core_config gm2_X_config;`); one with words of its own embeds the core's as a first member named `core`.
- A config holds `int32_t coef[GM2_<MODULE>_COEFS]`, the words the firmware's updaters stage, in the firmware's order, with an enum naming the positions the code refers to. Beside it sit only the fields that are not such words: `raw[]` (the raw values of the knobs whose turns arm a ramp), `note` (the Tempo value align follows) and what the effect alone needs.
- A state holds the kernel's own words, `coef[]` (the live words gliding to the config's), `ramp_step[]`, `raw[]` (the config's at the last job run), `chunk_pos` (the frame within the chunk) and `tick` (chunks since the last tick).
- Every word in a config or a state is a signed 24-bit value, sign-extended in its `int32_t`; a setup never stores an unsigned 24-bit pattern. Only audio input is masked on reading, with `gm2_s24((uint32_t)in[channel])`.
- Nothing is kept because the firmware keeps it: a scratch word is a local, a memory word that never changes is a constant, a staged copy of a config word does not exist, and a word no output depends on is not computed.
- Raw fields are named after the knob in lower case with underscores (`hi_cut`, `pulse_width`, `feedback_1`), and their comment gives the range and the meaning, never a default.

**Setup**

```c
int gm2_X_setup_config(const gm2_context *context, const gm2_X_setup_raw *raw, gm2_X_config *config) {
  const unsigned rate = gm2_context_rate(context);
  if (raw == NULL || config == NULL || rate == 0u) {
    return -1;
  }
  const int depth = gm2_clamp_raw(raw->depth, DEPTH_MIN, DEPTH_MAX);

  gm2_rate_words rate_words;
  gm2_rate_words_init(&rate_words, rate);
  ...
  return 0;
}
```

Every setup starts with this refusal, then clamps, then builds its rate words where it needs them. A raw value is clamped exactly once, by the function that converts it: the effect's setup for its own knobs, the family's design function for the knobs it converts. Software-float math calls `gm2_swf_*` directly, in mathematical operand order.

**Process**

```c
/* One stereo frame, P:0x...., then the chunk's job run once its last frame is done. */
static void x_frame(gm2_X_state *state, const gm2_X_config *config,
                    const int32_t in[GM2_CHANNELS], int32_t out[GM2_CHANNELS]) {
  ...
  if (++state->chunk_pos < GM2_CHUNK_FRAMES) {
    return;
  }
  state->chunk_pos = 0;
  x_jobs(state, config);
}

GM2_PROCESS_DEFINE(X, x_frame)
```

`GM2_PROCESS_DEFINE` (`gm2_process.h`) defines the four process entry points from the frame function; an effect that only runs its core passes `gm2_<family>_core_process_frame` itself. `x_jobs` is the chunk's job run: it steps each live word towards the config's with the jobs of `gm2_job.h`. The frame multiplies by the block level words the config carries: the input word `GM2_LEVEL_INPUT` always, and the effect's wet and dry words, which are `GM2_LEVEL_WET` and 0 without a Mix knob and the Mix pair otherwise.

**Shared pieces**

An effect never keeps its own copy of what another module already has:

| Header | What it shares |
|---|---|
| `gm2_dsp_math.h` | The DSP56300 arithmetic (`gm2_mpy`, `gm2_mac`, `gm2_store24`...), `gm2_clamp_raw`, the soft clip `gm2_soft_clip` |
| `gm2_swf.h` | The software float, its exact constants (`gm2_swf_one`...), and the firmware's unit maps `gm2_swf_hz20`, `gm2_swf_hz80`, `gm2_swf_db` |
| `gm2_rate.h` | The words derived from the sample rate |
| `gm2_tables.h` | Every firmware table and the steps that read them: `gm2_log2`, `gm2_exp2`, `gm2_wave_read` |
| `gm2_lfo.h` | The LFO rate, step and beat, the phase advances `gm2_lfo_advance` and `gm2_lfo_advance_restart`, and `gm2_lfo_phase_at` |
| `gm2_job.h` | The parameter jobs: `gm2_job_smooth`, `gm2_job_ramp_arm`, `gm2_job_ramp`, `gm2_job_fade` |
| `gm2_tick.h` | The control tick: its period, the peak hold, the touch follower and its Response and Sense words |
| `gm2_biquad.h` | The double-precision biquad of the EQ, the resonance filters and the chorus output |
| `gm2_gate_detector.h` | The Noise Gate's level detector |
| `gm2_mix.h` | The Mix knob's wet and dry words: `gm2_mix_words` and the curve half `gm2_mix_curve` |
| `gm2_process.h` | The block level words and `GM2_PROCESS_DEFINE` |

**Comments and order**

- A header's file comment says what the module is and which firmware routines it ports. The interface functions carry no comment unless the effect has something of its own to say: the contract is in `gm2.h`.
- A header declares state_init, process_split, process_f32, process_interleaved, process_frame, align, in that order.
- A `.c` file orders: includes (its own header, then the others alphabetically), constants, static helpers, the frame function, state_init, `GM2_PROCESS_DEFINE`, align.

## Effect independence

Every effect becomes its own audio plugin, so each is a standalone module with its own config, state and process functions. Effects otherwise share only the common helpers and their family's core and design named in Code conventions. The Noise Gate's level detector (`gm2_gate_detector`) also serves the Touch Res Filter and Dynamic Delay. DSP arithmetic, the software float, sample-rate values, firmware lookup tables and their readers, LFO setup and parameter jobs always live in common helpers, never in an effect module. Where several effects run one firmware audio routine, that routine is one shared core, and each effect keeps only its own config, state, setup and small process wrapper.

The firmware groups effects into blocks, one per front-panel key; Gate & EQ is one block holding two effects. Do not port that layer: no block objects, task chain or routing, block on/off or kill flags, and none of the block level code that computes input-gain, mix and output-level words. Where an effect's kernel multiplies by such a word, keep the multiply but build the word in the effect, and never expose it in the API: the input word is `GM2_LEVEL_INPUT` for every effect, and the wet and dry words are `GM2_LEVEL_WET` and 0 for an effect without the device's Mix knob, the pair `gm2_mix_words()` gives for one with it, at Out Level 0 dB. Never assume the values: the recorder reads the words the block's own level code stages (through `gm2_block_levels()` for the Gate & EQ and Compressor blocks, otherwise from the block's own level job) and the checker requires them to equal the effect's. These words differ from their power-on values; a full mix, for example, gives a wet word of `0x7FFFFE`. The multiplies cannot be dropped: such words sit just below 1.0, so each one can move a sample by one LSB. The recorder still drives the firmware's blocks, because that is how the firmware runs; that is test tooling, not library code.

In code and docs, "block" means only such an effect block. The firmware's four-frame processing unit is a chunk, and a caller's buffer is a buffer.

## Host integration

- **Sample rates up to 48 kHz.** The effects' buffers are the device's sizes, so above 48 kHz the delays would read past them or wrap; `gm2_context_init` refuses faster rates (`GM2_SAMPLE_RATE_MAX`) and a host running faster resamples.
- **The Mix knob is the device's.** Where the device has one, the effect takes it as a raw knob and its setup builds the wet and dry words its kernel multiplies by, following the block-word rule above; the host sets the Mix in the effect and keeps no blend of its own. Out Level is not a knob: every effect runs at Out Level 0 dB. An effect the device gives no Mix knob — the Whammy, the Vibrato, the Tremolo, the Panner, the through-zero flangers, the Compressor and the Gate & EQ blocks — keeps the device's own blend. Kill Dry is not ported.
- **Tempo sync follows the host's tempo.** Setup takes the beat length in milliseconds, fractions included, forced into 100–3000 ms; a NaN, which a wrapper passes when the host reports no tempo, takes the hardware's factory Tap Time, 500 ms (`P:0x083A95`). The device holds its tempo as whole milliseconds per beat, so whole-millisecond lengths run the firmware's own tempo math and are bit-exact; other lengths run the same math on the exact length and cannot be checked against the firmware.
- **Tempo wins.** With a Tempo value set, the beat decides and Speed or Delay do nothing; at Ignore, Speed or Delay decides, as the manual describes. The device's own one-knob edit can let a later Speed or Delay turn override Tempo; the ports follow the knob values alone, as after a preset load.
- **Knob turns follow the device.** On a change each effect moves its words exactly as the firmware's parameter edit and jobs do: words glide through the one-pole jobs, ramp through `P:0x09605F` records that every turn of their knob re-arms, crossfade between delay taps, or change at once.
- **LFOs line up with the host's beat.** Each LFO effect gets a call that sets the LFO position from the song position, for the host to use at playback start and after jumps. From there the LFO runs on the firmware's arithmetic; computing the position is our addition.
- **The control tick is fixed.** The device moves some effects from its background loop: a timer tick it takes as 13 ms sends the blocks a control message, a 1 kHz hardware timer times the Reverse Delay's grain splices, and smoothing and crossfade jobs walk parameter and tap-gain words towards their targets whenever its task scheduler gets to them. None of these is locked to the audio. The ports run the control tick every 13 ms worth of whole four-frame chunks (`gm2_tick_chunks()`: 143 at 44.1 kHz, 156 at 48 kHz), the Reverse Delay's grain splice every `delay_ms` worth of whole four-frame chunks (`gm2_tick_period(rate, delay_ms)`), restarted by a Delay or Tempo turn, and every smoothing or crossfade job once after each chunk, and the recorder runs the firmware's own routines on that same schedule. The arithmetic is the firmware's; only the timing is ours.
- **Effects start at rest.** A fresh state is the one the effect settles in after a long silence: filters and detectors empty, smoothed words on their targets. A parameter the device carries over from another effect's page and shows on no page of its own is not ported.

## Testing

The emulator only records; every check runs offline. `tests/README.md` explains the method and its limits; this section holds the rules.

**Test code never enters `src/`.** `src/` holds library code only: no coverage counters, no functions or macros only tests use, no mentions of tests, fixtures or the emulator. Tests reach the library through its public API alone, and coverage comes from gcov.

- `tests/gm2_record.c` runs the firmware in the emulator and writes its answers to `tests/fixtures/<name>.bin`. `tests/gm2_check.c` replays every fixture through the library. Expected values come only from the firmware.
- Everything runs at 48 kHz.
- An effect is described once: its knob table and native adapter in `tests/effect_<name>.c`, its firmware adapter in its block's `tests/firmware_<block>.c`, and one line in `tests/gm2_effects.h`. There are no per-effect test programs.
- An effect's fixture holds:
  - **Setup:** each knob on its own with the others at default, at every position for a knob with 32 positions or fewer, otherwise at 32 positions evenly spaced from minimum to maximum plus the default, with the firmware's setup words; plus four multi-knob presets, every knob active at 33 % and at 66 % (Tempo at Ignore, switches flipped, continuous knobs at that share), at its minimum and at its maximum, each built one knob at a time and each stepped through min, 33 %, 66 %, max, default and its preset value.
  - **Level words:** the input, wet and dry words the block's level code stages at Out Level 0 dB, which must equal the ones the effect's own knobs give: `GM2_LEVEL_INPUT` with `GM2_LEVEL_WET` and 0, or the Mix pair at the fixture's default Mix.
  - **Audio:** one run on the generated stereo input (`tests/gm2_signal.c`: a 4,096-frame pattern of loud noise, held notes, bursts, quiet noise, a sweep and clicks, with a four-cycle window that replaces one cycle with a clean 110 Hz to 300 Hz note transition and the start of the next with 300 Hz). It starts at the defaults, moves each knob through its minimum, 33 %, 66 %, maximum and default, 4,096 frames each (a delay-time knob holds its maximum for 98,304), then two more at the defaults; then walks every knob to an active setting (Tempo at Ignore, switches flipped, continuous knobs at 66 %), sweeps each knob through 33 %, maximum and back to its active setting, gives each knob ten 64-frame fast turns around its active setting and walks the knobs back to their defaults; then one default segment, ten fast turns per knob from the defaults, and the defaults again. Each turn lands while the previous turn's glide still moves. Segment i runs i % 4 frames longer, so the turns land on every frame of a four-frame chunk. Every output sample is stored.
- **The recorder follows the device.** A knob turn is the device's own parameter edit (`P:0x083268`: the raw word, the descriptor callback and, where the descriptor asks for it, its second call), then the raw words other callbacks rewrote are put back, then, only where that edit leaves words that depend on the order of turns, the firmware routine that rebuilds them from the knob values (the Tempo rule). After every four frames the recorder runs exactly the job records the firmware holds, each through its settled check and handler (`gm2_firmware_jobs()`, `gm2_firmware_run_job()`), and fails if the list differs from what the adapter expects. An adapter never copies staged words onto live ones and never takes a recorded value from the library.
- **The checker** replays each run through the split, float and interleaved entry points with random buffer sizes and through the per-frame one, with junk in the paired inputs' upper bytes, and checks that every knob's setup at min-1 and max+1 equals its setup at min and max. The `host` item checks the beat rule, LFO alignment and the context because they have no fixture (`make check-host`).
- Internal state is not compared, and inputs are generated, never stored.
- `swf.bin` holds the firmware's software-float routines on edge and fixed-seed random operands, and `tables.bin` every table in `src/gm2_tables.c` as the firmware holds it at 48 kHz. The float <-> Q23 converter is tested natively against its definition (`tests/test_convert.c`).
- `tests/gm2_bench.c` times each effect's `process_split` kernel on the generated signal at 48 kHz in 128-frame buffers: setup and conversion stay outside the timed region, a 1 s warm-up precedes seven timed 2 s runs, and the median run is reported in nanoseconds per stereo frame with its share of real time. Each effect runs at one fixed knob setting chosen for processing-code coverage.

```sh
make build                          # build/default/libgm2.a; also what plain make does
make install                        # libgm2.a, the headers and gm2.pc under PREFIX; uninstall removes them
make check-install                  # a C and a C++ host built against a staged install
make check                          # offline: every fixture and the host checks
make check-fixture FIXTURE=<name>   # one fixture; check-swf and check-tables are shortcuts
make check-host                     # the beat rule, LFO alignment and context
make benchmark-all-fx               # every effect's ns per stereo frame
make benchmark-fx FX=<name>         # one effect, or several names in quotes
make check-convert                  # all 2^32 floats on every conversion path, NEON and RVV under qemu
make record-fixture FIXTURE=<name>  # re-record one fixture; record-all-fixtures does all (fetches the firmware first)
make coverage                       # gcov line and branch coverage of src/
make format                         # apply clang-format
make static-check                   # format check, cppcheck, clang and gcc warnings and analyzers
make sanitize                       # fixtures, host checks and quick converter test under ASan and UBSan, gcc and clang
make clean                          # remove every build artifact
make ghidra-prepare                 # fetch the firmware and manual, build the disassembly and Ghidra project
make ghidra-clean                   # remove what ghidra-prepare generates, keeping the downloads and the Ghidra project
```

Re-record only when an effect's description, the input signal or the fixture format changes, never because `src/` changed; recording is deterministic. When adding an effect, run `coverage` and confirm that a deliberately broken constant makes `check` fail. Run `check-convert` when the converter changes; it needs `aarch64-linux-gnu-gcc` and `qemu-aarch64` for the NEON path and `riscv64-linux-gnu-gcc` and `qemu-riscv64` for the RVV path, which runs at 128- and 1024-bit vectors and built for Zve32f. Run `check-host` when the tempo setup in `src/gm2_lfo*` or `src/gm2_delay_design*` changes. `format` and `static-check` cover every C file in `src/`, `tests/` and `reverse_engineering/harness/`; the style is the root `.clang-format`, and the tools are overridable with `CLANG_FORMAT`, `CPPCHECK`, `CLANG` and `GCC`. The root `Makefile` compiles every C file with one warning set, warnings as errors, and writes all output to `build/`.

**Before every commit**, run `make format`, `make static-check`, `make sanitize`, `make check` and `make check-install`, and fix every finding.

## Load record invariants

- Load addresses are DSP **word addresses**. Ghidra address creation uses byte offsets, so multiply word addresses by three.
- Extracted DSP payloads are 24-bit **big-endian**. Ghidra's DSP56300 extension reads per-word byte-reversed **LE24** words; `build_ghidra.py` reverses them in memory and checks every record against `manifest.csv`; `make ghidra-prepare` also writes LE24 copies to `build/ghidra/` for an import by hand.
- Bootstrap and application vectors both start at address zero but run at different stages. Never import them into one program at that address.

## Main code regions

| Region | Load address |
|---|---|
| Bootstrap | `0x000000` |
| Application vectors | `0x000000` |
| Low program | `0x000030` |
| Middle code | `0x082AA2` |
| High program | `0x08CE20` |
| Init | `0x098000` |

Application reset is `0x000097`. The terminator also points to `0x97`.

## Tooling

### xemu DSP56300 toolkit — one project

**`dsp56300-disasm`, `dsp56300-asm`, `dsp56300-emu`, and the C emulator API all come from the SAME repository: <https://github.com/xemu-project/dsp56300>** (also associated with `mborgerson/dsp56300`). They are not unrelated tools. In particular, `dsp56300-disasm` is the disassembler executable from the same project as the emulator recommended for running firmware functions.

| Component | Installed path | Purpose |
|---|---|---|
| Assembler CLI | `/usr/local/bin/dsp56300-asm` | Assemble DSP56300 source |
| Disassembler CLI | `/usr/local/bin/dsp56300-disasm` | Produce assembly and the portable call graph/seeds used here |
| JIT emulator CLI | `/usr/local/bin/dsp56300-emu` | Execute DSP56300 code and audio-effect examples |
| Example runner | `/usr/local/bin/dsp56300-run` | Assemble and run the bundled examples |
| C emulator library | `/usr/local/lib/libdsp56300_emu_ffi.a` | Link custom function-level emulation harnesses |
| C emulator header | `/usr/local/include/dsp56300.h` | Memory maps, callbacks, state, stepping, and execution API |
| Documentation/examples | `/usr/local/share/doc/dsp56300/` | Emulator limitations, architecture notes, and `.a56` examples |

All three main CLI tools are version 1.0.0. The release workspace passed 1,854 tests, and the installed C library passed its quickstart. Link C harnesses with `-ldsp56300_emu_ffi -ldl -lpthread -lm`.

The xemu emulator is a **CPU core**, not a G-Major 2 device emulator. Its CLI is an audio-effect runner, not a loader for this firmware. For this project, use the C FFI to load records, map memory, set registers/PC, and provide HDI08, DMA, ESSI, and other hardware behavior through callbacks.

The separate `dsp56300/dsp56300` C++ project is useful only as a reference for ready-made DSP56362 peripherals such as HDI08 and ESAI; it is not the source of the tools listed above.

### Ghidra and Python

- Ghidra is at `/opt/ghidra`.
- Use the **Ghidra-Rare DSP56300 fork** (`aleph-void/ghidra-dsp56300`) installed at `/opt/ghidra/Ghidra/Extensions/DSP56300/`. This is a separate static-analysis/decompilation extension, not the xemu emulator. Reinstall it by unzipping the fork release built for the installed Ghidra version (`ghidra_12.2_PUBLIC_*.zip`) into `/opt/ghidra/Ghidra/Extensions/`.
- Use **PyGhidra only** for new Ghidra automation. Do not add custom Java scripts or `analyzeHeadless` wrappers.
- PyGhidra 3.2.0 is installed persistently at `/home/cosku/.local/share/gmajor2/pyghidra/`; use its `bin/python` for `build_ghidra.py`.
- Python 3 scripts use the standard library unless documented otherwise. `ruff` and `uv` are on `PATH`; run `ruff check` and `ruff format --check` after Python edits.

## Regeneration

Run from the project root:

```sh
make ghidra-prepare
```

It downloads TC Electronic's G-Major 2 v1.02 updater and manual from the Internet Archive's copies of TC's download server into `reverse_engineering/downloads/`, checks their SHA-256, extracts the load records into `reverse_engineering/firmware/` and checks each one's SHA-256, then runs `build_workspace.py` and `build_ghidra.py` (`--prepare` for the LE24 files, then the project through the persistent PyGhidra environment). When a download fails it lists the files with their names and SHA-256 for the user to put into `reverse_engineering/downloads/`. It keeps an existing Ghidra project; to build another, run `build_ghidra.py` with a new name from the PyGhidra environment:

```sh
/home/cosku/.local/share/gmajor2/pyghidra/bin/python \
  reverse_engineering/build_ghidra.py GMajor2-fresh
```

If the environment needs to be recreated, install Ghidra's bundled offline package with uv:

```sh
uv venv /home/cosku/.local/share/gmajor2/pyghidra
uv pip install --python /home/cosku/.local/share/gmajor2/pyghidra/bin/python \
  --no-index -f /opt/ghidra/Ghidra/Features/PyGhidra/pypkg/dist pyghidra
```

`reverse_engineering/README.md` documents the workspace and is maintained by hand; `build_workspace.py` writes only analysis artifacts and does not modify the Ghidra project.

## Current Ghidra baseline

- Application: 9 memory regions, 36,222 instructions, 601 functions.
- Bootstrap: 1 memory region, 94 instructions, 3 functions.
- Cross-region call `0x082AE4 -> 0x082AA2` resolves.
- Reset, middle-code, high-code, and init samples decompile successfully.
- Portable call graph: 3,809 direct `jsr`/`bsr` edges, 664 candidate functions, 2,957 disassembly seeds.

These are analysis results, not complete function or instruction coverage.

## Boundaries and safeguards

- Never commit TC Electronic's files or the reverse-engineering outputs that carry the firmware's bytes: the downloads, the records, the disassembly, LE24 copies and the Ghidra project stay in their gitignored paths. Never modify them by hand; the scripts check the downloads and records against their SHA-256.
- Do not claim exact P/X/Y physical memory-space assignments; current analysis uses numeric load addresses as a flat address view.
- Direct call graphs omit indirect/computed calls, jump tables, and embedded data. Candidate function targets are not proven function boundaries.
- Full emulation requires hardware shims for the TC bootloader, HDI08/host protocol, external memory, codec, MIDI, and board-specific behavior.
- Keep generated work separate from interactive Ghidra analysis. Never delete or overwrite a project that may contain user annotations without explicit permission.
