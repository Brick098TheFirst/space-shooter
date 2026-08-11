#include "save.h"
#include "types.h"

GameSettings g_settings;

#define SRAM_BASE ((volatile u8*)0x0E000000)
#define SAVE_MAGIC_V1 0x53554742 // 'SUGB' legacy
#define SAVE_MAGIC_V2 0x53554743 // 'SUGC' with coins & shop

// Legacy layout (20 bytes)
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

// New layout (32 bytes)
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
    g_settings.accent_index = 1; // Ion Cyan free
    g_settings.trail_index = 1;  // Ion free
    g_settings.weapon_rig = WEAPON_TWIN; // Twin free
    g_settings.laser_index = 0; // Cyan free
    g_settings.high_score = 0;
    g_settings.coins = 0;
    g_settings.owned_accents = (1 << 1); // Ion Cyan
    g_settings.owned_trails  = (1 << 1); // Ion
    g_settings.owned_rigs    = (1 << WEAPON_TWIN);
    g_settings.owned_lasers  = (1 << 0); // Cyan
}

void save_load(void) {
    save_init_defaults();

    // Try V2 first
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
        // Ensure equipped items are owned (repair corrupted saves)
        if (!(g_settings.owned_accents & (1<<g_settings.accent_index))) g_settings.accent_index = 1;
        if (!(g_settings.owned_trails & (1<<g_settings.trail_index))) g_settings.trail_index = 1;
        if (!(g_settings.owned_rigs & (1<<g_settings.weapon_rig))) g_settings.weapon_rig = WEAPON_TWIN;
        if (!(g_settings.owned_lasers & (1<<g_settings.laser_index))) g_settings.laser_index = 0;
        return;
    }

    // Fallback to V1
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
        // migrate owned flags: mark current equipped as owned plus defaults
        g_settings.owned_accents |= (1 << g_settings.accent_index);
        g_settings.owned_trails  |= (1 << g_settings.trail_index);
        g_settings.owned_rigs    |= (1 << g_settings.weapon_rig);
        // give a small starter bonus for returning players
        if (g_settings.coins == 0 && g_settings.high_score > 0) g_settings.coins = 150;
        save_write();
    }
}

void save_write(void) {
    SaveDataV2 data;
    data.magic = SAVE_MAGIC_V2;
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
    data.pad0 = data.pad1 = data.pad2 = data.pad3 = 0;
    data.checksum = calc_checksum_v2(&data);
    const u8* src = (const u8*)&data;
    for (u32 i = 0; i < sizeof(SaveDataV2); i++) {
        SRAM_BASE[i] = src[i];
    }
}

// ── Shop pricing ─────────────────────────────────────────────────────────
int shop_get_accent_price(int idx) {
    switch (idx) {
        case 0: return 400;  // Solar orange
        case 1: return 0;    // Ion cyan free
        case 2: return 650;  // Nova violet
        case 3: return 850;  // Plasma mint
        case 4: return 1400; // Pulsar gold
        default: return 9999;
    }
}
int shop_get_trail_price(int idx) {
    switch (idx) {
        case 0: return 350; // Ember
        case 1: return 0;   // Ion
        case 2: return 600; // Nova
        case 3: return 950; // Aurora
        default: return 9999;
    }
}
int shop_get_rig_price(WeaponRig rig) {
    switch (rig) {
        case WEAPON_SPREAD:  return 500;
        case WEAPON_TWIN:    return 0;
        case WEAPON_FOCUSED: return 1000;
        default: return 9999;
    }
}
int shop_get_laser_price(int idx) {
    switch (idx) {
        case 0: return 0;   // Cyan
        case 1: return 450; // Gold
        case 2: return 600; // Violet
        case 3: return 800; // Mint
        default: return 9999;
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
