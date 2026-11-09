#!/usr/bin/env python3
"""Client per la console di debug di Grillo (components/devcon).

Trova da solo la porta (USB-Serial-JTAG o CDC di debug) e la apre senza
toccare DTR/RTS, così il dongle non si resetta.

Uso:
    tools/grillo.py cmd state            # esegue un comando e stampa l'output
    tools/grillo.py cmd launch usbdisk
    tools/grillo.py shot screen.png      # screenshot del display (PNG 4x)
    tools/grillo.py log 10               # mostra i log per 10 secondi
Opzioni: --port /dev/cu.usbmodemXXX, -q (nasconde le righe di log)
"""
import argparse
import base64
import glob
import re
import struct
import sys
import time
import zlib

import serial

LOG_RE = re.compile(r"^(\x1b\[[0-9;]*m)?[EWIDV] \(\d+\) ")


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("nessuna porta /dev/cu.usbmodem* trovata")
    return ports[0]


def open_port(path):
    # DTR/RTS non vanno toccati: macOS li alza entrambi all'apertura, e
    # abbassare DTR lasciando RTS alto è la sequenza che resetta il chip
    # sulla USB-Serial-JTAG.
    return serial.Serial(path, 115200, timeout=0.1)


def run(s, command, timeout, quiet):
    """Invia un comando e restituisce (righe di output, rc)."""
    s.reset_input_buffer()
    s.write((command + "\n").encode())
    lines, buf, end = [], b"", time.time() + timeout
    while time.time() < end:
        buf += s.read(4096)
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = raw.decode(errors="replace").rstrip("\r")
            m = re.match(r"^>>> rc=(-?\d+)$", line)
            if m:
                return lines, int(m.group(1))
            if quiet and LOG_RE.match(line):
                continue
            lines.append(line)
    return lines, None


def write_png(path, w, h, rgb565be, scale):
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            hi, lo = rgb565be[(y * w + x) * 2], rgb565be[(y * w + x) * 2 + 1]
            c = (hi << 8) | lo
            r, g, b = (c >> 11) & 0x1F, (c >> 5) & 0x3F, c & 0x1F
            px = bytes(((r * 255) // 31, (g * 255) // 63, (b * 255) // 31))
            row += px * scale
        rows.extend([b"\x00" + bytes(row)] * scale)
    raw = b"".join(rows)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("-q", action="store_true", help="nascondi le righe di log")
    ap.add_argument("-t", type=float, default=5, help="timeout comando (s)")
    sub = ap.add_subparsers(dest="action", required=True)
    c = sub.add_parser("cmd")
    c.add_argument("words", nargs="+")
    sh = sub.add_parser("shot")
    sh.add_argument("out")
    sh.add_argument("--scale", type=int, default=4)
    lg = sub.add_parser("log")
    lg.add_argument("secs", type=float, nargs="?", default=10)
    a = ap.parse_args()

    s = open_port(a.port or find_port())
    if a.action == "cmd":
        lines, rc = run(s, " ".join(a.words), a.t, a.q)
        print("\n".join(lines))
        if rc is None:
            sys.exit("timeout: nessuna risposta")
        sys.exit(rc)
    if a.action == "shot":
        lines, rc = run(s, "shot", max(a.t, 10), True)
        try:
            start = next(i for i, l in enumerate(lines) if l.startswith("SHOT "))
            end = lines.index("END", start)
        except (StopIteration, ValueError):
            sys.exit("screenshot non ricevuto")
        _, w, h, _ = lines[start].split()
        data = base64.b64decode("".join(lines[start + 1:end]))
        write_png(a.out, int(w), int(h), data, a.scale)
        print(a.out)
        return
    if a.action == "log":
        end = time.time() + a.secs
        while time.time() < end:
            d = s.read(4096)
            if d:
                sys.stdout.write(d.decode(errors="replace"))
                sys.stdout.flush()


if __name__ == "__main__":
    main()
