/* Headless playthrough: simulates a competent-but-not-perfect pilot flying
 * every story level, to prove each one is winnable and to measure length. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "story.h"
#include "save.h"

extern void platform_set_keys(u16 k);

/* Simple dodge AI: slide away from the nearest incoming threat, keep firing. */
static u16 pilot_input(void) {
    u16 k = KEY_A;                 /* hold fire */
    int px = g_game.player.x >> 8;
    int py = g_game.player.y >> 8;
    int best_d = 1<<30, threat_x = -1, threat_y = -1;

    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        if (!g_game.asteroids[i].active) continue;
        int ax = g_game.asteroids[i].x >> 8, ay = g_game.asteroids[i].y >> 8;
        if (ay > py) continue;
        int d = (ax-px)*(ax-px) + (ay-py)*(ay-py);
        if (d < best_d) { best_d = d; threat_x = ax; threat_y = ay; }
    }
    for (int i = 0; i < MAX_BOSS_BULLETS; i++) {
        if (!g_game.boss_bullets[i].active) continue;
        int ax = g_game.boss_bullets[i].x >> 8, ay = g_game.boss_bullets[i].y >> 8;
        int d = (ax-px)*(ax-px) + (ay-py)*(ay-py);
        if (d < best_d) { best_d = d; threat_x = ax; threat_y = ay; }
    }
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!g_game.bullets[i].active || !g_game.bullets[i].enemy) continue;
        int ax = g_game.bullets[i].x >> 8, ay = g_game.bullets[i].y >> 8;
        int d = (ax-px)*(ax-px) + (ay-py)*(ay-py);
        if (d < best_d) { best_d = d; threat_x = ax; threat_y = ay; }
    }

    int W = host_screen_width();
    if (threat_x >= 0 && best_d < 60*60) {
        if (threat_x < px && px < W - 20) k |= KEY_RIGHT;
        else if (px > 20) k |= KEY_LEFT;
        if (threat_y < py && py < 150) k |= KEY_DOWN;
    } else {
        /* Otherwise drift toward whatever we should be shooting. */
        int tx = -1, td = 1<<30;
        for (int i = 0; i < MAX_ASTEROIDS; i++) {
            if (!g_game.asteroids[i].active) continue;
            int ax = g_game.asteroids[i].x >> 8;
            int d = abs(ax - px);
            if (d < td) { td = d; tx = ax; }
        }
        for (int i = 0; i < MAX_DRONES; i++) {
            if (!g_game.drones[i].active) continue;
            int ax = g_game.drones[i].x >> 8;
            int d = abs(ax - px);
            if (d < td) { td = d; tx = ax; }
        }
        if (g_game.boss_active && g_game.boss.active) tx = g_game.boss.x >> 8;
        if (tx >= 0) { if (tx > px + 3) k |= KEY_RIGHT; else if (tx < px - 3) k |= KEY_LEFT; }
        if (py < 130) k |= KEY_DOWN;
    }
    return k;
}

/* Returns ticks taken, or -1 if the level was lost / timed out. */
static int g_god = 0;
static int g_hits = 0;

static int play_level(int lv, int max_ticks) {
    game_story_set_level(lv);
    game_set_mode(GAME_MODE_STORY);
    game_start();
    g_hits = 0;
    int last_lives = g_game.player.lives;
    for (int t = 0; t < max_ticks; t++) {
        platform_set_keys(pilot_input());
        game_update();
        if (g_game.player.lives < last_lives) { g_hits++; last_lives = g_game.player.lives; }
        if (g_god) {
            /* Immortal probe pilot: we are measuring whether the level can
             * be finished at all, and how much damage it deals. */
            if (g_game.player.lives < 5) g_game.player.lives = 5;
            g_game.is_game_over = false;
        }
        int out = game_story_outcome();
        if (out == 1) return t;
        if (out == 2) return -1;
    }
    return -2; /* timed out: level may be unwinnable */
}

int main(int argc, char** argv) {
    int upgrade_tier = (argc > 1) ? atoi(argv[1]) : 0;
    g_god = (argc > 2) ? atoi(argv[2]) : 0;
    srand(12345);
    save_init_defaults();
    story_init();

    /* Model a player who buys gear as they go: by late sectors they have a
     * mid-tier rig and some upgrades, as Mr Chubbs' shelf allows. */
    int fails = 0, timeouts = 0;
    long total_ticks = 0;
    printf("%-4s %-16s %-9s %-15s %6s %8s %5s\n", "LV", "NAME", "OBJ", "TWIST",
           "SECS", "RESULT", "HITS");
    for (int lv = 1; lv <= STORY_LEVEL_COUNT; lv++) {
        /* Loadout progression: story shop purchases over the campaign. */
        int rig = upgrade_tier ? (lv / 6) : 0;
        if (rig > 12) rig = 12;
        g_settings.weapon_rig = (WeaponRig)rig;
        g_settings.owned_rigs |= (1u << rig);
        g_settings.laser_index = upgrade_tier ? (lv > 40 ? 3 : (lv > 20 ? 2 : (lv > 8 ? 1 : 0))) : 0;
        if (upgrade_tier) {
            int u = lv / 16; if (u > 5) u = 5;
            g_settings.upgrade_levels[UPG_DAMAGE] = u;
            g_settings.upgrade_levels[UPG_FIRE_RATE] = u;
            g_settings.upgrade_levels[UPG_ENGINE] = u;
            g_settings.upgrade_levels[UPG_HULL] = u;
        }

        const StoryLevel* L = &g_story_levels[lv-1];
        const char* on = L->objective==OBJ_BOSS?"BOSS":L->objective==OBJ_HUNT?"HUNT":
                         L->objective==OBJ_SURVIVE?"SURVIVE":
                         L->objective==OBJ_BIGGAME?"BIGGAME":
                         L->objective==OBJ_TIMED?"TIMED":"CLEAR";
        int max_ticks = 90 * 400;   /* 400 seconds of patience */
        int ticks = play_level(lv, max_ticks);
        if (ticks >= 0) {
            total_ticks += ticks;
            printf("%-4d %-16s %-9s %-15s %6.1f %8s %5d\n", lv, L->name, on,
                   story_modifier_name(L->modifier), ticks/90.0, "WIN", g_hits);
        } else if (ticks == -1) {
            fails++;
            printf("%-4d %-16s %-9s %-15s %6s %8s\n", lv, L->name, on,
                   story_modifier_name(L->modifier), "-", "DIED");
        } else {
            timeouts++;
            printf("%-4d %-16s %-9s %-15s %6s %8s\n", lv, L->name, on,
                   story_modifier_name(L->modifier), ">400", "STALL");
        }
    }
    printf("\ntier=%d  deaths=%d  stalls=%d  total=%.1f min\n",
           upgrade_tier, fails, timeouts, total_ticks/90.0/60.0);
    return timeouts ? 1 : 0;
}
