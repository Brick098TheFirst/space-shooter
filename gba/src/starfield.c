#include "starfield.h"
#include "renderer.h"
#include <stdlib.h>

#define NUM_STARS 40
#define NUM_NEBULAE 4

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

EWRAM_BSS static Star s_stars[NUM_STARS];
EWRAM_BSS static Nebula s_nebulae[NUM_NEBULAE];

void starfield_init(void) {
    for (int i = 0; i < NUM_STARS; i++) {
        s_stars[i].x = (rand() % SCREEN_WIDTH) << 8;
        s_stars[i].y = (rand() % SCREEN_HEIGHT) << 8;
        int layer = i % 3;
        if (layer == 0) {
            s_stars[i].speed = (rand() % 40) + 30;
            s_stars[i].base_color = 3;
        } else if (layer == 1) {
            s_stars[i].speed = (rand() % 80) + 90;
            s_stars[i].base_color = 7;
        } else {
            s_stars[i].speed = (rand() % 120) + 200;
            s_stars[i].base_color = 11;
        }
        s_stars[i].phase = rand() % 256;
    }

    for (int i = 0; i < NUM_NEBULAE; i++) {
        s_nebulae[i].x = rand() % (SCREEN_WIDTH - 60) + 30;
        s_nebulae[i].y = rand() % SCREEN_HEIGHT;
        s_nebulae[i].radius = (rand() % 16) + 18;
        s_nebulae[i].speed = (rand() % 20) + 15;
        s_nebulae[i].color = (i % 2 == 0) ? 2 : 4;
    }
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

    for (int i = 0; i < NUM_NEBULAE; i++) {
        s_nebulae[i].y += (s_nebulae[i].speed >> 5);
        if (s_nebulae[i].y >= SCREEN_HEIGHT + s_nebulae[i].radius) {
            s_nebulae[i].y = -s_nebulae[i].radius;
            s_nebulae[i].x = rand() % (SCREEN_WIDTH - 60) + 30;
        }
    }
}

void starfield_draw_base(int offset_x, int offset_y) {
    gfx_clear(PAL_SPACE_BLACK);

    for (int i = 0; i < NUM_NEBULAE; i++) {
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
        if ((unsigned)sx < SCREEN_WIDTH && (unsigned)sy < SCREEN_HEIGHT) {
            u8 col = s_stars[i].base_color;
            if (col >= 11) {
                if ((s_stars[i].phase & 0x40) == 0) col += 1;
            }
            gfx_draw_pixel(sx, sy, col);
        }
    }
}
