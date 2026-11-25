#!/usr/bin/env python3
"""Build a portable reverse-engineering workspace from the firmware's load records.

Reads `manifest.csv` and the ten payload files of the DSP load stream
and writes, under the output directory: address-correct disassembly of every
code payload, a memory map, entry points, a candidate direct-call graph and
Ghidra seeds.
"""

import argparse
import csv
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import NamedTuple

WORD = 3
DEFAULT_INPUT = Path("reverse_engineering/firmware")
DEFAULT_OUTPUT = Path("reverse_engineering")
DEFAULT_DISASSEMBLER = "dsp56300-disasm"
DISASSEMBLER_TIMEOUT = 600

# The load-stream address is used as a plain 24-bit number so that code and data
# share one analysis address space. It is not a proven P/X/Y space mapping.
ADDRESS_CAVEAT = (
    "numeric load-stream address used as a flat analysis address; "
    "P/X/Y space mapping not claimed"
)

# region name, stage, source payload, notes. Words, bytes, kind and the address
# itself come from manifest.csv; the region splits the load stream by role.
REGION_SPECS = (
    (
        "bootstrap",
        "bootstrap",
        "00_bootstrap_000000_152w.bin",
        (
            "overlay: loads first at 0x000000 and is overwritten later by the "
            "application vectors (0x000000..0x00002e) and the start of program_low "
            "(0x000030..0x000097); jumps to 0x000006 and to the host loop at 0x000091"
        ),
    ),
    (
        "vectors",
        "application",
        "01_vectors_000000_47w.bin",
        (
            "overlay: interrupt vector table at 0x000000, overwrites bootstrap; "
            "jmp/jsr to 0x000097 and to 0x082e3b"
        ),
    ),
    (
        "program_low",
        "application",
        "02_program_low_000030_5039w.bin",
        (
            "code following the vector table; overwrites bootstrap code at "
            "0x000030..0x000097 at application time"
        ),
    ),
    (
        "config",
        "application",
        "03_config_080000_213w.bin",
        "data block, not disassembled",
    ),
    (
        "vars",
        "application",
        "04_vars_080112_766w.bin",
        "data block, not disassembled",
    ),
    (
        "data",
        "application",
        "05a_data_080412_9872w.bin",
        (
            "first half of the mixed record; the words stop decoding as "
            "instructions before 0x082aa2"
        ),
    ),
    (
        "middle_code",
        "application",
        "05b_code_082aa2_14795w.bin",
        "second half of the mixed record, disassembles cleanly from 0x082aa2",
    ),
    (
        "presets",
        "application",
        "06_presets_088000_16701w.bin",
        "data block (preset tables), not disassembled",
    ),
    (
        "program_high",
        "application",
        "07_program_high_08ce20_44304w.bin",
        "largest code block",
    ),
    (
        "init",
        "application",
        "08_init_098000_77w.bin",
        "init block after a gap from 0x097b30",
    ),
)

# name, address, stage, region, kind, evidence
ENTRYPOINT_SPECS = (
    (
        "bootstrap_start",
        0x000000,
        "bootstrap",
        "bootstrap",
        "region_start",
        "start of the 152-word bootstrap payload",
    ),
    (
        "bootstrap_reset",
        0x000006,
        "bootstrap",
        "bootstrap",
        "reset",
        "target of `jmp >$0006` at P:$000000",
    ),
    (
        "bootstrap_host_loop",
        0x000091,
        "bootstrap",
        "bootstrap",
        "host_loop",
        "target of `jmp >$0091` at P:$000002 and `jsr >$0091` at P:$000004",
    ),
    (
        "application_vectors_start",
        0x000000,
        "application",
        "vectors",
        "region_start",
        "start of the 47-word interrupt vector table, loaded over the bootstrap",
    ),
    (
        "program_low_start",
        0x000030,
        "application",
        "program_low",
        "region_start",
        "first word after the vector table",
    ),
    (
        "application_reset",
        0x000097,
        "application",
        "program_low",
        "reset",
        (
            "target of `jmp >$0097` at P:$000000 in the vectors; the same value is "
            "the middle word of the 09_terminator record, which appears to carry "
            "the entry point"
        ),
    ),
    (
        "middle_code_start",
        0x082AA2,
        "application",
        "middle_code",
        "region_start",
        "first instruction is `move #$080000,r2`",
    ),
    (
        "application_vector_82e3b",
        0x082E3B,
        "application",
        "middle_code",
        "vector",
        (
            "target of `jmp $82e3b` at P:$000002 and `jsr $82e3b` at P:$000004 in "
            "the vectors"
        ),
    ),
    (
        "program_high_start",
        0x08CE20,
        "application",
        "program_high",
        "region_start",
        "first instruction is `movec ssh,x:(r7)+`",
    ),
    (
        "init_start",
        0x098000,
        "application",
        "init",
        "region_start",
        "first instruction is `move #>$0000ce,a`",
    ),
)

# Edges that must appear if the call-site parsing is right: (caller, target, mnemonic).
KNOWN_EDGES = (
    (0x82AE4, 0x82AA2, "jsr"),
    (0x82AE6, 0x82AF0, "jsr"),
)

CALL_MNEMONICS = ("jsr", "bsr")
BRANCH_MNEMONICS = {
    "bcc",
    "bcs",
    "beq",
    "bes",
    "bge",
    "bgt",
    "blc",
    "ble",
    "blt",
    "bmi",
    "bne",
    "bpl",
    "bra",
    "brclr",
    "brset",
    "bseq",
    "bsge",
    "bsne",
    "bsset",
    "jclr",
    "jcs",
    "jeq",
    "jge",
    "jgt",
    "jle",
    "jlt",
    "jmi",
    "jmp",
    "jne",
    "jsclr",
    "jseq",
    "jset",
    "jslt",
    "jsne",
    "jsset",
}

# P:$082AA2  62F400 080000  move #$080000,r2
INSTRUCTION = re.compile(r"^P:\$([0-9a-fA-F]{6,8})\s+[0-9a-fA-F]{6}\s+(.+)$")
HEX_WORD = re.compile(r"^[0-9a-fA-F]{6}$")
DIRECT_OPERAND = re.compile(r"^>?\$([0-9a-fA-F]+)$")
DIRECT_TARGET_AT_END = re.compile(r"(?:^|[,\s])>?\$([0-9a-fA-F]+)$")


class Payload(NamedTuple):
    filename: str
    address: int
    words: int
    size: int
    kind: str


class Region(NamedTuple):
    name: str
    stage: str
    kind: str
    address: int
    words: int
    source_file: str
    notes: str

    @property
    def end(self) -> int:
        return self.address + self.words - 1

    def contains(self, address: int) -> bool:
        return self.address <= address <= self.end


class Insn(NamedTuple):
    address: int
    mnemonic: str
    operand: str


class Edge(NamedTuple):
    caller: int
    caller_region: str
    target: int
    target_region: str
    mnemonic: str
    target_status: str


class Entry(NamedTuple):
    name: str
    address: int
    stage: str
    region: str
    kind: str
    evidence: str


def hex_address(address: int) -> str:
    return f"0x{address:06x}"


def read_manifest(path: Path) -> dict[str, Payload]:
    with path.open(newline="") as fh:
        rows = list(csv.DictReader(fh))
    payloads: dict[str, Payload] = {}
    for row in rows:
        if row["kind"] in ("terminator", "filler"):
            continue
        if not row["load_address"]:
            raise SystemExit(f"{row['filename']}: payload without a load address")
        payloads[row["filename"]] = Payload(
            filename=row["filename"],
            address=int(row["load_address"], 0),
            words=int(row["words"]),
            size=int(row["bytes"]),
            kind=row["kind"],
        )
    return payloads


def build_regions(manifest: dict[str, Payload]) -> list[Region]:
    wanted = {spec[2] for spec in REGION_SPECS}
    missing = wanted - set(manifest)
    extra = set(manifest) - wanted
    if missing or extra:
        raise SystemExit(f"manifest/region mismatch: missing {missing} extra {extra}")

    regions = []
    for name, stage, filename, note in REGION_SPECS:
        payload = manifest[filename]
        if payload.words * WORD != payload.size:
            raise SystemExit(
                f"{filename}: {payload.words} words is not {payload.size} bytes"
            )
        regions.append(
            Region(
                name=name,
                stage=stage,
                kind=payload.kind,
                address=payload.address,
                words=payload.words,
                source_file=filename,
                notes=f"{note}; {ADDRESS_CAVEAT}",
            )
        )
    return regions


def check_disjoint_code(regions: list[Region]) -> None:
    # Loaded/external classification is only unambiguous if code regions of one
    # stage never share an address.
    code = [r for r in regions if r.kind == "code"]
    for index, left in enumerate(code):
        for right in code[index + 1 :]:
            if left.stage != right.stage:
                continue
            if left.address <= right.end and right.address <= left.end:
                raise SystemExit(
                    f"{left.name} and {right.name} overlap in stage {left.stage}"
                )


def resolve_disassembler(value: str) -> str:
    if "/" in value:
        path = Path(value).expanduser()
        if not path.is_file():
            raise SystemExit(f"disassembler not found: {path}")
        return str(path.resolve())
    found = shutil.which(value)
    if found is None:
        raise SystemExit(
            f"disassembler {value!r} not on PATH: install dsp56300-disasm from "
            "https://github.com/xemu-project/dsp56300 or pass --disassembler "
            "(DISASSEMBLER= for make)"
        )
    return found


def disassemble_all(
    exe: str, indir: Path, outdir: Path, regions: list[Region]
) -> dict[str, str]:
    assembly: dict[str, str] = {}
    for region in regions:
        if region.kind != "code":
            continue
        source = indir / region.source_file
        done = subprocess.run(
            [
                exe,
                "-f",
                "u24be",
                "--color",
                "never",
                "-s",
                f"{region.address:x}",
                str(source.resolve()),
            ],
            capture_output=True,
            text=True,
            timeout=DISASSEMBLER_TIMEOUT,
            check=False,
        )
        if done.returncode != 0:
            raise SystemExit(f"disassembler failed on {source.name}: {done.stderr}")
        if not done.stdout.strip():
            raise SystemExit(f"disassembler produced no output for {source.name}")
        (outdir / "asm" / f"{region.name}.asm").write_text(done.stdout)
        assembly[region.name] = done.stdout
    return assembly


def parse_assembly(text: str) -> tuple[list[Insn], int]:
    instructions: list[Insn] = []
    unparsed = 0
    for line in text.splitlines():
        if not line.strip():
            continue
        match = INSTRUCTION.match(line)
        if match is None:
            unparsed += 1
            continue
        tokens = match.group(2).split()
        if len(tokens) > 1 and HEX_WORD.match(tokens[0]):
            tokens = tokens[1:]
        instructions.append(
            Insn(
                address=int(match.group(1), 16),
                mnemonic=tokens[0].lower(),
                operand=" ".join(tokens[1:]),
            )
        )
    return instructions, unparsed


def find_code_region(regions: list[Region], address: int, stage: str) -> Region | None:
    for region in regions:
        if region.kind == "code" and region.stage == stage and region.contains(address):
            return region
    return None


def build_callgraph(
    regions: list[Region], instructions: dict[str, list[Insn]]
) -> tuple[list[Edge], int]:
    edges: list[Edge] = []
    indirect = 0
    for region in regions:
        for insn in instructions.get(region.name, ()):
            if insn.mnemonic not in CALL_MNEMONICS:
                continue
            match = DIRECT_OPERAND.match(insn.operand)
            if match is None:
                indirect += 1
                continue
            target = int(match.group(1), 16)
            hit = find_code_region(regions, target, region.stage)
            edges.append(
                Edge(
                    caller=insn.address,
                    caller_region=region.name,
                    target=target,
                    target_region=hit.name if hit else "",
                    mnemonic=insn.mnemonic,
                    target_status="loaded" if hit else "external",
                )
            )
    return edges, indirect


def build_functions(edges: list[Edge]) -> list[tuple[int, str, int, int]]:
    candidates: dict[int, list[int]] = {}
    regions_of: dict[int, str] = {}
    for edge in edges:
        if edge.target_status != "loaded":
            continue
        candidates.setdefault(edge.target, []).append(edge.caller)
        regions_of[edge.target] = edge.target_region
    return [
        (target, regions_of[target], len(callers), min(callers))
        for target, callers in sorted(candidates.items())
    ]


def build_seeds(
    regions: list[Region],
    entries: list[Entry],
    edges: list[Edge],
    instructions: dict[str, list[Insn]],
):
    seeds: dict[int, dict[str, list[str]]] = {}
    stage_of = {region.name: region.stage for region in regions}

    def add(address: int, stage: str, kind: str, source: str) -> None:
        entry = seeds.setdefault(address, {"stage": [], "kind": [], "source": []})
        for key, value in (("stage", stage), ("kind", kind), ("source", source)):
            if value not in entry[key]:
                entry[key].append(value)

    for region in regions:
        if region.kind == "code":
            add(region.address, region.stage, "region_start", region.name)
    for item in entries:
        add(item.address, item.stage, item.kind, item.name)
    for edge in edges:
        if edge.target_status == "loaded":
            add(
                edge.target,
                stage_of[edge.caller_region],
                "call_target",
                f"{edge.mnemonic}_from_{hex_address(edge.caller)}",
            )
    for region in regions:
        for insn in instructions.get(region.name, ()):
            if insn.mnemonic not in BRANCH_MNEMONICS:
                continue
            match = DIRECT_TARGET_AT_END.search(insn.operand)
            if match is None:
                continue
            target = int(match.group(1), 16)
            if find_code_region(regions, target, region.stage):
                add(
                    target,
                    region.stage,
                    "branch_target",
                    f"{insn.mnemonic}_from_{hex_address(insn.address)}",
                )
    return seeds


def build_entries(regions: list[Region]) -> list[Entry]:
    entries = [
        Entry(
            name=name,
            address=address,
            stage=stage,
            region=region,
            kind=kind,
            evidence=evidence,
        )
        for name, address, stage, region, kind, evidence in ENTRYPOINT_SPECS
    ]
    by_name = {region.name: region for region in regions}
    for entry in entries:
        region = by_name[entry.region]
        if region.stage != entry.stage or not region.contains(entry.address):
            raise SystemExit(
                f"entry point {entry.name} at {hex_address(entry.address)} "
                f"is not inside {entry.region}"
            )
    order = {"bootstrap": 0, "application": 1}
    return sorted(entries, key=lambda e: (order[e.stage], e.address))


def write_memory_map(outdir: Path, regions: list[Region]) -> None:
    with (outdir / "memory_map.csv").open("w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(
            [
                "region",
                "stage",
                "kind",
                "load_address",
                "words",
                "bytes",
                "source_file",
                "notes",
            ]
        )
        for region in regions:
            writer.writerow(
                [
                    region.name,
                    region.stage,
                    region.kind,
                    hex_address(region.address),
                    region.words,
                    region.words * WORD,
                    region.source_file,
                    region.notes,
                ]
            )


def write_entrypoints(outdir: Path, entries: list[Entry]) -> None:
    with (outdir / "entrypoints.csv").open("w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(["name", "address", "stage", "region", "kind", "evidence"])
        for entry in entries:
            writer.writerow(
                [
                    entry.name,
                    hex_address(entry.address),
                    entry.stage,
                    entry.region,
                    entry.kind,
                    entry.evidence,
                ]
            )


def write_callgraph(
    outdir: Path,
    edges: list[Edge],
    functions: list[tuple[int, str, int, int]],
    entries: list[Entry],
) -> None:
    graph = outdir / "callgraph"
    graph.mkdir(parents=True, exist_ok=True)

    with (graph / "direct_calls.csv").open("w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(
            [
                "caller",
                "caller_region",
                "target",
                "target_region",
                "mnemonic",
                "target_status",
            ]
        )
        for edge in edges:
            writer.writerow(
                [
                    hex_address(edge.caller),
                    edge.caller_region,
                    hex_address(edge.target),
                    edge.target_region,
                    edge.mnemonic,
                    edge.target_status,
                ]
            )

    with (graph / "functions.csv").open("w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(["target", "region", "incoming_direct_calls", "first_caller"])
        for target, region, count, first in functions:
            writer.writerow([hex_address(target), region, count, hex_address(first)])

    labels: dict[int, list[str]] = {}
    for entry in entries:
        labels.setdefault(entry.address, []).append(entry.name)

    nodes = (
        {edge.caller for edge in edges} | {edge.target for edge in edges} | set(labels)
    )
    external = {edge.target for edge in edges if edge.target_status != "loaded"}
    name_of = {edge.target: edge.target_region for edge in edges}

    lines = [
        "digraph direct_calls {",
        '  rankdir="LR";',
        '  node [shape="box", fontname="monospace", fontsize=9];',
    ]
    for address in sorted(nodes):
        text = hex_address(address)
        if address in labels:
            text += ":" + ",".join(labels[address])
        elif name_of.get(address):
            text += ":" + name_of[address]
        attrs = ['label="' + text.replace('"', "'") + '"']
        if address in external:
            attrs += ['style="dashed"', 'color="gray40"', 'fontcolor="gray40"']
        lines.append(f'  "{hex_address(address)}" [{", ".join(attrs)}];')
    for edge in edges:
        lines.append(
            f'  "{hex_address(edge.caller)}" -> "{hex_address(edge.target)}"'
            f' [label="{edge.mnemonic}"];'
        )
    lines.append("}")
    (graph / "direct_calls.dot").write_text("\n".join(lines) + "\n")


def join_sources(sources: list[str]) -> str:
    # Hot targets can have dozens of call sites; the full list is in direct_calls.csv.
    if len(sources) <= 3:
        return ";".join(sources)
    return ";".join(sources[:3]) + f";+{len(sources) - 3}_more"


def write_seeds(outdir: Path, seeds: dict[int, dict[str, list[str]]]) -> None:
    with (outdir / "seeds.csv").open("w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(["address", "stage", "kind", "source"])
        for address in sorted(seeds):
            entry = seeds[address]
            writer.writerow(
                [
                    hex_address(address),
                    ";".join(entry["stage"]),
                    ";".join(entry["kind"]),
                    join_sources(entry["source"]),
                ]
            )


def report(
    regions: list[Region],
    instructions: dict[str, list[Insn]],
    unparsed: int,
    edges: list[Edge],
    indirect: int,
    functions: list[tuple[int, str, int, int]],
    seeds: dict[int, dict[str, list[str]]],
    entries: list[Entry],
) -> None:
    print(f"payloads mapped: {len(regions)}")
    total = 0
    for region in regions:
        count = len(instructions.get(region.name, ()))
        total += count
        if region.kind == "code":
            print(
                f"  {region.name:12s} {hex_address(region.address)}.."
                f"{hex_address(region.end)} {region.words:6d}w {count:6d} instr"
            )
        else:
            print(
                f"  {region.name:12s} {hex_address(region.address)}.."
                f"{hex_address(region.end)} {region.words:6d}w data"
            )
    loaded = sum(1 for edge in edges if edge.target_status == "loaded")
    print(f"instructions parsed: {total} ({unparsed} lines not parsed)")
    print(
        f"direct call edges: {len(edges)} ({loaded} loaded, "
        f"{len(edges) - loaded} external), {indirect} indirect call sites skipped"
    )
    print(f"candidate functions: {len(functions)}")
    print(f"seed addresses: {len(seeds)}")
    print(f"entry points: {len(entries)}")


def run_checks(
    regions: list[Region],
    instructions: dict[str, list[Insn]],
    assembly: dict[str, str],
    edges: list[Edge],
    seeds: dict[int, dict[str, list[str]]],
    entries: list[Entry],
) -> None:
    present = {(edge.caller, edge.target, edge.mnemonic) for edge in edges}
    for caller, target, mnemonic in KNOWN_EDGES:
        if (caller, target, mnemonic) not in present:
            raise SystemExit(
                f"known edge {hex_address(caller)} -> {hex_address(target)} "
                f"({mnemonic}) is missing"
            )

    stage_of = {region.name: region.stage for region in regions}
    for edge in edges:
        hit = find_code_region(regions, edge.target, stage_of[edge.caller_region])
        wanted = hit.name if hit else ""
        if wanted != edge.target_region:
            raise SystemExit(
                f"{edge.caller_region}: target {hex_address(edge.target)} classified "
                f"{edge.target_region!r} but belongs to {wanted!r}"
            )

    reset = [entry for entry in entries if entry.address == 0x97]
    if not reset:
        raise SystemExit("0x000097 is not an entry point")
    if 0x97 not in seeds:
        raise SystemExit("0x000097 is not a seed")

    for entry in entries:
        addresses = {insn.address for insn in instructions[entry.region]}
        if entry.address not in addresses:
            raise SystemExit(
                f"{entry.name}: {hex_address(entry.address)} is not an instruction "
                f"start in asm/{entry.region}.asm"
            )

    for region in regions:
        if region.kind != "code":
            continue
        first = assembly[region.name].splitlines()[0]
        if not first.startswith(f"P:${region.address:06X} "):
            raise SystemExit(
                f"asm/{region.name}.asm starts at {first.split()[0]}, "
                f"expected P:${region.address:06X}"
            )

    print("checks: known edges present, targets in code regions,")
    print("        0x000097 is entry point and seed, entry points are instruction")
    print("        starts, every asm file starts at its load address")


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    ap.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    ap.add_argument("--disassembler", default=DEFAULT_DISASSEMBLER)
    return ap.parse_args()


def main() -> int:
    args = parse_args()
    exe = resolve_disassembler(args.disassembler)

    manifest = read_manifest(args.input / "manifest.csv")
    regions = build_regions(manifest)
    check_disjoint_code(regions)

    for subdir in ("asm", "callgraph"):
        (args.output / subdir).mkdir(parents=True, exist_ok=True)

    assembly = disassemble_all(exe, args.input, args.output, regions)

    instructions: dict[str, list[Insn]] = {}
    unparsed = 0
    for name, text in assembly.items():
        instructions[name], skipped = parse_assembly(text)
        unparsed += skipped

    edges, indirect = build_callgraph(regions, instructions)
    functions = build_functions(edges)
    entries = build_entries(regions)
    seeds = build_seeds(regions, entries, edges, instructions)

    write_memory_map(args.output, regions)
    write_entrypoints(args.output, entries)
    write_callgraph(args.output, edges, functions, entries)
    write_seeds(args.output, seeds)

    run_checks(regions, instructions, assembly, edges, seeds, entries)
    report(regions, instructions, unparsed, edges, indirect, functions, seeds, entries)
    print(f"written to {args.output}/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
