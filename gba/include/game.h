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
    u8 accent;
    u8 style;
    bool active;
} Drone;

/* Boss ship (wave 10/20/30...) and mini-boss (wave 5/15/25...). */
typedef enum {
    BOSS_IDLE,
    BOSS_SWEEP,        // Corsair: strafing bomb run across the screen
    BOSS_BURST,        // Corsair: snap fans / Dreadnought: ring barrage
    BOSS_BEAM_WIND,    // Dreadnought: siege beam warning column
    BOSS_BEAM_FIRE,    // Dreadnought: constant-speed sweeping siege beam
    BOSS_DIVE,         // Corsair: feint dive at the player
    BOSS_REPOSITION,   // recover high
    BOSS_WALL,         // Dreadnought: walking curtain walls with a gap
    BOSS_SCISSOR       // Dreadnought: crossing broadsides from both wings
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
    int last_move;       // previous attack roll, so moves never repeat twice
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

typedef struct {
    Player player;
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

    // Infinity Beam primary fire: no charge/cooldown, active while A is held.
    // Damage ramps from 20% to full over 0.6s to keep tapping sub-optimal.
    int primary_beam_ramp;
    bool primary_beam_active;
} GameState;

extern GameState g_game;

void game_init(void);
void game_request_full_redraw(void);
void game_set_mode(GameMode mode);
GameMode game_get_mode(void);
void game_start(void);
void game_update(void);
void game_draw(void);

#endif
