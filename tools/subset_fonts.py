#!/usr/bin/env python3
"""Reduces the web page fonts to the characters needed and saves them as woff2.

The pages live in the dongle's flash and with the hotspot there is no internet:
the fonts must be embedded, so it pays to keep them small.

Sources (SIL OFL 1.1 license, text in components/webtheme/web/fonts):
  Silkscreen-Regular.ttf   https://github.com/google/fonts/tree/main/ofl/silkscreen
  iosevka-latin-400-normal.woff2
                           https://cdn.jsdelivr.net/npm/@fontsource/iosevka@5.3.0/files/

Requires fontTools with brotli (pip install fonttools brotli).

Usage: python3 tools/subset_fonts.py <Silkscreen-Regular.ttf> <iosevka-latin-400-normal.woff2>
"""
import sys
from pathlib import Path

from fontTools import subset

OUT = Path(__file__).resolve().parent.parent / "components" / "webtheme" / "web" / "fonts"

# ASCII, Latin-1 (Italian accents included) and the few symbols used by pages
# and scripts: dashes, quotes, ellipsis, arrows, check marks.
UNICODES = (list(range(0x20, 0x7F)) + list(range(0xA0, 0x100))
            + [0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026,
               0x2190, 0x2191, 0x2192, 0x2193, 0x2713, 0x2717])


def reduce(src, dst):
    opts = subset.Options()
    opts.flavor = "woff2"
    # No OpenType features: no ligatures (Iosevka has them in calt) and
    # no tables that monospace or pixel text does not use.
    opts.layout_features = []
    opts.hinting = False
    opts.name_IDs = ["*"]  # copyright and license stay in the file
    font = subset.load_font(src, opts)
    s = subset.Subsetter(opts)
    s.populate(unicodes=UNICODES)
    s.subset(font)
    subset.save_font(font, dst, opts)
    print(f"{dst.name}: {Path(src).stat().st_size} -> {dst.stat().st_size} B")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    OUT.mkdir(parents=True, exist_ok=True)
    reduce(sys.argv[1], OUT / "silkscreen.woff2")
    reduce(sys.argv[2], OUT / "iosevka.woff2")
