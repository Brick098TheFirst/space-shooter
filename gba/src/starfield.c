#include "starfield.h"
#include "renderer.h"
#include <stdlib.h>

#define NUM_STARS 40
#define NUM_NEBULAE 6
#define NUM_MOTES 24

typedef struct {
    int x;
    int y;
    int speed;
    u8 base_color;
    u8 phase;
} Star;

typedef struct {
    int x;
    int y;
    int radius;
    int speed;
    u8 color;
} Nebula;

/* The per-theme drifting layer: dust, sleet, embers, rust flecks... Same
 * struct for every theme, only the motion rule and colour change. */
typedef struct {
    int x;      /* 8.8 */
    int y;      /* 8.8 */
    int vx;     /* 8.8 */
    int vy;     /* 8.8 */
    u8  color;
    u8  size;
} Mote;

/* ── Theme table ──────────────────────────────────────────────────────────
 * Each kingdom gets its own sky.  `mote_kind` picks the motion rule in
 * starfield_update(); `mote_count` of 0 disables the layer entirely. */
typedef enum {
    MOTE_NONE = 0,
    MOTE_DUST,     /* slow warm grit drifting down-right   */
    MOTE_SLEET,    /* fast pale streaks, steep angle        */
    MOTE_EMBER,    /* glowing flecks RISING against gravity */
    MOTE_FLECK,    /* tumbling rust chips                   */
    MOTE_SPARK,    /* vault: rare cold sparks, near-still    */
    MOTE_FOLD,     /* reality: violet shards, sideways slip  */
    MOTE_ECHO      /* horizon: paired motes cross in reverse */
} MoteKind;

typedef struct {
    u8  clear;            /* backdrop fill                          */
    u8  neb_a, neb_b;     /* the two nebula tints                   */
    u8  neb_count;        /* how many blobs (0..NUM_NEBULAE)        */
    u8  neb_min_r, neb_span_r;
    u8  star_far, star_mid, star_near;
    u8  mote_kind;
    u8  mote_count;
    u8  mote_color;
} SfTheme;

static const SfTheme s_themes[SF_THEME_COUNT] = {
    /* SF_THEME_ARCADE - byte-for-byte the original arcade backdrop:
     * black clear, 4 blobs alternating palette 2 / 4, radius 18..33. */
    {  0,   2,   4, 4, 18, 16,   3,   7,  11,  MOTE_NONE,   0,  0 },

    /* Nebula tints are deliberately only a shade or two off the clear
     * colour: they are backdrop, and anything brighter reads as an obstacle
     * the player should be dodging. The theme's character comes from the
     * star palette and its drifting layer, not from big bright blobs. */

    /* SF_THEME_BELT - the Chubb Belt: warm dust and a lot of it. */
    { 14, 175, 174, 5, 16, 18,   3,  37, 170,  MOTE_DUST,  20, 169 },

    /* SF_THEME_RUST - the Rust Yards: brown iron haze. Deliberately browner
     * than Ember Reach below, which is the red one. */
    { 175, 174, 131, 6, 20, 20,  35,  49, 185,  MOTE_FLECK, 18, 139 },

    /* SF_THEME_ICE - the Ice Fields: pale blue whiteout, sleet. */
    { 32,  33,  39, 6, 22, 22,  18,  42, 165,  MOTE_SLEET, 24, 189 },

    /* SF_THEME_SCRAPLINE - sodium-green glare over dead metal. */
    { 183, 182, 210, 5, 18, 20,  73,  61, 180,  MOTE_FLECK, 16, 177 },

    /* SF_THEME_EMBER - Ember Reach: red heat, embers rising. */
    { 128, 130, 134, 6, 22, 22, 133, 141, 186,  MOTE_EMBER, 22, 147 },

    /* SF_THEME_VAULT - the Cold Vault: almost nothing. No stars to speak
     * of, two barely-there blobs, the occasional cold spark. */
    {  1,  81,  83, 2, 26, 10,  81,  84,  93,  MOTE_SPARK,  8,  22 },

    /* SF_THEME_REALITY - the Reality Gate: folded violet space. */
    { 112, 80, 126, 6, 20, 24,  45, 115, 192,  MOTE_FOLD,  20, 123 },

    /* SF_THEME_HORIZON - beyond the Queen: near-black teal space where
     * paired time motes run sideways against the ordinary star scroll. */
    {  96, 97, 113, 4, 24, 18,  22,  53, 201,  MOTE_ECHO,  24,  58 },
};

EWRAM_BSS static Star s_stars[NUM_STARS];
EWRAM_BSS static Nebula s_nebulae[NUM_NEBULAE];
EWRAM_BSS static Mote s_motes[NUM_MOTES];

static int s_theme = SF_THEME_ARCADE;
static int s_neb_count = 4;
static int s_mote_count = 0;

static const SfTheme* theme(void) {
    int t = s_theme;
    if (t < 0 || t >= SF_THEME_COUNT) t = SF_THEME_ARCADE;
    return &s_themes[t];
}

/* Re-seed the stars for the active theme. Layer speeds are unchanged from
 * the original arcade field so nothing about the feel of the scroll moves;
 * only the colours differ. */
static void seed_stars(void) {
    const SfTheme* th = theme();
    for (int i = 0; i < NUM_STARS; i++) {
        s_stars[i].x = (rand() % SCREEN_WIDTH) << 8;
        s_stars[i].y = (rand() % SCREEN_HEIGHT) << 8;
        int layer = i % 3;
        if (layer == 0) {
            s_stars[i].speed = (rand() % 40) + 30;
            s_stars[i].base_color = th->star_far;
        } else if (layer == 1) {
            s_stars[i].speed = (rand() % 80) + 90;
            s_stars[i].base_color = th->star_mid;
        } else {
            s_stars[i].speed = (rand() % 120) + 200;
            s_stars[i].base_color = th->star_near;
        }
        s_stars[i].phase = rand() % 256;
    }
}

static void seed_nebulae(void) {
    const SfTheme* th = theme();
    s_neb_count = th->neb_count;
    if (s_neb_count > NUM_NEBULAE) s_neb_count = NUM_NEBULAE;
    for (int i = 0; i < s_neb_count; i++) {
        s_nebulae[i].x = rand() % (SCREEN_WIDTH - 60) + 30;
        s_nebulae[i].y = rand() % SCREEN_HEIGHT;
        s_nebulae[i].radius = (rand() % (th->neb_span_r ? th->neb_span_r : 1)) + th->neb_min_r;
        s_nebulae[i].speed = (rand() % 20) + 15;
        s_nebulae[i].color = (i % 2 == 0) ? th->neb_a : th->neb_b;
    }
}

/* Give one mote a fresh position + velocity for the active theme. `top`
 * re-launches it from the edge it enters from. */
static void seed_mote(Mote* m, bool anywhere) {
    const SfTheme* th = theme();
    m->color = th->mote_color;
    m->size = 1;
    switch (th->mote_kind) {
        case MOTE_DUST:
            m->x = (rand() % SCREEN_WIDTH) << 8;
            m->y = anywhere ? ((rand() % SCREEN_HEIGHT) << 8) : -(rand() % 800);
            m->vx = 20 + (rand() % 40);
            m->vy = 50 + (rand() % 70);
            break;
        case MOTE_SLEET:
            m->x = (rand() % SCREEN_WIDTH) << 8;
            m->y = anywhere ? ((rand() % SCREEN_HEIGHT) << 8) : -(rand() % 600);
            m->vx = -(60 + (rand() % 60));
            m->vy = 260 + (rand() % 200);
            m->size = 2;
            break;
        case MOTE_EMBER:
            m->x = (rand() % SCREEN_WIDTH) << 8;
            m->y = anywhere ? ((rand() % SCREEN_HEIGHT) << 8)
                            : ((SCREEN_HEIGHT + (rand() % 24)) << 8);
            m->vx = (rand() % 60) - 30;
            m->vy = -(60 + (rand() % 90));      /* embers rise */
            break;
        case MOTE_FLECK:
            m->x = (rand() % SCREEN_WIDTH) << 8;
            m->y = anywhere ? ((rand() % SCREEN_HEIGHT) << 8) : -(rand() % 700);
            m->vx = (rand() % 120) - 60;
            m->vy = 90 + (rand() % 130);
            m->size = (rand() & 3) ? 1 : 2;
            break;
        case MOTE_SPARK:
            m->x = (rand() % SCREEN_WIDTH) << 8;
            m->y = anywhere ? ((rand() % SCREEN_HEIGHT) << 8) : -(rand() % 900);
            m->vx = 0;
            m->vy = 24 + (rand() % 30);
            break;
        case MOTE_FOLD:
            m->x = (rand() % SCREEN_WIDTH) << 8;
            m->y = anywhere ? ((rand() % SCREEN_HEIGHT) << 8) : -(rand() % 700);
            m->vx = (rand() & 1) ? (140 + (rand() % 90)) : -(140 + (rand() % 90));
            m->vy = 40 + (rand() % 60);
            m->size = 2;
            break;
        case MOTE_ECHO:
            /* Alternate direction by scanline so the sky looks like two
             * moments sliding through one another. */
            m->x = (rand() % SCREEN_WIDTH) << 8;
            m->y = (rand() % SCREEN_HEIGHT) << 8;
            m->vx = ((m->y >> 11) & 1) ? (220 + rand() % 100) : -(220 + rand() % 100);
            m->vy = (rand() % 25) - 12;
            m->size = (rand() & 1) ? 1 : 2;
            break;
        default:
            m->x = m->y = m->vx = m->vy = 0;
            break;
    }
}

static void seed_motes(void) {
    const SfTheme* th = theme();
    s_mote_count = th->mote_count;
    if (s_mote_count > NUM_MOTES) s_mote_count = NUM_MOTES;
    for (int i = 0; i < s_mote_count; i++) seed_mote(&s_motes[i], true);
}

void starfield_set_theme(int t) {
    if (t < 0 || t >= SF_THEME_COUNT) t = SF_THEME_ARCADE;
    if (t == s_theme) return;
    s_theme = t;
    /* Keep the star POSITIONS (the scroll never jumps) and just recolour
     * them; the nebulae and motes get a full re-roll so the new sky does not
     * inherit the old one's layout. */
    const SfTheme* th = theme();
    for (int i = 0; i < NUM_STARS; i++) {
        int layer = i % 3;
        s_stars[i].base_color = (layer == 0) ? th->star_far
                              : (layer == 1) ? th->star_mid : th->star_near;
    }
    seed_nebulae();
    seed_motes();
}

int starfield_theme(void) { return s_theme; }

void starfield_init(void) {
    s_theme = SF_THEME_ARCADE;
    seed_stars();
    seed_nebulae();
    seed_motes();
}

void starfield_update(void) {
    for (int i = 0; i < NUM_STARS; i++) {
        s_stars[i].y += s_stars[i].speed;
        if ((s_stars[i].y >> 8) >= SCREEN_HEIGHT) {
            s_stars[i].y = 0;
            s_stars[i].x = (rand() % SCREEN_WIDTH) << 8;
        }
        s_stars[i].phase += 4;
    }

    for (int i = 0; i < s_neb_count; i++) {
        s_nebulae[i].y += (s_nebulae[i].speed >> 5);
        if (s_nebulae[i].y >= SCREEN_HEIGHT + s_nebulae[i].radius) {
            s_nebulae[i].y = -s_nebulae[i].radius;
            s_nebulae[i].x = rand() % (SCREEN_WIDTH - 60) + 30;
        }
    }

    for (int i = 0; i < s_mote_count; i++) {
        Mote* m = &s_motes[i];
        m->x += m->vx;
        m->y += m->vy;
        int px = m->x >> 8;
        int py = m->y >> 8;
        /* Wrap horizontally, respawn once it leaves the top or the bottom. */
        if (px < -4) m->x = (SCREEN_WIDTH + 2) << 8;
        else if (px > SCREEN_WIDTH + 4) m->x = -(2 << 8);
        if (py > SCREEN_HEIGHT + 6 || py < -8) seed_mote(m, false);
    }
}

void starfield_draw_base(int offset_x, int offset_y) {
    const SfTheme* th = theme();
    gfx_clear(th->clear);

    for (int i = 0; i < s_neb_count; i++) {
        int nx = s_nebulae[i].x + offset_x;
        int ny = s_nebulae[i].y + offset_y;
        int r = s_nebulae[i].radius;
        u8 col = s_nebulae[i].color;
        for (int dy = -r; dy <= r; dy += 2) {
            int span = r - abs(dy);
            if (span > 0) {
                gfx_fill_rect(nx - span, ny + dy, span * 2, 2, col);
            }
        }
    }

}

void starfield_draw_stars(int offset_x, int offset_y) {
    for (int i = 0; i < NUM_STARS; i++) {
        int sx = (s_stars[i].x >> 8) + offset_x;
        int sy = (s_stars[i].y >> 8) + offset_y;
        if ((unsigned)sx < (unsigned)SCREEN_WIDTH && (unsigned)sy < (unsigned)SCREEN_HEIGHT) {
            u8 col = s_stars[i].base_color;
            if (col >= 11) {
                if ((s_stars[i].phase & 0x40) == 0) col += 1;
            }
            gfx_draw_pixel(sx, sy, col);
        }
    }

    /* The theme's own weather, drawn with the stars so it scrolls with the
     * field rather than sitting in the cached static layer. */
    for (int i = 0; i < s_mote_count; i++) {
        const Mote* m = &s_motes[i];
        int mx = (m->x >> 8) + offset_x;
        int my = (m->y >> 8) + offset_y;
        if (m->size <= 1) {
            if ((unsigned)mx < (unsigned)SCREEN_WIDTH && (unsigned)my < (unsigned)SCREEN_HEIGHT)
                gfx_draw_pixel(mx, my, m->color);
        } else {
            gfx_fill_rect(mx, my, 1, m->size, m->color);
        }
    }
}
