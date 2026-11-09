#!/usr/bin/env python3
"""Genera components/hid/layouts_data.c dalle specifiche dei layout qui sotto.

Ogni layout descrive, riga per riga della tastiera fisica, il carattere
prodotto da ciascun tasto a quattro livelli: normale, Shift, AltGr,
AltGr+Shift. I tasti fisici sono indicati con l'usage HID della posizione
US (quella che il dongle invia); "·" indica nessun carattere.

Aggiungere un layout = aggiungere una voce a LAYOUTS e rigenerare:
    python3 tools/gen_layouts.py
"""
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "components" / "hid" / "layouts_data.c"
NONE = "·"

# Tasti fisici per riga, in ordine da sinistra a destra (usage HID)
ROWS = [
    [0x35, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2D, 0x2E],  # ` 1..0 - =
    [0x14, 0x1A, 0x08, 0x15, 0x17, 0x1C, 0x18, 0x0C, 0x12, 0x13, 0x2F, 0x30],        # q..p [ ]
    [0x04, 0x16, 0x07, 0x09, 0x0A, 0x0B, 0x0D, 0x0E, 0x0F, 0x33, 0x34, 0x31],        # a..l ; ' \
    [0x64, 0x1D, 0x1B, 0x06, 0x19, 0x05, 0x11, 0x10, 0x36, 0x37, 0x38],              # ISO<> z..m , . /
]

MOD_SHIFT = 0x02
MOD_ALTGR = 0x40  # AltDestro: su Windows/Linux è AltGr, su macOS vale come Option

US = {
    "rows": [
        ("`1234567890-=", "~!@#$%^&*()_+", "", ""),
        ("qwertyuiop[]", "QWERTYUIOP{}", "", ""),
        ("asdfghjkl;'\\", "ASDFGHJKL:\"|", "", ""),
        ("·zxcvbnm,./", "·ZXCVBNM<>?", "", ""),
    ],
}

# Italiano standard Windows/Linux (tastiera ISO)
IT = {
    "rows": [
        ("\\1234567890'ì", "|!\"£$%&/()=?^", "", ""),
        ("qwertyuiopè+", "QWERTYUIOPé*", "··€·······[]", "··········{}"),
        ("asdfghjklòàù", "ASDFGHJKLç°§", "·········@#·", ""),
        ("<zxcvbnm,.-", ">ZXCVBNM;:_", "", ""),
    ],
    # Solo Linux (xkb "it"): AltGr+' = `, AltGr+ì = ~ (non presenti su Windows)
    "linux_extra": {"`": (0x2D, MOD_ALTGR), "~": (0x2E, MOD_ALTGR)},
}

# Colemak: variante del layout US con le lettere spostate
COLEMAK = {
    "rows": [
        ("`1234567890-=", "~!@#$%^&*()_+", "", ""),
        ("qwfpgjluy;[]", "QWFPGJLUY:{}", "", ""),
        ("arstdhneio'\\", "ARSTDHNEIO\"|", "", ""),
        ("·zxcvbkm,./", "·ZXCVBKM<>?", "", ""),
    ],
}

LAYOUTS = [("us", US), ("it", IT), ("colemak", COLEMAK)]


def entries(spec):
    out = {}
    levels = [0, MOD_SHIFT, MOD_ALTGR, MOD_ALTGR | MOD_SHIFT]
    for keys, row in zip(ROWS, spec["rows"]):
        for text, mods in zip(row, levels):
            if not text:
                continue
            assert len(text) <= len(keys), (text, len(text), len(keys))
            for key, ch in zip(keys, text):
                if ch == NONE:
                    continue
                # Il primo modo trovato vince (livello più semplice)
                out.setdefault(ord(ch), (key, mods))
    # Tasti comuni a tutti i layout
    for ch, key in ((" ", 0x2C), ("\n", 0x28), ("\t", 0x2B)):
        out.setdefault(ord(ch), (key, 0))
    return out


def main():
    with OUT.open("w") as f:
        f.write("// File generato da tools/gen_layouts.py: non modificare a mano.\n")
        f.write('#include "hid_layout.h"\n\n')
        for name, spec in LAYOUTS:
            e = entries(spec)
            f.write(f"static const hid_map_t s_{name}[] = {{\n")
            for cp in sorted(e):
                key, mods = e[cp]
                f.write(f"    {{0x{cp:04X}, 0x{key:02X}, 0x{mods:02X}}},\n")
            f.write("};\n")
            extra = spec.get("linux_extra", {})
            f.write(f"static const hid_map_t s_{name}_linux[] = {{\n")
            for ch, (key, mods) in sorted(extra.items()):
                f.write(f"    {{0x{ord(ch):04X}, 0x{key:02X}, 0x{mods:02X}}},\n")
            f.write("    {0, 0, 0},\n};\n\n")
        f.write("const hid_layout_t hid_layouts[] = {\n")
        for name, spec in LAYOUTS:
            f.write(f'    {{"{name}", s_{name}, sizeof(s_{name}) / sizeof(s_{name}[0]), s_{name}_linux}},\n')
        f.write("};\n")
        f.write(f"const int hid_layout_count = {len(LAYOUTS)};\n")
    print(f"scritto {OUT}")


if __name__ == "__main__":
    main()
