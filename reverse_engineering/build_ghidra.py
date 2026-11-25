#!/usr/bin/env python3
"""Build the G-Major 2 Ghidra project from the firmware's load records with PyGhidra.

Imports the bootstrap and the application overlay as two separate programs, adds
the workspace regions as memory blocks, seeds disassembly from `seeds.csv`,
creates entry points from `entrypoints.csv`, runs Ghidra analysis and exports the
function and call tables to `ghidra_analysis/`. Standalone PyGhidra only: no
Java scripts and no analyzeHeadless.

With `--prepare DIR` it only writes the same programs' records as LE24 files and
a list of how to import them by hand in the Ghidra GUI, and needs no PyGhidra.
"""

import argparse
import csv
import sys
import time
from pathlib import Path
from typing import NamedTuple

RE_ROOT = Path(__file__).resolve().parent
# reverse_engineering/ghidra is the project directory, and PyGhidra only drops the
# working directory from sys.path. With this file's directory on the path, the import
# system resolves `ghidra` to that directory and startup dies on import recursion.
for _entry in list(sys.path):
    if Path(_entry or ".").resolve() == RE_ROOT:
        sys.path.remove(_entry)

PROJECT_DIR = RE_ROOT / "ghidra"
ANALYSIS_DIR = RE_ROOT / "ghidra_analysis"
SEEDS = RE_ROOT / "seeds.csv"
ENTRYPOINTS = RE_ROOT / "entrypoints.csv"
DEFAULT_FIRMWARE = RE_ROOT / "firmware"
WORD = 3

LOADER = "BinaryLoader"
LANGUAGE = "DSP56300:LE:24:default"
JDK_OPTIONS = ("--enable-final-field-mutation=ALL-UNNAMED",)
DEFAULT_PROJECT = "GMajor2"
DEFAULT_GHIDRA = Path("/opt/ghidra")
DEFAULT_ANALYSIS_TIMEOUT = 900

# Instructions reported per program in _info.txt.
KEY_ADDRESSES = (0x0, 0x6, 0x97, 0x82AA2, 0x82AE4, 0x8CE20, 0x98000)
# Cross-region call site 0x082AE4 -> 0x082AA2, reported as a reference check.
CROSS_REGION_TARGET = 0x82AA2


class Region(NamedTuple):
    name: str
    record: str
    address: int
    code: bool


class Spec(NamedTuple):
    stage: str
    record: str
    initial_block: str
    regions: tuple[Region, ...]

    @property
    def name(self) -> str:
        return f"gm2_{self.stage}"

    @property
    def import_name(self) -> str:
        # The imported bytes are LE24 whatever the record's byte order.
        return f"{self.name}.le24.bin"

    @property
    def project_path(self) -> str:
        return "/" + self.import_name


PROGRAM_SPECS = (
    Spec(
        stage="bootstrap",
        record="00_bootstrap_000000_152w.bin",
        initial_block="bootstrap",
        regions=(),
    ),
    Spec(
        stage="application",
        record="01_vectors_000000_47w.bin",
        initial_block="vectors",
        regions=(
            Region("program_low", "02_program_low_000030_5039w.bin", 0x30, True),
            Region("config", "03_config_080000_213w.bin", 0x80000, False),
            Region("vars", "04_vars_080112_766w.bin", 0x80112, False),
            Region("data_080412", "05a_data_080412_9872w.bin", 0x80412, False),
            Region("code_082aa2", "05b_code_082aa2_14795w.bin", 0x82AA2, True),
            Region("presets", "06_presets_088000_16701w.bin", 0x88000, False),
            Region("program_high", "07_program_high_08ce20_44304w.bin", 0x8CE20, True),
            Region("init", "08_init_098000_77w.bin", 0x98000, True),
        ),
    ),
)


def read_manifest(firmware: Path) -> dict[str, int]:
    """Record filename -> byte count."""
    manifest = firmware / "manifest.csv"
    if not manifest.is_file():
        raise SystemExit(f"missing {manifest}; run make ghidra-prepare")
    with manifest.open(newline="") as fh:
        return {row["filename"]: int(row["bytes"]) for row in csv.DictReader(fh)}


def read_record(firmware: Path, sizes: dict[str, int], filename: str) -> bytes:
    """One firmware record, its big-endian words byte-reversed for Ghidra's LE24."""
    path = firmware / filename
    if not path.is_file():
        raise SystemExit(f"missing firmware record {path}")
    blob = path.read_bytes()
    if filename not in sizes:
        raise SystemExit(f"{filename}: not in {firmware / 'manifest.csv'}")
    if len(blob) != sizes[filename]:
        raise SystemExit(f"{filename}: {len(blob)} bytes, manifest {sizes[filename]}")
    if len(blob) % WORD:
        raise SystemExit(
            f"{filename}: {len(blob)} bytes is not whole {WORD}-byte words"
        )
    return b"".join(blob[i : i + WORD][::-1] for i in range(0, len(blob), WORD))


def read_seeds() -> dict[str, list[int]]:
    """Word addresses per stage, in file order and without repeats."""
    seeds: dict[str, list[int]] = {spec.stage: [] for spec in PROGRAM_SPECS}
    seen: dict[str, set[int]] = {spec.stage: set() for spec in PROGRAM_SPECS}
    with SEEDS.open(newline="") as fh:
        rows = csv.reader(fh)
        next(rows, None)
        for row in rows:
            if len(row) < 2:
                continue
            address = int(row[0], 16)
            for stage in row[1].split(";"):
                if stage in seeds and address not in seen[stage]:
                    seen[stage].add(address)
                    seeds[stage].append(address)
    return seeds


def read_entrypoints() -> dict[str, list[tuple[str, int]]]:
    """(name, word address) per stage, in file order."""
    entries: dict[str, list[tuple[str, int]]] = {}
    with ENTRYPOINTS.open(newline="") as fh:
        for row in csv.DictReader(fh):
            for stage in row["stage"].split(";"):
                entries.setdefault(stage, []).append(
                    (row["name"], int(row["address"], 16))
                )
    return entries


def setup_program(
    program, spec: Spec, records: dict[str, bytes], seeds: list[int], entries
) -> int:
    import jpype
    import pyghidra
    from ghidra.program.flatapi import FlatProgramAPI
    from ghidra.program.model.symbol import SourceType
    from java.io import ByteArrayInputStream

    default_space = program.getAddressFactory().getDefaultAddressSpace()

    def word(address: int):
        # Ghidra addresses are byte offsets; workspace addresses are DSP words.
        return default_space.getAddress(address * 3)

    memory = program.getMemory()
    listing = program.getListing()
    monitor = pyghidra.task_monitor()

    with pyghidra.transaction(program, f"Set up {spec.name}"):
        program.setName(spec.name)

        blocks = memory.getBlocks()
        if not blocks:
            raise SystemExit(f"{spec.name}: the import produced no memory block")
        initial = blocks[0]
        initial.setName(spec.initial_block)
        initial.setExecute(True)
        initial.setComment(f"DSP56300 {spec.stage} region; 24-bit little-endian import")

        api = FlatProgramAPI(program)
        for region in spec.regions:
            data = records[region.record]
            words = jpype.JArray(jpype.JByte)(data)
            block = memory.createInitializedBlock(
                region.name,
                word(region.address),
                ByteArrayInputStream(words),
                len(data),
                monitor,
                False,
            )
            block.setExecute(region.code)
            block.setRead(True)
            block.setWrite(True)
            block.setComment(f"G-Major 2 load-stream region {region.name}")
            api.createLabel(
                word(region.address), region.name, True, SourceType.USER_DEFINED
            )
            if region.code:
                api.disassemble(word(region.address))

        for address in seeds:
            api.disassemble(word(address))

        for name, address in entries:
            point = word(address)
            api.createLabel(point, name, True, SourceType.USER_DEFINED)
            api.addEntryPoint(point)
            api.disassemble(point)
            if listing.getInstructionAt(point) is not None:
                api.createFunction(point, name)

    return len(seeds)


def analyze_program(program, timeout: int) -> float:
    import pyghidra

    started = time.monotonic()
    pyghidra.analyze(program, pyghidra.task_monitor(timeout))
    elapsed = time.monotonic() - started
    if elapsed >= timeout:
        raise SystemExit(
            f"{program.getName()}: analysis hit the {timeout}s timeout and is "
            "incomplete; raise --analysis-timeout and rebuild"
        )
    return elapsed


def export_program(program, outdir: Path) -> Path:
    import pyghidra

    default_space = program.getAddressFactory().getDefaultAddressSpace()
    name = program.getName()
    monitor = pyghidra.task_monitor()
    listing = program.getListing()
    functions = program.getFunctionManager()
    edges = [
        (
            function,
            # Ghidra returns callees from an identity-hashed HashSet, so sort for a
            # reproducible export.
            sorted(
                function.getCalledFunctions(monitor),
                key=lambda target: target.getEntryPoint().getOffset(),
            ),
        )
        for function in functions.getFunctions(True)
    ]

    functions_path = outdir / f"{name}_functions.csv"
    with functions_path.open("w", newline="") as fh:
        writer = csv.writer(fh, lineterminator="\n")
        writer.writerow(("address", "name", "body_words", "callers", "callees"))
        for function, callees in edges:
            writer.writerow(
                (
                    function.getEntryPoint(),
                    function.getName(),
                    function.getBody().getNumAddresses(),
                    len(function.getCallingFunctions(monitor)),
                    len(callees),
                )
            )

    calls_path = outdir / f"{name}_calls.csv"
    with calls_path.open("w", newline="") as fh:
        writer = csv.writer(fh, lineterminator="\n")
        writer.writerow(("caller", "caller_name", "target", "target_name"))
        for function, callees in edges:
            for target in callees:
                writer.writerow(
                    (
                        function.getEntryPoint(),
                        function.getName(),
                        target.getEntryPoint(),
                        target.getName(),
                    )
                )

    lines = [
        f"program={name}",
        f"domain_file={program.getDomainFile().getName()}",
        f"instructions={listing.getNumInstructions()}",
        f"functions={functions.getFunctionCount()}",
    ]
    for block in program.getMemory().getBlocks():
        execute = "true" if block.isExecute() else "false"
        lines.append(
            f"block={block.getName()},{block.getStart()},{block.getEnd()},"
            f"execute={execute}"
        )
    for value in KEY_ADDRESSES:
        address = default_space.getAddress(value * 3)
        instruction = listing.getInstructionAt(address)
        if instruction is not None:
            lines.append(f"instruction={address},{instruction}")
    lines.append("references_to_082aa2:")
    for reference in program.getReferenceManager().getReferencesTo(
        default_space.getAddress(CROSS_REGION_TARGET * 3)
    ):
        lines.append(f"  {reference.getFromAddress()} {reference.getReferenceType()}")

    info_path = outdir / f"{name}_info.txt"
    info_path.write_text("\n".join(lines) + "\n")


def import_program(project, spec: Spec, data: bytes) -> None:
    import jpype
    import pyghidra

    results = (
        pyghidra.program_loader()
        .project(project)
        .source(jpype.JArray(jpype.JByte)(data))
        .name(spec.import_name)
        .loaders(LOADER)
        .language(LANGUAGE)
        .load()
    )
    results.save(pyghidra.task_monitor())


def report(program, spec: Spec, seed_count: int, elapsed: float) -> None:
    print(
        f"{spec.name}: blocks={len(program.getMemory().getBlocks())} "
        f"instructions={program.getListing().getNumInstructions()} "
        f"functions={program.getFunctionManager().getFunctionCount()} "
        f"seeds={seed_count} analysis={elapsed:.1f}s"
    )


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("project", nargs="?", default=DEFAULT_PROJECT)
    ap.add_argument("--ghidra", type=Path, default=DEFAULT_GHIDRA)
    ap.add_argument("--firmware", type=Path, default=DEFAULT_FIRMWARE)
    ap.add_argument("--analysis-timeout", type=int, default=DEFAULT_ANALYSIS_TIMEOUT)
    ap.add_argument(
        "--prepare",
        type=Path,
        metavar="DIR",
        help="write LE24 files and an import list to DIR for the Ghidra GUI, and stop",
    )
    return ap.parse_args()


IMPORT_GUIDE = f"""\
G-Major 2 records as LE24 files, for importing by hand in the Ghidra GUI.

Import each program's first file with File > Import File: format Raw Binary,
language {LANGUAGE}, base address 0. Add the program's other files
with File > Add To Program, each at its byte address under its block name.
Ghidra addresses are bytes, three per DSP word. Keep the two programs apart: both
start at address zero but run at different stages. build_ghidra.py builds the
same programs with seeds, entry points and analysis.

"""


def prepare(firmware: Path, outdir: Path) -> int:
    sizes = read_manifest(firmware)
    outdir.mkdir(parents=True, exist_ok=True)
    rows = []
    for spec in PROGRAM_SPECS:
        blocks = [(spec.import_name, spec.record, spec.initial_block, 0, True)]
        blocks += [
            (f"{Path(r.record).stem}.le24.bin", r.record, r.name, r.address, r.code)
            for r in spec.regions
        ]
        for file, record, block, address, code in blocks:
            (outdir / file).write_bytes(read_record(firmware, sizes, record))
            rows.append(
                f"{spec.name:<16} {file:<38} {block:<13} 0x{address:06X}  "
                f"0x{address * WORD:06X}  {'yes' if code else 'no'}"
            )
    header = (
        f"{'program':<16} {'file':<38} {'block':<13} {'word':<9} {'byte':<9} execute"
    )
    (outdir / "IMPORT.txt").write_text(IMPORT_GUIDE + "\n".join([header, *rows]) + "\n")
    print(f"{len(rows)} LE24 files and IMPORT.txt written to {outdir}")
    return 0


def main() -> int:
    args = parse_args()
    if args.prepare is not None:
        return prepare(args.firmware, args.prepare)

    import pyghidra

    gpr = PROJECT_DIR / f"{args.project}.gpr"
    rep = PROJECT_DIR / f"{args.project}.rep"
    if gpr.exists() or rep.exists():
        raise SystemExit(
            f"refusing to overwrite the existing project {args.project} in "
            f"{PROJECT_DIR}; pick another name"
        )
    if not args.ghidra.is_dir():
        raise SystemExit(f"Ghidra install directory not found: {args.ghidra}")

    sizes = read_manifest(args.firmware)
    records = {
        filename: read_record(args.firmware, sizes, filename)
        for filename in {spec.record for spec in PROGRAM_SPECS}
        | {region.record for spec in PROGRAM_SPECS for region in spec.regions}
    }

    PROJECT_DIR.mkdir(parents=True, exist_ok=True)
    ANALYSIS_DIR.mkdir(parents=True, exist_ok=True)

    launcher = pyghidra.HeadlessPyGhidraLauncher(install_dir=args.ghidra)
    launcher.add_vmargs(*JDK_OPTIONS)
    launcher.start()

    seeds = read_seeds()
    entries = read_entrypoints()

    project = pyghidra.open_project(PROJECT_DIR, args.project, create=True)
    try:
        for spec in PROGRAM_SPECS:
            import_program(project, spec, records[spec.record])
            print(f"imported {spec.import_name} from {args.firmware / spec.record}")

        for spec in PROGRAM_SPECS:
            with pyghidra.program_context(project, spec.project_path) as program:
                seed_count = setup_program(
                    program,
                    spec,
                    records,
                    seeds[spec.stage],
                    entries.get(spec.stage, ()),
                )
                elapsed = analyze_program(program, args.analysis_timeout)
                program.save(
                    f"G-Major 2 {spec.stage} analysis", pyghidra.task_monitor()
                )
                report(program, spec, seed_count, elapsed)
                export_program(program, ANALYSIS_DIR)
                print(f"  exported to {ANALYSIS_DIR}")
    finally:
        project.close()
        # Ghidra's create path does not release the project lock on close, which makes
        # the GUI ask to break a lock nobody holds. The files are advisory only.
        for leftover in (gpr.with_suffix(".lock"), gpr.with_suffix(".lock~")):
            leftover.unlink(missing_ok=True)

    print(f"project written to {PROJECT_DIR}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
