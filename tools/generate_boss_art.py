#!/usr/bin/env python3
"""Author + emit the boss hull sprites.

The bosses are drawn in EXACTLY the same pixel language as the player ships
(tools/preview_ship_styles.py): the shared hull lighting ramp (H/h/s/d/D/X),
white glow pixels (g) and the 241..243 accent mask that is remapped at draw
time onto one of the eight paint ramps.  That is what keeps a boss looking
like a bigger, meaner cousin of the ships instead of a pile of filled
rectangles.

Bosses fly NOSE-DOWN toward the player, so the templates are authored with
the bright prow at the bottom and the engine glow at the top.

Outputs:
  gba/src/boss_gfx.c + gba/include/boss_gfx.h   (the real game data)
  tools/out/bosses/*.png                        (12x previews to eyeball)

Usage:
  python3 tools/generate_boss_art.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from preview_ship_styles import MAP, parse_palette, write_png  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(REPO, "tools", "out", "bosses")

W, H = 24, 20


def mirror(half_rows):
    """12-char left halves (incl. center seam) -> symmetric 24-char rows."""
    full = []
    for r in half_rows:
        r = r.ljust(12, ".")[:12]
        full.append(r + r[::-1])
    return full


# ── The boss hulls (24x20) ─────────────────────────────────────────────
# AUTHORED NOSE-UP, exactly like the player ship templates in
# tools/preview_ship_styles.py, so every convention carries over 1:1:
# nose/leading edges = light hull (H, X highlight), h/s/d stepping toward
# trailing edges, small D-glass canopy in an s frame, accents 1/2 on wing
# leading edges, 3 = engine flare, g = white glow.  The generator flips
# each sprite vertically at emit time because bosses fly nose-DOWN.
#
# Index order must match BossSpriteId in boss_gfx.h.

# 0 - RAZORWING (arcade mini): a swept-delta interceptor — the Viper's
# bigger, meaner cousin.  Long accent-tipped nose, narrow canopy strip,
# wide swept wings with bright leading edges.
RAZORWING = [
    "...........1",
    "...........H",
    "..........HH",
    "..........HH",
    ".........HXX",
    ".........HH1",
    "........HHsD",
    "........HsDg",
    ".......HHsDD",
    ".1......HsDD",
    ".1h....HHsDD",
    ".1Hh..HHHsDD",
    ".1HHhHHHHsDs",
    "..1HHHHHHsss",
    "...1HHHHhhhh",
    "....1HHHhhhh",
    ".....1HHhhhh",
    "......1Hshhh",
    ".......sshhh",
    "..........33",
]

# 1 - GOLIATH (arcade battleship): a broad armored slab in the Aegis
# Titan's language — plated prow, deep canopy, side engine pods, layered
# stern.  Drawn at 2x it fills the sky.
GOLIATH = [
    ".........HHH",
    "........HHHH",
    ".......HHHHH",
    ".......HHHXH",
    "......HHHHHH",
    ".2....HHsDDg",
    ".2h...HHsDDD",
    ".shh..HHssss",
    ".sHh2.HhHHHH",
    ".sHH2.HhHHHH",
    ".sHHh.hhhHHH",
    ".sHHhshhhhHH",
    ".1HHssssssHH",
    ".1HHsddsssHH",
    ".1HHsddsssHH",
    "..1HsHhhhhHH",
    "..1sHHhhhHHH",
    "...s33HhhHHH",
    "....33HHHHHH",
    "......33..33",
]

# 2 - IRONMAW (story L10): forked mandible prow — two hull prongs with
# accent teeth around a glowing maw, heavy shoulders, plated body.
IRONMAW = [
    "......1H....",
    "......HH....",
    ".....HHh3...",
    ".....HHh....",
    ".....HHh3...",
    ".....HHhs.gg",
    "....HHHhs3.g",
    "....HHHhs...",
    "....HHHhhss.",
    ".1..HHHHhhhs",
    ".1h.HHHHHhhh",
    ".1hhHHHHXhhh",
    "..1hHHHHHHHh",
    "..1HHHHsDDgh",
    "...1HHHssssh",
    "...1HHHHhhhh",
    "....1HHHhhhh",
    ".....shhhhhh",
    "......sshhhh",
    "..........33",
]

# 3 - GEMINI (story L20): a twin-boom raider — each half is a complete
# narrow hull with its own canopy and engine, joined by an accent spar.
# When it splits at 50% the two halves read instantly.
GEMINI = [
    ".....1......",
    ".....H......",
    "....HHH.....",
    "....HHH.....",
    "....HXH.....",
    "...HHsDH....",
    "...HHsDH....",
    "...HsDgH....",
    "...HHsDH....",
    "...HHHHH....",
    "..1HHHHHs...",
    "..1HHHHHs...",
    ".1HHhHHHhs22",
    "..1HHHHhs222",
    "...1HHHhs...",
    "...1HHhhs...",
    "....shhss...",
    "....shh.....",
    ".....33.....",
    "............",
]

# 4 - FROSTBITE (story L30): an ice interceptor with four crystal leg
# fins — thin h/s diagonals off a slim Viper-style spine, accent tips.
FROSTBITE = [
    "...........1",
    "...........H",
    "1.........HH",
    "1s........HH",
    ".hs......HHX",
    "..hs.....HH1",
    "...hs...HHsD",
    "....hs..HsDg",
    ".....hsHHsDD",
    "......hHHsDD",
    ".1....HHHsDD",
    ".1h..HHHHsDs",
    ".1HhHHHHHsss",
    "..1HHHHHhhhh",
    "...1HHHHhhhh",
    "..hs.1HHhhhh",
    ".hs...1Hshhh",
    "1s.....sshhh",
    "1.........33",
    "............",
]

# 5 - JUGGERNAUT (story L40): the Aegis Titan pushed to a wall — full
# width armored slab, riveted skirt, twin pods, triple stern flare.
JUGGERNAUT = [
    "........HHHH",
    ".......HHHHH",
    "......HHHHHH",
    "......HsDDDD",
    ".2...HHsDDDg",
    ".2h..HHsDDDD",
    ".shh.HHsDDDD",
    ".sHh2HHhHHHH",
    ".sHH2HHhHHHH",
    ".sHHhHhhhHHH",
    ".sHHhshhhhHH",
    ".XHHssssssHH",
    ".1HHsddsssHH",
    ".1HHsddsssHH",
    ".1HHsHhhhhHH",
    "..1HsHhhhHHH",
    "..1sHHhhHHHX",
    "...s33HhHHHH",
    "....33HHHHHH",
    ".....33..333",
]

# 6 - INFERNO (story L50): the Manta Ray grown into a firestorm — huge
# curved wings with hot accent tips, twin tail fins, wingtip flares.
INFERNO = [
    "...........H",
    "..........HH",
    ".........HHH",
    "........HHHH",
    ".......HHHHH",
    "......HHHsDD",
    ".....HHHsDgD",
    "....HHHHsDDD",
    "...1HHHHHsDD",
    "..1HHHHHHHHH",
    ".1HHHHhhhhhh",
    "1HHHhh......",
    "1HHhh.......",
    "3Hh.........",
    "1h..........",
    "3...........",
    "....1Hhs....",
    "....1Hhs....",
    ".....shs....",
    "..........33",
]

# 7 - AEGIS (story L60): a sealed hexagonal fortress — solid hull walls
# stepping down to an accent ring around the dark vault core.
AEGIS = [
    ".......HHHHH",
    ".....HHHHHHH",
    "....HHHHHHHH",
    "...HHHhhhhhh",
    "..HHHhhhhhhh",
    ".HHHhh111111",
    ".HHhh11sssss",
    "HHHh11sDDDDD",
    "HHHh1sDDgDDD",
    "HHHh1sDDDDDD",
    "HHHh1sDDDDDD",
    "HHHh11sDDDDD",
    ".HHhh11sssss",
    ".HHHhh111111",
    "..HHHhhhhhhh",
    "...HHHhhhhhh",
    "....HHHHHHHH",
    ".....HHHHHHH",
    ".......HHHHH",
    ".....33..333",
]

# 8 - THE VOID EMPRESS (story L70): the Phoenix's dark mirror — crown
# spires over the canopy, layered accent-veined wings, twin tail fins.
EMPRESS = [
    ".....1.....1",
    ".....X.....X",
    "....1X..1..X",
    "....HX..X.HX",
    "...HHXHHXHHX",
    "...HHHHHHsDD",
    "..1HHHHHsDgD",
    "..1HHHHHsDDD",
    ".1HHHHHHHsDD",
    ".1HH1HHHHsDD",
    "1HHH1HHHHsss",
    "1HHs1HHHhhhh",
    "1Hhs.1HHhhhh",
    ".3...1HHhhhh",
    "......1Hhhhh",
    ".....1Hhs...",
    "....1Hhs....",
    "....shs.....",
    ".....3......",
    "..........33",
]

BOSSES = [
    ("RAZORWING", RAZORWING),
    ("GOLIATH", GOLIATH),
    ("IRONMAW", IRONMAW),
    ("GEMINI", GEMINI),
    ("FROSTBITE", FROSTBITE),
    ("JUGGERNAUT", JUGGERNAUT),
    ("INFERNO", INFERNO),
    ("AEGIS", AEGIS),
    ("EMPRESS", EMPRESS),
]

# Paint ramp (accent index 0..7) each boss wears by default; the same ramps
# the ship paints use, so the fleet and its enemies share one palette.
DEFAULT_ACCENTS = {
    "RAZORWING": 1,    # ion cyan
    "GOLIATH": 4,      # pulsar gold
    "IRONMAW": 0,      # solar orange
    "GEMINI": 2,       # nova violet
    "FROSTBITE": 1,    # ion cyan
    "JUGGERNAUT": 4,   # pulsar gold
    "INFERNO": 5,      # crimson void
    "AEGIS": 3,        # plasma mint
    "EMPRESS": 2,      # nova violet
}


def rows_to_pixels(rows, label):
    """Mirror the half-rows, then FLIP VERTICALLY: the art is authored
    nose-up in the exact same orientation as the player ship templates,
    but bosses fly nose-down toward the player."""
    rows = mirror(rows)[::-1]
    assert len(rows) == H, f"{label}: {len(rows)} rows"
    out = []
    for r, row in enumerate(rows):
        row = row.ljust(W, ".")[:W]
        for ch in row:
            assert ch in MAP, f"{label} row {r}: bad char {ch!r}"
            out.append(MAP[ch])
    return out


def template_pixel(p, accent):
    if 241 <= p <= 243:
        return 48 + accent * 4 + (p - 240)
    return p


def render(pixels, accent, palette, scale=10):
    bg = (10, 14, 26)
    img = [[bg for _ in range(W * scale)] for _ in range(H * scale)]
    for y in range(H):
        for x in range(W):
            c = template_pixel(pixels[y * W + x], accent)
            if c == 0:
                continue
            col = palette[c]
            for dy in range(scale):
                for dx in range(scale):
                    img[y * scale + dy][x * scale + dx] = col
    return img


def main():
    palette = parse_palette()
    os.makedirs(OUT_DIR, exist_ok=True)

    data = []
    for name, rows in BOSSES:
        px = rows_to_pixels(rows, name)
        data.append((name, px))
        write_png(os.path.join(OUT_DIR, f"boss_{name.lower()}.png"),
                  W * 10, H * 10, render(px, DEFAULT_ACCENTS[name], palette))

    # One lineup image for a quick side-by-side check.
    scale = 6
    lineup = []
    imgs = [render(px, DEFAULT_ACCENTS[name], palette, scale)
            for name, px in data]
    for y in range(H * scale):
        row = []
        for img in imgs:
            row.extend(img[y])
            row.extend([(10, 14, 26)] * 4)
        lineup.append(row)
    write_png(os.path.join(OUT_DIR, "lineup.png"),
              len(lineup[0]), len(lineup), lineup)

    # ── Emit the C data ────────────────────────────────────────────────
    h_path = os.path.join(REPO, "gba", "include", "boss_gfx.h")
    c_path = os.path.join(REPO, "gba", "src", "boss_gfx.c")

    with open(h_path, "w") as f:
        f.write("""#ifndef BOSS_GFX_H
#define BOSS_GFX_H

#include "types.h"

/* Boss hull sprites, authored in the SAME pixel language AND the same
 * nose-up orientation as the player ship templates (hull ramp 33..38,
 * glow 16, canopy D-glass in an s frame, accent mask 241..243 remapped
 * onto a paint ramp at draw time), then flipped vertically at generation
 * time because bosses fly nose-down toward the player.
 * Generated by tools/generate_boss_art.py - edit the art there. */

#define BOSS_SPR_W 24
#define BOSS_SPR_H 20
#define NUM_BOSS_SPRITES 9

typedef enum {
    BOSS_SPR_RAZORWING = 0,    /* arcade mini-boss (waves 5/15/25...) */
    BOSS_SPR_GOLIATH,          /* arcade battleship (waves 10/20/30...) */
    BOSS_SPR_IRONMAW,          /* story L10 */
    BOSS_SPR_GEMINI,           /* story L20 */
    BOSS_SPR_FROSTBITE,        /* story L30 */
    BOSS_SPR_JUGGERNAUT,       /* story L40 */
    BOSS_SPR_INFERNO,          /* story L50 */
    BOSS_SPR_AEGIS,            /* story L60 */
    BOSS_SPR_EMPRESS           /* story L70 */
} BossSpriteId;

extern const u8 spr_boss[NUM_BOSS_SPRITES][BOSS_SPR_W * BOSS_SPR_H];

/* Default paint ramp (accent 0..7) per boss - the same ramps the ship
 * paints use. */
extern const u8 boss_default_accent[NUM_BOSS_SPRITES];

#endif
""")

    with open(c_path, "w") as f:
        f.write("#include \"boss_gfx.h\"\n\n")
        f.write("/* Generated by tools/generate_boss_art.py - "
                "edit the art there. */\n")
        f.write(f"const u8 spr_boss[NUM_BOSS_SPRITES]"
                f"[BOSS_SPR_W * BOSS_SPR_H] = {{\n")
        for name, px in data:
            f.write(f"    /* {name} */\n    {{\n")
            for y in range(H):
                line = ", ".join(str(v) for v in px[y * W:(y + 1) * W])
                f.write(f"        {line},\n")
            f.write("    },\n")
        f.write("};\n\n")
        accents = ", ".join(str(DEFAULT_ACCENTS[name]) for name, _ in data)
        f.write(f"const u8 boss_default_accent[NUM_BOSS_SPRITES] = "
                f"{{ {accents} }};\n")

    print(f"Wrote {c_path}, {h_path} and previews in {OUT_DIR}")


if __name__ == "__main__":
    main()
