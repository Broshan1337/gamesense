#!/usr/bin/env python3
"""Regenerates NotoCJK-subset.ttf - the CJK glyph fallback merged into the menu fonts.

The menu's Inter fonts are Latin-only; CJK shows as boxes without this. We only need the
handful of codepoints that appear in Neverlose.cpp UI strings, so a tiny committed subset
(objcopy-embedded like the other fonts) beats a 19 MB system font dependency.

stb_truetype (ImGui's rasterizer) cannot read CFF2, and the system Noto CJK is a variable
CFF2 .ttc - so this script instantiates a static face, subsets it to the needed codepoints,
and converts CFF quadratics... actually cubics to TrueType quadratics (cu2qu).

Usage: python3 make-cjk-subset.py
Source: /usr/share/fonts/google-noto-sans-cjk-vf-fonts/NotoSansCJK-VF.ttc (OFL, see LICENSE-NOTES.txt)
Re-run whenever UI strings gain new CJK characters.
"""
import sys

from fontTools import varLib
from fontTools.ttLib import TTFont, newTable
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.subset import Subsetter, Options

# Keep in sync with non-ASCII literals in Neverlose.cpp (python3 one-liner in the roadmap
# below). U+58F1 壱, U+738B 王, U+9F99 龍 as of 2026-08-28.
CODEPOINTS = [0x58F1, 0x738B, 0x9F99]
SOURCE = "/usr/share/fonts/google-noto-sans-cjk-vf-fonts/NotoSansCJK-VF.ttc"
OUTPUT = "NotoCJK-subset.ttf"


def find_face(path):
    """First face in the collection that covers every needed codepoint."""
    for i in range(10):
        try:
            font = TTFont(path, fontNumber=i)
        except Exception:
            break
        cmap = font.getBestCmap()
        if all(cp in cmap for cp in CODEPOINTS):
            print(f"face {i}: {font['name'].getDebugName(4)}")
            return font
    sys.exit("no face covers all needed codepoints")


def cff_to_glyf(font):
    """The fontTools otf2ttf recipe: cubic CFF outlines -> quadratic glyf. NOTE the explicit
    glyf.compile(font) - it populates the loca table; without it the saved font has glyf but
    no loca, which stb_truetype (ImGui's rasterizer) cannot load."""
    glyphOrder = font.getGlyphOrder()
    glyphSet = font.getGlyphSet()
    font["loca"] = newTable("loca")
    font["glyf"] = glyf = newTable("glyf")
    glyf.glyphOrder = glyphOrder
    quadGlyphs = {}
    for name in glyphOrder:
        glyph = glyphSet[name]
        ttPen = TTGlyphPen(glyphSet)
        cu2quPen = Cu2QuPen(ttPen, 1.0, reverse_direction=True)
        glyph.draw(cu2quPen)
        quadGlyphs[name] = ttPen.glyph()
    glyf.glyphs = quadGlyphs
    glyf.compile(font)

    hmtx = font["hmtx"]
    for name, glyph in glyf.glyphs.items():
        if hasattr(glyph, "xMin"):
            hmtx[name] = (hmtx[name][0], glyph.xMin)

    maxp = font["maxp"]
    maxp.tableVersion = 0x00010000
    maxp.maxZones = 1
    maxp.maxTwilightPoints = 0
    maxp.maxStorage = 0
    maxp.maxFunctionDefs = 0
    maxp.maxInstructionDefs = 0
    maxp.maxStackElements = 0
    maxp.maxSizeOfInstructions = 0
    maxp.maxComponentElements = max(
        (len(g.components) for g in glyf.glyphs.values() if hasattr(g, "components")), default=0
    )
    maxp.maxComponentDepth = 1 if maxp.maxComponentElements else 0
    maxp.numGlyphs = len(glyphOrder)

    # stb_truetype needs only head/hhea/hmtx/maxp/cmap/glyf/loca (+OS/2, post, name for
    # completeness). Everything else is dead weight or CFF2 leftovers that could confuse it.
    for tag in ("CFF ", "CFF2", "VORG", "VVAR", "STAT", "BASE", "GPOS", "GSUB", "vhea", "vmtx", "fvar", "avar", "hvar"):
        if tag in font:
            del font[tag]
    font.sfntVersion = "\x00\x01\x00\x00"


def main():
    font = find_face(SOURCE)

    # Static instance first (variable axes off), then subset.
    if "fvar" in font:
        from fontTools.varLib.instancer import instantiateVariableFont
        instantiateVariableFont(font, {"wght": 400}, inplace=True)

    options = Options()
    options.name_IDs = ["*"]
    options.notdef_outline = True
    options.recalc_bounds = True
    options.drop_tables += ["FFTM"]
    subsetter = Subsetter(options)
    subsetter.populate(unicodes=CODEPOINTS)
    subsetter.subset(font)

    cff_to_glyf(font)
    font.save(OUTPUT)
    import os
    print(f"{OUTPUT}: {os.path.getsize(OUTPUT)} bytes, glyphs: {font.getGlyphOrder()}")


if __name__ == "__main__":
    main()
