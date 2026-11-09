#!/usr/bin/env python3
"""Genera components/gfx/font_data.c dai font unscii (pubblico dominio).

Fonte: http://viznut.fi/unscii/ (unscii-8.hex, unscii-16.hex).
I glifi sono larghi 8 px nel font originale; qui calcoliamo per ogni glifo
la colonna minima/massima occupata così da poter disegnare il testo in modo
proporzionale (più caratteri per riga sui 160 px del display).

Uso: python3 tools/gen_font.py
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FONTS = ROOT / "tools" / "fonts"
OUT = ROOT / "components" / "gfx" / "font_data.c"

# ASCII stampabile + pochi simboli usati dalla UI. L'ordine definisce
# l'indice del glifo: gfx_text decodifica UTF-8 e cerca qui il codepoint.
EXTRA = [0x2022, 0x2713, 0x2717, 0x2190, 0x2192, 0x2191, 0x2193, 0x25B8,
         0x2026, 0x00B0, 0x2039, 0x203A, 0x00E0, 0x00E8, 0x00E9, 0x00EC,
         0x00F2, 0x00F9, 0x00B7]
CODEPOINTS = list(range(0x20, 0x7F)) + EXTRA
# unscii non ha questi glifi (uscirebbe '?'): si usa il simbolo ASCII più vicino.
ALIAS = {0x2039: ord("<"), 0x203A: ord(">"), 0x00B7: 0x2022}


def load(name):
    glyphs = {}
    for line in (FONTS / name).read_text().splitlines():
        cp, data = line.split(":")
        glyphs[int(cp, 16)] = bytes.fromhex(data)
    return glyphs


def emit(f, name, glyphs, height):
    rows_out, metrics = [], []
    for cp in CODEPOINTS:
        rows = glyphs.get(ALIAS.get(cp, cp), glyphs[ord("?")])
        assert len(rows) == height, (hex(cp), len(rows))
        used = 0
        for r in rows:
            used |= r
        if cp == 0x20 or used == 0:
            left, width = 0, height // 4 + 1  # spazio: larghezza fissa
        else:
            cols = [c for c in range(8) if used & (0x80 >> c)]
            left, width = cols[0], cols[-1] - cols[0] + 1
        metrics.append((left, width))
        rows_out.append(rows)
    f.write(f"static const uint8_t {name}_bits[{len(CODEPOINTS)}][{height}] = {{\n")
    for cp, rows in zip(CODEPOINTS, rows_out):
        f.write("    {" + ",".join(f"0x{b:02X}" for b in rows) + f"}}, // U+{cp:04X}\n")
    f.write("};\n")
    f.write(f"static const uint8_t {name}_metrics[{len(CODEPOINTS)}][2] = {{\n")
    for left, width in metrics:
        f.write(f"    {{{left},{width}}},\n")
    f.write("};\n")
    f.write(f"const gfx_font_t {name} = {{ .height = {height}, .count = {len(CODEPOINTS)}, "
            f".bits = &{name}_bits[0][0], .metrics = &{name}_metrics[0][0], .codepoints = s_codepoints }};\n\n")


def main():
    with OUT.open("w") as f:
        f.write("// File generato da tools/gen_font.py: non modificare a mano.\n")
        f.write("// Font unscii di viznut (pubblico dominio).\n")
        f.write('#include "gfx.h"\n\n')
        f.write(f"static const uint16_t s_codepoints[{len(CODEPOINTS)}] = {{")
        f.write(",".join(f"0x{c:04X}" for c in CODEPOINTS))
        f.write("};\n\n")
        emit(f, "gfx_font_small", load("unscii-8.hex"), 8)
        emit(f, "gfx_font_large", load("unscii-16.hex"), 16)
    print(f"scritto {OUT}")


if __name__ == "__main__":
    main()
