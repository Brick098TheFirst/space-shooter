#ifndef GAME_H
#define GAME_H

#include "types.h"

#define MAX_ASTEROIDS 48
#define MAX_BULLETS 96
#define MAX_DRONES 8
/* Boss bullet budget. The GBA has to render every live bullet from a plain
 * Mode 4 software blitter twice per frame, so it gets a smaller pool. */
#ifdef PLATFORM_HOST
#define MAX_BOSS_BULLETS 48
#else
#define MAX_BOSS_BULLETS 28
#endif
#define MAX_POWERUPS 6
#define MAX_PARTICLES 64
#define MAX_EXPLOSIONS 16

typedef struct {
    int x; // 8.8 fixed point
    int y; // 8.8 fixed point
    int dash_dir_x; // 8.8
    int dash_dir_y; // 8.8
    int radius;
    int lives;
    int shield_charges;
    int fire_cooldown;
    int dash_cooldown;
    int dash_remaining;
    int invulnerable_timer;
    int rapid_fire_timer;
    // Big laser state for the SECOND (co-op) ship. The first ship keeps the
    // legacy top-level GameState beam fields so the original single-player
    // code path is untouched; these fields let player 2 charge/fire its own
    // beam independently on the host-authoritative simulation.
    int beam_charge;
    int beam_timer;
    bool beam_active;
    bool dead; // player 2 lost all lives (host continues with player 1)
} Player;

typedef enum {
    AST_LARGE,
    AST_MED_A,
    AST_MED_B,
    AST_SMALL,
    AST_TINY
} AsteroidType;

typedef struct {
    int x; // 8.8
    int y; // 8.8
    int vx; // 8.8
    int vy; // 8.8
    AsteroidType type;
    int radius;
    int hp;
    int hp_frac; // 8.8 fixed fraction of HP (big laser does <1 dmg/frame)
    bool active;
} Asteroid;

typedef struct {
    int x; // 8.8
    int y; // 8.8
    int vx; // 8.8
    int vy; // 8.8
    int radius;
    int damage;
    int life;
    bool enemy;
    bool heavy;
    bool active;
    /* Co-op: which player owns this player bullet (0 = player 1 / host,
     * 1 = player 2 / guest, 2 = enemy/drone/boss).  Used to render each
     * ship's lasers in its own crystal colour. */
    u8 owner;
} Bullet;

typedef struct {
    int x; // 8.8
    int y; // 8.8
    int vx; // 8.8
    int vy; // 8.8
    int shoot_timer;
    int burst_timer;
    int burst_shots;
    int phase;
    int hp;
    int hp_frac; // 8.8 fixed fraction of HP (big laser)
    bool active;
} Drone;

/* Boss ship (wave 10/20/30...) and mini-boss (wave 5/15/25...). */
typedef enum {
    BOSS_IDLE,
    BOSS_SWEEP,        // side-to-side sweep firing straight down
    BOSS_BURST,        // radial 8-way burst
    BOSS_BEAM_WIND,    // warning flash before laser
    BOSS_BEAM_FIRE,    // huge sweeping beam
    BOSS_DIVE,         // fast chase lunge
    BOSS_REPOSITION    // recover high
} BossPhase;

typedef struct {
    int x, y;            // 8.8
    int vx, vy;          // 8.8
    int hp;
    int hp_max;
    int hp_frac;         // 8.8
    int phase;           // BossPhase
    int phase_timer;
    int aim_x;           // 8.8 target during sweeps
    int beam_x;          // 8.8 beam column
    int beam_timer;
    int beam_width;      // half-width in fixed
    int flash_timer;
    bool active;
    bool mini;           // wave 5/15/25...  ~1/4 HP of the next full boss
    int cooldown;
    int fire_state;
    int sweep_dir;
} Boss;

typedef enum {
    PWR_SHIELD,
    PWR_RAPID,
    PWR_REPAIR
} PowerupType;

typedef struct {
    int x; // 8.8
    int y; // 8.8
    int vy; // 8.8
    PowerupType type;
    bool active;
} Powerup;

typedef struct {
    int x; // 8.8
    int y; // 8.8
    int vx; // 8.8
    int vy; // 8.8
    u8 color;
    u8 life;
    u8 max_life;
    bool active;
} Particle;

typedef struct {
    int x;
    int y;
    int frame;
    int timer;
    bool active;
} Explosion;

/* The subset of the player's equipped loadout that matters for co-op: the
 * ship paint, laser crystal, weapon rig and engine trail of a remote player,
 * plus their stat upgrades so the host-authoritative simulation applies the
 * guest's own fire rate / damage / speed numbers. */
typedef struct {
    int accent_index;   // ship colour
    int laser_index;    // laser colour
    WeaponRig weapon_rig;
    int trail_index;    // engine trail colour
    u8 upgrade_levels[NUM_UPGRADES];
} CoopLoadout;

typedef struct {
    Player player;
    /* Second player for host-authoritative co-op (the guest ship). The host
     * simulates it from the guest's streamed input; the guest just renders
     * it (and itself) from the host's snapshots. */
    Player player2;
    CoopLoadout p1_loadout; // host visuals (mirrors g_settings on host)
    CoopLoadout p2_loadout; // guest visuals
    Asteroid asteroids[MAX_ASTEROIDS];
    Bullet bullets[MAX_BULLETS];
    Drone drones[MAX_DRONES];
    Boss boss;
    Bullet boss_bullets[MAX_BOSS_BULLETS];
    bool boss_active;
    int boss_hit_flash;
    Powerup powerups[MAX_POWERUPS];
    Particle particles[MAX_PARTICLES];
    Explosion explosions[MAX_EXPLOSIONS];

    u32 score;
    int wave;
    int combo;
    int combo_timer;
    int wave_banner_timer;
    int shake_timer;
    int shake_x;
    int shake_y;
    int intermission_timer;
    bool is_game_over;
    bool is_new_high_score;
    GameMode mode;
    int spawn_timer;
    int overdrive_timer;
    bool time_up;

    // Big laser (replaces dash): hold to charge 2s, then a piercing beam
    // fires for 3s dealing (current laser damage / 10) every tick.
    int beam_charge; // ticks accumulated while charging
    int beam_timer;  // ticks remaining while beam is live
    bool beam_active;
} GameState;

extern GameState g_game;

void game_init(void);
void game_request_full_redraw(void);
void game_set_mode(GameMode mode);
GameMode game_get_mode(void);
void game_start(void);
void game_update(void);
void game_draw(void);

/* ── Co-op (host-authoritative) integration hooks ───────────────────────
 * These are called from the multiplayer-test network layer (coop.c) and are
 * no-ops / inert on the normal single-player build.  On the HOST the game
 * simulates a second ship from the guest's streamed input; on the GUEST the
 * game stops simulating and only renders the host's snapshots. */
void game_coop_set_guest_active(int active);        // host: start simulating player 2
void game_coop_set_guest_loadout(const CoopLoadout* lo); // host: guest's visual/combat config
void game_coop_set_guest_keys(u16 keys);            // host: guest input each tick
void game_coop_set_render_only(int on);             // guest: render-only mode
int  game_coop_is_guest_active(void);               // host is simulating player 2
int  game_coop_is_render_only(void);                // guest is rendering host world
/* Host snapshot out / guest snapshot in (wire buffers owned by coop.c). */
int  game_coop_serialize(u8* buf, int cap);
void game_coop_apply(const u8* buf, int len);
/* Advance smooth rendering between snapshots on the guest. */
void game_coop_advance_render(void);
/* Guest-side: report the last authoritative position of player 2 so the
 * input/feedback layer can keep the local ship crisp. */
void game_coop_get_p2_pos(int* fx, int* fy);

#endif
