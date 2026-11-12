**English** · [Italiano](../it/explanation/layout-e-tasti-morti.md)

# Layouts, operating systems and dead keys

## The problem

A USB keyboard does not send characters but codes of physical keys (HID usages). It is the PC that translates "key next to L" into `ò` or `;`, according to the layout it has configured. The dongle must therefore do the reverse: given a character to type, find the key (and modifiers) that produces it in the PC's layout.

The layout alone is not enough. The same layout produces different characters depending on the operating system: AltGr on Windows and Linux is Option on macOS, and the combinations for accented letters differ.

## How it works

For each layout (`us`, `it`, `colemak`) `tools/gen_layouts.py` generates:

- a main table (levels: normal, Shift, AltGr, AltGr+Shift) valid for all systems;
- for each system, a supplementary character table (`linux`, `mac`, `win`) that complements the main one: the generator puts in it only characters the main table lacks. The exception is `linux_extra`, written by hand, which can also override;
- for each system, a list of dead-key sequences: a key that produces nothing by itself, followed by the letter to accent (e.g. AltGr+r then `a` = `à` on Linux Colemak).

`hid_translate` looks first in the system table, then in the main one, then in the dead-key sequences; if it finds nothing it uses a fallback, when one exists.

## Fallbacks

- **Windows**: Alt held down and the character's Windows-1252 code on the numeric keypad. For characters that have a code in that codepage.
- **Linux**: Ctrl+Shift+U, the codepoint in hexadecimal, space. Works with GTK/IBus applications, not in a bare terminal.
- **macOS**: none. A character missing from the layout is an error (`not typeable`), because there is no universal keyboard input method.

## Data sources

The Option/AltGr tables are not written by hand but read from the system:

- Linux (Colemak): xkeyboard-config, `symbols/us`, section `colemak_base`.
- Windows (Colemak): official KLC file from colemak.com (hash in `tools/layouts/README.md`).
- macOS (Colemak and Italian - Pro): dump with `UCKeyTranslate` from the Carbon API, program `tools/dump_mac_layout.c`, to be rerun on a Mac if Apple changes the layouts.

The Italian layout on Linux and Windows is written in the main table (standard ISO keyboard, plus `` ` `` and `~` on Linux).

## The `<` and `\` case on macOS

ISO keyboards have an extra key, next to the left Shift. For ISO keyboards macOS swaps the internal codes of the key next to Shift and of the one to the left of 1; to decide whether a keyboard is ISO it relies on the identification made at first connection (Keyboard Setup Assistant). This is a reconstruction, with medium confidence: it has not been verified against Apple documentation.

The `it` layout on `mac` assumes that macOS treats the dongle as ISO, so it uses the same keys as Windows for `<` and `\`. If your Mac recognizes it as ANSI, the two characters come out swapped: remove `mac_iso` from the `IT` entry in `tools/gen_layouts.py` and regenerate (see `tools/layouts/README.md`).

The assumption has not been verified on a Mac with the dongle connected.
