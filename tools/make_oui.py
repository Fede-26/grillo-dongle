#!/usr/bin/env python3
"""Converts the IEEE OUI list into the /oui.bin file read by Netscan.

Usage: tools/make_oui.py oui.csv oui.bin
       tools/make_oui.py oui.txt oui.bin

The source file is downloaded by hand from https://standards-oui.ieee.org/oui/oui.csv
(or oui.txt in the same folder). Both formats are accepted.

Output format: "OUI1", record count (u32 little endian), then 32-byte records
sorted by OUI: 3 bytes of OUI and 29 of name padded with NUL (28 characters
at most). A fixed-size record allows binary search directly on the SD,
without loading the file into RAM.
"""
import csv
import re
import struct
import sys

NAME_MAX = 28
# Company suffixes that eat space on the 160 px screen and say nothing
NOISE = re.compile(
    r"[ ,]+(co\.?,? ?ltd\.?|inc\.?|corporation|corp\.?|limited|ltd\.?|gmbh|s\.?p\.?a\.?|llc|technologies|technology)$",
    re.I,
)


def short(name: str) -> str:
    name = " ".join(name.split())
    prev = None
    while prev != name:
        prev = name
        name = NOISE.sub("", name)
    # ASCII only: the screen has no glyphs for the rest
    name = name.encode("ascii", "replace").decode().replace("?", "")
    return name[:NAME_MAX]


def read_csv(path):
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        for row in csv.DictReader(f):
            # MA-L only (24-bit blocks): MA-M and MA-S share the OUI among several companies
            if row.get("Registry") == "MA-L":
                yield row["Assignment"], row["Organization Name"]


def read_txt(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.match(r"^([0-9A-F]{2})-([0-9A-F]{2})-([0-9A-F]{2})\s+\(hex\)\s+(.+)$", line.strip())
            if m:
                yield "".join(m.group(1, 2, 3)), m.group(4)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1:]
    rows = read_csv(src) if src.lower().endswith(".csv") else read_txt(src)
    table = {}
    for assignment, name in rows:
        table[int(assignment, 16)] = short(name)
    if not table:
        sys.exit("no records found: is the format the IEEE one?")
    with open(dst, "wb") as f:
        f.write(b"OUI1" + struct.pack("<I", len(table)))
        for oui in sorted(table):
            f.write(oui.to_bytes(3, "big") + table[oui].encode().ljust(NAME_MAX + 1, b"\0"))
    print(f"{len(table)} records, {8 + 32 * len(table)} bytes")


if __name__ == "__main__":
    main()
