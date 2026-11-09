#!/usr/bin/env python3
"""Converte la lista OUI dell'IEEE nel file /oui.bin letto da Netscan.

Uso: tools/make_oui.py oui.csv oui.bin
     tools/make_oui.py oui.txt oui.bin

Il file di partenza si scarica a mano da https://standards-oui.ieee.org/oui/oui.csv
(oppure oui.txt nella stessa cartella). Accetta entrambi i formati.

Formato di uscita: "OUI1", numero di record (u32 little endian), poi record da 32
byte ordinati per OUI: 3 byte di OUI e 29 di nome riempito con NUL (28 caratteri
al massimo). Un record a dimensione fissa permette la ricerca binaria direttamente
sulla SD, senza caricare il file in RAM.
"""
import csv
import re
import struct
import sys

NAME_MAX = 28
# Suffissi societari che sullo schermo da 160 px mangiano spazio e non dicono nulla
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
    # Solo ASCII: lo schermo non ha glifi per il resto
    name = name.encode("ascii", "replace").decode().replace("?", "")
    return name[:NAME_MAX]


def read_csv(path):
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        for row in csv.DictReader(f):
            # Solo MA-L (blocchi da 24 bit): MA-M e MA-S condividono l'OUI tra più aziende
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
        sys.exit("nessun record trovato: il formato è quello dell'IEEE?")
    with open(dst, "wb") as f:
        f.write(b"OUI1" + struct.pack("<I", len(table)))
        for oui in sorted(table):
            f.write(oui.to_bytes(3, "big") + table[oui].encode().ljust(NAME_MAX + 1, b"\0"))
    print(f"{len(table)} record, {8 + 32 * len(table)} byte")


if __name__ == "__main__":
    main()
