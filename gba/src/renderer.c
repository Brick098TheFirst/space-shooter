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

void gfx_init(void) {
    REG_DISPCNT = DCNT_MODE4 | DCNT_BG2;
    vid_page = (COLOR*)MEM_VRAM_BACK;
    tonccpy(pal_bg_mem, master_palette, sizeof(master_palette));
    memset(s_back_buffer, PAL_SPACE_BLACK, sizeof(s_back_buffer));

    // Build laser colour variants (cyan/gold/violet/mint)
    for (int l = 0; l < NUM_LASERS; l++) {
        u8 col = 21;
        if (l == 1) col = 24;
        else if (l == 2) col = 28;
        else if (l == 3) col = 27;
        // standard: 21 outer, 16 core
        for (int i = 0; i < 4*10; i++) {
            u8 p = spr_laser_standard[i];
            if (p == 21) s_laser_std[l][i] = col;
            else s_laser_std[l][i] = p;
        }
        // heavy: 22,23 outer, 16 core, 22/23 map to same col variant
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
    switch (accent_idx) {
        case 0: return 50;
        case 1: return 54;
        case 2: return 58;
        case 3: return 62;
        default: return 66;
    }
}

u8 gfx_get_trail_color(int trail_idx) {
    switch (trail_idx) {
        case 0: return 69;
        case 1: return 72;
        case 2: return 75;
        default: return 78;
    }
}

const char* gfx_get_accent_name(int accent_idx) {
    switch (accent_idx) {
        case 0: return "Solar orange";
        case 1: return "Ion cyan";
        case 2: return "Nova violet";
        case 3: return "Plasma mint";
        default: return "Pulsar gold";
    }
}

const char* gfx_get_trail_name(int trail_idx) {
    switch (trail_idx) {
        case 0: return "Ember";
        case 1: return "Ion";
        case 2: return "Nova";
        default: return "Aurora";
    }
}

const char* gfx_get_weapon_name(WeaponRig rig) {
    switch (rig) {
        case WEAPON_FOCUSED: return "Focused beam";
        case WEAPON_TWIN:    return "Twin cannons";
        default:             return "Spread cannons";
    }
}

const char* gfx_get_diff_name(Difficulty diff) {
    switch (diff) {
        case DIFF_CADET: return "Cadet";
        case DIFF_ACE:   return "Ace";
        default:         return "Pilot";
    }
}

u8 gfx_get_laser_color(int laser_idx) {
    switch (laser_idx) {
        case 1: return 24;
        case 2: return 28;
        case 3: return 27;
        default: return 21;
    }
}

const char* gfx_get_laser_name(int laser_idx) {
    switch (laser_idx) {
        case 1: return "Gold";
        case 2: return "Violet";
        case 3: return "Mint";
        default: return "Cyan";
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
