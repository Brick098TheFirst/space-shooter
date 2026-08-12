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
    g_settings.accent_index = 1; // Ion Cyan free starter
    g_settings.trail_index = 1;  // Ion free starter
    g_settings.weapon_rig = WEAPON_SINGLE; // NEW: single weak starter
    g_settings.laser_index = 0; // Ion Basic weak starter
    g_settings.high_score = 0;
    g_settings.coins = 0;
    g_settings.owned_accents = (1 << 1); // Ion Cyan
    g_settings.owned_trails  = (1 << 1); // Ion
    g_settings.owned_rigs    = (1 << WEAPON_SINGLE); // only single
    g_settings.owned_lasers  = (1 << 0); // Ion Basic
    for (int i = 0; i < NUM_UPGRADES; i++) {
        g_settings.upgrade_levels[i] = 0;
    }
}

void save_load(void) {
    save_init_defaults();

#ifdef PLATFORM_HOST
    /* Pull coins/loot/settings from filesDir/saves/save.sav if present. */
    platform_restore_save();
#endif

    // Try V4
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
        save_write();
    }
}

void save_write(void) {
    SaveDataV4 data;
    memset(&data, 0, sizeof(SaveDataV4));
    data.magic = SAVE_MAGIC_V4;
    data.difficulty = (u8)g_settings.difficulty;
    data.music_volume = (u8)g_settings.music_volume;
    data.sfx_volume = (u8)g_settings.sfx_volume;
    data.screen_shake = g_settings.screen_shake ? 1 : 0;
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
    for (int i = 0; i < NUM_UPGRADES; i++) {
        data.upgrade_levels[i] = g_settings.upgrade_levels[i];
    }
    data.checksum = calc_checksum_v4(&data);

    const u8* src = (const u8*)&data;
    for (u32 i = 0; i < sizeof(SaveDataV4); i++) {
        SRAM_BASE[i] = src[i];
    }

#ifdef PLATFORM_HOST
    platform_persist_save();
#endif
}

// ── Shop Pricing ────────────────────────────────────────────────────────
int shop_get_accent_price(int idx) {
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
}

int shop_get_trail_price(int idx) {
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
}

int shop_get_rig_price(WeaponRig rig) {
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
}

int shop_get_laser_price(int idx) {
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
}

// New upgrade pricing: 5 levels (0->1,1->2,2->3,3->4,4->5)
int shop_get_upgrade_price(UpgradeType upg, int level) {
    if (level < 0 || level >= UPG_MAX_LEVEL) return 999999;
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
    return (g_settings.owned_lasers & (1 << idx)) != 0;
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
    if (g_settings.coins < (u32)price) return false;
    g_settings.coins -= price;
    g_settings.owned_accents |= (1 << idx);
    g_settings.accent_index = idx;
    save_write();
    return true;
}

bool shop_try_purchase_trail(int idx) {
    if (shop_is_trail_owned(idx)) { shop_equip_trail(idx); return true; }
    int price = shop_get_trail_price(idx);
    if (g_settings.coins < (u32)price) return false;
    g_settings.coins -= price;
    g_settings.owned_trails |= (1 << idx);
    g_settings.trail_index = idx;
    save_write();
    return true;
}

bool shop_try_purchase_rig(WeaponRig rig) {
    if (shop_is_rig_owned(rig)) { shop_equip_rig(rig); return true; }
    int price = shop_get_rig_price(rig);
    if (g_settings.coins < (u32)price) return false;
    g_settings.coins -= price;
    g_settings.owned_rigs |= (1 << rig);
    g_settings.weapon_rig = rig;
    save_write();
    return true;
}

bool shop_try_purchase_laser(int idx) {
    if (shop_is_laser_owned(idx)) { shop_equip_laser(idx); return true; }
    int price = shop_get_laser_price(idx);
    if (g_settings.coins < (u32)price) return false;
    g_settings.coins -= price;
    g_settings.owned_lasers |= (1 << idx);
    g_settings.laser_index = idx;
    save_write();
    return true;
}

bool shop_try_purchase_upgrade(UpgradeType upg) {
    if (upg < 0 || upg >= NUM_UPGRADES) return false;
    int current_lv = g_settings.upgrade_levels[upg];
    if (current_lv >= UPG_MAX_LEVEL) return false;
    int price = shop_get_upgrade_price(upg, current_lv);
    if (g_settings.coins < (u32)price) return false;
    g_settings.coins -= price;
    g_settings.upgrade_levels[upg]++;
    save_write();
    return true;
}

const char* shop_get_upgrade_name(UpgradeType upg) {
    switch (upg) {
        case UPG_ENGINE:    return "Ion Engine";
        case UPG_FIRE_RATE: return "Fire Rate";
        case UPG_DAMAGE:    return "Plasma Core";
        case UPG_SHIELD:    return "Shield Battery";
        case UPG_HULL:      return "Hull Plating";
        case UPG_DASH:      return "Afterburner";
        case UPG_SCAVENGER: return "Graviton Magnet";
        case UPG_OVERDRIVE: return "Overdrive Unit";
        default:            return "Unknown Tech";
    }
}

const char* shop_get_upgrade_desc_line1(UpgradeType upg) {
    switch (upg) {
        case UPG_ENGINE:    return "Ship speed 0.7->2x";
        case UPG_FIRE_RATE: return "2/sec -> 10/sec";
        case UPG_DAMAGE:    return "+1 dmg per lvl";
        case UPG_SHIELD:    return "+Shield cap & start";
        case UPG_HULL:      return "+Extra life per lvl";
        case UPG_DASH:      return "-Dash cd & +invuln";
        case UPG_SCAVENGER: return "+Coins & magnet";
        case UPG_OVERDRIVE: return "+Rapid duration";
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
            if (level == 0) return "Lv0 2 shots/sec";
            if (level == 1) return "Lv1 ~3/sec";
            if (level == 2) return "Lv2 ~4/sec";
            if (level == 3) return "Lv3 ~6/sec";
            if (level == 4) return "Lv4 ~8/sec";
            return "MAX 10+/sec!";
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
            if (level == 0) return "Long cd 1.35s";
            if (level == 1) return "Cd 1.1s -20%";
            if (level == 2) return "Cd 0.86s -35%";
            if (level == 3) return "Cd 0.66s -50%";
            if (level == 4) return "Cd 0.5s -65%";
            return "MAX cd 0.4s";
        case UPG_SCAVENGER:
            if (level == 0) return "No magnet 1x $";
            if (level == 1) return "+35% $ small mag";
            if (level == 2) return "+70% $ med mag";
            if (level == 3) return "+105% $ big mag";
            if (level == 4) return "+140% $ huge mag";
            return "MAX +175% & pull";
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
