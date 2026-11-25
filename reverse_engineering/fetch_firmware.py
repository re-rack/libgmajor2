#!/usr/bin/env python3
"""Fetch TC Electronic's G-Major 2 v1.02 updater and manual, and extract the firmware.

The repository holds neither file: this script downloads them from the
Internet Archive's copies of TC's download server into `downloads/`, checks each
against its SHA-256, and cuts the DSP load records out of the updater's SysEx
stream into `firmware/`, with the `manifest.csv` the other tools read. When no
download succeeds it lists the files to find by hand; any file in `downloads/`
with the right SHA-256 is used, whatever its name.

Standard library only.
"""

import csv
import hashlib
import io
import sys
import time
import urllib.request
import zipfile
from pathlib import Path
from typing import NamedTuple

RE_ROOT = Path(__file__).resolve().parent
DOWNLOADS = RE_ROOT / "downloads"
FIRMWARE = RE_ROOT / "firmware"
WORD = 3
TIMEOUT = 60
ATTEMPTS = 3
RETRY_SECONDS = 10


class Source(NamedTuple):
    name: str
    size: int
    sha256: str
    what: str
    origin: str
    captures: tuple[str, ...]

    @property
    def urls(self) -> tuple[str, ...]:
        # `id_` asks the Wayback Machine for the bytes as they were served,
        # without its page rewriting.
        archived = tuple(
            f"https://web.archive.org/web/{capture}id_/{self.origin}"
            for capture in self.captures
        )
        return (*archived, self.origin)


UPDATER = Source(
    name="gmajortwo_swupdater-102.zip",
    size=752277,
    sha256="85560e8cfe0d7f36452a4ee8f389d3fae949e3a7707e2e861736193ebf07858f",
    what="TC Electronic's G-Major 2 firmware updater 1.02 for Windows",
    origin="http://cdn-downloads.tcelectronic.com/media/636051/gmajortwo_swupdater-102.zip",
    captures=("20161026183434", "20150531132405"),
)
MANUAL = Source(
    name="G_Major_II_US.pdf",
    size=551724,
    sha256="20f8e80fb9c495f2f6eff5a903eb9296a238398bb2c620729b0f34046c3cf638",
    what="TC Electronic's G-Major 2 user's manual, English, 2009 (58 pages)",
    origin="http://www.tcelectronic.com/media/G_Major_II_US.pdf",
    captures=("20090520163154", "20111125103259", "20111223073544"),
)
SOURCES = (UPDATER, MANUAL)

UPDATER_EXE = "gmajortwo_swupdater-1.02.exe"

# TC Electronic's SysEx ID and the bytes every message of the download starts with;
# the next byte tells a data message from the end of the stream.
SYSEX_HEADER = bytes((0xF0, 0x00, 0x20, 0x1F, 0x00, 0x66, 0x52))
SYSEX_DATA = 0x00
SYSEX_END = 0x01
SYSEX_EOX = 0xF7


class Record(NamedTuple):
    filename: str
    address: int | None
    words: int
    kind: str
    sha256: str


# In load-stream order. 05a and 05b are one load record, split where its data
# turns into code.
RECORDS = (
    Record(
        "00_bootstrap_000000_152w.bin",
        0x000000,
        152,
        "code",
        "9aeab594756fbf9fd1b8b581dc5f8339e23e4e1a18cf77de8d06840cd08f1069",
    ),
    Record(
        "01_vectors_000000_47w.bin",
        0x000000,
        47,
        "code",
        "2d35bd5db0cdc883136b61df02485bb1843ce85c256f0474c12a8365ad568c32",
    ),
    Record(
        "02_program_low_000030_5039w.bin",
        0x000030,
        5039,
        "code",
        "e478a7aa784c2e00deff2f67b722c824b6214b2bbe0d42dba855a601c97e167a",
    ),
    Record(
        "03_config_080000_213w.bin",
        0x080000,
        213,
        "data",
        "b59865f2a4269aa0ee822f531ac28fca1dc98b6c84a3226e3d9104e872a8e10f",
    ),
    Record(
        "04_vars_080112_766w.bin",
        0x080112,
        766,
        "data",
        "5d1138d07297a58beccd77526ffc762075ce5d835c1c29495d8c35e3954800e3",
    ),
    Record(
        "05a_data_080412_9872w.bin",
        0x080412,
        9872,
        "data",
        "5aadd050d9da1cae4069aa57b96f16a561ce909fd950039ebdcaafe485064029",
    ),
    Record(
        "05b_code_082aa2_14795w.bin",
        0x082AA2,
        14795,
        "code",
        "35261aaf71e3eb711f8c51524b68d07eb149b1275126b0f8a31558025d6b5775",
    ),
    Record(
        "06_presets_088000_16701w.bin",
        0x088000,
        16701,
        "data",
        "24610c1d654aac3872c0902afd3edb69045d07ad2d924e86c0a11900cd9555f3",
    ),
    Record(
        "07_program_high_08ce20_44304w.bin",
        0x08CE20,
        44304,
        "code",
        "574a741f1e6992d2e2d26a2e1efe9d6cfaf02822114e1c00aa50864b7055b202",
    ),
    Record(
        "08_init_098000_77w.bin",
        0x098000,
        77,
        "code",
        "335aec7d9b93d6cf54dc58ad3b28be9ba1136abd01cbb9ae24f6fd574c758a51",
    ),
    Record(
        "09_terminator.bin",
        None,
        3,
        "terminator",
        "47ac70275cf5294f4279636e5f90bbf8cfd4b0bec4e766565399dc868e2f6d08",
    ),
    Record(
        "10_filler.bin",
        None,
        8,
        "filler",
        "29fffc4930bb1de55df2c0cb6bffe4ebc555d044c4e4a062f09fec612e0d3778",
    ),
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def matches(data: bytes, source: Source) -> bool:
    return len(data) == source.size and sha256(data) == source.sha256


def local_copy(source: Source) -> bytes | None:
    """The source from `downloads/`, under its own name or any other."""
    named = DOWNLOADS / source.name
    candidates = [named] if named.is_file() else []
    if DOWNLOADS.is_dir():
        candidates += sorted(
            path
            for path in DOWNLOADS.iterdir()
            if path != named and path.is_file() and path.stat().st_size == source.size
        )
    for path in candidates:
        data = path.read_bytes()
        if matches(data, source):
            return data
    return None


def download(source: Source) -> bytes | None:
    request_headers = {"User-Agent": "gm2-fetch-firmware/1"}
    for url in source.urls:
        for attempt in range(1, ATTEMPTS + 1):
            try:
                request = urllib.request.Request(url, headers=request_headers)
                with urllib.request.urlopen(request, timeout=TIMEOUT) as response:
                    data = response.read()
            except OSError as error:
                if attempt < ATTEMPTS:
                    time.sleep(RETRY_SECONDS)
                    continue
                print(f"  {url}: {error}")
                break
            if matches(data, source):
                return data
            print(f"  {url}: {len(data)} bytes that are not {source.name}")
            break
    return None


def fetch(source: Source) -> bool:
    if local_copy(source) is not None:
        print(f"{source.name}: present")
        return True
    print(f"{source.name}: downloading")
    data = download(source)
    if data is None:
        return False
    DOWNLOADS.mkdir(parents=True, exist_ok=True)
    partial = DOWNLOADS / f"{source.name}.part"
    partial.write_bytes(data)
    partial.replace(DOWNLOADS / source.name)
    print(f"{source.name}: downloaded")
    return True


def ask_for(missing: list[Source]) -> None:
    print(
        "\nThese files could not be downloaded. Find them elsewhere, in an archive or a\n"
        "mirror of TC Electronic's G-Major 2 support page, and put them into\n"
        f"  {DOWNLOADS}/\n"
        "under any name: a file counts when its SHA-256 matches.\n"
    )
    for source in missing:
        print(f"  {source.name}  ({source.size} bytes)")
        print(f"    {source.what}")
        print(f"    sha256 {source.sha256}")
        print(f"    TC's address: {source.origin}\n")


def ensure_sources() -> None:
    missing = [source for source in SOURCES if not fetch(source)]
    if not missing:
        return
    DOWNLOADS.mkdir(parents=True, exist_ok=True)
    ask_for(missing)
    if not sys.stdin.isatty():
        raise SystemExit("then run the make target again")
    while missing:
        try:
            input("Press Enter once they are there, or Ctrl-C to stop. ")
        except (EOFError, KeyboardInterrupt):
            raise SystemExit("\nstopped") from None
        missing = [source for source in missing if local_copy(source) is None]
        for source in missing:
            print(f"  still missing: {source.name}, sha256 {source.sha256}")


def unpack_7bit(payload: bytes) -> bytes:
    """MIDI-safe bytes to data: each group's first byte holds the high bits of the
    up to seven bytes after it, the first byte's in bit 6."""
    out = bytearray()
    for start in range(0, len(payload), 8):
        high = payload[start]
        for index, low in enumerate(payload[start + 1 : start + 8]):
            out.append(low | (0x80 if high & (0x40 >> index) else 0))
    return bytes(out)


def sysex_stream(exe: bytes) -> bytes:
    """The download's data messages, unpacked and joined, up to its end message."""
    stream = bytearray()
    position = 0
    while True:
        start = exe.find(SYSEX_HEADER, position)
        if start < 0:
            raise SystemExit(f"{UPDATER_EXE}: the download stream has no end message")
        end = exe.find(bytes((SYSEX_EOX,)), start)
        kind = exe[start + len(SYSEX_HEADER)]
        if kind == SYSEX_DATA:
            stream += unpack_7bit(exe[start + len(SYSEX_HEADER) + 1 : end])
        elif kind == SYSEX_END:
            return bytes(stream)
        position = end + 1


def word(stream: bytes, position: int) -> int:
    return int.from_bytes(stream[position : position + WORD], "big")


def split_stream(stream: bytes) -> dict[str, bytes]:
    """The load stream to record files. It is the bootstrap as {count, address,
    words}, then records as {count, address, flags, words}, then the terminator, a
    record header with count 0, then the filler."""
    count = word(stream, 0)
    loads = [(word(stream, WORD), stream[2 * WORD : (2 + count) * WORD])]
    position = (2 + count) * WORD
    while (count := word(stream, position)) != 0:
        start = position + 3 * WORD
        loads.append(
            (word(stream, position + WORD), stream[start : start + count * WORD])
        )
        position = start + count * WORD

    files = {}
    pending = list(RECORDS[:-2])
    for address, payload in loads:
        offset = 0
        while offset < len(payload):
            if not pending:
                raise SystemExit("the load stream has more records than expected")
            record = pending.pop(0)
            found = address + offset // WORD
            if record.address != found:
                raise SystemExit(
                    f"{record.filename}: the load stream has 0x{found:06X} in its place"
                )
            files[record.filename] = payload[offset : offset + record.words * WORD]
            offset += record.words * WORD
    if pending:
        raise SystemExit(f"the load stream ends before {pending[0].filename}")
    terminator, filler = RECORDS[-2:]
    files[terminator.filename] = stream[position : position + terminator.words * WORD]
    files[filler.filename] = stream[position + terminator.words * WORD :]
    return files


def extract(updater: bytes) -> None:
    with zipfile.ZipFile(io.BytesIO(updater)) as archive:
        exe = archive.read(UPDATER_EXE)
    files = split_stream(sysex_stream(exe))
    for record in RECORDS:
        data = files[record.filename]
        if len(data) != record.words * WORD or sha256(data) != record.sha256:
            raise SystemExit(f"{record.filename}: does not match its SHA-256")

    FIRMWARE.mkdir(parents=True, exist_ok=True)
    for record in RECORDS:
        (FIRMWARE / record.filename).write_bytes(files[record.filename])
    with (FIRMWARE / "manifest.csv").open("w", newline="") as fh:
        writer = csv.writer(fh, lineterminator="\n")
        writer.writerow(("filename", "load_address", "words", "bytes", "kind"))
        for record in RECORDS:
            address = "" if record.address is None else hex(record.address)
            writer.writerow(
                (
                    record.filename,
                    address,
                    record.words,
                    record.words * WORD,
                    record.kind,
                )
            )
    print(f"firmware: {len(RECORDS)} records and manifest.csv written to {FIRMWARE}")


def main() -> int:
    ensure_sources()
    updater = local_copy(UPDATER)
    assert updater is not None
    extract(updater)
    return 0


if __name__ == "__main__":
    sys.exit(main())
