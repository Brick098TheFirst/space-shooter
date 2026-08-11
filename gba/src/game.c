#include "game.h"
#include "renderer.h"
#include "audio.h"
#include "save.h"
#include "starfield.h"
#include <stdlib.h>
#include <string.h>

EWRAM_BSS GameState g_game;

static bool s_game_static_valid = false;

#define FIXED_ONE 256
#define TO_FIXED(n) ((n) * 256)
#define FROM_FIXED(n) ((n) >> 8)

static int get_diff_speed_mult(void) {
    switch (g_settings.difficulty) {
        case DIFF_CADET: return 220; // 0.86x (in 256 base)
        case DIFF_ACE:   return 307; // 1.20x
        default:         return 256; // 1.00x
    }
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

    // Spawn sparks (8 directions, fast bitwise math)
    for (int p = 0; p < 8; p++) {
        int angle = p * 32;
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
    u8 col = gfx_get_trail_color(g_settings.trail_index);
    int px = g_game.player.x + (rand() & 1023) - 512;
    int py = g_game.player.y + TO_FIXED(8);
    int pvx = (rand() & 255) - 128;
    int pvy = (rand() & 127) + 200;
    spawn_particle(px, py, pvx, pvy, col, (rand() & 7) + 6);
}

static void try_spawn_powerup(int x, int y, int chance_pct) {
    if ((rand() % 100) >= chance_pct) return;
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!g_game.powerups[i].active) {
            g_game.powerups[i].x = TO_FIXED(x);
            g_game.powerups[i].y = TO_FIXED(y);
            g_game.powerups[i].vy = 90; // gentle float downward
            int roll = rand() % 100;
            if (roll < 40) g_game.powerups[i].type = PWR_SHIELD;
            else if (roll < 80) g_game.powerups[i].type = PWR_RAPID;
            else g_game.powerups[i].type = PWR_REPAIR;
            g_game.powerups[i].active = true;
            return;
        }
    }
}

static void award_score(int base_pts) {
    g_game.score += base_pts * g_game.combo;
    if (g_game.combo < 8) g_game.combo++;
    g_game.combo_timer = 156; // 2.6 seconds at 60 FPS
}

static void damage_player(void) {
    if (g_game.player.invulnerable_timer > 0) return;

    int px = FROM_FIXED(g_game.player.x);
    int py = FROM_FIXED(g_game.player.y);

    if (g_game.player.shield_charges > 0) {
        g_game.player.shield_charges--;
        g_game.player.invulnerable_timer = 60; // 1.0s
        trigger_explosion(px, py);
    } else {
        g_game.player.lives--;
        g_game.player.invulnerable_timer = 100; // 1.6s
        g_game.player.x = TO_FIXED(SCREEN_WIDTH / 2);
        g_game.player.y = TO_FIXED(SCREEN_HEIGHT - 20);
        trigger_explosion(px, py);

        if (g_game.player.lives <= 0) {
            g_game.is_game_over = true;
            if (g_game.score > g_settings.high_score) {
                g_settings.high_score = g_game.score;
                g_game.is_new_high_score = true;
                save_write();
            }
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
                g_game.asteroids[i].hp = 2;
            } else if (type == AST_MED_A || type == AST_MED_B) {
                g_game.asteroids[i].radius = 8;
                g_game.asteroids[i].hp = 1;
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
    a->active = false;
    int ax = FROM_FIXED(a->x);
    int ay = FROM_FIXED(a->y);
    trigger_explosion(ax, ay);

    if (award) {
        int pts = (a->type == AST_LARGE) ? 60 : ((a->type == AST_MED_A || a->type == AST_MED_B) ? 35 : 20);
        award_score(pts);

        int mult = get_diff_speed_mult();
        if (a->type == AST_LARGE) {
            int spd = (160 * mult) >> 8;
            spawn_asteroid(AST_MED_A, a->x - TO_FIXED(6), a->y, -spd, spd);
            spawn_asteroid(AST_MED_B, a->x + TO_FIXED(6), a->y, spd, spd);
        } else if (a->type == AST_MED_A || a->type == AST_MED_B) {
            int spd = (200 * mult) >> 8;
            spawn_asteroid(AST_SMALL, a->x - TO_FIXED(4), a->y, -spd, spd);
            spawn_asteroid(AST_TINY, a->x + TO_FIXED(4), a->y, spd, spd);
        }
        try_spawn_powerup(ax, ay, 15);
    }
}

static void destroy_drone(int idx, bool award) {
    Drone* d = &g_game.drones[idx];
    d->active = false;
    int dx = FROM_FIXED(d->x);
    int dy = FROM_FIXED(d->y);
    trigger_explosion(dx, dy);

    if (award) {
        award_score(110);
        try_spawn_powerup(dx, dy, 28);
    }
}

static void begin_wave(void) {
    g_game.wave++;
    g_game.wave_banner_timer = 120; // 2 seconds

    int diff_extra = (g_settings.difficulty == DIFF_ACE) ? 2 : 0;
    int ast_count = 3 + g_game.wave + diff_extra;
    if (ast_count > 12) ast_count = 12;

    int mult = get_diff_speed_mult();

    for (int i = 0; i < ast_count; i++) {
        int x = TO_FIXED((rand() % (SCREEN_WIDTH - 40)) + 20);
        int y = -TO_FIXED((rand() % 120) + 20 + i * 20);
        int vx = ((rand() % 120) - 60) * mult >> 8;
        int vy = ((rand() % 100) + 70) * mult >> 8;
        
        bool is_large = (rand() % 100) < (25 + g_game.wave * 4);
        spawn_asteroid(is_large ? AST_LARGE : (rand() % 2 == 0 ? AST_MED_A : AST_MED_B), x, y, vx, vy);
    }

    if (g_game.wave >= 3) {
        int drone_count = g_game.wave / 3;
        if (drone_count > 4) drone_count = 4;
        for (int i = 0; i < drone_count; i++) {
            if (i < MAX_DRONES) {
                int spacing = (SCREEN_WIDTH - 60) / (drone_count > 1 ? drone_count - 1 : 1);
                g_game.drones[i].x = TO_FIXED(30 + (drone_count > 1 ? i * spacing : 90));
                g_game.drones[i].y = -TO_FIXED(30 + i * 35);
                g_game.drones[i].vx = 0;
                g_game.drones[i].vy = (70 * mult) >> 8;
                g_game.drones[i].shoot_timer = (rand() % 60) + 60;
                g_game.drones[i].phase = rand() % 256;
                g_game.drones[i].hp = (g_settings.difficulty == DIFF_CADET) ? 2 : 3;
                g_game.drones[i].active = true;
            }
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

static void add_enemy_bullet(int x, int y, int vx, int vy) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!g_game.bullets[i].active) {
            g_game.bullets[i].x = x;
            g_game.bullets[i].y = y;
            g_game.bullets[i].vx = vx;
            g_game.bullets[i].vy = vy;
            g_game.bullets[i].radius = 3;
            g_game.bullets[i].damage = 1;
            g_game.bullets[i].life = 140;
            g_game.bullets[i].enemy = true;
            g_game.bullets[i].heavy = false;
            g_game.bullets[i].active = true;
            return;
        }
    }
}

static void fire_player_weapon(void) {
    bool rapid = (g_game.player.rapid_fire_timer > 0);
    int px = g_game.player.x;
    int py = g_game.player.y;

    switch (g_settings.weapon_rig) {
        case WEAPON_FOCUSED:
            add_player_bullet(px, py - TO_FIXED(8), 0, -TO_FIXED(5), 2, true);
            g_game.player.fire_cooldown = rapid ? 6 : 11;
            break;
        case WEAPON_TWIN:
            add_player_bullet(px - TO_FIXED(4), py - TO_FIXED(6), 0, -TO_FIXED(5), 1, false);
            add_player_bullet(px + TO_FIXED(4), py - TO_FIXED(6), 0, -TO_FIXED(5), 1, false);
            g_game.player.fire_cooldown = rapid ? 7 : 13;
            break;
        default: // Spread
            add_player_bullet(px, py - TO_FIXED(6), 0, -TO_FIXED(5), 1, false);
            add_player_bullet(px - TO_FIXED(4), py - TO_FIXED(4), -TO_FIXED(1), -TO_FIXED(4), 1, false);
            add_player_bullet(px + TO_FIXED(4), py - TO_FIXED(4), TO_FIXED(1), -TO_FIXED(4), 1, false);
            g_game.player.fire_cooldown = rapid ? 9 : 17;
            break;
    }
    audio_play_sfx(SFX_LASER);
}

void game_init(void) {
    memset(&g_game, 0, sizeof(GameState));
}

void game_start(void) {
    memset(&g_game, 0, sizeof(GameState));

    g_game.player.x = TO_FIXED(SCREEN_WIDTH / 2);
    g_game.player.y = TO_FIXED(SCREEN_HEIGHT - 24);
    g_game.player.radius = 6;
    g_game.player.lives = (g_settings.difficulty == DIFF_CADET) ? 4 : ((g_settings.difficulty == DIFF_ACE) ? 2 : 3);
    g_game.player.shield_charges = (g_settings.difficulty == DIFF_CADET) ? 1 : 0;
    g_game.player.invulnerable_timer = 90;
    g_game.combo = 1;
    g_game.intermission_timer = 30;
    s_game_static_valid = false;

    audio_play_bgm(BGM_GAME);
}

#define GAME_SPEED_MULTIPLIER 2

/* One simulation tick. */
static void game_update_tick(void) {
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

    // Update Particles
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (g_game.particles[i].active) {
            g_game.particles[i].x += g_game.particles[i].vx;
            g_game.particles[i].y += g_game.particles[i].vy;
            g_game.particles[i].life--;
            if (g_game.particles[i].life == 0) {
                g_game.particles[i].active = false;
            }
        }
    }

    // Update Explosions
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (g_game.explosions[i].active) {
            g_game.explosions[i].timer++;
            if (g_game.explosions[i].timer >= 3) {
                g_game.explosions[i].timer = 0;
                g_game.explosions[i].frame++;
                if (g_game.explosions[i].frame >= 9) {
                    g_game.explosions[i].active = false;
                }
            }
        }
    }

    if (g_game.is_game_over) return;

    if (g_game.combo_timer > 0) {
        g_game.combo_timer--;
        if (g_game.combo_timer == 0) {
            g_game.combo = 1;
        }
    }

    if (g_game.player.fire_cooldown > 0) g_game.player.fire_cooldown--;
    if (g_game.player.dash_cooldown > 0) g_game.player.dash_cooldown--;
    if (g_game.player.dash_remaining > 0) g_game.player.dash_remaining--;
    if (g_game.player.invulnerable_timer > 0) g_game.player.invulnerable_timer--;
    if (g_game.player.rapid_fire_timer > 0) g_game.player.rapid_fire_timer--;

    // Input Handling
    int mx = 0;
    int my = 0;
    if (key_is_down(KEY_LEFT))  mx -= 1;
    if (key_is_down(KEY_RIGHT)) mx += 1;
    if (key_is_down(KEY_UP))    my -= 1;
    if (key_is_down(KEY_DOWN))  my += 1;

    // Dash (B button or R/L shoulder)
    if ((key_hit(KEY_B) || key_hit(KEY_R) || key_hit(KEY_L)) && g_game.player.dash_cooldown == 0) {
        if (mx != 0 || my != 0) {
            g_game.player.dash_dir_x = mx;
            g_game.player.dash_dir_y = my;
        } else {
            g_game.player.dash_dir_x = 0;
            g_game.player.dash_dir_y = -1;
        }
        g_game.player.dash_remaining = 13;
        g_game.player.dash_cooldown = 81; // 1.35s
        g_game.player.invulnerable_timer = 17; // 0.28s
        for (int b = 0; b < 8; b++) emit_engine_particle();
    }

    int spd = (g_game.player.dash_remaining > 0) ? TO_FIXED(3) + 200 : TO_FIXED(1) + 110;
    int dir_x = (g_game.player.dash_remaining > 0) ? g_game.player.dash_dir_x : mx;
    int dir_y = (g_game.player.dash_remaining > 0) ? g_game.player.dash_dir_y : my;

    if (dir_x != 0 && dir_y != 0) {
        g_game.player.x += (dir_x * spd * 181) / 256;
        g_game.player.y += (dir_y * spd * 181) / 256;
    } else {
        g_game.player.x += dir_x * spd;
        g_game.player.y += dir_y * spd;
    }

    // Bounds clamp (HUD margin at top)
    if (g_game.player.x < TO_FIXED(12)) g_game.player.x = TO_FIXED(12);
    if (g_game.player.x > TO_FIXED(SCREEN_WIDTH - 12)) g_game.player.x = TO_FIXED(SCREEN_WIDTH - 12);
    if (g_game.player.y < TO_FIXED(22)) g_game.player.y = TO_FIXED(22);
    if (g_game.player.y > TO_FIXED(SCREEN_HEIGHT - 12)) g_game.player.y = TO_FIXED(SCREEN_HEIGHT - 12);

    if (mx != 0 || my != 0 || g_game.player.dash_remaining > 0) {
        if ((rand() & 1) == 0) emit_engine_particle();
    }

    // Fire weapon (A button)
    if (key_is_down(KEY_A) && g_game.player.fire_cooldown == 0) {
        fire_player_weapon();
    }

    // Update Bullets
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (g_game.bullets[i].active) {
            g_game.bullets[i].x += g_game.bullets[i].vx;
            g_game.bullets[i].y += g_game.bullets[i].vy;
            g_game.bullets[i].life--;
            int bx = FROM_FIXED(g_game.bullets[i].x);
            int by = FROM_FIXED(g_game.bullets[i].y);
            if (g_game.bullets[i].life == 0 || bx < -10 || bx > SCREEN_WIDTH + 10 || by < -15 || by > SCREEN_HEIGHT + 15) {
                g_game.bullets[i].active = false;
            }
        }
    }

    // Update Asteroids
    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        if (g_game.asteroids[i].active) {
            g_game.asteroids[i].x += g_game.asteroids[i].vx;
            g_game.asteroids[i].y += g_game.asteroids[i].vy;
            int ax = FROM_FIXED(g_game.asteroids[i].x);
            int ay = FROM_FIXED(g_game.asteroids[i].y);
            int rad = g_game.asteroids[i].radius;
            // Screen wrap
            if (ax < -rad) g_game.asteroids[i].x = TO_FIXED(SCREEN_WIDTH + rad);
            if (ax > SCREEN_WIDTH + rad) g_game.asteroids[i].x = -TO_FIXED(rad);
            if (ay > SCREEN_HEIGHT + rad) {
                g_game.asteroids[i].y = -TO_FIXED(rad + 10);
                g_game.asteroids[i].x = TO_FIXED((rand() % (SCREEN_WIDTH - 30)) + 15);
            }
        }
    }

    // Update Drones
    int mult = get_diff_speed_mult();
    for (int i = 0; i < MAX_DRONES; i++) {
        if (g_game.drones[i].active) {
            if (g_game.drones[i].y < TO_FIXED(32)) {
                g_game.drones[i].y += g_game.drones[i].vy;
            } else {
                g_game.drones[i].phase = (g_game.drones[i].phase + 2) & 255;
                int osc = (lu_sin(g_game.drones[i].phase * 256) * 110) >> 12;
                g_game.drones[i].x += osc;
                if (g_game.drones[i].x < TO_FIXED(16)) g_game.drones[i].x = TO_FIXED(16);
                if (g_game.drones[i].x > TO_FIXED(SCREEN_WIDTH - 16)) g_game.drones[i].x = TO_FIXED(SCREEN_WIDTH - 16);
            }

            g_game.drones[i].shoot_timer--;
            if (g_game.drones[i].y > TO_FIXED(20) && g_game.drones[i].shoot_timer <= 0) {
                int dx = (g_game.player.x - g_game.drones[i].x) >> 8;
                int dy = (g_game.player.y - g_game.drones[i].y) >> 8;
                int dist_sq = dx*dx + dy*dy;
                int dist = Sqrt(dist_sq);
                if (dist > 5) {
                    int bvx = (dx * 170 * mult) / (dist << 8);
                    int bvy = (dy * 170 * mult) / (dist << 8);
                    add_enemy_bullet(g_game.drones[i].x, g_game.drones[i].y + TO_FIXED(6), bvx, bvy);
                }
                g_game.drones[i].shoot_timer = ((rand() % 60) + 70) * 256 / mult;
            }
        }
    }

    // Update Powerups
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (g_game.powerups[i].active) {
            g_game.powerups[i].y += g_game.powerups[i].vy;
            if (FROM_FIXED(g_game.powerups[i].y) > SCREEN_HEIGHT + 10) {
                g_game.powerups[i].active = false;
            }
        }
    }

    // Resolve Collisions
    int px = FROM_FIXED(g_game.player.x);
    int py = FROM_FIXED(g_game.player.y);

    // Bullet Collisions
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

        // Player bullet vs Asteroids
        bool consumed = false;
        for (int a = 0; a < MAX_ASTEROIDS; a++) {
            if (!g_game.asteroids[a].active) continue;
            int ax = FROM_FIXED(g_game.asteroids[a].x);
            int ay = FROM_FIXED(g_game.asteroids[a].y);
            int ar = g_game.asteroids[a].radius;

            int dist_sq = (bx - ax)*(bx - ax) + (by - ay)*(by - ay);
            if (dist_sq <= (br + ar)*(br + ar)) {
                g_game.asteroids[a].hp -= g_game.bullets[b].damage;
                g_game.bullets[b].active = false;
                consumed = true;
                if (g_game.asteroids[a].hp <= 0) {
                    destroy_asteroid(a, true);
                }
                break;
            }
        }
        if (consumed) continue;

        // Player bullet vs Drones
        for (int d = 0; d < MAX_DRONES; d++) {
            if (!g_game.drones[d].active) continue;
            int dx = FROM_FIXED(g_game.drones[d].x);
            int dy = FROM_FIXED(g_game.drones[d].y);

            int dist_sq = (bx - dx)*(bx - dx) + (by - dy)*(by - dy);
            if (dist_sq <= (br + 8)*(br + 8)) {
                g_game.drones[d].hp -= g_game.bullets[b].damage;
                g_game.bullets[b].active = false;
                if (g_game.drones[d].hp <= 0) {
                    destroy_drone(d, true);
                }
                break;
            }
        }
    }

    // Ship vs Asteroids / Drones
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

    // Ship vs Powerups
    for (int p = 0; p < MAX_POWERUPS; p++) {
        if (!g_game.powerups[p].active) continue;
        int pow_x = FROM_FIXED(g_game.powerups[p].x);
        int pow_y = FROM_FIXED(g_game.powerups[p].y);

        int dist_sq = (px - pow_x)*(px - pow_x) + (py - pow_y)*(py - pow_y);
        if (dist_sq <= (6 + 6)*(6 + 6)) {
            if (g_game.powerups[p].type == PWR_SHIELD) {
                if (g_game.player.shield_charges < 3) g_game.player.shield_charges++;
            } else if (g_game.powerups[p].type == PWR_RAPID) {
                g_game.player.rapid_fire_timer = 540; // 9 seconds
            } else if (g_game.powerups[p].type == PWR_REPAIR) {
                if (g_game.player.lives < 5) g_game.player.lives++;
            }
            g_game.score += 75;
            g_game.powerups[p].active = false;
            audio_play_sfx(SFX_PICKUP);
        }
    }

    // Check Wave Completion
    int active_enemies = 0;
    for (int i = 0; i < MAX_ASTEROIDS; i++) if (g_game.asteroids[i].active) active_enemies++;
    for (int i = 0; i < MAX_DRONES; i++) if (g_game.drones[i].active) active_enemies++;

    if (active_enemies == 0) {
        g_game.intermission_timer--;
        if (g_game.intermission_timer <= 0) {
            begin_wave();
            g_game.intermission_timer = 60;
        }
    } else {
        g_game.intermission_timer = 60;
    }
}

void game_update(void) {
    for (int tick = 0; tick < GAME_SPEED_MULTIPLIER; tick++) {
        game_update_tick();
    }
}

static void game_draw_static(void) {
    starfield_draw_base(0, 0);

    // HUD Glass Cards (static frames)
    gfx_draw_glass_card(3, 2, 70, 16, PAL_BTN_BORDER, 14);
    gfx_draw_glass_card(96, 2, 48, 16, PAL_BTN_BORDER, 14);
    gfx_draw_glass_card(167, 2, 70, 16, PAL_BTN_BORDER, 14);

    gfx_draw_text(160, SCREEN_HEIGHT - 10, "DASH", PAL_TEXT_WHITE);
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

    // Draw Powerups
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (g_game.powerups[i].active) {
            int px = FROM_FIXED(g_game.powerups[i].x) - 5 + ox;
            int py = FROM_FIXED(g_game.powerups[i].y) - 5 + oy;
            const u8* spr = (g_game.powerups[i].type == PWR_SHIELD) ? spr_pwr_shield :
                            ((g_game.powerups[i].type == PWR_RAPID) ? spr_pwr_rapid : spr_pwr_repair);
            gfx_draw_sprite(px, py, 10, 10, spr);
        }
    }

    // Draw Bullets
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (g_game.bullets[i].active) {
            int bx = FROM_FIXED(g_game.bullets[i].x) + ox;
            int by = FROM_FIXED(g_game.bullets[i].y) + oy;
            if (g_game.bullets[i].enemy) {
                gfx_draw_sprite(bx - 3, by - 3, 6, 6, spr_laser_enemy);
            } else if (g_game.bullets[i].heavy) {
                gfx_draw_sprite(bx - 3, by - 7, 6, 14, spr_laser_heavy);
            } else {
                gfx_draw_sprite(bx - 2, by - 5, 4, 10, spr_laser_standard);
            }
        }
    }

    // Draw Asteroids
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

    // Draw Drones
    for (int i = 0; i < MAX_DRONES; i++) {
        if (g_game.drones[i].active) {
            int dx = FROM_FIXED(g_game.drones[i].x) - 9 + ox;
            int dy = FROM_FIXED(g_game.drones[i].y) - 7 + oy;
            gfx_draw_sprite(dx, dy, 18, 14, spr_drone);
        }
    }

    // Draw Particles
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (g_game.particles[i].active) {
            int px = FROM_FIXED(g_game.particles[i].x) + ox;
            int py = FROM_FIXED(g_game.particles[i].y) + oy;
            gfx_draw_pixel(px, py, g_game.particles[i].color);
        }
    }

    // Draw Player Ship (flicker if invulnerable)
    if (!g_game.is_game_over) {
        bool visible = true;
        if (g_game.player.invulnerable_timer > 0) {
            visible = (g_game.player.invulnerable_timer & 2) != 0;
        }
        if (visible) {
            int px = FROM_FIXED(g_game.player.x) - 10 + ox;
            int py = FROM_FIXED(g_game.player.y) - 8 + oy;
            int accent = g_settings.accent_index;
            if (accent < 0 || accent > 4) accent = 1;
            gfx_draw_sprite(px, py, 20, 16, spr_ship[accent]);

            // Draw shield bubble if shielded
            if (g_game.player.shield_charges > 0) {
                gfx_draw_sprite(px - 2, py - 4, 24, 24, spr_shield_bubble);
            }
        }
    }

    // Draw Explosions
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

    // HUD dynamic text
    char buf[32];
    siprintf(buf, "%06u", (unsigned int)g_game.score);
    gfx_draw_text(6, 4, buf, PAL_TEXT_WHITE);

    siprintf(buf, "W%02d", g_game.wave);
    gfx_draw_text_centered(96, 4, 48, buf, PAL_TEXT_CYAN);

    for (int i = 0; i < g_game.player.lives && i < 5; i++) {
        gfx_draw_char(170 + i * 8, 4, '^', PAL_TEXT_GREEN);
    }
    for (int i = 0; i < g_game.player.shield_charges && i < 3; i++) {
        gfx_draw_char(214 + i * 8, 4, '*', PAL_TEXT_CYAN);
    }

    // Combo Indicator (Below score if > 1)
    if (g_game.combo > 1) {
        siprintf(buf, "x%d", g_game.combo);
        u8 acc = gfx_get_accent_color(g_settings.accent_index);
        gfx_draw_text(6, 20, buf, acc);
        gfx_draw_progress_bar(20, 22, 40, 4, g_game.combo_timer, 156, acc, 18);
    }

    // Rapid Fire Timer (Below wave if active)
    if (g_game.player.rapid_fire_timer > 0) {
        siprintf(buf, "RAPID %d", (g_game.player.rapid_fire_timer + 59) / 60);
        gfx_draw_text_centered(80, 20, 80, buf, PAL_TEXT_GOLD);
    }

    // Dash Bar (Bottom Right)
    int dash_ready = 81 - g_game.player.dash_cooldown;
    u8 dash_col = (g_game.player.dash_cooldown == 0) ? PAL_TEXT_GREEN : gfx_get_accent_color(g_settings.accent_index);
    gfx_draw_progress_bar(188, SCREEN_HEIGHT - 9, 48, 5, dash_ready, 81, dash_col, 18);

    // Wave Announcement Banner
    if (g_game.wave_banner_timer > 0) {
        int banner_w = 120;
        int banner_h = 24;
        int bx = (SCREEN_WIDTH - banner_w) / 2;
        int by = 68;
        gfx_draw_glass_card(bx, by, banner_w, banner_h, PAL_TEXT_WHITE, 15);
        siprintf(buf, "WAVE %02d", g_game.wave);
        gfx_draw_text_centered(bx, by + 4, banner_w, buf, PAL_TEXT_WHITE);
        gfx_draw_text_centered(bx, by + 13, banner_w, "GET READY!", PAL_TEXT_CYAN);
    }
}
