#ifndef TYPES_H
#define TYPES_H

#include <tonc.h>

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 160

#define NUM_ACCENTS 9
#define NUM_TRAILS 8
#define NUM_RIGS 6
#define NUM_LASERS 8
#define NUM_UPGRADES 7

typedef enum {
    DIFF_CADET = 0,
    DIFF_PILOT = 1,
    DIFF_ACE   = 2
} Difficulty;

typedef enum {
    WEAPON_TWIN      = 0,
    WEAPON_SPREAD    = 1,
    WEAPON_FOCUSED   = 2,
    WEAPON_TRIPLE    = 3,
    WEAPON_PLASMA    = 4,
    WEAPON_QUANTUM   = 5
} WeaponRig;

typedef enum {
    UPG_SHIELD    = 0,
    UPG_HULL      = 1,
    UPG_THRUSTERS = 2,
    UPG_SCAVENGER = 3,
    UPG_DAMAGE    = 4,
    UPG_OVERDRIVE = 5,
    UPG_COMBO     = 6
} UpgradeType;

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
    int accent_index; // 0..8
    int trail_index;  // 0..7
    WeaponRig weapon_rig; // 0..5
    int laser_index;  // 0..7
    u32 high_score;
    u32 coins;
    u16 owned_accents;  // bitmask for 9 paints
    u16 owned_trails;   // bitmask for 8 trails
    u16 owned_rigs;     // bitmask for 6 rigs
    u16 owned_lasers;   // bitmask for 8 lasers
    u8  upgrade_levels[NUM_UPGRADES]; // 0..3 for each upgrade
} GameSettings;

extern GameSettings g_settings;

#endif
