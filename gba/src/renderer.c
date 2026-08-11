#include "renderer.h"
#include <string.h>

EWRAM_BSS static u8 s_back_buffer[SCREEN_WIDTH * SCREEN_HEIGHT] __attribute__((aligned(4)));

void gfx_init(void) {
    REG_DISPCNT = DCNT_MODE4 | DCNT_BG2;
    // vid_page is the VRAM page we render the completed back-buffer into.
    // Start with the page bit clear (displaying MEM_VRAM / 0x06000000), so we
    // draw into MEM_VRAM_BACK (0x0600A000) as the off-screen page.
    vid_page = (COLOR*)MEM_VRAM_BACK;
    tonccpy(pal_bg_mem, master_palette, sizeof(master_palette));
    memset(s_back_buffer, PAL_SPACE_BLACK, sizeof(s_back_buffer));
}

void gfx_flip(void) {
    VBlankIntrWait();
    u16* vram = (u16*)vid_page;
    const u16* src = (const u16*)s_back_buffer;
    /* Copy the frame with the CPU, not with a single blocking DMA3 burst.
     * A 38 KB DMA transfer halts the CPU for ~2+ ms while DirectSound FIFO A
     * holds only ~16-32 samples (~0.5-1 ms at 32,768 Hz): the timer-driven
     * audio IRQ could not feed the FIFO during the copy, so the output
     * collapsed into a ~60 Hz crackle every frame.  GBATEK's DMA section
     * explicitly warns that long DMA bursts starve the sound FIFO.  tonccpy
     * copies word-wise from IWRAM so the audio interrupt can pre-empt it and
     * keep the FIFO fed. */
    tonccpy(vram, src, SCREEN_WIDTH * SCREEN_HEIGHT);

    // Flip the display page: toggle bit 4 of REG_DISPCNT so the frame we just
    // rendered becomes visible, then retarget vid_page at the other (now
    // off-screen) page for the next frame. Without this the display always
    // shows page 0 while we keep writing to page 1 -> black screen.
    REG_DISPCNT ^= DCNT_PAGE;
    vid_page = (COLOR*)((REG_DISPCNT & DCNT_PAGE) ? MEM_VRAM : MEM_VRAM_BACK);
}

void gfx_clear(u8 color) {
    memset(s_back_buffer, color, sizeof(s_back_buffer));
}

void gfx_draw_pixel(int x, int y, u8 color) {
    if ((unsigned)x < SCREEN_WIDTH && (unsigned)y < SCREEN_HEIGHT) {
        s_back_buffer[y * SCREEN_WIDTH + x] = color;
    }
}

void gfx_fill_rect(int x, int y, int w, int h, u8 color) {
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT || w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > SCREEN_WIDTH ? SCREEN_WIDTH : x + w;
    int y1 = y + h > SCREEN_HEIGHT ? SCREEN_HEIGHT : y + h;
    int span = x1 - x0;
    if (span <= 0) return;
    
    for (int py = y0; py < y1; py++) {
        memset(&s_back_buffer[py * SCREEN_WIDTH + x0], color, span);
    }
}

void gfx_draw_rect(int x, int y, int w, int h, u8 color) {
    if (w <= 0 || h <= 0) return;
    gfx_fill_rect(x, y, w, 1, color);
    gfx_fill_rect(x, y + h - 1, w, 1, color);
    gfx_fill_rect(x, y, 1, h, color);
    gfx_fill_rect(x + w - 1, y, 1, h, color);
}

void gfx_draw_glass_card(int x, int y, int w, int h, u8 border_color, u8 fill_color) {
    if (w < 4 || h < 4) return;
    gfx_fill_rect(x + 1, y + 1, w - 2, h - 2, fill_color);
    gfx_draw_rect(x + 1, y, w - 2, 1, border_color);
    gfx_draw_rect(x + 1, y + h - 1, w - 2, 1, border_color);
    gfx_draw_rect(x, y + 1, 1, h - 2, border_color);
    gfx_draw_rect(x + w - 1, y + 1, 1, h - 2, border_color);
}

void gfx_draw_sprite(int x, int y, int w, int h, const u8* data) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > SCREEN_WIDTH ? SCREEN_WIDTH : x + w;
    int y1 = y + h > SCREEN_HEIGHT ? SCREEN_HEIGHT : y + h;
    
    for (int py = y0; py < y1; py++) {
        int sy = py - y;
        const u8* src_row = &data[sy * w];
        u8* dst_row = &s_back_buffer[py * SCREEN_WIDTH];
        for (int px = x0; px < x1; px++) {
            int sx = px - x;
            u8 pix = src_row[sx];
            if (pix != 0) {
                dst_row[px] = pix;
            }
        }
    }
}

void gfx_draw_char(int x, int y, char c, u8 color) {
    if (c < 32 || c > 127) c = '?';
    const u8* glyph = font_5x7[c - 32];
    for (int r = 0; r < 7; r++) {
        int py = y + r;
        if ((unsigned)py >= SCREEN_HEIGHT) continue;
        u8 row = glyph[r];
        u8* dst = &s_back_buffer[py * SCREEN_WIDTH];
        for (int col = 0; col < 5; col++) {
            int px = x + col;
            if ((unsigned)px < SCREEN_WIDTH && (row & (1 << (4 - col)))) {
                dst[px] = color;
            }
        }
    }
}

void gfx_draw_text(int x, int y, const char* str, u8 color) {
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

void gfx_draw_text_shadow(int x, int y, const char* str, u8 color, u8 shadow_color) {
    gfx_draw_text(x + 1, y + 1, str, shadow_color);
    gfx_draw_text(x, y, str, color);
}

void gfx_draw_text_centered(int x, int y, int w, const char* str, u8 color) {
    if (!str) return;
    int len = strlen(str);
    int text_w = len * 6 - 1;
    int start_x = x + (w - text_w) / 2;
    gfx_draw_text(start_x, y, str, color);
}

void gfx_draw_button(int x, int y, int w, int h, const char* label, bool selected) {
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

void gfx_draw_badge(int x, int y, const char* label, u8 accent_color) {
    int len = strlen(label);
    int w = len * 6 + 6;
    int h = 10;
    gfx_draw_glass_card(x, y, w, h, accent_color, 14);
    gfx_draw_text(x + 3, y + 2, label, accent_color);
}

void gfx_draw_swatch(int x, int y, int size, u8 color_idx, const char* label) {
    gfx_draw_glass_card(x, y, size, size, PAL_TEXT_WHITE, color_idx);
    if (label) {
        gfx_draw_text(x + size + 4, y + (size - 7) / 2, label, PAL_TEXT_WHITE);
    }
}

void gfx_draw_progress_bar(int x, int y, int w, int h, int current, int max_val, u8 fg_color, u8 bg_color) {
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
