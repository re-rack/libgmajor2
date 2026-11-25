# Reverse-engineering workspace

The repository holds neither the firmware nor the manual, nor any analysis output
that carries the firmware's bytes. `make ghidra-prepare` builds this workspace
locally, from TC Electronic's own downloads, into gitignored paths:

1. `fetch_firmware.py` downloads TC's G-Major 2 v1.02 firmware updater for Windows
   and the 2009 English user's manual from the Internet Archive's copies of TC's
   download server, checks their SHA-256, and cuts the DSP load records out of the
   updater's SysEx stream, checking each record's SHA-256.
2. `build_ghidra.py --prepare` writes the records as LE24 files for a Ghidra
   import by hand.
3. `build_workspace.py` disassembles the code records and writes the memory map,
   entry points, call graph and Ghidra seeds.
4. `build_ghidra.py` builds the analysed Ghidra project, once: an existing project
   is kept, since it may hold your own analysis.

| Path | Content |
|---|---|
| `downloads/` | `gmajortwo_swupdater-102.zip` and `G_Major_II_US.pdf`, the manual |
| `firmware/` | the load records and `manifest.csv` (below) |
| `asm/<region>.asm` | full disassembly of each code region, at its load address |
| `memory_map.csv` | one row per payload: region, stage, kind, load address, size, source file, notes |
| `entrypoints.csv` | reset, vector and region-start addresses with the evidence for each |
| `callgraph/direct_calls.csv` | every direct `jsr`/`bsr` call site and its target |
| `callgraph/functions.csv` | candidate targets with their incoming direct-call count |
| `callgraph/direct_calls.dot` | the same graph as Graphviz source (not rendered) |
| `seeds.csv` | region starts, entry points and direct call/branch targets that seed Ghidra's disassembly |
| `ghidra/GMajor2.gpr` | the Ghidra project |
| `ghidra_analysis/` | functions, call edges, memory blocks and key instructions exported from it |
| `../build/ghidra/` | the records as LE24 files, with `IMPORT.txt` |

`make ghidra-clean` removes `firmware/`, the disassembly, call graph, CSVs and
`build/ghidra/`; the next `make ghidra-prepare` rebuilds them from `downloads/`
without the network. It leaves `downloads/` and the Ghidra project with
`ghidra_analysis/`, which only building a project writes. To rebuild the project,
delete `ghidra/` and `ghidra_analysis/` yourself.

## When a download fails

If no copy of a file can be downloaded, `fetch_firmware.py` lists the missing files
with their size and SHA-256. Put them into `downloads/` under any name; a file
counts when its SHA-256 matches. In a terminal the script waits for Enter and
continues; otherwise run the make target again.

## Tools

- `python3`: steps 1 and 2.
- `dsp56300-disasm` from [xemu-project/dsp56300](https://github.com/xemu-project/dsp56300)
  (`DISASSEMBLER=` names another): step 3. It must accept
  `-f u24be --color never -s HEX -n COUNT FILE`.
- Ghidra 12.2 with the Ghidra-Rare DSP56300 extension from
  [aleph-void/ghidra-dsp56300](https://github.com/aleph-void/ghidra-dsp56300)
  (`GHIDRA=`, default `/opt/ghidra`), and a Python with PyGhidra (`PYGHIDRA=`,
  default `~/.local/share/gmajor2/pyghidra/bin/python`): step 4, which is skipped
  without them. uv installs PyGhidra from Ghidra's bundled offline package:

```sh
uv venv ~/.local/share/gmajor2/pyghidra
uv pip install --python ~/.local/share/gmajor2/pyghidra/bin/python --no-index \
  -f /opt/ghidra/Ghidra/Features/PyGhidra/pypkg/dist pyghidra
```

To build another project beside the kept one, run `build_ghidra.py` with a new
name from that Python: `~/.local/share/gmajor2/pyghidra/bin/python
reverse_engineering/build_ghidra.py GMajor2-fresh`.

## The load records

Each record holds one payload of the load stream the device's AVR bootloader feeds
to the DSP56362, as 24-bit big-endian words; its name gives its order, role, load
address and word count. There is no AVR firmware.

| File | Words | Load address | Content |
|---|---|---|---|
| `00_bootstrap_000000_152w.bin` | 152 | `0x000000` | DSP bootstrap code |
| `01_vectors_000000_47w.bin` | 47 | `0x000000` | interrupt vectors |
| `02_program_low_000030_5039w.bin` | 5 039 | `0x000030` | code |
| `03_config_080000_213w.bin` | 213 | `0x080000` | data |
| `04_vars_080112_766w.bin` | 766 | `0x080112` | data |
| `05a_data_080412_9872w.bin` | 9 872 | `0x080412` | data |
| `05b_code_082aa2_14795w.bin` | 14 795 | `0x082AA2` | code |
| `06_presets_088000_16701w.bin` | 16 701 | `0x088000` | data (presets) |
| `07_program_high_08ce20_44304w.bin` | 44 304 | `0x08CE20` | code |
| `08_init_098000_77w.bin` | 77 | `0x098000` | code |
| `09_terminator.bin` | 3 | – | end marker: `0x000000`, `0x000097`, `0x000004` |
| `10_filler.bin` | 8 | – | eight `0xA00000` filler words |

`05a` and `05b` are one load record whose content turns from data to code at
`0x082AA2`, which disassembly confirms. The updater sends the stream as TC
SysEx messages (`F0 00 20 1F 00 66 52`) whose payloads pack seven bytes into eight:
the bootstrap as {count, address, words}, every other record as {count, address,
flags, words}, then the terminator, a header with count 0 and the start address,
then the filler.

## Addresses

Every address is the numeric load address taken from `manifest.csv`, printed as
`0x%06x`, and used as one flat analysis space. Which DSP space (P, X or Y) each
record loads into is not established, so no such mapping is claimed here; P/X/Y
in the disassembly output is the disassembler's default choice. Keep that in mind
before trusting a data access through a program-space pointer.

Cells that need several values join them with `;`: one address can be a region
start and an entry point at the same time, and `0x000000` is loaded twice (see
the overlay below). `seeds.csv` truncates long `source` lists to three entries
plus `+N_more`; `callgraph/direct_calls.csv` has every call site. In
`callgraph/functions.csv` the caller count is the number of direct call sites and
`first_caller` is the lowest caller address.

## Byte order

The records are big-endian 24-bit words; the Ghidra DSP56300 language reads
little-endian 24-bit words. `build_ghidra.py` byte-reverses every word in memory
and checks each record's length against `manifest.csv`. For an import by hand,
`build/ghidra/IMPORT.txt` lists each LE24 file's program, block and byte address
(three per word); then load `seeds.csv` as the disassembly and function seed set.
Importing a region at zero silently produces wrong cross references, because the
code is relocated by the loader.

## Overlay at address 0

The bootstrap (152 words at `0x000000`) runs first. The application then loads
its vector table over `0x000000` and its low code from `0x000030`, so
`0x000030..0x000097` holds different instructions in the two stages. The `stage`
column separates them, and `0x000097` is the application reset reached through the
vector table. Never import both stages into one Ghidra program at the same
addresses; the project keeps them as two programs.

## Call graph limits

`callgraph/` is a candidate direct-call graph, not a proven call graph:

- only `jsr` and `bsr` with an explicit address operand are recorded; `jsr (rN)`
  and `bsr (rN)` register calls, `jmp` tail calls and computed branches are not
  resolved, and parameters moved in registers say nothing about targets;
- code inside data (inline tables, jump tables) is not analysed;
- function boundaries are unknown: `functions.csv` counts callers of an address,
  which is a hint, not a confirmed entry point;
- a target counts as `loaded` only when it falls inside a code interval of the
  caller's own stage; otherwise it is marked `external`, which means either code
  with no payload in this load stream or an address that is never loaded.

## The Ghidra project

`ghidra/GMajor2.gpr` holds two programs, imported as `gm2_application.le24.bin`
and `gm2_bootstrap.le24.bin`, names that say which byte order Ghidra reads:

- `gm2_application`: nine address-loaded blocks, 36 222 instructions and
  601 functions. Data blocks are marked non-executable; the application reset is
  `0x000097`, and cross-region calls such as `0x082AE4 → 0x082AA2` resolve.
- `gm2_bootstrap`: a separate address-zero overlay with 94 of the 97
  reference-disassembly instructions and 3 functions.

These counts describe analysed coverage, not the whole firmware: indirect calls,
jump tables and embedded data are not fully resolved, and `callgraph/` remains the
complete list of direct `jsr`/`bsr` call sites. The project is built with PyGhidra
alone, without Java scripts or `analyzeHeadless`.
