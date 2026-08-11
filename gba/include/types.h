#ifndef TYPES_H
#define TYPES_H

#include <tonc.h>

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 160

#define NUM_ACCENTS 5
#define NUM_TRAILS 4
#define NUM_RIGS 3
#define NUM_LASERS 4

typedef enum {
    DIFF_CADET = 0,
    DIFF_PILOT = 1,
    DIFF_ACE   = 2
} Difficulty;

typedef enum {
    WEAPON_SPREAD  = 0,
    WEAPON_TWIN    = 1,
    WEAPON_FOCUSED = 2
} WeaponRig;

typedef enum {
    SCREEN_MAIN_MENU,
    SCREEN_HANGAR,
    SCREEN_SETTINGS,
    SCREEN_CONTROLS,
    SCREEN_PLAYING,
    SCREEN_PAUSED,
    SCREEN_GAME_OVER,
    SCREEN_CREDITS
} GameScreen;

typedef struct {
    Difficulty difficulty;
    int music_volume; // 0..100
    int sfx_volume;   // 0..100
    bool screen_shake;
    int accent_index; // 0..4
    int trail_index;  // 0..3
    WeaponRig weapon_rig; // 0..2
    int laser_index;  // 0..3
    u32 high_score;
    u32 coins;
    u8 owned_accents; // bitmask for 5 paints
    u8 owned_trails;  // bitmask for 4 trails
    u8 owned_rigs;    // bitmask for 3 rigs
    u8 owned_lasers;  // bitmask for 4 laser colours
} GameSettings;

extern GameSettings g_settings;

#endif
