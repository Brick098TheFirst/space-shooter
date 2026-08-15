#include "game.h"
#include "renderer.h"
#include "audio.h"
#include "save.h"
#include "starfield.h"
#include "story.h"
#include <stdlib.h>
#include <string.h>
#ifdef PLATFORM_HOST
#include <math.h>
#endif

EWRAM_BSS GameState g_game;

static bool s_game_static_valid = false;
static int s_game_frame = 0;
static GameMode s_game_mode = GAME_MODE_WAVES;

/* ── Co-op (host-authoritative) state ────────────────────────────────────
 * The HOST simulates a second ship from the guest's streamed input; the
 * GUEST stops simulating and only renders the host's snapshots.  Everything
 * below is inert when co-op is not active, so offline single-player is
 * completely unchanged. */
static int s_coop_guest_active = 0;   // host: simulate the guest ship
static int s_coop_render_only = 0;    // guest: render host world only
static u16 s_coop_guest_keys = 0;     // guest input applied to player 2 on host
static CoopLoadout s_coop_p2_lo;      // guest loadout (drives player 2 on host)
static u8 s_coop_p2_lo_valid = 0;
static int s_coop_frame = 0;          // host: ticks since session start
static u16 s_coop_snapshot_seq = 0;
/* Guest render-advance bookkeeping: how many sim ticks have elapsed since the
 * last snapshot was applied, so we can extrapolate smoothly between them. */
static int s_render_ticks = 0;
static u16 s_coop_local_keys = 0;     // guest: own touch input (frame prediction)

/* ── Co-op FX event sync (protocol v3) ─────────────────────────────────
 * Snapshots carry entity positions but not one-shot effects (explosions,
 * weapon fire, pickups).  Without them the guest sees rocks simply vanish
 * with no explosion and hears none of the host's lasers.  Every particle-
 * heavy one-shot event the host sim produces also pushes a tiny tagged
 * event into this ring; the next snapshot drains it, and the guest replays
 * the effect locally (visual + SFX) at the synced position.  Syncs BOTH
 * directions because the guest's ship is simulated on the host. */
#define COOP_FX_MAX 24
#define COOP_FX_EXPLOSION 1   // big boom + particles         (sfx: explosion)
#define COOP_FX_FIRE_P1   2   // host ship laser volley       (sfx: laser)
#define COOP_FX_FIRE_P2   3   // guest ship laser volley      (sfx: laser)
#define COOP_FX_PICKUP    4   // powerup collected            (sfx: pickup)
#define COOP_FX_BEAM_P1   5   // host big beam engaged        (sfx: laser)
#define COOP_FX_BEAM_P2   6   // guest big beam engaged       (sfx: laser)
#define COOP_FX_PLAYER_DIE 7  // a ship went down             (sfx: explosion)
typedef struct { u8 type; s16 x; s16 y; } CoopFxEvent;
static CoopFxEvent s_fx_ring[COOP_FX_MAX];
static int s_fx_head = 0;   // next slot to write
static int s_fx_count = 0;  // events not yet serialized

static void coop_fx_push(u8 type, int x, int y) {
    if (!s_coop_guest_active) return;   // solo / guest: nothing to sync out
    if (s_fx_count < COOP_FX_MAX) s_fx_count++;
    s_fx_ring[s_fx_head].type = type;
    s_fx_ring[s_fx_head].x = (s16)x;
    s_fx_ring[s_fx_head].y = (s16)y;
    s_fx_head = (s_fx_head + 1) % COOP_FX_MAX;
}

#define COOP_SNAPSHOT_EVERY 3  // host sends one full snapshot every N ticks
#define COOP_CHUNK_MAX 1100    // < EOS P2P 1170-byte packet limit
#define COOP_RELIABLE_CH 0
#define COOP_INPUT_CH 1

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
    if (m < (int)GAME_MODE_WAVES || m > (int)GAME_MODE_STORY) m = (int)GAME_MODE_WAVES;
    s_game_mode = (GameMode)m;
}

/* ── Story Mode runtime ───────────────────────────────────────────────────
 * The level being flown, its objective progress, and the outcome the menu
 * layer reads once the level ends. */
static int  s_story_level = 1;          /* 1..70 */
static int  s_story_kills = 0;          /* hunters downed (OBJ_HUNT) */
static int  s_story_bigs  = 0;          /* large rocks cracked (OBJ_BIGGAME) */
static int  s_story_timer = 0;          /* ticks left  (OBJ_SURVIVE) */
static int  s_story_spawned = 0;        /* rocks released so far */
static int  s_story_to_spawn = 0;       /* rocks still owed to the field */
static int  s_story_outcome = 0;        /* 0 running, 1 cleared, 2 failed */
static int  s_story_earned = 0;         /* chubbcoin banked this level */
static int  s_story_end_delay = 0;      /* victory pause before the result */
/* Story cards no longer disappear on a timer. The opening field is prepared
 * but the entire simulation stays frozen until the player taps to continue. */
static bool s_story_waiting_for_start = false;

static void story_finish(int outcome);
static void story_begin_level(void);
static void story_update_objective(void);
static void story_on_hunter_killed(void);
static void story_on_large_killed(void);

int  game_story_level(void)   { return s_story_level; }
int  game_story_outcome(void) { return s_story_outcome; }
int  game_story_earned(void)  { return s_story_earned; }
int  game_story_waiting_for_start(void) {
    return g_game.mode == GAME_MODE_STORY && s_story_waiting_for_start;
}
void game_story_continue(void) {
    if (!game_story_waiting_for_start()) return;
    s_story_waiting_for_start = false;
    /* The story card is the whole countdown now. Remove it immediately so
     * the very next frame is gameplay rather than another timed banner. */
    g_game.wave_banner_timer = 0;
}
#ifdef STORY_DEBUG
int  game_story_kills(void)   { return s_story_kills; }
int  game_story_secs_left(void){ return s_story_timer; }
#endif

void game_story_set_level(int level) {
    if (level < 1) level = 1;
    if (level > STORY_LEVEL_COUNT) level = STORY_LEVEL_COUNT;
    s_story_level = level;
}

static const StoryLevel* story_cur(void) {
    int i = s_story_level - 1;
    if (i < 0) i = 0;
    if (i >= STORY_LEVEL_COUNT) i = STORY_LEVEL_COUNT - 1;
    return &g_story_levels[i];
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

/* Monotonic five-crystal ladder. */
static int laser_bonus_for_idx(int idx) {
    static const int bonus[NUM_LASERS] = { 0, 1, 4, 10, 14 };
    if (idx < 0 || idx >= NUM_LASERS) return 0;
    return bonus[idx];
}
static int get_laser_bonus(void) {
    return laser_bonus_for_idx(g_settings.laser_index);
}

/* Per-projectile damage; projectile counts below never decrease. */
static int get_weapon_base_damage(WeaponRig rig) {
    switch (rig) {
        case WEAPON_SINGLE:
        case WEAPON_TWIN:
        case WEAPON_SPREAD:    return 1;
        case WEAPON_FOCUSED:   return 2;
        case WEAPON_TRIPLE:    return 3;
        case WEAPON_PLASMA:    return 4;
        case WEAPON_QUANTUM:   return 5;
        case WEAPON_NOVA:      return 6;
        case WEAPON_ARC_HEX:   return 7;
        case WEAPON_RIFT:      return 8;
        case WEAPON_COMET:     return 9;
        case WEAPON_SOLAR:     return 10;
        case WEAPON_STARQUAKE: return 11;
        case WEAPON_VOID:      return 12;
        case WEAPON_PRISM:     return 14;
        case WEAPON_INFINITY:  return 20;
        default:               return 1;
    }
}

static bool is_top_tier_build(void) {
    return g_settings.weapon_rig >= WEAPON_PLASMA;
}
static bool lo_is_top_tier(WeaponRig rig) {
    return rig >= WEAPON_PLASMA;
}

/* ── Loadout-aware helpers (used for the co-op guest ship) ───────────────
 * The host simulates the guest ship with the guest's own upgrades. */
static int lo_upg(const CoopLoadout* lo, UpgradeType t) {
    int lv = (t >= 0 && t < NUM_UPGRADES) ? lo->upgrade_levels[t] : 0;
    if (lv < 0) lv = 0;
    if (lv > UPG_MAX_LEVEL) lv = UPG_MAX_LEVEL;
    return lv;
}
static int lo_engine_mult(const CoopLoadout* lo) {
    return s_engine_mult[lo_upg(lo, UPG_ENGINE)];
}
static int lo_fire_rate_cooldown_mult(const CoopLoadout* lo) {
    static const int tbl[6] = { 256, 215, 178, 148, 122, 102 };
    return tbl[lo_upg(lo, UPG_FIRE_RATE)];
}
static int lo_damage_bonus(const CoopLoadout* lo) {
    return lo_upg(lo, UPG_DAMAGE);
}
static int lo_rapid_duration(const CoopLoadout* lo) {
    static const int dur[6] = { 480, 660, 840, 1080, 1320, 1560 };
    return dur[lo_upg(lo, UPG_OVERDRIVE)];
}
static int lo_beam_damage(const CoopLoadout* lo) {
    int dmg = get_weapon_base_damage(lo->weapon_rig) + lo_damage_bonus(lo)
            + laser_bonus_for_idx(lo->laser_index);
    return (dmg * (100 + lo_upg(lo, UPG_DASH) * 25)) / 100;
}

/* Big charged laser remains tied to the local loadout and Beam upgrade. */
static int get_beam_damage(void) {
    int dmg = get_weapon_base_damage(g_settings.weapon_rig)
            + get_damage_bonus() + get_laser_bonus();
    int lv = g_settings.upgrade_levels[UPG_DASH];
    if (lv < 0) lv = 0;
    if (lv > UPG_MAX_LEVEL) lv = UPG_MAX_LEVEL;
    return (dmg * (100 + lv * 25)) / 100;
}

/* Big laser timing: 2s charge, 3s beam. Ticks run at 90/s on the Android
 * host and 120/s on GBA (2 sim ticks per 60fps frame). */
#ifdef PLATFORM_HOST
#define BEAM_CHARGE_TICKS (2 * 90)
#define BEAM_DURATION_TICKS (3 * 90)
/* Beam ticks 90/s; divide dmg so a 5HP big rock at base gear takes ~1s of
 * sustained beam, while later rigs improve it steadily. */
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

#define PRIMARY_BEAM_TICK_RATE 90
#define PRIMARY_BEAM_RAMP_TICKS 54 /* 0.6 seconds */

static int primary_beam_tick_damage_for(const CoopLoadout* lo, int ramp) {
    if (ramp < 0) ramp = 0;
    if (ramp > PRIMARY_BEAM_RAMP_TICKS) ramp = PRIMARY_BEAM_RAMP_TICKS;
    int full_dps = 6000 + laser_bonus_for_idx(lo->laser_index) * 20
                         + lo_damage_bonus(lo) * 40;
    int pct = 20 + (80 * ramp) / PRIMARY_BEAM_RAMP_TICKS;
    return (full_dps * pct * 256) / (100 * PRIMARY_BEAM_TICK_RATE);
}

static int primary_beam_tick_damage(int ramp) {
    CoopLoadout lo;
    lo.laser_index = g_settings.laser_index;
    for (int i = 0; i < NUM_UPGRADES; i++) lo.upgrade_levels[i] = g_settings.upgrade_levels[i];
    return primary_beam_tick_damage_for(&lo, ramp);
}

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
    coop_fx_push(COOP_FX_EXPLOSION, x, y);
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
    /* Story Mode has its own purse (chubbcoin, paid out per level clear).
     * Arcade coins are never earned while flying the campaign, which keeps
     * the two economies — and the locked arcade shop — honest. */
    if (g_game.mode == GAME_MODE_STORY) return;
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
    if (g_game.player.dead) return; // spectating co-op pilot: untouchable
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
            if (s_coop_guest_active) {
                /* Co-op: losing your last life doesn't end the run. Your ship
                 * goes down, you SPECTATE your partner, and the run only ends
                 * when both ships are down. */
                g_game.player.dead = true;
                g_game.player.x = TO_FIXED(SCREEN_WIDTH / 2);
                g_game.player.y = TO_FIXED(SCREEN_HEIGHT + 60); // parked off-screen
                g_game.beam_active = false;
                g_game.beam_charge = 0;
                g_game.primary_beam_active = false;
                g_game.primary_beam_ramp = 0;
                coop_fx_push(COOP_FX_PLAYER_DIE, px, py);
                if (g_game.player2.dead) finish_run(false); // both down: run over
            } else {
                finish_run(false);
            }
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
                /* Starter needs several hits; HP rises gently with wave while
                 * the strictly ordered weapon ladder cuts the time-to-kill. */
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
    /* OBJ_BIGGAME levels only care about the big ones. */
    if (award && t == AST_LARGE) story_on_large_killed();
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
        story_on_hunter_killed();
    }
}

static int count_active_asteroids(void) {
    int n = 0;
    for (int i = 0; i < MAX_ASTEROIDS; i++) if (g_game.asteroids[i].active) n++;
    return n;
}

/* ── The CLEAR counter, in medium-rock units ──────────────────────────────
 * The HUD counts MEDIUM rocks, because that is the unit the field is really
 * made of: a big rock is worth exactly the two mediums it breaks into, and
 * the small/tiny debris that mediums shed is not counted at all (it still
 * exists and still has to be dodged - it just is not the objective).
 *
 * So five big rocks read as 10, two big rocks read as 4, and popping a big
 * rock leaves the number where it was - it becomes the two mediums it just
 * turned into.  The number only drops when a MEDIUM dies. */
#define MED_PER_LARGE 2

static int asteroid_med_value(AsteroidType t) {
    switch (t) {
        case AST_LARGE:  return MED_PER_LARGE;   /* becomes 2 mediums */
        case AST_MED_A:
        case AST_MED_B:  return 1;
        default:         return 0;               /* small / tiny: not counted */
    }
}

/* Medium-equivalents currently on screen. */
static int count_medium_equivalents(void) {
    int n = 0;
    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        if (g_game.asteroids[i].active)
            n += asteroid_med_value(g_game.asteroids[i].type);
    }
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

/* Co-op targeting: enemies aim at a LIVING ship. With both pilots up they
 * track player 1 (classic behaviour); when one ship goes down all fire
 * redirects at the survivor instead of chewing on a corpse. */
static Player* ai_target_ship(void) {
    if (s_coop_guest_active) {
        if (g_game.player.dead && !g_game.player2.dead) return &g_game.player2;
    }
    return &g_game.player;
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
        g_game.drones[i].accent = (u8)(rand() % NUM_ACCENTS);
        g_game.drones[i].style = (u8)(rand() % NUM_SHIP_STYLES);
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
    if (g_game.mode == GAME_MODE_STORY) {
        /* Losing a story level costs one from the story life pool; the menu
         * layer reads the outcome and shows the failure card. */
        story_finish(2);
        return;
    }
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
    /* Give the first boss taunt a little longer than a normal wave banner so
     * the player has time to read it. */
    g_game.wave_banner_timer = (g_game.wave == 5) ? 180 : 110;

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
            g_game.drones[i].accent = (u8)(rand() % NUM_ACCENTS);
            g_game.drones[i].style = (u8)(rand() % NUM_SHIP_STYLES);
            g_game.drones[i].active = true;
        }
    }
}

/* Forward declarations for helpers the story code calls before they are
 * defined further down this file. */
static bool add_boss_bullet(int x, int y, int vx, int vy, int heavy);
static void spawn_asteroid(AsteroidType type, int x, int y, int vx, int vy);
static void spawn_particle(int x, int y, int vx, int vy, u8 color, u8 life);
static void trigger_explosion(int x, int y);
static void damage_player(void);
static void award_score(int base_pts);
static int  asteroid_speed_mult(AsteroidType type);
static int  get_diff_speed_mult(void);

/* ── Story Mode: level setup ──────────────────────────────────────────────
 * A story level is a fixed, hand-tuned field rather than the endless wave
 * escalator, so every level is finite and beatable. Enemy speed and HP are
 * scaled by the level's own curve. */

static int story_mod(void) { return story_cur()->modifier; }

/* ── Modifier hooks ───────────────────────────────────────────────────────
 * Each modifier leans on exactly one or two of these, so a twist changes
 * what you are flying through instead of only how much of it there is. */

static int story_speed_scale(void) {
    int s = story_cur()->speed_pct;
    if (story_mod() == MOD_SWIFT) s = (s * 145) / 100;   /* everything hurries */
    return s;
}

static int story_hp_scale(void) {
    const StoryLevel* L = story_cur();
    int h = L->hp_pct;
    if (story_mod() == MOD_TOUGH) {
        /* Armoured rock. Big-game levels are already gated on cracking the
         * big ones, so they get a gentler plating than the rest. */
        h = (h * (L->objective == OBJ_BIGGAME ? 130 : 165)) / 100;
    }
    /* Big-game asks you to crack a fixed number of the toughest rocks in the
     * game, so the late-campaign HP curve is capped here. Without this the
     * objective turns into a chore for anyone flying a mid-tier gun, which
     * tools/story_sim catches as a stall at tier 0. */
    if (L->objective == OBJ_BIGGAME && h > 200) h = 200;
    return h;
}

/* Chance (percent) that the next rock is a big one. */
static int story_large_chance(void) {
    switch (story_mod()) {
        case MOD_BOULDERS: return 88;   /* almost nothing but big rocks */
        case MOD_SHARDS:   return 0;    /* no big rocks at all */
        default:           return 12 + s_story_level;
    }
}

/* How many hunters the level wants on screen at once. */
static int story_drone_target(void) {
    const StoryLevel* L = story_cur();
    int d = L->drones;
    if (story_mod() == MOD_SWARM)   d += 3;
    if (story_mod() == MOD_SNIPERS && d > 2) d -= 1;
    if (d > MAX_DRONES) d = MAX_DRONES;
    if (d < 0) d = 0;
    return d;
}

/* Reinforcement cadence scalar, in percent of the base cooldown. */
static int story_spawn_cd_pct(void) {
    switch (story_mod()) {
        case MOD_TRICKLE: return 220;   /* a few at a time, slowly */
        case MOD_STORM:   return 45;    /* relentless */
        default:          return 100;
    }
}

/* The rocks a CLEAR level still owes the field, pre-rolled at level start so
 * the HUD's medium-equivalent counter is exact from the very first frame
 * (see s_story_pending_med). Each entry is simply "is this one large?". */
static u8  s_story_queue[MAX_ASTEROIDS];
static int s_story_queue_len = 0;
static int s_story_queue_pos = 0;
static int s_story_pending_med = 0;    /* medium-equivalents still to spawn */

/* Pop the next pre-rolled rock size; falls back to a fresh roll if the queue
 * ran dry (HUNT/SURVIVE levels top the field up indefinitely). */
static bool story_next_rock_is_large(void) {
    if (s_story_queue_pos < s_story_queue_len)
        return s_story_queue[s_story_queue_pos++] != 0;
    return (rand() % 100) < story_large_chance();
}

static void story_spawn_rock(void) {
    int mult = (get_diff_speed_mult() * story_speed_scale()) / 100;
    int x = TO_FIXED((rand() % (SCREEN_WIDTH - 40)) + 20);
    int y = -TO_FIXED((rand() % 60) + 8);
    int vx = ((rand() % 150) - 75) * mult >> 8;
    int vy = ((rand() % 70) + 70) * mult >> 8;
    bool is_large = story_next_rock_is_large();
    AsteroidType type = is_large ? AST_LARGE : ((rand() & 1) ? AST_MED_A : AST_MED_B);
    /* This rock has left the "still owed" pile and joined the field, so move
     * its medium-equivalents from pending to on-screen. */
    s_story_pending_med -= asteroid_med_value(type);
    if (s_story_pending_med < 0) s_story_pending_med = 0;
    int tmult = asteroid_speed_mult(type);
    spawn_asteroid(type, x, y, (vx * tmult) >> 8, (vy * tmult) >> 8);
    /* Apply the level HP curve to whichever slot just filled. */
    for (int i = MAX_ASTEROIDS - 1; i >= 0; i--) {
        if (g_game.asteroids[i].active && g_game.asteroids[i].y == y) {
            int hp = (g_game.asteroids[i].hp * story_hp_scale()) / 100;
            if (hp < 1) hp = 1;
            g_game.asteroids[i].hp = hp;
            break;
        }
    }
}

/* OBJ_BIGGAME needs a guaranteed big rock on the board: the queue may be
 * exhausted or rolling mediums, and the objective would stall behind them. */
static void story_force_spawn_large(void) {
    int mult = (get_diff_speed_mult() * story_speed_scale()) / 100;
    int x = TO_FIXED((rand() % (SCREEN_WIDTH - 40)) + 20);
    int y = -TO_FIXED((rand() % 60) + 8);
    int vx = ((rand() % 150) - 75) * mult >> 8;
    int vy = ((rand() % 70) + 70) * mult >> 8;
    int tmult = asteroid_speed_mult(AST_LARGE);
    spawn_asteroid(AST_LARGE, x, y, (vx * tmult) >> 8, (vy * tmult) >> 8);
    for (int i = MAX_ASTEROIDS - 1; i >= 0; i--) {
        if (g_game.asteroids[i].active && g_game.asteroids[i].y == y) {
            int hp = (g_game.asteroids[i].hp * story_hp_scale()) / 100;
            if (hp < 1) hp = 1;
            g_game.asteroids[i].hp = hp;
            break;
        }
    }
}

static bool story_spawn_hunter(void) {
    int mult = (get_diff_speed_mult() * story_speed_scale()) / 100;
    for (int i = 0; i < MAX_DRONES; i++) {
        if (g_game.drones[i].active) continue;
        g_game.drones[i].x = TO_FIXED((rand() % (SCREEN_WIDTH - 40)) + 20);
        g_game.drones[i].y = -TO_FIXED((rand() % 40) + 14);
        g_game.drones[i].vx = 0;
        g_game.drones[i].vy = (60 + s_story_level) * mult >> 8;
        int base_cd = 78 - s_story_level;
        if (base_cd < 26) base_cd = 26;
        /* Sharpshooters: fewer of them, but they hardly stop firing. */
        if (story_mod() == MOD_SNIPERS) base_cd = (base_cd * 45) / 100;
        g_game.drones[i].shoot_timer = (rand() % 40) + base_cd;
        g_game.drones[i].burst_timer = 0;
        g_game.drones[i].burst_shots = 0;
        g_game.drones[i].phase = rand() % 256;
        int hp = (3 * story_hp_scale()) / 100;
        if (hp < 2) hp = 2;
        if (hp > 14) hp = 14;
        g_game.drones[i].hp = hp;
        g_game.drones[i].hp_frac = 0;
        g_game.drones[i].accent = (u8)(rand() % NUM_ACCENTS);
        g_game.drones[i].style = (u8)(rand() % NUM_SHIP_STYLES);
        g_game.drones[i].active = true;
        return true;
    }
    return false;
}

static void story_spawn_boss(int boss_id);

static void story_begin_level(void) {
    const StoryLevel* L = story_cur();
    s_story_kills = 0;
    s_story_bigs = 0;
    s_story_spawned = 0;
    s_story_outcome = 0;
    s_story_earned = 0;
    s_story_end_delay = 0;
    s_story_timer = (L->objective == OBJ_SURVIVE || L->objective == OBJ_TIMED)
                  ? L->quota * 90 : 0;
    g_game.wave = s_story_level;
    /* The opening card is dismissed explicitly by game_story_continue().
     * Keep the legacy timer non-zero for snapshot/UI compatibility; it does
     * not count down while the card is waiting. */
    g_game.wave_banner_timer = 1;
    g_game.intermission_timer = 9999;   /* story never auto-advances waves */
    g_game.spawn_timer = 40;

    if (L->objective == OBJ_BOSS) {
        s_story_to_spawn = 0;
        s_story_queue_len = s_story_queue_pos = 0;
        s_story_pending_med = 0;
        int boss_id = story_boss_for_level(s_story_level);
        story_spawn_boss(boss_id >= 0 ? boss_id : 0);
        return;
    }

    /* Pre-roll every rock this level will release, so the HUD can state the
     * exact medium-equivalent total up front instead of guessing. */
    int total = L->rocks;
    if (total > MAX_ASTEROIDS) total = MAX_ASTEROIDS;
    s_story_queue_len = total;
    s_story_queue_pos = 0;
    s_story_pending_med = 0;
    for (int i = 0; i < total; i++) {
        bool large = (rand() % 100) < story_large_chance();
        s_story_queue[i] = large ? 1 : 0;
        s_story_pending_med += asteroid_med_value(large ? AST_LARGE : AST_MED_A);
    }

    /* Open the level with roughly half the field; the rest trickles in so
     * the screen is never instantly unsurvivable. */
    int opening = total / 2;
    if (opening < 3) opening = 3;
    if (opening > total) opening = total;
    s_story_to_spawn = total - opening;
    if (s_story_to_spawn < 0) s_story_to_spawn = 0;
    for (int i = 0; i < opening; i++) story_spawn_rock();
    for (int i = 0; i < L->drones; i++) story_spawn_hunter();
}

/* Called from the kill paths so HUNT levels can count progress. */
static void story_on_hunter_killed(void) {
    if (g_game.mode != GAME_MODE_STORY) return;
    s_story_kills++;
}

/* OBJ_BIGGAME counts the big rocks you personally crack. */
static void story_on_large_killed(void) {
    if (g_game.mode != GAME_MODE_STORY) return;
    s_story_bigs++;
}

static void story_finish(int outcome) {
    if (s_story_outcome != 0) return;
    s_story_outcome = outcome;
    if (outcome == 1) {
        s_story_earned = story_complete_level(s_story_level);
    }
}

/* ── Story Mode: objective tracking ───────────────────────────────────────
 * Runs once per tick in place of the wave escalator. Every objective has a
 * guaranteed end condition, so no level can stall out unwinnable. */
static void story_update_objective(void) {
    if (s_story_outcome != 0) {
        /* Victory pause so the final explosion plays before the result card. */
        if (s_story_end_delay > 0) s_story_end_delay--;
        return;
    }

    const StoryLevel* L = story_cur();
    int rocks = count_active_asteroids();
    int hunters = count_active_drones();
    int want_hunters = story_drone_target();
    int cd_pct = story_spawn_cd_pct();

    switch (L->objective) {
        case OBJ_BOSS:
            /* Ends via defeat_boss() -> story_finish(1). Nothing to poll. */
            break;

        case OBJ_CLEAR:
            /* Release the rest of the field a few at a time, then win when
             * the sky is empty. */
            if (s_story_to_spawn > 0) {
                if (--g_game.spawn_timer <= 0) {
                    int batch = 1 + s_story_level / 18;
                    if (story_mod() == MOD_TRICKLE) batch = 1;
                    if (story_mod() == MOD_STORM)   batch += 2;
                    for (int i = 0; i < batch && s_story_to_spawn > 0; i++) {
                        if (rocks >= MAX_ASTEROIDS - 8) break;
                        story_spawn_rock();
                        s_story_to_spawn--;
                        rocks++;
                    }
                    int cd = 70 - s_story_level;
                    if (cd < 22) cd = 22;
                    g_game.spawn_timer = (cd * cd_pct) / 100;
                }
            } else if (count_medium_equivalents() == 0 && s_story_pending_med == 0 &&
                       hunters == 0) {
                /* The objective is the counter: once every big and medium is
                 * gone the level is cleared. Leftover small/tiny debris is
                 * still on screen and still lethal, but it was never part of
                 * the count and does not hold the level open. */
                s_story_end_delay = 45;
                story_finish(1);
            }
            break;

        case OBJ_TIMED:
            /* Same job as CLEAR, but against a clock: run it out and the
             * level is lost. The only failure that is not "you died", which
             * makes these play completely differently from a plain clear. */
            if (s_story_timer > 0) s_story_timer--;
            if (s_story_timer <= 0) {
                story_finish(2);
                break;
            }
            if (s_story_to_spawn > 0) {
                if (--g_game.spawn_timer <= 0) {
                    int batch = 2 + s_story_level / 16;
                    if (story_mod() == MOD_TRICKLE) batch = 1;
                    if (story_mod() == MOD_STORM)   batch += 2;
                    for (int i = 0; i < batch && s_story_to_spawn > 0; i++) {
                        if (rocks >= MAX_ASTEROIDS - 8) break;
                        story_spawn_rock();
                        s_story_to_spawn--;
                        rocks++;
                    }
                    int cd = 46 - s_story_level / 3;
                    if (cd < 16) cd = 16;
                    g_game.spawn_timer = (cd * cd_pct) / 100;
                }
            } else if (count_medium_equivalents() == 0 && s_story_pending_med == 0 &&
                       hunters == 0) {
                s_story_end_delay = 45;
                story_finish(1);
            }
            break;

        case OBJ_BIGGAME:
            /* Crack N LARGE rocks. The mediums and debris they shed are
             * weather, not the objective, so the field is a constant churn
             * you fly through looking for the next big one. */
            if (s_story_bigs >= L->quota) {
                s_story_end_delay = 45;
                story_finish(1);
                break;
            }
            if (--g_game.spawn_timer <= 0) {
                /* Keep at least one big rock on offer at all times, or the
                 * objective would stall behind a screen of debris. */
                int bigs_up = 0;
                for (int i = 0; i < MAX_ASTEROIDS; i++)
                    if (g_game.asteroids[i].active && g_game.asteroids[i].type == AST_LARGE)
                        bigs_up++;
                int want_bigs = 2 + s_story_level / 20;
                if (bigs_up < want_bigs && rocks < MAX_ASTEROIDS - 6) {
                    story_force_spawn_large();
                } else if (hunters < want_hunters) {
                    story_spawn_hunter();
                } else if (rocks < L->rocks) {
                    story_spawn_rock();
                }
                int cd = 66 - s_story_level / 2;
                if (cd < 20) cd = 20;
                g_game.spawn_timer = (cd * cd_pct) / 100;
            }
            break;

        case OBJ_HUNT:
            /* Keep exactly enough hunters on screen to chase, and top the
             * rocks up as ambient pressure, until the quota is met. */
            if (s_story_kills >= L->quota) {
                s_story_end_delay = 45;
                story_finish(1);
                break;
            }
            if (--g_game.spawn_timer <= 0) {
                if (hunters < want_hunters) story_spawn_hunter();
                else if (rocks < L->rocks) story_spawn_rock();
                int cd = 80 - s_story_level;
                if (cd < 26) cd = 26;
                g_game.spawn_timer = (cd * cd_pct) / 100;
            }
            break;

        case OBJ_SURVIVE:
            if (s_story_timer > 0) s_story_timer--;
            if (s_story_timer <= 0) {
                s_story_end_delay = 45;
                story_finish(1);
                break;
            }
            if (--g_game.spawn_timer <= 0) {
                if (rocks < L->rocks) story_spawn_rock();
                else if (hunters < want_hunters) story_spawn_hunter();
                int cd = 60 - s_story_level / 2;
                if (cd < 18) cd = 18;
                g_game.spawn_timer = (cd * cd_pct) / 100;
            }
            break;

        default:
            break;
    }
}

/* ── Story Mode: the seven bosses ─────────────────────────────────────────
 * Each boss owns a distinct movement pattern, attack set and gimmick. They
 * all share the Boss struct; the story extension fields carry per-boss
 * state. HP is tuned so a fair loadout kills them in 45-90 seconds. */

static int story_boss_hp(int boss_id) {
    /* Tuned against a headless playthrough harness (tools/story_sim): a
     * mid-progression loadout kills each boss in roughly 60-120 s, and the
     * ladder is strictly monotonic. Real players out-damage the test pilot,
     * so these land a little faster in practice. */
    static const int base[STORY_SECTOR_COUNT] = {
        340, 780, 2400, 3200, 8500, 5800, 30000
    };
    int id = (boss_id < 0 || boss_id >= STORY_SECTOR_COUNT) ? 0 : boss_id;
    int hp = base[id];
    if (g_settings.difficulty == DIFF_CADET) hp = (hp * 3) / 4;
    else if (g_settings.difficulty == DIFF_ACE) hp = (hp * 13) / 10;
    return hp;
}

static void story_spawn_boss(int boss_id) {
    memset(&g_game.boss, 0, sizeof(Boss));
    Boss* b = &g_game.boss;
    b->x = TO_FIXED(SCREEN_WIDTH / 2);
    b->y = -TO_FIXED(60);
    b->anchor_x = b->x;
    b->vx = TO_FIXED(1) + 40;
    b->vy = TO_FIXED(1) + 60;
    b->mini = false;
    b->story_id = boss_id;
    b->hp_max = story_boss_hp(boss_id);
    b->hp = b->hp_max;
    b->hp_frac = 0;
    b->phase = SB_ENTER;
    b->phase_timer = 120;
    b->beam_width = TO_FIXED(10);
    b->active = true;
    b->stage = 0;
    b->sweep_dir = 1;
    b->clone_active = false;

    if (boss_id == SBOSS_SCRAPTITAN || boss_id == SBOSS_VAULTWARDEN) {
        for (int i = 0; i < 4; i++) b->node_hp[i] = b->hp_max / 12;
        if (boss_id == SBOSS_VAULTWARDEN) b->shield = 1;
    }

    g_game.boss_active = true;
    g_game.boss_hit_flash = 0;
    audio_begin_boss_music();
    for (int i = 0; i < MAX_DRONES; i++) g_game.drones[i].active = false;
    for (int i = 0; i < MAX_ASTEROIDS; i++) g_game.asteroids[i].active = false;
}

/* Aimed shot helper in story space. */
static void sb_shoot_at_player(int from_x, int from_y, int speed_fixed, int heavy) {
    const Player* aim = ai_target_ship();
    int dx = FROM_FIXED(aim->x) - from_x;
    int dy = FROM_FIXED(aim->y) - from_y;
    double mag = hypot((double)dx, (double)dy);
    if (mag < 1.0) mag = 1.0;
    double sp = (double)speed_fixed / 256.0;
    add_boss_bullet(TO_FIXED(from_x), TO_FIXED(from_y),
                    (int)(dx / mag * sp * 256.0), (int)(dy / mag * sp * 256.0), heavy);
}

/* Evenly spaced ring, optionally rotated by `spin`. */
static void sb_ring(int from_x, int from_y, int n, int speed_fixed, int spin, int heavy_every) {
    for (int i = 0; i < n; i++) {
        int ang = (i * 65536) / n + spin;
        int vx = (lu_cos(ang) * speed_fixed) >> 12;
        int vy = (lu_sin(ang) * speed_fixed) >> 12;
        add_boss_bullet(TO_FIXED(from_x), TO_FIXED(from_y), vx, vy,
                        heavy_every && (i % heavy_every) == 0);
    }
}

/* A fan of `n` shots aimed downward, spread across `spread` BAM units. */
static void sb_fan(int from_x, int from_y, int n, int speed_fixed, int spread) {
    for (int i = 0; i < n; i++) {
        int centered = i * 2 - (n - 1);
        int ang = 16384 + (centered * spread) / (n > 1 ? (n - 1) : 1); /* 16384 = straight down */
        int vx = (lu_cos(ang) * speed_fixed) >> 12;
        int vy = (lu_sin(ang) * speed_fixed) >> 12;
        add_boss_bullet(TO_FIXED(from_x), TO_FIXED(from_y), vx, vy, (i & 1) == 0);
    }
}

static void sb_drift(Boss* b, int speed) {
    b->x += b->sweep_dir * speed;
    if (b->x < TO_FIXED(24)) { b->x = TO_FIXED(24); b->sweep_dir = 1; }
    if (b->x > TO_FIXED(SCREEN_WIDTH - 24)) { b->x = TO_FIXED(SCREEN_WIDTH - 24); b->sweep_dir = -1; }
}

static void sb_hover(Boss* b, int target_y, int speed) {
    if (b->y < TO_FIXED(target_y) - speed) b->y += speed;
    else if (b->y > TO_FIXED(target_y) + speed) b->y -= speed;
}

/* Fraction of HP remaining, 0..100. */
static int sb_hp_pct(const Boss* b) {
    if (b->hp_max <= 0) return 0;
    return (b->hp * 100) / b->hp_max;
}

/* ── BOSS 1 - RUSTJAW (L10) ───────────────────────────────────────────────
 * A scrapyard jaw. Slams down at the player, spits chewed scrap in a wide
 * arc, and gets faster the closer to death it is. Teaches dodging a telegraph. */
static void sb_rustjaw(Boss* b) {
    int cx = FROM_FIXED(b->x), cy = FROM_FIXED(b->y);
    switch (b->phase) {
        case SB_IDLE:
            sb_drift(b, TO_FIXED(1) + 60);
            sb_hover(b, 34, TO_FIXED(1));
            if (--b->cooldown <= 0) {
                sb_shoot_at_player(cx, cy + 14, TO_FIXED(4), 0);
                b->cooldown = (sb_hp_pct(b) < 40) ? 26 : 40;
            }
            if (b->phase_timer <= 0) {
                b->phase = (rand() & 1) ? SB_ATTACK_A : SB_ATTACK_B;
                b->phase_timer = 150;
                b->aim_x = ai_target_ship()->x;
                b->charge = 0;
            }
            break;
        case SB_ATTACK_A: {   /* JAW SLAM: line up, telegraph, dive, recover */
            if (b->phase_timer > 110) {
                int diff = b->aim_x - b->x;
                int step = TO_FIXED(3);
                if (diff > step) b->x += step; else if (diff < -step) b->x -= step; else b->x = b->aim_x;
                if ((b->phase_timer & 3) == 0)
                    spawn_particle(b->x, b->y + TO_FIXED(16), 0, 120, PAL_TEXT_RED, 8);
            } else if (b->phase_timer > 60) {
                b->y += TO_FIXED(5);
                if (FROM_FIXED(b->y) > SCREEN_HEIGHT - 40) b->y = TO_FIXED(SCREEN_HEIGHT - 40);
                if ((b->phase_timer & 7) == 0) sb_fan(cx, cy + 14, 3, TO_FIXED(4), 5000);
            } else {
                b->y -= TO_FIXED(3);
                if (b->y <= TO_FIXED(34)) { b->y = TO_FIXED(34); b->phase = SB_IDLE; b->phase_timer = 90; b->cooldown = 30; }
            }
            if (b->phase_timer <= 0) { b->phase = SB_IDLE; b->phase_timer = 90; }
            break;
        }
        case SB_ATTACK_B:     /* SCRAP SPIT: wide arcs of debris */
            sb_drift(b, TO_FIXED(2));
            if ((b->phase_timer % 26) == 0) sb_fan(cx, cy + 14, sb_hp_pct(b) < 50 ? 7 : 5, TO_FIXED(3), 14000);
            if (b->phase_timer <= 0) { b->phase = SB_IDLE; b->phase_timer = 80; b->cooldown = 30; }
            break;
        default:
            b->phase = SB_IDLE; b->phase_timer = 70; b->cooldown = 40;
            break;
    }
}

/* ── BOSS 2 - THE TWINS (L20) ─────────────────────────────────────────────
 * One hull until 50%, then it splits: the clone mirrors your position while
 * the original hunts you, and they cross-fire the gap between them. */
static void sb_twins(Boss* b) {
    int cx = FROM_FIXED(b->x), cy = FROM_FIXED(b->y);

    if (!b->clone_active && sb_hp_pct(b) <= 50) {
        b->clone_active = true;
        b->clone_x = TO_FIXED(SCREEN_WIDTH) - b->x;
        b->clone_y = b->y;
        b->clone_hp = b->hp / 2;
        b->stage = 1;
        g_game.shake_timer = 24;
        for (int i = 0; i < 4; i++) trigger_explosion(cx + (rand() % 24) - 12, cy);
    }

    switch (b->phase) {
        case SB_IDLE:
            sb_hover(b, 32, TO_FIXED(1));
            sb_drift(b, TO_FIXED(2));
            if (--b->cooldown <= 0) {
                sb_shoot_at_player(cx, cy + 12, TO_FIXED(4) + 64, 0);
                b->cooldown = b->clone_active ? 30 : 42;
            }
            if (b->phase_timer <= 0) {
                b->phase = b->clone_active ? SB_ATTACK_C : ((rand() & 1) ? SB_ATTACK_A : SB_ATTACK_B);
                b->phase_timer = 140;
            }
            break;
        case SB_ATTACK_A:     /* MIRROR VOLLEY: alternating left/right walls */
            sb_drift(b, TO_FIXED(3));
            if ((b->phase_timer % 18) == 0) {
                int side = (b->phase_timer / 18) & 1;
                int ox = side ? -30 : 30;
                sb_fan(cx + ox, cy + 12, 4, TO_FIXED(3) + 128, 8000);
            }
            if (b->phase_timer <= 0) { b->phase = SB_IDLE; b->phase_timer = 80; b->cooldown = 26; }
            break;
        case SB_ATTACK_B:     /* SPIN RING */
            sb_hover(b, 30, TO_FIXED(1));
            b->spin += 900;
            if ((b->phase_timer % 22) == 0) sb_ring(cx, cy, 9, TO_FIXED(3), b->spin, 3);
            if (b->phase_timer <= 0) { b->phase = SB_IDLE; b->phase_timer = 80; b->cooldown = 26; }
            break;
        case SB_ATTACK_C: {   /* CROSSFIRE: both halves squeeze the middle */
            sb_drift(b, TO_FIXED(3));
            if ((b->phase_timer % 14) == 0) {
                sb_shoot_at_player(cx, cy + 12, TO_FIXED(5), 1);
                sb_shoot_at_player(FROM_FIXED(b->clone_x), FROM_FIXED(b->clone_y) + 12, TO_FIXED(5), 0);
            }
            if (b->phase_timer <= 0) { b->phase = SB_IDLE; b->phase_timer = 70; b->cooldown = 22; }
            break;
        }
        default: b->phase = SB_IDLE; b->phase_timer = 70; b->cooldown = 40; break;
    }

    /* The clone mirrors the player's column, always threatening from the side. */
    if (b->clone_active) {
        int want = TO_FIXED(SCREEN_WIDTH) - ai_target_ship()->x;
        int diff = want - b->clone_x;
        int step = TO_FIXED(1) + 100;
        if (diff > step) b->clone_x += step; else if (diff < -step) b->clone_x -= step; else b->clone_x = want;
        if (b->clone_x < TO_FIXED(20)) b->clone_x = TO_FIXED(20);
        if (b->clone_x > TO_FIXED(SCREEN_WIDTH - 20)) b->clone_x = TO_FIXED(SCREEN_WIDTH - 20);
        b->clone_y = b->y + TO_FIXED(10);
        if ((s_game_frame % 40) == 0)
            add_boss_bullet(b->clone_x, b->clone_y + TO_FIXED(10), 0, TO_FIXED(4), false);
    }
}

/* ── BOSS 3 - FROSTWIDOW (L30) ────────────────────────────────────────────
 * Spins a web: static shard "strands" that linger, plus a chilling beam that
 * slows nothing but punishes standing still. Forces constant repositioning. */
static void sb_frostwidow(Boss* b) {
    int cx = FROM_FIXED(b->x), cy = FROM_FIXED(b->y);
    switch (b->phase) {
        case SB_IDLE:
            sb_hover(b, 30, TO_FIXED(1));
            sb_drift(b, TO_FIXED(1) + 100);
            if (--b->cooldown <= 0) { sb_fan(cx, cy + 12, 3, TO_FIXED(4), 6000); b->cooldown = 34; }
            if (b->phase_timer <= 0) {
                int roll = rand() % 3;
                b->phase = (roll == 0) ? SB_ATTACK_A : (roll == 1 ? SB_ATTACK_B : SB_ATTACK_C);
                b->phase_timer = 160;
                b->beam_x = ai_target_ship()->x;
                b->spin = 0;
            }
            break;
        case SB_ATTACK_A:     /* WEB: slow drifting shards from eight anchors */
            sb_hover(b, 26, TO_FIXED(1));
            if ((b->phase_timer % 30) == 0) {
                b->spin += 4000;
                sb_ring(cx, cy, 8, TO_FIXED(1) + 128, b->spin, 2);
            }
            if (b->phase_timer <= 0) { b->phase = SB_IDLE; b->phase_timer = 80; b->cooldown = 28; }
            break;
        case SB_ATTACK_B:     /* FROST LANCE: telegraphed tracking beam */
            if (b->phase_timer > 100) {
                b->beam_x = ai_target_ship()->x;
                if ((b->phase_timer & 3) == 0)
                    spawn_particle(b->beam_x + TO_FIXED((rand() & 7) - 4), TO_FIXED(40 + rand() % 90),
                                   0, 70, PAL_TEXT_CYAN, 9);
                b->beam_timer = 0;
            } else {
                if (b->beam_timer == 0) { b->beam_timer = 100; b->beam_width = TO_FIXED(9); }
                int diff = ai_target_ship()->x - b->beam_x;
                int step = TO_FIXED(1) / 6;
                if (diff > step) b->beam_x += step; else if (diff < -step) b->beam_x -= step;
                b->beam_timer--;
                int beam_px = FROM_FIXED(b->beam_x);
                int px = FROM_FIXED(g_game.player.x), py = FROM_FIXED(g_game.player.y);
                if (g_game.player.invulnerable_timer == 0 && py > 12 &&
                    px >= beam_px - 9 && px <= beam_px + 9) damage_player();
                if ((b->phase_timer & 1) == 0)
                    spawn_particle(TO_FIXED(beam_px + (rand() & 31) - 15), TO_FIXED(30 + rand() % 110),
                                   (rand() & 63) - 32, -((rand() & 31) + 40), PAL_TEXT_CYAN, 10);
            }
            if (b->phase_timer <= 0) { b->phase = SB_IDLE; b->phase_timer = 90; b->cooldown = 34; }
            break;
        case SB_ATTACK_C:     /* SKITTER: fast side dashes with trailing ice */
            b->x += b->sweep_dir * (TO_FIXED(4) + 80);
            if (b->x < TO_FIXED(24)) { b->x = TO_FIXED(24); b->sweep_dir = 1; }
            if (b->x > TO_FIXED(SCREEN_WIDTH - 24)) { b->x = TO_FIXED(SCREEN_WIDTH - 24); b->sweep_dir = -1; }
            if ((b->phase_timer & 7) == 0)
                add_boss_bullet(b->x, b->y + TO_FIXED(12), 0, TO_FIXED(2) + 100, false);
            if (b->phase_timer <= 0) { b->phase = SB_IDLE; b->phase_timer = 70; b->cooldown = 30; }
            break;
        default: b->phase = SB_IDLE; b->phase_timer = 70; b->cooldown = 40; break;
    }
}

/* ── BOSS 4 - SCRAP TITAN (L40) ───────────────────────────────────────────
 * Four armour plates soak damage until broken; it drags you toward it with a
 * magnet field and hurls whole asteroids. Slow, heavy, unavoidable pressure. */
static void sb_scraptitan(Boss* b) {
    int cx = FROM_FIXED(b->x), cy = FROM_FIXED(b->y);
    int plates = 0;
    for (int i = 0; i < 4; i++) if (b->node_hp[i] > 0) plates++;

    switch (b->phase) {
        case SB_IDLE:
            sb_hover(b, 32, TO_FIXED(1));
            sb_drift(b, TO_FIXED(1));
            if (--b->cooldown <= 0) { sb_fan(cx, cy + 16, 5, TO_FIXED(3) + 100, 11000); b->cooldown = 44; }
            if (b->phase_timer <= 0) {
                int roll = rand() % 3;
                b->phase = (roll == 0) ? SB_ATTACK_A : (roll == 1 ? SB_ATTACK_B : SB_ATTACK_C);
                b->phase_timer = 170;
            }
            break;
        case SB_ATTACK_A:     /* MAGNET: pulls the player upward into the shots */
            sb_hover(b, 30, TO_FIXED(1));
            if (g_game.player.y > TO_FIXED(30))
                g_game.player.y -= (plates > 2) ? 26 : 40;
            if ((b->phase_timer % 20) == 0) sb_ring(cx, cy, 10, TO_FIXED(3), b->spin += 2200, 2);
            if ((b->phase_timer & 3) == 0)
                spawn_particle(g_game.player.x, g_game.player.y + TO_FIXED(10), 0, -140, PAL_TEXT_VIOLET, 7);
            if (b->phase_timer <= 0) { b->phase = SB_IDLE; b->phase_timer = 90; b->cooldown = 30; }
            break;
        case SB_ATTACK_B:     /* ROCK THROW: launches real asteroids at you */
            sb_drift(b, TO_FIXED(2));
            if ((b->phase_timer % 34) == 0) {
                int mult = (get_diff_speed_mult() * story_speed_scale()) / 100;
                spawn_asteroid(AST_LARGE, b->x, b->y + TO_FIXED(18),
                               ((rand() % 120) - 60) * mult >> 8, (140 * mult) >> 8);
            }
            if (b->phase_timer <= 0) { b->phase = SB_IDLE; b->phase_timer = 80; b->cooldown = 34; }
            break;
        case SB_ATTACK_C:     /* STOMP: slams the floor, shock columns rise */
            if (b->phase_timer > 120) { b->y += TO_FIXED(3); }
            else if (b->phase_timer > 90) {
                if (b->phase_timer == 120) { g_game.shake_timer = 20; audio_play_sfx(SFX_EXPLOSION); }
                for (int i = 0; i < 3; i++) {
                    int col = ((rand() % (SCREEN_WIDTH - 40)) + 20);
                    if ((b->phase_timer % 10) == 0)
                        add_boss_bullet(TO_FIXED(col), TO_FIXED(SCREEN_HEIGHT), 0, -TO_FIXED(3), false);
                }
            } else { b->y -= TO_FIXED(2); if (b->y < TO_FIXED(32)) b->y = TO_FIXED(32); }
            if (b->phase_timer <= 0) { b->phase = SB_IDLE; b->phase_timer = 80; b->cooldown = 30; }
            break;
        default: b->phase = SB_IDLE; b->phase_timer = 70; b->cooldown = 44; break;
    }
}

/* ── BOSS 5 - EMBERLASH (L50) ─────────────────────────────────────────────
 * Two counter-rotating whips of fire. The safe gaps rotate continuously, so
 * you must keep orbiting rather than camping a corner. */
static void sb_emberlash(Boss* b) {
    int cx = FROM_FIXED(b->x), cy = FROM_FIXED(b->y);
    b->spin += (sb_hp_pct(b) < 45) ? 1500 : 1000;

    switch (b->phase) {
        case SB_IDLE:
            sb_hover(b, 28, TO_FIXED(1));
            sb_drift(b, TO_FIXED(1) + 80);
            /* The whips are always live: two arms of embers sweeping round. */
            if ((s_game_frame % 7) == 0) {
                add_boss_bullet(TO_FIXED(cx), TO_FIXED(cy),
                                (lu_cos(b->spin) * (TO_FIXED(3))) >> 12,
                                (lu_sin(b->spin) * (TO_FIXED(3))) >> 12, false);
                add_boss_bullet(TO_FIXED(cx), TO_FIXED(cy),
                                (lu_cos(b->spin + 32768) * (TO_FIXED(3))) >> 12,
                                (lu_sin(b->spin + 32768) * (TO_FIXED(3))) >> 12, false);
            }
            if (b->phase_timer <= 0) {
                b->phase = (rand() & 1) ? SB_ATTACK_A : SB_ATTACK_B;
                b->phase_timer = 150;
            }
            break;
        case SB_ATTACK_A:     /* FLARE: aimed heavy bolts between whip passes */
            sb_drift(b, TO_FIXED(2) + 60);
            if ((b->phase_timer % 24) == 0) sb_shoot_at_player(cx, cy + 12, TO_FIXED(5) + 60, 1);
            if ((s_game_frame % 9) == 0)
                add_boss_bullet(TO_FIXED(cx), TO_FIXED(cy),
                                (lu_cos(b->spin) * TO_FIXED(3)) >> 12,
                                (lu_sin(b->spin) * TO_FIXED(3)) >> 12, false);
            if (b->phase_timer <= 0) { b->phase = SB_IDLE; b->phase_timer = 90; }
            break;
        case SB_ATTACK_B:     /* SOLAR WIND: dense downward curtain with a gap */
            sb_hover(b, 24, TO_FIXED(1));
            if ((b->phase_timer % 12) == 0) {
                int gap = 30 + (rand() % (SCREEN_WIDTH - 90));
                for (int x = 16; x < SCREEN_WIDTH - 16; x += 26) {
                    if (x > gap && x < gap + 52) continue;   /* always a way through */
                    add_boss_bullet(TO_FIXED(x), TO_FIXED(cy + 12), 0, TO_FIXED(3), false);
                }
            }
            if (b->phase_timer <= 0) { b->phase = SB_IDLE; b->phase_timer = 90; }
            break;
        default: b->phase = SB_IDLE; b->phase_timer = 80; break;
    }
}

/* ── BOSS 6 - VAULT WARDEN (L60) ──────────────────────────────────────────
 * Invulnerable behind a shield until all four turret nodes are destroyed;
 * the nodes rotate around the hull and fire independently. */
static void sb_vaultwarden(Boss* b) {
    int cx = FROM_FIXED(b->x), cy = FROM_FIXED(b->y);
    int alive = 0;
    for (int i = 0; i < 4; i++) if (b->node_hp[i] > 0) alive++;
    b->shield = (alive > 0) ? 1 : 0;
    b->spin += 700;

    switch (b->phase) {
        case SB_IDLE:
            sb_hover(b, 34, TO_FIXED(1));
            sb_drift(b, TO_FIXED(1) + 40);
            /* Each surviving node fires on its own cadence. */
            for (int i = 0; i < 4; i++) {
                if (b->node_hp[i] <= 0) continue;
                if ((s_game_frame % (34 + i * 7)) == 0) {
                    int ang = b->spin + i * 16384;
                    int nx = cx + ((lu_cos(ang) * 30) >> 12);
                    int ny = cy + ((lu_sin(ang) * 22) >> 12);
                    sb_shoot_at_player(nx, ny, TO_FIXED(4), 0);
                }
            }
            if (--b->cooldown <= 0 && !b->shield) {
                sb_fan(cx, cy + 14, 7, TO_FIXED(4), 12000);
                b->cooldown = 30;
            }
            if (b->phase_timer <= 0) {
                b->phase = b->shield ? SB_ATTACK_A : SB_ATTACK_B;
                b->phase_timer = 160;
            }
            break;
        case SB_ATTACK_A:     /* LOCKDOWN: sweeping searchlight volleys */
            sb_drift(b, TO_FIXED(3));
            if ((b->phase_timer % 16) == 0) sb_fan(cx, cy + 14, 4, TO_FIXED(3) + 80, 9000);
            if (b->phase_timer <= 0) { b->phase = SB_IDLE; b->phase_timer = 80; b->cooldown = 28; }
            break;
        case SB_ATTACK_B:     /* BREACH RAGE: unshielded, everything at once */
            sb_hover(b, 30, TO_FIXED(1));
            if ((b->phase_timer % 18) == 0) sb_ring(cx, cy, 12, TO_FIXED(3) + 60, b->spin, 3);
            if ((b->phase_timer % 30) == 0) sb_shoot_at_player(cx, cy + 14, TO_FIXED(6), 1);
            if (b->phase_timer <= 0) { b->phase = SB_IDLE; b->phase_timer = 70; b->cooldown = 24; }
            break;
        default: b->phase = SB_IDLE; b->phase_timer = 70; b->cooldown = 34; break;
    }
}

/* ── BOSS 7 - THE REALITY QUEEN (L70) ─────────────────────────────────────
 * The finale. Jack flies as the Reality King against the Queen's ship, in
 * three cinematic stages: her honour guard, the unmaking (reality tears that
 * rewrite the field), and a last desperate all-out barrage. */
static void sb_realityqueen(Boss* b) {
    int cx = FROM_FIXED(b->x), cy = FROM_FIXED(b->y);
    int pct = sb_hp_pct(b);

    /* Stage transitions punctuate the fight with a stagger + shockwave. */
    if (b->stage == 0 && pct <= 66) {
        b->stage = 1; b->phase = SB_STAGGER; b->phase_timer = 110;
        g_game.shake_timer = 30; g_game.wave_banner_timer = 100;
    } else if (b->stage == 1 && pct <= 33) {
        b->stage = 2; b->phase = SB_STAGGER; b->phase_timer = 110;
        g_game.shake_timer = 40; g_game.wave_banner_timer = 100;
    }

    b->spin += 800 + b->stage * 400;

    switch (b->phase) {
        case SB_STAGGER:      /* cinematic beat: she reels, the screen burns */
            sb_hover(b, 30, TO_FIXED(1));
            if ((b->phase_timer & 3) == 0) {
                trigger_explosion(cx + (rand() % 40) - 20, cy + (rand() % 20) - 10);
                spawn_particle(b->x, b->y, (rand() & 255) - 128, (rand() & 255) - 128,
                               PAL_TEXT_VIOLET, 12);
            }
            if (b->phase_timer <= 0) {
                sb_ring(cx, cy, 16, TO_FIXED(3), b->spin, 2);   /* shockwave */
                b->phase = SB_IDLE; b->phase_timer = 60; b->cooldown = 20;
            }
            break;
        case SB_IDLE:
            sb_hover(b, 30, TO_FIXED(1) + 60);
            sb_drift(b, TO_FIXED(2));
            if (--b->cooldown <= 0) {
                sb_shoot_at_player(cx, cy + 14, TO_FIXED(5), 1);
                b->cooldown = 30 - b->stage * 6;
            }
            if (b->phase_timer <= 0) {
                int roll = rand() % (b->stage >= 1 ? 3 : 2);
                b->phase = (roll == 0) ? SB_ATTACK_A : (roll == 1 ? SB_ATTACK_B : SB_ATTACK_C);
                b->phase_timer = 170;
                b->beam_x = ai_target_ship()->x;
            }
            break;
        case SB_ATTACK_A:     /* HONOUR GUARD: spiral lattice */
            sb_hover(b, 26, TO_FIXED(1));
            if ((b->phase_timer % (14 - b->stage * 3)) == 0)
                sb_ring(cx, cy, 8 + b->stage * 2, TO_FIXED(3), b->spin, 3);
            if (b->phase_timer <= 0) { b->phase = SB_IDLE; b->phase_timer = 70; b->cooldown = 24; }
            break;
        case SB_ATTACK_B: {   /* THE UNMAKING: twin tearing beams */
            if (b->phase_timer > 120) {
                b->beam_x = ai_target_ship()->x;
                if ((b->phase_timer & 2) == 0)
                    spawn_particle(b->beam_x + TO_FIXED((rand() & 15) - 8), TO_FIXED(30 + rand() % 100),
                                   0, 90, PAL_TEXT_VIOLET, 9);
                b->beam_timer = 0;
            } else {
                if (b->beam_timer == 0) b->beam_timer = 120;
                b->beam_timer--;
                int diff = ai_target_ship()->x - b->beam_x;
                int step = TO_FIXED(1) / 5;
                if (diff > step) b->beam_x += step; else if (diff < -step) b->beam_x -= step;
                int beam_px = FROM_FIXED(b->beam_x);
                int px = FROM_FIXED(g_game.player.x), py = FROM_FIXED(g_game.player.y);
                int half = 10;
                /* Mirrored second tear on the opposite side of the screen. */
                int mirror_px = SCREEN_WIDTH - beam_px;
                if (g_game.player.invulnerable_timer == 0 && py > 12 &&
                    ((px >= beam_px - half && px <= beam_px + half) ||
                     (b->stage >= 2 && px >= mirror_px - half && px <= mirror_px + half)))
                    damage_player();
                if ((b->phase_timer & 1) == 0) {
                    spawn_particle(TO_FIXED(beam_px + (rand() & 31) - 15), TO_FIXED(20 + rand() % 120),
                                   (rand() & 63) - 32, -((rand() & 31) + 40), PAL_TEXT_VIOLET, 10);
                    if (b->stage >= 2)
                        spawn_particle(TO_FIXED(mirror_px + (rand() & 31) - 15), TO_FIXED(20 + rand() % 120),
                                       (rand() & 63) - 32, -((rand() & 31) + 40), PAL_TEXT_RED, 10);
                }
            }
            if (b->phase_timer <= 0) { b->phase = SB_IDLE; b->phase_timer = 80; b->cooldown = 24; }
            break;
        }
        case SB_ATTACK_C:     /* NO TOMORROW: curtain + aimed lances, with gaps */
            sb_drift(b, TO_FIXED(3));
            if ((b->phase_timer % 16) == 0) {
                int gap = 24 + (rand() % (SCREEN_WIDTH - 80));
                for (int x = 14; x < SCREEN_WIDTH - 14; x += 24) {
                    if (x > gap && x < gap + 46) continue;
                    add_boss_bullet(TO_FIXED(x), TO_FIXED(cy + 14), 0, TO_FIXED(3) + 40, false);
                }
            }
            if ((b->phase_timer % 40) == 0) sb_shoot_at_player(cx, cy + 14, TO_FIXED(6), 1);
            if (b->phase_timer <= 0) { b->phase = SB_IDLE; b->phase_timer = 70; b->cooldown = 20; }
            break;
        default: b->phase = SB_IDLE; b->phase_timer = 70; b->cooldown = 26; break;
    }
}

/* Damage gating for bosses with destructible parts.
 * Scrap Titan: four armour plates soak most damage until broken.
 * Vault Warden: fully immune while any turret node lives — you must shoot
 * the rotating nodes off first (hits near a node damage that node). */
static int story_boss_absorb(Boss* b, int dmg, int hit_x, int hit_y) {
    if (g_game.mode != GAME_MODE_STORY) return dmg;

    if (b->story_id == SBOSS_VAULTWARDEN) {
        int cx = FROM_FIXED(b->x), cy = FROM_FIXED(b->y);
        int best = -1, best_d = 0;
        for (int i = 0; i < 4; i++) {
            if (b->node_hp[i] <= 0) continue;
            int ang = b->spin + i * 16384;
            int nx = cx + ((lu_cos(ang) * 30) >> 12);
            int ny = cy + ((lu_sin(ang) * 22) >> 12);
            int d = (hit_x - nx) * (hit_x - nx) + (hit_y - ny) * (hit_y - ny);
            if (best < 0 || d < best_d) { best = i; best_d = d; }
        }
        if (best >= 0) {
            /* Hits land on the nearest surviving node; the hull is sealed. */
            b->node_hp[best] -= dmg;
            if (b->node_hp[best] <= 0) {
                b->node_hp[best] = 0;
                trigger_explosion(hit_x, hit_y);
                g_game.shake_timer = 16;
            }
            b->flash_timer = 4;
            return 0;
        }
        return dmg;                     /* all nodes down: hull is open */
    }

    if (b->story_id == SBOSS_SCRAPTITAN) {
        for (int i = 0; i < 4; i++) {
            if (b->node_hp[i] > 0) {
                b->node_hp[i] -= dmg;
                if (b->node_hp[i] <= 0) {
                    b->node_hp[i] = 0;
                    trigger_explosion(hit_x, hit_y);
                    g_game.shake_timer = 14;
                }
                return dmg / 4;         /* plates bleed most of the hit */
            }
        }
        return dmg;
    }

    if (b->story_id == SBOSS_TWINS && b->clone_active) {
        /* Shots landing nearer the clone chew the clone down instead. */
        int dx = hit_x - FROM_FIXED(b->clone_x);
        int dy = hit_y - FROM_FIXED(b->clone_y);
        int dcx = hit_x - FROM_FIXED(b->x);
        int dcy = hit_y - FROM_FIXED(b->y);
        if (dx * dx + dy * dy < dcx * dcx + dcy * dcy) {
            b->clone_hp -= dmg;
            if (b->clone_hp <= 0) {
                b->clone_active = false;
                trigger_explosion(FROM_FIXED(b->clone_x), FROM_FIXED(b->clone_y));
                g_game.shake_timer = 20;
            }
            return dmg / 2;             /* the pair share a health pool */
        }
    }

    return dmg;
}

/* Dispatch: run the current story boss's script for one tick. */
static void story_boss_ai(Boss* b) {
    if (b->phase == SB_ENTER) {
        b->y += b->vy;
        if (b->y >= TO_FIXED(32)) {
            b->y = TO_FIXED(32);
            b->phase = SB_IDLE;
            b->phase_timer = 90;
            b->cooldown = 50;
        }
        return;
    }
    b->phase_timer--;
    switch (b->story_id) {
        case SBOSS_RUSTJAW:      sb_rustjaw(b); break;
        case SBOSS_TWINS:        sb_twins(b); break;
        case SBOSS_FROSTWIDOW:   sb_frostwidow(b); break;
        case SBOSS_SCRAPTITAN:   sb_scraptitan(b); break;
        case SBOSS_EMBERLASH:    sb_emberlash(b); break;
        case SBOSS_VAULTWARDEN:  sb_vaultwarden(b); break;
        default:                 sb_realityqueen(b); break;
    }
}

static void add_player_bullet(int x, int y, int vx, int vy, int damage, bool heavy, int owner) {
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
            g_game.bullets[i].owner = (u8)owner;
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

/* Boss HP is based on wave and selected difficulty, never on the equipped
 * weapon.  The old loadout-scaling formula erased the value of buying a
 * stronger rig: every upgrade still took roughly the same 15 shots. */
static int boss_max_hp(void) {
    int tier = is_mini_boss_wave(g_game.wave)
        ? (g_game.wave / 10) + 1
        : g_game.wave / 10;
    if (tier < 1) tier = 1;
    int hp = (is_mini_boss_wave(g_game.wave) ? 1250 : 5000) * tier;
    if (g_settings.difficulty == DIFF_CADET) hp = (hp * 3) / 4;
    else if (g_settings.difficulty == DIFF_ACE) hp = (hp * 13) / 10;
    return hp;
}

static void spawn_boss(void) {
    memset(&g_game.boss, 0, sizeof(Boss));
    g_game.boss.x = TO_FIXED(SCREEN_WIDTH / 2);
    g_game.boss.y = -TO_FIXED(70);
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
    audio_begin_boss_music();
    // Clear normal drones/asteroids to make room for the boss entrance
    for (int i = 0; i < MAX_DRONES; i++) g_game.drones[i].active = false;
}

static int boss_hit_radius(const Boss* b) {
    return b->mini ? 12 : 16;
}

static void defeat_boss(Boss* b, int boss_cx, int boss_cy) {
    g_game.boss_active = false;
    b->active = false;
    audio_end_boss_music();

    if (g_game.mode == GAME_MODE_STORY) {
        /* Story bosses get a bigger send-off, then hand control to the
         * result card (the campaign banks the reward, not arcade coins). */
        int blasts = (b->story_id == SBOSS_REALITYQUEEN) ? 14 : 8;
        for (int k = 0; k < blasts; k++)
            trigger_explosion(boss_cx + (rand() % 40) - 20, boss_cy + (rand() % 30) - 15);
        g_game.shake_timer = (b->story_id == SBOSS_REALITYQUEEN) ? 60 : 34;
        award_score(2000 * (b->story_id + 1));
        platform_queue_haptic(HAPTIC_BEAM);
        story_finish(1);
        return;
    }

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
    const Player* aim = ai_target_ship();
    int px = FROM_FIXED(aim->x);
    int py = FROM_FIXED(aim->y);
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
static int get_weapon_base_cooldown(WeaponRig rig) {
    (void)rig;
    return 28;
}

static int weapon_projectile_count(WeaponRig rig) {
    static const u8 count[NUM_RIGS] = {
        1, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 9, 9, 11, 0
    };
    int idx = (int)rig;
    return (idx >= 0 && idx < NUM_RIGS) ? count[idx] : 1;
}

static void loadout_from_settings(CoopLoadout* lo) {
    lo->accent_index = g_settings.accent_index;
    lo->laser_index = g_settings.laser_index;
    lo->weapon_rig = g_settings.weapon_rig;
    lo->trail_index = g_settings.trail_index;
    lo->ship_index = g_settings.ship_index;
    memcpy(lo->upgrade_levels, g_settings.upgrade_levels, NUM_UPGRADES);
}

static void fire_fan_for(Player* p, int owner, int count, int damage,
                         bool heavy, bool broad) {
    for (int i = 0; i < count; i++) {
        int centered = i * 2 - (count - 1);
        int xoff = (count == 1) ? 0 : (centered * 9) / (count - 1);
        int vx = centered * (broad ? 34 : 14);
        int vy = -TO_FIXED(heavy ? 8 : 6);
        add_player_bullet(p->x + TO_FIXED(xoff), p->y - TO_FIXED(7), vx, vy,
                          damage, heavy, owner);
    }
}

/* Fire one projectile volley. Infinity Beam bypasses this and is evaluated
 * every simulation tick while A is held. */
static void fire_weapon_for(Player* p, const CoopLoadout* lo, int owner) {
    WeaponRig rig = lo->weapon_rig;
    if (rig == WEAPON_INFINITY) return;
    int cooldown = (get_weapon_base_cooldown(rig) * lo_fire_rate_cooldown_mult(lo)) >> 8;
    if (p->rapid_fire_timer > 0) cooldown = (cooldown * 2) / 5;
    if (cooldown < 4) cooldown = 4;

    int damage = get_weapon_base_damage(rig) + lo_damage_bonus(lo)
               + laser_bonus_for_idx(lo->laser_index);
    int count = weapon_projectile_count(rig);
    bool heavy = rig >= WEAPON_FOCUSED;
    bool broad = (rig == WEAPON_SPREAD || rig == WEAPON_TRIPLE ||
                  rig == WEAPON_NOVA || rig == WEAPON_ARC_HEX ||
                  rig == WEAPON_COMET || rig == WEAPON_STARQUAKE ||
                  rig == WEAPON_PRISM);
    fire_fan_for(p, owner, count, damage, heavy, broad);
    p->fire_cooldown = cooldown;
    audio_play_sfx(SFX_LASER);
    coop_fx_push(owner == 1 ? COOP_FX_FIRE_P2 : COOP_FX_FIRE_P1,
                 FROM_FIXED(p->x), FROM_FIXED(p->y) - 8);
}

static void fire_player_weapon(void) {
    CoopLoadout lo;
    loadout_from_settings(&lo);
    fire_weapon_for(&g_game.player, &lo, 0);
}

void game_init(void) {
    memset(&g_game, 0, sizeof(GameState));
    s_story_waiting_for_start = false;
}

/* The HUD backing card layout depends on the (runtime) screen width, so the
 * cached static layer must be redrawn when the Android viewport changes. */
void game_request_full_redraw(void) {
    s_game_static_valid = false;
}

void game_start(void) {
    memset(&g_game, 0, sizeof(GameState));
    g_game.mode = s_game_mode;
    s_story_waiting_for_start = false;

    g_game.player.x = TO_FIXED(SCREEN_WIDTH / 2);
    g_game.player.y = TO_FIXED(SCREEN_HEIGHT - 24);
    g_game.player.radius = 6;

    // Player 1 visuals always mirror the local settings; player 2's are set
    // by the co-op layer (guest loadout on host, snapshot on guest).
    loadout_from_settings(&g_game.p1_loadout);
    g_game.p2_loadout = g_game.p1_loadout;
    g_game.p2_loadout.accent_index = (g_game.p1_loadout.accent_index + 1) % NUM_ACCENTS;

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

    if (g_game.mode == GAME_MODE_STORY) {
        /* Each kingdom flies over its own sky. Prepare the opening field, but
         * freeze it behind the story card until the player acknowledges it. */
        starfield_set_theme(story_theme_for_level(s_story_level));
        story_begin_level();
        s_story_waiting_for_start = true;
    } else if (g_game.mode == GAME_MODE_WAVES) {
        starfield_set_theme(SF_THEME_ARCADE);
        g_game.intermission_timer = 30;
    } else {
        starfield_set_theme(SF_THEME_ARCADE);
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

/* ── Co-op player 2 simulation (runs ONLY on the host) ───────────────────
 * The host is authoritative for everything.  It simulates the guest ship
 * from the guest's streamed input using the guest's own loadout, so both
 * players see the exact same world and fight with their own gear. */
#ifdef PLATFORM_HOST
static void damage_player2(void) {
    Player* p = &g_game.player2;
    if (p->invulnerable_timer > 0 || p->dead) return;
    int px = FROM_FIXED(p->x);
    int py = FROM_FIXED(p->y);
    platform_queue_haptic(HAPTIC_HIT);

    if (p->shield_charges > 0) {
        p->shield_charges--;
        p->invulnerable_timer = 60;
        trigger_explosion(px, py);
    } else {
        p->lives--;
        p->invulnerable_timer = 90 + get_dash_invuln();
        p->x = TO_FIXED(SCREEN_WIDTH / 2);
        p->y = TO_FIXED(SCREEN_HEIGHT - 20);
        trigger_explosion(px, py);
        if (p->lives <= 0) {
            p->dead = true;
            p->x = TO_FIXED(SCREEN_WIDTH / 2);
            p->y = TO_FIXED(SCREEN_HEIGHT + 60); // hide below screen
            p->beam_active = false;
            p->beam_charge = 0;
            p->primary_beam_active = false;
            p->primary_beam_ramp = 0;
            coop_fx_push(COOP_FX_PLAYER_DIE, px, py);
            /* Guest down: host spectates nothing (they're alive) — the run
             * only ends here if the host ship already fell too. */
            if (g_game.player.dead) finish_run(false); // both down: run over
        }
    }
    if (g_settings.screen_shake) g_game.shake_timer = 18;
}

static void coop_update_player2(void) {
    Player* p = &g_game.player2;
    if (p->dead) return;
    if (p->fire_cooldown > 0) p->fire_cooldown--;
    if (p->invulnerable_timer > 0) p->invulnerable_timer--;
    if (p->rapid_fire_timer > 0) p->rapid_fire_timer--;

    int mx = 0, my = 0;
    if (s_coop_guest_keys & KEY_LEFT) mx -= 1;
    if (s_coop_guest_keys & KEY_RIGHT) mx += 1;
    if (s_coop_guest_keys & KEY_UP) my -= 1;
    if (s_coop_guest_keys & KEY_DOWN) my += 1;

    int eng_mult = lo_engine_mult(&s_coop_p2_lo);
    int base_spd = TO_FIXED(1) + 50;
    base_spd = (base_spd * eng_mult) >> 8;
    int spd = base_spd;
    if (mx != 0 && my != 0) {
        p->x += (mx * spd * 181) / 256;
        p->y += (my * spd * 181) / 256;
    } else {
        p->x += mx * spd;
        p->y += my * spd;
    }
    if (p->x < TO_FIXED(12)) p->x = TO_FIXED(12);
    if (p->x > TO_FIXED(SCREEN_WIDTH - 12)) p->x = TO_FIXED(SCREEN_WIDTH - 12);
    if (p->y < TO_FIXED(22)) p->y = TO_FIXED(22);
    if (p->y > TO_FIXED(SCREEN_HEIGHT - 12)) p->y = TO_FIXED(SCREEN_HEIGHT - 12);

    if (mx != 0 || my != 0) {
        if ((rand() & 1) == 0) {
            u8 col = gfx_get_trail_color_animated(s_coop_p2_lo.trail_index, ((s_game_frame >> 1) + (rand() & 3)) * 4);
            spawn_particle(p->x + (rand() & 1023) - 512, p->y + TO_FIXED(8),
                           (rand() & 255) - 128, (rand() & 127) + 200, col, (rand() & 7) + 6);
        }
    }

    // Big laser for player 2.
    bool beam_held = (s_coop_guest_keys & (KEY_B | KEY_R | KEY_L)) != 0;
    if (!p->beam_active) {
        if (beam_held) {
            if (p->beam_charge < BEAM_CHARGE_TICKS) {
                p->beam_charge++;
                if (p->beam_charge >= BEAM_CHARGE_TICKS) {
                    p->beam_active = true;
                    p->beam_timer = BEAM_DURATION_TICKS;
                    platform_queue_haptic(HAPTIC_BEAM);
                    audio_play_sfx(SFX_LASER);
                    coop_fx_push(COOP_FX_BEAM_P2, FROM_FIXED(p->x), FROM_FIXED(p->y));
                }
            }
        } else if (p->beam_charge > 0) {
            p->beam_charge -= 2;
            if (p->beam_charge < 0) p->beam_charge = 0;
        }
    } else {
        p->beam_timer--;
        if (p->beam_timer <= 0) { p->beam_active = false; p->beam_charge = 0; }
    }

    bool primary_held = (s_coop_guest_keys & KEY_A) != 0;
    if (s_coop_p2_lo.weapon_rig == WEAPON_INFINITY) {
        bool was_active = p->primary_beam_active;
        p->primary_beam_active = primary_held;
        if (primary_held) {
            if (p->primary_beam_ramp < PRIMARY_BEAM_RAMP_TICKS) p->primary_beam_ramp++;
            if (!was_active) {
                audio_play_sfx(SFX_LASER);
                coop_fx_push(COOP_FX_BEAM_P2, FROM_FIXED(p->x), FROM_FIXED(p->y));
            }
        } else p->primary_beam_ramp = 0;
    } else {
        p->primary_beam_active = false;
        p->primary_beam_ramp = 0;
        if (primary_held && p->fire_cooldown == 0) fire_weapon_for(p, &s_coop_p2_lo, 1);
    }

    int p2x = FROM_FIXED(p->x);
    int p2y = FROM_FIXED(p->y);

    // Player 2's charged or Infinity beam chews the shared world.
    if (p->beam_active || p->primary_beam_active) {
        int bx = FROM_FIXED(p->x);
        int beam_dmg = p->primary_beam_active
            ? primary_beam_tick_damage_for(&s_coop_p2_lo, p->primary_beam_ramp)
            : ((lo_beam_damage(&s_coop_p2_lo) << 8) + BEAM_DMG_ROUND) / BEAM_DMG_DIVISOR;
        for (int a = 0; a < MAX_ASTEROIDS; a++) {
            Asteroid* ast = &g_game.asteroids[a];
            if (!ast->active) continue;
            int ax = FROM_FIXED(ast->x);
            int ar = ast->radius;
            if (ax + ar < bx - 6 || ax - ar > bx + 6) continue;
            ast->hp_frac += beam_dmg;
            while (ast->hp_frac >= 256) { ast->hp_frac -= 256; ast->hp--; }
            if (ast->hp <= 0) destroy_asteroid(a, true);
        }
        for (int d = 0; d < MAX_DRONES; d++) {
            Drone* dr = &g_game.drones[d];
            if (!dr->active) continue;
            int dx = FROM_FIXED(dr->x);
            if (dx + 8 < bx - 6 || dx - 8 > bx + 6) continue;
            dr->hp_frac += beam_dmg;
            while (dr->hp_frac >= 256) { dr->hp_frac -= 256; dr->hp--; }
            if (dr->hp <= 0) destroy_drone(d, true);
        }
    }

    // Boss interactions for player 2.
    if (g_game.boss_active && g_game.boss.active) {
        Boss* b = &g_game.boss;
        int boss_cx = FROM_FIXED(b->x);
        int boss_cy = FROM_FIXED(b->y);
        int boss_r = boss_hit_radius(b);

        // Boss body collision.
        if (p->invulnerable_timer == 0) {
            int dsq = (p2x - boss_cx)*(p2x - boss_cx) + (p2y - boss_cy)*(p2y - boss_cy);
            if (dsq <= (6 + boss_r)*(6 + boss_r)) damage_player2();
        }
        // Boss laser beam column.
        if (b->phase == BOSS_BEAM_FIRE) {
            int beam_px = FROM_FIXED(b->beam_x);
            int half_w = 7 + (120 - b->beam_timer) / 20;
            if (half_w > 14) half_w = 14;
            if (p->invulnerable_timer == 0 && p2y > 12) {
                if (p2x >= beam_px - half_w && p2x <= beam_px + half_w) damage_player2();
            }
        }
        // Player 2's beam vs boss.
        if (p->beam_active) {
            if (boss_cx + boss_r >= p2x - 6 && boss_cx - boss_r <= p2x + 6) {
                int denom = BEAM_DURATION_TICKS * 5;
                if (denom < 1) denom = 1;
                int tick = (b->hp_max << 8) / denom;
                if (tick < 1) tick = 1;
                b->hp_frac += tick;
                while (b->hp_frac >= 256) { b->hp_frac -= 256; b->hp--; }
                b->flash_timer = 4;
                if (b->hp <= 0) defeat_boss(b, boss_cx, boss_cy);
            }
        }
        if (p->primary_beam_active) {
            if (boss_cx + boss_r >= p2x - 6 && boss_cx - boss_r <= p2x + 6) {
                b->hp_frac += primary_beam_tick_damage_for(&s_coop_p2_lo, p->primary_beam_ramp);
                while (b->hp_frac >= 256) { b->hp_frac -= 256; b->hp--; }
                b->flash_timer = 4;
                if (b->hp <= 0) defeat_boss(b, boss_cx, boss_cy);
            }
        }
    }

    // Boss bullets vs player 2.
    for (int bb = 0; bb < MAX_BOSS_BULLETS; bb++) {
        if (!g_game.boss_bullets[bb].active) continue;
        int bbx = FROM_FIXED(g_game.boss_bullets[bb].x);
        int bby = FROM_FIXED(g_game.boss_bullets[bb].y);
        int bbr = g_game.boss_bullets[bb].radius;
        if (p->invulnerable_timer == 0) {
            int dsq = (bbx - p2x)*(bbx - p2x) + (bby - p2y)*(bby - p2y);
            if (dsq <= (bbr + 6)*(bbr + 6)) {
                g_game.boss_bullets[bb].active = false;
                damage_player2();
            }
        }
    }

    // Enemy bullets vs player 2 (and powerup pickup by player 2).
    for (int b = 0; b < MAX_BULLETS; b++) {
        if (!g_game.bullets[b].active || !g_game.bullets[b].enemy) continue;
        int bx = FROM_FIXED(g_game.bullets[b].x);
        int by = FROM_FIXED(g_game.bullets[b].y);
        int br = g_game.bullets[b].radius;
        if (p->invulnerable_timer == 0) {
            int dsq = (bx - p2x)*(bx - p2x) + (by - p2y)*(by - p2y);
            if (dsq <= (br + 6)*(br + 6)) {
                g_game.bullets[b].active = false;
                damage_player2();
            }
        }
    }

    // Asteroid / drone impact on player 2.
    if (p->invulnerable_timer == 0) {
        for (int a = 0; a < MAX_ASTEROIDS; a++) {
            if (!g_game.asteroids[a].active) continue;
            int ax = FROM_FIXED(g_game.asteroids[a].x);
            int ay = FROM_FIXED(g_game.asteroids[a].y);
            int ar = g_game.asteroids[a].radius;
            int dsq = (p2x - ax)*(p2x - ax) + (p2y - ay)*(p2y - ay);
            if (dsq <= (5 + ar)*(5 + ar)) {
                destroy_asteroid(a, false);
                damage_player2();
                break;
            }
        }
        if (p->invulnerable_timer == 0) {
            for (int d = 0; d < MAX_DRONES; d++) {
                if (!g_game.drones[d].active) continue;
                int dx = FROM_FIXED(g_game.drones[d].x);
                int dy = FROM_FIXED(g_game.drones[d].y);
                int dsq = (p2x - dx)*(p2x - dx) + (p2y - dy)*(p2y - dy);
                if (dsq <= (5 + 8)*(5 + 8)) {
                    destroy_drone(d, false);
                    damage_player2();
                    break;
                }
            }
        }
    }

    // Powerup pickup by player 2.
    if (p->lives > 0 && !p->dead) {
        int max_shields = get_max_shields();
        int max_lives = get_max_lives() + 1;
        int rapid_duration = lo_rapid_duration(&s_coop_p2_lo);
        for (int pw = 0; pw < MAX_POWERUPS; pw++) {
            if (!g_game.powerups[pw].active) continue;
            int pow_x = FROM_FIXED(g_game.powerups[pw].x);
            int pow_y = FROM_FIXED(g_game.powerups[pw].y);
            int dsq = (p2x - pow_x)*(p2x - pow_x) + (p2y - pow_y)*(p2y - pow_y);
            if (dsq <= (6 + 6)*(6 + 6)) {
                if (g_game.powerups[pw].type == PWR_SHIELD) {
                    if (p->shield_charges < max_shields) p->shield_charges++;
                } else if (g_game.powerups[pw].type == PWR_RAPID) {
                    p->rapid_fire_timer = rapid_duration;
                } else if (g_game.powerups[pw].type == PWR_REPAIR) {
                    if (p->lives < max_lives) p->lives++;
                }
                g_game.powerups[pw].active = false;
                audio_play_sfx(SFX_PICKUP);
                coop_fx_push(COOP_FX_PICKUP, pow_x, pow_y);
            }
        }
    }
}
#endif /* PLATFORM_HOST */

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

    /* A downed co-op pilot spectates: no steering, firing or beam until the
     * run is over. Only reachable while the partner is still alive. */
    const bool p1_spectating = g_game.player.dead;

    int mx = 0, my = 0;
    if (!p1_spectating) {
        if (key_is_down(KEY_LEFT)) mx -= 1;
        if (key_is_down(KEY_RIGHT)) mx += 1;
        if (key_is_down(KEY_UP)) my -= 1;
        if (key_is_down(KEY_DOWN)) my += 1;
    }

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
    bool beam_held = !p1_spectating &&
        (key_is_down(KEY_B) || key_is_down(KEY_R) || key_is_down(KEY_L));
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
                    coop_fx_push(COOP_FX_BEAM_P1, FROM_FIXED(g_game.player.x), FROM_FIXED(g_game.player.y));
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

    bool primary_held = !p1_spectating && key_is_down(KEY_A);
    if (g_settings.weapon_rig == WEAPON_INFINITY) {
        bool was_active = g_game.primary_beam_active;
        g_game.primary_beam_active = primary_held;
        if (primary_held) {
            if (g_game.primary_beam_ramp < PRIMARY_BEAM_RAMP_TICKS) g_game.primary_beam_ramp++;
            if (!was_active) {
                audio_play_sfx(SFX_LASER);
                coop_fx_push(COOP_FX_BEAM_P1, FROM_FIXED(g_game.player.x), FROM_FIXED(g_game.player.y));
            }
        } else g_game.primary_beam_ramp = 0;
    } else {
        g_game.primary_beam_active = false;
        g_game.primary_beam_ramp = 0;
        if (primary_held && g_game.player.fire_cooldown == 0) fire_player_weapon();
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
            int target_x = ai_target_ship()->x + aim_wobble;
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

        if (g_game.mode == GAME_MODE_STORY) {
            /* Story bosses run their own unique scripts. */
            story_boss_ai(b);
        } else if (by < 30) {
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
                        const Player* aim = ai_target_ship();
                        /* Mini-boss skips the dive lunge — same kit, less mean. */
                        int roll = rand() % (b->mini ? 3 : 4);
                        if (roll == 0) { b->phase = BOSS_BURST; b->phase_timer = 90; b->fire_state = 0; }
                        else if (roll == 1) { b->phase = BOSS_BEAM_WIND; b->phase_timer = 60; b->beam_x = aim->x; }
                        else if (roll == 2) { b->phase = BOSS_SWEEP; b->phase_timer = 120; b->sweep_dir = (aim->x < b->x) ? -1 : 1; }
                        else { b->phase = BOSS_DIVE; b->phase_timer = 140; b->aim_x = aim->x; }
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
                    b->beam_x = ai_target_ship()->x;
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
                    int pxi = ai_target_ship()->x;
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
                    dmg = story_boss_absorb(b, dmg, bxp, byp);
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

        // Charged B-beam vs boss: a full 3s dump is ~20% of the bar.
        if (g_game.beam_active) {
            int pbx = FROM_FIXED(g_game.player.x);
            if (boss_cx + boss_r >= pbx - 6 && boss_cx - boss_r <= pbx + 6) {
                int denom = BEAM_DURATION_TICKS * 5;
                if (denom < 1) denom = 1;
                int tick = (b->hp_max << 8) / denom;
                if (tick < 1) tick = 1;
                b->hp_frac += tick;
                while (b->hp_frac >= 256) { b->hp_frac -= 256; b->hp--; }
                b->flash_timer = 4;
                if (b->hp <= 0) defeat_boss(b, boss_cx, boss_cy);
            }
        }
        if (g_game.primary_beam_active) {
            int pbx = FROM_FIXED(g_game.player.x);
            if (boss_cx + boss_r >= pbx - 6 && boss_cx - boss_r <= pbx + 6) {
                b->hp_frac += primary_beam_tick_damage(g_game.primary_beam_ramp);
                while (b->hp_frac >= 256) { b->hp_frac -= 256; b->hp--; }
                b->flash_timer = 4;
                if (b->hp <= 0) defeat_boss(b, boss_cx, boss_cy);
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
                /* Per-owner pierce rules: higher heavy rigs drill through
                 * small/medium rocks; Void and Prism also pierce large ones. */
                bool pierce = false;
                if (g_game.bullets[b].owner == 1) {
                    WeaponRig rig = s_coop_p2_lo.weapon_rig;
                    if (g_game.bullets[b].heavy && lo_is_top_tier(rig))
                        pierce = (g_game.asteroids[a].type != AST_LARGE) || rig >= WEAPON_VOID;
                } else {
                    if (g_game.bullets[b].heavy && is_top_tier_build())
                        pierce = (g_game.asteroids[a].type != AST_LARGE) ||
                                 g_settings.weapon_rig >= WEAPON_VOID;
                }
                if (!pierce) {
                    g_game.bullets[b].active = false;
                    consumed = true;
                }
#else
                bool pierce = g_game.bullets[b].heavy;
                WeaponRig prig = (g_game.bullets[b].owner == 1) ? s_coop_p2_lo.weapon_rig : g_settings.weapon_rig;
                if (!pierce || (g_game.asteroids[a].type == AST_LARGE && prig < WEAPON_VOID)) {
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

    // Charged beam and $1B Infinity primary share the piercing column.
    if (g_game.beam_active || g_game.primary_beam_active) {
        int bx = FROM_FIXED(g_game.player.x);
        int beam_dmg = g_game.primary_beam_active
            ? primary_beam_tick_damage(g_game.primary_beam_ramp)
            : BEAM_TICK_DAMAGE();
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
            coop_fx_push(COOP_FX_PICKUP, pow_x, pow_y);
        }
    }

#ifdef PLATFORM_HOST
    /* Host-authoritative second ship: simulate the guest in the same world
     * after player 1's interactions so collisions stay consistent. */
    if (s_coop_guest_active) {
        coop_update_player2();
    }
#endif

    if (g_game.mode == GAME_MODE_STORY) {
        story_update_objective();
    } else if (g_game.mode == GAME_MODE_ENDLESS || g_game.mode == GAME_MODE_OVERDRIVE) {
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
    /* Keep this guard in the game core as well as the menu. Tests, future UI
     * paths, and JNI callers cannot accidentally advance a story level while
     * its tap-to-continue card is still on screen. */
    if (game_story_waiting_for_start()) return;
#ifdef PLATFORM_HOST
    if (s_coop_render_only) {
        /* Guest side: no local simulation — just advance the smooth render
         * between host snapshots. */
        game_coop_advance_render();
        return;
    }
#endif
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
                /* Each player's shots render in that player's own crystal. */
                int laser = (g_game.bullets[i].owner == 1)
                          ? g_game.p2_loadout.laser_index
                          : g_game.p1_loadout.laser_index;
                gfx_draw_laser(bx, by, g_game.bullets[i].heavy,
                               laser, s_game_frame, false);
            }
        }
    }

    // Big laser: faint aim line while charging, then a full-height beam
    // that reaches the top of the screen (and the bottom).
    int beam_bx = FROM_FIXED(g_game.player.x) + ox;
    if (g_game.beam_active || g_game.primary_beam_active) {
        u8 beam_col = gfx_get_laser_color(g_game.p1_loadout.laser_index);
        int bw = g_game.beam_active ? (((s_game_frame & 3) == 0) ? 10 : 8)
                                    : (((s_game_frame & 3) == 0) ? 8 : 6);
        gfx_fill_rect(beam_bx - bw / 2, 0, bw, SCREEN_HEIGHT, beam_col);
        gfx_fill_rect(beam_bx - 1, 0, 2, SCREEN_HEIGHT, PAL_TEXT_WHITE);
    } else if (g_game.beam_charge > 0) {
        gfx_fill_rect(beam_bx - 1, 0, 2, SCREEN_HEIGHT, 15);
    }

    /* The host's complete rainbow set controls world cosmetics. p1_loadout is
     * part of every snapshot, so guests render the exact same rainbow rocks. */
    bool rainbow_asteroids =
        g_game.p1_loadout.accent_index == ACCENT_RAINBOW_IDX &&
        g_game.p1_loadout.trail_index == TRAIL_RAINBOW_IDX &&
        g_game.p1_loadout.laser_index == LASER_RAINBOW_IDX;
    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        if (g_game.asteroids[i].active) {
            int ax = FROM_FIXED(g_game.asteroids[i].x) + ox;
            int ay = FROM_FIXED(g_game.asteroids[i].y) + oy;
            const u8* sprite;
            int size, radius;
            if (g_game.asteroids[i].type == AST_LARGE) {
                sprite = spr_ast_large; size = 24; radius = 12;
            } else if (g_game.asteroids[i].type == AST_MED_A) {
                sprite = spr_ast_med_a; size = 16; radius = 8;
            } else if (g_game.asteroids[i].type == AST_MED_B) {
                sprite = spr_ast_med_b; size = 16; radius = 8;
            } else if (g_game.asteroids[i].type == AST_SMALL) {
                sprite = spr_ast_small; size = 10; radius = 5;
            } else {
                sprite = spr_ast_tiny; size = 6; radius = 3;
            }
            if (rainbow_asteroids)
                gfx_draw_sprite_rainbow(ax - radius, ay - radius, size, size, sprite, s_game_frame);
            else
                gfx_draw_sprite(ax - radius, ay - radius, size, size, sprite);
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
            gfx_draw_enemy_ship(dx, dy, g_game.drones[i].accent, g_game.drones[i].style);
        }
    }

    if (!g_game.is_game_over && !g_game.player.dead) {
        bool visible = true;
        if (g_game.player.invulnerable_timer > 0) {
            visible = (g_game.player.invulnerable_timer & 2) != 0;
        }
        if (visible) {
            int px = FROM_FIXED(g_game.player.x) - 10 + ox;
            int py = FROM_FIXED(g_game.player.y) - 8 + oy;
            int accent = g_game.p1_loadout.accent_index;
            if (accent < 0 || accent >= NUM_ACCENTS) accent = 1;
            gfx_draw_ship_styled(px, py, accent, s_game_frame, g_game.p1_loadout.ship_index);
            if (g_game.player.shield_charges > 0) {
                gfx_draw_sprite(px - 2, py - 4, 24, 24, spr_shield_bubble);
            }
        }
    }

    // Player 2 (co-op). Drawn whenever a second ship is live: the host draws
    // the guest ship, and the guest draws itself (its own ship is player 2).
    if (s_coop_guest_active || s_coop_render_only) {
        bool vis2 = true;
        if (g_game.player2.invulnerable_timer > 0) {
            vis2 = (g_game.player2.invulnerable_timer & 2) != 0;
        }
        if (vis2 && !g_game.player2.dead && !g_game.is_game_over) {
            int p2x = FROM_FIXED(g_game.player2.x) - 10 + ox;
            int p2y = FROM_FIXED(g_game.player2.y) - 8 + oy;
            int accent2 = g_game.p2_loadout.accent_index;
            if (accent2 < 0 || accent2 >= NUM_ACCENTS) accent2 = 1;
            gfx_draw_ship_styled(p2x, p2y, accent2, s_game_frame + 1, g_game.p2_loadout.ship_index);
            if (g_game.player2.shield_charges > 0) {
                gfx_draw_sprite(p2x - 2, p2y - 4, 24, 24, spr_shield_bubble);
            }
        }
        // Player 2's charged or Infinity beam column.
        if (g_game.player2.beam_active || g_game.player2.primary_beam_active) {
            int b2x = FROM_FIXED(g_game.player2.x) + ox;
            u8 bc2 = gfx_get_laser_color(g_game.p2_loadout.laser_index);
            int bw2 = g_game.player2.beam_active ? (((s_game_frame & 3) == 0) ? 10 : 8)
                                                : (((s_game_frame & 3) == 0) ? 8 : 6);
            gfx_fill_rect(b2x - bw2 / 2, 0, bw2, SCREEN_HEIGHT, bc2);
            gfx_fill_rect(b2x - 1, 0, 2, SCREEN_HEIGHT, PAL_TEXT_WHITE);
        } else if (g_game.player2.beam_charge > 0) {
            int b2x = FROM_FIXED(g_game.player2.x) + ox;
            gfx_fill_rect(b2x - 1, 0, 2, SCREEN_HEIGHT, 15);
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
    if (g_game.mode == GAME_MODE_STORY) {
        siprintf(buf, "LV%02d", s_story_level);
    } else {
        siprintf(buf, "W%02d", g_game.wave);
    }
    gfx_draw_text_centered(wave_x, 4, 52, buf, PAL_TEXT_CYAN);

    int right_card_x = SCREEN_WIDTH - 75;
    if (g_game.mode == GAME_MODE_STORY) {
        /* The campaign never pays arcade coins, so a "$0" up here was just
         * a lie. Story flies on CHUBBCOIN: show that balance instead. */
        char cbuf[24];
        siprintf(cbuf, "%u CHUBBCOIN", (unsigned int)story_chubbcoin());
        int cw = (int)strlen(cbuf) * 6;
        gfx_draw_text(right_card_x + 72 - 3 - cw, 4, cbuf, PAL_TEXT_GOLD);
    } else {
        // Right-aligned inside the card so long balances (e.g. cheat money) fit.
        save_format_coins(buf, sizeof(buf));
        int coin_px = (int)strlen(buf) * 6 + 6; // digits + "$"
        int coin_x = right_card_x + 72 - 3 - coin_px;
        gfx_draw_char(coin_x, 4, '$', PAL_TEXT_GOLD);
        gfx_draw_text(coin_x + 6, 4, buf, PAL_TEXT_GOLD);
    }

    // The HUD reflects the LOCAL player: player 1 on the host, player 2 on
    // the guest (which renders itself as player 2 from the host snapshot).
    const Player* hud_player = s_coop_render_only ? &g_game.player2 : &g_game.player;
    for (int i = 0; i < hud_player->lives && i < 7; i++) {
        gfx_draw_char(right_card_x + 3 + i * 6, 11, '^', PAL_TEXT_GREEN);
    }
    for (int i = 0; i < hud_player->shield_charges && i < 6; i++) {
        gfx_draw_char(right_card_x + 39 + i * 6, 11, '*', PAL_TEXT_CYAN);
    }

    /* Story objective readout: always tells you exactly what ends the level. */
    if (g_game.mode == GAME_MODE_STORY && !g_game.is_game_over) {
        const StoryLevel* L = story_cur();
        char obuf[40];
        switch (L->objective) {
            case OBJ_HUNT:
                siprintf(obuf, "HUNTERS %d/%d", s_story_kills, (int)L->quota);
                break;
            case OBJ_SURVIVE:
                siprintf(obuf, "SURVIVE %ds", (s_story_timer + 89) / 90);
                break;
            case OBJ_BIGGAME:
                siprintf(obuf, "BIG ROCKS %d/%d", s_story_bigs, (int)L->quota);
                break;
            case OBJ_TIMED: {
                int left = count_medium_equivalents() + s_story_pending_med
                         + count_active_drones();
                siprintf(obuf, "%d LEFT  %ds", left, (s_story_timer + 89) / 90);
                break;
            }
            case OBJ_BOSS:
                siprintf(obuf, "%s", story_boss_name(story_boss_for_level(s_story_level)));
                break;
            default: {
                /* Counted in MEDIUM rocks: a big rock is the two mediums it
                 * will break into, and the small/tiny debris those shed is
                 * not counted (it is still out there, it just is not the
                 * objective). Five bigs read 10; popping one keeps it at 10
                 * until a medium actually dies. */
                int left = count_medium_equivalents() + s_story_pending_med
                         + count_active_drones();
                siprintf(obuf, "CLEAR %d LEFT", left);
                break;
            }
        }
        gfx_draw_text_centered((SCREEN_WIDTH - 120) / 2, 12, 120, obuf, PAL_TEXT_GOLD);
    }

    if (g_game.combo > 1) {
        siprintf(buf, "x%d", g_game.combo);
        u8 acc = (g_game.combo >= 15) ? PAL_TEXT_GOLD : gfx_get_accent_color(g_settings.accent_index);
        gfx_draw_text(6, 20, buf, acc);
        int max_comb_t = get_max_combo_timer();
        gfx_draw_progress_bar(20, 22, 42, 4, g_game.combo_timer, max_comb_t, acc, 18);
    }

    if (hud_player->rapid_fire_timer > 0) {
        siprintf(buf, "RAPID %d", (hud_player->rapid_fire_timer + 59) / 60);
        gfx_draw_text_centered((SCREEN_WIDTH - 80) / 2, 20, 80, buf, PAL_TEXT_GOLD);
    }

    // Big laser status: charge-up fills while holding, then drains as it fires
    const int hud_beam_active = s_coop_render_only ? (g_game.player2.beam_active ? 1 : 0) : (g_game.beam_active ? 1 : 0);
    const int hud_beam_timer  = s_coop_render_only ? g_game.player2.beam_timer : g_game.beam_timer;
    const int hud_beam_charge = s_coop_render_only ? g_game.player2.beam_charge : g_game.beam_charge;
    if (hud_beam_active) {
        gfx_draw_text(SCREEN_WIDTH - 82, SCREEN_HEIGHT - 10, "FIRING", PAL_TEXT_GOLD);
        gfx_draw_progress_bar(SCREEN_WIDTH - 54, SCREEN_HEIGHT - 9, 50, 5,
                              hud_beam_timer, BEAM_DURATION_TICKS, PAL_TEXT_GOLD, 18);
    } else {
        u8 beam_col = gfx_get_accent_color(g_settings.accent_index);
        if (hud_beam_charge >= BEAM_CHARGE_TICKS) beam_col = PAL_TEXT_GREEN;
        gfx_draw_progress_bar(SCREEN_WIDTH - 54, SCREEN_HEIGHT - 9, 50, 5,
                              hud_beam_charge, BEAM_CHARGE_TICKS, beam_col, 18);
    }

#ifdef PLATFORM_HOST
    // ── Co-op spectate / partner HUD ──────────────────────────────────
    // "me"/"them" flip on the guest: it renders itself as player 2.
    if (s_coop_guest_active || s_coop_render_only) {
        const Player* me_p   = s_coop_render_only ? &g_game.player2 : &g_game.player;
        const Player* them_p = s_coop_render_only ? &g_game.player  : &g_game.player2;
        if (me_p->dead && !them_p->dead && !g_game.is_game_over) {
            int bw = 150;
            int bx2 = (SCREEN_WIDTH - bw) / 2;
            gfx_draw_glass_card(bx2, 100, bw, 24, PAL_TEXT_GOLD, 15);
            gfx_draw_text_centered(bx2, 103, bw, "YOU ARE DOWN", PAL_TEXT_RED);
            gfx_draw_text_centered(bx2, 112, bw, "SPECTATING PARTNER", PAL_TEXT_GOLD);
        }
        // Partner mini-status (top-center, under the wave banner area).
        if (!g_game.is_game_over) {
            const char* tag = s_coop_render_only ? "P1" : "P2";
            gfx_draw_text((SCREEN_WIDTH - 52) / 2 + 4, 13, tag, 17);
            if (them_p->dead) {
                gfx_draw_text((SCREEN_WIDTH - 52) / 2 + 18, 13, "DOWN", PAL_TEXT_RED);
            } else {
                for (int li = 0; li < them_p->lives && li < 7; li++) {
                    gfx_draw_char((SCREEN_WIDTH - 52) / 2 + 18 + li * 5, 13, '^', PAL_TEXT_GREEN);
                }
            }
        }
    }
#endif

    if (game_story_waiting_for_start()) {
        const StoryLevel* L = story_cur();
        int boss_id = story_boss_for_level(s_story_level);
        /* The level opens on its own two lines of story, so each of the 70
         * levels reads as a beat in Jack's run rather than a numbered slot. */
        int banner_w = 228;
        if (banner_w > SCREEN_WIDTH - 8) banner_w = SCREEN_WIDTH - 8;
        int bx = (SCREEN_WIDTH - banner_w) / 2;
        gfx_draw_glass_card(bx, 54, banner_w, 68, PAL_TEXT_WHITE, 15);
        if (boss_id >= 0) {
            siprintf(buf, "LEVEL %d  -  %s", s_story_level, story_sector_name(story_sector_of(s_story_level)));
            gfx_draw_text_centered(bx, 60, banner_w, buf, 17);
            gfx_draw_text_centered(bx, 70, banner_w, story_boss_name(boss_id), PAL_TEXT_RED);
            gfx_draw_text_centered(bx, 82, banner_w, L->brief1, PAL_TEXT_WHITE);
            gfx_draw_text_centered(bx, 92, banner_w, L->brief2, PAL_TEXT_WHITE);
            gfx_draw_text_centered(bx, 102, banner_w, story_boss_taunt(boss_id), PAL_TEXT_GOLD);
        } else {
            siprintf(buf, "LEVEL %d  -  %s", s_story_level, story_sector_name(story_sector_of(s_story_level)));
            gfx_draw_text_centered(bx, 60, banner_w, buf, 17);
            gfx_draw_text_centered(bx, 70, banner_w, L->name, PAL_TEXT_CYAN);
            gfx_draw_text_centered(bx, 82, banner_w, L->brief1, PAL_TEXT_WHITE);
            gfx_draw_text_centered(bx, 91, banner_w, L->brief2, PAL_TEXT_WHITE);
            /* Announce the twist, so you know what kind of level this is
             * before the first rock reaches you. */
            {
                const char* objn;
                switch (L->objective) {
                    case OBJ_HUNT:    objn = "HUNT THE FIGHTERS"; break;
                    case OBJ_SURVIVE: objn = "SURVIVE THE FIELD"; break;
                    case OBJ_BIGGAME: objn = "CRACK THE BIG ONES"; break;
                    case OBJ_TIMED:   objn = "CLEAR IT BEFORE THE CLOCK"; break;
                    default:          objn = "CLEAR EVERYTHING"; break;
                }
                const char* modn = story_modifier_name(L->modifier);
                if (modn[0]) siprintf(buf, "%s  -  %s", objn, modn);
                else         siprintf(buf, "%s", objn);
                gfx_draw_text_centered(bx, 102, banner_w, buf, PAL_TEXT_GOLD);
            }
        }
        gfx_draw_text_centered(bx, 112, banner_w, "TAP TO CONTINUE", PAL_TEXT_GREEN);
    } else if (g_game.wave_banner_timer > 0) {
        bool first_boss = g_game.mode == GAME_MODE_WAVES && g_game.wave == 5;
        int banner_w = first_boss ? 180 : 120;
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
        } else if (first_boss) {
            gfx_draw_text_centered(bx, by + 4, banner_w, "MINI BOSS", PAL_TEXT_GOLD);
            gfx_draw_text_centered(bx, by + 13, banner_w, "THINK YOU CAN HANDLE THIS", PAL_TEXT_CYAN);
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

        /* Story boss extras: the Twins' second hull and the Warden/Titan
         * nodes are real, shootable parts, so they must be drawn. */
        if (g_game.mode == GAME_MODE_STORY) {
            const Boss* sb = &g_game.boss;
            if (sb->story_id == SBOSS_TWINS && sb->clone_active) {
                int qx = FROM_FIXED(sb->clone_x) + ox;
                int qy = FROM_FIXED(sb->clone_y) + oy;
                gfx_draw_boss_drone(qx, qy, true, sb->flash_timer > 0, s_game_frame);
            }
            if (sb->story_id == SBOSS_VAULTWARDEN || sb->story_id == SBOSS_SCRAPTITAN) {
                for (int i = 0; i < 4; i++) {
                    if (sb->node_hp[i] <= 0) continue;
                    int ang = sb->spin + i * 16384;
                    int nx = bxi + ((lu_cos(ang) * 30) >> 12);
                    int ny = byi + ((lu_sin(ang) * 22) >> 12);
                    u8 col = (sb->story_id == SBOSS_VAULTWARDEN) ? PAL_TEXT_CYAN : PAL_TEXT_GOLD;
                    gfx_fill_rect(nx - 4, ny - 4, 8, 8, col);
                    gfx_fill_rect(nx - 2, ny - 2, 4, 4, PAL_TEXT_WHITE);
                }
            }
        }

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
        if (g_game.mode == GAME_MODE_STORY) {
            int sid = g_game.boss.story_id;
            gfx_draw_text_centered(0, bar_y - 8, SCREEN_WIDTH, story_boss_name(sid), PAL_TEXT_RED);
            /* Vault Warden: show how many turret nodes are still sealing the
             * hull, so the player understands why damage is bouncing off. */
            if (sid == SBOSS_VAULTWARDEN) {
                int alive = 0;
                for (int i = 0; i < 4; i++) if (g_game.boss.node_hp[i] > 0) alive++;
                if (alive > 0) {
                    char nbuf[28];
                    siprintf(nbuf, "SHIELDED - %d NODES", alive);
                    gfx_draw_text_centered(0, bar_y + 7, SCREEN_WIDTH, nbuf, PAL_TEXT_CYAN);
                }
            } else if (sid == SBOSS_SCRAPTITAN) {
                int plates = 0;
                for (int i = 0; i < 4; i++) if (g_game.boss.node_hp[i] > 0) plates++;
                if (plates > 0) {
                    char nbuf[28];
                    siprintf(nbuf, "ARMOUR %d/4", plates);
                    gfx_draw_text_centered(0, bar_y + 7, SCREEN_WIDTH, nbuf, PAL_TEXT_CYAN);
                }
            }
        } else {
            gfx_draw_text_centered(0, bar_y - 8, SCREEN_WIDTH,
                                  g_game.boss.mini ? "MINI BOSS" : "BOSS",
                                  g_game.boss.mini ? PAL_TEXT_GOLD : PAL_TEXT_RED);
        }
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


/* ════════════════════════════════════════════════════════════════════════
 * Co-op (host-authoritative) network-facing API.
 *
 * Wire snapshot layout (little-endian, both ends are Android ARM/x86_64):
 *   u16 magic 0xC0A3 | u16 seq | u8 flags | u32 score
 *   u32 coins_lo | u32 coins_hi (host balance, for guest progression sync)
 *   u16 wave | u8 mode | u8 combo | u16 wave_banner | u16 intermission
 *   u16 overdrive | u16 spawn | s8 shake_x | s8 shake_y | u8 shake_timer
 *   [player1 block] [player2 block] (v4 includes Infinity active + ramp)
 *   u8 nAsteroids + n*(x,y,vx,vy,type,hp)
 *   u8 nBullets  + n*(x,y,vx,vy,damage,life,flags)
 *   u8 nDrones   + n*(x,y,vx,vy,hp,phase)
 *   u8 bossActive + boss fields (if active)
 *   u8 nBossBullets + n*(x,y,vx,vy,heavy)
 *   u8 nPowerups + n*(x,y,vy,type)
 *   u8 nExplosions + n*(x,y,frame)              (v3: synced boom anims)
 *   u8 nFxEvents  + n*(type,x,y)                (v3: effect sync)
 *
 * A snapshot can exceed one EOS packet (1170 bytes), so coop.c fragments it
 * over reliable-ordered packets and reassembles before calling game_coop_apply.
 * ════════════════════════════════════════════════════════════════════════ */
#define COOP_SNAPSHOT_MAGIC 0xC0A3

/* ── Guest progression sync ─────────────────────────────────────────────
 * The host is authoritative for the world AND the run's earnings: every
 * kill pays coins into the host's balance (award_coins -> save_write).  The
 * guest never simulates, so without a bridge it would earn nothing while
 * playing co-op.  We ship the host balance in every snapshot; the guest
 * adds only the POSITIVE DELTA between snapshots to its own balance and
 * banks its new best score when the run ends.  The first snapshot after a
 * GAME_START only establishes a baseline — the host's pre-join fortune is
 * not transferred. */
static u64 s_host_coins_prev = 0;
static int s_coins_baselined = 0;

#ifdef PLATFORM_HOST
static void wr8(u8** p, u8 v) { **p = v; *p += 1; }
static void wr16(u8** p, u16 v) { memcpy(*p, &v, 2); *p += 2; }
static void wr32(u8** p, u32 v) { memcpy(*p, &v, 4); *p += 4; }
static u8 rd8(const u8** p) { u8 v = **p; *p += 1; return v; }
static u16 rd16(const u8** p) { u16 v; memcpy(&v, *p, 2); *p += 2; return v; }
static u32 rd32(const u8** p) { u32 v; memcpy(&v, *p, 4); *p += 4; return v; }
static int rd16s(const u8** p) { return (s16)rd16(p); }

static void ser_player(u8** p, const Player* pl, const CoopLoadout* lo, u8 live,
                       int beam_charge, int beam_timer, int beam_active,
                       int primary_ramp, int primary_active) {
    wr16(p, (u16)pl->x);
    wr16(p, (u16)pl->y);
    wr8(p, (u8)pl->lives);
    wr8(p, (u8)pl->shield_charges);
    wr8(p, (u8)pl->invulnerable_timer);
    wr8(p, (u8)pl->rapid_fire_timer);
    wr8(p, (u8)(beam_active ? 1 : 0));
    wr8(p, (u8)(pl->dead ? 1 : 0));
    wr16(p, (u16)beam_charge);
    wr16(p, (u16)beam_timer);
    wr8(p, (u8)(primary_active ? 1 : 0));
    wr8(p, (u8)primary_ramp);
    wr8(p, (u8)lo->accent_index);
    wr8(p, (u8)lo->laser_index);
    wr8(p, (u8)lo->weapon_rig);
    wr8(p, (u8)lo->trail_index);
    wr8(p, (u8)lo->ship_index);
    wr8(p, live);
}

static void deser_player(const u8** p, Player* pl, CoopLoadout* lo,
                         int* beam_charge, int* beam_timer, bool* beam_active,
                         int* primary_ramp, bool* primary_active) {
    pl->x = (int)rd16(p);
    pl->y = (int)rd16(p);
    pl->lives = (int)rd8(p);
    pl->shield_charges = (int)rd8(p);
    pl->invulnerable_timer = (int)rd8(p);
    pl->rapid_fire_timer = (int)rd8(p);
    *beam_active = rd8(p) ? true : false;
    pl->dead = rd8(p) ? true : false;
    *beam_charge = (int)rd16(p);
    *beam_timer = (int)rd16(p);
    *primary_active = rd8(p) ? true : false;
    *primary_ramp = (int)rd8(p);
    lo->accent_index = (int)rd8(p);
    lo->laser_index = (int)rd8(p);
    lo->weapon_rig = (WeaponRig)rd8(p);
    if (lo->laser_index < 0 || lo->laser_index >= NUM_LASERS) lo->laser_index = 0;
    if (lo->weapon_rig < 0 || lo->weapon_rig >= NUM_RIGS) lo->weapon_rig = WEAPON_SINGLE;
    lo->trail_index = (int)rd8(p);
    lo->ship_index = (int)rd8(p);
    if (lo->ship_index < 0 || lo->ship_index >= NUM_SHIP_STYLES) lo->ship_index = 0;
    rd8(p); // live flag
    pl->radius = 6;
}

static int game_coop_serialize_into(u8* buf, int cap) {
    u8* p = buf;
    u8* start = buf;
    if (cap < 2600) return 0;
    wr16(&p, COOP_SNAPSHOT_MAGIC);
    wr16(&p, s_coop_snapshot_seq++);
    u8 flags = 0;
    if (g_game.is_game_over) flags |= 1;
    if (g_game.boss_active) flags |= 2;
    if (g_game.is_new_high_score) flags |= 4;
    if (g_game.time_up) flags |= 8;
    wr8(&p, flags);
    wr32(&p, g_game.score);
    /* Host coin balance (u64 split into two u32s for wire safety). */
    wr32(&p, (u32)(g_settings.coins & 0xFFFFFFFFu));
    wr32(&p, (u32)((g_settings.coins >> 32) & 0xFFFFFFFFu));
    wr16(&p, (u16)g_game.wave);
    wr8(&p, (u8)g_game.mode);
    wr8(&p, (u8)g_game.combo);
    wr16(&p, (u16)g_game.wave_banner_timer);
    wr16(&p, (u16)g_game.intermission_timer);
    wr16(&p, (u16)g_game.overdrive_timer);
    wr16(&p, (u16)g_game.spawn_timer);
    wr8(&p, (u8)(s8)g_game.shake_x);
    wr8(&p, (u8)(s8)g_game.shake_y);
    wr8(&p, (u8)g_game.shake_timer);

    ser_player(&p, &g_game.player, &g_game.p1_loadout, 1,
               g_game.beam_charge, g_game.beam_timer, g_game.beam_active,
               g_game.primary_beam_ramp, g_game.primary_beam_active);
    ser_player(&p, &g_game.player2, &g_game.p2_loadout, s_coop_guest_active ? 1 : 0,
               g_game.player2.beam_charge, g_game.player2.beam_timer, g_game.player2.beam_active,
               g_game.player2.primary_beam_ramp, g_game.player2.primary_beam_active);

    int ac = 0;
    for (int i = 0; i < MAX_ASTEROIDS; i++) if (g_game.asteroids[i].active) ac++;
    wr8(&p, (u8)ac);
    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        if (!g_game.asteroids[i].active) continue;
        wr16(&p, (u16)g_game.asteroids[i].x);
        wr16(&p, (u16)g_game.asteroids[i].y);
        wr16(&p, (u16)(s16)g_game.asteroids[i].vx);
        wr16(&p, (u16)(s16)g_game.asteroids[i].vy);
        wr8(&p, (u8)g_game.asteroids[i].type);
        wr8(&p, (u8)g_game.asteroids[i].hp);
    }

    int bc = 0;
    for (int i = 0; i < MAX_BULLETS; i++) if (g_game.bullets[i].active) bc++;
    wr8(&p, (u8)bc);
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!g_game.bullets[i].active) continue;
        wr16(&p, (u16)g_game.bullets[i].x);
        wr16(&p, (u16)g_game.bullets[i].y);
        wr16(&p, (u16)(s16)g_game.bullets[i].vx);
        wr16(&p, (u16)(s16)g_game.bullets[i].vy);
        wr8(&p, (u8)g_game.bullets[i].damage);
        wr8(&p, (u8)g_game.bullets[i].life);
        u8 bf = (u8)((g_game.bullets[i].enemy ? 1 : 0) |
                      (g_game.bullets[i].heavy ? 2 : 0) |
                      ((g_game.bullets[i].owner & 3) << 2));
        wr8(&p, bf);
    }

    int dc = 0;
    for (int i = 0; i < MAX_DRONES; i++) if (g_game.drones[i].active) dc++;
    wr8(&p, (u8)dc);
    for (int i = 0; i < MAX_DRONES; i++) {
        if (!g_game.drones[i].active) continue;
        wr16(&p, (u16)g_game.drones[i].x);
        wr16(&p, (u16)g_game.drones[i].y);
        wr16(&p, (u16)(s16)g_game.drones[i].vx);
        wr16(&p, (u16)(s16)g_game.drones[i].vy);
        wr8(&p, (u8)g_game.drones[i].hp);
        wr8(&p, (u8)g_game.drones[i].phase);
    }

    int boss_on = (g_game.boss_active && g_game.boss.active) ? 1 : 0;
    wr8(&p, (u8)boss_on);
    if (boss_on) {
        wr8(&p, g_game.boss.mini ? 1 : 0);
        wr16(&p, (u16)g_game.boss.x);
        wr16(&p, (u16)g_game.boss.y);
        wr16(&p, (u16)(s16)g_game.boss.vx);
        wr16(&p, (u16)(s16)g_game.boss.vy);
        wr16(&p, (u16)g_game.boss.hp);
        wr16(&p, (u16)g_game.boss.hp_max);
        wr8(&p, (u8)g_game.boss.phase);
        wr16(&p, (u16)g_game.boss.beam_x);
        wr16(&p, (u16)g_game.boss.beam_timer);
        wr8(&p, (u8)g_game.boss.flash_timer);
        wr16(&p, (u16)g_game.boss.phase_timer);
        wr16(&p, (u16)g_game.boss.aim_x);
    }

    int bbc = 0;
    for (int i = 0; i < MAX_BOSS_BULLETS; i++) if (g_game.boss_bullets[i].active) bbc++;
    wr8(&p, (u8)bbc);
    for (int i = 0; i < MAX_BOSS_BULLETS; i++) {
        if (!g_game.boss_bullets[i].active) continue;
        wr16(&p, (u16)g_game.boss_bullets[i].x);
        wr16(&p, (u16)g_game.boss_bullets[i].y);
        wr16(&p, (u16)(s16)g_game.boss_bullets[i].vx);
        wr16(&p, (u16)(s16)g_game.boss_bullets[i].vy);
        wr8(&p, g_game.boss_bullets[i].heavy ? 1 : 0);
    }

    int pwc = 0;
    for (int i = 0; i < MAX_POWERUPS; i++) if (g_game.powerups[i].active) pwc++;
    wr8(&p, (u8)pwc);
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!g_game.powerups[i].active) continue;
        wr16(&p, (u16)g_game.powerups[i].x);
        wr16(&p, (u16)g_game.powerups[i].y);
        wr16(&p, (u16)(s16)g_game.powerups[i].vy);
        wr8(&p, (u8)g_game.powerups[i].type);
    }

    /* ── v3 tail: explosion animations-in-flight so booms line up. ──── */
    int xc = 0;
    for (int i = 0; i < MAX_EXPLOSIONS; i++) if (g_game.explosions[i].active) xc++;
    wr8(&p, (u8)xc);
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!g_game.explosions[i].active) continue;
        wr16(&p, (u16)(s16)g_game.explosions[i].x);
        wr16(&p, (u16)(s16)g_game.explosions[i].y);
        wr8(&p, (u8)(g_game.explosions[i].frame & 0xF));
    }

    /* ── v3 tail: one-shot FX events accumulated since the last snapshot ── */
    wr8(&p, (u8)s_fx_count);
    for (int i = 0; i < s_fx_count; i++) {
        int idx = (s_fx_head - s_fx_count + i + COOP_FX_MAX) % COOP_FX_MAX;
        wr8(&p, s_fx_ring[idx].type);
        wr16(&p, (u16)s_fx_ring[idx].x);
        wr16(&p, (u16)s_fx_ring[idx].y);
    }
    s_fx_count = 0; // drained
    s_fx_head = 0;

    return (int)(p - start);
}

/* ── Guest-side FX replay (effect sync) ────────────────────────────────
 * The guest runs no simulation, so these helpers spawn the pretty parts of
 * a host event locally: explosion animation, particles and SFX, without the
 * host-only bits (no shake/haptics, no game-state damage). */
static void guest_spawn_explosion(int x, int y) {
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
    for (int pc = 0; pc < 10; pc++) {
        int angle = pc * 25;
        int spd = (rand() & 127) + 80;
        int vx = (lu_cos(angle * 256) * spd) >> 12;
        int vy = (lu_sin(angle * 256) * spd) >> 12;
        u8 col = 128 + (rand() & 31);
        spawn_particle(TO_FIXED(x), TO_FIXED(y), vx, vy, col, (rand() & 15) + 12);
    }
    audio_play_sfx(SFX_EXPLOSION);
}

static void guest_replay_fx(u8 type, int x, int y) {
    switch (type) {
        case COOP_FX_EXPLOSION:
            guest_spawn_explosion(x, y);
            break;
        case COOP_FX_PLAYER_DIE:
            /* A ship went down: extra-big boom so it reads across the map. */
            guest_spawn_explosion(x, y);
            guest_spawn_explosion(x + 6, y - 4);
            guest_spawn_explosion(x - 6, y + 4);
            break;
        case COOP_FX_FIRE_P1:
            /* Host ship firing heard+seen on the guest (vice-versa sync).
             * FIRE_P2 is skipped: the guest already mirrors its OWN fire SFX
             * from the local cadence loop for zero-latency feedback. */
            audio_play_sfx(SFX_LASER);
            for (int pc = 0; pc < 2; pc++) {
                u8 col = gfx_get_laser_color(g_game.p1_loadout.laser_index);
                spawn_particle(TO_FIXED(x + (rand() & 7) - 3), TO_FIXED(y + (rand() & 3)),
                               (rand() & 63) - 32, -((rand() & 63) + 60), col, 6);
            }
            break;
        case COOP_FX_BEAM_P1:
        case COOP_FX_BEAM_P2:
            /* Charge-complete flash: the beam column itself comes from the
             * synced beam_active flag; this sells the moment it engages. */
            audio_play_sfx(SFX_LASER);
            {
                u8 col = gfx_get_laser_color(
                    (type == COOP_FX_BEAM_P1) ? g_game.p1_loadout.laser_index
                                              : g_game.p2_loadout.laser_index);
                for (int pc = 0; pc < 6; pc++) {
                    spawn_particle(TO_FIXED(x + (rand() & 11) - 5), TO_FIXED(y + (rand() & 5) - 2),
                                   (rand() & 127) - 64, (rand() & 127) - 64, col, 10);
                }
            }
            break;
        case COOP_FX_PICKUP:
            audio_play_sfx(SFX_PICKUP);
            for (int pc = 0; pc < 5; pc++) {
                spawn_particle(TO_FIXED(x + (rand() & 9) - 4), TO_FIXED(y + (rand() & 9) - 4),
                               (rand() & 63) - 32, -((rand() & 91) + 30), PAL_TEXT_GOLD, 8);
            }
            break;
        default:
            break;
    }
}

static void game_coop_apply_into(const u8* buf, int len) {
    const u8* p = buf;
    if (!buf || len < 8) return;
#define SNAP_LEFT() (len - (int)(p - buf))
    if (rd16(&p) != COOP_SNAPSHOT_MAGIC) return;
    rd16(&p); // seq
    u8 flags = rd8(&p);
    bool boss_was_active = g_game.boss_active;
    bool boss_now_active = (flags & 2) != 0;
    g_game.is_game_over = (flags & 1) != 0;
    g_game.boss_active = boss_now_active;
    if (boss_now_active && !boss_was_active) audio_begin_boss_music();
    else if (!boss_now_active && boss_was_active) audio_end_boss_music();
    g_game.is_new_high_score = (flags & 4) != 0;
    g_game.time_up = (flags & 8) != 0;
    g_game.score = rd32(&p);
    u64 host_coins = (u64)rd32(&p) | ((u64)rd32(&p) << 32);
    g_game.wave = (int)rd16(&p);
    g_game.mode = (GameMode)rd8(&p);
    g_game.combo = (int)rd8(&p);
    g_game.wave_banner_timer = (int)rd16(&p);
    g_game.intermission_timer = (int)rd16(&p);
    g_game.overdrive_timer = (int)rd16(&p);
    g_game.spawn_timer = (int)rd16(&p);
    g_game.shake_x = (int)(s8)rd8(&p);
    g_game.shake_y = (int)(s8)rd8(&p);
    g_game.shake_timer = (int)rd8(&p);

    if (SNAP_LEFT() < 44) return; // two v4 player blocks
    deser_player(&p, &g_game.player, &g_game.p1_loadout,
                 &g_game.beam_charge, &g_game.beam_timer, &g_game.beam_active,
                 &g_game.primary_beam_ramp, &g_game.primary_beam_active);
    deser_player(&p, &g_game.player2, &g_game.p2_loadout,
                 &g_game.player2.beam_charge, &g_game.player2.beam_timer,
                 &g_game.player2.beam_active, &g_game.player2.primary_beam_ramp,
                 &g_game.player2.primary_beam_active);

    for (int i = 0; i < MAX_ASTEROIDS; i++) g_game.asteroids[i].active = false;
    int ac = (int)rd8(&p);
    if (SNAP_LEFT() < ac * 10) return;
    for (int i = 0; i < ac && i < MAX_ASTEROIDS; i++) {
        Asteroid* a = &g_game.asteroids[i];
        a->active = true;
        a->x = (int)rd16(&p);
        a->y = (int)rd16(&p);
        a->vx = rd16s(&p);
        a->vy = rd16s(&p);
        a->type = (AsteroidType)rd8(&p);
        a->hp = (int)rd8(&p);
        a->hp_frac = 0;
        a->radius = (a->type == AST_LARGE) ? 12 : ((a->type == AST_MED_A || a->type == AST_MED_B) ? 8 : (a->type == AST_SMALL ? 5 : 3));
    }

    for (int i = 0; i < MAX_BULLETS; i++) g_game.bullets[i].active = false;
    int bcnt = (int)rd8(&p);
    if (SNAP_LEFT() < bcnt * 11) return;
    for (int i = 0; i < bcnt && i < MAX_BULLETS; i++) {
        Bullet* b = &g_game.bullets[i];
        b->active = true;
        b->x = (int)rd16(&p);
        b->y = (int)rd16(&p);
        b->vx = rd16s(&p);
        b->vy = rd16s(&p);
        b->damage = (int)rd8(&p);
        b->life = (int)rd8(&p);
        u8 bf = rd8(&p);
        b->enemy = (bf & 1) != 0;
        b->heavy = (bf & 2) != 0;
        b->owner = (bf >> 2) & 3;
        b->radius = b->heavy ? 3 : 2;
    }

    for (int i = 0; i < MAX_DRONES; i++) g_game.drones[i].active = false;
    int dcount = (int)rd8(&p);
    if (SNAP_LEFT() < dcount * 10) return;
    for (int i = 0; i < dcount && i < MAX_DRONES; i++) {
        Drone* d = &g_game.drones[i];
        d->active = true;
        d->x = (int)rd16(&p);
        d->y = (int)rd16(&p);
        d->vx = rd16s(&p);
        d->vy = rd16s(&p);
        d->hp = (int)rd8(&p);
        d->phase = (int)rd8(&p);
    }

    int boss_on = (int)rd8(&p);
    if (boss_on) {
        if (SNAP_LEFT() < 21) return; // boss block
        g_game.boss_active = true;
        g_game.boss.active = true;
        g_game.boss.mini = rd8(&p) ? true : false;
        g_game.boss.x = (int)rd16(&p);
        g_game.boss.y = (int)rd16(&p);
        g_game.boss.vx = rd16s(&p);
        g_game.boss.vy = rd16s(&p);
        g_game.boss.hp = (int)rd16(&p);
        g_game.boss.hp_max = (int)rd16(&p);
        g_game.boss.phase = (int)rd8(&p);
        g_game.boss.beam_x = (int)rd16(&p);
        g_game.boss.beam_timer = (int)rd16(&p);
        g_game.boss.flash_timer = (int)rd8(&p);
        g_game.boss.phase_timer = (int)rd16(&p);
        g_game.boss.aim_x = (int)rd16(&p);
    } else {
        g_game.boss_active = false;
        g_game.boss.active = false;
    }

    for (int i = 0; i < MAX_BOSS_BULLETS; i++) g_game.boss_bullets[i].active = false;
    int bbc = (int)rd8(&p);
    if (SNAP_LEFT() < bbc * 9) return;
    for (int i = 0; i < bbc && i < MAX_BOSS_BULLETS; i++) {
        Bullet* b = &g_game.boss_bullets[i];
        b->active = true;
        b->enemy = true;
        b->owner = 2;
        b->x = (int)rd16(&p);
        b->y = (int)rd16(&p);
        b->vx = rd16s(&p);
        b->vy = rd16s(&p);
        b->heavy = rd8(&p) ? true : false;
        b->radius = b->heavy ? 3 : 2;
    }

    for (int i = 0; i < MAX_POWERUPS; i++) g_game.powerups[i].active = false;
    int pwc = (int)rd8(&p);
    if (SNAP_LEFT() < pwc * 7) return;
    for (int i = 0; i < pwc && i < MAX_POWERUPS; i++) {
        Powerup* pu = &g_game.powerups[i];
        pu->active = true;
        pu->x = (int)rd16(&p);
        pu->y = (int)rd16(&p);
        pu->vy = rd16s(&p);
        pu->type = (PowerupType)rd8(&p);
    }

    /* ── v3 tail: explosion animations + FX events. Older/corrupt buffers
     * simply end here — every read above was length-guarded. ─────────── */
    if (SNAP_LEFT() >= 1) {
        int xc = (int)rd8(&p);
        if (SNAP_LEFT() < xc * 5) return;
        for (int i = 0; i < MAX_EXPLOSIONS; i++) g_game.explosions[i].active = false;
        for (int i = 0; i < xc && i < MAX_EXPLOSIONS; i++) {
            Explosion* ex = &g_game.explosions[i];
            ex->active = true;
            ex->x = (int)(s16)rd16(&p);
            ex->y = (int)(s16)rd16(&p);
            ex->frame = (int)rd8(&p) & 0xF;
            ex->timer = 0;
        }
        if (SNAP_LEFT() >= 1) {
            int nfx = (int)rd8(&p);
            if (SNAP_LEFT() < nfx * 5) return;
            for (int i = 0; i < nfx; i++) {
                u8 type = rd8(&p);
                int fx = (int)(s16)rd16(&p);
                int fy = (int)(s16)rd16(&p);
                guest_replay_fx(type, fx, fy);
            }
        }
    }

    /* Guest progression sync: bank only what was earned during this session.
     * The first snapshot after a GAME_START is the baseline (the host's
     * pre-join balance is not transferred); afterwards every positive delta
     * is co-op earnings and lands in the guest's own save too. */
    if (!s_coins_baselined) {
        s_host_coins_prev = host_coins;
        s_coins_baselined = 1;
    } else if (host_coins > s_host_coins_prev) {
        u64 delta = host_coins - s_host_coins_prev;
        g_settings.coins += delta;
        if (g_settings.coins > COINS_MAX) g_settings.coins = COINS_MAX;
        save_write();
        s_host_coins_prev = host_coins;
    } else if (host_coins < s_host_coins_prev) {
        /* Host spent / reset data: re-baseline so we never go backwards. */
        s_host_coins_prev = host_coins;
    }

    /* Bank a new best score when the run ends (writes once per run). */
    if (g_game.is_game_over && g_game.score > g_settings.high_score) {
        g_settings.high_score = g_game.score;
        save_write();
    }

    s_render_ticks = 0;
#undef SNAP_LEFT
}

static void game_coop_advance_render_impl(void) {
    s_render_ticks++;
    /* The guest's own animation clock: drives rainbow paint waves, laser
     * sprites, boss engine pulse, HUD blink, and the synced explosion
     * animations. Without this the guest world rendered frozen. */
    s_game_frame++;
    starfield_update();

    if (g_game.shake_timer > 0) g_game.shake_timer--;
    else { g_game.shake_x = 0; g_game.shake_y = 0; }
    if (g_game.wave_banner_timer > 0) g_game.wave_banner_timer--;
    if (g_game.combo_timer > 0) {
        g_game.combo_timer--;
        if (g_game.combo_timer == 0) g_game.combo = 1;
    }

    /* Synced/Fx-spawned explosions animate at the host's 3-tick cadence. */
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
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (g_game.particles[i].active) {
            g_game.particles[i].x += g_game.particles[i].vx;
            g_game.particles[i].y += g_game.particles[i].vy;
            g_game.particles[i].life--;
            if (g_game.particles[i].life == 0) g_game.particles[i].active = false;
        }
    }

    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        if (!g_game.asteroids[i].active) continue;
        g_game.asteroids[i].x += g_game.asteroids[i].vx;
        g_game.asteroids[i].y += g_game.asteroids[i].vy;
    }
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!g_game.bullets[i].active) continue;
        g_game.bullets[i].x += g_game.bullets[i].vx;
        g_game.bullets[i].y += g_game.bullets[i].vy;
    }
    for (int i = 0; i < MAX_DRONES; i++) {
        if (!g_game.drones[i].active) continue;
        g_game.drones[i].x += g_game.drones[i].vx;
        g_game.drones[i].y += g_game.drones[i].vy;
    }
    if (g_game.boss_active && g_game.boss.active) {
        g_game.boss.x += g_game.boss.vx;
        g_game.boss.y += g_game.boss.vy;
    }
    for (int i = 0; i < MAX_BOSS_BULLETS; i++) {
        if (!g_game.boss_bullets[i].active) continue;
        g_game.boss_bullets[i].x += g_game.boss_bullets[i].vx;
        g_game.boss_bullets[i].y += g_game.boss_bullets[i].vy;
    }
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!g_game.powerups[i].active) continue;
        g_game.powerups[i].y += g_game.powerups[i].vy;
    }

    /* Engine trails are pure cosmetics: spawn them locally under BOTH ships
     * so the host's ship shows its own trail colours on the guest too. */
    if (!g_game.is_game_over) {
        if (!g_game.player.dead && (rand() & 2) == 0) {
            u8 col = gfx_get_trail_color_animated(g_game.p1_loadout.trail_index,
                                                  ((s_game_frame >> 1) + (rand() & 3)) * 4);
            spawn_particle(g_game.player.x + (rand() & 1023) - 512,
                           g_game.player.y + TO_FIXED(8),
                           (rand() & 255) - 128, (rand() & 127) + 200, col, (rand() & 7) + 6);
        }
        if (!g_game.player2.dead && (rand() & 2) == 0) {
            u8 col = gfx_get_trail_color_animated(g_game.p2_loadout.trail_index,
                                                  ((s_game_frame >> 1) + (rand() & 3)) * 4);
            spawn_particle(g_game.player2.x + (rand() & 1023) - 512,
                           g_game.player2.y + TO_FIXED(8),
                           (rand() & 255) - 128, (rand() & 127) + 200, col, (rand() & 7) + 6);
        }
    }

    /* Local input prediction: nudge the guest's OWN ship from its live touch
     * input between snapshots so it feels 1:1 responsive; the authoritative
     * host position corrects drift every snapshot. */
    if (!g_game.is_game_over && !g_game.player2.dead && s_coop_local_keys) {
        Player* p = &g_game.player2;
        int mx = 0, my = 0;
        if (s_coop_local_keys & KEY_LEFT) mx -= 1;
        if (s_coop_local_keys & KEY_RIGHT) mx += 1;
        if (s_coop_local_keys & KEY_UP) my -= 1;
        if (s_coop_local_keys & KEY_DOWN) my += 1;
        if (mx || my) {
            CoopLoadout lo;
            loadout_from_settings(&lo);
            int eng_mult = lo_engine_mult(&lo);
            int spd = ((TO_FIXED(1) + 50) * eng_mult) >> 8;
            if (mx && my) {
                p->x += (mx * spd * 181) / 256;
                p->y += (my * spd * 181) / 256;
            } else {
                p->x += mx * spd;
                p->y += my * spd;
            }
            if (p->x < TO_FIXED(12)) p->x = TO_FIXED(12);
            if (p->x > TO_FIXED(SCREEN_WIDTH - 12)) p->x = TO_FIXED(SCREEN_WIDTH - 12);
            if (p->y < TO_FIXED(22)) p->y = TO_FIXED(22);
            if (p->y > TO_FIXED(SCREEN_HEIGHT - 12)) p->y = TO_FIXED(SCREEN_HEIGHT - 12);
        }
    }
}

void game_coop_set_guest_active(int active) {
    s_coop_guest_active = active ? 1 : 0;
    if (active) {
        memset(&g_game.player2, 0, sizeof(Player));
        g_game.player2.x = TO_FIXED(SCREEN_WIDTH / 2 - 30);
        g_game.player2.y = TO_FIXED(SCREEN_HEIGHT - 24);
        g_game.player2.radius = 6;
        int max_lives = get_max_lives();
        if (g_settings.difficulty == DIFF_CADET) max_lives++;
        g_game.player2.lives = max_lives;
        g_game.player2.shield_charges = get_start_shields();
        g_game.player2.invulnerable_timer = 90;
        if (s_coop_p2_lo_valid) g_game.p2_loadout = s_coop_p2_lo;
        s_coop_snapshot_seq = 0;
        s_coop_frame = 0;
        s_fx_head = 0;
        s_fx_count = 0;
    } else if (g_game.player.dead && !g_game.is_game_over) {
        /* Partner left while the host pilot was spectating: nobody is left
         * to fight, so the run is over for real. */
        finish_run(false);
    }
}

void game_coop_set_guest_loadout(const CoopLoadout* lo) {
    if (!lo) return;
    s_coop_p2_lo = *lo;
    s_coop_p2_lo_valid = 1;
    g_game.p2_loadout = *lo;
}

void game_coop_set_guest_keys(u16 keys) {
    s_coop_guest_keys = keys;
}

void game_coop_set_local_input(u16 keys) {
    s_coop_local_keys = keys;
}

/* ── Guest-side audio helpers ───────────────────────────────────────────
 * The guest doesn't simulate, but it still wants its own laser SFX to play
 * exactly when the host fires its ship.  These expose the host simulation's
 * numbers (Android fire-rate formula + big-laser timing) so coop.c can
 * mirror the cadence locally. */
int game_coop_local_shot_cooldown(void) {
    static const int tbl[6] = { 256, 215, 178, 148, 122, 102 };
    int lv = g_settings.upgrade_levels[UPG_FIRE_RATE];
    if (lv < 0) lv = 0;
    if (lv > 5) lv = 5;
    int cd = (28 * tbl[lv]) >> 8; /* matches fire_weapon_for() on Android */
    if (cd < 4) cd = 4;
    return cd;
}

int game_coop_beam_charge_ticks(void) { return BEAM_CHARGE_TICKS; }
int game_coop_beam_duration_ticks(void) { return BEAM_DURATION_TICKS; }

void game_coop_set_render_only(int on) {
    s_coop_render_only = on ? 1 : 0;
    if (on) {
        /* A fresh GAME_START is arriving: the next snapshot re-establishes
         * the coin baseline instead of paying out the host's whole balance. */
        s_coins_baselined = 0;
    }
}

int game_coop_is_guest_active(void) { return s_coop_guest_active; }
int game_coop_is_render_only(void) { return s_coop_render_only; }

int game_coop_local_player_dead(void) {
    /* The dead ship is player 1 on the host, player 2 on the guest. */
    return s_coop_render_only ? (g_game.player2.dead ? 1 : 0)
                              : (g_game.player.dead ? 1 : 0);
}

int game_coop_partner_present(void) {
    return (s_coop_guest_active || s_coop_render_only) ? 1 : 0;
}

int game_coop_serialize(u8* buf, int cap) {
    return game_coop_serialize_into(buf, cap);
}

void game_coop_apply(const u8* buf, int len) {
    game_coop_apply_into(buf, len);
}

void game_coop_advance_render(void) {
    game_coop_advance_render_impl();
}

void game_coop_get_p2_pos(int* fx, int* fy) {
    *fx = g_game.player2.x;
    *fy = g_game.player2.y;
}
#endif /* PLATFORM_HOST */
