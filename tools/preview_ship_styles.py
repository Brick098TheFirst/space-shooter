#!/usr/bin/env python3
"""Preview + emit the hull-style ship templates.

Renders every style in every paint to PNG (tools/out/ships/*.png) using the
real master palette from gba/src/gfx_data.c, and prints the C array block
for the templates table.

Usage:
  python3 tools/preview_ship_styles.py            # PNG previews + C block
  python3 tools/preview_ship_styles.py --c-only   # just the C block
"""
import os
import re
import struct
import sys
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(REPO, "tools", "out", "ships")

W, H = 20, 16

MAP = {
    ".": 0,
    "H": 37, "h": 36, "s": 35, "d": 34, "D": 33, "X": 38,
    "1": 241, "2": 242, "3": 243,
    "g": 16,
}


def mirror(half_rows):
    """Take 10-char half-rows (left side incl. center seam) and mirror them
    against the 10-char right side -> perfectly symmetric 20-char rows."""
    full = []
    for r in half_rows:
        r = r.ljust(10, ".")[:10]
        full.append(r + r[::-1])
    return full


# ── Ship templates (20x16) ─────────────────────────────────────────────
# New hulls are authored as left halves and mirrored for perfect symmetry.
# Lighting convention (same as classic sprite): nose/top = light hull (H),
# shading steps down h/s/d toward trailing edges, canopy = s frame + D glass,
# accents 1 (bright) / 2 (mid) / 3 (hot tip) carry the player's paint.

# Style 0 placeholder: the real classic pixels come from gfx_data.c so this
# table entry is byte-identical to the classic sprite (49->241, 50->242).
CYBER = None  # filled by parse_classic()

# Style 1: Viper Mk II — sleek arrow interceptor: long sharp accent-tipped
# nose, narrow canopy, wide swept delta wings with bright leading edges,
# twin tail fins flanking a hot engine block.
# (half rows: wingtip .. center -> col 0 .. col 9)
VIPER = [
    ".........1",
    ".........H",
    ".........H",
    "........HH",
    "........H1",
    ".......HH2",
    ".......HsD",
    "......HHsD",
    ".1HH1HsggD",
    "..1HHHsDDD",
    "...1HHHsDD",
    "....1HHhsh",
    ".....1Hhhs",
    "......1Hhh",
    ".......ss.",
    "........33",
]

# Style 2: Manta Ray — wide organic glider: huge rounded wings that curve
# from nose down to glowing wingtip nacelles, thin pod spine.
MANTA = [
    ".........H",
    "........HH",
    ".......HHH",
    "......HHHH",
    ".....HHHsD",
    "....HHHsgD",
    "...HHHHsDD",
    "..1HHHHHHH",
    ".1HHHhhhhh",
    "1HHhh.....",
    "1hh.......",
    "3.........",
    "1.........",
    "..........",
    "..........",
    "..........",
]

# Style 3: Aegis Titan — heavy armored gunship: blunt plated nose, small
# recessed canopy, twin side engine pods with paint strips, riveted armor
# skirt, stabilizer keel fins.
TITAN = [
    "........HH",
    ".......HHH",
    "......HHHH",
    "......HsDD",
    ".1....HsDD",
    ".H1..HsgDD",
    ".HH..HsDDD",
    ".HH2.HsDDD",
    ".hh2.HhHHH",
    ".1hhshhhhH",
    ".1HssssssH",
    ".1HsdssssH",
    "..HsHhhhhH",
    "..s33HhhHH",
    "...33.....",
    "..........",
]

# Style 4: Phoenix MkX — ceremonial flagship: crown crest over the canopy,
# layered feathered wing blades with paint edges, radiant core, tail halo.
PHOENIX = [
    ".........3",
    "........31",
    ".........H",
    "........H1",
    ".......HH1",
    "......H1HD",
    ".....HH1sg",
    "....1HHHsD",
    "...1H1HHsD",
    "..1HH1H1HD",
    ".1HHhH1HDD",
    "1HHshh1HsD",
    "1Hhssh.1Hs",
    ".3....1.sH",
    ".......1.H",
    "........33",
]


# ── Palette parsing ────────────────────────────────────────────────────

def parse_palette():
    src = open(os.path.join(REPO, "gba", "src", "gfx_data.c")).read()
    m = re.search(r"master_palette\[256\] = \{(.*?)\};", src, re.S)
    vals = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", m.group(1))]
    assert len(vals) == 256
    def rgb(c):
        r = (c & 0x1F) << 3
        g = ((c >> 5) & 0x1F) << 3
        b = ((c >> 10) & 0x1F) << 3
        return (r, g, b)
    return [rgb(v) for v in vals]


def parse_classic():
    src = open(os.path.join(REPO, "gba", "src", "gfx_data.c")).read()
    m = re.search(r"spr_ship\[9\]\[20 \* 16\] = \{(.*?)\n\};", src, re.S)
    nums = [int(x) for x in re.findall(r"\d+", m.group(1))]
    assert len(nums) == 9 * 20 * 16
    spr = [nums[i * 320:(i + 1) * 320] for i in range(9)]
    return spr


# ── PNG writer ─────────────────────────────────────────────────────────

def write_png(path, width, height, rgb_rows):
    raw = bytearray()
    for row in rgb_rows:
        raw.append(0)
        for (r, g, b) in row:
            raw += bytes((r, g, b))
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


RAINBOW_SHIP_ACCENTS = [5, 0, 4, 3, 1, 2, 7]  # same as gfx_draw_ship()


def template_pixel(p, accent):
    """Map a template pixel to a final palette index (matches renderer.c)."""
    if 241 <= p <= 243:
        shade = p - 240
        if accent == 8:
            # rainbow: caller passes a fixed accent per preview; runtime animates
            base = 1
            return 48 + base * 4 + shade
        return 48 + accent * 4 + shade
    return p


def render_style(pixels, accent, palette, scale=12):
    px = [[template_pixel(pixels[y * W + x], accent) for x in range(W)] for y in range(H)]
    bg = (10, 14, 26)
    img = [[bg for _ in range(W * scale)] for _ in range(H * scale)]
    for y in range(H):
        for x in range(W):
            c = px[y][x]
            if c == 0:
                continue
            col = palette[c]
            for dy in range(scale):
                for dx in range(scale):
                    img[y * scale + dy][x * scale + dx] = col
    return img


def emit_c(name, pixels):
    print(f"    /* {name} */")
    print("    {")
    for y in range(H):
        line = ", ".join(str(v) for v in pixels[y * W:(y + 1) * W])
        print(f"        {line},")
    print("    },")


def rows_to_pixels(rows, label):
    assert len(rows) == H, f"{label}: {len(rows)} rows"
    out = []
    for r, row in enumerate(rows):
        row = row.ljust(W, ".")[:W]
        for ch in row:
            assert ch in MAP, f"{label} row {r}: bad char {ch!r}"
            out.append(MAP[ch])
    return out


def main():
    palette = parse_palette()
    classic = parse_classic()

    # Classic template: paint 0 spr with accents remapped to 241/242/243.
    cyber = []
    for p in classic[0]:
        if p == 49:
            cyber.append(241)
        elif p == 50:
            cyber.append(242)
        else:
            cyber.append(p)

    styles = [
        ("CYBER", cyber),
        ("VIPER", rows_to_pixels(mirror(VIPER), "VIPER")),
        ("MANTA", rows_to_pixels(mirror(MANTA), "MANTA")),
        ("TITAN", rows_to_pixels(mirror(TITAN), "TITAN")),
        ("PHOENIX", rows_to_pixels(mirror(PHOENIX), "PHOENIX")),
    ]

    # sanity: classic template must equal the real sprite for every paint
    for a in range(8):
        for i, p in enumerate(cyber):
            if 241 <= p <= 243:
                expect = 48 + a * 4 + (p - 240)
            else:
                expect = p
            actual = classic[a][i]
            if actual != expect:
                print(f"classic mismatch paint {a} px {i}: template {expect} vs sprite {actual}",
                      file=sys.stderr)

    if "--c-only" not in sys.argv:
        os.makedirs(OUT_DIR, exist_ok=True)
        accents = [0, 1, 2, 3, 4, 5, 6, 7]
        for si, (name, pixels) in enumerate(styles):
            rowimgs = []
            for a in accents:
                rowimgs.append(render_style(pixels, a, palette))
            gap = 8
            total_w = sum(len(im[0]) for im in rowimgs) + gap * (len(rowimgs) - 1)
            hh = rowimgs[0] and len(rowimgs[0])
            canvas = [[(10, 14, 26) for _ in range(total_w)] for _ in range(hh)]
            ox = 0
            for im in rowimgs:
                for y in range(len(im)):
                    for x in range(len(im[0])):
                        px = im[y][x]
                        if px != (10, 14, 26):
                            canvas[y][ox + x] = px
                ox += len(im[0]) + gap
            write_png(os.path.join(OUT_DIR, f"ship_{si}_{name.lower()}.png"),
                      total_w, hh, canvas)
        print("wrote previews to", OUT_DIR, file=sys.stderr)

    if "--no-c" not in sys.argv:
        print("const u8 spr_ship_styles[NUM_SHIP_STYLES][20 * 16] = {")
        for name, pixels in styles:
            emit_c(name, pixels)
        print("};")


if __name__ == "__main__":
    main()
