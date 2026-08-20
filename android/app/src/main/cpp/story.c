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
    /* Only the 14 real docks have bits; drop anything a corrupt save set. */
    g_story.docks_used &= (u16)((1u << STORY_DOCK_COUNT) - 1u);
    if (g_story.ending_phase > STORY_ENDING_RETURN_MENU)
        g_story.ending_phase = STORY_ENDING_NONE;
    /* A repair deadline further out than the repair itself means the device
     * clock moved; clamp it rather than grounding the player for ever. */
    if (g_story.repair_until != 0) {
        u32 now = platform_epoch_seconds();
        if (g_story.repair_until <= now) g_story.repair_until = 0;
        else if (g_story.repair_until - now > (u32)STORY_REPAIR_SECONDS)
            g_story.repair_until = now + STORY_REPAIR_SECONDS;
    }
}

static void story_shop_forget(void);

void story_reset_progress(void) {
    memset(&g_story, 0, sizeof(g_story));
    g_story.level = 1;
    g_story.unlocked = 1;
    g_story.lives = STORY_START_LIVES;
    story_shop_forget();
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

void story_unlock_all_levels(void) {
    /* Unlocking is deliberately not the same as completing: boss gifts,
     * rewards, clear stars and the ending still have to be earned. */
    g_story.unlocked = STORY_LEVEL_COUNT;
    if (g_story.level < 1 || g_story.level > STORY_LEVEL_COUNT) g_story.level = 1;
    save_write();
}

static void story_mark_cleared(int level) {
    if (level < 1 || level > STORY_LEVEL_COUNT) return;
    if (!story_is_cleared(level)) {
        g_story.cleared[(level - 1) >> 3] |= (u8)(1u << ((level - 1) & 7));
        if (g_story.cleared_count < STORY_LEVEL_COUNT) g_story.cleared_count++;
    }
}

/* ── Dynamic payouts ─────────────────────────────────────────────────────
 * The level's `reward` is the floor.  Four bonuses ride on top of it, all
 * expressed as a percentage of that floor, so the level tables stay the
 * single place difficulty and value are tuned:
 *
 *   speed      up to +60%   finishing well inside par
 *   combat     up to +50%   what you actually shot down
 *   precision  up to +20%   accuracy
 *   clean      flat  +25%   not losing a life
 *
 * The combat term is the important one: it is what makes SURVIVE levels pay
 * for fighting rather than for hiding behind the clock. */
#define STORY_BONUS_SPEED_PCT 60
#define STORY_BONUS_COMBAT_PCT 50
#define STORY_BONUS_PRECISION_PCT 20
#define STORY_BONUS_CLEAN_PCT 25

/* Last payout's breakdown, so the result card can show where it came from. */
static int s_pay_base = 0;
static int s_pay_speed = 0;
static int s_pay_combat = 0;
static int s_pay_precision = 0;
static int s_pay_clean = 0;

int story_pay_base(void)      { return s_pay_base; }
int story_pay_speed(void)     { return s_pay_speed; }
int story_pay_combat(void)    { return s_pay_combat; }
int story_pay_precision(void) { return s_pay_precision; }
int story_pay_clean(void)     { return s_pay_clean; }

static int pct_of(int base, int pct) {
    if (base <= 0 || pct <= 0) return 0;
    return (base * pct) / 100;
}

/* Was the clear just banked a replay?  The outro uses this to play exactly
 * once - the first time the final level falls, never again. */
static bool s_last_clear_replay = false;

bool story_last_clear_was_replay(void) { return s_last_clear_replay; }

int story_complete_level(int level, const StoryPerf* perf) {
    if (level < 1 || level > STORY_LEVEL_COUNT) return 0;
    bool replay = story_is_cleared(level);
    s_last_clear_replay = replay;
    int base = g_story_levels[level - 1].reward;

    s_pay_base = base;
    s_pay_speed = s_pay_combat = s_pay_precision = s_pay_clean = 0;

    if (perf) {
        /* SPEED: full bonus at half par or better, nothing at par or over.
         * A level with no meaningful par (survive levels run a fixed clock)
         * passes par_secs = 0 and simply skips this term. */
        if (perf->par_secs > 0 && perf->secs < perf->par_secs) {
            int spare = perf->par_secs - perf->secs;          /* seconds saved */
            int pct = (spare * 2 * STORY_BONUS_SPEED_PCT) / perf->par_secs;
            if (pct > STORY_BONUS_SPEED_PCT) pct = STORY_BONUS_SPEED_PCT;
            s_pay_speed = pct_of(base, pct);
        }

        /* COMBAT: paid for what you actually destroyed, against the body
         * count the level expects.  Sitting out a SURVIVE timer without
         * firing banks the floor and nothing more. */
        if (perf->par_kills > 0) {
            int pct = (perf->kills * STORY_BONUS_COMBAT_PCT) / perf->par_kills;
            if (pct > STORY_BONUS_COMBAT_PCT) pct = STORY_BONUS_COMBAT_PCT;
            s_pay_combat = pct_of(base, pct);
        }

        /* PRECISION: accuracy, but only once enough shots were fired for the
         * number to mean anything. */
        if (perf->shots >= 20) {
            int acc = (perf->hits * 100) / perf->shots;
            if (acc > 100) acc = 100;
            s_pay_precision = pct_of(base, (acc * STORY_BONUS_PRECISION_PCT) / 100);
        }

        /* CLEAN: no lives lost the whole level. */
        if (perf->hits_taken == 0) s_pay_clean = pct_of(base, STORY_BONUS_CLEAN_PCT);
    }

    int reward = s_pay_base + s_pay_speed + s_pay_combat +
                 s_pay_precision + s_pay_clean;
    /* Replays pay half, so grinding an easy level is never the fast route. */
    if (replay) {
        reward /= 2;
        s_pay_base /= 2;
        s_pay_speed /= 2;
        s_pay_combat /= 2;
        s_pay_precision /= 2;
        s_pay_clean /= 2;
    }

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

/* ── The wreck ────────────────────────────────────────────────────────────
 * Losing the last life used to claim it "reset" you to a checkpoint, which
 * it never really did.  It now does something concrete instead: the last two
 * levels you got through are re-locked, the money they paid is taken back,
 * and the ship goes into the yard for fifteen real minutes. */

static int s_repair_bill = 0;     /* chubbcoin the last wreck cost */
static int s_relocked = 0;        /* levels the last wreck took back */

int story_last_repair_bill(void) { return s_repair_bill; }
int story_last_relocked(void)    { return s_relocked; }

static void story_unmark_cleared(int level) {
    if (level < 1 || level > STORY_LEVEL_COUNT) return;
    if (story_is_cleared(level)) {
        g_story.cleared[(level - 1) >> 3] &= (u8)~(1u << ((level - 1) & 7));
        if (g_story.cleared_count > 0) g_story.cleared_count--;
    }
}

int story_wreck_ship(void) {
    /* The two levels behind the one that just went wrong: those are the ones
     * the wreck costs you.  Level 1 can never be taken away, so an early
     * wreck simply re-locks fewer levels. */
    int from = g_story.level;
    if (from < 1) from = 1;
    if (from > STORY_LEVEL_COUNT) from = STORY_LEVEL_COUNT;

    s_repair_bill = 0;
    s_relocked = 0;
    for (int i = 0; i < STORY_RELOCK_LEVELS; i++) {
        int lv = from - i;
        if (lv < 2) break;                    /* level 1 always stays open */
        if (!story_is_cleared(lv)) continue;  /* nothing banked, nothing lost */
        story_unmark_cleared(lv);
        s_repair_bill += g_story_levels[lv - 1].reward;
        s_relocked++;
    }

    /* Walk the unlock frontier back over everything just re-locked. */
    int unlocked = g_story.unlocked - s_relocked;
    if (unlocked < 1) unlocked = 1;
    g_story.unlocked = (u8)unlocked;

    /* Take back what those levels paid (never below zero). */
    if (s_repair_bill > 0) {
        if (g_story.chubbcoin > (u32)s_repair_bill) g_story.chubbcoin -= (u32)s_repair_bill;
        else { s_repair_bill = (int)g_story.chubbcoin; g_story.chubbcoin = 0; }
    }

    /* Resume at the earliest level the wreck took back, or where you were. */
    int resume = from - s_relocked + 1;
    if (resume < 1) resume = 1;
    if (resume > g_story.unlocked) resume = g_story.unlocked;
    g_story.level = (u8)resume;

    /* Into the yard: fifteen real minutes of repairs. */
    g_story.lives = STORY_START_LIVES;
    g_story.repair_until = platform_epoch_seconds() + STORY_REPAIR_SECONDS;

    save_write();
    return resume;
}

int story_repair_seconds_left(void) {
    if (g_story.repair_until == 0) return 0;
    u32 now = platform_epoch_seconds();
    if (now >= g_story.repair_until) {
        /* Repairs finished while we were away: hand the ship back. */
        g_story.repair_until = 0;
        return 0;
    }
    u32 left = g_story.repair_until - now;
    /* A clock that jumped backwards (device time change) must not strand the
     * player for longer than the repair ever takes. */
    if (left > (u32)STORY_REPAIR_SECONDS) {
        g_story.repair_until = now + STORY_REPAIR_SECONDS;
        left = STORY_REPAIR_SECONDS;
    }
    return (int)left;
}

bool story_is_grounded(void) { return story_repair_seconds_left() > 0; }

void story_finish_repairs(void) {
    if (g_story.repair_until != 0) {
        g_story.repair_until = 0;
        save_write();
    }
}

void story_format_repair(char* dst, int cap) {
    if (!dst || cap <= 0) return;
    int left = story_repair_seconds_left();
    if (left < 0) left = 0;
    snprintf(dst, (size_t)cap, "%d:%02d", left / 60, left % 60);
}

int story_lose_life(void) {
    if (g_story.lives > 0) g_story.lives--;
    if (g_story.lives > 0) {
        save_write();
        return g_story.level;              /* retry the same level */
    }
    /* Out of lives: the ship is a write-off. */
    return story_wreck_ship();
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

StoryEndingPhase story_ending_phase(void) {
    if (g_story.ending_phase > STORY_ENDING_RETURN_MENU)
        return STORY_ENDING_NONE;
    return (StoryEndingPhase)g_story.ending_phase;
}

void story_set_ending_phase(StoryEndingPhase phase) {
    if (phase < STORY_ENDING_NONE || phase > STORY_ENDING_RETURN_MENU)
        phase = STORY_ENDING_NONE;
    g_story.ending_phase = (u8)phase;
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
 * for that dock. He only ever shows a few things at a time — you never meet
 * him, just his prices.
 *
 * He docks once every five levels and that dock is a single visit: leaving
 * closes it permanently (g_story.docks_used) and the player flies the next
 * five levels without a shop. Unsold stock is NOT lost — it rides along to
 * whichever dock comes next. */

static StoryStockItem s_stock[STORY_SHOP_SLOTS];
static bool s_stock_held[STORY_SHOP_SLOTS];
static int s_stock_level = -1;
/* True once a dock has rolled its shelf at least this session; the shelf
 * outlives s_stock_level, which only marks the currently open dock. */
static bool s_stock_seeded = false;

/* Wipe the carried-over shelf (fresh campaign). */
static void story_shop_forget(void) {
    memset(s_stock, 0, sizeof(s_stock));
    memset(s_stock_held, 0, sizeof(s_stock_held));
    s_stock_level = -1;
    s_stock_seeded = false;
}

bool story_shop_can_open(int level) {
    int d = story_dock_index(level);
    if (d < 0 || d >= STORY_DOCK_COUNT) return false;
    return (g_story.docks_used & (u16)(1u << d)) == 0;
}

int story_shop_next_dock(int from_level) {
    if (from_level < 0) from_level = 0;
    for (int lv = from_level + 1; lv <= STORY_LEVEL_COUNT; lv++) {
        if (story_shop_can_open(lv)) return lv;
    }
    return 0;
}

void story_shop_close(void) {
    int d = story_dock_index(s_stock_level);
    if (d >= 0 && d < STORY_DOCK_COUNT) {
        u16 bit = (u16)(1u << d);
        if (!(g_story.docks_used & bit)) {
            g_story.docks_used |= bit;
            save_write();
        }
    }
    /* Keep s_stock/s_stock_held: the unsold shelf follows him to the next
     * dock.  Only the "which dock is open" marker is cleared. */
    s_stock_level = -1;
}

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

static int stock_trail_for(int dock, u32 r) {
    int top = 2 + dock / 3;
    if (top > NUM_TRAILS - 2) top = NUM_TRAILS - 2;
    return 1 + (int)(r % (u32)top);
}

static int stock_ship_for(int dock, u32 r) {
    int top = 1 + dock / 4;
    if (top > NUM_SHIP_STYLES - 1) top = NUM_SHIP_STYLES - 1;
    return 1 + (int)(r % (u32)top);
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
        case SSTOCK_TRAIL:   return 420 + it->item * 150 + dock * 45;
        case SSTOCK_SHIP:    return 900 + it->item * 420 + dock * 80;
        default:             return 0;
    }
}

/* Roll one gear slot for a dock. */
static void stock_roll_slot(StoryStockItem* it, int level, int i, int dock) {
    u32 r = stock_hash((u32)(level * 7919 + i * 104729));
    int kind_roll = (int)(r % 100u);
    if (kind_roll < 25)      it->kind = SSTOCK_WEAPON;
    else if (kind_roll < 43) it->kind = SSTOCK_LASER;
    else if (kind_roll < 61) it->kind = SSTOCK_UPGRADE;
    else if (kind_roll < 75) it->kind = SSTOCK_PAINT;
    else if (kind_roll < 89) it->kind = SSTOCK_TRAIL;
    else                     it->kind = SSTOCK_SHIP;

    u32 r2 = stock_hash(r ^ 0x9e3779b9u);
    switch (it->kind) {
        case SSTOCK_WEAPON:  it->item = (u8)stock_rig_for(dock, r2); break;
        case SSTOCK_LASER:   it->item = (u8)stock_laser_for(dock, r2); break;
        case SSTOCK_PAINT:   it->item = (u8)stock_paint_for(r2); break;
        case SSTOCK_TRAIL:   it->item = (u8)stock_trail_for(dock, r2); break;
        case SSTOCK_SHIP:    it->item = (u8)stock_ship_for(dock, r2); break;
        default:             it->item = (u8)stock_upgrade_for(r2); break;
    }
    it->qty = 1;
    it->price = (u16)stock_price(it, dock);
}

/* Has this item already been bought elsewhere (so it is dead stock)? */
static bool stock_is_dead(const StoryStockItem* it) {
    switch (it->kind) {
        case SSTOCK_WEAPON:  return (g_settings.owned_rigs & (1u << it->item)) != 0;
        case SSTOCK_LASER:   return (g_settings.owned_lasers & (1u << it->item)) != 0;
        case SSTOCK_PAINT:   return (g_settings.owned_accents & (1u << it->item)) != 0;
        case SSTOCK_UPGRADE: return g_settings.upgrade_levels[it->item] >= UPG_MAX_LEVEL;
        case SSTOCK_TRAIL:   return (g_settings.owned_trails & (1u << it->item)) != 0;
        case SSTOCK_SHIP:    return (g_settings.owned_ships & (1u << it->item)) != 0;
        default:             return false;
    }
}

void story_shop_open(int level) {
    if (level < 1) level = 1;
    if (level > STORY_LEVEL_COUNT) level = STORY_LEVEL_COUNT;
    /* He only docks every fifth level, and only once per dock. Callers ask
     * story_shop_can_open() first; this is the belt-and-braces guard. */
    if (!story_shop_can_open(level)) return;
    if (s_stock_level == level) return;        /* already open: same shelf */

    bool first_ever = !s_stock_seeded;
    s_stock_seeded = true;
    s_stock_level = level;

    /* Which dock this is, 1-based: level 4 is his first catch-up, 9 the
     * second, and so on. The shelf tier climbs with it. */
    int dock = story_dock_index(level) + 1;
    if (dock < 1) dock = 1;

    /* Slot 0 is always lives — cheap-ish, and strictly limited so you can't
     * simply buy your way through the campaign. */
    s_stock[0].kind = SSTOCK_LIFE;
    s_stock[0].item = 0;
    s_stock[0].qty = (u8)(1 + (dock % 2));     /* 1 or 2 in stock */
    s_stock[0].price = (u16)stock_price(&s_stock[0], dock);
    s_stock_held[0] = false;

    /* Slots 1..7: gear. Anything you walked past last time is still sitting
     * there — he does not clear the shelf just because you were broke. Only
     * sold, claimed or now-useless slots get restocked. */
    for (int i = 1; i < STORY_SHOP_SLOTS; i++) {
        StoryStockItem* it = &s_stock[i];
        bool keep = !first_ever && it->kind != SSTOCK_EMPTY && it->qty > 0 && !stock_is_dead(it);
        if (keep) {
            s_stock_held[i] = true;
            continue;
        }
        memset(it, 0, sizeof(*it));
        s_stock_held[i] = false;

        /* Roll until the slot is something the player can actually use AND
         * is not already sitting in another slot - two identical rows on a
         * eight-item shelf made the dock look broken. */
        int salt = 0;
        for (; salt < 8; salt++) {
            stock_roll_slot(it, level + salt * 13, i, dock);
            if (stock_is_dead(it)) continue;
            bool dupe = false;
            for (int j = 1; j < STORY_SHOP_SLOTS && !dupe; j++) {
                if (j == i) continue;
                const StoryStockItem* o = &s_stock[j];
                if (o->kind == it->kind && o->item == it->item && o->qty > 0) dupe = true;
            }
            if (!dupe) break;
        }
        if (salt >= 8) {
            /* Nothing distinct left to sell: fall back to a spare life. */
            memset(it, 0, sizeof(*it));
            it->kind = SSTOCK_LIFE;
            it->qty = 1;
            it->price = (u16)stock_price(it, dock);
        }
    }
}

int story_shop_level(void) { return s_stock_level > 0 ? s_stock_level : 0; }

bool story_shop_slot_held_over(int i) {
    if (i < 0 || i >= STORY_SHOP_SLOTS) return false;
    return s_stock_held[i];
}

/* The level you fly out of this dock is the one after it. */
bool story_shop_is_boss_dock(void) {
    return story_boss_dock(story_shop_level() + 1);
}

/* Mr Chubbs on the radio. Before a boss he stops haggling and talks Jack up;
 * the rest of the time he runs his Trading Post with a queue behind Jack. */
static const char* const s_chubb_pep1[STORY_SECTOR_COUNT] = {
    "JACK. THE ALIEN IS AHEAD.",
    "TWO OF THEM. STAY STEADY, JACK.",
    "KEEP WARM OR FROSTBITE WINS.",
    "JUGGERNAUT IS HUGE. FIND A GAP.",
    "INFERNO BURNS HOT. STAY COOL.",
    "AEGIS NEVER BLINKS. BLIND IT.",
    "THIS IS IT, JACK. THE REALITY QUEEN.",
    "ONE HUNDRED DRONES, JACK. THE LAST SKY."
};
static const char* const s_chubb_pep2[STORY_SECTOR_COUNT] = {
    "TAKE A LIFE. NO CHARGE. GO.",
    "TAKE A LIFE. COME BACK IN ONE PIECE.",
    "TAKE A LIFE. STAY WARM OUT THERE.",
    "TAKE A LIFE. DON'T GET CRUSHED.",
    "TAKE A LIFE. DON'T MELT, JACK.",
    "TAKE A LIFE. IT'S ON THE HOUSE.",
    "TAKE A LIFE. FINISH YOUR REVENGE.",
    "TAKE A LIFE. BRING IT HOME."
};
static const char* const s_chubb_idle[6] = {
    "STILL ALIVE, JACK? GOOD FOR TRADE.",
    "FRESH SCRAP. FAIR CHUBB PRICES.",
    "I KEPT WHAT YOU MISSED LAST TIME.",
    "BUY OR DON'T. THE STARS WAIT.",
    "I DOCK, YOU SPEND. THAT'S THE DEAL.",
    "NO REFUNDS IN THE CHUBB SYSTEM."
};

const char* story_shop_line1(void) {
    int next = story_shop_level() + 1;
    if (story_shop_is_boss_dock()) {
        int s = story_sector_of(next);
        return s_chubb_pep1[s];
    }
    return s_chubb_idle[story_shop_level() % 6];
}

const char* story_shop_line2(void) {
    int next = story_shop_level() + 1;
    if (story_shop_is_boss_dock()) {
        int s = story_sector_of(next);
        return s_chubb_pep2[s];
    }
    /* Always reminds the player this dock is a one-shot: leaving undocks him
     * until he catches up five levels later. */
    return "LEAVE AND I UNDOCK. STOCK KEEPS.";
}

int story_shop_take_gift(void) {
    if (!story_shop_is_boss_dock()) return 0;
    int boss = story_sector_of(story_shop_level() + 1);
    if (boss < 0 || boss >= STORY_SECTOR_COUNT) return 0;
    u8 bit = (u8)(1u << boss);
    if (g_story.boss_gifts & bit) return 0;    /* one gift per boss, ever */
    g_story.boss_gifts |= bit;
    story_add_lives(1);
    save_write();
    return 1;
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
        case SSTOCK_TRAIL:   return gfx_get_trail_name(it->item);
        case SSTOCK_SHIP:    return gfx_get_ship_style_name(it->item);
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
        case SSTOCK_TRAIL:   return gfx_get_trail_desc(it->item);
        case SSTOCK_SHIP:    return gfx_get_ship_style_desc(it->item);
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
        case SSTOCK_TRAIL:
            if (g_settings.owned_trails & (1u << it->item)) return 2;
            break;
        case SSTOCK_SHIP:
            if (g_settings.owned_ships & (1u << it->item)) return 2;
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
        case SSTOCK_TRAIL:
            g_settings.owned_trails |= (u16)(1u << it->item);
            g_settings.trail_index = it->item;
            break;
        case SSTOCK_SHIP:
            g_settings.owned_ships |= (u16)(1u << it->item);
            g_settings.ship_index = it->item;
            break;
        default: break;
    }

    it->qty--;
    save_write();
    return 0;
}
