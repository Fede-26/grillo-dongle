#!/usr/bin/env python3
"""Genera components/hid/layouts_data.c dalle specifiche dei layout qui sotto.

Ogni layout descrive, riga per riga della tastiera fisica, il carattere
prodotto da ciascun tasto a quattro livelli: normale, Shift, AltGr,
AltGr+Shift. I tasti fisici sono indicati con l'usage HID della posizione
US (quella che il dongle invia); "·" indica nessun carattere.

Aggiungere un layout = aggiungere una voce a LAYOUTS e rigenerare:
    python3 tools/gen_layouts.py
"""
import unicodedata
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
    # macOS "Italian - Pro", letto con tools/dump_mac_layout.c. Il dump assume una
    # tastiera ANSI (HID 0x35 -> `<`, 0x64 -> `\`); una tastiera ISO ha i due tasti
    # scambiati, come la tabella Windows qui sopra, quindi si scambiano anche nel dump.
    "mac_dump": Path(__file__).resolve().parent / "layouts" / "macos_italian-pro.tsv",
    "mac_iso": True,
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

# Livelli AltGr (3) e AltGr+Shift (4) del Colemak di xkb su Linux, "us(colemak)":
# https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config/-/blob/master/symbols/us
# (sezione colemak_base). Stesse posizioni di ROWS; "·" = nessun carattere.
# Un segno diacritico combinante (U+03xx) indica un tasto morto: AltGr+r è
# dead_grave (U+0300), poi la lettera: "à" = AltGr+r, a.
# Solo Linux: su Windows il Colemak ufficiale ha una mappa diversa e su macOS
# i tasti morti non sono stati verificati, lì restano i ripieghi.
COLEMAK["linux_altgr"] = [
    ("\u0303¡ºª¢€ħðþ‘’–×", "~¹²³£¥ĦÐÞ“”—÷"),
    ("äåãø\u0328đłúüö«»", "ÄÅÃØ~ĐŁÚÜÖ‹›"),
    ("á\u0300ß\u0301\u0308\u030cñéíóõ~", "Á~ẞ\u030b~~ÑÉÍÓÕ~"),
    ("·æ\u0302çœ\u0306\u030a\u0304\u0327\u0307¿", "·Æ~ÇŒ~~~~~~"),
]

# macOS: livelli Option e composizioni letti dal layout di sistema con
# tools/dump_mac_layout.c (UCKeyTranslate), non scritti a mano.
COLEMAK["mac_dump"] = Path(__file__).resolve().parent / "layouts" / "macos_colemak.tsv"

# Windows: sorgente KLC del Colemak ufficiale (vedi tools/layouts/README.md)
COLEMAK["windows_klc"] = Path(__file__).resolve().parent / "layouts" / "colemak-windows.klc"

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


def linux_altgr(spec, base):
    """Da `linux_altgr` ricava i caratteri diretti e le sequenze con tasto morto.

    Restituisce (extra, dead): extra = {char: (key, mods)}; dead = lista di
    (char, (key1, mods1), (key2, mods2)), cioè tasto morto poi lettera base.
    `base` = entries() del layout, per trovare i tasti delle lettere.
    """
    extra, marks = {}, {}
    for keys, (l3, l4) in zip(ROWS, spec["linux_altgr"]):
        for text, mods in ((l3, MOD_ALTGR), (l4, MOD_ALTGR | MOD_SHIFT)):
            assert len(text) <= len(keys), (text, len(text), len(keys))
            for key, ch in zip(keys, text):
                if ch == NONE:
                    continue
                if unicodedata.combining(ch):
                    marks.setdefault(ch, (key, mods))
                elif ord(ch) not in base:
                    extra.setdefault(ch, (key, mods))
    dead = []
    for mark, first in marks.items():
        for letter in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ":
            composed = unicodedata.normalize("NFC", letter + mark)
            # Solo se esiste un carattere precomposto e non è già diretto
            if len(composed) != 1 or composed in extra or ord(composed) in base:
                continue
            dead.append((composed, first, base[ord(letter)]))
    return extra, dead


# Scancode set 1 (quelli del KLC, posizionali come l'HID US) -> usage HID
KLC_SCAN = {
    0x29: 0x35, **{0x02 + i: 0x1E + i for i in range(10)}, 0x0C: 0x2D, 0x0D: 0x2E,
    0x10: 0x14, 0x11: 0x1A, 0x12: 0x08, 0x13: 0x15, 0x14: 0x17, 0x15: 0x1C, 0x16: 0x18, 0x17: 0x0C, 0x18: 0x12,
    0x19: 0x13, 0x1A: 0x2F, 0x1B: 0x30,
    0x1E: 0x04, 0x1F: 0x16, 0x20: 0x07, 0x21: 0x09, 0x22: 0x0A, 0x23: 0x0B, 0x24: 0x0D, 0x25: 0x0E, 0x26: 0x0F,
    0x27: 0x33, 0x28: 0x34, 0x2B: 0x31,
    0x56: 0x64, 0x2C: 0x1D, 0x2D: 0x1B, 0x2E: 0x06, 0x2F: 0x19, 0x30: 0x05, 0x31: 0x11, 0x32: 0x10, 0x33: 0x36,
    0x34: 0x37, 0x35: 0x38,
}


def windows_klc(spec, base):
    """Livelli AltGr e tasti morti dal sorgente KLC del Colemak per Windows.

    Colonne del KLC: base, Shift, Ctrl, Ctrl+Alt (= AltGr), Ctrl+Alt+Shift.
    Un carattere seguito da "@" è un tasto morto; le sue composizioni stanno
    nei blocchi DEADKEY. Restituisce (extra, dead) come linux_altgr().
    """
    lines = spec["windows_klc"].read_bytes().decode("utf-16").splitlines()
    layout_at = next(i for i, l in enumerate(lines) if l.startswith("LAYOUT"))
    extra, triggers = {}, {}
    i = layout_at + 1
    while i < len(lines) and not lines[i].startswith(("DEADKEY", "KEYNAME")):
        cols = [c for c in lines[i].split("//")[0].split("\t") if c != ""]
        i += 1
        if len(cols) < 8 or not all(c in "0123456789abcdefABCDEF" for c in cols[0]):
            continue
        key = KLC_SCAN.get(int(cols[0], 16))
        if key is None or key == 0x64:
            continue
        levels = ((cols[3], 0), (cols[4], MOD_SHIFT), (cols[6], MOD_ALTGR), (cols[7], MOD_ALTGR | MOD_SHIFT))
        for value, mods in levels:
            if value == "-1":
                continue
            dead = value.endswith("@")
            value = value.rstrip("@")
            ch = value if len(value) == 1 else chr(int(value, 16))
            if dead:
                triggers.setdefault(ch, (key, mods))
            elif ord(ch) not in base:
                extra.setdefault(ch, (key, mods))
    # Blocchi DEADKEY: "DEADKEY <tasto morto>" poi righe "<base> <risultato>"
    compose, current = {}, None
    for line in lines[layout_at:]:
        if line.startswith("DEADKEY"):
            current = chr(int(line.split("\t")[1], 16))
            compose[current] = {}
        elif line.startswith(("KEYNAME", "ENDKBD")):
            current = None
        elif current and line.strip() and not line.startswith("//"):
            cols = line.split("//")[0].split()
            if len(cols) == 2:
                compose[current][chr(int(cols[0], 16))] = chr(int(cols[1], 16))
    dead = []
    for mark, first in triggers.items():
        for letter, result in compose.get(mark, {}).items():
            if ord(letter) not in base or letter == " " or ord(result) in base or result in extra:
                continue
            dead.append((result, first, base[ord(letter)]))
    return extra, dead


MOD_OPTION = 0x04  # AltSinistro = Option su macOS


def mac_option(spec, base):
    """Livelli Option e composizioni di macOS, dal dump di tools/dump_mac_layout.c.

    Restituisce (extra, dead) come linux_altgr().
    """
    extra, composed = {}, []
    swap = {0x35: 0x64, 0x64: 0x35} if spec.get("mac_iso") else {}
    for line in spec["mac_dump"].read_text().splitlines():
        cols = line.split("\t")
        if cols[0] == "C":
            _, hid, level, letter, result = cols
            mods = MOD_OPTION | (MOD_SHIFT if level == "3" else 0)
            hid = int(hid, 16)
            composed.append((result, (swap.get(hid, hid), mods), base[ord(letter)]))
            continue
        hid = swap.get(int(cols[0], 16), int(cols[0], 16))
        if hid == 0x64 and not swap:  # tasto ISO: non esiste sulle tastiere ANSI dei Mac
            continue
        for ch, mods in ((cols[3], MOD_OPTION), (cols[4], MOD_OPTION | MOD_SHIFT)):
            if ch == "-" or ch.startswith("DEAD:") or ord(ch) in base:
                continue
            extra.setdefault(ch, (hid, mods))
    return extra, composed


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
            dead = []
            if "linux_altgr" in spec:
                more, dead = linux_altgr(spec, e)
                extra = {**more, **extra}
            f.write(f"static const hid_map_t s_{name}_linux[] = {{\n")
            for ch, (key, mods) in sorted(extra.items()):
                f.write(f"    {{0x{ord(ch):04X}, 0x{key:02X}, 0x{mods:02X}}},\n")
            f.write("    {0, 0, 0},\n};\n")
            f.write(f"static const hid_dead_t s_{name}_dead[] = {{\n")
            for ch, (k1, m1), (k2, m2) in sorted(dead):
                f.write(f"    {{0x{ord(ch):04X}, 0x{k1:02X}, 0x{m1:02X}, 0x{k2:02X}, 0x{m2:02X}}},\n")
            f.write("    {0, 0, 0, 0, 0},\n};\n")
            mac_extra, mac_dead = {}, []
            if "mac_dump" in spec:
                mac_extra, mac_dead = mac_option(spec, e)
            f.write(f"static const hid_map_t s_{name}_mac[] = {{\n")
            for ch, (key, mods) in sorted(mac_extra.items()):
                f.write(f"    {{0x{ord(ch):04X}, 0x{key:02X}, 0x{mods:02X}}},\n")
            f.write("    {0, 0, 0},\n};\n")
            f.write(f"static const hid_dead_t s_{name}_mac_dead[] = {{\n")
            for ch, (k1, m1), (k2, m2) in sorted(mac_dead):
                f.write(f"    {{0x{ord(ch):04X}, 0x{k1:02X}, 0x{m1:02X}, 0x{k2:02X}, 0x{m2:02X}}},\n")
            f.write("    {0, 0, 0, 0, 0},\n};\n")
            win_extra, win_dead = {}, []
            if "windows_klc" in spec:
                win_extra, win_dead = windows_klc(spec, e)
            f.write(f"static const hid_map_t s_{name}_win[] = {{\n")
            for ch, (key, mods) in sorted(win_extra.items()):
                f.write(f"    {{0x{ord(ch):04X}, 0x{key:02X}, 0x{mods:02X}}},\n")
            f.write("    {0, 0, 0},\n};\n")
            f.write(f"static const hid_dead_t s_{name}_win_dead[] = {{\n")
            for ch, (k1, m1), (k2, m2) in sorted(win_dead):
                f.write(f"    {{0x{ord(ch):04X}, 0x{k1:02X}, 0x{m1:02X}, 0x{k2:02X}, 0x{m2:02X}}},\n")
            f.write("    {0, 0, 0, 0, 0},\n};\n\n")
        f.write("const hid_layout_t hid_layouts[] = {\n")
        for name, spec in LAYOUTS:
            f.write(f'    {{"{name}", s_{name}, sizeof(s_{name}) / sizeof(s_{name}[0]), s_{name}_linux, s_{name}_dead, s_{name}_mac, s_{name}_mac_dead, s_{name}_win, s_{name}_win_dead}},\n')
        f.write("};\n")
        f.write(f"const int hid_layout_count = {len(LAYOUTS)};\n")
    print(f"scritto {OUT}")


if __name__ == "__main__":
    main()
