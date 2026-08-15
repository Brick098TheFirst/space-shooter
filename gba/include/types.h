#ifndef TYPES_H
#define TYPES_H

#include "platform.h"

#ifndef SCREEN_WIDTH
#ifdef PLATFORM_HOST
/* Android (host): runtime width so the game adapts to any phone aspect
 * ratio and fills the screen edge-to-edge.  Code paths that draw or clamp
 * against the screen edges all read this live value. */
#define SCREEN_WIDTH host_screen_width()
#else
#define SCREEN_WIDTH 240
#endif
#endif

#ifndef SCREEN_HEIGHT
#define SCREEN_HEIGHT 160
#endif

/* Frame buffer sizes: on the host the visible width is dynamic, so static
 * layers are sized for the widest supported viewport. */
#ifdef PLATFORM_HOST
#define FB_PIXELS (HOST_SCREEN_W_MAX * SCREEN_HEIGHT)
#else
#define FB_PIXELS (SCREEN_WIDTH * SCREEN_HEIGHT)
#endif

/* Coins need 64 bits on Android for cheat-code-scale fortunes
 * (999,000,000,000,000).  The GBA keeps its tight 32-bit counter. */
#ifdef PLATFORM_HOST
typedef u64 coin_t;
#define COINS_MAX ((u64)999000000000000ULL)
#else
typedef u32 coin_t;
/* A billion-credit ultimate weapon must also be purchasable on real GBA
 * hardware.  SRAM already stores this as u32, so no save-layout change is
 * needed. */
#define COINS_MAX ((u32)2000000000u)
#endif

#define NUM_ACCENTS 9
#define NUM_TRAILS 8
#define NUM_RIGS 16
/* Hull STYLES in the SHIPS tab: distinct ship silhouettes (not paints).
 * Style index 0 is the classic Cyber Mk I everyone starts with.  The paint
 * (accent) and the style are applied together: every style respects every
 * paint, including the animated rainbow one. */
#define NUM_SHIP_STYLES 5
#define SHIP_STYLE_CLASSIC 0
/* Laser crystals are deliberately a short, readable five-step ladder.  The
 * weapon-rig catalog below supplies the broad combat progression; crystals
 * are a secondary damage/colour choice instead of a second 24-item weapon
 * list.  The final crystal is also the animated rainbow cosmetic. */
#define NUM_LASERS 5
#define LASER_RAINBOW_IDX 4
#define LASER_FINAL_IDX   LASER_RAINBOW_IDX

#define ACCENT_RAINBOW_IDX 8
#define TRAIL_RAINBOW_IDX  7
#define NUM_UPGRADES 8
#define UPG_MAX_LEVEL 5

typedef enum {
    DIFF_CADET = 0,
    DIFF_PILOT = 1,
    DIFF_ACE   = 2
} Difficulty;

typedef enum {
    WEAPON_SINGLE     = 0,  // starter pulse
    WEAPON_TWIN       = 1,
    WEAPON_SPREAD     = 2,
    WEAPON_FOCUSED    = 3,
    WEAPON_TRIPLE     = 4,
    WEAPON_PLASMA     = 5,
    WEAPON_QUANTUM    = 6,
    WEAPON_NOVA       = 7,
    WEAPON_ARC_HEX    = 8,
    WEAPON_RIFT       = 9,
    WEAPON_COMET      = 10,
    WEAPON_SOLAR      = 11,
    WEAPON_STARQUAKE  = 12,
    WEAPON_VOID       = 13,
    WEAPON_PRISM      = 14,
    WEAPON_INFINITY   = 15  // $1B hold-to-fire continuous beam
} WeaponRig;

typedef enum {
    UPG_ENGINE      = 0, // Speed
    UPG_FIRE_RATE   = 1, // Fire Rate
    UPG_DAMAGE      = 2, // Damage
    UPG_SHIELD      = 3, // Shield
    UPG_HULL        = 4, // Lives
    UPG_DASH        = 5, // Beam damage (+25%/lv)
    UPG_SCAVENGER   = 6, // Coins + magnet
    UPG_OVERDRIVE   = 7  // Rapid duration
} UpgradeType;

typedef enum {
    SCREEN_MAIN_MENU,
    SCREEN_HANGAR,
    SCREEN_SETTINGS,
    SCREEN_CONTROLS,
    SCREEN_PLAYING,
    SCREEN_PAUSED,
    SCREEN_GAME_OVER,
    SCREEN_OPTIONS,
    SCREEN_MODE_SELECT,
    SCREEN_MULTIPLAYER, /* Android: co-op Quick Match lobby browser */
    /* ── Story Mode screens (Android) ── */
    SCREEN_STORY_INTRO,   /* the opening speech, typed out page by page */
    SCREEN_STORY_MAP,     /* fly a mini ship between level nodes */
    SCREEN_STORY_SHOP,    /* Mr Chubbs' docked ship */
    SCREEN_STORY_RESULT   /* level cleared / failed card */
} GameScreen;

typedef enum {
    GAME_MODE_WAVES = 0,
    GAME_MODE_ENDLESS = 1,
    GAME_MODE_OVERDRIVE = 2,
    /* Android Story Mode: 70 hand-tuned levels driven by g_story_levels[]
     * instead of the endless wave escalator. */
    GAME_MODE_STORY = 3
} GameMode;

typedef struct {
    Difficulty difficulty;
    int music_volume; // 0..100
    int sfx_volume;   // 0..100
    bool screen_shake;
    bool tilt_steer;  // unused (gyro removed); kept for save layout
    bool haptics;     // Android only: vibration feedback
    int accent_index; // 0..8
    int trail_index;  // 0..7
    WeaponRig weapon_rig; // 0..5
    int laser_index;  // 0..7
    int ship_index;   // hull style 0..NUM_SHIP_STYLES-1 (SHIPS shop tab)
    u32 high_score;
    coin_t coins;
    u16 owned_accents;  // bitmask for 9 paints
    u16 owned_trails;   // bitmask for 8 trails
    u16 owned_rigs;     // bitmask for 8 rigs
    u32 owned_lasers;   // bitmask (12 on GBA, 24 on Android)
    u16 owned_ships;    // bitmask for hull styles (bit 0 = classic, always owned)
    u8  upgrade_levels[NUM_UPGRADES]; // 0..3 for each upgrade
} GameSettings;

extern GameSettings g_settings;

#endif
