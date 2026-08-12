#ifndef TYPES_H
#define TYPES_H

#include "platform.h"

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 160

#define NUM_ACCENTS 9
#define NUM_TRAILS 8
#define NUM_RIGS 8
#define NUM_LASERS 12
#define NUM_UPGRADES 8
#define UPG_MAX_LEVEL 5

typedef enum {
    DIFF_CADET = 0,
    DIFF_PILOT = 1,
    DIFF_ACE   = 2
} Difficulty;

typedef enum {
    WEAPON_SINGLE    = 0, // starter: 1 weak bullet
    WEAPON_TWIN      = 1,
    WEAPON_SPREAD    = 2,
    WEAPON_FOCUSED   = 3,
    WEAPON_TRIPLE    = 4,
    WEAPON_PLASMA    = 5,
    WEAPON_QUANTUM   = 6,
    WEAPON_NOVA      = 7  // final mega weapon
} WeaponRig;

typedef enum {
    UPG_ENGINE      = 0, // ship move speed: 0.70x .. 2.00x
    UPG_FIRE_RATE   = 1, // shoot interval: 2/sec .. 10/sec
    UPG_DAMAGE      = 2, // bullet damage
    UPG_SHIELD      = 3, // shield cap
    UPG_HULL        = 4, // extra lives
    UPG_DASH        = 5, // dash cooldown / invuln
    UPG_SCAVENGER   = 6, // coin bonus + magnet
    UPG_OVERDRIVE   = 7  // rapid & powerup duration
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
