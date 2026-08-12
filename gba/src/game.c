#include "game.h"
#include "renderer.h"
#include "audio.h"
#include "save.h"
#include "starfield.h"
#include <stdlib.h>
#include <string.h>

EWRAM_BSS GameState g_game;

static bool s_game_static_valid = false;
static int s_game_frame = 0;
static GameMode s_game_mode = GAME_MODE_WAVES;

#ifdef PLATFORM_HOST
#define OVERDRIVE_TICK_RATE 90
#define ENDLESS_THREAT_INTERVAL 360
#define OVERDRIVE_THREAT_INTERVAL 180
#else
#define OVERDRIVE_TICK_RATE 60
#define ENDLESS_THREAT_INTERVAL 240
#define OVERDRIVE_THREAT_INTERVAL 120
#endif
#define OVERDRIVE_DURATION (90 * OVERDRIVE_TICK_RATE)

void game_set_mode(GameMode mode) {
    if (mode < GAME_MODE_WAVES || mode > GAME_MODE_OVERDRIVE) mode = GAME_MODE_WAVES;
    s_game_mode = mode;
}

GameMode game_get_mode(void) {
    return s_game_mode;
}

static void finish_run(bool time_up);

#define FIXED_ONE 256
#define TO_FIXED(n) ((n) * 256)
#define FROM_FIXED(n) ((n) >> 8)

static int get_diff_speed_mult(void) {
    switch (g_settings.difficulty) {
        case DIFF_CADET: return 220; // 0.86x
        case DIFF_ACE:   return 310; // 1.21x now slightly harder
        default:         return 256; // 1.00x
    }
}

/* ── New core upgrade helpers ─────────────────────────────────────────── */
// Engine speed multipliers: 0.70x .. 2.00x  (256 = 1.0x)
static const int s_engine_mult[6] = { 180, 220, 256, 320, 410, 512 };

static int get_engine_level(void) {
    int lv = g_settings.upgrade_levels[UPG_ENGINE];
    if (lv < 0) lv = 0;
    if (lv > 5) lv = 5;
    return lv;
}
static int get_engine_mult(void) {
    return s_engine_mult[get_engine_level()];
}

static int get_max_dash_cooldown(void) {
    static const int cd[6] = { 84, 66, 52, 40, 30, 24 }; // 1.4s -> 0.4s
    int lv = g_settings.upgrade_levels[UPG_DASH];
    if (lv < 0) lv = 0;
    if (lv > 5) lv = 5;
    return cd[lv];
}
static int get_dash_invuln(void) {
    int lv = g_settings.upgrade_levels[UPG_DASH];
    return 16 + lv * 3; // 16 .. 31
}

static int get_fire_rate_level(void) {
    int lv = g_settings.upgrade_levels[UPG_FIRE_RATE];
    if (lv < 0) lv = 0;
    if (lv > 5) lv = 5;
    return lv;
}
// Multiplier for cooldown: level0 256 (slow), level5 75 (very fast ~3.4x faster)
static int get_fire_rate_cooldown_mult(void) {
    static const int tbl[6] = { 256, 210, 165, 125, 95, 75 };
    return tbl[get_fire_rate_level()];
}

static int get_damage_bonus(void) {
    int lv = g_settings.upgrade_levels[UPG_DAMAGE];
    if (lv < 0) lv = 0;
    if (lv > 5) lv = 5;
    return lv; // 0 .. 5 extra damage
}

static int get_laser_bonus(void) {
    // 12 lasers - starter weak, final god
    static const int bonus[12] = { 0, 1, 1, 2, 2, 3, 3, 3, 2, 3, 4, 5 };
    int idx = g_settings.laser_index;
    if (idx < 0 || idx >= 12) return 0;
    return bonus[idx];
}

static int get_max_shields(void) {
    int lv = g_settings.upgrade_levels[UPG_SHIELD];
    if (lv < 0) lv = 0;
    if (lv > 5) lv = 5;
    // Lv0: 2 max, Lv5: 6 max
    static const int maxs[6] = { 2, 3, 4, 5, 6, 6 };
    return maxs[lv];
}
static int get_start_shields(void) {
    int lv = g_settings.upgrade_levels[UPG_SHIELD];
    if (lv < 0) lv = 0;
    if (lv > 5) lv = 5;
    static const int starts[6] = { 0, 1, 1, 2, 2, 3 };
    return starts[lv];
}

static int get_max_lives(void) {
    int lv = g_settings.upgrade_levels[UPG_HULL];
    if (lv < 0) lv = 0;
    if (lv > 5) lv = 5;
    // Lv0: 2 lives (hard!), Lv1:3 normal, up to 7
    static const int lives[6] = { 2, 3, 4, 5, 6, 7 };
    return lives[lv];
}

static int get_rapid_duration(void) {
    int lv = g_settings.upgrade_levels[UPG_OVERDRIVE];
    if (lv < 0) lv = 0;
    if (lv > 5) lv = 5;
    static const int dur[6] = { 480, 660, 840, 1080, 1320, 1560 }; // 8s .. 26s at 60fps
    return dur[lv];
}

// Combo now fixed but slightly scales with fire_rate to reward fast shooting
static int get_max_combo_cap(void) { return 15; }
static int get_max_combo_timer(void) { 
    int fr = get_fire_rate_level();
    return 140 + fr * 12; // 140..200
}

IWRAM_CODE static void spawn_particle(int x, int y, int vx, int vy, u8 color, u8 life) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!g_game.particles[i].active) {
            g_game.particles[i].x = x;
            g_game.particles[i].y = y;
            g_game.particles[i].vx = vx;
            g_game.particles[i].vy = vy;
            g_game.particles[i].color = color;
            g_game.particles[i].life = life;
            g_game.particles[i].max_life = life;
            g_game.particles[i].active = true;
            return;
        }
    }
}

IWRAM_CODE static void trigger_explosion(int x, int y) {
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!g_game.explosions[i].active) {
            g_game.explosions[i].x = x;
            g_game.explosions[i].y = y;
            g_game.explosions[i].frame = 0;
            g_game.explosions[i].timer = 0;
            g_game.explosions[i].active = true;
            break;
        }
    }
    for (int p = 0; p < 10; p++) {
        int angle = p * 25;
        int spd = (rand() & 127) + 80;
        int vx = (lu_cos(angle * 256) * spd) >> 12;
        int vy = (lu_sin(angle * 256) * spd) >> 12;
        u8 col = 128 + (rand() & 31);
        spawn_particle(TO_FIXED(x), TO_FIXED(y), vx, vy, col, (rand() & 15) + 12);
    }
    if (g_settings.screen_shake) {
        g_game.shake_timer = 12;
    }
    audio_play_sfx(SFX_EXPLOSION);
}

IWRAM_CODE static void emit_engine_particle(void) {
    int phase = (s_game_frame >> 1) + (rand() & 3);
    u8 col = gfx_get_trail_color_animated(g_settings.trail_index, phase * 4);
    int px = g_game.player.x + (rand() & 1023) - 512;
    int py = g_game.player.y + TO_FIXED(8);
    int pvx = (rand() & 255) - 128;
    int pvy = (rand() & 127) + 200;
    spawn_particle(px, py, pvx, pvy, col, (rand() & 7) + 6);
}

IWRAM_CODE static void emit_enemy_engine_particle(const Drone* drone) {
    u8 col = 199 + (rand() % 3);
    int px = drone->x + (rand() & 1023) - 512;
    int py = drone->y - TO_FIXED(8);
    int pvx = (rand() & 255) - 128;
    int pvy = -((rand() & 127) + 160);
    spawn_particle(px, py, pvx, pvy, col, (rand() & 3) + 6);
}

static void try_spawn_powerup(int x, int y, int chance_pct) {
    if ((rand() % 100) >= chance_pct) return;
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!g_game.powerups[i].active) {
            g_game.powerups[i].x = TO_FIXED(x);
            g_game.powerups[i].y = TO_FIXED(y);
            g_game.powerups[i].vy = 90;
            int roll = rand() % 100;
            if (roll < 40) g_game.powerups[i].type = PWR_SHIELD;
            else if (roll < 80) g_game.powerups[i].type = PWR_RAPID;
            else g_game.powerups[i].type = PWR_REPAIR;
            g_game.powerups[i].active = true;
            return;
        }
    }
}

static void award_coins(int base_amount) {
    int scav_lv = g_settings.upgrade_levels[UPG_SCAVENGER];
    if (scav_lv < 0) scav_lv = 0;
    if (scav_lv > 5) scav_lv = 5;
    int mult = 100 + scav_lv * 35; // 100% .. 275%
    int earned = (base_amount * mult) / 100;
    if (earned < 1) earned = 1;
    g_settings.coins += earned;
    if (g_settings.coins > 9999999) g_settings.coins = 9999999;
    save_write();
}

static void award_score(int base_pts) {
    g_game.score += base_pts * g_game.combo;
    int max_combo = get_max_combo_cap();
    if (g_game.combo < max_combo) g_game.combo++;
    g_game.combo_timer = get_max_combo_timer();
}

static int coins_for_asteroid(AsteroidType type) {
    switch (type) {
        case AST_LARGE: return 20;
        case AST_MED_A:
        case AST_MED_B: return 12;
        case AST_SMALL: return 6;
        default: return 4;
    }
}

static void damage_player(void) {
    if (g_game.player.invulnerable_timer > 0) return;
    int px = FROM_FIXED(g_game.player.x);
    int py = FROM_FIXED(g_game.player.y);

    if (g_game.player.shield_charges > 0) {
        g_game.player.shield_charges--;
        g_game.player.invulnerable_timer = 60;
        trigger_explosion(px, py);
    } else {
        g_game.player.lives--;
        g_game.player.invulnerable_timer = 90 + get_dash_invuln();
        g_game.player.x = TO_FIXED(SCREEN_WIDTH / 2);
        g_game.player.y = TO_FIXED(SCREEN_HEIGHT - 20);
        trigger_explosion(px, py);
        if (g_game.player.lives <= 0) {
            finish_run(false);
        }
    }
    g_game.combo = 1;
    g_game.combo_timer = 0;
    if (g_settings.screen_shake) g_game.shake_timer = 18;
}

static void spawn_asteroid(AsteroidType type, int x, int y, int vx, int vy) {
    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        if (!g_game.asteroids[i].active) {
            g_game.asteroids[i].type = type;
            g_game.asteroids[i].x = x;
            g_game.asteroids[i].y = y;
            g_game.asteroids[i].vx = vx;
            g_game.asteroids[i].vy = vy;
            g_game.asteroids[i].active = true;
            if (type == AST_LARGE) {
                g_game.asteroids[i].radius = 12;
                // Large tougher in later waves
                g_game.asteroids[i].hp = 2 + (g_game.wave / 5);
            } else if (type == AST_MED_A || type == AST_MED_B) {
                g_game.asteroids[i].radius = 8;
                g_game.asteroids[i].hp = 1 + (g_game.wave / 8);
            } else if (type == AST_SMALL) {
                g_game.asteroids[i].radius = 5;
                g_game.asteroids[i].hp = 1;
            } else {
                g_game.asteroids[i].radius = 3;
                g_game.asteroids[i].hp = 1;
            }
            return;
        }
    }
}

static void destroy_asteroid(int idx, bool award) {
    Asteroid* a = &g_game.asteroids[idx];
    AsteroidType t = a->type;
    a->active = false;
    int ax = FROM_FIXED(a->x);
    int ay = FROM_FIXED(a->y);
    trigger_explosion(ax, ay);

    if (award) {
        int pts = (t == AST_LARGE) ? 60 : ((t == AST_MED_A || t == AST_MED_B) ? 35 : 20);
        award_score(pts);
        award_coins(coins_for_asteroid(t));
        int mult = get_diff_speed_mult() + g_game.wave * 12;
        if (t == AST_LARGE) {
            int spd = (160 * mult) >> 8;
            spawn_asteroid(AST_MED_A, a->x - TO_FIXED(6), a->y, -spd, spd);
            spawn_asteroid(AST_MED_B, a->x + TO_FIXED(6), a->y, spd, spd);
        } else if (t == AST_MED_A || t == AST_MED_B) {
            int spd = (200 * mult) >> 8;
            spawn_asteroid(AST_SMALL, a->x - TO_FIXED(4), a->y, -spd, spd);
            spawn_asteroid(AST_TINY, a->x + TO_FIXED(4), a->y, spd, spd);
        }
        try_spawn_powerup(ax, ay, 4);
    }
}

static void destroy_drone(int idx, bool award) {
    Drone* d = &g_game.drones[idx];
    d->active = false;
    int dx = FROM_FIXED(d->x);
    int dy = FROM_FIXED(d->y);
    trigger_explosion(dx, dy);
    if (award) {
        award_score(120);
        award_coins(45);
        try_spawn_powerup(dx, dy, 10);
    }
}

static int count_active_asteroids(void) {
    int n = 0;
    for (int i = 0; i < MAX_ASTEROIDS; i++) if (g_game.asteroids[i].active) n++;
    return n;
}

static int count_active_drones(void) {
    int n = 0;
    for (int i = 0; i < MAX_DRONES; i++) if (g_game.drones[i].active) n++;
    return n;
}

static int current_threat(void) {
    int threat = g_game.wave;
    if (threat < 1) threat = 1;
    return threat;
}

static void spawn_random_asteroid(void) {
    int threat = current_threat();
    int mult = get_diff_speed_mult() + threat * 18;
    int x = TO_FIXED((rand() % (SCREEN_WIDTH - 40)) + 20);
    int y = -TO_FIXED((rand() % 56) + 8);
    int vx = ((rand() % 160) - 80) * mult >> 8;
    int vy = ((rand() % 90) + 80 + threat * 6) * mult >> 8;
    bool is_large = (rand() % 100) < (15 + threat * 7);
    AsteroidType type = is_large ? AST_LARGE : ((rand() & 1) ? AST_MED_A : AST_MED_B);
    spawn_asteroid(type, x, y, vx, vy);
}

static bool spawn_random_drone(void) {
    int threat = current_threat();
    int mult = get_diff_speed_mult() + threat * 18;
    for (int i = 0; i < MAX_DRONES; i++) {
        if (g_game.drones[i].active) continue;
        g_game.drones[i].x = TO_FIXED((rand() % (SCREEN_WIDTH - 40)) + 20);
        g_game.drones[i].y = -TO_FIXED((rand() % 44) + 14);
        g_game.drones[i].vx = 0;
        g_game.drones[i].vy = (70 + threat * 5) * mult >> 8;
        int base_cd = 70 - threat * 4;
        if (base_cd < 22) base_cd = 22;
        g_game.drones[i].shoot_timer = (rand() % 40) + base_cd;
        g_game.drones[i].burst_timer = 0;
        g_game.drones[i].burst_shots = 0;
        g_game.drones[i].phase = rand() % 256;
        g_game.drones[i].hp = 2 + (threat / 3) + (rand() % 3);
        if (g_settings.difficulty == DIFF_ACE) g_game.drones[i].hp++;
        if (g_game.drones[i].hp > 6) g_game.drones[i].hp = 6;
        g_game.drones[i].active = true;
        return true;
    }
    return false;
}

static void spawn_continuous_threat(void) {
    int threat = current_threat();
    int ast_n = count_active_asteroids();
    int dr_n = count_active_drones();
    int ast_cap = 6 + threat / 2;
    if (g_game.mode == GAME_MODE_OVERDRIVE) ast_cap += 3;
    if (ast_cap > 20) ast_cap = 20;
    int dr_cap = 1 + threat / 3;
    if (g_game.mode == GAME_MODE_OVERDRIVE) dr_cap++;
    if (dr_cap > MAX_DRONES) dr_cap = MAX_DRONES;

    int drone_chance = (g_game.mode == GAME_MODE_OVERDRIVE) ? 42 : 30;
    bool want_drone = ((rand() % 100) < drone_chance) && (dr_n < dr_cap);
    if (want_drone) {
        spawn_random_drone();
    } else if (ast_n < ast_cap) {
        spawn_random_asteroid();
    } else if (dr_n < dr_cap) {
        spawn_random_drone();
    }

    if (g_game.mode == GAME_MODE_OVERDRIVE && (rand() % 100) < 35) {
        if (count_active_asteroids() < ast_cap) spawn_random_asteroid();
        else spawn_random_drone();
    }
}

static void finish_run(bool time_up) {
    g_game.is_game_over = true;
    g_game.time_up = time_up;
    if (g_game.score > g_settings.high_score) {
        g_settings.high_score = g_game.score;
        g_game.is_new_high_score = true;
    }
    save_write();
}

static void update_continuous_modes(void) {
    int threat_every = (g_game.mode == GAME_MODE_OVERDRIVE)
        ? OVERDRIVE_THREAT_INTERVAL : ENDLESS_THREAT_INTERVAL;
    g_game.spawn_timer--;
    if ((s_game_frame % threat_every) == 0) {
        g_game.wave++;
        if (g_game.wave > 99) g_game.wave = 99;
    }

    if (g_game.mode == GAME_MODE_OVERDRIVE) {
        if (g_game.overdrive_timer > 0) {
            g_game.overdrive_timer--;
        } else {
            finish_run(true);
            return;
        }
    }

    int ast_n = count_active_asteroids();
    int dr_n = count_active_drones();
    if (ast_n + dr_n == 0 && g_game.spawn_timer > 8) g_game.spawn_timer = 8;

    if (g_game.spawn_timer <= 0) {
        spawn_continuous_threat();
        int threat = current_threat();
        int cd = 52 - threat * 2;
        if (g_game.mode == GAME_MODE_OVERDRIVE) cd -= 14;
        if (cd < 12) cd = 12;
        g_game.spawn_timer = cd;
    }
}

/* ── Faster difficulty ramp: by wave 4 it's serious ────────────────────── */
static void begin_wave(void) {
    g_game.wave++;
    g_game.wave_banner_timer = 110;

    if (g_game.wave > 1) {
        award_coins(g_game.wave * 30);
    }

    int diff_extra = (g_settings.difficulty == DIFF_ACE) ? 3 : (g_settings.difficulty == DIFF_CADET ? -1 : 0);
    // Wave 1: ~4 asteroids, Wave 2: 6-7, Wave 3: 9-10, Wave 4: 12-14, Wave 5+: 16-22
    int ast_count = 4 + g_game.wave * 2 + (g_game.wave > 2 ? g_game.wave : 0);
    ast_count += diff_extra;
    if (ast_count < 3) ast_count = 3;
    if (ast_count > 22) ast_count = 22;

    int diff_mult = get_diff_speed_mult();
    // Add wave scaling to speed: each wave + ~7% faster
    int wave_speed = g_game.wave * 18;
    int mult = diff_mult + wave_speed;

    for (int i = 0; i < ast_count; i++) {
        int x = TO_FIXED((rand() % (SCREEN_WIDTH - 40)) + 20);
        int y = -TO_FIXED((rand() % 80) + 10 + i * 14); // tighter spawn, faster appear
        int vx = ((rand() % 160) - 80) * mult >> 8;
        int vy = ((rand() % 90) + 80 + g_game.wave * 6) * mult >> 8; // faster downward over waves

        bool is_large = (rand() % 100) < (15 + g_game.wave * 7); // 22% w1, 43% w4, 71% w8
        spawn_asteroid(is_large ? AST_LARGE : (rand() % 2 == 0 ? AST_MED_A : AST_MED_B), x, y, vx, vy);
    }

    // Drones from wave 2, much more aggressive ramp
    if (g_game.wave >= 2) {
        int drone_count = (g_game.wave / 2) + 1;
        if (g_settings.difficulty == DIFF_ACE) drone_count++;
        if (drone_count > MAX_DRONES) drone_count = MAX_DRONES;
        for (int i = 0; i < drone_count; i++) {
            int spacing = (SCREEN_WIDTH - 60) / (drone_count > 1 ? drone_count - 1 : 1);
            g_game.drones[i].x = TO_FIXED(30 + (drone_count > 1 ? i * spacing : 60));
            g_game.drones[i].y = -TO_FIXED(20 + i * 28);
            g_game.drones[i].vx = 0;
            g_game.drones[i].vy = (70 + g_game.wave * 5) * mult >> 8;
            // Faster shooting as waves go up
            int base_cd = 70 - g_game.wave * 4;
            if (base_cd < 22) base_cd = 22;
            g_game.drones[i].shoot_timer = (rand() % 40) + base_cd;
            g_game.drones[i].burst_timer = 0;
            g_game.drones[i].burst_shots = 0;
            g_game.drones[i].phase = rand() % 256;
            g_game.drones[i].hp = 2 + (g_game.wave / 3) + (g_settings.difficulty == DIFF_ACE ? 1 : 0);
            if (g_game.drones[i].hp > 6) g_game.drones[i].hp = 6;
            g_game.drones[i].active = true;
        }
    }
}

static void add_player_bullet(int x, int y, int vx, int vy, int damage, bool heavy) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!g_game.bullets[i].active) {
            g_game.bullets[i].x = x;
            g_game.bullets[i].y = y;
            g_game.bullets[i].vx = vx;
            g_game.bullets[i].vy = vy;
            g_game.bullets[i].radius = heavy ? 3 : 2;
            g_game.bullets[i].damage = damage;
            g_game.bullets[i].life = 70;
            g_game.bullets[i].enemy = false;
            g_game.bullets[i].heavy = heavy;
            g_game.bullets[i].active = true;
            return;
        }
    }
}

static bool add_enemy_bullet(int x, int y, int vx, int vy) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!g_game.bullets[i].active) {
            g_game.bullets[i].x = x;
            g_game.bullets[i].y = y;
            g_game.bullets[i].vx = vx;
            g_game.bullets[i].vy = vy;
            g_game.bullets[i].radius = 2;
            g_game.bullets[i].damage = 1;
            g_game.bullets[i].life = 140;
            g_game.bullets[i].enemy = true;
            g_game.bullets[i].heavy = false;
            g_game.bullets[i].active = true;
            return true;
        }
    }
    return false;
}

/* ── New weapon system: single weak starter, final NOVA god ──────────── */
static int get_weapon_base_cooldown(WeaponRig rig) {
    switch (rig) {
        case WEAPON_SINGLE:  return 30; // 2/sec starter
        case WEAPON_TWIN:    return 26; // 2.3/sec
        case WEAPON_SPREAD:  return 28; // slightly slower due 3 bullets
        case WEAPON_FOCUSED: return 22; // focused fast heavy
        case WEAPON_TRIPLE:  return 20;
        case WEAPON_PLASMA:  return 18;
        case WEAPON_QUANTUM: return 14;
        case WEAPON_NOVA:    return 10; // god fast
        default: return 30;
    }
}

static void fire_player_weapon(void) {
    bool rapid = (g_game.player.rapid_fire_timer > 0);
    int px = g_game.player.x;
    int py = g_game.player.y;
    int dmg_bonus = get_damage_bonus() + get_laser_bonus();
    // Omega and Rainbow add extra pierce
    bool is_omega = (g_settings.laser_index == 11);
    bool is_rainbow = (g_settings.laser_index == 7);

    WeaponRig rig = g_settings.weapon_rig;
    int base_cd = get_weapon_base_cooldown(rig);
    int fr_mult = get_fire_rate_cooldown_mult(); // 75..256
    int cooldown = (base_cd * fr_mult) >> 8;
    if (rapid) cooldown = (cooldown * 2) / 5; // rapid cuts to 40%
    if (cooldown < 3) cooldown = 3; // absolute min 20/sec
    if (is_omega) { if (cooldown > 3) cooldown--; } // omega tiny buff

    switch (rig) {
        case WEAPON_SINGLE: {
            // One weak laser that can only break 1 small rock at a time (low dmg, no pierce)
            add_player_bullet(px, py - TO_FIXED(8), 0, -TO_FIXED(6), 1 + dmg_bonus, false);
            break;
        }
        case WEAPON_TWIN: {
            add_player_bullet(px - TO_FIXED(4), py - TO_FIXED(6), 0, -TO_FIXED(5), 1 + dmg_bonus, false);
            add_player_bullet(px + TO_FIXED(4), py - TO_FIXED(6), 0, -TO_FIXED(5), 1 + dmg_bonus, false);
            break;
        }
        case WEAPON_SPREAD: {
            add_player_bullet(px, py - TO_FIXED(6), 0, -TO_FIXED(5), 1 + dmg_bonus, false);
            add_player_bullet(px - TO_FIXED(4), py - TO_FIXED(4), -TO_FIXED(1), -TO_FIXED(4), 1 + dmg_bonus, false);
            add_player_bullet(px + TO_FIXED(4), py - TO_FIXED(4), TO_FIXED(1), -TO_FIXED(4), 1 + dmg_bonus, false);
            break;
        }
        case WEAPON_FOCUSED: {
            // Heavy single that pierces small rocks
            bool heavy = true;
            add_player_bullet(px, py - TO_FIXED(8), 0, -TO_FIXED(6), 2 + dmg_bonus, heavy);
            break;
        }
        case WEAPON_TRIPLE: {
            add_player_bullet(px - TO_FIXED(6), py - TO_FIXED(6), 0, -TO_FIXED(5), 1 + dmg_bonus, false);
            add_player_bullet(px, py - TO_FIXED(8), 0, -TO_FIXED(6), 2 + dmg_bonus, true);
            add_player_bullet(px + TO_FIXED(6), py - TO_FIXED(6), 0, -TO_FIXED(5), 1 + dmg_bonus, false);
            break;
        }
        case WEAPON_PLASMA: {
            add_player_bullet(px - TO_FIXED(5), py - TO_FIXED(7), -50, -TO_FIXED(5), 3 + dmg_bonus, true);
            add_player_bullet(px + TO_FIXED(5), py - TO_FIXED(7), 50, -TO_FIXED(5), 3 + dmg_bonus, true);
            break;
        }
        case WEAPON_QUANTUM: {
            add_player_bullet(px - TO_FIXED(4), py - TO_FIXED(8), 0, -TO_FIXED(7), 4 + dmg_bonus, true);
            add_player_bullet(px + TO_FIXED(4), py - TO_FIXED(8), 0, -TO_FIXED(7), 4 + dmg_bonus, true);
            add_player_bullet(px, py - TO_FIXED(10), 0, -TO_FIXED(8), 4 + dmg_bonus, true);
            break;
        }
        case WEAPON_NOVA: {
            // Final god weapon: 5 bullets, massive dmg, piercing, fast
            int d = 5 + dmg_bonus;
            if (is_omega) d += 2; // extra crazy with omega crystal
            add_player_bullet(px, py - TO_FIXED(10), 0, -TO_FIXED(9), d, true);
            add_player_bullet(px - TO_FIXED(5), py - TO_FIXED(8), -70, -TO_FIXED(8), d, true);
            add_player_bullet(px + TO_FIXED(5), py - TO_FIXED(8), 70, -TO_FIXED(8), d, true);
            add_player_bullet(px - TO_FIXED(9), py - TO_FIXED(6), -140, -TO_FIXED(6), d-1, true);
            add_player_bullet(px + TO_FIXED(9), py - TO_FIXED(6), 140, -TO_FIXED(6), d-1, true);
            break;
        }
        default: {
            add_player_bullet(px, py - TO_FIXED(6), 0, -TO_FIXED(5), 1 + dmg_bonus, false);
            break;
        }
    }
    g_game.player.fire_cooldown = cooldown;
    audio_play_sfx(SFX_LASER);
}

void game_init(void) {
    memset(&g_game, 0, sizeof(GameState));
}

void game_start(void) {
    memset(&g_game, 0, sizeof(GameState));
    g_game.mode = s_game_mode;

    g_game.player.x = TO_FIXED(SCREEN_WIDTH / 2);
    g_game.player.y = TO_FIXED(SCREEN_HEIGHT - 24);
    g_game.player.radius = 6;

    // Lives based on hull upgrade - start with 2 on lv0 (hard)
    int max_lives = get_max_lives();
    // Difficulty still impacts a bit
    if (g_settings.difficulty == DIFF_CADET) max_lives++;
    if (g_settings.difficulty == DIFF_ACE && max_lives > 2) max_lives--;
    g_game.player.lives = max_lives;

    // Shields based on upgrade
    g_game.player.shield_charges = get_start_shields();
    if (g_settings.difficulty == DIFF_CADET) g_game.player.shield_charges++;

    // Clamp shields to max
    int max_sh = get_max_shields();
    if (g_game.player.shield_charges > max_sh) g_game.player.shield_charges = max_sh;

    g_game.player.invulnerable_timer = 90;
    g_game.combo = 1;
    s_game_frame = 0;
    s_game_static_valid = false;

    if (g_game.mode == GAME_MODE_WAVES) {
        g_game.intermission_timer = 30;
    } else {
        g_game.wave = 1;
        g_game.wave_banner_timer = 110;
        g_game.spawn_timer = 12;
        g_game.intermission_timer = 9999;
        if (g_game.mode == GAME_MODE_OVERDRIVE) {
            g_game.overdrive_timer = OVERDRIVE_DURATION;
        }
        for (int i = 0; i < 4; i++) spawn_random_asteroid();
        if (g_game.mode == GAME_MODE_OVERDRIVE) spawn_random_drone();
    }

    audio_play_bgm(BGM_GAME);
}

#ifndef GAME_SPEED_MULTIPLIER
#ifdef PLATFORM_HOST
#define GAME_SPEED_MULTIPLIER 1
#else
#define GAME_SPEED_MULTIPLIER 2
#endif
#endif

static void game_update_tick(void) {
    s_game_frame++;
    starfield_update();

    if (g_game.shake_timer > 0) {
        g_game.shake_timer--;
        g_game.shake_x = (rand() & 3) - (rand() & 3);
        g_game.shake_y = (rand() & 3) - (rand() & 3);
    } else {
        g_game.shake_x = 0;
        g_game.shake_y = 0;
    }

    if (g_game.wave_banner_timer > 0) g_game.wave_banner_timer--;

    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (g_game.particles[i].active) {
            g_game.particles[i].x += g_game.particles[i].vx;
            g_game.particles[i].y += g_game.particles[i].vy;
            g_game.particles[i].life--;
            if (g_game.particles[i].life == 0) g_game.particles[i].active = false;
        }
    }
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (g_game.explosions[i].active) {
            g_game.explosions[i].timer++;
            if (g_game.explosions[i].timer >= 3) {
                g_game.explosions[i].timer = 0;
                g_game.explosions[i].frame++;
                if (g_game.explosions[i].frame >= 9) g_game.explosions[i].active = false;
            }
        }
    }

    if (g_game.is_game_over) return;

    if (g_game.combo_timer > 0) {
        g_game.combo_timer--;
        if (g_game.combo_timer == 0) g_game.combo = 1;
    }

    if (g_game.player.fire_cooldown > 0) g_game.player.fire_cooldown--;
    if (g_game.player.dash_cooldown > 0) g_game.player.dash_cooldown--;
    if (g_game.player.dash_remaining > 0) g_game.player.dash_remaining--;
    if (g_game.player.invulnerable_timer > 0) g_game.player.invulnerable_timer--;
    if (g_game.player.rapid_fire_timer > 0) g_game.player.rapid_fire_timer--;

    int mx = 0, my = 0;
    if (key_is_down(KEY_LEFT)) mx -= 1;
    if (key_is_down(KEY_RIGHT)) mx += 1;
    if (key_is_down(KEY_UP)) my -= 1;
    if (key_is_down(KEY_DOWN)) my += 1;

    int max_dash_cd = get_max_dash_cooldown();
    if ((key_hit(KEY_B) || key_hit(KEY_R) || key_hit(KEY_L)) && g_game.player.dash_cooldown == 0) {
        if (mx != 0 || my != 0) {
            g_game.player.dash_dir_x = mx;
            g_game.player.dash_dir_y = my;
        } else {
            g_game.player.dash_dir_x = 0;
            g_game.player.dash_dir_y = -1;
        }
        g_game.player.dash_remaining = 12 + get_engine_level();
        g_game.player.dash_cooldown = max_dash_cd;
        g_game.player.invulnerable_timer = get_dash_invuln();
        for (int b = 0; b < 10; b++) emit_engine_particle();
    }

    // ── Engine speed with 2x cap logic ───────────────────────────────
    int eng_mult = get_engine_mult(); // 180..512
    int base_spd = TO_FIXED(1) + 50; // 306 base
    base_spd = (base_spd * eng_mult) >> 8;
    int dash_extra = TO_FIXED(2) + (get_engine_level() * 30);
    dash_extra = (dash_extra * eng_mult) >> 8;
    int spd = (g_game.player.dash_remaining > 0) ? (base_spd + dash_extra) : base_spd;

    int dir_x = (g_game.player.dash_remaining > 0) ? g_game.player.dash_dir_x : mx;
    int dir_y = (g_game.player.dash_remaining > 0) ? g_game.player.dash_dir_y : my;

    if (dir_x != 0 && dir_y != 0) {
        g_game.player.x += (dir_x * spd * 181) / 256;
        g_game.player.y += (dir_y * spd * 181) / 256;
    } else {
        g_game.player.x += dir_x * spd;
        g_game.player.y += dir_y * spd;
    }

    if (g_game.player.x < TO_FIXED(12)) g_game.player.x = TO_FIXED(12);
    if (g_game.player.x > TO_FIXED(SCREEN_WIDTH - 12)) g_game.player.x = TO_FIXED(SCREEN_WIDTH - 12);
    if (g_game.player.y < TO_FIXED(22)) g_game.player.y = TO_FIXED(22);
    if (g_game.player.y > TO_FIXED(SCREEN_HEIGHT - 12)) g_game.player.y = TO_FIXED(SCREEN_HEIGHT - 12);

    if (mx != 0 || my != 0 || g_game.player.dash_remaining > 0) {
        if ((rand() & 1) == 0) emit_engine_particle();
    }

    if (key_is_down(KEY_A) && g_game.player.fire_cooldown == 0) {
        fire_player_weapon();
    }

    for (int i = 0; i < MAX_BULLETS; i++) {
        if (g_game.bullets[i].active) {
            g_game.bullets[i].x += g_game.bullets[i].vx;
            g_game.bullets[i].y += g_game.bullets[i].vy;
            g_game.bullets[i].life--;
            int bx = FROM_FIXED(g_game.bullets[i].x);
            int by = FROM_FIXED(g_game.bullets[i].y);
            if (g_game.bullets[i].life == 0 || bx < -12 || bx > SCREEN_WIDTH + 12 || by < -20 || by > SCREEN_HEIGHT + 20) {
                g_game.bullets[i].active = false;
            }
        }
    }

    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        if (g_game.asteroids[i].active) {
            g_game.asteroids[i].x += g_game.asteroids[i].vx;
            g_game.asteroids[i].y += g_game.asteroids[i].vy;
            int ax = FROM_FIXED(g_game.asteroids[i].x);
            int ay = FROM_FIXED(g_game.asteroids[i].y);
            int rad = g_game.asteroids[i].radius;
            if (ax < -rad) g_game.asteroids[i].x = TO_FIXED(SCREEN_WIDTH + rad);
            if (ax > SCREEN_WIDTH + rad) g_game.asteroids[i].x = -TO_FIXED(rad);
            if (ay > SCREEN_HEIGHT + rad) {
                g_game.asteroids[i].y = -TO_FIXED(rad + 10);
                g_game.asteroids[i].x = TO_FIXED((rand() % (SCREEN_WIDTH - 30)) + 15);
            }
        }
    }

    int mult = get_diff_speed_mult() + g_game.wave * 12;
    for (int i = 0; i < MAX_DRONES; i++) {
        Drone* drone = &g_game.drones[i];
        if (!drone->active) continue;
        if (drone->y < TO_FIXED(32)) {
            drone->y += drone->vy;
        } else {
            drone->phase = (drone->phase + 1) & 255;
            int aim_wobble = (lu_sin(drone->phase * 256) * TO_FIXED(4)) >> 12;
            int target_x = g_game.player.x + aim_wobble;
            int dx = target_x - drone->x;
            int track_step = (95 * mult) >> 8;
            if (dx > track_step) drone->x += track_step;
            else if (dx < -track_step) drone->x -= track_step;
            else drone->x = target_x;
            if (drone->x < TO_FIXED(12)) drone->x = TO_FIXED(12);
            if (drone->x > TO_FIXED(SCREEN_WIDTH - 12)) drone->x = TO_FIXED(SCREEN_WIDTH - 12);
        }
        if (((s_game_frame + i) & 2) == 0) emit_enemy_engine_particle(drone);
        if (drone->y <= TO_FIXED(20)) continue;

        if (drone->burst_shots > 0) {
            drone->burst_timer--;
            if (drone->burst_timer <= 0) {
                int cannon_x = drone->x + ((drone->burst_shots & 1) ? -TO_FIXED(4) : TO_FIXED(4));
                int bullet_speed = (190 + g_game.wave * 8) * mult >> 8;
                if (add_enemy_bullet(cannon_x, drone->y + TO_FIXED(9), 0, bullet_speed)) {
                    audio_play_sfx(SFX_LASER);
                }
                drone->burst_shots--;
                drone->burst_timer = (rand() % 5) + 5;
                if (drone->burst_shots == 0) {
                    int base_cd = 65 - g_game.wave * 3;
                    if (base_cd < 18) base_cd = 18;
                    drone->shoot_timer = ((rand() % 50) + base_cd) * 256 / mult;
                }
            }
        } else {
            drone->shoot_timer--;
            if (drone->shoot_timer <= 0) {
                drone->burst_shots = (rand() % 3) + 2 + (g_game.wave / 4);
                if (drone->burst_shots > 5) drone->burst_shots = 5;
                drone->burst_timer = 0;
            }
        }
    }

    int scav_lv = g_settings.upgrade_levels[UPG_SCAVENGER];
    int mag_dist_sq = (scav_lv > 0) ? (25 + scav_lv * 28) * (25 + scav_lv * 28) : 0;

    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (g_game.powerups[i].active) {
            g_game.powerups[i].y += g_game.powerups[i].vy;
            if (mag_dist_sq > 0) {
                int pwx = FROM_FIXED(g_game.powerups[i].x);
                int pwy = FROM_FIXED(g_game.powerups[i].y);
                int plx = FROM_FIXED(g_game.player.x);
                int ply = FROM_FIXED(g_game.player.y);
                int dsq = (plx - pwx)*(plx - pwx) + (ply - pwy)*(ply - pwy);
                if (dsq < mag_dist_sq && dsq > 9) {
                    int pull = 40 + scav_lv * 22;
                    int d_x = (plx - pwx);
                    int d_y = (ply - pwy);
                    g_game.powerups[i].x += (d_x > 0 ? pull : -pull);
                    g_game.powerups[i].y += (d_y > 0 ? pull : -pull);
                }
            }
            if (FROM_FIXED(g_game.powerups[i].y) > SCREEN_HEIGHT + 10) {
                g_game.powerups[i].active = false;
            }
        }
    }

    int px = FROM_FIXED(g_game.player.x);
    int py = FROM_FIXED(g_game.player.y);

    for (int b = 0; b < MAX_BULLETS; b++) {
        if (!g_game.bullets[b].active) continue;
        int bx = FROM_FIXED(g_game.bullets[b].x);
        int by = FROM_FIXED(g_game.bullets[b].y);
        int br = g_game.bullets[b].radius;

        if (g_game.bullets[b].enemy) {
            if (g_game.player.invulnerable_timer == 0) {
                int dist_sq = (bx - px)*(bx - px) + (by - py)*(by - py);
                if (dist_sq <= (br + 6)*(br + 6)) {
                    g_game.bullets[b].active = false;
                    damage_player();
                }
            }
            continue;
        }

        bool consumed = false;
        for (int a = 0; a < MAX_ASTEROIDS; a++) {
            if (!g_game.asteroids[a].active) continue;
            int ax = FROM_FIXED(g_game.asteroids[a].x);
            int ay = FROM_FIXED(g_game.asteroids[a].y);
            int ar = g_game.asteroids[a].radius;
            int dist_sq = (bx - ax)*(bx - ax) + (by - ay)*(by - ay);
            if (dist_sq <= (br + ar)*(br + ar)) {
                g_game.asteroids[a].hp -= g_game.bullets[b].damage;
                // Heavy / Nova / Omega pierce logic
                bool pierce = g_game.bullets[b].heavy;
                // Single weak should NOT pierce - only 1 rock at a time
                if (g_settings.weapon_rig == WEAPON_SINGLE) pierce = false;
                // Omega / Nova pierces even large? keep but still consume on large for balance
                if (!pierce || (g_game.asteroids[a].type == AST_LARGE && g_settings.weapon_rig != WEAPON_NOVA)) {
                    g_game.bullets[b].active = false;
                    consumed = true;
                }
                if (g_game.asteroids[a].hp <= 0) {
                    destroy_asteroid(a, true);
                }
                break;
            }
        }
        if (consumed) continue;

        for (int d = 0; d < MAX_DRONES; d++) {
            if (!g_game.drones[d].active) continue;
            int dx = FROM_FIXED(g_game.drones[d].x);
            int dy = FROM_FIXED(g_game.drones[d].y);
            int dist_sq = (bx - dx)*(bx - dx) + (by - dy)*(by - dy);
            if (dist_sq <= (br + 8)*(br + 8)) {
                g_game.drones[d].hp -= g_game.bullets[b].damage;
                g_game.bullets[b].active = false;
                if (g_game.drones[d].hp <= 0) destroy_drone(d, true);
                break;
            }
        }
    }

    if (g_game.player.invulnerable_timer == 0) {
        for (int a = 0; a < MAX_ASTEROIDS; a++) {
            if (!g_game.asteroids[a].active) continue;
            int ax = FROM_FIXED(g_game.asteroids[a].x);
            int ay = FROM_FIXED(g_game.asteroids[a].y);
            int ar = g_game.asteroids[a].radius;
            int dist_sq = (px - ax)*(px - ax) + (py - ay)*(py - ay);
            if (dist_sq <= (5 + ar)*(5 + ar)) {
                destroy_asteroid(a, false);
                damage_player();
                break;
            }
        }
        for (int d = 0; d < MAX_DRONES; d++) {
            if (!g_game.drones[d].active) continue;
            int dx = FROM_FIXED(g_game.drones[d].x);
            int dy = FROM_FIXED(g_game.drones[d].y);
            int dist_sq = (px - dx)*(px - dx) + (py - dy)*(py - dy);
            if (dist_sq <= (5 + 8)*(5 + 8)) {
                destroy_drone(d, false);
                damage_player();
                break;
            }
        }
    }

    int max_shields = get_max_shields();
    int max_lives = get_max_lives() + 1; // allow pickup slightly over
    int rapid_duration = get_rapid_duration();

    for (int p = 0; p < MAX_POWERUPS; p++) {
        if (!g_game.powerups[p].active) continue;
        int pow_x = FROM_FIXED(g_game.powerups[p].x);
        int pow_y = FROM_FIXED(g_game.powerups[p].y);
        int dist_sq = (px - pow_x)*(px - pow_x) + (py - pow_y)*(py - pow_y);
        if (dist_sq <= (6 + 6)*(6 + 6)) {
            if (g_game.powerups[p].type == PWR_SHIELD) {
                if (g_game.player.shield_charges < max_shields) g_game.player.shield_charges++;
            } else if (g_game.powerups[p].type == PWR_RAPID) {
                g_game.player.rapid_fire_timer = rapid_duration;
            } else if (g_game.powerups[p].type == PWR_REPAIR) {
                if (g_game.player.lives < max_lives) g_game.player.lives++;
            }
            g_game.score += 75;
            award_coins(15);
            g_game.powerups[p].active = false;
            audio_play_sfx(SFX_PICKUP);
        }
    }

    if (g_game.mode == GAME_MODE_ENDLESS || g_game.mode == GAME_MODE_OVERDRIVE) {
        update_continuous_modes();
    } else {
        int active_enemies = 0;
        for (int i = 0; i < MAX_ASTEROIDS; i++) if (g_game.asteroids[i].active) active_enemies++;
        for (int i = 0; i < MAX_DRONES; i++) if (g_game.drones[i].active) active_enemies++;

        if (active_enemies == 0) {
            g_game.intermission_timer--;
            if (g_game.intermission_timer <= 0) {
                begin_wave();
                g_game.intermission_timer = 50; // shorter intermission for harder feel
            }
        } else {
            g_game.intermission_timer = 50;
        }
    }
}

void game_update(void) {
    for (int tick = 0; tick < GAME_SPEED_MULTIPLIER; tick++) {
        game_update_tick();
    }
}

static void game_draw_static(void) {
    starfield_draw_base(0, 0);
    int wave_x = (SCREEN_WIDTH - 52) / 2;
    int right_card_x = SCREEN_WIDTH - 75;
    gfx_draw_glass_card(3, 2, 72, 16, PAL_BTN_BORDER, 14);
    gfx_draw_glass_card(wave_x, 2, 52, 16, PAL_BTN_BORDER, 14);
    gfx_draw_glass_card(right_card_x, 2, 72, 16, PAL_BTN_BORDER, 14);
    gfx_draw_text(SCREEN_WIDTH - 82, SCREEN_HEIGHT - 10, "DASH", PAL_TEXT_WHITE);
}

void game_draw(void) {
    int ox = g_game.shake_x;
    int oy = g_game.shake_y;

    if (!s_game_static_valid) {
        gfx_set_target(gfx_static_layer);
        game_draw_static();
        gfx_set_target(NULL);
        s_game_static_valid = true;
    }
    gfx_apply_static();

    starfield_draw_stars(ox, oy);

    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (g_game.powerups[i].active) {
            int px = FROM_FIXED(g_game.powerups[i].x) - 5 + ox;
            int py = FROM_FIXED(g_game.powerups[i].y) - 5 + oy;
            const u8* spr = (g_game.powerups[i].type == PWR_SHIELD) ? spr_pwr_shield :
                            ((g_game.powerups[i].type == PWR_RAPID) ? spr_pwr_rapid : spr_pwr_repair);
            gfx_draw_sprite(px, py, 10, 10, spr);
        }
    }

    for (int i = 0; i < MAX_BULLETS; i++) {
        if (g_game.bullets[i].active) {
            int bx = FROM_FIXED(g_game.bullets[i].x) + ox;
            int by = FROM_FIXED(g_game.bullets[i].y) + oy;
            if (g_game.bullets[i].enemy) {
                gfx_draw_laser(bx, by, false, g_settings.laser_index, s_game_frame, true);
            } else {
                gfx_draw_laser(bx, by, g_game.bullets[i].heavy,
                               g_settings.laser_index, s_game_frame, false);
            }
        }
    }

    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        if (g_game.asteroids[i].active) {
            int ax = FROM_FIXED(g_game.asteroids[i].x) + ox;
            int ay = FROM_FIXED(g_game.asteroids[i].y) + oy;
            if (g_game.asteroids[i].type == AST_LARGE) {
                gfx_draw_sprite(ax - 12, ay - 12, 24, 24, spr_ast_large);
            } else if (g_game.asteroids[i].type == AST_MED_A) {
                gfx_draw_sprite(ax - 8, ay - 8, 16, 16, spr_ast_med_a);
            } else if (g_game.asteroids[i].type == AST_MED_B) {
                gfx_draw_sprite(ax - 8, ay - 8, 16, 16, spr_ast_med_b);
            } else if (g_game.asteroids[i].type == AST_SMALL) {
                gfx_draw_sprite(ax - 5, ay - 5, 10, 10, spr_ast_small);
            } else {
                gfx_draw_sprite(ax - 3, ay - 3, 6, 6, spr_ast_tiny);
            }
        }
    }

    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (g_game.particles[i].active) {
            int px = FROM_FIXED(g_game.particles[i].x) + ox;
            int py = FROM_FIXED(g_game.particles[i].y) + oy;
            gfx_draw_pixel(px, py, g_game.particles[i].color);
        }
    }

    for (int i = 0; i < MAX_DRONES; i++) {
        if (g_game.drones[i].active) {
            int dx = FROM_FIXED(g_game.drones[i].x) - 10 + ox;
            int dy = FROM_FIXED(g_game.drones[i].y) - 8 + oy;
            gfx_draw_enemy_ship(dx, dy);
        }
    }

    if (!g_game.is_game_over) {
        bool visible = true;
        if (g_game.player.invulnerable_timer > 0) {
            visible = (g_game.player.invulnerable_timer & 2) != 0;
        }
        if (visible) {
            int px = FROM_FIXED(g_game.player.x) - 10 + ox;
            int py = FROM_FIXED(g_game.player.y) - 8 + oy;
            int accent = g_settings.accent_index;
            if (accent < 0 || accent >= NUM_ACCENTS) accent = 1;
            gfx_draw_ship(px, py, accent, s_game_frame);
            if (g_game.player.shield_charges > 0) {
                gfx_draw_sprite(px - 2, py - 4, 24, 24, spr_shield_bubble);
            }
        }
    }

    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (g_game.explosions[i].active) {
            int ex = g_game.explosions[i].x - 12 + ox;
            int ey = g_game.explosions[i].y - 12 + oy;
            int f = g_game.explosions[i].frame;
            if (f < 9) {
                gfx_draw_sprite(ex, ey, 24, 24, spr_explosion[f]);
            }
        }
    }

    char buf[32];
    siprintf(buf, "%06u", (unsigned int)g_game.score);
    gfx_draw_text(6, 4, buf, PAL_TEXT_WHITE);

    int wave_x = (SCREEN_WIDTH - 52) / 2;
    siprintf(buf, "W%02d", g_game.wave);
    gfx_draw_text_centered(wave_x, 4, 52, buf, PAL_TEXT_CYAN);

    int right_card_x = SCREEN_WIDTH - 75;
    siprintf(buf, "$%u", (unsigned int)g_settings.coins);
    gfx_draw_text(right_card_x + 7, 4, buf, PAL_TEXT_GOLD);

    for (int i = 0; i < g_game.player.lives && i < 7; i++) {
        gfx_draw_char(right_card_x + 3 + i * 6, 11, '^', PAL_TEXT_GREEN);
    }
    for (int i = 0; i < g_game.player.shield_charges && i < 6; i++) {
        gfx_draw_char(right_card_x + 39 + i * 6, 11, '*', PAL_TEXT_CYAN);
    }

    if (g_game.combo > 1) {
        siprintf(buf, "x%d", g_game.combo);
        u8 acc = gfx_get_accent_color(g_settings.accent_index);
        gfx_draw_text(6, 20, buf, acc);
        int max_comb_t = get_max_combo_timer();
        gfx_draw_progress_bar(20, 22, 42, 4, g_game.combo_timer, max_comb_t, acc, 18);
    }

    if (g_game.player.rapid_fire_timer > 0) {
        siprintf(buf, "RAPID %d", (g_game.player.rapid_fire_timer + 59) / 60);
        gfx_draw_text_centered((SCREEN_WIDTH - 80) / 2, 20, 80, buf, PAL_TEXT_GOLD);
    }

    int max_dash_cd = get_max_dash_cooldown();
    int dash_ready = max_dash_cd - g_game.player.dash_cooldown;
    u8 dash_col = (g_game.player.dash_cooldown == 0) ? PAL_TEXT_GREEN : gfx_get_accent_color(g_settings.accent_index);
    gfx_draw_progress_bar(SCREEN_WIDTH - 54, SCREEN_HEIGHT - 9, 50, 5, dash_ready, max_dash_cd, dash_col, 18);

    if (g_game.wave_banner_timer > 0) {
        int banner_w = 120;
        int banner_h = 24;
        int bx = (SCREEN_WIDTH - banner_w) / 2;
        int by = 68;
        gfx_draw_glass_card(bx, by, banner_w, banner_h, PAL_TEXT_WHITE, 15);
        if (g_game.mode == GAME_MODE_ENDLESS) {
            gfx_draw_text_centered(bx, by + 4, banner_w, "ENDLESS", PAL_TEXT_WHITE);
            gfx_draw_text_centered(bx, by + 13, banner_w, "SURVIVE!", PAL_TEXT_CYAN);
        } else if (g_game.mode == GAME_MODE_OVERDRIVE) {
            gfx_draw_text_centered(bx, by + 4, banner_w, "OVERDRIVE", PAL_TEXT_GOLD);
            gfx_draw_text_centered(bx, by + 13, banner_w, "90 SECONDS", PAL_TEXT_CYAN);
        } else {
            siprintf(buf, "WAVE %02d", g_game.wave);
            gfx_draw_text_centered(bx, by + 4, banner_w, buf, PAL_TEXT_WHITE);
            gfx_draw_text_centered(bx, by + 13, banner_w, "GET READY!", PAL_TEXT_CYAN);
        }
    }
}
