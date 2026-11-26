# G-Major 2 effects

A C17 library that re-creates every guitar effect of the TC Electronic G-Major 2
for use as audio plugins. Each effect is bit-exact: for the same input and knob
settings it produces the same output samples as the original hardware.

- 36 effects, each a standalone module with the same small API
- 32-bit float and 24-bit fixed-point (Q23 as in original hardware) entry points
- no allocation, no global state, any buffer size
- tempo sync to the host's beat and LFO alignment to the song position
- SIMD float conversion: AVX-512, AVX2, SSE2, NEON, RISC-V V, fallsback to scalar

## Effects

| Family | Effects |
|---|---|
| Gate & EQ | Three-band EQ, Noise Gate |
| Filter | WahWah, TouchWah, Auto Wah, Resonance Filter, Touch Resonance Filter, Tremolo, Panner |
| Compressor | Compressor |
| Pitch | Detune, Whammy, Octaver, Pitch Shifter, Intelligent Pitch Shifter |
| Modulation | Classic and Advanced Chorus, Tri-Chorus (normal, asymmetric), Classic and Advanced Flanger, Through-Zero Flanger (negative, positive), Vibrato, Vintage and Smooth Phaser, Vintage and Modern Univibe |
| Delay | Ping Pong, Dynamic, Dual, Reverse |
| Reverb | Spring, Hall, Room, Plate |

## De-popped variants

Some knobs click or pop when turned fast, because the hardware does. For those effects `gm2_X_alt` is the same effect without the click: same knobs, setup and API, and bit-exact with `gm2_X` again once the knobs have settled.

| Variant | Knobs |
|---|---|
| `eq_alt` | Freq, Gain, Width (a band below ~120 Hz could rail) |
| `resfilter_alt` | Style, Hi-Cut, Resonance |
| `pitch_alt`, `ipitch_alt` | Dly1, Dly2 |
| `revdelay_alt` | Delay, Tempo |
| `reverb_alt` | Size, Pre-Delay, Shape, Hi Color |

A variant runs the effect and a de-popped copy side by side from a knob turn until both have settled, then fades back to the effect within about 2 s, so it costs about twice the effect while a knob moves. With feedback, the effect's own click can still sound in the echoes after that.

## Using the library

```sh
make                 # build/default/libgm2.a
sudo make install    # libgm2.a, the headers and gm2.pc into /usr/local
cc plugin.c $(pkg-config --cflags --libs gm2)
```

Default build uses SSE2, `CFLAGS='-O2 -march=x86-64-v3'` builds the AVX2 converter and `CFLAGS='-O2 -march=x86-64-v4'` builds it against AVX512. ARM and RISC-V uses vector extensions by default if your CPU supports it.

Sample rates go up to 48 kHz, the hardware's own: `gm2_context_init` refuses faster ones, so a host running faster resamples around the effect.

## Testing and Benchmarks

```sh
git clone git@github.com:tapir/libgmajor2.git
make check                # test output of every effect against its recorded hardware output
make benchmark-all-fx     # ns per stereo frame for each effect
```

| Target | What it does |
|---|---|
| `make check` | replays every effect's recorded firmware output through the library, and checks each de-popped variant against its effect |
| `make check-fixture FIXTURE=<name>` | one effect |
| `make check-install` | build a C and a C++ program against an installed copy |
| `make benchmark-fx FX=<name>` | time one or more effects |
| `make check-convert` | every float on every conversion path |
| `make static-check` | clang-format, cppcheck, gcc and clang warnings and analyzers |
| `make sanitize` | the checks under ASan and UBSan, with gcc and clang |
| `make coverage` | gcov line and branch coverage |
| `make record-fixture FIXTURE=<name>` | re-record an effect's fixture from the firmware |
| `make ghidra-prepare` | fetch the firmware and manual, build the disassembly and Ghidra project |
| `make ghidra-clean` | remove what `ghidra-prepare` generates, keeping the downloads and the Ghidra project |

The repository holds neither the firmware nor the manual. Recording fixtures and `make ghidra-prepare` download TC's firmware updater and manual from the Internet Archive and extract the firmware locally ([`reverse_engineering/README.md`](reverse_engineering/README.md)).

## Dependencies

- `make`, `gcc` and `clang` (C17)
- `clang-format` and `cppcheck`: static checks
- `aarch64-linux-gnu-gcc` and `qemu-aarch64`: the NEON conversion check
- `riscv64-linux-gnu-gcc`, `riscv64-linux-gnu-glibc` and `qemu-riscv64`: the RISC-V vector conversion check
- xemu DSP56300 toolkit ([xemu-project/dsp56300](https://github.com/xemu-project/dsp56300)): the emulator library for recording fixtures and `dsp56300-disasm`
- `python`, `ruff` and `uv`: the workspace and Ghidra scripts
- Ghidra 12.2 with the Ghidra-Rare DSP56300 extension ([aleph-void/ghidra-dsp56300](https://github.com/aleph-void/ghidra-dsp56300)) and PyGhidra
- `perf`: optional, for profiling

## Layout

| Path | Contents |
|---|---|
| `src/` | the library: one module per effect, shared family cores and helpers |
| `tests/` | the checker, the recorder, the benchmark and the recorded fixtures; [`tests/README.md`](tests/README.md) explains how the tests work |
| `reverse_engineering/` | the firmware download and analysis scripts, the emulator harness |