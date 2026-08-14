#include "save.h"
#include "types.h"
#include <string.h>

GameSettings g_settings;

#ifndef PLATFORM_HOST
#define SRAM_BASE ((volatile u8*)0x0E000000)
#endif
#define SAVE_MAGIC_V1 0x53554742 // 'SUGB' legacy
#define SAVE_MAGIC_V2 0x53554743 // 'SUGC' legacy
#define SAVE_MAGIC_V3 0x53554744 // 'SUGD' expanded shop & tech upgrades
#define SAVE_MAGIC_V4 0x53554745 // 'SUGE' new core upgrades + 12 lasers + 8 rigs + 5 lvls
#define SAVE_MAGIC_V5 0x53554746 // 'SUGF' + settings screen flags (tilt steer, haptics)
#define SAVE_MAGIC_V6 0x53554747 // 'SUGG' Android only: 64-bit coins (as 2 x u32)
#define SAVE_MAGIC_V7 0x53554748 // 'SUGH' Android only: 24-laser bitmask (hi word in pads)
#define SAVE_MAGIC_V8 0x53554749 // 'SUGI' Android only: hull styles (ship shop tab)

// Legacy layout V1 (20 bytes)
typedef struct {
    u32 magic;
    u8  difficulty;
    u8  music_volume;
    u8  sfx_volume;
    u8  screen_shake;
    u8  accent_index;
    u8  trail_index;
    u8  weapon_rig;
    u8  padding;
    u32 high_score;
    u32 checksum;
} SaveDataV1;

// Legacy layout V2 (32 bytes)
typedef struct {
    u32 magic;
    u8  difficulty;
    u8  music_volume;
    u8  sfx_volume;
    u8  screen_shake;
    u8  accent_index;
    u8  trail_index;
    u8  weapon_rig;
    u8  laser_index;
    u32 high_score;
    u32 coins;
    u8  owned_accents;
    u8  owned_trails;
    u8  owned_rigs;
    u8  owned_lasers;
    u8  pad0;
    u8  pad1;
    u8  pad2;
    u8  pad3;
    u32 checksum;
} SaveDataV2;

// Layout V3 (48 bytes) - 7 upgrades x3 levels
typedef struct {
    u32 magic;
    u8  difficulty;
    u8  music_volume;
    u8  sfx_volume;
    u8  screen_shake;
    u8  accent_index;
    u8  trail_index;
    u8  weapon_rig;
    u8  laser_index;
    u32 high_score;
    u32 coins;
    u16 owned_accents;
    u16 owned_trails;
    u16 owned_rigs;
    u16 owned_lasers;
    u8  upgrade_levels[7];
    u8  pad0;
    u32 pad1;
    u32 pad2;
    u32 checksum;
} SaveDataV3;

// Layout V4 (52 bytes) - 8 upgrades x5 levels, expanded rigs/lasers
typedef struct {
    u32 magic;
    u8  difficulty;
    u8  music_volume;
    u8  sfx_volume;
    u8  screen_shake;
    u8  accent_index;
    u8  trail_index;
    u8  weapon_rig;
    u8  laser_index;
    u32 high_score;
    u32 coins;
    u16 owned_accents;
    u16 owned_trails;
    u16 owned_rigs;   // up to 8 rigs in 16 bits
    u16 owned_lasers; // up to 12 lasers in 16 bits
    u8  upgrade_levels[NUM_UPGRADES]; // 8 x 0..5
    u8  pad0;
    u8  pad1;
    u8  pad2;
    u32 checksum;
} SaveDataV4;

// Layout V5 (56 bytes) - V4 + settings screen flags (tilt steer / haptics)
typedef struct {
    u32 magic;
    u8  difficulty;
    u8  music_volume;
    u8  sfx_volume;
    u8  screen_shake;
    u8  accent_index;
    u8  trail_index;
    u8  weapon_rig;
    u8  laser_index;
    u32 high_score;
    u32 coins;
    u16 owned_accents;
    u16 owned_trails;
    u16 owned_rigs;
    u16 owned_lasers;
    u8  upgrade_levels[NUM_UPGRADES]; // 8 x 0..5
    u8  tilt_steer;
    u8  haptics;
    u8  pad0;
    u8  pad1;
    u32 checksum;
} SaveDataV5;

#ifdef PLATFORM_HOST
// Layout V6 (48 bytes, Android only) - V5 with a 64-bit coin balance split
// into lo/hi u32 halves so the byte layout is identical on every compiler,
// regardless of u64 alignment rules.
typedef struct {
    u32 magic;
    u8  difficulty;
    u8  music_volume;
    u8  sfx_volume;
    u8  screen_shake;
    u8  accent_index;
    u8  trail_index;
    u8  weapon_rig;
    u8  laser_index;
    u32 high_score;
    u32 coins_lo;
    u32 coins_hi;
    u16 owned_accents;
    u16 owned_trails;
    u16 owned_rigs;
    u16 owned_lasers;
    u8  upgrade_levels[NUM_UPGRADES]; // 8 x 0..5
    u8  tilt_steer;
    u8  haptics;
    u8  pad0;
    u8  pad1;
    u32 checksum;
} SaveDataV6;

/* V7 is the same 48-byte layout as V6, but the two pad bytes hold the
 * high 16 bits of the 24-laser ownership mask. */
typedef struct {
    u32 magic;
    u8  difficulty;
    u8  music_volume;
    u8  sfx_volume;
    u8  screen_shake;
    u8  accent_index;
    u8  trail_index;
    u8  weapon_rig;
    u8  laser_index;
    u32 high_score;
    u32 coins_lo;
    u32 coins_hi;
    u16 owned_accents;
    u16 owned_trails;
    u16 owned_rigs;
    u16 owned_lasers_lo;
    u8  upgrade_levels[NUM_UPGRADES]; // 8 x 0..5
    u8  tilt_steer;
    u8  haptics;
    u16 owned_lasers_hi;
    u32 checksum;
} SaveDataV7;

/* V8 (52 bytes, Android) = V7 + equipped hull style + hull ownership mask. */
typedef struct {
    u32 magic;
    u8  difficulty;
    u8  music_volume;
    u8  sfx_volume;
    u8  screen_shake;
    u8  accent_index;
    u8  trail_index;
    u8  weapon_rig;
    u8  laser_index;
    u32 high_score;
    u32 coins_lo;
    u32 coins_hi;
    u16 owned_accents;
    u16 owned_trails;
    u16 owned_rigs;
    u16 owned_lasers_lo;
    u8  upgrade_levels[NUM_UPGRADES]; // 8 x 0..5
    u8  tilt_steer;
    u8  haptics;
    u16 owned_lasers_hi;
    u8  ship_index;
    u8  owned_ships_lo;   // hull styles 0..7 (5 today, room to grow)
    u8  pad0;
    u8  pad1;
    u32 checksum;
} SaveDataV8;

static u32 calc_checksum_v8(const SaveDataV8* data) {
    u32 sum = 0x12345678;
    const u8* bytes = (const u8*)data;
    for (u32 i = 0; i < sizeof(SaveDataV8) - sizeof(u32); i++) {
        sum = (sum * 33) ^ bytes[i];
    }
    return sum;
}
static u32 calc_checksum_v6(const SaveDataV6* data) {
    u32 sum = 0x12345678;
    const u8* bytes = (const u8*)data;
    for (u32 i = 0; i < sizeof(SaveDataV6) - sizeof(u32); i++) {
        sum = (sum * 33) ^ bytes[i];
    }
    return sum;
}
static u32 calc_checksum_v7(const SaveDataV7* data) {
    u32 sum = 0x12345678;
    const u8* bytes = (const u8*)data;
    for (u32 i = 0; i < sizeof(SaveDataV7) - sizeof(u32); i++) {
        sum = (sum * 33) ^ bytes[i];
    }
    return sum;
}
#endif // PLATFORM_HOST

static u32 calc_checksum_v5(const SaveDataV5* data) {
    u32 sum = 0x12345678;
    const u8* bytes = (const u8*)data;
    for (u32 i = 0; i < sizeof(SaveDataV5) - sizeof(u32); i++) {
        sum = (sum * 33) ^ bytes[i];
    }
    return sum;
}
static u32 calc_checksum_v4(const SaveDataV4* data) {
    u32 sum = 0x12345678;
    const u8* bytes = (const u8*)data;
    for (u32 i = 0; i < sizeof(SaveDataV4) - sizeof(u32); i++) {
        sum = (sum * 33) ^ bytes[i];
    }
    return sum;
}
static u32 calc_checksum_v3(const SaveDataV3* data) {
    u32 sum = 0x12345678;
    const u8* bytes = (const u8*)data;
    for (u32 i = 0; i < sizeof(SaveDataV3) - sizeof(u32); i++) {
        sum = (sum * 33) ^ bytes[i];
    }
    return sum;
}
static u32 calc_checksum_v2(const SaveDataV2* data) {
    u32 sum = 0x12345678;
    const u8* bytes = (const u8*)data;
    for (u32 i = 0; i < sizeof(SaveDataV2) - sizeof(u32); i++) {
        sum = (sum * 33) ^ bytes[i];
    }
    return sum;
}
static u32 calc_checksum_v1(const SaveDataV1* data) {
    u32 sum = 0x12345678;
    const u8* bytes = (const u8*)data;
    for (u32 i = 0; i < sizeof(SaveDataV1) - sizeof(u32); i++) {
        sum = (sum * 33) ^ bytes[i];
    }
    return sum;
}

void save_init_defaults(void) {
    g_settings.difficulty = DIFF_PILOT;
    g_settings.music_volume = 80;
    g_settings.sfx_volume = 80;
    g_settings.screen_shake = true;
    g_settings.tilt_steer = false; // Android: off by default
    g_settings.haptics = true;     // Android: on by default
    g_settings.accent_index = 1; // Ion Cyan free starter
    g_settings.trail_index = 1;  // Ion free starter
    g_settings.weapon_rig = WEAPON_SINGLE; // NEW: single weak starter
    g_settings.laser_index = 0; // Ion Basic weak starter
    g_settings.ship_index = SHIP_STYLE_CLASSIC; // Cyber Mk I starter hull
    g_settings.high_score = 0;
    g_settings.coins = 0;
    g_settings.owned_accents = (1 << 1); // Ion Cyan
    g_settings.owned_trails  = (1 << 1); // Ion
    g_settings.owned_rigs    = (1 << WEAPON_SINGLE); // only single
    g_settings.owned_lasers  = (1 << 0); // Ion Basic
    g_settings.owned_ships   = (1 << SHIP_STYLE_CLASSIC); // classic hull
    for (int i = 0; i < NUM_UPGRADES; i++) {
        g_settings.upgrade_levels[i] = 0;
    }
}

/* Keeps hull-style loadout sane after loading any older save layout (which
 * has no ship fields): the classic hull is always owned and always legal. */
static void repair_ship_loadout(void) {
    g_settings.owned_ships |= (1 << SHIP_STYLE_CLASSIC);
    g_settings.owned_ships &= (u16)((1 << NUM_SHIP_STYLES) - 1);
    if (g_settings.ship_index < 0 || g_settings.ship_index >= NUM_SHIP_STYLES ||
        !(g_settings.owned_ships & (1 << g_settings.ship_index))) {
        g_settings.ship_index = SHIP_STYLE_CLASSIC;
    }
}

void save_load(void) {
    save_init_defaults();

#ifdef PLATFORM_HOST
    /* Pull coins/loot/settings from filesDir/saves/save.sav if present. */
    platform_restore_save();

    // Android current format: V8 (64-bit coins + 24-laser mask + hull styles)
    SaveDataV8 d8;
    u8* dest8 = (u8*)&d8;
    for (u32 i = 0; i < sizeof(SaveDataV8); i++) dest8[i] = SRAM_BASE[i];
    if (d8.magic == SAVE_MAGIC_V8 && d8.checksum == calc_checksum_v8(&d8)) {
        if (d8.difficulty <= 2) g_settings.difficulty = (Difficulty)d8.difficulty;
        g_settings.music_volume = d8.music_volume <= 100 ? d8.music_volume : 80;
        g_settings.sfx_volume = d8.sfx_volume <= 100 ? d8.sfx_volume : 80;
        g_settings.screen_shake = (d8.screen_shake != 0);
        g_settings.tilt_steer = false; // gyro removed
        g_settings.haptics = (d8.haptics != 0);
        if (d8.accent_index < NUM_ACCENTS) g_settings.accent_index = d8.accent_index;
        if (d8.trail_index < NUM_TRAILS) g_settings.trail_index = d8.trail_index;
        if (d8.weapon_rig < NUM_RIGS) g_settings.weapon_rig = (WeaponRig)d8.weapon_rig;
        if (d8.laser_index < NUM_LASERS) g_settings.laser_index = d8.laser_index;
        g_settings.high_score = d8.high_score;
        g_settings.coins = ((coin_t)d8.coins_hi << 32) | (coin_t)d8.coins_lo;
        if (g_settings.coins > COINS_MAX) g_settings.coins = COINS_MAX;
        g_settings.owned_accents = d8.owned_accents ? d8.owned_accents : (1<<1);
        g_settings.owned_trails  = d8.owned_trails  ? d8.owned_trails  : (1<<1);
        g_settings.owned_rigs    = d8.owned_rigs    ? d8.owned_rigs    : (1<<WEAPON_SINGLE);
        g_settings.owned_lasers  = ((u32)d8.owned_lasers_hi << 16) | (u32)d8.owned_lasers_lo;
        if (g_settings.owned_lasers == 0) g_settings.owned_lasers = (1u << 0);
        g_settings.ship_index    = d8.ship_index;
        g_settings.owned_ships   = d8.owned_ships_lo ? d8.owned_ships_lo : (1<<SHIP_STYLE_CLASSIC);
        for (int i = 0; i < NUM_UPGRADES; i++) {
            int lv = d8.upgrade_levels[i];
            if (lv < 0) lv = 0;
            if (lv > UPG_MAX_LEVEL) lv = UPG_MAX_LEVEL;
            g_settings.upgrade_levels[i] = lv;
        }
        if (!(g_settings.owned_accents & (1 << g_settings.accent_index))) g_settings.accent_index = 1;
        if (!(g_settings.owned_trails & (1 << g_settings.trail_index))) g_settings.trail_index = 1;
        if (!(g_settings.owned_rigs & (1 << g_settings.weapon_rig))) g_settings.weapon_rig = WEAPON_SINGLE;
        if (!(g_settings.owned_lasers & (1u << g_settings.laser_index))) g_settings.laser_index = 0;
        repair_ship_loadout();
        return;
    }

    // Legacy V7 (64-bit coins + 24-laser mask, no hull styles)
    SaveDataV7 d7;
    u8* dest7 = (u8*)&d7;
    for (u32 i = 0; i < sizeof(SaveDataV7); i++) dest7[i] = SRAM_BASE[i];
    if (d7.magic == SAVE_MAGIC_V7 && d7.checksum == calc_checksum_v7(&d7)) {
        if (d7.difficulty <= 2) g_settings.difficulty = (Difficulty)d7.difficulty;
        g_settings.music_volume = d7.music_volume <= 100 ? d7.music_volume : 80;
        g_settings.sfx_volume = d7.sfx_volume <= 100 ? d7.sfx_volume : 80;
        g_settings.screen_shake = (d7.screen_shake != 0);
        g_settings.tilt_steer = false; // gyro removed
        g_settings.haptics = (d7.haptics != 0);
        if (d7.accent_index < NUM_ACCENTS) g_settings.accent_index = d7.accent_index;
        if (d7.trail_index < NUM_TRAILS) g_settings.trail_index = d7.trail_index;
        if (d7.weapon_rig < NUM_RIGS) g_settings.weapon_rig = (WeaponRig)d7.weapon_rig;
        if (d7.laser_index < NUM_LASERS) g_settings.laser_index = d7.laser_index;
        g_settings.high_score = d7.high_score;
        g_settings.coins = ((coin_t)d7.coins_hi << 32) | (coin_t)d7.coins_lo;
        if (g_settings.coins > COINS_MAX) g_settings.coins = COINS_MAX;
        g_settings.owned_accents = d7.owned_accents ? d7.owned_accents : (1<<1);
        g_settings.owned_trails  = d7.owned_trails  ? d7.owned_trails  : (1<<1);
        g_settings.owned_rigs    = d7.owned_rigs    ? d7.owned_rigs    : (1<<WEAPON_SINGLE);
        g_settings.owned_lasers  = ((u32)d7.owned_lasers_hi << 16) | (u32)d7.owned_lasers_lo;
        if (g_settings.owned_lasers == 0) g_settings.owned_lasers = (1u << 0);
        for (int i = 0; i < NUM_UPGRADES; i++) {
            int lv = d7.upgrade_levels[i];
            if (lv < 0) lv = 0;
            if (lv > UPG_MAX_LEVEL) lv = UPG_MAX_LEVEL;
            g_settings.upgrade_levels[i] = lv;
        }
        if (!(g_settings.owned_accents & (1 << g_settings.accent_index))) g_settings.accent_index = 1;
        if (!(g_settings.owned_trails & (1 << g_settings.trail_index))) g_settings.trail_index = 1;
        if (!(g_settings.owned_rigs & (1 << g_settings.weapon_rig))) g_settings.weapon_rig = WEAPON_SINGLE;
        if (!(g_settings.owned_lasers & (1u << g_settings.laser_index))) g_settings.laser_index = 0;
        repair_ship_loadout();
        save_write(); // upgrade V7 -> V8 (adds hull styles)
        return;
    }

    // Legacy V6 (64-bit coins, 16-bit laser mask)
    SaveDataV6 d6;
    u8* dest6 = (u8*)&d6;
    for (u32 i = 0; i < sizeof(SaveDataV6); i++) dest6[i] = SRAM_BASE[i];
    if (d6.magic == SAVE_MAGIC_V6 && d6.checksum == calc_checksum_v6(&d6)) {
        if (d6.difficulty <= 2) g_settings.difficulty = (Difficulty)d6.difficulty;
        g_settings.music_volume = d6.music_volume <= 100 ? d6.music_volume : 80;
        g_settings.sfx_volume = d6.sfx_volume <= 100 ? d6.sfx_volume : 80;
        g_settings.screen_shake = (d6.screen_shake != 0);
        g_settings.tilt_steer = false; // gyro removed
        g_settings.haptics = (d6.haptics != 0);
        if (d6.accent_index < NUM_ACCENTS) g_settings.accent_index = d6.accent_index;
        if (d6.trail_index < NUM_TRAILS) g_settings.trail_index = d6.trail_index;
        if (d6.weapon_rig < NUM_RIGS) g_settings.weapon_rig = (WeaponRig)d6.weapon_rig;
        if (d6.laser_index < NUM_LASERS) g_settings.laser_index = d6.laser_index;
        g_settings.high_score = d6.high_score;
        g_settings.coins = ((coin_t)d6.coins_hi << 32) | (coin_t)d6.coins_lo;
        if (g_settings.coins > COINS_MAX) g_settings.coins = COINS_MAX;
        g_settings.owned_accents = d6.owned_accents ? d6.owned_accents : (1<<1);
        g_settings.owned_trails  = d6.owned_trails  ? d6.owned_trails  : (1<<1);
        g_settings.owned_rigs    = d6.owned_rigs    ? d6.owned_rigs    : (1<<WEAPON_SINGLE);
        g_settings.owned_lasers  = d6.owned_lasers  ? (u32)d6.owned_lasers  : (1u<<0);
        for (int i = 0; i < NUM_UPGRADES; i++) {
            int lv = d6.upgrade_levels[i];
            if (lv < 0) lv = 0;
            if (lv > UPG_MAX_LEVEL) lv = UPG_MAX_LEVEL;
            g_settings.upgrade_levels[i] = lv;
        }
        if (!(g_settings.owned_accents & (1 << g_settings.accent_index))) g_settings.accent_index = 1;
        if (!(g_settings.owned_trails & (1 << g_settings.trail_index))) g_settings.trail_index = 1;
        if (!(g_settings.owned_rigs & (1 << g_settings.weapon_rig))) g_settings.weapon_rig = WEAPON_SINGLE;
        if (!(g_settings.owned_lasers & (1u << g_settings.laser_index))) g_settings.laser_index = 0;
        repair_ship_loadout();
        save_write(); // upgrade V6 -> V8
        return;
    }
#endif

    // Try V5
    SaveDataV5 d5;
    u8* dest5 = (u8*)&d5;
    for (u32 i = 0; i < sizeof(SaveDataV5); i++) dest5[i] = SRAM_BASE[i];
    if (d5.magic == SAVE_MAGIC_V5 && d5.checksum == calc_checksum_v5(&d5)) {
        if (d5.difficulty <= 2) g_settings.difficulty = (Difficulty)d5.difficulty;
        g_settings.music_volume = d5.music_volume <= 100 ? d5.music_volume : 80;
        g_settings.sfx_volume = d5.sfx_volume <= 100 ? d5.sfx_volume : 80;
        g_settings.screen_shake = (d5.screen_shake != 0);
        g_settings.tilt_steer = false; // gyro removed
        g_settings.haptics = (d5.haptics != 0);
        if (d5.accent_index < NUM_ACCENTS) g_settings.accent_index = d5.accent_index;
        if (d5.trail_index < NUM_TRAILS) g_settings.trail_index = d5.trail_index;
        if (d5.weapon_rig < NUM_RIGS) g_settings.weapon_rig = (WeaponRig)d5.weapon_rig;
        if (d5.laser_index < NUM_LASERS) g_settings.laser_index = d5.laser_index;
        g_settings.high_score = d5.high_score;
        g_settings.coins = d5.coins;
        g_settings.owned_accents = d5.owned_accents ? d5.owned_accents : (1<<1);
        g_settings.owned_trails  = d5.owned_trails  ? d5.owned_trails  : (1<<1);
        g_settings.owned_rigs    = d5.owned_rigs    ? d5.owned_rigs    : (1<<WEAPON_SINGLE);
        g_settings.owned_lasers  = d5.owned_lasers  ? d5.owned_lasers  : (1<<0);
        for (int i = 0; i < NUM_UPGRADES; i++) {
            int lv = d5.upgrade_levels[i];
            if (lv < 0) lv = 0;
            if (lv > UPG_MAX_LEVEL) lv = UPG_MAX_LEVEL;
            g_settings.upgrade_levels[i] = lv;
        }
        if (!(g_settings.owned_accents & (1 << g_settings.accent_index))) g_settings.accent_index = 1;
        if (!(g_settings.owned_trails & (1 << g_settings.trail_index))) g_settings.trail_index = 1;
        if (!(g_settings.owned_rigs & (1 << g_settings.weapon_rig))) g_settings.weapon_rig = WEAPON_SINGLE;
        if (!(g_settings.owned_lasers & (1 << g_settings.laser_index))) g_settings.laser_index = 0;
        /* Hull styles ride along in the V5 pad bytes on real GBA hardware
         * (magic stays V5 so old ROMs still accept the SRAM image, and old
         * ROMs' zeroed pads translate to "classic hull owned"). */
        g_settings.ship_index  = d5.pad0;
        g_settings.owned_ships = d5.pad1 ? d5.pad1 : (1 << SHIP_STYLE_CLASSIC);
        repair_ship_loadout();
#ifdef PLATFORM_HOST
        save_write(); // Android: upgrade a legacy V5 blob to V8
#endif
        return;
    }

    // Try V4 -> migrate to V5
    SaveDataV4 d4;
    u8* dest4 = (u8*)&d4;
    for (u32 i = 0; i < sizeof(SaveDataV4); i++) dest4[i] = SRAM_BASE[i];
    if (d4.magic == SAVE_MAGIC_V4 && d4.checksum == calc_checksum_v4(&d4)) {
        if (d4.difficulty <= 2) g_settings.difficulty = (Difficulty)d4.difficulty;
        g_settings.music_volume = d4.music_volume <= 100 ? d4.music_volume : 80;
        g_settings.sfx_volume = d4.sfx_volume <= 100 ? d4.sfx_volume : 80;
        g_settings.screen_shake = (d4.screen_shake != 0);
        if (d4.accent_index < NUM_ACCENTS) g_settings.accent_index = d4.accent_index;
        if (d4.trail_index < NUM_TRAILS) g_settings.trail_index = d4.trail_index;
        if (d4.weapon_rig < NUM_RIGS) g_settings.weapon_rig = (WeaponRig)d4.weapon_rig;
        if (d4.laser_index < NUM_LASERS) g_settings.laser_index = d4.laser_index;
        g_settings.high_score = d4.high_score;
        g_settings.coins = d4.coins;
        g_settings.owned_accents = d4.owned_accents ? d4.owned_accents : (1<<1);
        g_settings.owned_trails  = d4.owned_trails  ? d4.owned_trails  : (1<<1);
        g_settings.owned_rigs    = d4.owned_rigs    ? d4.owned_rigs    : (1<<WEAPON_SINGLE);
        g_settings.owned_lasers  = d4.owned_lasers  ? d4.owned_lasers  : (1<<0);
        for (int i = 0; i < NUM_UPGRADES; i++) {
            int lv = d4.upgrade_levels[i];
            if (lv < 0) lv = 0;
            if (lv > UPG_MAX_LEVEL) lv = UPG_MAX_LEVEL;
            g_settings.upgrade_levels[i] = lv;
        }
        if (!(g_settings.owned_accents & (1 << g_settings.accent_index))) g_settings.accent_index = 1;
        if (!(g_settings.owned_trails & (1 << g_settings.trail_index))) g_settings.trail_index = 1;
        if (!(g_settings.owned_rigs & (1 << g_settings.weapon_rig))) g_settings.weapon_rig = WEAPON_SINGLE;
        if (!(g_settings.owned_lasers & (1 << g_settings.laser_index))) g_settings.laser_index = 0;
        repair_ship_loadout();
        save_write(); // upgrade to V5
        return;
    }

    // Fallback V3 -> migrate
    SaveDataV3 d3;
    u8* dest3 = (u8*)&d3;
    for (u32 i = 0; i < sizeof(SaveDataV3); i++) dest3[i] = SRAM_BASE[i];
    if (d3.magic == SAVE_MAGIC_V3 && d3.checksum == calc_checksum_v3(&d3)) {
        if (d3.difficulty <= 2) g_settings.difficulty = (Difficulty)d3.difficulty;
        g_settings.music_volume = d3.music_volume <= 100 ? d3.music_volume : 80;
        g_settings.sfx_volume = d3.sfx_volume <= 100 ? d3.sfx_volume : 80;
        g_settings.screen_shake = (d3.screen_shake != 0);
        if (d3.accent_index < NUM_ACCENTS) g_settings.accent_index = d3.accent_index;
        if (d3.trail_index < NUM_TRAILS) g_settings.trail_index = d3.trail_index;

        // Weapon rig migration: old 0..5 -> new 1..6
        int old_rig = d3.weapon_rig;
        if (old_rig >= 0 && old_rig <= 5) {
            g_settings.weapon_rig = (WeaponRig)(old_rig + 1);
        }
        if (d3.laser_index < 8) g_settings.laser_index = d3.laser_index;

        g_settings.high_score = d3.high_score;
        g_settings.coins = d3.coins;
        g_settings.owned_accents = d3.owned_accents ? d3.owned_accents : (1<<1);
        g_settings.owned_trails  = d3.owned_trails  ? d3.owned_trails  : (1<<1);
        // Owned rigs shift by 1
        g_settings.owned_rigs = (1 << WEAPON_SINGLE);
        for (int i = 0; i < 6; i++) {
            if (d3.owned_rigs & (1 << i)) {
                g_settings.owned_rigs |= (1 << (i + 1));
            }
        }
        g_settings.owned_lasers  = d3.owned_lasers  ? d3.owned_lasers  : (1<<0);

        // Upgrade levels migration
        // Old mapping: 0 shield,1 hull,2 thrusters,3 scavenger,4 damage,5 overdrive,6 combo
        // New: 0 engine,1 fire_rate,2 damage,3 shield,4 hull,5 dash,6 scavenger,7 overdrive
        int old_lv[7];
        for (int i = 0; i < 7; i++) old_lv[i] = (d3.upgrade_levels[i] <= 3 ? d3.upgrade_levels[i] : 0);

        g_settings.upgrade_levels[UPG_SHIELD] = old_lv[0] > UPG_MAX_LEVEL ? UPG_MAX_LEVEL : old_lv[0];
        g_settings.upgrade_levels[UPG_HULL] = old_lv[1] > UPG_MAX_LEVEL ? UPG_MAX_LEVEL : old_lv[1];
        g_settings.upgrade_levels[UPG_ENGINE] = old_lv[2] > UPG_MAX_LEVEL ? UPG_MAX_LEVEL : old_lv[2];
        g_settings.upgrade_levels[UPG_DASH] = old_lv[2] > UPG_MAX_LEVEL ? UPG_MAX_LEVEL : old_lv[2];
        g_settings.upgrade_levels[UPG_SCAVENGER] = old_lv[3] > UPG_MAX_LEVEL ? UPG_MAX_LEVEL : old_lv[3];
        g_settings.upgrade_levels[UPG_DAMAGE] = old_lv[4] > UPG_MAX_LEVEL ? UPG_MAX_LEVEL : old_lv[4];
        g_settings.upgrade_levels[UPG_OVERDRIVE] = old_lv[5] > UPG_MAX_LEVEL ? UPG_MAX_LEVEL : old_lv[5];
        g_settings.upgrade_levels[UPG_FIRE_RATE] = old_lv[6] > UPG_MAX_LEVEL ? UPG_MAX_LEVEL : old_lv[6];

        repair_ship_loadout();
        save_write();
        return;
    }

    // V2 fallback
    SaveDataV2 d2;
    u8* dest2 = (u8*)&d2;
    for (u32 i = 0; i < sizeof(SaveDataV2); i++) dest2[i] = SRAM_BASE[i];
    if (d2.magic == SAVE_MAGIC_V2 && d2.checksum == calc_checksum_v2(&d2)) {
        if (d2.difficulty <= 2) g_settings.difficulty = (Difficulty)d2.difficulty;
        g_settings.music_volume = d2.music_volume <= 100 ? d2.music_volume : 80;
        g_settings.sfx_volume = d2.sfx_volume <= 100 ? d2.sfx_volume : 80;
        g_settings.screen_shake = (d2.screen_shake != 0);
        if (d2.accent_index < NUM_ACCENTS) g_settings.accent_index = d2.accent_index;
        if (d2.trail_index < NUM_TRAILS) g_settings.trail_index = d2.trail_index;
        g_settings.high_score = d2.high_score;
        g_settings.coins = d2.coins;
        g_settings.owned_accents = d2.owned_accents ? d2.owned_accents : (1<<1);
        g_settings.owned_trails  = d2.owned_trails  ? d2.owned_trails  : (1<<1);
        g_settings.owned_rigs = (1 << WEAPON_SINGLE);
        for (int i = 0; i < 6; i++) {
            if (d2.owned_rigs & (1 << i)) {
                g_settings.owned_rigs |= (1 << (i+1));
            }
        }
        g_settings.owned_lasers  = d2.owned_lasers  ? d2.owned_lasers  : (1<<0);
        repair_ship_loadout();
        save_write();
        return;
    }

    // V1 fallback
    SaveDataV1 d1;
    u8* dest1 = (u8*)&d1;
    for (u32 i = 0; i < sizeof(SaveDataV1); i++) dest1[i] = SRAM_BASE[i];
    if (d1.magic == SAVE_MAGIC_V1 && d1.checksum == calc_checksum_v1(&d1)) {
        if (d1.difficulty <= 2) g_settings.difficulty = (Difficulty)d1.difficulty;
        g_settings.music_volume = d1.music_volume <= 100 ? d1.music_volume : 80;
        g_settings.sfx_volume = d1.sfx_volume <= 100 ? d1.sfx_volume : 80;
        g_settings.screen_shake = (d1.screen_shake != 0);
        if (d1.accent_index < NUM_ACCENTS) g_settings.accent_index = d1.accent_index;
        if (d1.trail_index < NUM_TRAILS) g_settings.trail_index = d1.trail_index;
        g_settings.high_score = d1.high_score;
        g_settings.owned_accents |= (1 << g_settings.accent_index);
        g_settings.owned_trails  |= (1 << g_settings.trail_index);
        g_settings.owned_rigs    |= (1 << WEAPON_SINGLE);
        if (g_settings.coins == 0 && g_settings.high_score > 0) g_settings.coins = 250;
        repair_ship_loadout();
        save_write();
    }
}

void save_write(void) {
    repair_ship_loadout();
#ifdef PLATFORM_HOST
    // Android: V8 with 64-bit coins, 32-bit laser mask, and hull styles.
    SaveDataV8 data;
    memset(&data, 0, sizeof(SaveDataV8));
    data.magic = SAVE_MAGIC_V8;
    data.difficulty = (u8)g_settings.difficulty;
    data.music_volume = (u8)g_settings.music_volume;
    data.sfx_volume = (u8)g_settings.sfx_volume;
    data.screen_shake = g_settings.screen_shake ? 1 : 0;
    data.tilt_steer = g_settings.tilt_steer ? 1 : 0;
    data.haptics = g_settings.haptics ? 1 : 0;
    data.accent_index = (u8)g_settings.accent_index;
    data.trail_index = (u8)g_settings.trail_index;
    data.weapon_rig = (u8)g_settings.weapon_rig;
    data.laser_index = (u8)g_settings.laser_index;
    data.high_score = g_settings.high_score;
    data.coins_lo = (u32)(g_settings.coins & 0xFFFFFFFFu);
    data.coins_hi = (u32)((g_settings.coins >> 32) & 0xFFFFFFFFu);
    data.owned_accents = g_settings.owned_accents;
    data.owned_trails  = g_settings.owned_trails;
    data.owned_rigs    = g_settings.owned_rigs;
    data.owned_lasers_lo = (u16)(g_settings.owned_lasers & 0xFFFFu);
    data.owned_lasers_hi = (u16)((g_settings.owned_lasers >> 16) & 0xFFFFu);
    data.ship_index = (u8)g_settings.ship_index;
    data.owned_ships_lo = (u8)(g_settings.owned_ships & 0xFF);
    for (int i = 0; i < NUM_UPGRADES; i++) {
        data.upgrade_levels[i] = g_settings.upgrade_levels[i];
    }
    data.checksum = calc_checksum_v8(&data);

    const u8* src = (const u8*)&data;
    for (u32 i = 0; i < sizeof(SaveDataV8); i++) {
        SRAM_BASE[i] = src[i];
    }
    platform_persist_save();
#else
    SaveDataV5 data;
    memset(&data, 0, sizeof(SaveDataV5));
    data.magic = SAVE_MAGIC_V5;
    data.difficulty = (u8)g_settings.difficulty;
    data.music_volume = (u8)g_settings.music_volume;
    data.sfx_volume = (u8)g_settings.sfx_volume;
    data.screen_shake = g_settings.screen_shake ? 1 : 0;
    data.tilt_steer = g_settings.tilt_steer ? 1 : 0;
    data.haptics = g_settings.haptics ? 1 : 0;
    data.accent_index = (u8)g_settings.accent_index;
    data.trail_index = (u8)g_settings.trail_index;
    data.weapon_rig = (u8)g_settings.weapon_rig;
    data.laser_index = (u8)g_settings.laser_index;
    data.high_score = g_settings.high_score;
    data.coins = g_settings.coins;
    data.owned_accents = g_settings.owned_accents;
    data.owned_trails  = g_settings.owned_trails;
    data.owned_rigs    = g_settings.owned_rigs;
    data.owned_lasers  = g_settings.owned_lasers;
    data.pad0 = (u8)g_settings.ship_index;             // hull style (V5 pads)
    data.pad1 = (u8)(g_settings.owned_ships & 0xFF);   // owned hull styles
    for (int i = 0; i < NUM_UPGRADES; i++) {
        data.upgrade_levels[i] = g_settings.upgrade_levels[i];
    }
    data.checksum = calc_checksum_v5(&data);

    const u8* src = (const u8*)&data;
    for (u32 i = 0; i < sizeof(SaveDataV5); i++) {
        SRAM_BASE[i] = src[i];
    }
#endif
}

#ifdef PLATFORM_HOST
/* Append an unsigned value as plain decimal. Returns the new position. */
static int fmt_append_u32(char* dst, int dst_cap, int out, unsigned v) {
    char tmp[10]; int n = 0;
    do { tmp[n++] = (char)('0' + (v % 10)); v /= 10; } while (v);
    while (n > 0 && out < dst_cap - 1) dst[out++] = tmp[--n];
    return out;
}

/* "999T", "12.5T", "2.4B" style abbreviations for huge Android balances. */
static void fmt_magnitude(char* dst, int dst_cap, coin_t c, coin_t unit, char suffix) {
    unsigned whole = (unsigned)(c / unit);
    unsigned tenths = (unsigned)((c % unit) / (unit / 10));
    int out = 0;
    out = fmt_append_u32(dst, dst_cap, out, whole);
    if (tenths && whole < 100 && out < dst_cap - 2) {
        dst[out++] = '.';
        dst[out++] = (char)('0' + tenths);
    }
    if (out < dst_cap - 1) dst[out++] = suffix;
    dst[out] = '\0';
}
#endif

void save_format_coins(char* dst, int dst_cap) {
    if (!dst || dst_cap < 2) return;
    dst[0] = '\0';
    coin_t c = g_settings.coins;
    if (c > COINS_MAX) c = COINS_MAX;

#ifdef PLATFORM_HOST
    /* Android balances can reach 999 trillion (cheat codes), far wider than
     * any HUD slot — shorten the giant magnitudes. */
    if (c >= 1000000000000ULL) { fmt_magnitude(dst, dst_cap, c, 1000000000000ULL, 'T'); return; }
    if (c >= 1000000000ULL)    { fmt_magnitude(dst, dst_cap, c, 1000000000ULL,    'B'); return; }
#endif

    /* Grouped decimal: 1,234,567 */
    char digits[24]; int n = 0;
    do { digits[n++] = (char)('0' + (int)(c % 10u)); c /= 10u; } while (c > 0 && n < (int)sizeof(digits));
    int out = 0;
    while (n > 0 && out < dst_cap - 1) {
        dst[out++] = digits[--n];
        if (n > 0 && (n % 3) == 0 && out < dst_cap - 1) dst[out++] = ',';
    }
    dst[out] = '\0';
}

#ifdef PLATFORM_HOST
void save_reset_all(void) {
    save_init_defaults();
    save_write();
}

/* ── Cheat codes (Android only) ─────────────────────────────────────────── */
static int cheat_matches(const char* code, const char* want) {
    if (!code) return 0;
    int i = 0;
    while (want[i]) {
        char c = code[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c != want[i]) return 0;
        i++;
    }
    return code[i] == '\0';
}

int save_apply_cheat(const char* code) {
    if (cheat_matches(code, "GIMMEMONEY")) {
        g_settings.coins = COINS_MAX; // 999,000,000,000,000
        save_write();
        return 1;
    }
    return 0;
}
#endif

// ── Shop Pricing ────────────────────────────────────────────────────────
int shop_get_accent_price(int idx) {
#ifdef PLATFORM_HOST
    // Android: cosmetics are pricier so they feel like a reward.
    switch (idx) {
        case 0: return 1500;
        case 1: return 0;
        case 2: return 5500;
        case 3: return 12000;
        case 4: return 28000;
        case 5: return 60000;
        case 6: return 130000;
        case 7: return 260000;
        case 8: return 1500000;
        default: return 9999999;
    }
#else
    switch (idx) {
        case 0: return 800;     // Solar Orange
        case 1: return 0;       // Ion Cyan (Starter)
        case 2: return 2500;    // Nova Violet
        case 3: return 5500;    // Plasma Mint
        case 4: return 14000;   // Pulsar Gold
        case 5: return 30000;   // Crimson Void
        case 6: return 65000;   // Obsidian Shadow
        case 7: return 120000;  // Quantum Neon
        case 8: return 1000000; // Rainbow Prism
        default: return 9999999;
    }
#endif
}

int shop_get_trail_price(int idx) {
#ifdef PLATFORM_HOST
    switch (idx) {
        case 0: return 1800;
        case 1: return 0;
        case 2: return 6000;
        case 3: return 14000;
        case 4: return 32000;
        case 5: return 70000;
        case 6: return 140000;
        case 7: return 280000;
        default: return 999999;
    }
#else
    switch (idx) {
        case 0: return 1000;    // Ember Fire
        case 1: return 0;       // Ion Cyan (Starter)
        case 2: return 3200;    // Nova Purple
        case 3: return 7000;    // Aurora Mint
        case 4: return 16000;   // Solar Gold
        case 5: return 35000;   // Crimson Flame
        case 6: return 70000;   // Void Shadow
        case 7: return 130000;  // Rainbow Trail
        default: return 999999;
    }
#endif
}

int shop_get_rig_price(WeaponRig rig) {
#ifdef PLATFORM_HOST
    // Android: weapons are an investment. Early cheap, endgame costs real grind.
    switch (rig) {
        case WEAPON_SINGLE:  return 0;        // starter
        case WEAPON_TWIN:    return 1200;
        case WEAPON_SPREAD:  return 5500;
        case WEAPON_FOCUSED: return 14000;
        case WEAPON_TRIPLE:  return 32000;
        case WEAPON_PLASMA:  return 70000;
        case WEAPON_QUANTUM: return 150000;
        case WEAPON_NOVA:    return 400000;
        default: return 999999;
    }
#else
    switch (rig) {
        case WEAPON_SINGLE:  return 0;       // Single weak starter
        case WEAPON_TWIN:    return 500;     // Twin
        case WEAPON_SPREAD:  return 2500;    // Spread
        case WEAPON_FOCUSED: return 6500;    // Focused
        case WEAPON_TRIPLE:  return 14000;   // Triple
        case WEAPON_PLASMA:  return 30000;   // Plasma
        case WEAPON_QUANTUM: return 65000;   // Quantum
        case WEAPON_NOVA:    return 150000;  // Nova final god
        default: return 999999;
    }
#endif
}

int shop_get_laser_price(int idx) {
#ifdef PLATFORM_HOST
    /* Strictly more expensive as the index climbs. Early crystals are a
     * real investment; the last few are late-game flex buys. */
    switch (idx) {
        case 0:  return 0;
        case 1:  return 4000;
        case 2:  return 9000;
        case 3:  return 18000;
        case 4:  return 36000;
        case 5:  return 70000;
        case 6:  return 120000;
        case 7:  return 190000;
        case 8:  return 280000;
        case 9:  return 400000;
        case 10: return 560000;
        case 11: return 780000;
        case 12: return 1100000;
        case 13: return 1500000;
        case 14: return 2100000;
        case 15: return 2900000;
        case 16: return 4000000;
        case 17: return 5500000;
        case 18: return 7600000;
        case 19: return 10500000;
        case 20: return 14500000;
        case 21: return 20000000;
        case 22: return 28000000;
        case 23: return 40000000;
        default: return 99999999;
    }
#else
    switch (idx) {
        case 0: return 0;       // Ion Basic starter weak
        case 1: return 1200;    // Solar Gold
        case 2: return 3500;    // Nebula Violet
        case 3: return 7500;    // Toxic Mint
        case 4: return 15000;   // Crimson Fury
        case 5: return 30000;   // Emerald Surge
        case 6: return 60000;   // Void Shadow
        case 7: return 100000;  // Rainbow Laser
        case 8: return 40000;   // Inferno Red alt path
        case 9: return 70000;   // Frost Blue
        case 10: return 110000; // Photon Gold
        case 11: return 250000; // Omega Prism final god
        default: return 999999;
    }
#endif
}

// Upgrade pricing: 5 levels (0->1,1->2,2->3,3->4,4->5)
int shop_get_upgrade_price(UpgradeType upg, int level) {
    if (level < 0 || level >= UPG_MAX_LEVEL) return 999999;
#ifdef PLATFORM_HOST
    // Android: upgrades are noticeably more expensive, matching the slower
    // early-game damage progression so players can't buy the win too fast.
    switch (upg) {
        case UPG_ENGINE: {
            const int p[5] = { 1500, 5000, 14000, 36000, 80000 };
            return p[level];
        }
        case UPG_FIRE_RATE: {
            const int p[5] = { 2000, 6000, 16000, 40000, 90000 };
            return p[level];
        }
        case UPG_DAMAGE: {
            const int p[5] = { 2500, 7500, 20000, 50000, 110000 };
            return p[level];
        }
        case UPG_SHIELD: {
            const int p[5] = { 2000, 6000, 16000, 42000, 95000 };
            return p[level];
        }
        case UPG_HULL: {
            const int p[5] = { 2000, 6000, 16000, 42000, 95000 };
            return p[level];
        }
        case UPG_DASH: {
            const int p[5] = { 1800, 5500, 15000, 38000, 85000 };
            return p[level];
        }
        case UPG_SCAVENGER: {
            // Coin multiplier upgrade (magnet removed on Android)
            const int p[5] = { 1500, 5000, 14000, 36000, 80000 };
            return p[level];
        }
        case UPG_OVERDRIVE: {
            const int p[5] = { 1800, 5500, 14000, 36000, 80000 };
            return p[level];
        }
        default: return 999999;
    }
#else
    switch (upg) {
        case UPG_ENGINE: {
            const int p[5] = { 800, 2500, 7000, 18000, 40000 };
            return p[level];
        }
        case UPG_FIRE_RATE: {
            const int p[5] = { 1000, 3000, 8000, 20000, 45000 };
            return p[level];
        }
        case UPG_DAMAGE: {
            const int p[5] = { 1500, 4000, 10000, 25000, 55000 };
            return p[level];
        }
        case UPG_SHIELD: {
            const int p[5] = { 1200, 3500, 9000, 22000, 50000 };
            return p[level];
        }
        case UPG_HULL: {
            const int p[5] = { 1200, 3500, 9000, 22000, 50000 };
            return p[level];
        }
        case UPG_DASH: {
            const int p[5] = { 800, 2500, 7000, 18000, 40000 };
            return p[level];
        }
        case UPG_SCAVENGER: {
            const int p[5] = { 1000, 3000, 8000, 20000, 45000 };
            return p[level];
        }
        case UPG_OVERDRIVE: {
            const int p[5] = { 1000, 3000, 7500, 18000, 40000 };
            return p[level];
        }
        default: return 999999;
    }
#endif
}

bool shop_is_accent_owned(int idx) {
    if (idx < 0 || idx >= NUM_ACCENTS) return false;
    return (g_settings.owned_accents & (1 << idx)) != 0;
}

bool shop_is_trail_owned(int idx) {
    if (idx < 0 || idx >= NUM_TRAILS) return false;
    return (g_settings.owned_trails & (1 << idx)) != 0;
}

bool shop_is_rig_owned(WeaponRig rig) {
    if (rig < 0 || rig >= NUM_RIGS) return false;
    return (g_settings.owned_rigs & (1 << rig)) != 0;
}

bool shop_is_laser_owned(int idx) {
    if (idx < 0 || idx >= NUM_LASERS) return false;
    return (g_settings.owned_lasers & (1u << idx)) != 0;
}

int shop_get_upgrade_level(UpgradeType upg) {
    if (upg < 0 || upg >= NUM_UPGRADES) return 0;
    return g_settings.upgrade_levels[upg];
}

void shop_equip_accent(int idx) {
    if (shop_is_accent_owned(idx)) {
        g_settings.accent_index = idx;
        save_write();
    }
}

void shop_equip_trail(int idx) {
    if (shop_is_trail_owned(idx)) {
        g_settings.trail_index = idx;
        save_write();
    }
}

void shop_equip_rig(WeaponRig rig) {
    if (shop_is_rig_owned(rig)) {
        g_settings.weapon_rig = rig;
        save_write();
    }
}

void shop_equip_laser(int idx) {
    if (shop_is_laser_owned(idx)) {
        g_settings.laser_index = idx;
        save_write();
    }
}

bool shop_try_purchase_accent(int idx) {
    if (shop_is_accent_owned(idx)) { shop_equip_accent(idx); return true; }
    int price = shop_get_accent_price(idx);
    if (g_settings.coins < (coin_t)price) return false;
    g_settings.coins -= price;
    g_settings.owned_accents |= (1 << idx);
    g_settings.accent_index = idx;
    save_write();
    return true;
}

bool shop_try_purchase_trail(int idx) {
    if (shop_is_trail_owned(idx)) { shop_equip_trail(idx); return true; }
    int price = shop_get_trail_price(idx);
    if (g_settings.coins < (coin_t)price) return false;
    g_settings.coins -= price;
    g_settings.owned_trails |= (1 << idx);
    g_settings.trail_index = idx;
    save_write();
    return true;
}

bool shop_try_purchase_rig(WeaponRig rig) {
    if (shop_is_rig_owned(rig)) { shop_equip_rig(rig); return true; }
    int price = shop_get_rig_price(rig);
    if (g_settings.coins < (coin_t)price) return false;
    g_settings.coins -= price;
    g_settings.owned_rigs |= (1 << rig);
    g_settings.weapon_rig = rig;
    save_write();
    return true;
}

bool shop_try_purchase_laser(int idx) {
    if (shop_is_laser_owned(idx)) { shop_equip_laser(idx); return true; }
    int price = shop_get_laser_price(idx);
    if (g_settings.coins < (coin_t)price) return false;
    g_settings.coins -= price;
    g_settings.owned_lasers |= (1 << idx);
    g_settings.laser_index = idx;
    save_write();
    return true;
}

// ── Hull styles (SHIPS tab) ──────────────────────────────────────────────
int shop_get_ship_price(int idx) {
    /* Priced like mid-tier weapon rigs: a visible-unlock every few runs
     * early on, and the flagship as a long-term savings goal. */
#ifdef PLATFORM_HOST
    switch (idx) {
        case 0: return 0;        // Cyber Mk I (starter)
        case 1: return 4000;     // Viper Mk II
        case 2: return 15000;    // Manta Ray
        case 3: return 60000;    // Aegis Titan
        case 4: return 240000;   // Phoenix MkX
        default: return 9999999;
    }
#else
    switch (idx) {
        case 0: return 0;        // Cyber Mk I (starter)
        case 1: return 2000;     // Viper Mk II
        case 2: return 8000;     // Manta Ray
        case 3: return 30000;    // Aegis Titan
        case 4: return 120000;   // Phoenix MkX
        default: return 9999999;
    }
#endif
}

bool shop_is_ship_owned(int idx) {
    if (idx < 0 || idx >= NUM_SHIP_STYLES) return false;
    return (g_settings.owned_ships & (1 << idx)) != 0;
}

void shop_equip_ship(int idx) {
    if (shop_is_ship_owned(idx)) {
        g_settings.ship_index = idx;
        save_write();
    }
}

bool shop_try_purchase_ship(int idx) {
    if (shop_is_ship_owned(idx)) { shop_equip_ship(idx); return true; }
    int price = shop_get_ship_price(idx);
    if (g_settings.coins < (coin_t)price) return false;
    g_settings.coins -= price;
    g_settings.owned_ships |= (1 << idx);
    g_settings.ship_index = idx;
    save_write();
    return true;
}

bool shop_try_purchase_upgrade(UpgradeType upg) {
    if (upg < 0 || upg >= NUM_UPGRADES) return false;
    int current_lv = g_settings.upgrade_levels[upg];
    if (current_lv >= UPG_MAX_LEVEL) return false;
    int price = shop_get_upgrade_price(upg, current_lv);
    if (g_settings.coins < (coin_t)price) return false;
    g_settings.coins -= price;
    g_settings.upgrade_levels[upg]++;
    save_write();
    return true;
}

const char* shop_get_upgrade_name(UpgradeType upg) {
    switch (upg) {
        case UPG_ENGINE:    return "Speed";
        case UPG_FIRE_RATE: return "Fire Rate";
        case UPG_DAMAGE:    return "Damage";
        case UPG_SHIELD:    return "Shield";
        case UPG_HULL:      return "Lives";
        case UPG_DASH:      return "Beam";
        case UPG_SCAVENGER: return "Coins";
        case UPG_OVERDRIVE: return "Rapid";
        default:            return "Upgrade";
    }
}

const char* shop_get_upgrade_desc_line1(UpgradeType upg) {
    switch (upg) {
        case UPG_ENGINE:    return "Move speed 0.7->2x";
#ifdef PLATFORM_HOST
        case UPG_FIRE_RATE: return "Only way to shoot faster";
        case UPG_DAMAGE:    return "+1 damage per lvl";
        case UPG_SHIELD:    return "More shield slots";
        case UPG_HULL:      return "+1 life per lvl";
        case UPG_DASH:      return "+Beam damage / lvl";
        case UPG_SCAVENGER: return "Coin multiplier only";
        case UPG_OVERDRIVE: return "Longer rapid fire";
#else
        case UPG_FIRE_RATE: return "2/sec -> 10/sec";
        case UPG_DAMAGE:    return "+1 damage per lvl";
        case UPG_SHIELD:    return "More shield slots";
        case UPG_HULL:      return "+1 life per lvl";
        case UPG_DASH:      return "+Beam damage / lvl";
        case UPG_SCAVENGER: return "More coins + magnet";
        case UPG_OVERDRIVE: return "Longer rapid fire";
#endif
        default:            return "";
    }
}

const char* shop_get_upgrade_desc_line2(UpgradeType upg, int level) {
    switch (upg) {
        case UPG_ENGINE:
            if (level == 0) return "Lv0 Slow 0.7x";
            if (level == 1) return "Lv1 0.85x speed";
            if (level == 2) return "Lv2 1.0x normal";
            if (level == 3) return "Lv3 1.2x fast";
            if (level == 4) return "Lv4 1.56x v-fast";
            return "MAX 2.0x Speed!";
        case UPG_FIRE_RATE:
#ifdef PLATFORM_HOST
            if (level == 0) return "Lv0 ~2/sec";
            if (level == 1) return "Lv1 ~2.5/sec";
            if (level == 2) return "Lv2 ~3/sec";
            if (level == 3) return "Lv3 ~3.7/sec";
            if (level == 4) return "Lv4 ~4.5/sec";
            return "MAX ~5.7/sec!";
#else
            if (level == 0) return "Lv0 2 shots/sec";
            if (level == 1) return "Lv1 ~3/sec";
            if (level == 2) return "Lv2 ~4/sec";
            if (level == 3) return "Lv3 ~6/sec";
            if (level == 4) return "Lv4 ~8/sec";
            return "MAX 10+/sec!";
#endif
        case UPG_DAMAGE:
            if (level == 0) return "+0 dmg base";
            if (level == 1) return "+1 all weapons";
            if (level == 2) return "+2 heavy dmg";
            if (level == 3) return "+3 god mode";
            if (level == 4) return "+4 pierce boost";
            return "MAX +5 devastation";
        case UPG_SHIELD:
            if (level == 0) return "0 start 2 max";
            if (level == 1) return "1 start 3 max";
            if (level == 2) return "1 start 4 max";
            if (level == 3) return "2 start 5 max";
            if (level == 4) return "2 start 6 max";
            return "MAX 3 start 6 cap";
        case UPG_HULL:
            if (level == 0) return "2 lives only!";
            if (level == 1) return "3 lives normal";
            if (level == 2) return "4 lives sturdy";
            if (level == 3) return "5 lives tank";
            if (level == 4) return "6 lives beast";
            return "MAX 7 lives";
        case UPG_DASH:
            if (level == 0) return "Beam dmg 1.00x";
            if (level == 1) return "Beam dmg 1.25x";
            if (level == 2) return "Beam dmg 1.50x";
            if (level == 3) return "Beam dmg 1.75x";
            if (level == 4) return "Beam dmg 2.00x";
            return "MAX Beam dmg 2.25x";
        case UPG_SCAVENGER:
#ifdef PLATFORM_HOST
            if (level == 0) return "1x coins";
            if (level == 1) return "1.35x coins";
            if (level == 2) return "1.70x coins";
            if (level == 3) return "2.05x coins";
            if (level == 4) return "2.40x coins";
            return "MAX 2.75x coins";
#else
            if (level == 0) return "No magnet 1x $";
            if (level == 1) return "+35% $ small mag";
            if (level == 2) return "+70% $ med mag";
            if (level == 3) return "+105% $ big mag";
            if (level == 4) return "+140% $ huge mag";
            return "MAX +175% & pull";
#endif
        case UPG_OVERDRIVE:
            if (level == 0) return "Rapid 8 sec";
            if (level == 1) return "Rapid 11 sec";
            if (level == 2) return "Rapid 14 sec";
            if (level == 3) return "Rapid 18 sec";
            if (level == 4) return "Rapid 22 sec";
            return "MAX Rapid 26 sec";
        default:
            return "";
    }
}
