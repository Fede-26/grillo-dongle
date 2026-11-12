# Layout data from operating systems

These files are the source from which `tools/gen_layouts.py` derives the
AltGr/Option levels and the dead keys of Colemak. Do not edit them by hand.

- `colemak-windows.klc`: source of the "Colemak" 1.1 layout for Windows
  (2007), file `src/Colemak.klc` from
  <https://colemak.com/pub/windows/Colemak-1.1-Caps-Lock-Unchanged.zip>
  (SHA256 of the zip `65a082aa58b3910b2584e056aef818fe6dd40de5dc3b085de1ecbd5a2f45ea8d`,
  the same as the one on the page <https://colemak.com/Windows>). Public domain.
  Microsoft Keyboard Layout Creator format, UTF-16.
- `macos_colemak.tsv`: produced by `tools/dump_mac_layout.c`, which queries the
  macOS "Colemak" layout with `UCKeyTranslate`. To be regenerated on a Mac
  (command in the C file header) if Apple changes the layout.
- Linux: the map is written in `gen_layouts.py` (`linux_altgr`), copied from
  xkeyboard-config, `symbols/us`, section `colemak_base`.
- `macos_italian-pro.tsv`: as above, for the macOS "Italian - Pro" layout
  (`./dump_mac_layout com.apple.keylayout.Italian-Pro`). The dump assumes an
  ANSI keyboard; `gen_layouts.py` swaps HID keys 0x35 and 0x64 (`mac_iso`) because
  the dongle emulates an ISO keyboard, like the one on Italian Macs. If on a Mac
  `<` and `\` come out swapped, the keyboard type recognized by macOS is ANSI:
  remove `mac_iso` from `IT` and regenerate.
