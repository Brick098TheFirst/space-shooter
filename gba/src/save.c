#include "save.h"
#include "types.h"

GameSettings g_settings;

#define SRAM_BASE ((volatile u8*)0x0E000000)
#define SAVE_MAGIC 0x53554742 // 'SUGB' (Space Unlimited GBA)

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
} SaveData;

static u32 calc_checksum(const SaveData* data) {
    u32 sum = 0x12345678;
    const u8* bytes = (const u8*)data;
    for (u32 i = 0; i < sizeof(SaveData) - sizeof(u32); i++) {
        sum = (sum * 33) ^ bytes[i];
    }
    return sum;
}

void save_init_defaults(void) {
    g_settings.difficulty = DIFF_PILOT;
    g_settings.music_volume = 80;
    g_settings.sfx_volume = 80;
    g_settings.screen_shake = true;
    g_settings.accent_index = 1; // Ion Cyan
    g_settings.trail_index = 1;  // Ion
    g_settings.weapon_rig = WEAPON_TWIN;
    g_settings.high_score = 0;
}

void save_load(void) {
    save_init_defaults();
    
    SaveData data;
    u8* dest = (u8*)&data;
    for (u32 i = 0; i < sizeof(SaveData); i++) {
        dest[i] = SRAM_BASE[i];
    }
    
    if (data.magic == SAVE_MAGIC && data.checksum == calc_checksum(&data)) {
        if (data.difficulty <= 2) g_settings.difficulty = (Difficulty)data.difficulty;
        g_settings.music_volume = data.music_volume <= 100 ? data.music_volume : 80;
        g_settings.sfx_volume = data.sfx_volume <= 100 ? data.sfx_volume : 80;
        g_settings.screen_shake = (data.screen_shake != 0);
        if (data.accent_index < 5) g_settings.accent_index = data.accent_index;
        if (data.trail_index < 4) g_settings.trail_index = data.trail_index;
        if (data.weapon_rig <= 2) g_settings.weapon_rig = (WeaponRig)data.weapon_rig;
        g_settings.high_score = data.high_score;
    }
}

void save_write(void) {
    SaveData data;
    data.magic = SAVE_MAGIC;
    data.difficulty = (u8)g_settings.difficulty;
    data.music_volume = (u8)g_settings.music_volume;
    data.sfx_volume = (u8)g_settings.sfx_volume;
    data.screen_shake = g_settings.screen_shake ? 1 : 0;
    data.accent_index = (u8)g_settings.accent_index;
    data.trail_index = (u8)g_settings.trail_index;
    data.weapon_rig = (u8)g_settings.weapon_rig;
    data.padding = 0;
    data.high_score = g_settings.high_score;
    data.checksum = calc_checksum(&data);
    
    const u8* src = (const u8*)&data;
    for (u32 i = 0; i < sizeof(SaveData); i++) {
        SRAM_BASE[i] = src[i];
    }
}
