# Effect reimplementation notes

This is the current technical reference for the native effect library. It records
verified findings only; use the disassembly and the emulator for details.

## Current status

The three-band stereo EQ, the noise gate, the compressor, the Filter block's
seven effects, the Pitch block's five effects, the Modulation block's thirteen
effects, the Delay block's four effects and the Reverb block are implemented in
C17, including native parameter setup. The emulator is the reference, not a
replacement for the native library. The test system in `tests/` (see `AGENTS.md`)
covers every one of them, the software float, the firmware lookup tables and the
float <-> Q23 conversion: `make check` replays each recorded fixture
through the library and needs no emulator.

| File | Purpose |
|---|---|
| `src/gm2.h` | The header a host includes: the API contract and every effect's headers |
| `src/gm2_base.c`, `src/gm2_base.h` | Per-instance context carrying the sample rate every effect's setup reads |
| `src/gm2_convert.c`, `src/gm2_convert.h` | Float <-> Q23 sample conversion, one implementation per compile-time SIMD path |
| `src/gm2_eq.c`, `src/gm2_eq.h` | Fixed-point stereo EQ kernel |
| `src/gm2_eq_setup.c`, `src/gm2_eq_setup.h` | Raw Freq/Gain/Width to EQ coefficient conversion |
| `src/gm2_gate.c`, `src/gm2_gate.h` | Fixed-point noise gate kernel: one-pole, wet/dry, gain computer |
| `src/gm2_gate_setup.c`, `src/gm2_gate_setup.h` | Raw Mode/Threshold/Damping/Release to gate setup words |
| `src/gm2_comp.c`, `src/gm2_comp.h` | Fixed-point compressor kernel: detector, gain, gain computer |
| `src/gm2_comp_setup.c`, `src/gm2_comp_setup.h` | Raw Threshold/Ratio/Attack/Release/Gain to compressor setup words |
| `src/gm2_lfo_gain_core.c`, `src/gm2_lfo_gain_core.h` | Fixed-point Tremolo/Panner routine both effects share: LFO, waveform, gain, Hi-Cut |
| `src/gm2_lfo_gain_design.c`, `src/gm2_lfo_gain_design.h` | The shared Tremolo/Panner depth, rate, pulse and smoother words |
| `src/gm2_tremolo.c`, `src/gm2_tremolo.h` | Tremolo on the shared routine, with LFO alignment to a song position |
| `src/gm2_tremolo_setup.c`, `src/gm2_tremolo_setup.h` | Raw Speed/Depth/Tempo/Type/Pwidth/HiCut and the host's beat to Tremolo setup words |
| `src/gm2_panner.c`, `src/gm2_panner.h` | Panner on the shared routine, with LFO alignment to a song position |
| `src/gm2_panner_setup.c`, `src/gm2_panner_setup.h` | Raw Speed/Width/Tempo and the host's beat to Panner setup words |
| `src/gm2_wah_core.c`, `src/gm2_wah_core.h` | Fixed-point wah routine the three wahs share: input gain, sweep cubic, state-variable filter, soft clip and the frequency job |
| `src/gm2_wah_design.c`, `src/gm2_wah_design.h` | The wahs' shared Range shift; each effect owns its envelope or LFO and its tick |
| `src/gm2_wahwah.c`, `src/gm2_wahwah.h`, `src/gm2_wahwah_setup.c`, `src/gm2_wahwah_setup.h` | Wah Wah: Sweep and Range |
| `src/gm2_touchwah.c`, `src/gm2_touchwah.h`, `src/gm2_touchwah_setup.c`, `src/gm2_touchwah_setup.h` | Touch Wah: Sense, Response and Range, and its tick |
| `src/gm2_autowah.c`, `src/gm2_autowah.h`, `src/gm2_autowah_setup.c`, `src/gm2_autowah_setup.h` | Auto Wah: Speed, Tempo, Shape and Range, its tick and LFO alignment |
| `src/gm2_res_core.c`, `src/gm2_res_core.h` | Fixed-point resonance routine both resonance filters share: two biquads a channel, soft clip, the coefficient job |
| `src/gm2_res_design.c`, `src/gm2_res_design.h` | The resonance filters' coefficient design, and the Touch Res Filter's tick |
| `src/gm2_resfilter.c`, `src/gm2_resfilter.h`, `src/gm2_resfilter_setup.c`, `src/gm2_resfilter_setup.h` | Resonance Filter: Style, Hi-Cut and Resonance |
| `src/gm2_touchres.c`, `src/gm2_touchres.h`, `src/gm2_touchres_setup.c`, `src/gm2_touchres_setup.h` | Touch Res Filter: Style, Sense, Response and Fmax, its level detector and tick |
| `src/gm2_pitch_core.c`, `src/gm2_pitch_core.h`, `src/gm2_pitch_design.c`, `src/gm2_pitch_design.h` | Fixed-point two-voice pitch-synchronous delay and AMDF pitch detector (`P:0x000D15..0x000E4D`, `P:0x0003F2..0x00089C`, `P:0x0001E4..0x0001FA`) and parameter setup (`P:0x090937`) the five Pitch effects share |
| `src/gm2_detune.c`, `src/gm2_detune.h`, `src/gm2_detune_setup.c`, `src/gm2_detune_setup.h` | Detune: Voice L/R and Delay L/R |
| `src/gm2_whammy.c`, `src/gm2_whammy.h`, `src/gm2_whammy_setup.c`, `src/gm2_whammy_setup.h` | Whammy: Pitch, Direction and Range |
| `src/gm2_octaver.c`, `src/gm2_octaver.h`, `src/gm2_octaver_setup.c`, `src/gm2_octaver_setup.h` | Octaver: Direction and Range |
| `src/gm2_pitch.c`, `src/gm2_pitch.h`, `src/gm2_pitch_setup.c`, `src/gm2_pitch_setup.h` | Pitch Shifter: Voice 1/2, Pan 1/2, Delay 1/2, Feedback 1/2 and Level 1/2 |
| `src/gm2_ipitch.c`, `src/gm2_ipitch.h`, `src/gm2_ipitch_setup.c`, `src/gm2_ipitch_setup.h` | Intelligent Pitch Shifter: Voice 1/2, Pan 1/2, Delay 1/2, Key, Scale and Level 1/2 |
| `src/gm2_phaser_core.c`, `src/gm2_phaser_core.h`, `src/gm2_phaser_design.c`, `src/gm2_phaser_design.h` | Fixed-point phaser routine (`P:0x000FA8..0x001060`) and parameter setup (`P:0x092B92`) the Vintage and Smooth Phasers share |
| `src/gm2_vintphaser.c`, `src/gm2_vintphaser.h`, `src/gm2_vintphaser_setup.c`, `src/gm2_vintphaser_setup.h` | Vintage Phaser (4 all-pass stages): Speed, Depth, Tempo, Feedback, Range, Phase Reverse, and LFO alignment |
| `src/gm2_smoothphaser.c`, `src/gm2_smoothphaser.h`, `src/gm2_smoothphaser_setup.c`, `src/gm2_smoothphaser_setup.h` | Smooth Phaser (12 all-pass stages): Speed, Depth, Tempo, Feedback, Range, Phase Reverse, and LFO alignment |
| `src/gm2_univibe_core.c`, `src/gm2_univibe_core.h`, `src/gm2_univibe_design.c`, `src/gm2_univibe_design.h` | Fixed-point univibe routine (`P:0x00107F..0x001116`) and parameter setup (`P:0x09311B`) the Vintage and Modern Univibes share |
| `src/gm2_vintunivibe.c`, `src/gm2_vintunivibe.h`, `src/gm2_vintunivibe_setup.c`, `src/gm2_vintunivibe_setup.h` | Vintage Univibe: Speed 1/2, Depth, Tempo 1/2, Attack, Release, Select, Phase Reverse, and LFO alignment |
| `src/gm2_modunivibe.c`, `src/gm2_modunivibe.h`, `src/gm2_modunivibe_setup.c`, `src/gm2_modunivibe_setup.h` | Modern Univibe: Speed 1/2, Depth, Tempo 1/2, Attack, Release, Select, Phase Reverse, and LFO alignment |
| `src/gm2_moddelay_core.c`, `src/gm2_moddelay_core.h`, `src/gm2_moddelay_design.c`, `src/gm2_moddelay_design.h` | Fixed-point six-voice modulated delay routine (`P:0x001117..0x0011F8`, `P:0x00033A`, `P:0x00033C`) and parameter setup (`P:0x0938BE`) the nine Chorus, Flanger and Vibrato effects share |
| `src/gm2_cchorus.c`, `src/gm2_cchorus.h`, `src/gm2_cchorus_setup.c`, `src/gm2_cchorus_setup.h` | Classic Chorus: Speed, Depth, Tempo, Hi-Cut, and LFO alignment |
| `src/gm2_achorus.c`, `src/gm2_achorus.h`, `src/gm2_achorus_setup.c`, `src/gm2_achorus_setup.h` | Advanced Chorus: Speed, Depth, Tempo, Hi-Cut, Delay, Golden Ratio, Phase Reverse, and LFO alignment |
| `src/gm2_cflanger.c`, `src/gm2_cflanger.h`, `src/gm2_cflanger_setup.c`, `src/gm2_cflanger_setup.h` | Classic Flanger: Speed, Depth, Tempo, Hi-Cut, Feedback, Feedback Hi-Cut, and LFO alignment |
| `src/gm2_aflanger.c`, `src/gm2_aflanger.h`, `src/gm2_aflanger_setup.c`, `src/gm2_aflanger_setup.h` | Advanced Flanger: Speed, Depth, Tempo, Hi-Cut, Feedback, Feedback Hi-Cut, Delay, Golden Ratio, Phase Reverse, and LFO alignment |
| `src/gm2_trichorus.c`, `src/gm2_trichorus.h`, `src/gm2_trichorus_setup.c`, `src/gm2_trichorus_setup.h` | Tri-Chorus Normal: Speed, Depth, Tempo, Width, Phase Reverse, and LFO alignment |
| `src/gm2_trichorus_asym.c`, `src/gm2_trichorus_asym.h`, `src/gm2_trichorus_asym_setup.c`, `src/gm2_trichorus_asym_setup.h` | Tri-Chorus Asymmetric: Speed, Depth, Tempo, Width, Phase Reverse, and LFO alignment |
| `src/gm2_tzflanger_neg.c`, `src/gm2_tzflanger_neg.h`, `src/gm2_tzflanger_neg_setup.c`, `src/gm2_tzflanger_neg_setup.h` | Through-Zero Flanger Negative: Speed, Depth, Tempo, Feedback, Feedback Hi-Cut, and LFO alignment |
| `src/gm2_tzflanger_pos.c`, `src/gm2_tzflanger_pos.h`, `src/gm2_tzflanger_pos_setup.c`, `src/gm2_tzflanger_pos_setup.h` | Through-Zero Flanger Positive: Speed, Depth, Tempo, Feedback, Feedback Hi-Cut, and LFO alignment |
| `src/gm2_vibrato.c`, `src/gm2_vibrato.h`, `src/gm2_vibrato_setup.c`, `src/gm2_vibrato_setup.h` | Vibrato: Speed, Depth, Tempo, Hi-Cut, and LFO alignment |
| `src/gm2_delay_core.c`, `src/gm2_delay_core.h` | Fixed-point delay routine the four delays share: input/feedback matrix, 172,800-word circular buffer, two-tap equal-power crossfade, per-channel LP/HP filters, output matrix, smoothing and crossfade jobs |
| `src/gm2_delay_design.c`, `src/gm2_delay_design.h` | The delays' shared matrix, LP/HP filter and tempo/tap-offset setup; the Dynamic Delay owns ducking and the Reverse Delay owns its grain splice |
| `src/gm2_pingpong.c`, `src/gm2_pingpong.h`, `src/gm2_pingpong_setup.c`, `src/gm2_pingpong_setup.h` | Ping Pong Delay: Delay, Tempo, Width, Feedback, Feedback Hi-Cut and Lo-Cut |
| `src/gm2_dyndelay.c`, `src/gm2_dyndelay.h`, `src/gm2_dyndelay_setup.c`, `src/gm2_dyndelay_setup.h` | Dynamic Delay: Delay, Tempo, Feedback, Feedback Hi-Cut and Lo-Cut, Offset, Sense, Damp and Release, its level detector and ducking tick |
| `src/gm2_dualdelay.c`, `src/gm2_dualdelay.h`, `src/gm2_dualdelay_setup.c`, `src/gm2_dualdelay_setup.h` | Dual Delay: Delay 1/2, Tempo 1/2, Feedback 1/2, Feedback Hi-Cut and Lo-Cut, Pan 1/2 |
| `src/gm2_revdelay.c`, `src/gm2_revdelay.h`, `src/gm2_revdelay_setup.c`, `src/gm2_revdelay_setup.h` | Reverse Delay: Delay, Tempo, Feedback, Feedback Hi-Cut and Lo-Cut, and its grain splice |
| `src/gm2_reverb.c`, `src/gm2_reverb.h`, `src/gm2_reverb_setup.c`, `src/gm2_reverb_setup.h` | Reverb (Spring, Hall, Room, Plate): 32,768-word circular buffer, input LP and two allpass diffusers, seven tank delay lines with two-band damping filters, eight early reflections and two biquads per channel, and Decay/PreDly/Shape/Size/HiCol/HiFact/LoCol/LoFact/RoomLv/RevLev/Diff setup |
| `src/gm2_gate_detector.c`, `src/gm2_gate_detector.h` | The Noise Gate's level detector, which the gate, the Touch Res Filter and the Dynamic Delay share |
| `src/gm2_tick.c`, `src/gm2_tick.h` | The control tick's period, the peak-hold read, and the touch follower, Response and Sense words |
| `src/gm2_lfo.c`, `src/gm2_lfo.h` | LFO setup: Speed and tempo rates, phase steps and advances, and song-position phase |
| `src/gm2_rate.c`, `src/gm2_rate.h` | The six sample-rate values the rate install derives |
| `src/gm2_tables.c`, `src/gm2_tables.h` | Firmware lookup tables word for word, and the log2/exp2/decay and waveform readers |
| `src/gm2_dsp_math.h` | DSP56300 24/48/56-bit arithmetic primitives, header-only and inline |
| `src/gm2_swf.c`, `src/gm2_swf.h` | Native software-float arithmetic and transcendental helpers, plus the dB and frequency unit maps |
| `src/gm2_process.h` | Fixed block level words and the four process entry points every effect shares |
| `src/gm2_job.c`, `src/gm2_job.h` | One-pole coefficient smoothing, parameter ramps and delay-tap crossfades |
| `src/gm2_biquad.h` | The firmware's double-precision biquad step and state |
| `reverse_engineering/harness/` | Emulator plumbing the test recorder drives: loads the firmware, applies its initializer, installs a rate, calls routines and reads memory |
| `tests/` | The test recorder, the offline checker and the fixtures recorded from the firmware |

## Reusable firmware execution facts

### Initializer

`P:0x082AA2` walks four-word descriptors at `P:0x080000`:
`flags, destination, source, count`. Missing P source gaps read as zero in the
harness.

| Flags | Action |
|---|---|
| `0x201` | Copy P source to X destination |
| `0x301` | Copy P source to Y destination |
| `0x202` / `0x302` | Clear X / Y destination |
| `0x401` | Copy two P words per long destination |
| `0x402` | Clear long destination |

Long accesses use X as the high half and Y as the low half. The harness's
`gm2_apply_init_table()` reproduces the routine's result over all 45 descriptors.

Some descriptor records are addressed through X instructions even though their
contents are present in the P load stream. The harness mirrors only the required
records; this is not evidence that all P/X/Y spaces alias.

### Calling code

Ordinary functions can be called with a synthetic `jsr target` plus `stop`.
Continuation-based audio tasks must instead be entered directly with a task
record and a final continuation pointing at the stop stub. Repeated `jsr` entry
into a continuation grows the hardware stack and is incorrect.

The xemu harness maps separate P/X/Y buffers. The EQ path uses `OMR=0x004B01`;
this is sample-identical to the firmware's `0x004B81` and avoids the emulator's
unimplemented memory-switch warning. No firmware instruction reads OMR back.

### Sample rate

Install a rate without hardware by converting the integer with `P:0x000BFD`,
setting `R0=X:0x099429`, and calling `P:0x082DC0`. Coefficients are
rate-dependent; the audio kernel is not.

`P:0x082DC0` writes six (exponent, mantissa) pairs to `X:0x099429+0..11`,
exponent word first, from the rate float it receives in A:

| Words | Value |
|---|---|
| `+0/+1` | `rate` |
| `+2/+3` | `rate * {0x418937, 0x76}`, about 0.001 of it |
| `+4/+5` | `{0x6487EE, 0x82} / rate` |
| `+6/+7` | the negation of `+4/+5` |
| `+8/+9` | `{0x400000, 0x80} / rate`, the `1/rate` the EQ chain reads through `X:0x09938D` |
| `+10/+11` | `{0x7D0000, 0x89} / rate`, `1000/rate` |

All six pairs match the emulator at 44100 and 48000 Hz. `+8/+9` is
`{0x57619E, 0x70}` at 48000 and `{0x5F1BE2, 0x70}` at 44100. `src/gm2_rate.c`
ports the six values, and every setup takes them from there.

### Parameter edit

`P:0x083268` is the device's edit of one knob. It clamps the value into the
descriptor entry's window through `P:0x08E1A2`, which writes the raw word and
reports an unchanged word, in which case no callback runs. Otherwise it calls the
entry's callback (descriptor +14) with the value in A, and, when the entry's
format word (+12) has bits 23 and 13 set, writes the value to `X:0x09860C` and
calls the same callback again with `A = 0x50`, the second half of a two-part
edit. Word +13 of the entry is its factory default where it has one: `P:0x094509`
is the only code that reads it, copying the defaults of the Delay block's four
subtype banks.

### Parameter publication

A job record is four words, `{next, flag, params, handler}`. The device keeps two
lists of them: the background queue at `X:0x0993C3` with its count at
`X:0x0993C0`, and the active list at `X:0x0200` with its count at `X:0x0993C1`.
`P:0x096163` links a record in directly behind a list's head. `P:0x096194`
publishes a record's staged words through `P:0x095FC8` and queues it into the
background queue, which is how an activation leaves live words equal to staged
ones; the table at `X:0x09938E` picks the copier per handler, `0x1FD` and `0x26F`
to `P:0x095F9E`, `0x1B9` and `0x1CB` to `P:0x095FAC` (both copy X to Y) and
`0x215` to `P:0x095FBA` (X to X). `P:0x096175` queues into the active list with
no publication at all. `P:0x096183` copies the live words back into the staged
ones through `P:0x095FF9`, the same table one word on with the copiers `0x1FD`
and `0x26F` to `P:0x095FCF`, `0x1B9` and `0x1CB` to `P:0x095FDD`, and `0x215` to
`P:0x095FEB`.

The handlers move every live word of the payload toward its staged word with
snap and step from the record:

| Payload | Handler |
|---|---|
| `{src, dst, count, snap, step}` | `P:0x0001B9` moves by the fixed step; `P:0x0001CB` is the same; `P:0x0001FD` by the step times the gap, a one-pole smoothing, usual step `0x008371`; `P:0x000212` is a branch into `P:0x0001FD`; `P:0x000215` is the one-pole with the destination in X |
| `{src, dst, total, snap, step, unused, group, scratch}` | `P:0x00026F` runs the one-pole over `group` words at a time, computing each group into `scratch` before the copy to Y, so a routine never reads half an update |
| the channel's tap crossfade record | `P:0x000293` (jumped to from vector `P:0x000019`) crossfades a delay channel's two read taps along an equal-power parabola when the staged tap offset changes |

Both kinds of step snap onto the staged word once within the threshold, so a live
word settles exactly on its staged value. `gm2_job_smooth()` is one run on one
word.

A knob turn publishes nothing. The updater stages its word, or lets
`P:0x09605F` write it, and arms the record's snap and step; the jobs then walk the
live word to the staged one. `P:0x09605F` sets `snap = step = clamp(|live -
staged| * X:0x0993B4, 1, cap)` with the 1.23 word `X:0x0993B4 = 0x00722D`, so every ramp
covers its gap at the same rate whatever the jump, and it jumps the live word to
the staged one only when that product exceeds the cap. Every one of its fourteen
call sites hands it `0x200000`, more than the whole-scale gap scales to, so the
jump never happens. A parameter edit leaves the scheduler alone: the four-frame
countdown `X:0x0001` and the job runs keep their cadence.

The jobs run outside the audio. The list manager `P:0x0960C6`, run twice per pass
from `P:0x0961A5`, walks each list and runs every record's settled check through
`P:0x0960B4`, which needs the signature `0xD0BED0` two words before the handler
and calls the address one word before it with the record's params in R0: `P:0x0008EB`
behind `0x1FD`, `0x26F` and `0x212`, `P:0x000906` behind `0x215`, `P:0x000915`
behind `0x1B9` and `0x1CB`, `P:0x00089D` behind `0x293`, `P:0x000301` behind
`0x019` (the delay taps' handler, a jump to `0x293`) and `P:0x00033C` behind `0x33A`
(the Modulation taps'). The
runner `P:0x0001CD` then unlinks records from a list and calls their handlers, up
to the popped record's `flag` word. The runner is one entry of the service list
the audio interrupt visits one entry of after every chunk (`P:0x00012C`), so its
rate depends on the other services.

### Control tick

The background loop also sends the blocks message 13. `P:0x08F3D4` raises its
command `0xD1` when the counter it copies into `X:0x0091` (from `X:0x00A9`, which
no code writes by address) has moved more than `X:0x09871A` = 15 since the last
tick; `P:0x083E39` then refreshes the input-level word `X:0x0B03` from the Noise
Gate's peak hold (`P:0x090494`) and sends message 13 to the Modulation, Delay,
Pitch and Filter blocks. The firmware takes that period as 13 ms: the Response
updaters compute `15 / {0x48BAC7, 0x80} + 0.5` (15 / 1.1364), truncated. Neither
the tick nor the jobs are locked to the audio, so the ports run both on a fixed
schedule (`AGENTS.md`, "Host integration"): after each four-frame chunk every job
of the effect runs once, and every `gm2_tick_chunks()` chunks the effect's tick
runs, before the next frame.

A tick reads a peak hold, a log2 level that a routine max-holds once a chunk,
through `level = max(0, to_q23(1 + {0x7B4D49, 0x82} * peak))` and resets it to
`0x800000` (`P:0x09248A`, `P:0x090494`; `gm2_tick_level()`). The touch effects'
follower then takes the whole gap to that level when the gap is at most its
threshold word and the coefficient word times the gap above it
(`gm2_tick_follow()`).

### Block level words

Every block's kernel multiplies by level words that no effect parameter sets:
input gain, wet and dry. The activation fills a level struct S in the UI object
and calls the level code `P:0x08F763` with `R0 = S`. The level code stages the
three words at `X:(S+19..S+21)` for the destination `Y:(X:(S+8))` and following,
through a `P:0x0001FD` job. With the kill flag `X:(S+3)` clear, as activation
leaves it:

- input is `X:(S+5)`;
- wet is the MPYR of the out level `X:(S+6)` and the 1.23 word of
  `sin(X:(S+7) * pi/2)`, the mix, and exactly `0x7FFFFF` only when `X:(S)` is 4
  or `X:(S+1)` is 3;
- dry is the same with `0x7FFFFF - X:(S+7)`, and 0 while the device-wide Kill
  Dry flag `X:0x099A85` is set.

An object with an Out Level parameter sets `X:(S+6)` to `10^(dB/20)` through
`P:0x082DF1`, `0x7FFFFF` at 0 dB. Every block below runs with a wet word one LSB
below its input gain:

| Block | UI object | Activation | S | Destination | Input / wet / dry |
|---|---|---|---|---|---|
| Gate & EQ | `X:0x0987A4` | `P:0x09038B` | UI+6 | `Y:0x394` | `0x7FFFFF` / `0x7FFFFE` / 0 |
| Compressor | `X:0x098F45` | `P:0x094C7C` | UI+6 | `Y:0x578` | `0x7FFFFF` / `0x7FFFFE` / 0 |
| Tremolo/Panner | `X:0x0988CE` | `P:0x09172F` | UI+36 | `Y:0x3C1` | `0x7FFFFF` / `0x7FFFFE` / 0 at Out Level 0 dB |
| Wahs | `X:0x0989B4` | `P:0x09233B` | UI+60 | `Y:0x419` | `0x7FFFFF` / `0x7FFFFE` / 0 at Mix 100 %, Out Level 0 dB |
| Resonance filters | `X:0x098948` | `P:0x091BB1` | UI+54 | `Y:0x3EE` | `0x7FFFFF` / `0x7FFFFE` / 0 at Mix 100 %, Out Level 0 dB |
| Pitch | `X:0x0987EA` | `P:0x090937` | UI+80 | `Y:0x3A0` | `0x7FFFFF` / `0x7FFFFE` / 0 at Mix 100 %, Out Level 0 dB |
| Phasers | `X:0x098A8D` | `P:0x092B92`, `P:0x092F63` | UI+36 | `Y:0x459` | `0x7FFFFF` / `0x7FFFFE` / 0 at Mix 100 %, Out Level 0 dB |
| Univibes | `X:0x098B7A` | `P:0x09311B`, `P:0x0935E6` | UI+36 | `Y:0x497` | `0x7FFFFF` / `0x7FFFFE` / 0 at Mix 100 %, Out Level 0 dB |
| Choruses / Flangers / Vibrato | `X:0x098C7F` | `P:0x0938BE`, `P:0x093FA1` | UI+135 | `Y:0x4F6` | `0x7FFFFF` / `0x7FFFFE` / 0 at Mix 100 %, Out Level 0 dB |
| Delays | `X:0x098E7D` | `P:0x09423F` | UI+63 | `Y:0x55A` | `0x7FFFFF` / `0x7FFFFE` / 0 at Mix 100 %, Out Level 0 dB |
| Reverb | `X:0x098FF4` | `P:0x095099`, `P:0x095BE1` | UI+63 | `Y:0x5DA` | `0x7FFFFF` / `0x7FFFFE` / 0 at Mix 100 %, Out Level 0 dB |

The Gate & EQ and compressor activations set `S+4..S+7` to `0x7FFFFF`. The
Tremolo/Panner object fixes its mix at `0x7FFFFF`; the two filter objects set it
from their Mix parameter (index 13) times 0.01. Their first activation leaves the
out level at 0, and the next one sets it from Out Level. The init table's
power-on words differ: the Gate & EQ block's wet word is `0x7FFFFF` until
activation.

The level code builds its record inside the struct too: the payload at
`X:(S+13)` is `{src = X:(S+19), dst = X:(S+8), count, snap 1, step 0x008371}`,
`count` 3 once both addresses are set and 0 before, and the `P:0x0001FD` record
sits at `X:(S+22)`, its params word at `S+24` pointing at that payload, queued
through `P:0x096175`. So every block's list carries a level record next to its
own, and
`gm2_block_levels()` returns the staged words only for the two blocks that keep
their struct at UI+6; the other recorders read the staged words after the
activation and let the block's own level job settle the live ones (the Reverb's
payload is `X:0x099040`, delivering `Y:0x5DA`). The checker requires both to
match the words each kernel fixes.

### Instruction semantics

The kernels depend on these DSP56300 details, and each is reproduced natively:

- A conditionally executed instruction (`ifcc`) leaves the condition codes
  alone, so a later conditional still tests the earlier result.
- A `Tcc` with an address-register pair, such as `tle x0,b r4,r5`, moves that
  pair only when the condition holds too.
- A parallel move reads its source before the instruction changes it.
- Moving an accumulator to a 24-bit register or memory word limits it while the
  extension bits are in use; `move a,l:` limits to 48 bits.
- `MAXM` takes the operand with the larger magnitude and keeps the destination on
  a tie.
- `DMACSU` and `DMACSS` shift the accumulator down 24 bits before adding the
  doubled product; `DMACSU` reads its second operand as unsigned.
- `MPY S,#n,D` multiplies by 2^-n.
- The extension condition `ifes` tests holds when bits 55..47 differ: from 2^47
  up and below -2^47. The low word counts, so `00:7FFFFF:FFFC00` leaves it clear.
- With `M3 = 0xFF`, `(r3)+` wraps inside the 256-word block, so the last sine
  table entry interpolates towards the first.
- `move #xx,D` with an 8-bit immediate fills bits 23..16 of a data ALU register
  (`move #$80,y0` gives `0x800000`) and the low bits of an address register.
- `ADDR S,D` shifts the destination accumulator `D` right by one bit (`ASR`) and
  adds `S` (`S + (D >> 1)`), mirroring `ADDL S,D` (`S + 2*D`).
- The 48-bit limit is the 24-bit one twenty-four places up, so a 24-bit store of
  an accumulator already limited to 48 bits cannot limit again, and the word it
  reads is the floor shift of the 48-bit value.
- Two 24-bit words added at the A1 position give a sum whose 32-bit wrap is that
  A1 field, so an `ifes` test, a wrap word subtracted from it and a following
  24-bit limit are 32-bit steps on that word, as the LFO phase advance is.
- A table read interpolating between two entries cannot leave the range they
  span, so a wave-table read whose table lies inside the rails never limits.

## EQ firmware map

| Item | Address / value |
|---|---|
| Audio continuation | `P:0x000CC0` |
| Filter state | `L:0x000050..0x00005F` |
| Three five-word coefficient sets | `Y:0x000385..0x000393` |
| Input gain | `Y:0x000394` |
| Wet / dry gains | `Y:0x000395` / `Y:0x000396` |
| UI object | `X:0x0987A4` |
| Gate/EQ algorithm ID | 6 |
| Live raw values | `X:0x099B36 + 16*6 + index` |
| Per-band updater | `P:0x0906B6` |
| Raw-value mapping | `P:0x09065A` |
| Active coefficient routine | `P:0x095C3C` |
| Staged coefficients, one set a band | `X:0x098771 + 5*b` |
| Band job records | `X:0x098798`, `X:0x09879C`, `X:0x0987A0` |
| Band job payloads | `X:0x098780`, `X:0x098788`, `X:0x098790` |

For band `i` in 0..2, raw parameter indices are Freq `5+3*i`, Gain `6+3*i`, and
Width `7+3*i`. The accepted values and firmware mappings are:

| Parameter | Raw range | Meaning |
|---|---|---|
| Freq | 25..240 | `10^((raw+24)/80) * 10` Hz |
| Freq Off | 241 | Bypass band |
| Gain | -12..12 dB | `10^(raw/20)` linear; 0 dB is flat |
| Width | 3..16 | `2^((raw-10)/3)` octaves |

Flat gain and Freq Off both produce `{0, 0, 0x200000, 0xE00000, 0}`.

A band callback `P:0x0906B6` restages the five words at `X:0x098771 + 5*b`; the
record `{src, dst = Y:0x385 + 5*b, count 5, snap 1, step 0x008371}` in its payload
is a `P:0x00026F` job, so the live set glides there one step per chunk run. The
native state holds the live words and the config the staged ones.

The audio path applies the block's input gain, runs three cascaded second-order
sections on each channel, then applies the block's wet/dry gains. The native
kernel fixes those words at the values the block's activation leaves (see "Block
level words"): input gain `0x7FFFFF`, wet `0x7FFFFE`, dry 0.

## Noise gate firmware map

The gate shares the Gate/EQ algorithm (chain index 6) with the three-band EQ: its
own continuation, state and four updaters, on the block's shared input and
wet/dry words.

| Item | Address / value |
|---|---|
| Audio continuation | `P:0x000C47` |
| Per-frame path | `P:0x000C4C..0x000C66` |
| Gain computer | `P:0x000C69..0x000CBD`, first frame of every four |
| One-pole state | `X:0x0002A0` / `X:0x0002A1` |
| Envelope | `X:0x0002A2` |
| log2 gain word | `X:0x0002A3` |
| Slewed target | `X:0x0002A4` |
| One-pole coefficients | `Y:0x000371` / `Y:0x000372` |
| Threshold word | `Y:0x000376`, updater `P:0x090555` |
| Peak hold, a meter word | `Y:0x000377` |
| Mode word | `Y:0x000379`, updater `P:0x0904E8` |
| Damping floor | `Y:0x00037A`, updater `P:0x090513` |
| Release words | `Y:0x00037B..0x00037E`, updater `P:0x090582` |
| Block constants | `P:0x090441` |
| log2 / exp2 tables | `X:0x0009E8..0x000A08` / `X:0x000A09..0x000A29`, 33 words each |
| Parameter staging | `X:0x098745..0x09874B`, published by `P:0x09605F` |
| Updater caller | `P:0x09038B`, reads hidden index 15 into `X:0x0987AD` |
| UI object | `X:0x0987A4` |
| Gate staging words, EQ band object, gate state base | `X:0x0987C0..0x0987C2` hold `0x098745`, `0x098771`, `0x0002A0` |

`P:0x090441`, called with `R0 = X:0x0987A4` when the block is enabled, installs
the fixed constants below, which the port carries in `src/gm2_gate.c`,
`src/gm2_gate_detector.h` and `src/gm2_tables.h`:

| Word | Value | Role |
|---|---|---|
| `Y:0x0373` | `0x7FDD98` | envelope decay |
| `Y:0x0374` | `0x0009C9` | log2 table walk base |
| `Y:0x0375` | `0x020000` | log2 offset |
| `Y:0x0378` | `0x010000` | knee half |
| `Y:0x037F` | `0x7FFFFF` | unity |
| `Y:0x0380` | `0x000A09` | exp2 table base |
| `Y:0x0381` | `0x000001` | exp2 rounding step |
| `Y:0x0382` / `Y:0x0383` | `0xB2EFBB` / `0x566DF8` | coefficient shaping |
| `Y:0x0384` | `0x7FFFFF` | unity |

The parameter words `Y:0x0376`, `Y:0x0379`, `Y:0x037A` and `Y:0x037B..0x037E` lie
in that range but belong to the four updaters, not to this routine.

The four gate parameters are raw indices 0..3 of the same descriptor `P:0x0846B9`
the EQ uses, ahead of `EQ Active` at 4 and the bands at 5..13. Their accepted
values and the words the updaters publish are:

| Parameter | Raw range | Word |
|---|---|---|
| NG Mode | 0..1 | `0x180000` for 0 and `0x500000` for 1; the larger word drives the knee further down |
| NG Thres | -61..0 dB, -61 shown as Off | `raw * {0x550A96,0x77}` = `log2(gain)/64` |
| NG Damp | 0..90 dB | `raw * {0xAAF56A,0x77}`, the same log2 term negated |
| NG Release | 3..200 dB/s | step, blend and two one-pole coefficients, derived from the sample rate |

The device shows threshold -61 as Off, but the threshold updater converts it like
any other value. The manual's Mode settings Off/Hard/Soft cannot be matched to
the descriptor's two raw values.

Index 15 is outside the descriptor and is not a UI parameter; it acts as the
block's kill flag. While it is set, `P:0x090513` clears the damping word unless
the threshold is -61, and the EQ updater flattens its bands. `P:0x09038B` reads
it through `P:0x0832CB` before the four updaters run. Every covered
configuration keeps it 0, so the native damping word is the plain conversion and
the kill path is not ported.

### Kernel and control rate

Per stereo frame the kernel applies `Y:0x394` with two MPYRs, runs one pole per
channel with `Y:0x371`/`Y:0x372`, takes the wet path as a plain MPY against
`Y:0x395`, and follows `max(|L|, |R|)` with an instant attack and a one-pole
decay into `X:0x2A2`. The dry word `Y:0x396` is 0 on the device, which is why the
native kernel carries no dry term.

The gain computer runs on the first frame of each four-frame chunk only:
`P:0x000C67` leaves for `P:0x000CBF` unless bit 2 of `X:0x0001` is set, and the
scheduler counts that word 4, 3, 2, 1 across a chunk. The computer takes log2 of the envelope (INC, CLB,
NORMF, the log table and its interpolation pair), subtracts `Y:0x376`, bends the
result by the mode word and the knee half `Y:0x378`, clamps it to the `Y:0x37A`
floor, slews `X:0x2A4` down by one `Y:0x37B` step while the gain has to fall, and
moves `X:0x2A3` towards it between the two release coefficients `Y:0x37D` and
`Y:0x37E`. exp2 of the gain word out of the second table, shaped by
`Y:0x382`/`Y:0x383`, becomes `Y:0x371`/`Y:0x372` from the next frame on.

Two instruction semantics decide words in the computer and both are reproduced: a
conditionally executed instruction leaves the condition codes unchanged, which
picks `P:0x000C92` or `P:0x000C93`, and a parallel move reads the
pre-instruction accumulator, which decides the `Y:0x371` word at `P:0x000CBA`.
A valid envelope always normalizes to log-table steps `0x20..0x3F`; the native
kernel masks the step only to keep an arbitrary state word inside the table.

### Parameter changes

The updaters stage `X:0x098745..0x09874B` instead of writing the live words, and
each turn of a knob arms the ramp of the words it owns through `P:0x09605F` (see
"Parameter publication"), so the kernel follows the change over several frames:
Mode arms `Y:0x379`, Threshold arms `Y:0x376` and then `Y:0x37A` because
`P:0x090555` calls `P:0x090513`, and Damping arms `Y:0x37A`. Release is the only
updater that writes its own four staged words; they share one `P:0x0001FD` job
and are smoothed, never armed.

## Compressor firmware map

The compressor is algorithm 0, a block of its own. Its continuation runs the
detector and the gain on every frame and a gain computer on the first frame of
each four-frame chunk.

| Item | Address / value |
|---|---|
| Audio continuation | `P:0x00125E..0x0012D1`, task record `Y:0x00055D` |
| Per-frame path | `P:0x00125E..0x00127F` |
| Gain computer | `P:0x001282..0x0012CF`, first frame of every four |
| Detector level | `L:0x00000D`, 48 bits |
| Gain word | `X:0x000379` |
| Smoothed log2 gain | `X:0x00037A` / `X:0x00037B`, high and low word |
| Target | `X:0x00037C` |
| Detector coefficient | `Y:0x000567`, updater `P:0x094E18` |
| Threshold / ratio words | `Y:0x00056A` / `Y:0x00056C`, updater `P:0x094D5C` |
| Release step | `Y:0x00056D`, updater `P:0x094E18` |
| Release / attack triples | `Y:0x00056E..0x000570` / `Y:0x000571..0x000573`, updater `P:0x094E18` |
| Makeup gain | `Y:0x000575`, updater `P:0x094D5C` |
| Input gain, wet, dry | `Y:0x000578..0x00057A`, see "Block level words" |
| Constants and task record | `P:0x094CFA` |
| Activation / dispatcher | `P:0x094C7C` / `P:0x094BFA` |
| UI object | `X:0x098F45`; staging `X:0x098F0B..0x098F15` |
| Ratio / time tables | `X:0x098EEB`, 16 pairs / `X:0x0998D8`, 27 pairs |

`P:0x094CFA` installs the log2 walk base `Y:0x568 = 0x0009C9`, the log2 offset
`Y:0x569 = 0x020000`, the knee half-width `Y:0x56B = 0x03526A`, the unity word
`Y:0x574 = 0x7FFFFF`, the exp2 base `Y:0x576 = 0x000A09` and the exp2 rounding
step `Y:0x577 = 0x000001`; the knee curvature `0x4D104A` is an immediate at
`P:0x00129E`. The log2 and exp2 words are the gate's, so both kernels run the
shared `gm2_log2`/`gm2_exp2`.

The parameters are raw indices 0..4 of descriptor `P:0x08636D`, at
`X:0x099B36 + index`:

| Parameter | Raw range | Word |
|---|---|---|
| Thres | -40..0 dB | `(T - 3 dB)` as a log2 word |
| Ratio | 0..15: 1.0 (Off), 1.12 .. 64, 100 (Infinite) | `1/R - 1` |
| Attack | 3..14: 1..70 ms | detector coefficient `1 - exp(-1/(t*rate))` and the attack triple |
| Release | 13..23: 50..2000 ms | release step and the release triple |
| Gain | -6..6 dB | makeup gain, offset by about -30.1 dB for the kernel's 2^5 headroom |

Below a -20 dB threshold the makeup offset grows by `(-20 - T) * (1 - 1/R)`. The
manual lists Threshold -30..0 dB and Attack/Release 0..200 ms; the descriptor's
ranges and the time table are what the firmware uses. The accessor `P:0x0832CB`
clamps raw values into those ranges.

### Kernel and control rate

Per frame the kernel applies the input gain with two MPYRs, takes the louder word
with `MAXM`, and smooths its square into the 48-bit level with `Y:0x567` through
a `DMACSU`/`DMACSS` pair. It multiplies both channels by the gain word with MPYR,
shifts left five bits and leaves the output through the wet MPY and a MACR with
the dry word 0.

The gain computer takes log2 of the level (the gate's sequence), halves it,
subtracts the threshold and bends the result through a soft knee of half-width
`Y:0x56B` into a log2 gain change scaled by the ratio word. While that change lies
above the smoothed gain `X:0x37A`, the gain recovers: the target `X:0x37C` climbs
by one release step per chunk and the release triple applies. Otherwise the
target takes the new value and the attack triple applies. Each triple is a blend
factor, a slow pole and a fast pole: the coefficient is the slow pole plus the
blend times the squared gap shifted left 15 bits, clamped between the two poles,
and the 48-bit smoothed gain moves towards the target by one minus it. The
makeup gain is added and exp2 writes the gain word the next frames use.

### Parameter changes

The updaters stage their words as the gate's do. `P:0x094D5C` stages and arms all
three of Threshold, Ratio and Gain on every turn of any of them, so a turn
re-arms the three even where their staged words stay put, and `P:0x094E18` stages
Attack and Release with the detector word through two `P:0x0001FD` jobs that are
smoothed, never armed.

## Filter block

The Filter block is algorithm 1. Its record `P:0x084C7E` lists only Subtype (raw
index 0, values 0..6) and placeholders; every subtype brings its own sub-record
and descriptor table, listed per subtype in `P:0x084B79` as dispatcher, table and
one more word:

| Subtypes | Dispatcher | Sub-record | Descriptor tables |
|---|---|---|---|
| 0, 1, 2 | `P:0x092246` | `P:0x085326` | `P:0x085236`, `P:0x085056`, `P:0x085146` |
| 3, 4 | `P:0x091ABE` | `P:0x08504E` | `P:0x084F5E`, `P:0x084E6E` |
| 5 Tremolo, 6 Panner | `P:0x09162E` | `P:0x084E66` | `P:0x084C86`, `P:0x084D76` |

A table's entry number is the raw index, and every subtype reads its raw values
from the block's array at `X:0x099B46 + index`; index 14 is Out Level in all
seven tables. A knob turn is the generic edit `P:0x083268`: it clamps the value
into the entry's window, writes the raw word and calls the entry's callback (see
"Parameter edit"), so an updater always reads a value the table accepts. The accessor finds a subtype's ranges through `X:0x0985F0`, which on
the device reaches the sub-record through the shared P/X memory;
`gm2_filter_subtype()` gives the harness that view.

The reset code sets the software stack to `X:0x5A9` (`P:0x0000AE`).
`X:0x1000..0x15FF` holds six 256-word waveform tables that `P:0x093820` computes
when the Modulation block activates, each with a phase offset that
`X:0x098D1F`, `X:0x098D25` or `X:0x098D2B` gives by modulation type. The first
table has offset 0 for every type: `0.5 + 0.5 sin(2 pi k / 256)`, up to 6 LSB off
the rounded formula, which the Panner and the Auto Wah read. Harness calls that
must keep the tables use the device's stack, `GM2_DEVICE_STACK`.

| Subtype | Raw indices the port reads (range) |
|---|---|
| 0 Wah Wah | 1 Sweep (0..100 %), 9 Range (0 Low, 1 High) |
| 1 Touch Wah | 2 Sense (0..10), 3 Response (0..2), 9 Range |
| 2 Auto Wah | 4 Speed (0..208), 6 Tempo (0..16), 7 Shape (0 Triangle, 1 Sine), 9 Range |
| 3 Resonance Filter | 1 Style (0 2nd, 1 4th order), 11 HiCut (18..57), 12 Res (0..100 %) |
| 4 Touch Res Filter | 1 Style, 2 Sense (0..10), 3 Response (0..2), 6 FrqMax (34..54) |

Indices 13 and 14 are Mix and Out Level for all five, held at 100 % and 0 dB. The
Touch Res Filter's Style and FrqMax entries have the do-nothing callback
`P:0x0834CD`: its activation and tick read them. Every other index on these
pages is a Dummy entry. The activation of every wah still writes the sweep word
from index 1 (`P:0x09250B`); the Touch Wah and Auto Wah replace it at their first
tick, so the ports start them at rest instead.

## Tremolo and Panner firmware map

The Tremolo (subtype 5) and the Panner (subtype 6) are one routine and one UI
object; only their setup words differ.

| Item | Address / value |
|---|---|
| Audio continuation | `P:0x000E5A..0x000E8B`, task record `Y:0x3A7` |
| Waveform routines | Soft `P:0x000E8C`, Hard `P:0x000EAF`, Sine `P:0x000EC1` |
| State | phase `X:0x2AB`; smoother `X:0x2AE`/`X:0x2AF`; Hi-Cut `X:0x2B0`/`X:0x2B1` |
| LFO words | `Y:0x3B3` pulse, `Y:0x3B4` step, `Y:0x3B5` waveform, `Y:0x3B6` right-channel lag, `Y:0x3B7`/`Y:0x3B8` Soft's slopes, `Y:0x3B9` depth |
| Smoother words | `Y:0x3BA`/`Y:0x3BB`: pole, 1 - pole |
| Hi-Cut words | `Y:0x3BC`/`Y:0x3BD` left, `Y:0x3BF`/`Y:0x3C0` right: pole, 1 - pole |
| Input, wet, dry | `Y:0x3C1..0x3C3`; the kernel never reads the input word |
| UI object | `X:0x0988CE`; staging `X:0x09887A..0x098883`, level words `X:0x098905..0x098907` |
| Activation / dispatcher | `P:0x09172F` / `P:0x09162E` |

| Index | Tremolo | Panner | Range |
|---|---|---|---|
| 4 | Speed | Speed | 0..208: 0.050..19.95 Hz, 80 steps a decade |
| 5 | Depth | Width | 0..100 % |
| 6 | Tempo | Tempo | 0..16: Ignore, then a whole note down to a 1/32 triplet |
| 7 | Type | | 0 Soft, 1 Hard |
| 8 | Pwidth | | 0..100 % |
| 11 | HiCut | | 0..60: 20 Hz..20 kHz, 20 steps a decade |
| 14 | OutLev | OutLev | -100..0 dB |

The first activation, with `X:(UI+1)` set, installs the level struct and leaves
the out level at 0; an activation with `X:(UI+1)` clear then sets it from Out
Level. The staged words reach their live ones through six `P:0x0001B9` records
from `X:0x098884` (pulse, step, lag, rise, fall and depth), two `P:0x0001FD`
records (the Hi-Cut pairs) and the level record the level code builds in its
own struct. `P:0x0918CE` arms the pulse, step, rise and fall records on every
Speed, Tempo or Pwidth turn and `P:0x091999` arms the depth one on every Depth or
Width turn; no updater ever arms the lag record or the two Hi-Cut pairs. The
waveform word and the smoother are written directly.

### Kernel

Per frame, with no chunk logic:

1. The phase advances by the step; when the sum sets the extension condition, one
   subtract of `0x7FFFFF` wraps it. The right channel reads the phase minus the
   lag, plus `0x7FFFFF` when that is negative.
2. The waveform routine turns each channel's phase into a level from 0 to 1:
   - Soft: below the pulse word, the phase times the rise slope; from it on, the
     phase minus `0x7FFFFF` times the fall slope. Either product is shifted left
     10 bits and followed by an extension-conditional subtract of `0x7FFFFF`.
   - Hard: `0x7FFFFF` below the pulse word, 0 from it on.
   - Sine: the table entry the phase's top eight bits pick, interpolated towards
     the next by the low 15 bits.

   A one-pole with the smoother words follows each.
3. Each channel's gain is `0x7FFFFF` minus depth times level. The input times the
   gain (MPYR) runs through that channel's one-pole Hi-Cut.
4. The output is the MPY by the wet word plus the MACR of the dry word and the
   input.

The Tremolo moves both channels together. The Panner lags the right channel half
a cycle and opens the Hi-Cut, so the two move in opposition.

### Setup and tempo

| Updater | Words |
|---|---|
| `P:0x0918AF` | smoother pole `exp(-100/rate)`, a 10 ms time constant |
| `P:0x0918CE` | step: the rate over the sample rate as a 1.23 word. Tremolo only: pulse = 10 Pwidth clamped to 1..999, times 0.001; rise = 2^-10 / pulse; fall = 2^-10 / (pulse - 1) |
| `P:0x091999` | depth = Depth x 0.01 x `{0x7FFCB9, 0x7F}` |
| `P:0x0919C4` | waveform: Soft or Hard by Type, Sine for the Panner; lag 0, `0x400000` for the Panner |
| `P:0x091876` | Tremolo: f = 10^(1 + (HiCut + 6)/20) Hz, pole = exp(-2 pi f / rate) for both channels; Panner: pole 0 |

With Tempo at Ignore the rate is Speed's 10^(1 + (Speed - 184)/80) Hz
(`P:0x08F0F8`). Otherwise `P:0x08F30E` reads the global Tap Time, parameter 0x17
at `X:0x099A6B`, a whole number of milliseconds per beat in 100..3000. It takes
T = ms x 0.001 s, at least 0.2 s, and runs one cycle per note value: rate =
divisor / (beats x T), with the note length beats/divisor from `X:0x0AD4` and
`X:0x0AE5`, limited to the Speed range. It also writes the nearest Speed step back
into the Speed parameter, but the published step comes from the exact rate: at a
500 ms beat a 1/8 note gives `0x2BB` at 48 kHz, exactly 4 Hz, where the nearest
Speed step, 3.98 Hz, would give a different word.

## Wah firmware map

Wah Wah, Touch Wah and Auto Wah (subtypes 0..2) are one routine and one UI
object; they differ in what moves the sweep word.

| Item | Address / value |
|---|---|
| Audio continuation | `P:0x000F24..0x000FA3`, task record `Y:0x3F1`; the LFO `P:0x0003BE` |
| State | envelope `X:0x2C0`; filter `L:0x00E..0x011`, two 48-bit words a channel; LFO phase `X:0x2BC`, triangle smoother `X:0x2BD`, output `X:0x2BE`; peak hold `Y:0x418` |
| Sweep | sweep word `Y:0x40E`; staged frequency `X:0x09899E`, which the routine writes every chunk; live frequency `Y:0x402` |
| Setup words | `Y:0x40A` Range shift, `Y:0x414` Shape, `Y:0x415` envelope decay, staged LFO step `X:0x09899F` (live `Y:0x413`) |
| Constants | `Y:0x403..0x406` the sweep cubic, `Y:0x40D` 0x64F41F, `Y:0x40F..0x412` the clip words, `Y:0x416` the log table (`gm2_log2_table` less 0x1F), `Y:0x417` the log offset 0x020000 |
| Input, wet, dry | `Y:0x419..0x41B` |
| Jobs | `X:0x0989A0` moves the staged `X:0x09899E` to `Y:0x402`, `X:0x0989A6` moves the staged `X:0x09899F` to `Y:0x413`; handler `P:0x0001FD`, snap 1, step `0x008371`, one record each, plus the level record |
| UI object | `X:0x0989B4`; activation `P:0x09233B`, dispatcher `P:0x092246` |

### Kernel

Every frame: the input gain; an envelope on half the sum of the channel
magnitudes, taking a rise at once and a fall through the decay pole; then per
channel a state-variable filter at the live frequency word, the soft clip
`P:0x0003AF` inlined, ASL #3, and the wet/dry output. On a chunk's first frame
the routine also takes log2 of the envelope (plus one) less the log offset into
the peak hold, turns the sweep word into the staged frequency through the cubic,
and runs the LFO: the phase advances by the step with the `ifes` wrap; the sine
interpolates the table at `X:0x1000`, which the Modulation block's generator
`P:0x093820` fills, under `M0 = 0xFF`; the triangle is built
from the phase and smoothed by the fixed pole `0x7FB5C8`.

### Setup and tick

| Updater | Words |
|---|---|
| `P:0x092678` | the constants, and the decay `to_q23(exp(-50 / rate))` |
| `P:0x092477` | Shape |
| `P:0x0924C1` | the LFO rate, Speed's or Tempo's as for the Tremolo, and the staged step `to_q23(rate * 4 * period)`: the LFO advances once a chunk |
| `P:0x09250B` | the sweep word, Sweep / 100 |
| `P:0x092527` | the Range shift: 1 for High, 2 for Low |
| `P:0x0925F3` | Response: the follower's coefficient `K = 1 - exp(-1 / (c * 13000))`, c = 3/10000, 2/10000 or 1/10000, and threshold `2^-23 (1/K + 1)` |
| `P:0x0925A9` | Sense: with `s = (10 - Sense) / 11` and `u = min(s + 0.6, 1)`, span `1 / (u - s)` and centre `-s * span` |

The activation runs `P:0x0924C1` with A = 0, the Speed path. The Tempo callback
`P:0x09274B` acts only on the second call of a two-part edit, the one with
A = `0x50`: it writes the new Tempo back into index 6 and reruns `P:0x0924C1`
with A set, and that path writes the nearest Speed step back into index 4 before
staging the step from the exact tempo rate, which the job follows. The Speed
callback `P:0x09273A` clears A, so a Speed turn takes the Speed path even while
Tempo holds a note value.

Message 13 reaches `P:0x092541` for subtypes 1 and 2 only. It reads the peak hold
as a level and steps the follower; `P:0x0924A7` then sets the sweep word, for the
Touch Wah to `to_q23(centre + span * follower)` clamped inside +-1, for the Auto
Wah to the LFO output.

## Resonance filter firmware map

The Resonance Filter and the Touch Res Filter (subtypes 3 and 4) are one routine
and one UI object; the Touch Res Filter redesigns its coefficients on every tick.

| Item | Address / value |
|---|---|
| Audio continuation | `P:0x000EE9..0x000F23`, task record `Y:0x3C4` |
| State | `X/L:0x060..0x065` left, `0x066..0x06B` right: the input's last two words, and for each biquad its 48-bit output and the high word of the one before |
| Coefficients | `Y:0x3DA..0x3ED`, five a biquad, two biquads a channel; staged at `X:0x098928..0x09893B`, published by the job `X:0x09893C` (`P:0x00026F`) |
| Shifts and clip | `Y:0x3CF` = 3 before the biquads, then `Y:0x3D0` = 3 and the clip words `Y:0x3D1..0x3D4` (`P:0x092147`) |
| Input, wet, dry | `Y:0x3EE..0x3F0` |
| UI object | `X:0x098948`; activation `P:0x091BB1`, dispatcher `P:0x091ABE` |

Every frame: the input gain and ASR #3, per channel two biquads in the EQ
kernel's form, the second fed by the first's output history, then the soft clip,
ASL #3 and the wet/dry output.

The design (`gm2_res_design.c`), in the firmware's software float:

- Hi-Cut and FrqMax are `10^(1 + (raw + 6)/20)` Hz (`P:0x08F0DF`).
- `P:0x091F81`: Style picks the tick's Q, `{0x53AE15, 0x82}` for 2nd order and
  `{0x5BD70A, 0x81}` for 4th; Res sets `q = 0.7 * 2^(Res * k * 4.84)`, k = 0.01
  for 2nd and 0.005 for 4th order; each Q has the gain `sqrt(0.7 / Q)`.
- `P:0x091E7B`, inside `P:0x09208E`: the frequency clamped to 150 Hz..15 kHz and Q
  to 0.7..20, `w0 = 2 rate tan(pi f / rate)`, and the low-pass prototype
  `(w0^2, w0 / Q, 1)` through the bilinear matrix give the five words, with the
  denominators and gains folded into quarters and halves.
- `P:0x091FF2`: for 2nd order the second biquad passes the signal through
  (`0, 0, 0x200000, 0xE00000, 0`), for 4th it repeats the first; the right
  channel copies the left.
- The Touch Res tick `P:0x091CF9` reads the level `X:0x0B03`, steps its follower
  with the Response words, and designs at
  `10^2.5 Hz * (FrqMax / 10^2.5 Hz)^(centre + span * follower)`, clamped between
  those two, with Style's Q.

The two Styles differ in when they land. The Resonance Filter's Style callback
`P:0x0921D3` designs at once but leaves `P:0x091F81` alone, so it reuses the Q
words the last Res turn set; a Res turn refreshes them first. The Touch
Res Filter's Style entry is the do-nothing `P:0x0834CD`, so the word waits for the
next tick, and only the activation `P:0x091BB1` runs `P:0x091F81`.

## Delay firmware map

Ping Pong, Dynamic, Dual and Reverse Delay (algorithm 4, subtypes 0..3) share one
audio continuation, one UI object and one parameter-banked descriptor layout.

| Item | Address / value |
|---|---|
| Audio continuation | `P:0x001239..0x00125D` calling `P:0x0011F9..0x001238`, task record `Y:0x4F9` |
| Engines | Engine 0 (`X:0x30B..0x340`, `Y:0x506..0x52F`, buffer `X:0x0AB000..0x0D52FF`, jobs `X:0x098E15..0x098E48`) and Engine 1 (`X:0x341..0x376`, `Y:0x530..0x559`, buffer `X:0x0D5300..0x0FF5FF`, jobs `X:0x098E49..0x098E7C`), swapped by `X:0x098DF4` (`bchg #0` on activation) for preset spillover |
| Buffer | `0x02A300` words (172,800 words = 86,400 stereo frames = 1.8 s at 48 kHz) per engine; write pointer `Y+37` starts at `base + 0x02A2FF` and steps down two words a frame, wrapping by `+0x02A300` when `write_ptr <= base` |
| State (`X:0x30B` +) | `+0..19` staged matrix and filter words; `+26..29` Dynamic ducking words; `+30` ducking envelope; `+31..32` filtered channel outputs; `+33..36` channel 0 tap offsets and gains; `+37..40` channel 1 tap offsets and gains; `+41..42` and `+46..47` reverse offsets; `+43..45` and `+48..50` LP/HP filter states `u, v, w`; `+51` wet scale `0x7FFFFE`; `+52` subtype |
| Live words (`Y:0x506` +) | `+0..3` 2x2 input matrix; `+4..7` 2x2 feedback matrix; `+8..11` 2x2 output matrix; `+12..19` per-channel first-order LP and HP filter coefficients; `+20..25` dead words from `P:0x0948DE`; `+26` Reverse grain ms; `+27` Reverse timer target; `+28..41` engine pointer table (`+40` is `rev_step`: 4 for Reverse, 0 otherwise) |
| Input, wet, dry | `Y:0x55A..0x55C`, staged by `P:0x08F763` from `UI+63` (`X:0x098EBC`) |
| Jobs per engine | `S+36` (`P:0x0001FD`): 20 words `X+0..19 -> Y+0..19`, snap 1, step `0x008371`; `S+40` (`P:0x00026F`): 6 dead words `X+20..25 -> Y+20..25`; `S+44` and `S+48` (`P:0x000019 -> P:0x000293`, check `P:0x000301`): tap crossfades on `X+33..36` and `X+37..40`, step `0x008312`. Four records an engine, plus the level record the level code adds: nine in all |
| UI object | `X:0x098E7D`; dispatcher `P:0x09413D`, activation `P:0x09423F`, init `P:0x094336`, subtype tables `P:0x085EB5`, `P:0x085FA5`, `P:0x086095`, `P:0x086185` at `X:0x098ED5..0x098ED8`, active table written to `P:0x08627C` by `P:0x094A03` |

### Subtype parameters

The UI object banks parameters 1..14 per subtype at `UI + 3 + 14 * subtype`
(`P:0x094509` loads defaults from descriptor word `+13`, `P:0x09452D` swaps banks
on subtype change); live raw values sit at `X:0x099B76 + index`:

| Index | 0 Ping Pong | 1 Dynamic | 2 Dual | 3 Reverse |
|---|---|---|---|---|
| 0 | Subtype 0 | Subtype 1 | Subtype 2 | Subtype 3 |
| 1 | Dly `0..1800` ms | Dly `0..1800` ms | Dly1 `0..1800` ms | Dly `50..1000` ms |
| 2 | — | — | Dly2 `0..1800` ms | — |
| 3 | Tempo `0..16` | Tempo `0..16` | Tempo1 `0..16` | Tempo `0..16` |
| 4 | Width `0..100` % | — | Tempo2 `0..16` | — |
| 5 | FB `0..100` % | FB `0..100` % | FB1 `0..100` % | FB `0..100` % |
| 6 | — | — | FB2 `0..100` % | — (read as 0) |
| 7 | FBHcut `40..60` | FBHcut `40..60` | FBHcut `40..60` | FBHcut `40..60` |
| 8 | FBLcut `0..40` | FBLcut `0..40` | FBLcut `0..40` | FBLcut `0..40` |
| 9 | — | Offset `0..200` ms | Pan1 `-50..50` | — (read as 0) |
| 10 | — | Sense `-50..0` dB | Pan2 `-50..50` | — (read as 0) |
| 11 | — | Damp `0..100` dB | — | — |
| 12 | — | Rel `11..21` | — | (`DLY RevTim`, dummy cb, unread) |
| 13, 14 | Mix, OutLev | Mix, OutLev | Mix, OutLev | Mix, OutLev |

The manual prints Feedback as `0..120 %` and Reverse Delay time as `50..800 ms`;
the descriptor tables clamp Feedback to `0..100` and Reverse Delay time to
`50..1000`.

### Kernel, setup, ducking tick and reverse splice

Every frame, `P:0x001239` scales the stereo input by `Y:0x55A` and calls
`P:0x0011F9` on each engine, summing the two engines' wet outputs before
`Y:0x55B` and `Y:0x55C`. With the inactive spillover engine silent, one engine
gives the exact sum. Inside `P:0x0011F9`:

- `ch0_in = limit24(Y[0]*in_L + Y[2]*in_R + Y[5]*ch0_out + Y[7]*ch1_out)` and
  `ch1_in = limit24(Y[1]*in_L + Y[3]*in_R + Y[4]*ch1_out + Y[6]*ch0_out)` are
  written to `X:(write_ptr - 1)` and `X:(write_ptr)`.
- Each channel reads two taps at `write_ptr + tap_offset[t] + rev_offset[t]`
  (wrapped by `0x02A300`), advances `rev_offset[t]` by `rev_step` (wrapped by
  `0x02A300`), blends `u = limit24(gain[0]*tap[0] + gain[1]*tap[1])`, and runs
  the first-order LP `v = macr(-v_prev*c_hi + d_hi*u_prev + d_hi*u)` and HP
  `w = macr(-c_lo*w_prev - d_lo*v_prev + d_lo*v)` into `ch_out`.
- The output matrix forms `wet_L = limit24(Y[9]*ch0_out + Y[11]*ch1_out)` and
  `wet_R = limit24(Y[8]*ch1_out + Y[10]*ch0_out)`.

Updaters (`gm2_delay_design.c`):

- `P:0x094574` sets `X+51 = mpyr(to_q23(sin(Mix * 0.01 * pi/2)), OutLev)`
  (`0x7FFFFE` at Mix 100 %, OutLev 0 dB) and fills `X+0..7`: Ping Pong sums
  `0.5*(L+R)` into channel 0 and cross-couples `fb` in `X+6..7`; Dynamic sums
  `0.5*(L+R)` into channel 0, feeds `fb` in `X+5` and copies channel 0 into
  channel 1 via `X+6 = 0x7FFFFF`; Dual and Reverse feed L and R at `0x7FFFFE`
  into channels 0 and 1 with independent feedbacks `X+5 = fb1`, `X+4 = fb2`.
- `P:0x094643` fills `X+8..11`: Ping Pong sets `(0.5 +- Width * 0.005) * S`;
  Dynamic leaves `X+8..11` for the ducking tick (`X+10..11 = 0`); Dual and
  Reverse pan each channel by `(Pan + 50) * 0.01` scaled by `0x7FFFFE`.
- `P:0x094835` maps `FBHcut` and `FBLcut` through `P:0x08F0DF`, computes
  `t = tan(pi * f / rate)`, `c = to_q23((t - 1)/(t + 1))`,
  `d_hi = macr(0.5 + 0.5*c_hi)` and `d_lo = macr(0.5 - 0.5*c_lo)` into
  `X+12..15`, and sets `X+16..19` to `{0xF79C46, 0x3BCE23, 0x802027, 0x7FEFEC}`
  for Dynamic and a copy of `X+12..15` otherwise.
- `P:0x094735` stages the tap offsets, and its A picks the path: the raw Delay
  of every Dly and Offset callback arrives with A = 0, the tempo path of the
  activation and a subtype selection with A set. The tempo path reads the clamped
  global Tap Time (parameter 0x17 at `X:0x099A6B`), computes
  `delay_ms = min(1800, to_int(beat_ms * note[Tempo]))`
  (`gm2_delay_note_table` at `X:0x098DF3..0x098E14`) and writes the truncated ms
  back to the raw Dly slot through `P:0x0841B8`, which is what keeps a Tempo turn
  and the milliseconds on the page in step. For subtypes 0..2 it stages
  `2 * to_int({0x400008, 0x7F} + delay_ms * rate_khz) + (ch + 1)` at
  `X:0x098E2A` and `X:0x098E35`, taking the Dual's second line from its own Delay
  or Tempo word; for Reverse it writes `delay_ms` to `Y+26` and stages no offset,
  because the grain splice sets both.
- `P:0x09494E` computes Dynamic's `X+26..29`: Sense `to_q23(1 + Sense * 0.02)`,
  Damp `to_q23(10^(-0.05 * Damp))`, the 20 ms attack `0x3D2D9B` and Release from
  `gm2_time_table[Rel]` on the 13 ms tick. On message 13, `P:0x0944BE` compares
  `X:0x0B03` against `X+26`, steps `X+30` towards `X+27` (above) or `0x7FFFFF`
  (at or below) and writes `mpyr(X+30, X+51)` to `X+8` and `X+9`.
- Reverse Delay's service entry `X:0x0985DD` runs `P:0x094484`, which compares
  the 1 kHz hardware timer `X:$FFFF8C` (`P:0x0902D9`, period `0x00C4D9` = 50,393
  cycles = 1 ms) against `Y+27`. Each time `delay_ms` elapses and neither tap job
  is mid-fade, `P:0x094424` zeroes the silent tap's `rev_offset`, flips both
  channels' `X:0x098E2A` and `X:0x098E35` between `1` and `3`, and advances
  `Y+27` by `Y+26`; `P:0x000301` and `P:0x000293` then crossfade to the new
  grain over 501 chunks. A Delay or Tempo turn runs `P:0x094735` again, which
  re-anchors `Y+27` to the current 1 kHz count so the grain period starts over.

## Reverb firmware map

Spring, Hall, Room and Plate (algorithm 5, subtypes 0..3) run one audio
continuation, one UI object, one descriptor table (`P:0x086375`) and one
parameter setup chain (`P:0x095099`). `Subtype` (`0..3`) is read only by
`P:0x095B33`, which calls `P:0x0951D0` to swap the eleven parameters between the
live slots `X:0x099B87..0x099B91` and the four saved banks in `UI+3..58`
initialized at boot (`P:0x0951AD`) from `X:0x0992EC..0x0992EF` (`X:0x09A2E4`,
`0x09A2F2`, `0x09A300`, `0x09A30E`).

| Item | Address / value |
|---|---|
| Audio continuation | `P:0x0012D2..0x0013DE`, task record `Y:0x57B` |
| Circular buffer | `Y:0x0A0000..0x0A7FFF` (32,768 words, `m7 = m5 = 0x7FFF`); write pointer `X:0x37D` starts at `0x0A7FFF` and increments by `+1` each frame modulo `0x8000` |
| State (`X:0x37D` +) | `+0` write pointer; `+1..42` circular buffer tap offset deltas; `+43` pre-delay feedback sample; `+44..45` tank output sums A and B; `+46..47` left and right early biquad outputs; `+48` input LP state; `+49..62` seven tank damping filter state pairs; `+63..76` tank feedback matrix signs; `+77..79` and `+88..90` left/right early comb states; `+80..87` and `+91..98` left/right early biquad states; `+99..105` seven tank primary tap samples; `+106..113` and `+116..123` left/right early tap weights; `+114..115` and `+124..125` left/right early biquad `a0` words |
| Staged and live words | 80 words staged at `X:0x098F64..0x098FB3` (`S+0..79`), published to `Y:0x589..0x5D9` (`Y+14..62` and `Y+64..94`, with `Y+63` unused zero) by six smoothing jobs `X:0x098FDC..0x098FF3` (`P:0x0001FD` on `S+0..48`, `S+49..51`, `S+52..54` and `S+75..79`; `P:0x00026F` on `S+55..64` and `S+65..74`, all snap 1, step `0x008371`) |
| Input, wet, dry | `Y:0x5DA..0x5DC`, staged at `X:0x099046..0x099048` by `P:0x08F763` from `UI+63` (`X:0x099033`); initial activation (`UI+1 = 1`) zeroes `UI+69` so `P:0x095BE1` stages the 0 dB `OutLev` word `0x7FFFFE` |
| UI object | `X:0x098FF4`; dispatcher `P:0x094FC2` (message 13 is a no-op), activation `P:0x095099`, init `P:0x0951AD`, rate update `P:0x095186`, descriptor record `P:0x086465`, table `P:0x086375` |

### Parameters and factory subtype banks

Each factory bank is 14 words, the first eleven the parameters below; the other
three are not knobs.

| Index | Parameter | Range | UI strings / unit | Subtype 0 Spring (`X:0x09A2E4`) | Subtype 1 Hall (`X:0x09A2F2`) | Subtype 2 Room (`X:0x09A300`) | Subtype 3 Plate (`X:0x09A30E`) |
|---|---|---|---|---|---|---|---|
| 1 | Decay | `1..200` | `0.1..20.0` s | `20` | `25` | `12` | `15` |
| 2 | PreDly | `0..100` | `0..100` ms | `0` | `50` | `40` | `5` |
| 3 | Shape | `0..2` | Round, Curved, Square (`X:0x0998B2..0x0998B4`) | `0` | `0` | `0` | `1` |
| 4 | Size | `0..7` | Box, Tiny, Small, Medium, Large, XL, Grand, Huge (`X:0x0998B5..0x0998BC`) | `3` | `4` | `3` | `3` |
| 5 | HiCol | `0..6` | Wool, Warm, Real, Clear, Bright, Crisp, Glass (`X:0x099977..0x09997D`) | `4` | `3` | `2` | `1` |
| 6 | HiFact | `-25..25` | `-25..25` | `6` | `0` | `0` | `5` |
| 7 | LoCol | `0..6` | Thick, Round, Real, Light, Tight, Thin, NoBass (`X:0x09997E..0x099984`) | `3` | `4` | `4` | `5` |
| 8 | LoFact | `-25..25` | `-25..25` | `0` | `9` | `9` | `6` |
| 9 | RoomLv | `-100..0` | dB | `0` | `0` | `0` | `0` |
| 10 | RevLev | `-100..0` | dB | `-3` | `-6` | `-6` | `-6` |
| 11 | Diffus | `-25..25` | `-25..25` | `10` | `0` | `0` | `10` |

### Kernel and setup

Every frame (`P:0x0012D2..0x0013DE`):

- Scales `(in_L + in_R) * Y:0x5DA` by `ASR #2` into `Y:(r7)`, steps `X[48]`
  (`Y[14]*X[48] + Y[15]*X[43]` stored to `X[48]` before adding `Y[16]*X[43]` and
  `ASR #2`), and runs two allpass diffusers (`Y[17..18]` and `Y[19..20]`) into
  `y1`.
- Steps seven tank delay lines (`k = 0..6`, `P:0x0012F9..0x001340`): each reads
  `tap_k` into `X[99 + k]`, updates its two-state damping filter `X[49 + 2*k]`
  and `X[50 + 2*k]` with `Y[21 + 6*k .. 25 + 6*k]`, adds `y1`, and writes the new
  tank input to `Y:(r7)`.
- Forms the tank sums `X[44] = limit24(4 * sum(X[99 + k] * X[64 + 2*k]))` and
  `X[45] = limit24(4 * sum(tapB_k * X[63 + 2*k]))`.
- Per channel (`P:0x001364..0x0013C3`), sums eight early reflection taps
  weighted by `X[106..113]` (left) or `X[116..123]` (right) into three groups of
  `(3, 3, 2)` taps with one-pole comb feedbacks `X[77..79]` / `Y[64..66]` (left)
  and `X[88..90]` / `Y[77..79]` (right), runs two cascaded biquads (`Y[67..76]`
  and `Y[80..89]`, 48-bit outputs with low words in `X[114..115]` and
  `X[124..125]`) into `X[46]` and `X[47]`, and advances `X[0] = (n3 + 1) mod
  0x8000`.
- Mixes `wet_R = limit24(Y[90]*X[47] + Y[92]*X[45])` and
  `wet_L = limit24(Y[91]*X[46] + Y[93]*X[44])`, stores the pre-delay feedback
  `X[43] = limit24(Y[94] * Y:(n3 + X[42]))`, and scales each wet output by
  `2 * Y:0x5DB` (`mpy + asl + macr`).

Updaters (`gm2_reverb_setup.c`):

- `P:0x09520C` looks up `X:0x09913C[5*Size + 45*Shape + 3]`, sets
  `S[75..76] = db_to_lin(RoomLv + adj - 14)` and `rev_g = db_to_lin(RevLev - 5)`,
  and stages `S[77..78] = min(rev_g, 0x7FFFFF)` (railed to `0x7FFFFF` from
  `0x400000` up) and `S[79] = max(rev_g, 0x400000)`.
- `P:0x0952D4` scales `LoCol` (`X:0x09929B`) and `HiCol` (`X:0x0992A9`) by
  `1 + Fact / (Fact > 0 ? 50 : 100)`, computes the 15-bit filter poles `c_lo`
  and `c_hi` (`to_int(exp(-2*pi*f/rate) * 32767)`), interpolates the 513-word
  decay curve `Y:0x0952..0x0B52` (`P:0x08F161`) at `-3 * len / decay_{lo,mid,hi}`
  for each of the seven tank lengths `X:0x0992C9..0x0992CF`, scales
  `(g_mid - g_hi) * (0x8000 - c_hi)` through `P:0x08F179`, and shifts the six
  words per tank left by 8 bits into `S[7..48]`, and ends by calling `P:0x0954A4`.
- `P:0x0954A4` scales `Decay` when either color multiplier exceeds `100`, maps it
  to a base diffusion (`0.6727 * Decay` below `71`, `0.1802 * Decay + 35` from
  `71` up), adds `Diffus`, clamps to `[1, 55]`, divides by `100`, raises to
  `{0x555326, 0x7E}` (`1/3`) through `P:0x0970F1`, and stages `g` and `1 - g^2`
  into `S[3..6]`.
- `P:0x095601` scales the diffuser lengths (`0x3C0F`, `0x1BA9`) and the fourteen
  tank lengths (`X:0x0992C9..0x0992D6`) by `to_int48(rate) / 10` into
  `X[1..20, 22..26]`; `P:0x095710` writes the fourteen signs `X:0x0992DB..0x0992E8`
  as `0x7FFFFF` / `-0x800000` into `X[63..76]`.
- `P:0x095730` scales the eight left (`X:0x09904C`) and eight right (`X:0x0990C4`)
  early reflection tap positions by `X:0x09913C[5*Size + 45*Shape]` and
  `PreDly`, clamps each tap offset to `0x0033AE` (13,230 samples), and writes
  the relative deltas into `X[21, 27..42]`.
- `P:0x09526A` computes the input bandwidth filter `S[0..2]` from `HiCol` and
  `HiFact`; `P:0x095973` computes the six early comb feedback words `S[49..54]`
  and the sixteen signed tap weights `X[106..113, 116..123]`; `P:0x095A71` calls
  `P:0x08F233` (`X:0x0A5C` Q table, `P:0x097140` sin, `P:0x096F9F` cos) twice on
  `X:0x0991C3[9*HiCol + 72*Shape]` and permutes `{b0, b2, b1, a2, a1}` into
  `S[55..74]`.

Only the 80 staged words glide, one snap-and-step per chunk run: the six jobs
walk `Y+14..62` and `Y+64..94` toward `S+0..79`. The tap offsets, tank lengths,
matrix signs and early tap weights go straight into the state, so a Size, Shape
or PreDly turn moves the reads on the next frame while the filter and matrix
words follow over several chunks.

## Phaser and Univibe firmware map

The Modulation block (`P:0x085445`, dispatcher `P:0x0927CA`, outer object
`X:0x098A13`) dispatches subtypes 0..12 through the thirteen 3-word records at
`P:0x08532E..0x085354`: subtypes 0..8 run `P:0x0936C3` (`X:0x098C7F`), subtypes
9 and 10 (Vintage Phaser and Smooth Phaser) run `P:0x092AA9` (`X:0x098A8D`), and
subtypes 11 and 12 (Vintage Univibe and Modern Univibe) run `P:0x09301F`
(`X:0x098B7A`).

### Vintage Phaser and Smooth Phaser (subtypes 9 and 10)

| Item | Address / value |
|---|---|
| Audio continuation | `P:0x000FA8..0x001060`, task record `Y:0x420` |
| State (`X:0x2C2..0x2DD`) | `x[0..2]` feedback pre-clip filter states; `x[3]` feedback post-clip LP state; `x[4..6]` input all-pass pair; `x[7..15]` left 12-stage all-pass chain (4-stage tap `x[9]`, 12-stage tap `x[15]`); `x[16..22]` right 12-stage all-pass chain (4-stage tap `x[18]`, 12-stage tap `x[22]`); `x[23]` stage-1 to right-branch handoff; `x[24]` mono input sum; `x[25]` filtered feedback; `x[26]` feedback sum; `x[27]` LFO phase `[0, 0x7FFFFF]` |
| Staged and live words | 22 words staged at `X:0x098A1F..0x098A34` (`S+0..21`), published to `Y:0x433..0x458` by nine jobs `X:0x098A69..0x098A8C` (`P:0x0001B9` linear smoother armed via `P:0x09605F` on `S+0` and `S+1`, `P:0x0001FD` one-pole smoother on `S+2..21`, snap 1, step `0x008371`); `Y:0x43B..0x441` (`0x0147AE`, `0x0147AE`, `0x7D9168`, `0x7B020C`, `0x972B02`, `0x5DD2F2`, `0x74FDF4`) and `Y:0x457` (`0x7FFFFF`) are fixed |
| Input, wet, dry | `Y:0x459..0x45B`, staged at `X:0x098AC4..0x098AC6` by `P:0x08F763` from `UI+36` (`X:0x098AB1`) |
| UI object and tables | `X:0x098A8D`; dispatcher `P:0x092AA9`, activation `P:0x092B92`, sub-record `P:0x08562D`, Vintage Phaser table `P:0x08544D`, Smooth Phaser table `P:0x08553D`, Range table `X:0x098ACA..0x098AED` (`gm2_phaser_range_table`) |

Updaters (`gm2_phaser_design.c`):

- `P:0x092D6D` evaluates `((1.0 - x) / 2.0)^(1/3)` (`P:0x092D04`) across the nine
  `gm2_phaser_range_table` entries of `Range` (High at offset 0, Low at offset 9)
  into `S[2..7, 11..13]` and calls `P:0x092D1C`, which computes the feedback
  post-clip LP words `S[8..10]` from `period` (`1 / rate`).
- `P:0x092E54` stages `{0x7FFFF8, 0, 0}` (Vintage, subtype 9, selecting the
  4-stage taps `x[9]` and `x[18]`) or `{0, 0, 0x7FFFF8}` (Smooth, subtype 10,
  selecting the 12-stage taps `x[15]` and `x[22]`) into `S[14..19]` (written via
  `y:(r5+n5)` in the `AAR0 = 0x0805B9` shared external SRAM `0x080000..0x09FFFF`).
- `P:0x092E88` maps `FB` (`-100..100`) to `S[20]`; `P:0x092EAF` maps `Depth`
  (`0..100`) to `min(0x7FBE77, to_q23(Depth * 0.01))` in `S[1]`; `P:0x092EDA`
  computes the LFO step `S[0]`; `P:0x092F2B` sets `S[21]` to `0x800001` when
  `PhaRev != 0` and `0x7FFFFF` otherwise.

The block's list holds nine records, two `P:0x0001B9` for the LFO step and depth
and seven `P:0x0001FD` for the rest, plus the level record. The Speed callback
`P:0x092F8F` clears A before it runs `P:0x092EDA`, so a Speed turn takes the Speed
path even under a Tempo value; the Tempo callback `P:0x092FBA` acts only on the
second call of the edit, the one with A = `0x50`, writes the note into raw
index 4 and reruns the step.

### Vintage Univibe and Modern Univibe (subtypes 11 and 12)

| Item | Address / value |
|---|---|
| Audio continuation | `P:0x00107F..0x001116` (helpers `P:0x001061`, `P:0x001072`), task record `Y:0x45C` |
| State (`X:0x2DE..0x2EF`, `Y:0x483..0x486`) | `x[0..1]` stereo input/output scratch; `x[2]` slewed LFO speed; `x[3]` LFO phase `[0, 0x7FFFFF]`; `x[4]` lamp brightness one-pole state; `x[5..8]` four all-pass states; `x[9]` fourth all-pass output; `x[10..17]` four `{coef_curr, coef_step}` pairs; `y483[0..3]` per-frame stepped all-pass coefficients |
| Staged and live words | 35 words staged at `X:0x098B08..0x098B2A` (`S+0..34`), published to `Y:0x46B..0x496` by eight jobs `X:0x098B5A..0x098B79` (`P:0x0001B9` on `S+0..1`, `P:0x0001FD` on `S+2..34`); `Y:0x46C` (target LFO speed) and `Y:0x473..0x476` (waveshaper polynomial `{0, 0, 0x0CCCCD, 0}` for Vintage subtype 11, `{0, 0x200000, -0x100000, 0}` for Modern subtype 12) are written directly to Y |
| Input, wet, dry | `Y:0x497..0x499`, staged at `X:0x098BB1..0x098BB3` by `P:0x08F763` from `UI+36` (`X:0x098B9E`) |
| UI object and tables | `X:0x098B7A`; dispatcher `P:0x09301F`, activation `P:0x09311B`, sub-record `P:0x085815`, Vintage Univibe table `P:0x085635`, Modern Univibe table `P:0x085725`, all-pass table `X:0x098BB8..0x098BC7` (`gm2_univibe_ap_table`), bipolar sine table `Y:0x0100..0x01FF` (`gm2_sine_table`, generated at boot by `P:0x083A02`) |

Updaters (`gm2_univibe_design.c`):

- `P:0x0932C2` maps the eight `gm2_univibe_ap_table` frequencies through the
  bilinear pole `((1 / tan(pi * f)) - 1) / ((1 / tan(pi * f)) + 1)` (`P:0x093276`)
  and `((1 - x) / 2)^(1/3)` (`P:0x0932AE`, exponent `{0x555556, 0x7E}`) into the
  four `{slope, base}` pairs `S[23..30]`, writes the input and output soft-clipper
  blocks `S[11..22]`, and writes the subtype waveshaper polynomial into
  `Y:0x473..0x476`.
- `P:0x0933CD` computes the Attack (`S[4..5]`), Release (`S[2..3]`), and lamp
  filament (`S[6..9]`, 5 ms rise / 30 ms fall) one-pole pairs on the 4-frame chunk
  rate (`4 * period_ms`).
- `P:0x09339D` maps `Depth` (`0..100`) to `min(0x3FF972, to_q23(Depth * 0.005))`
  in `S[0]` with `S[1] = 0x400000`; `P:0x093565` stages the internal equal-power
  notch mix `S[31..34] = {±0x5A827B, 0x5A827B, 0x5A827B, 0x5A827B}` (with `S[31]`
  negated to `0xA57D85` when `PhaRev != 0`); `P:0x09349C` computes the 4-frame
  chunk LFO speed step (`hz * period * 256 * 4`, zeroed when the effective speed
  is `<= 37`) for `Speed 1` / `Tempo 1` (`Select == 0`) or `Speed 2` / `Tempo 2`
  (`Select != 0`) into `Y:0x46C`.

The list holds eight records, one `P:0x0001B9` for the two-word Depth pair and
seven `P:0x0001FD`, plus the level record. `P:0x093612` is the callback of both
Speed entries and of Select, and it clears A, so each of the three re-runs the
speed step on the Speed path. Both Tempo entries call `P:0x093637`, which acts
only on the second call of the edit and writes its value into raw index 4, so a
Tempo 2 edit lands on Tempo 1.

### Choruses, Flangers and Vibrato (subtypes 0..8)

Subtypes 0..8 (Classic Chorus, Advanced Chorus, Classic Flanger, Advanced
Flanger, Tri-Chorus Normal, Tri-Chorus Asymmetric, Through-Zero Flanger Negative,
Through-Zero Flanger Positive, and Vibrato) share one UI object (`X:0x098C7F`),
one audio continuation (`P:0x001117..0x0011F8`) and one setup chain (`P:0x0938BE
-> P:0x093A13`).

| Item | Address / value |
|---|---|
| Audio continuation | `P:0x001117..0x0011F8` (tap crossfaders `P:0x00033A` / `P:0x00033C`), task record `Y:0x49A` |
| Circular buffers | Left `X:0x080000..0x080FFF`, right `X:0x081000..0x081FFF` (4,096 words each, `m0 = m3 = 0x0FFF`); write pointers `Y:0x4A1` and `Y:0x4A2` decrement by 1 each frame |
| State (`X:0x2F0..0x30A`, `L:0x06C..0x073`) | `x[0..1]` input Hi-Cut LP states; `x[2]` LFO phase `[0, 0x7FFFFF]`; `x[3..8]` six voice LFO sine outputs; `x[9..20]` six voice tap pairs (`x[20]` overwritten by left output biquad); `x[21..22]` left and right voice sums; `x[23..26]` left and right feedback LP and 200 Hz HP states; `X:0x06C..0x06D` and `X:0x070..0x071` output biquad input delays, `L:0x06E` and `L:0x072` 48-bit `y[n-1]` states, `X:0x06F` and `X:0x073` 24-bit `y[n-2]` high words |
| Staged and live words | 30 words staged at `X:0x098BD1..0x098BEE` (`S+0..29`), published to `Y:0x4AD..0x4B9, 0x4E4..0x4F4` by five jobs `X:0x098C53..0x098C66` (`P:0x0001FD` on `S+0..11`, `S+12` and `S+13`; `P:0x00026F` on `S+14..23` and `S+24..29`); six voice delay blocks `Y:0x4BA..0x4D7` (`{depth_staged, depth_latched, tap0_off, 0x0960, tap1_off}`) and six tap crossfade records `X:0x098C01..0x098C42` (`Y:0x4D8..0x4E3` tap gains, driven by `X:0x098C67..0x098C7E`) |
| Input, wet, dry | `Y:0x4F6..0x4F8`, staged at `X:0x098D19..0x098D1B` by `P:0x08F763` from `UI+135` (`X:0x098D06`) |
| UI object and tables | `X:0x098C7F`; dispatcher `P:0x0936C3`, activation `P:0x0938BE`, sub-record `P:0x085EAD`, subtype tables `P:0x08581D, 0x08590D, 0x0859FD, 0x085AED, 0x085BDD, 0x085CCD, 0x085DBD` (`X:0x098DEB..0x098DF3`), LFO phase offsets `X:0x098D1F..0x098D30`, voice gains `X:0x098D37..0x098D54`, Depth tables `X:0x098D55, 0x098D71, 0x098D8D`, Width tables `X:0x098DA2, 0x098DB7` |

Updaters (`gm2_moddelay_design.c`):

- `P:0x093820` builds six 256-word unipolar sine tables at `X:0x1000..0x15FF` from
  `X:0x098D1F` (all 0s for subtypes `0..3, 6..8`), `X:0x098D25` (`{0, 0x200000,
  0x200000, 0, 0x600000, 0x600000}` for subtype 4 Tri-Chorus Normal), or
  `X:0x098D2B` (`{0, 0x072B02, 0x0E353F, 0x156042, 0x1C6A7F, 0x239581}` for
  subtype 5 Tri-Chorus Asymmetric); `P:0x093884` copies the six voice tap gains
  from `X:0x098D37..0x098D54` into `S[24..29]`.
- `P:0x093C85` computes the LFO step `S[12]` and calls `P:0x093B64`, which
  computes the Golden Ratio scale `clamp(1.0 - hz * 0.05, 0.03, 1.0)` when `Gold`
  is active on subtypes 1 and 3, interpolates the piecewise-linear Depth tables
  `P:0x093AC5` (`X:0x098D55, 0x098D71, 0x098D8D` scaled by `{0x68DB8C, 0x72}`) for
  subtypes 4..7, or scales `Depth * 0.01 * gold_scale` (multiplied by `0x19999A`
  on Vibrato, subtype 8) for subtypes `0..3, 8`, writing `Y:0x4BA + 5*v` (latched
  into `Y:0x4BB + 5*v` by the audio kernel when the voice's LFO is at its trough
  `<= 7`).
- `P:0x093CB7` copies the output biquads `S[14..23]` from `Y:0x0030..0x0034`
  (unity pass-through when `PhaRev == 0` on subtypes `1, 3, 4, 5, 6, 7`) or
  `Y:0x0035..0x003E` (stereo all-pass phase disperser when `PhaRev != 0` or on
  subtypes `0, 2, 8`).
- `P:0x093D5C` stages the six voice base delay offsets at `X:0x098C08 + 11*v`:
  `to_int(rate_khz * max(0.1, Delay * 0.1))` on subtypes 1 and 3, `to_int(rate_khz
  * 5.0)` on subtype 2, `to_int(rate_khz * 10.0)` on subtypes 0 and 8, `4` samples
  on subtypes 6 and 7, and the piecewise-linear Width tables `P:0x093B1B`
  (`X:0x098DA2, 0x098DB7` scaled by `0.1 ms * rate_khz`) on subtypes 4 and 5.
- `P:0x093EA1` maps `FB` to `to_q23(FB * 0.01)` on subtypes 2 and 3, `to_q23(FB *
  0.005)` on subtypes 6 and 7, and `0` otherwise in `S[13]`; `P:0x093EF9` maps `FB
  Cut` and the fixed 200 Hz HP pole into `S[4..11]`; `P:0x093F60` maps `HiCut`
  into `S[0..3]`.

A knob turn only stages words: the updaters copy them onto the live words at once
(`P:0x095FC8`) only while the block's flag `X:(UI+3)` is set (`P:0x093CA5`), which
the dispatcher raises around a selection (`P:0x093783`, `P:0x09378A`). The list
holds twelve records: the five smoothing
jobs, which glide the 30 staged words into `Y:0x4AD..0x4B9` and
`Y:0x4E4..0x4F4` once a chunk, and the six tap records, which each run their
check `P:0x00033C` and then their step `P:0x00033A`. Word +13 of every descriptor
entry is that parameter's factory default. The Speed callback runs `P:0x093C85`
and the Depth and Golden Ratio callbacks `P:0x093B64` on the Speed path even
under a Tempo value; only a Tempo turn's second call (`A = 0x50`) takes the tempo
path.

## Pitch firmware map

The Pitch block (`P:0x084B71`, dispatcher `P:0x0907EF`, UI object `X:0x0987EA`,
raw parameters `X:0x099B56..0x099B65`) runs all five subtypes (0 Detune, 1
Whammy, 2 Octaver, 3 Pitch Shifter, 4 Intelligent Pitch Shifter) through one
audio continuation (`P:0x000D15..0x000E55`, task record `Y:0x0397`), one
per-chunk detector (`P:0x0003F2..0x00089C`, entered as the audio chain's own
post-chunk task `P:0x0001E4..0x0001FA`) and one
activation chain (`P:0x090937 -> P:0x090F25 -> P:0x090F68`), switching descriptor
tables via `X:0x098853..0x098857` (`P:0x0846C1, 0x0847B1, 0x0848A1, 0x084991,
0x084A81`).

| Item | Address / value |
|---|---|
| Audio continuation | `P:0x000D15..0x000E55`, task record `Y:0x0397` (`Y:0x0398..0x039E`), boot init `P:0x082B31` |
| Circular buffers | Audio delay line `X:0x088000..0x08CE1F` (20,000 words, `m2 = 0x4E1F`, write pointer `X:0x03FD`); detector mono buffer `X:0x082000..0x0827FF` (2,048 words, `m4 = 0x07FF`, write pointer `X:0x042F`); pitch-period ring `X:0x0800..0x08FF` (256 words, `m3 = 0xFF`, write pointer `Y:0x0219`) |
| Voice and splice state | `X:0x03FE..0x0401` stereo input and voice outputs; `X:0x0402` chunk buffer pointer (`Y:0x0200..0x0207`); `X:0x0403` 32-chunk phase counter (`1..32`); `X:0x0410` valid pitch period; `X:0x0412..0x0415` target and slewed voice delays; `X:0x0416` crossfade rate factor; `X:0x0417..0x041C` two 3-word voice splice records `{weight, pos, span}`; `Y:0x0208..0x0211` two 5-word splice control blocks `{step, span, min, max, 0}`; `Y:0x021A..0x021B` period-ring tap offsets; `Y:0x021E..0x021F` voice pitch steps |
| Detector state | `Y:0x0275..0x0278` mono chunk scratch; `Y:0x0279..0x027D` (`gm2_pitch_det_biquad`) and `X:0x0425..0x0428` input biquad; `X:0x0429..0x042A` `-0.5` LP pole and state; `X:0x042B..0x042E` coarse template steps `{5, 7, 17, 31}`; `Y:0x027E..0x02BD` 64-word coarse template; `X:0x0430..0x0435` coarse AMDF pointers, state PC (`0x0604, 0x059A, 0x05D2`), 48-bit energy and norm shift; `X:0x0464..0x0564` 257-word coarse AMDF window; `X:0x0588..0x05A7` peak table; `X:0x0569..0x0572`, `Y:0x02BE..0x02D4` fine AMDF state PC (`0x0606, 0x062B, 0x0656, 0x06B4`), 7 lag scores and 16-word template; `X:0x0436..0x043E`, `X:0x0575..0x0587` tracked coarse/fine periods, confidence, log2 pitch (`X:0x043A`, via `X:0x0440..0x0445` `gm2_pitch_log2_poly`) and hysteresis state |
| Staged and live words | `X:0x041D..0x041E` target pitch ratios -> `Y:0x0224..0x0225` (payload `X:0x0987CC`, handler `P:0x000212`, a branch into `P:0x0001FD`, step `0x020DC6`); `X:0x0987C6..0x0987C7` feedback gains -> `Y:0x021C..0x021D` (`X:0x0987D2`, step `0x008371`); `X:0x0987C8..0x0987CB` voice pan/level gains -> `Y:0x0220..0x0223` (`X:0x0987D8`, step `0x008371`) |
| Input, wet, dry | `Y:0x03A0..0x03A2`, staged at `X:0x09884D..0x09884F` by `P:0x08F763` from `UI+80` (`X:0x09883A`) |

Updaters (`gm2_pitch_design.c`):

- `P:0x090A72` computes the period bounds `X:0x0566` and `X:0x0567`, the coarse
  AMDF lag count `X:0x0568` (`<= 256`), and the crossfade rate factor `X:0x0416`
  from `rate_khz` and Global `Tuner Range` (`X:0x099A63 = 0`); `P:0x090B50`
  computes the reference tuning offset `X:0x0424` from Global `Tuner Ref`
  (`X:0x099A61 = 440` Hz) and the sample rate.
- `P:0x0910A8` and `P:0x091017` expand the 8-word scale definition at
  `X:0x09A254 + 8*Scale` (`gm2_pitch_scale_table`) into the 8×12 chromatic cent
  grid `X:0x0997BF..0x09981E` (interpolating non-scale passing tones with
  `P:0x090FCD` and `P:0x090FEC`).
- `P:0x090C89` calls `P:0x0910BD` on subtype 4 (building the two 37-point octave
  deviation curves `Y:0x022B..0x024F` and `Y:0x0250..0x0274`, the base ratio
  words `Y:0x0227..0x022A`, the key-shifted reference `X:0x0424`, and `X:0x0423 =
  3`) or computes `((2^(cents / 1200) - 1) * 256) << 12` into `X:0x041D..0x041E`
  with `X:0x0423 = 0` on subtypes `0..3`.
- `P:0x090D7F` sets `X:0x0412` and `X:0x0414` to `to_int(Delay * rate_khz) << 8`
  on subtypes `0, 3, 4` and `0x037200` on subtypes `1, 2`; `P:0x090E51` (calling
  `P:0x090DD6` and `P:0x090E13` on subtype 3, clamping `FB1` to `100 - FB2` when
  `FB1 + FB2 > 100`) stages `X:0x0987C6..0x0987CB`.

Every knob turn is the generic edit `P:0x083268`, and each callback reruns only
what the knob feeds: Voice and Key run `P:0x090C89`, which on subtype 4 runs
`P:0x0910BD` twice so the octave curves and the base ratio pairs follow the knobs;
Delay runs `P:0x090D7F`; Pan and Level run `P:0x090E51`. A Scale turn runs
`P:0x0910A8` alone, which rebuilds the 8x12 cent grid and nothing else. The two
Feedback callbacks write the clamped value back into the parameter's raw word
when the pair overflows 100. `P:0x090F68` belongs to the activation chain
`P:0x090F25`, not to an edit. The list holds three records, the ratio pair under
`P:0x000212`, the feedback pair and the pan/level quad, plus the level record,
which makes four. Message 13 runs `P:0x090A46`: while `X:(UI+74)` is set it
compares `X:(UI+73)` with 300 and, once past it, clears the flag and re-runs
`P:0x090F68`. Nothing sets the flag (`P:0x090A59` only clears it), so the refresh
never runs.

## Arithmetic

The EQ audio kernel uses signed 24-bit samples and coefficients, signed 48-bit
state, and a wrapping 56-bit accumulator. The duplicated `a1` MAC, the shifts
and `ADDL` set the factors 8 and 4 of each band's sum, and rounding and limiting
have to happen at the firmware's store points. Only the wrapping sums between
those points may be regrouped, and only after checking that no intermediate
reaches 2^55: in the three-band EQ every product is at most 2^46 and every sum
stays below 2^52, so one band's chain is a single `int64` sum that never wraps.
The output's wet multiply is required: with the wet word `0x7FFFFE`, omitting it
changes every sample above about -12 dBFS.

The gate kernel stores its one-pole states, envelope and gain words as 24-bit
words and runs everything between those stores on the same 56-bit accumulator.
Its wet path is a plain MPY, so the 24-bit store floors where the EQ's output
MACR rounds. The frame's two one-poles, its two wet products and the detector's
envelope follower are chains of two doubled products of 24-bit words, at most
2^48 with the envelope it adds, so they need no accumulator wrap; the gain
computer keeps its own.

The compressor holds its detector level and smoothed gain as 48-bit values and
its gain word as a 24-bit word scaled down by 2^5. The ASL #5 after the gain
multiply can exceed full scale, so the 24-bit store's limiting is part of the
result. The output's MACR with the zero dry word still rounds the wet product.
Its own sums are two 24-bit products and a 48-bit state, under 2^49, while the
log2 result and the ASL #15 that widens the smoothing coefficient carry the
exponent and keep the 56-bit wrap.

The Tremolo/Panner routine keeps every state as a 24-bit word. Its phase wraps
and the Soft ramp's subtract depend on the extension condition, so on the exact
56-bit value, low word included. The input MPYR and the one-poles' MACRs round
at the firmware's points, and the output's MACR with the zero dry word still
rounds. Outside the Soft ramp's `asl`, which does wrap, every product there is at
most 2^47 doubled and no sum reaches 2^48.

The wah routine keeps its filter states as 48-bit values and multiplies them by
24-bit words with MPYSU/DMACSS pairs; its envelope, sweep and LFO words are
24-bit. Every sum there is one such state and a doubled product of two 24-bit
words, so it stays under 2^50 and only the 48-bit and 24-bit stores limit. The
resonance routine's biquads take the EQ kernel's form, with 48-bit outputs. Both
bend the accumulator through the parabolic soft clip (`gm2_soft_clip()`) before
ASL #3, so the 24-bit store after it limits.

The delay routine keeps every buffer sample, filter state and tap weight as a
24-bit word. Its input/feedback sum, two-tap blend and output matrix use unrounded
MPY/MAC chains limited at their 24-bit store points, while each channel's LP and
HP stages end with MACR and the wet/dry stage rounds the wet product through MACR
with the zero dry word. The tap crossfade job `P:0x000293` folds its parabola
with `ADDR B,A` (`gm2_addr()`) and detects both rails with `bes` (`gm2_es()`).

The reverb routine keeps its circular buffer, tank filters and early comb
filters as 24-bit words and its four early-reflection biquad outputs as 48-bit
values with low words in `X[114..115, 124..125]`. Parallel moves on `mac` at
`P:0x0012E9`, `P:0x0012EC` and `P:0x001300` store the accumulator's previous
value before the MAC updates it, and the wet/dry stage doubles the wet product
with `ASL` before `MACR`. Its longest chain is the early signal's eleven doubled
products of two 24-bit words (eight taps and three comb words); a tank line sums
four plus a word, each matrix sum seven, taken times four, and a biquad five,
doubled with the carried low word. So no intermediate reaches 2^52 and each
chain is one `int64` sum; only the rounding and the 24-bit store points follow
the firmware's accumulator.

The phaser routine keeps all 28 state words as 24-bit values, wraps its LFO
phase to zero on `ifes`, and runs its input section and seven chain sections,
two-pole all-passes, with unrounded MPY/MAC/SUB chains limited at each state
store. One section's two sums are below 2^49 and 2^48, with `g` at most 2^23 and
each operand pair at most 2^24 wide, so they are regrouped into one `int64`
expression each and never reach the 2^55 wrap. The univibe routine runs its LFO
speed slewer, bipolar sine lookup (`Y:0x0100..0x01FF`), cubic waveshaper, lamp
slewer and all-pass target deltas (`(target - curr) >> 2`) on the first frame of
each four-frame chunk (`jclr #2,x:$0001`), steps the four all-pass coefficients
on every frame, and passes the mono input and both notch-mixed channels through
`P:0x001061`, which scales `-over^2` by an explicit 24-bit curvature word before
shifting; the clipper's squared term is below 2^48 before that shift and below
2^52 after it, so it and each of the four all-pass stages' three-term sums,
below 2^49, are one `int64` expression each and never reach the 2^55 wrap. The
modulation delay routine negates its stereo inputs with `mpyr -y0,x0,a` and
`mpyr -x1,y0,b`, latches each voice's staged depth only when its LFO falls to
`<= 7`, interpolates its six two-tap delay reads with `mac` and `macr`, and runs
two 48-bit output biquads (`L:0x06E` and `L:0x072`) with `asl` and `addl`. In
the modulation delay routine every product is at most 2^47, doubled, and no
chain adds more than four of them, so no sum reaches 2^49 and its accumulator
never wraps either; only its `macr` and 24-bit store points have to follow the
firmware. The pitch routine holds each voice's crossfade weight in `[-1.0, 0.0]`
so `move a,x1` at `P:0x000D3F` pins it at `-1.0` and sets the sticky `L` flag
that `blc >$000d55` tests to trigger a splice jump by the delayed pitch period
`Y:0x0209` / `Y:0x020E`, while `P:0x000DC7` and `P:0x000DE9` use parallel `b,a`
on `mac` to copy the unslewed delay travel into `a` before adding `0.1 *
X:0x0567` to `b`.

The pitch kernel's sums stay inside the accumulator, so its chains are single
`int64` sums that never wrap: one voice's blend under 2^49, one fine-search score
under 2^51, the detector filter under 2^50, and a lag of its coarse difference
function under 2^21 (the capture scales a sample by 2^20, so a template word is a
sample's eighth), which leaves the 64-word energy under 2^49. Only the roundings
and the 24-bit stores follow the firmware there.

Coefficient setup uses the firmware software float:

```text
value = mantissa * 2^(exponent - 150)
```

The mantissa is a signed normalized 24-bit value. The native helpers preserve
normalization, half-to-even rounding, exponent rails, and the firmware's
transcendental approximations rather than using host `libm` results.

## Other verified discovery anchors

The firmware's UI strings and task tables can identify effects without guessing
from MAC patterns:

- `X:0x0988CE`, `X:0x0989B4`, `X:0x098948`, `X:0x0987EA`, `X:0x098A8D`,
  `X:0x098B7A`, `X:0x098C7F`, `X:0x098E7D` and `X:0x098FF4` are the
  Tremolo/Panner, wah, resonance-filter, pitch, phaser, univibe,
  chorus/flanger/vibrato, delay and reverb objects (see their maps).
- `P:0x000497` belongs to the background/tuner path, not the three-band EQ.
- `X:(0x0985EF + algorithm)` points at each algorithm's record in P: word +1 is
  the parameter count, +2 the block dispatcher, +7 the parameter descriptors.
  Each descriptor is 15 words: the name in 10, the minimum at +10, the maximum at
  +11, the format word at +12, the factory default at +13 and the update callback
  at +14. Only `P:0x094509` reads the default, for the Delay block.

The direct-call CSV omits continuation and indirect calls. Use initialized Y task
records, UI object links, and actual emulator traces when mapping new effects.

## Native API and limits

`gm2_context` carries one instance's sample rate and `gm2_context_init()` sets
it: 0 on success, -1 for a NULL context or a rate outside 1..48000
(`GM2_SAMPLE_RATE_MAX`), because the effects' buffers have the device's sizes and
hold their longest delays only up to 48 kHz; the firmware's own rate routines
`P:0x000BFD` and `P:0x082DC0` accept up to `0x7FFFFF`. A rejected rate leaves the
context without a rate. The library keeps no global state, so instances at
different rates coexist.

Every effect has one shape:

```c
int gm2_<effect>_setup_config(const gm2_context *context, const gm2_<effect>_setup_raw *raw,
                              gm2_<effect>_config *config);
void gm2_<effect>_state_init(gm2_<effect>_state *state, const gm2_<effect>_config *config);
void gm2_<effect>_process_split(...);
void gm2_<effect>_process_f32(gm2_<effect>_state *state, const gm2_<effect>_config *config,
                              const float *in_l, const float *in_r, float *out_l, float *out_r,
                              size_t frames);
void gm2_<effect>_process_interleaved(...);
void gm2_<effect>_process_frame(gm2_<effect>_state *state, const gm2_<effect>_config *config,
                                const int32_t in[2], int32_t out[2]);
```

Setup takes the raw knob words of one preset and returns 0, or -1 for a NULL
pointer or a context without a rate. Every raw value is forced into the
descriptor windows on the way in, as `P:0x0832CB` clamps them, so a setup never
has to check and the ratio and time table lookups stay in range. The host's beat
length goes through `gm2_lfo_beat()`: a NaN takes the factory Tap Time of 500
ms and every other value is forced into 100..3000 ms, so the tempo effects never
refuse a beat.

Knob changes are implicit. The config is passed by const pointer to every
process call and each effect notices a changed config itself, so a caller that
edits the config between any two frames gets the device's behaviour: words the
firmware writes at once change from the next frame, ramps arm where the firmware
arms them, and the live words glide toward their targets after each four-frame
chunk. A state started by `state_init()` is the effect at rest after a long silence, settled on
its config, and its position inside the four-frame chunk is part of the state,
so any call length gives the same output as one call.

The float wrappers are the same kernel behind planar 32-bit float buffers: they
convert through `gm2_f32_to_q23`/`gm2_q23_to_f32` in 256-frame chunks, allow each
output to be its channel's input buffer, and allocate nothing. The conversion
path (AVX-512, AVX2, SSE2, NEON, RVV or scalar) is chosen when the library is
compiled.

Which words a knob turn moves, briefly:

| Effect | A turn of a knob |
|---|---|
| EQ | each band's five coefficient words glide, `P:0x00026F`, step `0x008371` |
| Gate | Mode, Threshold and Damping ride `P:0x0001B9` ramps armed on the turn, a Threshold turn re-arming Damping; the four Release words glide on one `P:0x0001FD` job |
| Compressor | Threshold, Ratio and Gain ride ramps that any of the three re-arms; Attack, Release and the detector word glide |
| Tremolo, Panner | pulse, step, rise and fall ride ramps armed by a Speed, Tempo or Pwidth turn, depth one armed by a Depth or Width turn; the Hi-Cut words only glide and the lag is fixed |
| Wahs | the staged frequency and LFO step glide |
| Resonance filters | the twenty coefficient words glide as one group |
| Pitch | the ratio pair (step `0x020DC6`), the feedback pair and the pan/level quad glide |
| Phasers | the LFO step and depth on `P:0x0001B9`, the other eighteen words on `P:0x0001FD` |
| Univibes | Depth on `P:0x0001B9`, the other seventeen words on `P:0x0001FD` |
| Choruses, Flangers, Vibrato | five jobs glide thirty words and six tap crossfaders move the read taps |
| Delays | the twenty matrix and filter words glide, and a moved tap offset starts a crossfade |
| Reverb | eighty words glide in six jobs; tap offsets, tank signs and early tap weights land at once |

Each LFO effect also has `gm2_<effect>_align(state, config, beats)`, which sets
the LFO phase from a song position in quarter notes; the firmware has no such
word, so alignment is the plugins' addition and changes nothing with Tempo at
Ignore. The touch effects and the delays answer the control tick: every
`gm2_tick_chunks()` chunks the Touch Wah, Auto Wah, Touch Res Filter and Dynamic
Delay tick, and Reverse Delay runs its grain splice every
`gm2_tick_period(rate, delay_ms)` chunks.

`state_init()` takes the config because the rest position depends on it: the
Touch Wah where a silent tick leaves its sweep, the Auto Wah at its LFO's start,
the others on their settled words. The Touch Res Filter also runs the Noise
Gate's detector on its own input, and the Dynamic Delay does the same for its
ducking tick.

The four delays settle their buffer and filters at zero, both taps on the
configured delay offset with tap 1 audible and the crossfade on its negative
rail (for Reverse Delay both taps rest at offset 0 with `rev_step = 4`, and each
splice moves the silent tap to 1 or 3, whichever the audible one is not), and
Dynamic Delay settles the detector empty and the ducking state and `coef[8..9]`
on the silent rest fixed point of `P:0x0944BE`. The Reverb zeroes its
32,768-word circular buffer and filter states and settles its 80 live
coefficients on the configured words; the tap offsets, tank signs and early tap
weights are config words it reads directly, like a changed config's other words.
The phasers, univibes and the nine chorus, flanger and Vibrato effects zero
their filter states and delay buffers and settle their live coefficients on the
configured words, the last of them including the six voice depths and the six
tap crossfaders. The five Pitch effects initialize the 20,000-word audio buffer,
the 2,048-word detector buffer, the 256-word period ring and the AMDF detector
states (`P:0x082B31`), and settle the delay slewers, pitch steps and three
one-pole smoothers on the configured words.

The EQ is the one setup that can refuse a frequency in range: -1 for a band at
or above half the rate, where the chain takes the unported `P:0x095D2D` branch.
`gm2_convert_simd()` names the conversion path the library compiled.

The test system checks setups at 48000 Hz only; other rates are accepted but not
checked. The processing kernels accept any precomputed valid words. The
firmware's block layer is not ported (see `AGENTS.md`): block objects, routing,
on/off switching, and the blocks' kill flags (hidden parameter 15, including the
gate's damping kill path). Every kernel keeps its block's input gain and wet/dry
words at the values the block's activation leaves, `0x7FFFFF`, `0x7FFFFE` and 0,
and no API exposes them. The smoothing,
crossfade and level jobs run on the fixed schedule above, and bit-exactness is
established against the firmware emulator, not by analog hardware capture.
