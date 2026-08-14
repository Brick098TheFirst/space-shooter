#!/usr/bin/env python3
"""Generate the hull-style ship templates for the SHIPS shop tab.

Each style is a 20x16 pixel-art sprite in the SAME palette language the
classic Cyber Mk I uses, so every paint (including the animated rainbow
paint) works on every hull:

  .  transparent
  H  hull highlight      (37)
  h  hull mid            (36)
  s  hull shadow         (35)
  d  hull dark step      (34)
  D  hull deepest shadow (33)
  X  hull brightest      (38)
  1  accent shade 1      (241 -> 48 + paint*4 + 1)
  2  accent shade 2      (242 -> 48 + paint*4 + 2)
  3  accent shade 3      (243 -> 48 + paint*4 + 3, hottest)
  g  white specular glint (16)

Run:  python3 tools/generate_ship_styles.py > /tmp/ship_styles.inc
"""

W, H = 20, 16

MAP = {
    ".": 0,
    "H": 37, "h": 36, "s": 35, "d": 34, "D": 33, "X": 38,
    "1": 241, "2": 242, "3": 243,
    "g": 16,
}

# ── Style 0: Cyber Mk I (classic) ──────────────────────────────────────
# Faithful transcription of spr_ship[0] with 49->241 and 50->242 so the
# template table is complete; style 0 still renders through the classic
# fast path at runtime.
CYBER = [
    "........HHHH........",
    "........HHHH........",
    "........HHHH........",
    "........HHHH........",
    ".......HHHHHH.......",
    ".......2HHHH2.......",
    "H....222HHHH222....H",
    "HH..2222hss h2222..HH".replace(" ", "h"),
    "Hh222222hsDD sh222222hH".replace(" ", "s"),
    "Hh22222hXHhX2222222hH".replace("X", "h"),
    "Hh22222hHHHHh2222222hH",
    "Hh21122hHHHHh2211222hH".replace("11", "22"),
    "Hh22222sHHHHs2222222hH",
    "....222hHHHHh222....",
    ".......HHHHHH.......",
    "........HHHH........",
]

# ── Style 1: Viper Mk II ───────────────────────────────────────────────
# Sleek arrow interceptor: long sharp nose, wide swept delta wings with
# accent leading edges, twin tail fins, hot engine core.
VIPER = [
    ".........11.........",
    ".........HH.........",
    ".........HH.........",
    "........HHHH........",
    "........H11H........",
    ".......HH22HH.......",
    ".......HsDDsH.......",
    "......HHsDDsHH......",
    ".....1HHsgDsHH1.....",
    "....11HHsDDsHH11....",
    "...1HHHhsDDshHHH1...",
    "..1HHhhhHHHHhhhHH1..",
    ".1HHhhhssHHHHsshhhH1.",
    "1HhhssssshHHhssssshH1",
    "......ss.HHHH.ss.....",
    ".......33HH33........",
]

# ── Style 2: Manta Ray ─────────────────────────────────────────────────
# Wide organic glider: huge curved wings flowing from the nose, thin
# central pod, glowing accent wing-tip nacelles.
MANTA = [
    ".........HH.........",
    "........HHHH........",
    ".......HHHHHH.......",
    "......HHHHHHHH......",
    ".....HHHsDDsHHHH....",
    "....HHHHsDDsHHHHH...",
    "...HHHHHsgDsHHHHHH..",
    "..1HHHHHsDDsHHHHHH1.",
    ".1HHHHHHHDDHHHHHHHH1",
    "1HHhhhhhhHHHHhhhhhhH1",
    "1Hhh....ssHHss....hhH1",
    ".3......sHHHHs......3",
    "........sHHHHs.......",
    ".........hHHh........",
    ".........sHHs........",
    "..........33.........",
]

# ── Style 3: Aegis Titan ───────────────────────────────────────────────
# Heavy armored gunship: broad plated hull, twin side engine pods, blunt
# reinforced nose, armor seam rivets.
TITAN = [
    "........HHHH........",
    ".......HHHHHH.......",
    "......HHHHHHHH......",
    "..1...HsDDDDsH...1..",
    ".1H1.HsDDDDDDsH.1H1.",
    ".HHH2HsgDDDDgsH2HHH.",
    ".HHH2HsDDDDDDDsH2HHH",
    ".Hhh2HHDDDDDHH2HH2hH".replace("2HH2", "shHs"),
    "1HhhshHHHHHHHHhshhH1",
    "1HhssshhhhhhhhssshH1",
    "1HhsdsddddddddsdshH1",
    ".HssdsddddddddsdssH.",
    ".HssHHhhhddhhhHHssH.",
    "..ssH33h....h33Hs...".replace("s...", "sH.."),
    "...33...........33..",
    "....................",
]

# ── Style 4: Phoenix MkX ───────────────────────────────────────────────
# Ceremonial flagship: layered feathered wings, radiant core, crown crest
# over the canopy. The endgame vanity hull.
PHOENIX = [
    ".........33.........",
    "........3333........",
    ".........11.........",
    "........H11H........",
    ".......HH11HH.......",
    "......H1HDDH1H......",
    ".....HH1sggs1HH.....",
    "....1HHHsDDsHHH1....",
    "...1H1HHsDDsHH1H1...",
    "..1HH1H1HDDH1H1HH1..",
    ".1HHhH1HHHHHH1HhhH1.",
    "1HHshh1HsDDsH1hhsHH1",
    "1Hhssh.H1HH1H.hsshH1",
    ".3....1.sHHs.1....3.",
    ".......1.HH.1.......",
    "..........33........",
]

STYLES = [
    ("CYBER", CYBER),
    ("VIPER", VIPER),
    ("MANTA", MANTA),
    ("TITAN", TITAN),
    ("PHOENIX", PHOENIX),
]


def emit(name, rows):
    assert len(rows) == H, f"{name}: need {H} rows, got {len(rows)}"
    vals = []
    for r, row in enumerate(rows):
        assert len(row) <= W, f"{name} row {r} too wide ({len(row)})"
        row = row.ljust(W, ".")
        for ch in row:
            assert ch in MAP, f"{name} row {r}: bad char {ch!r}"
            vals.append(MAP[ch])
    print(f"    /* {name} */")
    print("    {")
    for y in range(H):
        line = ", ".join(str(v) for v in vals[y * W:(y + 1) * W])
        print(f"        {line},")
    print("    },")


def main():
    for name, rows in STYLES:
        emit(name, rows)


if __name__ == "__main__":
    main()
