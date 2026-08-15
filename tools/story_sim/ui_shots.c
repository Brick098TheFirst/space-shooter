/* Headless renderer for the Android Story Mode screens.
 *
 * Links the REAL menu/renderer/story code against stub audio + online, drives
 * it to a given screen, and dumps the framebuffer as a PPM so the Play tab,
 * Mr Chubbs' shop and the level cards can be eyeballed without a device.
 *
 * Usage: ui_shots <outdir>
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "menu.h"
#include "renderer.h"
#include "game.h"
#include "story.h"
#include "save.h"
#include "starfield.h"
#include "platform_host.h"

extern const u16 master_palette[256];

static void dump(const char* dir, const char* name) {
    int w = host_screen_width(), h = SCREEN_HEIGHT;
    const u8* fb = gfx_get_framebuffer();
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE* f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); return; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        u16 c = master_palette[fb[i]];
        unsigned char px[3] = { (unsigned char)((c & 0x1F) << 3),
                                (unsigned char)(((c >> 5) & 0x1F) << 3),
                                (unsigned char)(((c >> 10) & 0x1F) << 3) };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    printf("wrote %s (%dx%d)\n", path, w, h);
}

static void pump(int frames) {
    for (int i = 0; i < frames; i++) { menu_update(); menu_draw(); }
}

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "/tmp/ui";
    host_set_screen_width(HOST_SCREEN_W_DEFAULT);
    platform_host_init();
    gfx_init();
    starfield_init();
    save_init_defaults();
    story_init();
    game_init();
    menu_init();

    /* PLAY tab, fresh save: Story unlocked, Waves + Endless padlocked. */
    menu_open(SCREEN_MODE_SELECT);
    pump(4);
    dump(dir, "01_play_tab_locked");

    /* PLAY tab after the campaign: everything open. */
    story_free_everything();
    menu_open(SCREEN_MODE_SELECT);
    pump(4);
    dump(dir, "02_play_tab_unlocked");
    g_story.freed = 0;

    /* Level map, mid-campaign. */
    g_story.unlocked = 12; g_story.level = 9; g_story.chubbcoin = 1840;
    g_story.cleared_count = 8;
    for (int lv = 1; lv <= 8; lv++) g_story.cleared[(lv-1)>>3] |= (u8)(1u<<((lv-1)&7));
    menu_open(SCREEN_STORY_MAP);
    pump(20);
    dump(dir, "03_story_map");

    /* Mr Chubbs, ordinary dock. He only catches up every fifth level, so the
     * dock levels are 4, 9, 14, ... - level 4 is his first. */
    story_shop_open(4);
    menu_open(SCREEN_STORY_SHOP);
    pump(4);
    dump(dir, "04_shop_normal");

    /* Mr Chubbs, boss dock (after level 9 -> Rustjaw next): free life. */
    story_shop_close();
    story_shop_open(9);
    menu_open(SCREEN_STORY_SHOP);
    pump(4);
    dump(dir, "05_shop_boss_dock");

    /* Same dock a moment later, once the gift banner has expired. */
    pump(250);
    dump(dir, "06_shop_boss_after_gift");

    /* Buy attempt with an empty wallet -> the red status line. */
    g_story.chubbcoin = 0;
    story_shop_close();
    g_story.docks_used = 0;      /* re-open a dock for the screenshot */
    story_shop_open(14);
    menu_open(SCREEN_STORY_SHOP);
    pump(2);
    menu_queue_tap(40, 34 + 21); /* select row 1 */
    pump(2);
    menu_queue_tap(40, 34 + 21); /* buy it */
    pump(4);
    dump(dir, "07_shop_too_poor");

    /* Level result card, the beat before the dock opens itself. */
    g_story.chubbcoin = 1840; g_story.level = 8;
    game_story_set_level(7);
    menu_open(SCREEN_STORY_RESULT);
    pump(2);
    dump(dir, "08_result_cleared");

    /* In-game HUD during a story level: CHUBBCOIN, never "$0". */
    g_story.chubbcoin = 10; g_story.level = 3; g_story.lives = 3;
    game_story_set_level(3);
    game_set_mode(GAME_MODE_STORY);
    menu_open(SCREEN_PLAYING);
    game_start();
    /* The menu must leave this card up forever, then dismiss it on one tap. */
    pump(30);
    if (!game_story_waiting_for_start()) {
        fprintf(stderr, "story card continued without a tap\n");
        return 1;
    }
    dump(dir, "09_story_hud_banner");
    menu_queue_tap(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
    pump(1);
    if (game_story_waiting_for_start()) {
        fprintf(stderr, "story card ignored tap\n");
        return 1;
    }
    pump(260);
    dump(dir, "10_story_hud");

    /* Same HUD in an arcade run still shows the dollar balance. */
    g_settings.coins = 4210;
    game_set_mode(GAME_MODE_WAVES);
    game_start();
    for (int i = 0; i < 200; i++) { game_update(); }
    game_draw();
    dump(dir, "11_arcade_hud");

    /* One shot per kingdom backdrop, so a theme regression is visible at a
     * glance rather than only on a device. */
    for (int t = 0; t < SF_THEME_COUNT; t++) {
        starfield_set_theme(t);
        for (int i = 0; i < 120; i++) starfield_update();
        starfield_draw_base(0, 0);
        starfield_draw_stars(0, 0);
        gfx_flip();
        char nm[32];
        snprintf(nm, sizeof(nm), "sky_%d", t);
        dump(dir, nm);
    }
    starfield_set_theme(SF_THEME_ARCADE);

    /* Widest supported viewport: the shop must not just stretch, it must
     * still breathe. */
    host_set_screen_width(HOST_SCREEN_W_MAX);
    menu_request_full_redraw();
    game_request_full_redraw();
    g_story.chubbcoin = 1840;
    story_shop_close();
    g_story.docks_used = 0;
    story_shop_open(9);
    menu_open(SCREEN_STORY_SHOP);
    pump(4);
    dump(dir, "12_shop_wide");
    host_set_screen_width(HOST_SCREEN_W_DEFAULT);
    menu_request_full_redraw();

    printf("done\n");
    return 0;
}
