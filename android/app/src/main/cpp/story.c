#include "story.h"
#include "save.h"
#include "renderer.h"
#include <string.h>
#include <stdio.h>

/* ── Persistent campaign state ────────────────────────────────────────────
 * Mirrored into the V9 save block by save.c (g_story_save). */
StorySave g_story;

void story_init(void) {
    if (g_story.level < 1 || g_story.level > STORY_LEVEL_COUNT) g_story.level = 1;
    if (g_story.unlocked < 1) g_story.unlocked = 1;
    if (g_story.unlocked > STORY_LEVEL_COUNT) g_story.unlocked = STORY_LEVEL_COUNT;
    if (g_story.level > g_story.unlocked) g_story.level = g_story.unlocked;
    if (g_story.lives < 1) g_story.lives = STORY_START_LIVES;
    if (g_story.lives > STORY_MAX_LIVES) g_story.lives = STORY_MAX_LIVES;
    if (g_story.cleared_count > STORY_LEVEL_COUNT) g_story.cleared_count = STORY_LEVEL_COUNT;
}

void story_reset_progress(void) {
    memset(&g_story, 0, sizeof(g_story));
    g_story.level = 1;
    g_story.unlocked = 1;
    g_story.lives = STORY_START_LIVES;
}

int story_current_level(void) { return g_story.level; }

void story_set_current_level(int level) {
    if (level < 1) level = 1;
    if (level > STORY_LEVEL_COUNT) level = STORY_LEVEL_COUNT;
    if (level > g_story.unlocked) level = g_story.unlocked;
    g_story.level = (u8)level;
}

int story_highest_unlocked(void) { return g_story.unlocked; }

bool story_is_cleared(int level) {
    if (level < 1 || level > STORY_LEVEL_COUNT) return false;
    return (g_story.cleared[(level - 1) >> 3] & (1u << ((level - 1) & 7))) != 0;
}

bool story_is_unlocked(int level) {
    return level >= 1 && level <= g_story.unlocked;
}

static void story_mark_cleared(int level) {
    if (level < 1 || level > STORY_LEVEL_COUNT) return;
    if (!story_is_cleared(level)) {
        g_story.cleared[(level - 1) >> 3] |= (u8)(1u << ((level - 1) & 7));
        if (g_story.cleared_count < STORY_LEVEL_COUNT) g_story.cleared_count++;
    }
}

int story_complete_level(int level) {
    if (level < 1 || level > STORY_LEVEL_COUNT) return 0;
    bool replay = story_is_cleared(level);
    int reward = g_story_levels[level - 1].reward;
    /* Replays pay half, so grinding an easy level is never the fast route. */
    if (replay) reward /= 2;

    story_mark_cleared(level);
    if (level == g_story.unlocked && g_story.unlocked < STORY_LEVEL_COUNT) {
        g_story.unlocked++;
    }
    /* Park the map cursor on whatever comes next. */
    int next = level + 1;
    if (next > g_story.unlocked) next = g_story.unlocked;
    g_story.level = (u8)next;

    story_award(reward);
    save_write();
    return reward;
}

int story_lives(void) { return g_story.lives; }

void story_add_lives(int n) {
    int v = g_story.lives + n;
    if (v > STORY_MAX_LIVES) v = STORY_MAX_LIVES;
    if (v < 0) v = 0;
    g_story.lives = (u8)v;
}

int story_lose_life(void) {
    if (g_story.lives > 0) g_story.lives--;
    if (g_story.lives > 0) {
        save_write();
        return g_story.level;              /* retry the same level */
    }
    /* Out of lives: back to the level right after the previous boss. */
    int resume = story_checkpoint_for(g_story.level);
    g_story.lives = STORY_START_LIVES;
    g_story.level = (u8)resume;
    save_write();
    return resume;
}

u32 story_chubbcoin(void) { return g_story.chubbcoin; }

void story_award(int amount) {
    if (amount <= 0) return;
    u32 v = g_story.chubbcoin + (u32)amount;
    if (v > 9999999u) v = 9999999u;
    g_story.chubbcoin = v;
}

bool story_spend(int amount) {
    if (amount < 0) return false;
    if (g_story.chubbcoin < (u32)amount) return false;
    g_story.chubbcoin -= (u32)amount;
    return true;
}

bool story_is_finished(void) {
    return story_is_cleared(STORY_LEVEL_COUNT);
}

bool story_content_unlocked(void) {
    return story_is_finished() || g_story.freed != 0;
}

void story_free_everything(void) {
    g_story.freed = 1;
    save_write();
}

bool story_intro_seen(void) { return g_story.intro_seen != 0; }

void story_mark_intro_seen(void) {
    if (!g_story.intro_seen) {
        g_story.intro_seen = 1;
        save_write();
    }
}

/* ── Mr Chubbs' Shop ──────────────────────────────────────────────────────
 * Stock is a deterministic roll from the dock level, so the shelf is stable
 * for that dock no matter how many times you fly in and out. He only ever
 * shows a few things at a time — you never meet him, just his prices. */

static StoryStockItem s_stock[STORY_SHOP_SLOTS];
static int s_stock_level = -1;

/* Tiny deterministic hash -> pseudo-random, no rand() state involved. */
static u32 stock_hash(u32 x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

/* Which rigs/lasers/paints he is willing to sell at a given dock: the
 * further you are, the better the shelf, but never the arcade endgame gear. */
static int stock_rig_for(int dock, u32 r) {
    int tier = 1 + dock;                       /* dock 1 -> rig 2ish */
    if (tier > NUM_RIGS - 3) tier = NUM_RIGS - 3;
    int lo = tier > 2 ? tier - 2 : 1;
    int span = (tier - lo) + 1;
    return lo + (int)(r % (u32)(span > 0 ? span : 1));
}

static int stock_laser_for(int dock, u32 r) {
    int tier = 1 + dock / 3;                   /* crawls up to Quantum White */
    if (tier > NUM_LASERS - 2) tier = NUM_LASERS - 2;
    if (tier < 1) tier = 1;
    return 1 + (int)(r % (u32)tier);
}

static int stock_paint_for(u32 r) {
    /* Paints 2..6 only: never the 1M rainbow, never the free starter. */
    return 2 + (int)(r % 5u);
}

static int stock_upgrade_for(u32 r) {
    static const u8 pool[5] = { UPG_ENGINE, UPG_FIRE_RATE, UPG_DAMAGE, UPG_SHIELD, UPG_HULL };
    return pool[r % 5u];
}

/* Chubbcoin prices are tuned against the level rewards: a full sector pays
 * roughly 1,500-4,000, so one good item per dock is affordable. */
static int stock_price(const StoryStockItem* it, int dock) {
    switch (it->kind) {
        case SSTOCK_LIFE:    return 220 + dock * 60;
        case SSTOCK_WEAPON:  return 600 + it->item * 190 + dock * 70;
        case SSTOCK_LASER:   return 500 + it->item * 320 + dock * 60;
        case SSTOCK_PAINT:   return 300 + it->item * 90;
        case SSTOCK_UPGRADE: return 380 + dock * 110;
        default:             return 0;
    }
}

void story_shop_open(int level) {
    if (s_stock_level == level) return;        /* same dock: same shelf */
    s_stock_level = level;
    memset(s_stock, 0, sizeof(s_stock));

    int dock = level / 5;                      /* 1, 2, 3 ... */
    if (dock < 1) dock = 1;

    /* Slot 0 is always lives — cheap-ish, and strictly limited so you can't
     * simply buy your way through the campaign. */
    s_stock[0].kind = SSTOCK_LIFE;
    s_stock[0].item = 0;
    s_stock[0].qty = (u8)(1 + (dock % 2));     /* 1 or 2 in stock */
    s_stock[0].price = (u16)stock_price(&s_stock[0], dock);

    /* Slots 1..3: a small random shelf of gear. */
    for (int i = 1; i < STORY_SHOP_SLOTS; i++) {
        u32 r = stock_hash((u32)(level * 7919 + i * 104729));
        int kind_roll = (int)(r % 100u);
        StoryStockItem* it = &s_stock[i];
        if (kind_roll < 34)      it->kind = SSTOCK_WEAPON;
        else if (kind_roll < 58) it->kind = SSTOCK_LASER;
        else if (kind_roll < 78) it->kind = SSTOCK_UPGRADE;
        else                     it->kind = SSTOCK_PAINT;

        u32 r2 = stock_hash(r ^ 0x9e3779b9u);
        switch (it->kind) {
            case SSTOCK_WEAPON:  it->item = (u8)stock_rig_for(dock, r2); break;
            case SSTOCK_LASER:   it->item = (u8)stock_laser_for(dock, r2); break;
            case SSTOCK_PAINT:   it->item = (u8)stock_paint_for(r2); break;
            default:             it->item = (u8)stock_upgrade_for(r2); break;
        }
        it->qty = 1;
        it->price = (u16)stock_price(it, dock);
    }
}

const StoryStockItem* story_shop_slot(int i) {
    if (i < 0 || i >= STORY_SHOP_SLOTS) return NULL;
    return &s_stock[i];
}

const char* story_shop_slot_name(int i) {
    const StoryStockItem* it = story_shop_slot(i);
    if (!it) return "";
    switch (it->kind) {
        case SSTOCK_LIFE:    return "SPARE LIFE";
        case SSTOCK_WEAPON:  return gfx_get_weapon_name((WeaponRig)it->item);
        case SSTOCK_LASER:   return gfx_get_laser_name(it->item);
        case SSTOCK_PAINT:   return gfx_get_accent_name(it->item);
        case SSTOCK_UPGRADE: return shop_get_upgrade_name((UpgradeType)it->item);
        default:             return "";
    }
}

const char* story_shop_slot_desc(int i) {
    const StoryStockItem* it = story_shop_slot(i);
    if (!it) return "";
    switch (it->kind) {
        case SSTOCK_LIFE:    return "One more go. Limited stock.";
        case SSTOCK_WEAPON:  return gfx_get_weapon_desc((WeaponRig)it->item);
        case SSTOCK_LASER:   return gfx_get_laser_desc(it->item);
        case SSTOCK_PAINT:   return gfx_get_accent_desc(it->item);
        case SSTOCK_UPGRADE: return shop_get_upgrade_desc_line1((UpgradeType)it->item);
        default:             return "";
    }
}

int story_shop_buy(int i) {
    if (i < 0 || i >= STORY_SHOP_SLOTS) return 2;
    StoryStockItem* it = &s_stock[i];
    if (it->kind == SSTOCK_EMPTY || it->qty == 0) return 2;

    /* Already-owned gear is dead stock — don't take the player's money. */
    switch (it->kind) {
        case SSTOCK_WEAPON:
            if (g_settings.owned_rigs & (1u << it->item)) return 2;
            break;
        case SSTOCK_LASER:
            if (g_settings.owned_lasers & (1u << it->item)) return 2;
            break;
        case SSTOCK_PAINT:
            if (g_settings.owned_accents & (1u << it->item)) return 2;
            break;
        case SSTOCK_UPGRADE:
            if (g_settings.upgrade_levels[it->item] >= UPG_MAX_LEVEL) return 2;
            break;
        case SSTOCK_LIFE:
            if (g_story.lives >= STORY_MAX_LIVES) return 2;
            break;
        default: break;
    }

    if (!story_spend(it->price)) return 1;

    /* Gear bought from Mr Chubbs is yours everywhere afterwards — the arcade
     * hangar sees the same ownership bits. */
    switch (it->kind) {
        case SSTOCK_LIFE:
            story_add_lives(1);
            break;
        case SSTOCK_WEAPON:
            g_settings.owned_rigs |= (u16)(1u << it->item);
            g_settings.weapon_rig = (WeaponRig)it->item;
            break;
        case SSTOCK_LASER:
            g_settings.owned_lasers |= (1u << it->item);
            g_settings.laser_index = it->item;
            break;
        case SSTOCK_PAINT:
            g_settings.owned_accents |= (u16)(1u << it->item);
            g_settings.accent_index = it->item;
            break;
        case SSTOCK_UPGRADE:
            g_settings.upgrade_levels[it->item]++;
            break;
        default: break;
    }

    it->qty--;
    save_write();
    return 0;
}
