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

    /* ── The opening speech ──────────────────────────────────────────────
     * It is the very first thing a new player sees, so it gets shot first:
     * mid-type, fully typed, and the marked-up pages that use *bold* and
     * !faint!.  A fresh save has never seen it, so the campaign entry point
     * must land on the intro rather than the map. */
    g_story.intro_seen = 0;
    menu_open(SCREEN_STORY_INTRO);
    pump(14);
    dump(dir, "00a_intro_typing");
    pump(120);
    dump(dir, "00b_intro_page_full");
    /* Page 11 is the *bold* one, page 13 the !faint! one. */
    for (int p = 0; p < 10; p++) { menu_queue_tap(4, 4); pump(2); menu_queue_tap(4, 4); pump(2); }
    pump(2);
    dump(dir, "00c_intro_bold_page");
    for (int p = 0; p < 2; p++) { menu_queue_tap(4, 4); pump(2); menu_queue_tap(4, 4); pump(2); }
    pump(2);
    dump(dir, "00d_intro_faint_page");
    /* SKIP drops straight into the map and marks the intro as seen. */
    menu_queue_tap(SCREEN_WIDTH - 34, SCREEN_HEIGHT - 14);
    pump(2);
    if (menu_current_screen() != SCREEN_STORY_MAP) {
        fprintf(stderr, "SKIP did not reach the level map\n");
        return 1;
    }
    if (!story_intro_seen()) {
        fprintf(stderr, "SKIP did not mark the intro as seen\n");
        return 1;
    }

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

    /* Mr Chubbs, boss dock (after level 9 -> Alien next): free life. */
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

    /* The wreck card, driven through the REAL failure path: fly level 12 on
     * the last life and let the field take the ship.  Losing that life must
     * re-lock the last two levels, bill the player and ground them. */
    save_init_defaults();
    story_init();
    for (int lv = 1; lv <= 12; lv++) story_complete_level(lv, NULL);
    story_set_current_level(12);
    g_story.lives = 1;
    game_story_set_level(12);
    game_set_mode(GAME_MODE_STORY);
    menu_open(SCREEN_PLAYING);
    game_start();
    pump(2);
    menu_queue_tap(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);   /* dismiss the brief */
    pump(1);
    {
        int guard = 0;
        while (menu_current_screen() != SCREEN_STORY_RESULT && guard++ < 90 * 240) {
            /* An idle pilot with a single life: the field does the rest. */
            g_game.player.lives = 1;
            g_game.player.shield_charges = 0;
            g_game.player.invulnerable_timer = 0;
            pump(1);
        }
        if (menu_current_screen() != SCREEN_STORY_RESULT) {
            fprintf(stderr, "the story level never ended\n");
            return 1;
        }
    }
    if (!story_is_grounded()) {
        fprintf(stderr, "losing the run did not ground the ship\n");
        return 1;
    }
    if (story_is_cleared(12) || story_is_cleared(11)) {
        fprintf(stderr, "losing the run did not relock the last two levels\n");
        return 1;
    }
    dump(dir, "13_wreck_repair_yard");
    /* And the map, grounded: LAUNCH becomes the repair countdown. */
    menu_open(SCREEN_STORY_MAP);
    pump(6);
    dump(dir, "14_map_grounded");
    story_finish_repairs();

    /* Level result card, driven by a REAL clear so the dynamic payout is the
     * one the game actually computed: the breakdown line under the total is
     * whatever the flight earned. */
    save_init_defaults();
    story_init();
    g_story.chubbcoin = 1840;
    game_story_set_level(1);
    game_set_mode(GAME_MODE_STORY);
    menu_open(SCREEN_PLAYING);
    game_start();
    pump(2);
    menu_queue_tap(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
    pump(1);
    {
        int guard = 0;
        while (menu_current_screen() != SCREEN_STORY_RESULT && guard++ < 90 * 300) {
            /* Hold fire and sweep, and stay alive so the CLEAN bonus lands. */
            platform_set_keys((u16)(KEY_A | (((guard / 40) & 1) ? KEY_LEFT : KEY_RIGHT)));
            g_game.player.invulnerable_timer = 60;
            pump(1);
        }
        platform_set_keys(0);
        if (menu_current_screen() != SCREEN_STORY_RESULT) {
            fprintf(stderr, "level 1 never resolved\n");
            return 1;
        }
    }
    if (game_story_outcome() != 1) {
        fprintf(stderr, "level 1 was not cleared\n");
        return 1;
    }
    /* The payout must be more than the level's floor: this clear earned it. */
    if (game_story_earned() <= g_story_levels[0].reward) {
        fprintf(stderr, "a good clear did not beat the floor payout (%d vs %d)\n",
                game_story_earned(), g_story_levels[0].reward);
        return 1;
    }
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

    /* ── Kingdom 8, the drone attack and the outro ────────────────────────
     * Beat the whole campaign for real except the last level, then fly
     * level 80 (one hundred drones + the big drone) with an undying probe
     * pilot.  Clearing it must open the OUTRO - not the result card - and
     * the outro must end back on the main menu.  A replay afterwards goes
     * to the ordinary result card instead. */
    save_init_defaults();
    story_init();
    for (int lv = 1; lv < STORY_LEVEL_COUNT; lv++) story_complete_level(lv, NULL);
    /* Model the mid-campaign loadout Jack has earned by the last sky. */
    g_settings.weapon_rig = WEAPON_NOVA;
    g_settings.owned_rigs |= (u16)(1u << WEAPON_NOVA);
    g_settings.laser_index = 3;
    g_settings.owned_lasers |= (1u << 3);
    g_settings.upgrade_levels[UPG_DAMAGE] = 5;
    g_settings.upgrade_levels[UPG_FIRE_RATE] = 5;

    /* The map, parked in kingdom 8: ten drone nodes over the gold home sky. */
    menu_open(SCREEN_STORY_MAP);
    pump(20);
    dump(dir, "15_map_kingdom8");

    /* A drone level's opening card. */
    game_story_set_level(STORY_CLASSIC_LEVELS + 1);
    story_set_current_level(STORY_CLASSIC_LEVELS + 1);
    game_set_mode(GAME_MODE_STORY);
    menu_open(SCREEN_PLAYING);
    game_start();
    pump(30);
    if (!game_story_waiting_for_start()) {
        fprintf(stderr, "drone level brief did not hold\n");
        return 1;
    }
    dump(dir, "16_drone_brief");

    /* Fly the final level to its end. */
    game_story_set_level(STORY_LEVEL_COUNT);
    story_set_current_level(STORY_LEVEL_COUNT);
    game_set_mode(GAME_MODE_STORY);
    menu_open(SCREEN_PLAYING);
    game_start();
    pump(2);
    menu_queue_tap(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
    pump(1);
    {
        int guard = 0;
        while (menu_current_screen() != SCREEN_STORY_OUTRO && guard++ < 90 * 420) {
            platform_set_keys((u16)(KEY_A | (((guard / 30) & 1) ? KEY_LEFT : KEY_RIGHT)));
            g_game.player.lives = 5;               /* undying probe pilot */
            g_game.player.invulnerable_timer = 60;
            g_game.is_game_over = false;
            pump(1);
        }
        platform_set_keys(0);
        if (menu_current_screen() != SCREEN_STORY_OUTRO) {
            fprintf(stderr, "clearing level 80 did not open the outro\n");
            return 1;
        }
    }

    /* The outro itself: first page typed, then WELCOME HOME, then the slow
     * six-second fade, a four-second solid-white hold, and a controlled app
     * close.  In this headless harness the close request leaves the screen on
     * the menu so we can inspect the persisted hand-off. */
    pump(120);
    dump(dir, "17_outro_you_did_it");
    menu_queue_tap(4, 4); pump(2);
    menu_queue_tap(4, 4); pump(2);
    pump(110);
    dump(dir, "18_outro_welcome_home");
    pump(120);
    dump(dir, "19_outro_fading_white");
    pump(980);
    if (menu_current_screen() != SCREEN_MAIN_MENU ||
        story_ending_phase() != STORY_ENDING_REBOOT_MESSAGE) {
        fprintf(stderr, "the outro did not hand off after the whiteout\n");
        return 1;
    }

    /* A real Android process is now closed and reopened.  Reinitializing the
     * menu here models that boot and proves the boss-music message is the
     * first screen after the whiteout. */
    menu_init();
    if (menu_current_screen() != SCREEN_STORY_REBOOT) {
        fprintf(stderr, "the first reboot did not open the Chubbs message\n");
        return 1;
    }
    pump(20);
    dump(dir, "20_reboot_message");
    pump(1350);
    if (menu_current_screen() != SCREEN_MAIN_MENU ||
        story_ending_phase() != STORY_ENDING_RETURN_MENU) {
        fprintf(stderr, "the reboot message did not hand off to the menu\n");
        return 1;
    }

    /* The second close/reopen clears the hand-off and lands on a normal menu. */
    menu_init();
    if (menu_current_screen() != SCREEN_MAIN_MENU ||
        story_ending_phase() != STORY_ENDING_NONE) {
        fprintf(stderr, "the second reboot did not return to the menu\n");
        return 1;
    }

    /* The ending plays once: replaying level 80 gets the ordinary card. */
    game_story_set_level(STORY_LEVEL_COUNT);
    story_set_current_level(STORY_LEVEL_COUNT);
    game_set_mode(GAME_MODE_STORY);
    menu_open(SCREEN_PLAYING);
    game_start();
    pump(2);
    menu_queue_tap(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
    pump(1);
    {
        int guard = 0;
        while (menu_current_screen() != SCREEN_STORY_RESULT && guard++ < 90 * 420) {
            platform_set_keys((u16)(KEY_A | (((guard / 30) & 1) ? KEY_LEFT : KEY_RIGHT)));
            g_game.player.lives = 5;
            g_game.player.invulnerable_timer = 60;
            g_game.is_game_over = false;
            pump(1);
        }
        platform_set_keys(0);
        if (menu_current_screen() != SCREEN_STORY_RESULT) {
            fprintf(stderr, "replaying level 80 did not open the result card\n");
            return 1;
        }
    }
    dump(dir, "21_replay_result_card");

    printf("done\n");
    return 0;
}
