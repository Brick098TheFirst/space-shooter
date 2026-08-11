#include "renderer.h"
#include <string.h>

EWRAM_BSS static u8 s_back_buffer[SCREEN_WIDTH * SCREEN_HEIGHT] __attribute__((aligned(4)));
EWRAM_BSS u8 gfx_static_layer[SCREEN_WIDTH * SCREEN_HEIGHT] __attribute__((aligned(4)));

static u8* s_rt = s_back_buffer;

void gfx_set_target(u8* buf) {
    s_rt = buf ? buf : s_back_buffer;
}

void gfx_apply_static(void) {
    dma3_cpy(s_back_buffer, gfx_static_layer, sizeof(gfx_static_layer));
}

static u8 s_laser_std[NUM_LASERS][4*10];
static u8 s_laser_heavy[NUM_LASERS][6*14];
static bool s_laser_ready = false;

/* Bright shades from the seven ship-paint ramps.  These shared spectrum
 * colours keep rainbow lasers, trails and the animated paint visually
 * consistent without changing the global Mode 4 palette at runtime. */
static const u8 s_rainbow_colors[7] = { 70, 50, 66, 62, 54, 58, 78 };

void gfx_init(void) {
    REG_DISPCNT = DCNT_MODE4 | DCNT_BG2;
    vid_page = (COLOR*)MEM_VRAM_BACK;
    tonccpy(pal_bg_mem, master_palette, sizeof(master_palette));
    memset(s_back_buffer, PAL_SPACE_BLACK, sizeof(s_back_buffer));

    // Build the seven static laser variants. Index 7 is drawn as an animated
    // multi-colour spectrum by gfx_draw_laser (the cached pink is a fallback).
    const u8 laser_cols[NUM_LASERS] = { 21, 24, 28, 27, 26, 62, 116, 120 };

    for (int l = 0; l < NUM_LASERS; l++) {
        u8 col = laser_cols[l];
        for (int i = 0; i < 4*10; i++) {
            u8 p = spr_laser_standard[i];
            if (p == 21) s_laser_std[l][i] = col;
            else s_laser_std[l][i] = p;
        }
        for (int i = 0; i < 6*14; i++) {
            u8 p = spr_laser_heavy[i];
            if (p == 22 || p == 23) s_laser_heavy[l][i] = col;
            else s_laser_heavy[l][i] = p;
        }
    }
    s_laser_ready = true;
}

void gfx_flip(void) {
    VBlankIntrWait();
    u16* vram = (u16*)vid_page;
    const u16* src = (const u16*)s_back_buffer;
    dma3_cpy(vram, src, (SCREEN_WIDTH * SCREEN_HEIGHT));

    REG_DISPCNT ^= DCNT_PAGE;
    vid_page = (COLOR*)((REG_DISPCNT & DCNT_PAGE) ? MEM_VRAM : MEM_VRAM_BACK);
}

IWRAM_CODE void gfx_clear(u8 color) {
    memset(s_rt, color, SCREEN_WIDTH * SCREEN_HEIGHT);
}

IWRAM_CODE void gfx_draw_pixel(int x, int y, u8 color) {
    if ((unsigned)x < SCREEN_WIDTH && (unsigned)y < SCREEN_HEIGHT) {
        s_rt[y * SCREEN_WIDTH + x] = color;
    }
}

IWRAM_CODE void gfx_fill_rect(int x, int y, int w, int h, u8 color) {
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT || w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > SCREEN_WIDTH ? SCREEN_WIDTH : x + w;
    int y1 = y + h > SCREEN_HEIGHT ? SCREEN_HEIGHT : y + h;
    int span = x1 - x0;
    if (span <= 0) return;
    
    u8* dst = &s_rt[y0 * SCREEN_WIDTH + x0];
    for (int py = y0; py < y1; py++) {
        memset(dst, color, span);
        dst += SCREEN_WIDTH;
    }
}

IWRAM_CODE void gfx_draw_rect(int x, int y, int w, int h, u8 color) {
    if (w <= 0 || h <= 0) return;
    gfx_fill_rect(x, y, w, 1, color);
    gfx_fill_rect(x, y + h - 1, w, 1, color);
    gfx_fill_rect(x, y, 1, h, color);
    gfx_fill_rect(x + w - 1, y, 1, h, color);
}

IWRAM_CODE void gfx_draw_glass_card(int x, int y, int w, int h, u8 border_color, u8 fill_color) {
    if (w < 4 || h < 4) return;
    gfx_fill_rect(x + 1, y + 1, w - 2, h - 2, fill_color);
    gfx_draw_rect(x + 1, y, w - 2, 1, border_color);
    gfx_draw_rect(x + 1, y + h - 1, w - 2, 1, border_color);
    gfx_draw_rect(x, y + 1, 1, h - 2, border_color);
    gfx_draw_rect(x + w - 1, y + 1, 1, h - 2, border_color);
}

IWRAM_CODE void gfx_draw_sprite(int x, int y, int w, int h, const u8* data) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > SCREEN_WIDTH ? SCREEN_WIDTH : x + w;
    int y1 = y + h > SCREEN_HEIGHT ? SCREEN_HEIGHT : y + h;
    if (x0 >= x1 || y0 >= y1) return;

    int span_x = x1 - x0;
    int src_skip = w - span_x;
    const u8* src = &data[(y0 - y) * w + (x0 - x)];
    u8* dst = &s_rt[y0 * SCREEN_WIDTH + x0];
    int dst_skip = SCREEN_WIDTH - span_x;

    for (int py = y0; py < y1; py++) {
        int count = span_x;
        while (count--) {
            u8 pix = *src++;
            if (pix != 0) {
                *dst = pix;
            }
            dst++;
        }
        src += src_skip;
        dst += dst_skip;
    }
}

// Dedicated ship renderer supporting static and animated dynamic Rainbow Chroma paint
IWRAM_CODE void gfx_draw_ship(int x, int y, int accent_idx, int anim_frame) {
    if (accent_idx < 0 || accent_idx >= NUM_ACCENTS) accent_idx = 1;

    if (accent_idx != 8) {
        // Standard static paint
        gfx_draw_sprite(x, y, 20, 16, spr_ship[accent_idx]);
        return;
    }

    // Rainbow Chroma Paint: animated flowing spectrum wave!
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + 20 > SCREEN_WIDTH ? SCREEN_WIDTH : x + 20;
    int y1 = y + 16 > SCREEN_HEIGHT ? SCREEN_HEIGHT : y + 16;
    if (x0 >= x1 || y0 >= y1) return;

    const u8 rainbow_accents[7] = { 5, 0, 4, 3, 1, 2, 7 }; // Crimson, Orange, Gold, Mint, Cyan, Violet, Pink
    const u8* src = spr_ship[8];

    for (int py = y0; py < y1; py++) {
        int sy = py - y;
        int dst_idx = py * SCREEN_WIDTH + x0;
        int src_idx = sy * 20 + (x0 - x);

        for (int px = x0; px < x1; px++) {
            int sx = px - x;
            u8 pix = src[src_idx++];
            if (pix != 0) {
                if (pix >= 240 && pix <= 243) {
                    // Rainbow accent pixel: moving chromatic wave across ship wings & fuselage
                    int shade = pix - 240;
                    int phase = ((anim_frame >> 1) + sx * 2 + sy) % 28;
                    int color_step = phase / 4; // 0..6
                    u8 base_acc = rainbow_accents[color_step];
                    s_rt[dst_idx] = 48 + base_acc * 4 + shade;
                } else {
                    s_rt[dst_idx] = pix;
                }
            }
            dst_idx++;
        }
    }
}

/* Enemy fighters use the exact player silhouette and Crimson paint, rotated
 * 180 degrees so their cannons and flight direction face down-screen. */
IWRAM_CODE void gfx_draw_enemy_ship(int x, int y) {
    const u8* src = spr_ship[5];
    for (int sy = 0; sy < 16; sy++) {
        int py = y + sy;
        if ((unsigned)py >= SCREEN_HEIGHT) continue;
        for (int sx = 0; sx < 20; sx++) {
            int px = x + sx;
            if ((unsigned)px >= SCREEN_WIDTH) continue;
            u8 pix = src[(15 - sy) * 20 + (19 - sx)];
            if (pix != 0) s_rt[py * SCREEN_WIDTH + px] = pix;
        }
    }
}

/* Draw a complete player-style laser.  Rainbow Laser is deliberately rendered
 * here rather than cached: each coloured edge pixel gets a moving spectrum
 * phase, making several colours visible in the same bolt. */
IWRAM_CODE void gfx_draw_laser(int center_x, int center_y, bool heavy,
                               int laser_idx, int anim_frame, bool downward) {
    if (laser_idx < 0 || laser_idx >= NUM_LASERS) laser_idx = 0;

    int w = heavy ? 6 : 4;
    int h = heavy ? 14 : 10;
    int x = center_x - w / 2;
    int y = center_y - h / 2;
    const u8* src = heavy ? gfx_get_laser_heavy_sprite(laser_idx)
                          : gfx_get_laser_standard_sprite(laser_idx);

    if (laser_idx != 7 && !downward) {
        gfx_draw_sprite(x, y, w, h, src);
        return;
    }

    for (int draw_y = 0; draw_y < h; draw_y++) {
        int py = y + draw_y;
        if ((unsigned)py >= SCREEN_HEIGHT) continue;
        int sy = downward ? (h - 1 - draw_y) : draw_y;
        for (int sx = 0; sx < w; sx++) {
            int px = x + sx;
            if ((unsigned)px >= SCREEN_WIDTH) continue;
            u8 pix = src[sy * w + sx];
            if (pix == 0) continue;
            if (laser_idx == 7 && pix != PAL_TEXT_WHITE) {
                int phase = (anim_frame >> 2) + sy * 2 + sx;
                pix = s_rainbow_colors[phase % 7];
            }
            s_rt[py * SCREEN_WIDTH + px] = pix;
        }
    }
}

IWRAM_CODE void gfx_draw_char(int x, int y, char c, u8 color) {
    if (c < 32 || c > 127) c = '?';
    if ((unsigned)x <= SCREEN_WIDTH - 5 && (unsigned)y <= SCREEN_HEIGHT - 7) {
        const u8* glyph = font_5x7[c - 32];
        u8* dst = &s_rt[y * SCREEN_WIDTH + x];
        for (int r = 0; r < 7; r++) {
            u8 row = glyph[r];
            if (row & 0x10) dst[0] = color;
            if (row & 0x08) dst[1] = color;
            if (row & 0x04) dst[2] = color;
            if (row & 0x02) dst[3] = color;
            if (row & 0x01) dst[4] = color;
            dst += SCREEN_WIDTH;
        }
        return;
    }
    if (x < -5 || x >= SCREEN_WIDTH || y < -7 || y >= SCREEN_HEIGHT) return;
    const u8* glyph = font_5x7[c - 32];
    for (int r = 0; r < 7; r++) {
        int py = y + r;
        if ((unsigned)py >= SCREEN_HEIGHT) continue;
        u8 row = glyph[r];
        for (int col = 0; col < 5; col++) {
            int px = x + col;
            if ((unsigned)px < SCREEN_WIDTH && (row & (1 << (4 - col)))) {
                s_rt[py * SCREEN_WIDTH + px] = color;
            }
        }
    }
}

IWRAM_CODE void gfx_draw_text(int x, int y, const char* str, u8 color) {
    if (!str) return;
    int cur_x = x;
    while (*str) {
        if (*str == '\n') {
            cur_x = x;
            y += 9;
        } else {
            gfx_draw_char(cur_x, y, *str, color);
            cur_x += 6;
        }
        str++;
    }
}

IWRAM_CODE void gfx_draw_text_shadow(int x, int y, const char* str, u8 color, u8 shadow_color) {
    gfx_draw_text(x + 1, y + 1, str, shadow_color);
    gfx_draw_text(x, y, str, color);
}

IWRAM_CODE void gfx_draw_text_centered(int x, int y, int w, const char* str, u8 color) {
    if (!str) return;
    int len = strlen(str);
    int text_w = len * 6 - 1;
    int start_x = x + (w - text_w) / 2;
    gfx_draw_text(start_x, y, str, color);
}

IWRAM_CODE void gfx_draw_button(int x, int y, int w, int h, const char* label, bool selected) {
    u8 bg = selected ? PAL_BTN_HOVER : PAL_BTN_BG;
    u8 border = selected ? PAL_TEXT_WHITE : PAL_BTN_BORDER;
    u8 text_col = selected ? PAL_TEXT_WHITE : PAL_TEXT_CYAN;
    
    gfx_draw_glass_card(x, y, w, h, border, bg);
    
    if (selected) {
        gfx_fill_rect(x + 2, y + 2, 2, h - 4, PAL_TEXT_CYAN);
        gfx_draw_char(x + 5, y + (h - 7) / 2, '>', PAL_TEXT_CYAN);
        gfx_draw_text_centered(x + 6, y + (h - 7) / 2, w - 6, label, text_col);
    } else {
        gfx_draw_text_centered(x, y + (h - 7) / 2, w, label, text_col);
    }
}

IWRAM_CODE void gfx_draw_badge(int x, int y, const char* label, u8 accent_color) {
    int len = strlen(label);
    int w = len * 6 + 6;
    int h = 10;
    gfx_draw_glass_card(x, y, w, h, accent_color, 14);
    gfx_draw_text(x + 3, y + 2, label, accent_color);
}

IWRAM_CODE void gfx_draw_swatch(int x, int y, int size, u8 color_idx, const char* label) {
    gfx_draw_glass_card(x, y, size, size, PAL_TEXT_WHITE, color_idx);
    if (label) {
        gfx_draw_text(x + size + 4, y + (size - 7) / 2, label, PAL_TEXT_WHITE);
    }
}

IWRAM_CODE void gfx_draw_progress_bar(int x, int y, int w, int h, int current, int max_val, u8 fg_color, u8 bg_color) {
    gfx_draw_rect(x, y, w, h, bg_color);
    if (max_val <= 0) return;
    int fill_w = (current * (w - 2)) / max_val;
    if (fill_w > w - 2) fill_w = w - 2;
    if (fill_w > 0) {
        gfx_fill_rect(x + 1, y + 1, fill_w, h - 2, fg_color);
    }
}

u8 gfx_get_accent_color(int accent_idx) {
    if (accent_idx == 8) {
        const u8 rainbow_cols[7] = { 70, 50, 66, 62, 54, 58, 78 };
        return rainbow_cols[(REG_VCOUNT / 4) % 7];
    }
    if (accent_idx < 0 || accent_idx >= NUM_ACCENTS) accent_idx = 1;
    return 48 + accent_idx * 4 + 2;
}

u8 gfx_get_rainbow_color(int phase) {
    if (phase < 0) phase = -phase;
    return s_rainbow_colors[phase % 7];
}

u8 gfx_get_trail_color_animated(int trail_idx, int anim_frame) {
    if (trail_idx == 7) {
        return gfx_get_rainbow_color(anim_frame >> 2);
    }
    if (trail_idx < 0 || trail_idx >= NUM_TRAILS) trail_idx = 1;
    return 184 + trail_idx * 3 + 1;
}

u8 gfx_get_trail_color(int trail_idx) {
    return gfx_get_trail_color_animated(trail_idx, 0);
}

u8 gfx_get_laser_color(int laser_idx) {
    const u8 cols[NUM_LASERS] = { 21, 24, 28, 27, 26, 62, 116, 120 };
    if (laser_idx < 0 || laser_idx >= NUM_LASERS) laser_idx = 0;
    return cols[laser_idx];
}

const char* gfx_get_accent_name(int accent_idx) {
    switch (accent_idx) {
        case 0: return "Solar Orange";
        case 1: return "Ion Cyan";
        case 2: return "Nova Violet";
        case 3: return "Plasma Mint";
        case 4: return "Pulsar Gold";
        case 5: return "Crimson Void";
        case 6: return "Obsidian Dark";
        case 7: return "Quantum Neon";
        case 8: return "Rainbow Prism";
        default: return "Custom Paint";
    }
}

const char* gfx_get_accent_desc(int accent_idx) {
    switch (accent_idx) {
        case 0: return "Fleet amber hull";
        case 1: return "Cadet issue blue";
        case 2: return "Nebula purple coat";
        case 3: return "Bio-polymer mint";
        case 4: return "Gilded royal armor";
        case 5: return "Raider crimson red";
        case 6: return "Stealth gray alloy";
        case 7: return "Zero-point pink glow";
        case 8: return "Moving rainbow wave";
        default: return "Ship paint finish";
    }
}

const char* gfx_get_trail_name(int trail_idx) {
    switch (trail_idx) {
        case 0: return "Ember Fire";
        case 1: return "Ion Cyan";
        case 2: return "Nova Purple";
        case 3: return "Aurora Mint";
        case 4: return "Solar Gold";
        case 5: return "Crimson Blaze";
        case 6: return "Void Shadow";
        case 7: return "Rainbow Trail";
        default: return "Drive Wake";
    }
}

const char* gfx_get_trail_desc(int trail_idx) {
    switch (trail_idx) {
        case 0: return "Combustion burner";
        case 1: return "Sub-light ion wake";
        case 2: return "Exotic particle jet";
        case 3: return "Plasma engine plume";
        case 4: return "Photon flare thrust";
        case 5: return "Heavy afterburner";
        case 6: return "Tachyon dark plume";
        case 7: return "Animated spectrum wake";
        default: return "Engine exhaust";
    }
}

const char* gfx_get_weapon_name(WeaponRig rig) {
    switch (rig) {
        case WEAPON_TWIN:    return "Twin Cannons";
        case WEAPON_SPREAD:  return "Spread Cannon";
        case WEAPON_FOCUSED: return "Focused Beam";
        case WEAPON_TRIPLE:  return "Triple Blaster";
        case WEAPON_PLASMA:  return "Plasma Flak";
        case WEAPON_QUANTUM: return "Quantum Core";
        default:             return "Blaster";
    }
}

const char* gfx_get_weapon_desc(WeaponRig rig) {
    switch (rig) {
        case WEAPON_TWIN:    return "Dual balanced shot";
        case WEAPON_SPREAD:  return "3-way broad salvo";
        case WEAPON_FOCUSED: return "Heavy piercing beam";
        case WEAPON_TRIPLE:  return "Tri-barrel barrage";
        case WEAPON_PLASMA:  return "Twin explosive flak";
        case WEAPON_QUANTUM: return "High-energy annihilator";
        default:             return "Main cannons";
    }
}

const char* gfx_get_laser_name(int laser_idx) {
    switch (laser_idx) {
        case 0: return "Ion Cyan";
        case 1: return "Solar Gold";
        case 2: return "Nebula Violet";
        case 3: return "Toxic Mint";
        case 4: return "Crimson Fury";
        case 5: return "Emerald Surge";
        case 6: return "Void Shadow";
        case 7: return "Rainbow Laser";
        default: return "Beam Crystal";
    }
}

const char* gfx_get_laser_desc(int laser_idx) {
    switch (laser_idx) {
        case 0: return "Standard blue beam";
        case 1: return "Amber photon laser";
        case 2: return "Harmonic purple bolt";
        case 3: return "Bio-toxin emerald glow";
        case 4: return "Thermal overcharge pulse";
        case 5: return "Gamma radiation green";
        case 6: return "Dark matter particle";
        case 7: return "Animated spectrum beam";
        default: return "Laser wavelength";
    }
}

const char* gfx_get_diff_name(Difficulty diff) {
    switch (diff) {
        case DIFF_CADET: return "Cadet";
        case DIFF_ACE:   return "Ace";
        default:         return "Pilot";
    }
}

const u8* gfx_get_laser_standard_sprite(int laser_idx) {
    if (!s_laser_ready) return spr_laser_standard;
    if (laser_idx < 0 || laser_idx >= NUM_LASERS) laser_idx = 0;
    return s_laser_std[laser_idx];
}

const u8* gfx_get_laser_heavy_sprite(int laser_idx) {
    if (!s_laser_ready) return spr_laser_heavy;
    if (laser_idx < 0 || laser_idx >= NUM_LASERS) laser_idx = 0;
    return s_laser_heavy[laser_idx];
}
