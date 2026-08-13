#ifndef GAME_H
#define GAME_H

#include "types.h"

#define MAX_ASTEROIDS 48
#define MAX_BULLETS 64
#define MAX_DRONES 8
#define MAX_POWERUPS 6
#define MAX_PARTICLES 64
#define MAX_EXPLOSIONS 10

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
    bool active;
} Drone;

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
void game_set_mode(GameMode mode);
GameMode game_get_mode(void);
void game_start(void);
void game_update(void);
void game_draw(void);

#endif
