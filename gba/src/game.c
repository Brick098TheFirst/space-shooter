#include "game.h"
#include "renderer.h"
#include "audio.h"
#include "save.h"
#include "starfield.h"
#include <stdlib.h>
#include <string.h>
#ifdef PLATFORM_HOST
#include <math.h>
#endif

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
    /* Compare as int: GameMode may be an unsigned underlying type, which makes
     * `mode < GAME_MODE_WAVES` a tautology and trips -Wtype-limits. */
    int m = (int)mode;
    if (m < (int)GAME_MODE_WAVES || m > (int)GAME_MODE_OVERDRIVE) m = (int)GAME_MODE_WAVES;
    s_game_mode = (GameMode)m;
}

GameMode game_get_mode(void) {
    return s_game_mode;
}

static void finish_run(bool time_up);
static void spawn_boss(void);

/* Full bosses every 10th wave; a lighter mini-boss on the 5s (5, 15, 25...). */
static inline bool is_full_boss_wave(int wave) {
    return wave > 0 && (wave % 10) == 0;
}
static inline bool is_mini_boss_wave(int wave) {
    return wave > 0 && (wave % 10) == 5;
}
static inline bool is_boss_wave(int wave) {
    return is_full_boss_wave(wave) || is_mini_boss_wave(wave);
}

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

static int get_dash_invuln(void) {
    int lv = g_settings.upgrade_levels[UPG_DASH];
    return 16 + lv * 3; // 16 .. 31 (post-hit invulnerability)
}

static int get_fire_rate_level(void) {
    int lv = g_settings.upgrade_levels[UPG_FIRE_RATE];
    if (lv < 0) lv = 0;
    if (lv > 5) lv = 5;
    return lv;
}
// Multiplier for cooldown: level0 slow, level5 very fast.
#ifdef PLATFORM_HOST
// Android: base cooldown is 28 ticks (~2.1/s at lv0), ~6/s at lv5.
static int get_fire_rate_cooldown_mult(void) {
    static const int tbl[6] = { 256, 215, 178, 148, 122, 102 };
    return tbl[get_fire_rate_level()];
}
#else
static int get_fire_rate_cooldown_mult(void) {
    static const int tbl[6] = { 256, 210, 165, 125, 95, 75 };
    return tbl[get_fire_rate_level()];
}
#endif

static int get_damage_bonus(void) {
    int lv = g_settings.upgrade_levels[UPG_DAMAGE];
    if (lv < 0) lv = 0;
    if (lv > 5) lv = 5;
    return lv; // 0 .. 5 extra damage
}

#ifdef PLATFORM_HOST
/* True endgame build: NOVA rig + the final Apex crystal. Rocks still
 * evaporate; boss HP scales up so the same volley takes ~15 shots. */
static bool is_oneshot_build(void) {
    return (g_settings.weapon_rig == WEAPON_NOVA && g_settings.laser_index == LASER_FINAL_IDX);
}
#endif

#ifdef PLATFORM_HOST
/* Android damage ladder: each crystal is strictly stronger than the last.
 * Early lasers are tiny; Apex (23) is the only true nuke. */
static int get_laser_bonus(void) {
    static const int bonus[24] = {
        0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 6,
        7, 8, 9, 10, 12, 14, 16, 18, 20, 23, 26, 32
    };
    int idx = g_settings.laser_index;
    if (idx < 0 || idx >= NUM_LASERS) return 0;
    return bonus[idx];
}

/* Returns true for rigs at the "god tier" that pierce everything without one-shot */
static bool is_top_tier_build(void) {
    return g_settings.weapon_rig >= WEAPON_QUANTUM;
}
#else
static int get_laser_bonus(void) {
    // 12 lasers - starter is weak, final Omega one-shots everything.
    static const int bonus[12] = { 0, 0, 1, 1, 2, 3, 4, 5, 2, 3, 5, 7 };
    int idx = g_settings.laser_index;
    if (idx < 0 || idx >= 12) return 0;
    return bonus[idx];
}
#endif

/* Base damage of a single main bullet for the equipped rig. */
static int get_weapon_base_damage(WeaponRig rig) {
#ifdef PLATFORM_HOST
    /* Android rebalance:
     *   SINGLE/TWIN/SPREAD = 1 (peashooter - needs ~5 hits on big rock early)
     *   FOCUSED/TRIPLE     = 2
     *   PLASMA             = 3
     *   QUANTUM            = 5  (strong, not oneshot)
     *   NOVA base          = 8  (strong base; +omega crystal = 28 -> oneshot everything)
     * The "oneshot everything" threshold is NOVA+OMEGA only (see is_oneshot_build).
     */
    switch (rig) {
        case WEAPON_SINGLE:
        case WEAPON_TWIN:
        case WEAPON_SPREAD:  return 1;
        case WEAPON_FOCUSED:
        case WEAPON_TRIPLE:  return 2;
        case WEAPON_PLASMA:  return 3;
        case WEAPON_QUANTUM: return 5;
        case WEAPON_NOVA:    return 8;
        default:             return 1;
    }
#else
    switch (rig) {
        case WEAPON_SINGLE:
        case WEAPON_TWIN:
        case WEAPON_SPREAD:  return 1;
        case WEAPON_FOCUSED:
        case WEAPON_TRIPLE:  return 2;
        case WEAPON_PLASMA:  return 3;
        case WEAPON_QUANTUM: return 4;
        case WEAPON_NOVA:    return 5;
        default:             return 1;
    }
#endif
}

/* Big laser beam damage = bullet damage per tick (divided by tick divisor so
 * it doesn't instantly mulch the field). Beam upgrade (UPG_DASH) boosts it
 * +25% per level. */
static int get_beam_damage(void) {
#ifdef PLATFORM_HOST
    int dmg = get_weapon_base_damage(g_settings.weapon_rig)
            + get_damage_bonus() + get_laser_bonus();
    /* NOVA+OMEGA beam is the endgame nuke - it tears through anything */
    if (is_oneshot_build()) dmg = 200;
    int lv = g_settings.upgrade_levels[UPG_DASH];
    if (lv < 0) lv = 0;
    if (lv > UPG_MAX_LEVEL) lv = UPG_MAX_LEVEL;
    dmg = (dmg * (100 + lv * 25)) / 100;
    return dmg;
#else
    int dmg = get_weapon_base_damage(g_settings.weapon_rig)
            + get_damage_bonus() + get_laser_bonus();
    if (g_settings.weapon_rig == WEAPON_NOVA && g_settings.laser_index == 11) {
        dmg += 2; // omega crystal + nova synergy
    }
    int lv = g_settings.upgrade_levels[UPG_DASH];
    if (lv < 0) lv = 0;
    if (lv > UPG_MAX_LEVEL) lv = UPG_MAX_LEVEL;
    dmg = (dmg * (100 + lv * 25)) / 100;
    return dmg;
#endif
}

/* Big laser timing: 2s charge, 3s beam. Ticks run at 90/s on the Android
 * host and 120/s on GBA (2 sim ticks per 60fps frame). */
#ifdef PLATFORM_HOST
#define BEAM_CHARGE_TICKS (2 * 90)
#define BEAM_DURATION_TICKS (3 * 90)
/* Beam ticks 90/s; divide dmg so a 5HP big rock at base gear takes ~1s of
 * sustained beam, while the oneshot build melts instantly. */
#define BEAM_DMG_DIVISOR 45
#define BEAM_DMG_ROUND   22
#else
#define BEAM_CHARGE_TICKS (2 * 120)
#define BEAM_DURATION_TICKS (3 * 120)
#define BEAM_DMG_DIVISOR 10
#define BEAM_DMG_ROUND   5
#endif

/* Per-tick beam damage in 8.8 fixed point, normalized for the platform's
 * simulation rate so the beam feels the same on both. */
#define BEAM_TICK_DAMAGE() (((get_beam_damage() << 8) + BEAM_DMG_ROUND) / BEAM_DMG_DIVISOR)

/* Rock speeds: big rocks drift slow, medium keep current speed, small/tiny
 * scream past. 256 = 1.0x. */
static int asteroid_speed_mult(AsteroidType type) {
    switch (type) {
        case AST_LARGE:            return 179; // 0.70x slow
        case AST_MED_A:
        case AST_MED_B:            return 256; // 1.00x current speed
        case AST_SMALL:            return 666; // 2.60x very fast
        default:                   return 768; // tiny 3.00x
    }
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

/* Combo used to multiply coins 1:1 (x15 = 15x cash) which printed money.
 * Soft ramp for most of the chain, then a real payday at max 15x. */
static int combo_coin_pct(int combo) {
    if (combo <= 1) return 100;
    if (combo >= 15) return 450; // 4.5x jackpot once you lock 15x
    // combo 2..14: +15% per step -> 115% .. 295%  (was 200%..1400%)
    return 100 + (combo - 1) * 15;
}

static int s_wave_reinforcements = 0;
static bool s_combo15_bonus = false;

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
#ifdef PLATFORM_HOST
            /* Android: the shield and rapid-fire drops are removed — the only
             * powerup left is the life (repair) pickup, and it keeps its
             * original rarity (the old 20% repair slice of the roll). */
            if (roll >= 20) return;
            g_game.powerups[i].type = PWR_REPAIR;
#else
            if (roll < 40) g_game.powerups[i].type = PWR_SHIELD;
            else if (roll < 80) g_game.powerups[i].type = PWR_RAPID;
            else g_game.powerups[i].type = PWR_REPAIR;
#endif
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
    g_settings.coins += (coin_t)earned;
    if (g_settings.coins > COINS_MAX) g_settings.coins = COINS_MAX;
    save_write();
}

static void award_score(int base_pts) {
    g_game.score += base_pts * g_game.combo;
    int max_combo = get_max_combo_cap();
    if (g_game.combo < max_combo) g_game.combo++;
    g_game.combo_timer = get_max_combo_timer();
    // Extra lump sum the first time a chain hits 15x.
    if (g_game.combo >= 15 && !s_combo15_bonus) {
        s_combo15_bonus = true;
        award_coins(75);
    }
}

static int coins_for_asteroid(AsteroidType type) {
    switch (type) {
        case AST_LARGE: return 20;
        case AST_MED_A:
        case AST_MED_B: return 12;
        case AST_SMALL: return 30; // fast & tricky - pays a lot
        default:        return 18; // tiny
    }
}

static void damage_player(void) {
    if (g_game.player.invulnerable_timer > 0) return;
    int px = FROM_FIXED(g_game.player.x);
    int py = FROM_FIXED(g_game.player.y);
    platform_queue_haptic(HAPTIC_HIT);

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
    s_combo15_bonus = false;
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
            g_game.asteroids[i].hp_frac = 0;
            if (type == AST_LARGE) {
                g_game.asteroids[i].radius = 12;
#ifdef PLATFORM_HOST
                /* Android: starter laser does 1dmg, need 5 hits. HP scales
                 * slowly with wave; NOVA+OMEGA overrides to oneshot. */
                g_game.asteroids[i].hp = 5 + (g_game.wave / 4);
                if (g_game.asteroids[i].hp > 30) g_game.asteroids[i].hp = 30;
#else
                g_game.asteroids[i].hp = 2 + (g_game.wave / 8);
                if (g_game.asteroids[i].hp > 12) g_game.asteroids[i].hp = 12;
#endif
            } else if (type == AST_MED_A || type == AST_MED_B) {
                g_game.asteroids[i].radius = 8;
#ifdef PLATFORM_HOST
                /* Medium: 2-3 hits with starter (1dmg). */
                g_game.asteroids[i].hp = 2 + (g_game.wave / 6);
                if (g_game.asteroids[i].hp > 15) g_game.asteroids[i].hp = 15;
#else
                g_game.asteroids[i].hp = 1 + (g_game.wave / 12);
                if (g_game.asteroids[i].hp > 9) g_game.asteroids[i].hp = 9;
#endif
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
        int combo = g_game.combo; // soft combo cash; jackpot at 15x
        int pts = (t == AST_LARGE) ? 60 : ((t == AST_MED_A || t == AST_MED_B) ? 35 : 20);
        award_score(pts);
        award_coins((coins_for_asteroid(t) * combo_coin_pct(combo)) / 100);
        platform_queue_haptic(HAPTIC_KILL);
        int mult = get_diff_speed_mult() + g_game.wave * 12;
        if (t == AST_LARGE) {
#ifdef PLATFORM_HOST
            /* Android: break into 2 mediums with RANDOM angle spread, not fixed
             * +/-45 degree diagonals. Use random directions. */
            int base = ((160 * mult) >> 8) * asteroid_speed_mult(AST_MED_A) >> 8;
            int spd1 = base + (rand() & 63) - 32;
            int spd2 = base + (rand() & 63) - 32;
            int ang1 = rand() % 256;
            int ang2 = rand() % 256;
            int vx1 = (lu_cos(ang1 * 256) * spd1) >> 12;
            int vy1 = ((lu_sin(ang1 * 256) * spd1) >> 12);
            if (vy1 < base / 2) vy1 = base / 2; // drift downwards
            int vx2 = (lu_cos(ang2 * 256) * spd2) >> 12;
            int vy2 = ((lu_sin(ang2 * 256) * spd2) >> 12);
            if (vy2 < base / 2) vy2 = base / 2;
            // Offset positions slightly
            int offx1 = (rand() & 63) - 32;
            int offx2 = (rand() & 63) - 32;
            spawn_asteroid((rand() & 1) ? AST_MED_A : AST_MED_B,
                           a->x + offx1, a->y, vx1, vy1);
            spawn_asteroid((rand() & 1) ? AST_MED_A : AST_MED_B,
                           a->x + offx2, a->y, vx2, vy2);
#else
            int spd = ((160 * mult) >> 8) * asteroid_speed_mult(AST_MED_A) >> 8;
            spawn_asteroid(AST_MED_A, a->x - TO_FIXED(6), a->y, -spd, spd);
            spawn_asteroid(AST_MED_B, a->x + TO_FIXED(6), a->y, spd, spd);
#endif
        } else if (t == AST_MED_A || t == AST_MED_B) {
#ifdef PLATFORM_HOST
            /* Android: medium rock breaks into 3 random smalls at random angles */
            int base = ((200 * mult) >> 8) * asteroid_speed_mult(AST_SMALL) >> 8;
            for (int k = 0; k < 3; k++) {
                int spd = base + (rand() & 95) - 32;
                int ang = rand() % 256;
                int vx = (lu_cos(ang * 256) * spd) >> 12;
                int vy = (lu_sin(ang * 256) * spd) >> 12;
                if (vy < base / 3) vy = base / 3;
                int offx = (rand() & 63) - 32;
                AsteroidType nt = (rand() & 3) ? AST_SMALL : AST_TINY;
                spawn_asteroid(nt, a->x + offx, a->y, vx, vy);
            }
#else
            int spd = ((200 * mult) >> 8) * asteroid_speed_mult(AST_SMALL) >> 8;
            spawn_asteroid(AST_SMALL, a->x - TO_FIXED(4), a->y, -spd, spd);
            spawn_asteroid(AST_TINY, a->x + TO_FIXED(4), a->y, spd, spd);
#endif
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
        int combo = g_game.combo;
        award_score(120);
        award_coins((45 * combo_coin_pct(combo)) / 100);
        platform_queue_haptic(HAPTIC_KILL);
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
    int tmult = asteroid_speed_mult(type);
    vx = (vx * tmult) >> 8;
    vy = (vy * tmult) >> 8;
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
#ifdef PLATFORM_HOST
        if (g_game.drones[i].hp > 12) g_game.drones[i].hp = 12;
#else
        if (g_game.drones[i].hp > 6) g_game.drones[i].hp = 6;
#endif
        g_game.drones[i].hp_frac = 0;
        g_game.drones[i].active = true;
        return true;
    }
    return false;
}

static void spawn_continuous_threat(void) {
    int threat = current_threat();
    int ast_n = count_active_asteroids();
    int dr_n = count_active_drones();
    int ast_cap = 6 + threat;
    if (g_game.mode == GAME_MODE_OVERDRIVE) ast_cap += 4;
    if (ast_cap > MAX_ASTEROIDS - 12) ast_cap = MAX_ASTEROIDS - 12;
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
        int cd = 48 - threat * 3;
        if (g_game.mode == GAME_MODE_OVERDRIVE) cd -= 16;
        if (cd < 8) cd = 8;
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

    /* Wave 5/15/25... = mini-boss, wave 10/20/30... = full boss. Clear the
     * field so the fight isn't buried under the asteroid horde. */
    if (g_game.mode == GAME_MODE_WAVES && is_boss_wave(g_game.wave)) {
        // Wipe existing threats so the boss fight feels clean.
        for (int i = 0; i < MAX_ASTEROIDS; i++) g_game.asteroids[i].active = false;
        for (int i = 0; i < MAX_BULLETS; i++) g_game.bullets[i].active = false;
        for (int i = 0; i < MAX_BOSS_BULLETS; i++) g_game.boss_bullets[i].active = false;
        for (int i = 0; i < MAX_DRONES; i++) g_game.drones[i].active = false;
        s_wave_reinforcements = 0;
        g_game.spawn_timer = 9999;
        spawn_boss();
        return;
    }

    int diff_extra = (g_settings.difficulty == DIFF_ACE) ? 4 : (g_settings.difficulty == DIFF_CADET ? -1 : 0);
    // Wave 1: ~8, Wave 3: ~18, Wave 5: ~28, Wave 8+: packed field
    int ast_count = 5 + g_game.wave * 3;
    if (g_game.wave >= 3) ast_count += (g_game.wave - 2) * 2;
    ast_count += diff_extra;
    if (ast_count < 3) ast_count = 3;
    if (ast_count > MAX_ASTEROIDS - 14) ast_count = MAX_ASTEROIDS - 14;

    s_wave_reinforcements = 0;
    if (g_game.wave >= 3) {
        s_wave_reinforcements = (g_game.wave - 2) * 4 + g_game.wave;
    }
    g_game.spawn_timer = 48;

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
        AsteroidType type = is_large ? AST_LARGE : (rand() % 2 == 0 ? AST_MED_A : AST_MED_B);
        int tmult = asteroid_speed_mult(type);
        vx = (vx * tmult) >> 8;
        vy = (vy * tmult) >> 8;
        spawn_asteroid(type, x, y, vx, vy);
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
#ifdef PLATFORM_HOST
            if (g_game.drones[i].hp > 12) g_game.drones[i].hp = 12;
#else
            if (g_game.drones[i].hp > 6) g_game.drones[i].hp = 6;
#endif
            g_game.drones[i].hp_frac = 0;
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

/* Boss bullets: separate array so they don't compete with player/drone shots */
static bool add_boss_bullet(int x, int y, int vx, int vy, int heavy) {
    for (int i = 0; i < MAX_BOSS_BULLETS; i++) {
        if (!g_game.boss_bullets[i].active) {
            g_game.boss_bullets[i].x = x;
            g_game.boss_bullets[i].y = y;
            g_game.boss_bullets[i].vx = vx;
            g_game.boss_bullets[i].vy = vy;
            g_game.boss_bullets[i].radius = heavy ? 3 : 2;
            g_game.boss_bullets[i].damage = heavy ? 2 : 1;
            g_game.boss_bullets[i].life = 200;
            g_game.boss_bullets[i].enemy = true;
            g_game.boss_bullets[i].heavy = heavy;
            g_game.boss_bullets[i].active = true;
            return true;
        }
    }
    return false;
}

/* Damage of one trigger-pull with the current loadout (all bullets that
 * fire_player_weapon() would spawn). Used so a maxed NOVA volley still
 * takes ~15 shots instead of deleting the bar. */
static int current_volley_damage(void) {
    WeaponRig rig = g_settings.weapon_rig;
    int dmg_bonus = get_damage_bonus() + get_laser_bonus();
    int base = get_weapon_base_damage(rig);
#ifdef PLATFORM_HOST
    if (is_oneshot_build()) base = 99;
#endif
    int d = base + dmg_bonus;
    if (d < 1) d = 1;
    switch (rig) {
        case WEAPON_SINGLE:  return d;
        case WEAPON_TWIN:    return d * 2;
        case WEAPON_SPREAD:  return d * 3;
        case WEAPON_FOCUSED: return d;
        case WEAPON_TRIPLE:  return (1 + dmg_bonus) * 2 + d;
        case WEAPON_PLASMA:  return d * 2;
        case WEAPON_QUANTUM: return d * 3;
        case WEAPON_NOVA: {
            int nd = d;
            if (g_settings.laser_index == LASER_FINAL_IDX) nd += 2;
            return nd * 3 + (nd - 1) * 2;
        }
        default: return d;
    }
}

/* One maxed Focused Beam shot (beam rig + Damage 5 + best crystal). Weak
 * loadouts are measured against this floor so a starter laser cannot
 * reasonably solo the boss — you need good stuff. */
static int maxed_beam_shot_damage(void) {
    int dmg = get_weapon_base_damage(WEAPON_FOCUSED) + UPG_MAX_LEVEL;
#ifdef PLATFORM_HOST
    dmg += 32; /* Apex crystal floor for boss HP */
#else
    dmg += 7;
#endif
    if (dmg < 1) dmg = 1;
    return dmg;
}

/* Full boss: ~15 shots of a maxed Focused Beam (or 15 volleys of whatever
 * stronger rig you brought). Mini-bosses are 4× easier. Never shrink HP
 * for the NOVA+OMEGA trash-mob nuke — that combo must still work for it. */
static int boss_max_hp(void) {
    int per_shot = current_volley_damage();
    int beam_floor = maxed_beam_shot_damage();
    if (per_shot < beam_floor) per_shot = beam_floor;

    int boss_tier;
    if (is_mini_boss_wave(g_game.wave))
        boss_tier = (g_game.wave / 10) + 1; /* wave 5 → 1, wave 15 → 2 */
    else
        boss_tier = (g_game.wave > 0) ? (g_game.wave / 10) : 1;
    if (boss_tier < 1) boss_tier = 1;

    int hp = per_shot * 15 * boss_tier;
    if (is_mini_boss_wave(g_game.wave))
        hp = (hp + 3) / 4;
    if (hp < 8) hp = 8;
    return hp;
}

static void spawn_boss(void) {
    memset(&g_game.boss, 0, sizeof(Boss));
    g_game.boss.x = TO_FIXED(SCREEN_WIDTH / 2);
    g_game.boss.y = -TO_FIXED(40);
    g_game.boss.vx = TO_FIXED(1) + 40; // ~1.16 pixels/tick drift right
    g_game.boss.vy = TO_FIXED(1) + 40; // ~1.16 pixels/tick descent
    g_game.boss.mini = is_mini_boss_wave(g_game.wave);
    g_game.boss.hp_max = boss_max_hp();
    g_game.boss.hp = g_game.boss.hp_max;
    g_game.boss.hp_frac = 0;
    g_game.boss.phase = BOSS_IDLE;
    g_game.boss.phase_timer = 90;
    g_game.boss.beam_timer = 0;
    g_game.boss.beam_width = TO_FIXED(10);
    g_game.boss.flash_timer = 0;
    g_game.boss.active = true;
    g_game.boss.cooldown = g_game.boss.mini ? 80 : 60;
    g_game.boss.fire_state = 0;
    g_game.boss.sweep_dir = 1;
    g_game.boss_active = true;
    g_game.boss_hit_flash = 0;
    // Clear normal drones/asteroids to make room for the boss entrance
    for (int i = 0; i < MAX_DRONES; i++) g_game.drones[i].active = false;
}

static int boss_hit_radius(const Boss* b) {
    return b->mini ? 12 : 16;
}

static void defeat_boss(Boss* b, int boss_cx, int boss_cy) {
    g_game.boss_active = false;
    b->active = false;
    int blasts = b->mini ? 4 : 6;
    for (int k = 0; k < blasts; k++)
        trigger_explosion(boss_cx + (rand() % 20) - 10, boss_cy + (rand() % 20) - 10);
    g_game.shake_timer = b->mini ? 18 : 30;
    int tier = (g_game.wave / 10) + 1;
    if (b->mini) {
        award_score(400 * tier);
        award_coins(125 + g_game.wave * 8);
        try_spawn_powerup(boss_cx, boss_cy, 100);
        try_spawn_powerup(boss_cx + 8, boss_cy + 6, 100);
    } else {
        award_score(1500 * tier);
        award_coins(500 + g_game.wave * 25);
        try_spawn_powerup(boss_cx - 10, boss_cy, 100);
        try_spawn_powerup(boss_cx + 10, boss_cy + 8, 100);
        try_spawn_powerup(boss_cx, boss_cy - 6, 100);
    }
    platform_queue_haptic(HAPTIC_BEAM);
    g_game.intermission_timer = 90;
}

static void boss_fire_burst(int n, int speed_fixed) {
    int bx = FROM_FIXED(g_game.boss.x);
    int by = FROM_FIXED(g_game.boss.y) + 14;
    for (int i = 0; i < n; i++) {
        int ang = (i * 256 / n) + (s_game_frame & 0x3F);
        // lu_cos/lu_sin return 4.12 fixed (range -4096..4096). Multiply by
        // speed_fixed (8.8) then shift to get 8.8 velocity.
        int vx = (lu_cos(ang * 256) * speed_fixed) >> 12;
        int vy = (lu_sin(ang * 256) * speed_fixed) >> 12;
        if (vy < TO_FIXED(1)/2) vy = TO_FIXED(1)/2; // always move down
        add_boss_bullet(TO_FIXED(bx), TO_FIXED(by), vx, vy, (i & 1) == 0);
    }
}

#ifndef PLATFORM_HOST
/* Integer square root (Newton). Keeps the aimed spread identical on GBA,
 * which has no FPU — software doubles here would be very expensive. */
static u32 isqrt32(u32 v) {
    if (v == 0) return 0;
    u32 x = v, y = (x + 1) >> 1;
    while (y < x) {
        x = y;
        y = (x + v / x) >> 1;
    }
    return x;
}
#endif

static void boss_fire_spread_at_player(int speed_fixed) {
    int bx = FROM_FIXED(g_game.boss.x);
    int by = FROM_FIXED(g_game.boss.y) + 14;
    int px = FROM_FIXED(g_game.player.x);
    int py = FROM_FIXED(g_game.player.y);
    int dx = px - bx, dy = py - by;
#ifdef PLATFORM_HOST
    double mag = hypot((double)dx, (double)dy);
    if (mag < 1.0) mag = 1.0;
    // speed_fixed is 8.8 fixed pixels/tick (e.g. TO_FIXED(4) = 4 pixels/tick)
    double sp = (double)speed_fixed / 256.0;
    double ndx = (double)dx / mag;
    double ndy = (double)dy / mag;
    for (int k = -1; k <= 1; k++) {
        double spread = (double)k * 0.25;
        double vxd = ndx + spread;
        double vyd = ndy;
        double vmag = hypot(vxd, vyd);
        if (vmag < 0.01) vmag = 0.01;
        vxd = vxd / vmag * sp;
        vyd = vyd / vmag * sp;
        add_boss_bullet(TO_FIXED(bx), TO_FIXED(by),
                        (int)(vxd * 256.0), (int)(vyd * 256.0), k == 0);
    }
#else
    /* Same aimed 3-way spread in pure 8.8 fixed point: the GBA has no FPU and
     * software doubles in the boss fire path would blow the frame budget. */
    int mag = (int)isqrt32((u32)(dx * dx + dy * dy));
    if (mag < 1) mag = 1;
    int ndx = (dx * 256) / mag; // 8.8 unit vector
    int ndy = (dy * 256) / mag;
    for (int k = -1; k <= 1; k++) {
        int vxd = ndx + k * 64; // +/-0.25 in 8.8
        int vyd = ndy;
        int vmag = (int)isqrt32((u32)(vxd * vxd + vyd * vyd));
        if (vmag < 3) vmag = 3;
        int vx = (vxd * speed_fixed) / vmag;
        int vy = (vyd * speed_fixed) / vmag;
        add_boss_bullet(TO_FIXED(bx), TO_FIXED(by), vx, vy, k == 0);
    }
#endif
}

/* ── Weapon system ──────────────────────────────────────────────────── */
#ifdef PLATFORM_HOST
/* Android: fire rate is IDENTICAL across all rigs — it only scales with the
 * Fire Rate upgrade. Rigs differ in DAMAGE and PATTERN only, so your "weapon
 * upgrade" doesn't also secretly give you a faster trigger finger. */
static int get_weapon_base_cooldown(WeaponRig rig) {
    (void)rig;
    return 28; // baseline ~2.1/sec
}
#else
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
#endif

static void fire_player_weapon(void) {
    bool rapid = (g_game.player.rapid_fire_timer > 0);
    int px = g_game.player.x;
    int py = g_game.player.y;
    int dmg_bonus = get_damage_bonus() + get_laser_bonus();
    // Omega and Rainbow add extra pierce
    bool is_omega = (g_settings.laser_index == LASER_FINAL_IDX);
    /* Rainbow crystal is purely a visual variant (see gfx_draw_laser), so it
     * intentionally has no effect on the damage/cooldown maths here. */

    WeaponRig rig = g_settings.weapon_rig;
    int base_cd = get_weapon_base_cooldown(rig);
    int fr_mult = get_fire_rate_cooldown_mult(); // 75..256
    int cooldown = (base_cd * fr_mult) >> 8;
#ifdef PLATFORM_HOST
    /* Android: laser crystal does NOT affect fire rate at all. Only the
     * fire-rate upgrade does. Omega still gets no extra rate buff. */
    if (rapid) cooldown = (cooldown * 2) / 5; // rapid cuts to 40%
    if (cooldown < 4) cooldown = 4; // absolute min ~15/sec
#else
    if (rapid) cooldown = (cooldown * 2) / 5; // rapid cuts to 40%
    if (cooldown < 3) cooldown = 3; // absolute min 20/sec
    if (is_omega) { if (cooldown > 3) cooldown--; } // omega tiny buff
#endif

    int base = get_weapon_base_damage(rig);
#ifdef PLATFORM_HOST
    /* NOVA + OMEGA is the ONLY combo that truly oneshots anything.  Force
     * damage to a huge number so nothing survives even a single bullet. */
    if (is_oneshot_build()) {
        base = 99;
    }
#endif
    switch (rig) {
        case WEAPON_SINGLE: {
            // One weak laser that can only break 1 small rock at a time (low dmg, no pierce)
            add_player_bullet(px, py - TO_FIXED(8), 0, -TO_FIXED(6), base + dmg_bonus, false);
            break;
        }
        case WEAPON_TWIN: {
            add_player_bullet(px - TO_FIXED(4), py - TO_FIXED(6), 0, -TO_FIXED(5), base + dmg_bonus, false);
            add_player_bullet(px + TO_FIXED(4), py - TO_FIXED(6), 0, -TO_FIXED(5), base + dmg_bonus, false);
            break;
        }
        case WEAPON_SPREAD: {
            add_player_bullet(px, py - TO_FIXED(6), 0, -TO_FIXED(5), base + dmg_bonus, false);
            add_player_bullet(px - TO_FIXED(4), py - TO_FIXED(4), -TO_FIXED(1), -TO_FIXED(4), base + dmg_bonus, false);
            add_player_bullet(px + TO_FIXED(4), py - TO_FIXED(4), TO_FIXED(1), -TO_FIXED(4), base + dmg_bonus, false);
            break;
        }
        case WEAPON_FOCUSED: {
            // Heavy single that pierces small rocks
            bool heavy = true;
            add_player_bullet(px, py - TO_FIXED(8), 0, -TO_FIXED(6), base + dmg_bonus, heavy);
            break;
        }
        case WEAPON_TRIPLE: {
            add_player_bullet(px - TO_FIXED(6), py - TO_FIXED(6), 0, -TO_FIXED(5), 1 + dmg_bonus, false);
            add_player_bullet(px, py - TO_FIXED(8), 0, -TO_FIXED(6), base + dmg_bonus, true);
            add_player_bullet(px + TO_FIXED(6), py - TO_FIXED(6), 0, -TO_FIXED(5), 1 + dmg_bonus, false);
            break;
        }
        case WEAPON_PLASMA: {
            add_player_bullet(px - TO_FIXED(5), py - TO_FIXED(7), -50, -TO_FIXED(5), base + dmg_bonus, true);
            add_player_bullet(px + TO_FIXED(5), py - TO_FIXED(7), 50, -TO_FIXED(5), base + dmg_bonus, true);
            break;
        }
        case WEAPON_QUANTUM: {
            add_player_bullet(px - TO_FIXED(4), py - TO_FIXED(8), 0, -TO_FIXED(7), base + dmg_bonus, true);
            add_player_bullet(px + TO_FIXED(4), py - TO_FIXED(8), 0, -TO_FIXED(7), base + dmg_bonus, true);
            add_player_bullet(px, py - TO_FIXED(10), 0, -TO_FIXED(8), base + dmg_bonus, true);
            break;
        }
        case WEAPON_NOVA: {
            // Final god weapon: 5 bullets, massive dmg, piercing, fast
            int d = base + dmg_bonus;
            if (is_omega) d += 2; // extra crazy with omega crystal
            add_player_bullet(px, py - TO_FIXED(10), 0, -TO_FIXED(9), d, true);
            add_player_bullet(px - TO_FIXED(5), py - TO_FIXED(8), -70, -TO_FIXED(8), d, true);
            add_player_bullet(px + TO_FIXED(5), py - TO_FIXED(8), 70, -TO_FIXED(8), d, true);
            add_player_bullet(px - TO_FIXED(9), py - TO_FIXED(6), -140, -TO_FIXED(6), d-1, true);
            add_player_bullet(px + TO_FIXED(9), py - TO_FIXED(6), 140, -TO_FIXED(6), d-1, true);
            break;
        }
        default: {
            add_player_bullet(px, py - TO_FIXED(6), 0, -TO_FIXED(5), base + dmg_bonus, false);
            break;
        }
    }
    g_game.player.fire_cooldown = cooldown;
    audio_play_sfx(SFX_LASER);
}

void game_init(void) {
    memset(&g_game, 0, sizeof(GameState));
}

/* The HUD backing card layout depends on the (runtime) screen width, so the
 * cached static layer must be redrawn when the Android viewport changes. */
void game_request_full_redraw(void) {
    s_game_static_valid = false;
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
    s_combo15_bonus = false;
    s_wave_reinforcements = 0;
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
        if (g_game.combo_timer == 0) {
            g_game.combo = 1;
            s_combo15_bonus = false;
        }
    }

    if (g_game.player.fire_cooldown > 0) g_game.player.fire_cooldown--;
    if (g_game.player.invulnerable_timer > 0) g_game.player.invulnerable_timer--;
    if (g_game.player.rapid_fire_timer > 0) g_game.player.rapid_fire_timer--;

    int mx = 0, my = 0;
    if (key_is_down(KEY_LEFT)) mx -= 1;
    if (key_is_down(KEY_RIGHT)) mx += 1;
    if (key_is_down(KEY_UP)) my -= 1;
    if (key_is_down(KEY_DOWN)) my += 1;

    // ── Engine speed with 2x cap logic ───────────────────────────────
    int eng_mult = get_engine_mult(); // 180..512
    int base_spd = TO_FIXED(1) + 50; // 306 base
    base_spd = (base_spd * eng_mult) >> 8;
    int spd = base_spd;

    if (mx != 0 && my != 0) {
        g_game.player.x += (mx * spd * 181) / 256;
        g_game.player.y += (my * spd * 181) / 256;
    } else {
        g_game.player.x += mx * spd;
        g_game.player.y += my * spd;
    }

    if (g_game.player.x < TO_FIXED(12)) g_game.player.x = TO_FIXED(12);
    if (g_game.player.x > TO_FIXED(SCREEN_WIDTH - 12)) g_game.player.x = TO_FIXED(SCREEN_WIDTH - 12);
    if (g_game.player.y < TO_FIXED(22)) g_game.player.y = TO_FIXED(22);
    if (g_game.player.y > TO_FIXED(SCREEN_HEIGHT - 12)) g_game.player.y = TO_FIXED(SCREEN_HEIGHT - 12);

    if (mx != 0 || my != 0) {
        if ((rand() & 1) == 0) emit_engine_particle();
    }

    // ── Big laser: hold B/R/L for 2s to charge, then a piercing beam
    //    fires for 3s (Undertale yellow-soul style). ──────────────────
    bool beam_held = key_is_down(KEY_B) || key_is_down(KEY_R) || key_is_down(KEY_L);
    if (!g_game.beam_active) {
        if (beam_held) {
            if (g_game.beam_charge < BEAM_CHARGE_TICKS) {
                g_game.beam_charge++;
                if (g_game.beam_charge >= BEAM_CHARGE_TICKS) {
                    platform_queue_haptic(HAPTIC_CHARGE);
                    g_game.beam_active = true;
                    g_game.beam_timer = BEAM_DURATION_TICKS;
                    platform_queue_haptic(HAPTIC_BEAM);
                    audio_play_sfx(SFX_LASER);
                }
            }
        } else if (g_game.beam_charge > 0) {
            g_game.beam_charge -= 2; // released early: charge drains fast
            if (g_game.beam_charge < 0) g_game.beam_charge = 0;
        }
    } else {
        g_game.beam_timer--;
        if (g_game.beam_timer <= 0) {
            g_game.beam_active = false;
            g_game.beam_charge = 0;
        }
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

    // Boss bullets
    for (int i = 0; i < MAX_BOSS_BULLETS; i++) {
        if (g_game.boss_bullets[i].active) {
            g_game.boss_bullets[i].x += g_game.boss_bullets[i].vx;
            g_game.boss_bullets[i].y += g_game.boss_bullets[i].vy;
            g_game.boss_bullets[i].life--;
            int bx = FROM_FIXED(g_game.boss_bullets[i].x);
            int by = FROM_FIXED(g_game.boss_bullets[i].y);
            if (g_game.boss_bullets[i].life == 0 || bx < -12 || bx > SCREEN_WIDTH + 12
                || by < -20 || by > SCREEN_HEIGHT + 20) {
                g_game.boss_bullets[i].active = false;
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
                if (g_game.asteroids[i].type == AST_SMALL || g_game.asteroids[i].type == AST_TINY) {
                    // Small rocks only come around once: they leave the field
                    // for good when they fall off-screen, so waves end cleanly.
                    g_game.asteroids[i].active = false;
                } else {
                    g_game.asteroids[i].y = -TO_FIXED(rad + 10);
                    g_game.asteroids[i].x = TO_FIXED((rand() % (SCREEN_WIDTH - 30)) + 15);
                }
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

#ifndef PLATFORM_HOST
    int scav_lv = g_settings.upgrade_levels[UPG_SCAVENGER];
    int mag_dist_sq = (scav_lv > 0) ? (25 + scav_lv * 28) * (25 + scav_lv * 28) : 0;
#endif

    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (g_game.powerups[i].active) {
            g_game.powerups[i].y += g_game.powerups[i].vy;
#ifndef PLATFORM_HOST
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
#endif
            if (FROM_FIXED(g_game.powerups[i].y) > SCREEN_HEIGHT + 10) {
                g_game.powerups[i].active = false;
            }
        }
    }

    int px = FROM_FIXED(g_game.player.x);
    int py = FROM_FIXED(g_game.player.y);

    /* ── Boss AI ─────────────────────────────────────────────────── */
    if (g_game.boss_active && g_game.boss.active) {
        Boss* b = &g_game.boss;
        if (b->flash_timer > 0) b->flash_timer--;

        // Entry animation: descend to y~34 then start fighting
        int target_y = TO_FIXED(34);
        int by = FROM_FIXED(b->y);

        if (by < 30) {
            b->y += b->vy;
            if (b->y > target_y) b->y = target_y;
        } else {
            b->phase_timer--;
            int boss_spd = TO_FIXED(2); // faster than drones, ~2 pixels/tick

            switch (b->phase) {
                case BOSS_IDLE: {
                    int left_lim = TO_FIXED(20), right_lim = TO_FIXED(SCREEN_WIDTH - 20);
                    b->x += b->vx;
                    if (b->x < left_lim) { b->x = left_lim; b->vx = -b->vx; }
                    if (b->x > right_lim) { b->x = right_lim; b->vx = -b->vx; }
                    b->cooldown--;
                    if (b->cooldown <= 0) {
                        boss_fire_spread_at_player(TO_FIXED(4));
                        b->cooldown = 28 - (g_game.wave / 5);
                        if (b->mini) b->cooldown += 14;
                        if (b->cooldown < 10) b->cooldown = 10;
                    }
                    if (b->phase_timer <= 0) {
                        /* Mini-boss skips the dive lunge — same kit, less mean. */
                        int roll = rand() % (b->mini ? 3 : 4);
                        if (roll == 0) { b->phase = BOSS_BURST; b->phase_timer = 90; b->fire_state = 0; }
                        else if (roll == 1) { b->phase = BOSS_BEAM_WIND; b->phase_timer = 60; b->beam_x = g_game.player.x; }
                        else if (roll == 2) { b->phase = BOSS_SWEEP; b->phase_timer = 120; b->sweep_dir = (g_game.player.x < b->x) ? -1 : 1; }
                        else { b->phase = BOSS_DIVE; b->phase_timer = 140; b->aim_x = g_game.player.x; }
                    }
                    break;
                }
                case BOSS_SWEEP: {
                    b->x += b->sweep_dir * boss_spd * 2; // fast strafe
                    if (b->x < TO_FIXED(20)) { b->x = TO_FIXED(20); b->sweep_dir = 1; }
                    if (b->x > TO_FIXED(SCREEN_WIDTH - 20)) { b->x = TO_FIXED(SCREEN_WIDTH - 20); b->sweep_dir = -1; }
                    if ((b->phase_timer & 3) == 0) {
                        int cx = FROM_FIXED(b->x);
                        int cy = FROM_FIXED(b->y) + 14;
                        add_boss_bullet(TO_FIXED(cx), TO_FIXED(cy), 0, TO_FIXED(4), false);
                    }
                    if (b->phase_timer <= 0) { b->phase = BOSS_IDLE; b->phase_timer = 80; b->cooldown = 30; }
                    break;
                }
                case BOSS_BURST: {
                    b->x += b->vx / 2;
                    int left_lim = TO_FIXED(20), right_lim = TO_FIXED(SCREEN_WIDTH - 20);
                    if (b->x < left_lim) { b->x = left_lim; b->vx = -b->vx; }
                    if (b->x > right_lim) { b->x = right_lim; b->vx = -b->vx; }
                    if ((b->phase_timer % 20) == 0) {
                        boss_fire_burst(8, TO_FIXED(3));
                        b->fire_state++;
                    }
                    if (b->phase_timer <= 0) { b->phase = BOSS_IDLE; b->phase_timer = 70; b->cooldown = 30; }
                    break;
                }
                case BOSS_BEAM_WIND: {
                    b->beam_x = g_game.player.x;
                    if ((b->phase_timer & 3) == 0) {
                        int wx = FROM_FIXED(b->beam_x);
                        spawn_particle(TO_FIXED(wx + (rand()&7)-4), TO_FIXED(40+rand()%90), 0, 80,
                                       PAL_TEXT_RED + (rand()&2), 8);
                    }
                    if (b->phase_timer <= 0) {
                        b->phase = BOSS_BEAM_FIRE;
                        b->phase_timer = 120;
                        b->beam_width = TO_FIXED(12);
                        b->beam_timer = 120;
                    }
                    break;
                }
                case BOSS_BEAM_FIRE: {
                    int pxi = g_game.player.x;
                    int diff = pxi - b->beam_x;
                    int step = TO_FIXED(1) / 8; // slow tracking, ~0.12 pix/tick
                    if (diff > step) b->beam_x += step;
                    else if (diff < -step) b->beam_x -= step;
                    else b->beam_x = pxi;
                    b->beam_timer--;
                    int beam_px = FROM_FIXED(b->beam_x);
                    int half_w = 7 + (120 - b->beam_timer) / 20;
                    if (half_w > 14) half_w = 14;
                    if (g_game.player.invulnerable_timer == 0 && py > 12) {
                        if (px >= beam_px - half_w && px <= beam_px + half_w) {
                            damage_player();
                        }
                    }
                    if ((b->phase_timer & 1) == 0) {
                        int sx = beam_px + (rand()&31)-15;
                        spawn_particle(TO_FIXED(sx), TO_FIXED(30+rand()%110),
                                       (rand()&63)-32, -((rand()&31)+40),
                                       PAL_TEXT_GOLD + (rand()&3), 10);
                    }
                    if (b->beam_timer <= 0) { b->phase = BOSS_IDLE; b->phase_timer = 100; b->cooldown = 40; }
                    break;
                }
                case BOSS_DIVE: {
                    if (b->phase_timer > 80) {
                        int diff = b->aim_x - b->x;
                        if (diff > boss_spd) b->x += boss_spd;
                        else if (diff < -boss_spd) b->x -= boss_spd;
                        else b->x = b->aim_x;
                    } else if (b->phase_timer > 20) {
                        b->y += TO_FIXED(4);
                        if ((b->phase_timer & 2) == 0) {
                            boss_fire_burst(6, TO_FIXED(3));
                        }
                    } else {
                        b->y -= TO_FIXED(2);
                        if (b->y <= target_y) { b->y = target_y; b->phase = BOSS_IDLE; b->phase_timer = 80; b->cooldown = 30; }
                    }
                    break;
                }
                default:
                    b->phase = BOSS_IDLE;
                    b->phase_timer = 60;
                    break;
            }
        }

        // Collide player with boss body
        int boss_cx = FROM_FIXED(b->x);
        int boss_cy = FROM_FIXED(b->y);
        int boss_r = boss_hit_radius(b);
        if (g_game.player.invulnerable_timer == 0) {
            int dist_sq = (px - boss_cx)*(px - boss_cx) + (py - boss_cy)*(py - boss_cy);
            if (dist_sq <= (6 + boss_r)*(6 + boss_r)) {
                damage_player();
            }
        }

        // Player bullets vs boss
        for (int bu = 0; bu < MAX_BULLETS; bu++) {
            if (g_game.bullets[bu].active && !g_game.bullets[bu].enemy) {
                int bxp = FROM_FIXED(g_game.bullets[bu].x);
                int byp = FROM_FIXED(g_game.bullets[bu].y);
                int br = g_game.bullets[bu].radius;
                int dist_sq = (bxp - boss_cx)*(bxp - boss_cx) + (byp - boss_cy)*(byp - boss_cy);
                if (dist_sq <= (br + boss_r)*(br + boss_r)) {
                    int dmg = g_game.bullets[bu].damage;
                    b->hp -= dmg;
                    b->flash_timer = 6;
                    if (g_settings.screen_shake) {
                        if (g_game.shake_timer < 4) g_game.shake_timer = 4;
                    }
                    g_game.bullets[bu].active = false;
                    for (int p = 0; p < 3; p++) {
                        int pvx = (rand() & 127) - 64;
                        int pvy = -((rand() & 80) + 40);
                        spawn_particle(TO_FIXED(bxp), TO_FIXED(byp), pvx, pvy, PAL_TEXT_GOLD, 8);
                    }
                    if (b->hp <= 0) {
                        defeat_boss(b, boss_cx, boss_cy);
                    }
                }
            }
        }

        // Big laser vs boss: a full 3s dump is ~20% of the bar (5 beams to
        // solo). Never oneshots, even with NOVA+OMEGA + max Beam upgrade.
        if (g_game.beam_active) {
            int pbx = FROM_FIXED(g_game.player.x);
            if (boss_cx + boss_r >= pbx - 6 && boss_cx - boss_r <= pbx + 6) {
                int denom = BEAM_DURATION_TICKS * 5;
                if (denom < 1) denom = 1;
                int tick = (b->hp_max << 8) / denom;
                if (tick < 1) tick = 1;
                b->hp_frac += tick;
                while (b->hp_frac >= 256) {
                    b->hp_frac -= 256;
                    b->hp--;
                }
                b->flash_timer = 4;
                if (b->hp <= 0) {
                    defeat_boss(b, boss_cx, boss_cy);
                }
            }
        }
    }

    // Boss bullets vs player
    for (int bb = 0; bb < MAX_BOSS_BULLETS; bb++) {
        if (!g_game.boss_bullets[bb].active) continue;
        int bbx = FROM_FIXED(g_game.boss_bullets[bb].x);
        int bby = FROM_FIXED(g_game.boss_bullets[bb].y);
        int bbr = g_game.boss_bullets[bb].radius;
        if (g_game.player.invulnerable_timer == 0) {
            int dist_sq = (bbx - px)*(bbx - px) + (bby - py)*(bby - py);
            if (dist_sq <= (bbr + 6)*(bbr + 6)) {
                g_game.boss_bullets[bb].active = false;
                damage_player();
            }
        }
    }

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
#ifdef PLATFORM_HOST
                /* Android pierce rules:
                 *   - NOVA+OMEGA bullets: pierce EVERYTHING (they oneshot).
                 *   - QUANTUM/PLASMA heavy bullets: pierce small/medium, stop on large.
                 *   - Everything else: stops on first rock hit. */
                bool pierce = false;
                if (is_oneshot_build()) {
                    pierce = true;
                } else if (g_game.bullets[b].heavy && is_top_tier_build()) {
                    pierce = (g_game.asteroids[a].type != AST_LARGE);
                }
                if (!pierce) {
                    g_game.bullets[b].active = false;
                    consumed = true;
                }
#else
                // Heavy / Nova / Omega pierce logic
                bool pierce = g_game.bullets[b].heavy;
                // Single weak should NOT pierce - only 1 rock at a time
                if (g_settings.weapon_rig == WEAPON_SINGLE) pierce = false;
                // Omega / Nova pierces even large? keep but still consume on large for balance
                if (!pierce || (g_game.asteroids[a].type == AST_LARGE && g_settings.weapon_rig != WEAPON_NOVA)) {
                    g_game.bullets[b].active = false;
                    consumed = true;
                }
#endif
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

    // ── Big laser beam: pierces everything in its column, dealing
    //    laser_damage/10 per tick (fractional HP accumulates). ─────────
    if (g_game.beam_active) {
        int bx = FROM_FIXED(g_game.player.x);
        int beam_dmg = BEAM_TICK_DAMAGE(); // 8.8 fixed
        for (int a = 0; a < MAX_ASTEROIDS; a++) {
            Asteroid* ast = &g_game.asteroids[a];
            if (!ast->active) continue;
            int ax = FROM_FIXED(ast->x);
            int ar = ast->radius;
            if (ax + ar < bx - 6 || ax - ar > bx + 6) continue; // beam column
            ast->hp_frac += beam_dmg;
            while (ast->hp_frac >= 256) {
                ast->hp_frac -= 256;
                ast->hp--;
            }
            if (ast->hp <= 0) destroy_asteroid(a, true);
        }
        for (int d = 0; d < MAX_DRONES; d++) {
            Drone* dr = &g_game.drones[d];
            if (!dr->active) continue;
            int dx = FROM_FIXED(dr->x);
            if (dx + 8 < bx - 6 || dx - 8 > bx + 6) continue;
            dr->hp_frac += beam_dmg;
            while (dr->hp_frac >= 256) {
                dr->hp_frac -= 256;
                dr->hp--;
            }
            if (dr->hp <= 0) destroy_drone(d, true);
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
        if (g_game.boss_active && g_game.boss.active) active_enemies++;

        // Later waves keep dumping extra rocks so the field stays packed.
        // Don't spawn extra asteroids during a boss fight (handled in boss AI)
        bool boss_fight = g_game.boss_active && g_game.boss.active;
        if (s_wave_reinforcements > 0 && active_enemies > 0 && !boss_fight) {
            g_game.spawn_timer--;
            if (g_game.spawn_timer <= 0) {
                int ast_n = count_active_asteroids();
                int cap = MAX_ASTEROIDS - 12;
                int extra = 1 + g_game.wave / 5;
                if (extra > s_wave_reinforcements) extra = s_wave_reinforcements;
                for (int n = 0; n < extra && ast_n < cap; n++) {
                    spawn_random_asteroid();
                    ast_n++;
                    s_wave_reinforcements--;
                }
                int cd = 52 - g_game.wave * 3;
                if (cd < 16) cd = 16;
                g_game.spawn_timer = cd;
            }
        }

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
    gfx_draw_text(SCREEN_WIDTH - 82, SCREEN_HEIGHT - 10, "BEAM", PAL_TEXT_WHITE);
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

    // Big laser: faint aim line while charging, then a full-height beam
    // that reaches the top of the screen (and the bottom).
    int beam_bx = FROM_FIXED(g_game.player.x) + ox;
    if (g_game.beam_active) {
        u8 beam_col = gfx_get_laser_color(g_settings.laser_index);
        int bw = ((s_game_frame & 3) == 0) ? 10 : 8; // subtle flicker
        gfx_fill_rect(beam_bx - bw / 2, 0, bw, SCREEN_HEIGHT, beam_col);
        gfx_fill_rect(beam_bx - 1, 0, 2, SCREEN_HEIGHT, PAL_TEXT_WHITE);
    } else if (g_game.beam_charge > 0) {
        gfx_fill_rect(beam_bx - 1, 0, 2, SCREEN_HEIGHT, 15);
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
    // Right-aligned inside the card so long balances (e.g. cheat money) fit.
    save_format_coins(buf, sizeof(buf));
    int coin_px = (int)strlen(buf) * 6 + 6; // digits + "$"
    int coin_x = right_card_x + 72 - 3 - coin_px;
    gfx_draw_char(coin_x, 4, '$', PAL_TEXT_GOLD);
    gfx_draw_text(coin_x + 6, 4, buf, PAL_TEXT_GOLD);

    for (int i = 0; i < g_game.player.lives && i < 7; i++) {
        gfx_draw_char(right_card_x + 3 + i * 6, 11, '^', PAL_TEXT_GREEN);
    }
    for (int i = 0; i < g_game.player.shield_charges && i < 6; i++) {
        gfx_draw_char(right_card_x + 39 + i * 6, 11, '*', PAL_TEXT_CYAN);
    }

    if (g_game.combo > 1) {
        siprintf(buf, "x%d", g_game.combo);
        u8 acc = (g_game.combo >= 15) ? PAL_TEXT_GOLD : gfx_get_accent_color(g_settings.accent_index);
        gfx_draw_text(6, 20, buf, acc);
        int max_comb_t = get_max_combo_timer();
        gfx_draw_progress_bar(20, 22, 42, 4, g_game.combo_timer, max_comb_t, acc, 18);
    }

    if (g_game.player.rapid_fire_timer > 0) {
        siprintf(buf, "RAPID %d", (g_game.player.rapid_fire_timer + 59) / 60);
        gfx_draw_text_centered((SCREEN_WIDTH - 80) / 2, 20, 80, buf, PAL_TEXT_GOLD);
    }

    // Big laser status: charge-up fills while holding, then drains as it fires
    if (g_game.beam_active) {
        gfx_draw_text(SCREEN_WIDTH - 82, SCREEN_HEIGHT - 10, "FIRING", PAL_TEXT_GOLD);
        gfx_draw_progress_bar(SCREEN_WIDTH - 54, SCREEN_HEIGHT - 9, 50, 5,
                              g_game.beam_timer, BEAM_DURATION_TICKS, PAL_TEXT_GOLD, 18);
    } else {
        u8 beam_col = gfx_get_accent_color(g_settings.accent_index);
        if (g_game.beam_charge >= BEAM_CHARGE_TICKS) beam_col = PAL_TEXT_GREEN;
        gfx_draw_progress_bar(SCREEN_WIDTH - 54, SCREEN_HEIGHT - 9, 50, 5,
                              g_game.beam_charge, BEAM_CHARGE_TICKS, beam_col, 18);
    }

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
        } else if (is_mini_boss_wave(g_game.wave)) {
            gfx_draw_text_centered(bx, by + 4, banner_w, "MINI BOSS", PAL_TEXT_GOLD);
            gfx_draw_text_centered(bx, by + 13, banner_w, "INCOMING!", PAL_TEXT_CYAN);
        } else if (is_full_boss_wave(g_game.wave)) {
            gfx_draw_text_centered(bx, by + 4, banner_w, "!! BOSS !!", PAL_TEXT_RED);
            gfx_draw_text_centered(bx, by + 13, banner_w, "INCOMING!", PAL_TEXT_GOLD);
        } else {
            siprintf(buf, "WAVE %02d", g_game.wave);
            gfx_draw_text_centered(bx, by + 4, banner_w, buf, PAL_TEXT_WHITE);
            gfx_draw_text_centered(bx, by + 13, banner_w, "GET READY!", PAL_TEXT_CYAN);
        }
    }

    // ── Boss rendering ──────────────────────────────────────────────
    if (g_game.boss_active && g_game.boss.active) {
        int bxi = FROM_FIXED(g_game.boss.x) + ox;
        int byi = FROM_FIXED(g_game.boss.y) + oy;

        // Boss beam warning / firing
        if (g_game.boss.phase == BOSS_BEAM_WIND) {
            // Warning dashed line
            int wpx = FROM_FIXED(g_game.boss.beam_x) + ox;
            u8 col = ((s_game_frame >> 1) & 2) ? PAL_TEXT_RED : PAL_TEXT_GOLD;
            for (int yy = byi + 12; yy < SCREEN_HEIGHT; yy += 6) {
                gfx_fill_rect(wpx - 1, yy, 2, 3, col);
            }
        } else if (g_game.boss.phase == BOSS_BEAM_FIRE) {
            int wpx = FROM_FIXED(g_game.boss.beam_x) + ox;
            int half_w = 8;
            gfx_fill_rect(wpx - half_w, 0, half_w * 2, SCREEN_HEIGHT, PAL_TEXT_RED);
            gfx_fill_rect(wpx - 2, 0, 4, SCREEN_HEIGHT, PAL_TEXT_GOLD);
            gfx_fill_rect(wpx - 1, 0, 2, SCREEN_HEIGHT, PAL_TEXT_WHITE);
        }

        // Boss hull: the mini-drone sprite, recolored + scaled.
        int flash = (g_game.boss.flash_timer > 0) ? 1 : 0;
#ifdef PLATFORM_HOST
        gfx_draw_boss_drone(bxi, byi, g_game.boss.mini, flash != 0, s_game_frame);
#else
        u8 hull = flash ? PAL_TEXT_WHITE : PAL_TEXT_RED;
        u8 trim = flash ? PAL_TEXT_WHITE : PAL_TEXT_GOLD;
        u8 glow = PAL_TEXT_VIOLET;
        if (g_game.boss.mini) {
            gfx_fill_rect(bxi - 13, byi - 8, 26, 14, hull);
            gfx_fill_rect(bxi - 10, byi - 11, 20, 5, hull);
            gfx_fill_rect(bxi - 16, byi - 3, 6, 8, hull);
            gfx_fill_rect(bxi + 10, byi - 3, 6, 8, hull);
            gfx_fill_rect(bxi - 3, byi + 6, 6, 6, hull);
            gfx_fill_rect(bxi - 11, byi - 7, 22, 1, trim);
            gfx_fill_rect(bxi - 13, byi + 4, 26, 2, trim);
            gfx_fill_rect(bxi - 2, byi - 10, 4, 2, trim);
            gfx_fill_rect(bxi - 3, byi - 4, 6, 4, glow);
            gfx_fill_rect(bxi - 1, byi - 3, 2, 2, PAL_TEXT_WHITE);
        } else {
            gfx_fill_rect(bxi - 18, byi - 10, 36, 18, hull);
            gfx_fill_rect(bxi - 14, byi - 14, 28, 6, hull);
            gfx_fill_rect(bxi - 22, byi - 4, 8, 10, hull);
            gfx_fill_rect(bxi + 14, byi - 4, 8, 10, hull);
            gfx_fill_rect(bxi - 4, byi + 8, 8, 8, hull);
            gfx_fill_rect(bxi - 16, byi - 9, 32, 1, trim);
            gfx_fill_rect(bxi - 18, byi + 6, 36, 2, trim);
            gfx_fill_rect(bxi - 3, byi - 13, 6, 3, trim);
            gfx_fill_rect(bxi - 4, byi - 5, 8, 5, glow);
            gfx_fill_rect(bxi - 2, byi - 4, 4, 3, PAL_TEXT_WHITE);
        }
#endif

        // HP bar across top
        int bar_w = SCREEN_WIDTH - 40;
        int bar_x = 20;
        int bar_y = 22;
        gfx_fill_rect(bar_x - 1, bar_y - 1, bar_w + 2, 7, PAL_TEXT_WHITE);
        gfx_fill_rect(bar_x, bar_y, bar_w, 5, PAL_BTN_BG);
#ifdef PLATFORM_HOST
        int hp_w = (int)(bar_w * (float)g_game.boss.hp / (float)g_game.boss.hp_max);
#else
        /* Integer math: the GBA has no FPU, so software floats in the HUD
         * path would cost real frame time. */
        int hp_max = g_game.boss.hp_max > 0 ? g_game.boss.hp_max : 1;
        int hp_now = g_game.boss.hp > 0 ? g_game.boss.hp : 0;
        int hp_w = (int)(((s32)bar_w * hp_now) / hp_max);
#endif
        if (hp_w < 0) hp_w = 0;
        if (hp_w > bar_w) hp_w = bar_w;
        gfx_fill_rect(bar_x, bar_y, hp_w, 5, PAL_TEXT_RED);
        gfx_draw_text_centered(0, bar_y - 8, SCREEN_WIDTH,
                              g_game.boss.mini ? "MINI BOSS" : "BOSS",
                              g_game.boss.mini ? PAL_TEXT_GOLD : PAL_TEXT_RED);
    }

    // Boss bullets
    for (int i = 0; i < MAX_BOSS_BULLETS; i++) {
        if (g_game.boss_bullets[i].active) {
            int bx = FROM_FIXED(g_game.boss_bullets[i].x) + ox;
            int by = FROM_FIXED(g_game.boss_bullets[i].y) + oy;
            bool heavy = g_game.boss_bullets[i].heavy;
            int c = (s_game_frame + i) % NUM_LASERS;
            gfx_draw_laser(bx, by, heavy, c, s_game_frame, true);
        }
    }
}

