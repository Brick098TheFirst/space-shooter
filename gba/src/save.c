#include "save.h"
#include "types.h"
#include <string.h>

GameSettings g_settings;

#define SRAM_BASE ((volatile u8*)0x0E000000)
#define SAVE_MAGIC_V1 0x53554742 // 'SUGB' legacy
#define SAVE_MAGIC_V2 0x53554743 // 'SUGC' legacy
#define SAVE_MAGIC_V3 0x53554744 // 'SUGD' expanded shop & tech upgrades

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

// Layout V3 (48 bytes)
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
    u8  upgrade_levels[NUM_UPGRADES]; // 7 bytes (0..3)
    u8  pad0;
    u32 pad1;
    u32 pad2;
    u32 checksum;
} SaveDataV3;

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
    g_settings.weapon_rig = WEAPON_TWIN; // Twin free starter
    g_settings.laser_index = 0; // Cyan free starter
    g_settings.high_score = 0;
    g_settings.coins = 0;
    g_settings.owned_accents = (1 << 1); // Ion Cyan
    g_settings.owned_trails  = (1 << 1); // Ion
    g_settings.owned_rigs    = (1 << WEAPON_TWIN);
    g_settings.owned_lasers  = (1 << 0); // Cyan
    for (int i = 0; i < NUM_UPGRADES; i++) {
        g_settings.upgrade_levels[i] = 0;
    }
}

void save_load(void) {
    save_init_defaults();

    // 1. Try V3 first
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
        if (d3.weapon_rig < NUM_RIGS) g_settings.weapon_rig = (WeaponRig)d3.weapon_rig;
        if (d3.laser_index < NUM_LASERS) g_settings.laser_index = d3.laser_index;
        g_settings.high_score = d3.high_score;
        g_settings.coins = d3.coins;
        g_settings.owned_accents = d3.owned_accents ? d3.owned_accents : (1<<1);
        g_settings.owned_trails  = d3.owned_trails  ? d3.owned_trails  : (1<<1);
        g_settings.owned_rigs    = d3.owned_rigs    ? d3.owned_rigs    : (1<<WEAPON_TWIN);
        g_settings.owned_lasers  = d3.owned_lasers  ? d3.owned_lasers  : (1<<0);
        for (int i = 0; i < NUM_UPGRADES; i++) {
            g_settings.upgrade_levels[i] = d3.upgrade_levels[i] <= 3 ? d3.upgrade_levels[i] : 0;
        }

        // Safety repair: ensure equipped items are owned
        if (!(g_settings.owned_accents & (1 << g_settings.accent_index))) g_settings.accent_index = 1;
        if (!(g_settings.owned_trails & (1 << g_settings.trail_index))) g_settings.trail_index = 1;
        if (!(g_settings.owned_rigs & (1 << g_settings.weapon_rig))) g_settings.weapon_rig = WEAPON_TWIN;
        if (!(g_settings.owned_lasers & (1 << g_settings.laser_index))) g_settings.laser_index = 0;
        return;
    }

    // 2. Fallback to V2
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
        if (d2.weapon_rig < NUM_RIGS) g_settings.weapon_rig = (WeaponRig)d2.weapon_rig;
        if (d2.laser_index < NUM_LASERS) g_settings.laser_index = d2.laser_index;
        g_settings.high_score = d2.high_score;
        g_settings.coins = d2.coins;
        g_settings.owned_accents = d2.owned_accents ? d2.owned_accents : (1<<1);
        g_settings.owned_trails  = d2.owned_trails  ? d2.owned_trails  : (1<<1);
        g_settings.owned_rigs    = d2.owned_rigs    ? d2.owned_rigs    : (1<<WEAPON_TWIN);
        g_settings.owned_lasers  = d2.owned_lasers  ? d2.owned_lasers  : (1<<0);
        save_write();
        return;
    }

    // 3. Fallback to V1
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
        if (d1.weapon_rig < NUM_RIGS) g_settings.weapon_rig = (WeaponRig)d1.weapon_rig;
        g_settings.high_score = d1.high_score;
        g_settings.owned_accents |= (1 << g_settings.accent_index);
        g_settings.owned_trails  |= (1 << g_settings.trail_index);
        g_settings.owned_rigs    |= (1 << g_settings.weapon_rig);
        if (g_settings.coins == 0 && g_settings.high_score > 0) g_settings.coins = 250;
        save_write();
    }
}

void save_write(void) {
    SaveDataV3 data;
    memset(&data, 0, sizeof(SaveDataV3));
    data.magic = SAVE_MAGIC_V3;
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
    data.checksum = calc_checksum_v3(&data);

    const u8* src = (const u8*)&data;
    for (u32 i = 0; i < sizeof(SaveDataV3); i++) {
        SRAM_BASE[i] = src[i];
    }
}

// ── Shop Pricing (Expensive Progression Grind) ───────────────────────────
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
        case 8: return 1000000; // Rainbow Prism (1 Million)
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
        case 7: return 130000;  // Prismatic Arc
        default: return 999999;
    }
}

int shop_get_rig_price(WeaponRig rig) {
    switch (rig) {
        case WEAPON_TWIN:    return 0;       // Twin Cannons (Starter)
        case WEAPON_SPREAD:  return 2500;    // Spread Cannons
        case WEAPON_FOCUSED: return 7500;    // Focused Beam
        case WEAPON_TRIPLE:  return 20000;   // Triple Blaster
        case WEAPON_PLASMA:  return 50000;   // Plasma Wave
        case WEAPON_QUANTUM: return 100000;  // Quantum Core
        default: return 999999;
    }
}

int shop_get_laser_price(int idx) {
    switch (idx) {
        case 0: return 0;       // Ion Cyan (Starter)
        case 1: return 1800;    // Solar Gold
        case 2: return 4500;    // Nebula Violet
        case 3: return 9500;    // Toxic Mint
        case 4: return 22000;   // Crimson Fury
        case 5: return 48000;   // Emerald Surge
        case 6: return 85000;   // Void Shadow
        case 7: return 150000;  // Prism Radiance
        default: return 999999;
    }
}

int shop_get_upgrade_price(UpgradeType upg, int level) {
    // level: 0 = buying lv 1, 1 = buying lv 2, 2 = buying lv 3
    if (level < 0 || level >= 3) return 999999;
    switch (upg) {
        case UPG_SHIELD: {
            const int p[3] = { 3500, 15000, 45000 };
            return p[level];
        }
        case UPG_HULL: {
            const int p[3] = { 4500, 18000, 55000 };
            return p[level];
        }
        case UPG_THRUSTERS: {
            const int p[3] = { 3000, 12000, 38000 };
            return p[level];
        }
        case UPG_SCAVENGER: {
            const int p[3] = { 4000, 16000, 50000 };
            return p[level];
        }
        case UPG_DAMAGE: {
            const int p[3] = { 6000, 24000, 75000 };
            return p[level];
        }
        case UPG_OVERDRIVE: {
            const int p[3] = { 2800, 10500, 32000 };
            return p[level];
        }
        case UPG_COMBO: {
            const int p[3] = { 5000, 20000, 65000 };
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
    if (current_lv >= 3) return false; // Already maxed
    int price = shop_get_upgrade_price(upg, current_lv);
    if (g_settings.coins < (u32)price) return false;
    g_settings.coins -= price;
    g_settings.upgrade_levels[upg]++;
    save_write();
    return true;
}

const char* shop_get_upgrade_name(UpgradeType upg) {
    switch (upg) {
        case UPG_SHIELD:    return "Shield Battery";
        case UPG_HULL:      return "Reinforced Hull";
        case UPG_THRUSTERS: return "Hyper Thruster";
        case UPG_SCAVENGER: return "Coin Scavenger";
        case UPG_DAMAGE:    return "Plasma Reactor";
        case UPG_OVERDRIVE: return "Overdrive Unit";
        case UPG_COMBO:     return "Combo Matrix";
        default:            return "Unknown Tech";
    }
}

const char* shop_get_upgrade_desc_line1(UpgradeType upg) {
    switch (upg) {
        case UPG_SHIELD:    return "+1 Shield Cap/Lv";
        case UPG_HULL:      return "+1 Extra Life/Lv";
        case UPG_THRUSTERS: return "+15% Speed & Dash";
        case UPG_SCAVENGER: return "+35% Coins & Magnet";
        case UPG_DAMAGE:    return "+1 Bullet Damage";
        case UPG_OVERDRIVE: return "+40% Buff Time";
        case UPG_COMBO:     return "+Chain Window & x20";
        default:            return "";
    }
}

const char* shop_get_upgrade_desc_line2(UpgradeType upg, int level) {
    switch (upg) {
        case UPG_SHIELD:
            if (level == 0) return "Start with 1 shld";
            if (level == 1) return "Start with 2 shld";
            if (level == 2) return "Start with 3 shld";
            return "Max 6 shield cap";
        case UPG_HULL:
            if (level == 0) return "Start with +1 life";
            if (level == 1) return "Start with +2 lives";
            if (level == 2) return "Start with +3 lives";
            return "Start with 6 lives";
        case UPG_THRUSTERS:
            if (level == 0) return "Dash cooldown -20%";
            if (level == 1) return "Dash cooldown -35%";
            if (level == 2) return "Dash cooldown -50%";
            return "Ultra agile thrust";
        case UPG_SCAVENGER:
            if (level == 0) return "+35% Coin drops";
            if (level == 1) return "+70% Coins + Magnet";
            if (level == 2) return "+105% Coins & Pull";
            return "Double coin harvest";
        case UPG_DAMAGE:
            if (level == 0) return "+1 Heavy Laser dmg";
            if (level == 1) return "+1 All Weapons dmg";
            if (level == 2) return "+2 Max Firepower";
            return "Maximum devastation";
        case UPG_OVERDRIVE:
            if (level == 0) return "Rapid lasts 12 sec";
            if (level == 1) return "Rapid lasts 16 sec";
            if (level == 2) return "Rapid lasts 20 sec";
            return "Extended overdrive";
        case UPG_COMBO:
            if (level == 0) return "Max Combo x12 cap";
            if (level == 1) return "Max Combo x16 cap";
            if (level == 2) return "Max Combo x20 cap";
            return "Score titan status";
        default:
            return "";
    }
}
