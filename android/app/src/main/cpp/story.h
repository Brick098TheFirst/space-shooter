#ifndef SPACE_UNLIMITED_STORY_H
#define SPACE_UNLIMITED_STORY_H

/* ── STORY MODE (Android only) ────────────────────────────────────────────
 * A 70-level campaign with 7 themed sectors, a unique boss every 10 levels,
 * a fly-the-ship level map, and Mr Chubbs' shop docked before each boss.
 *
 * The story owns its own currency (CHUBBCOIN) and its own life pool; the
 * arcade coin balance is untouched.  Everything persists in the V9 save.  */

#include "types.h"

#define STORY_LEVEL_COUNT   70
#define STORY_SECTOR_COUNT  7
#define STORY_SECTOR_LEVELS 10
#define STORY_START_LIVES   3
#define STORY_MAX_LIVES     9

/* Objective kinds. Every level is beatable: clear/hunt objectives stop
 * spawning once the quota is met, and survive levels always end on a timer. */
typedef enum {
    OBJ_CLEAR   = 0,  /* destroy every rock and hunter in the field */
    OBJ_HUNT    = 1,  /* destroy N hunters (rocks keep coming as pressure) */
    OBJ_SURVIVE = 2,  /* stay alive for N seconds against endless spawns */
    OBJ_BOSS    = 3   /* single unique boss, no field spawns */
} StoryObjective;

typedef struct {
    const char* name;
    /* Two lines of radio chatter shown on the map card and again as the
     * level opens, so the campaign reads as a story and not a level list. */
    const char* brief1;
    const char* brief2;
    u8  objective;    /* StoryObjective */
    u8  rocks;        /* asteroids in the opening field */
    u8  drones;       /* hunters in the opening field */
    u8  quota;        /* HUNT: kills required.  SURVIVE: seconds. */
    u8  speed_pct;    /* enemy speed scalar, 100 = baseline */
    u16 hp_pct;       /* enemy HP scalar, 100 = baseline */
    u16 reward;       /* chubbcoin paid for a first clear */
} StoryLevel;

/* ── The seven bosses ─────────────────────────────────────────────────────
 * Levels 10/20/30/40/50/60/70.  Each has its own attack script, movement
 * and gimmick — see story_boss_ai() in story.c. */
typedef enum {
    SBOSS_RUSTJAW = 0,    /* L10 - jaw slam + scrap spit */
    SBOSS_TWINS,          /* L20 - splits into two halves at 50% */
    SBOSS_FROSTWIDOW,     /* L30 - ice web, freezing shards */
    SBOSS_SCRAPTITAN,     /* L40 - magnet pull + armour plates */
    SBOSS_EMBERLASH,      /* L50 - rotating flame whips */
    SBOSS_VAULTWARDEN,    /* L60 - shielded, four turret nodes */
    SBOSS_REALITYQUEEN    /* L70 - three cinematic phases */
} StoryBossId;

extern const StoryLevel g_story_levels[STORY_LEVEL_COUNT];

const char* story_sector_name(int sector);
const char* story_boss_name(int boss_id);
const char* story_boss_taunt(int boss_id);
int         story_boss_for_level(int level);   /* -1 when not a boss level */

/* Sector a 1-based level belongs to (0..6). */
static inline int story_sector_of(int level) {
    int s = (level - 1) / STORY_SECTOR_LEVELS;
    if (s < 0) s = 0;
    if (s >= STORY_SECTOR_COUNT) s = STORY_SECTOR_COUNT - 1;
    return s;
}

/* The level you respawn at after losing every life: the one right after the
 * previous boss (1, 11, 21, ...). */
static inline int story_checkpoint_for(int level) {
    return story_sector_of(level) * STORY_SECTOR_LEVELS + 1;
}

/* Mr Chubbs now docks after EVERY level - you always get a look at the
 * shelf before flying on.  Anything you leave on it rides along to the next
 * dock, so skipping a shop never costs you the stock. */
static inline bool story_shop_at(int level) {
    return level > 0 && level <= STORY_LEVEL_COUNT;
}

/* The dock before a boss is the big one: he talks you up and hands over a
 * spare life for nothing. */
static inline bool story_boss_dock(int next_level) {
    return next_level > 0 && next_level <= STORY_LEVEL_COUNT &&
           (next_level % STORY_SECTOR_LEVELS) == 0;
}


/* ── Runtime state & progression ─────────────────────────────────────────
 * All of this is persisted in the V9 save block (see save.c). */

/* Campaign save block. Kept plain and byte-stable so save.c can copy it
 * straight into the V9 layout on every compiler. */
typedef struct {
    u8  level;          /* 1..70 - map cursor / level to fly next */
    u8  unlocked;       /* 1..70 - furthest level reachable */
    u8  lives;          /* story life pool */
    u8  cleared_count;  /* how many distinct levels are beaten */
    u8  cleared[9];     /* 70-level bitmask */
    u8  intro_seen;     /* the opening speech has played once */
    u8  freed;          /* "LET ME BE FREE" tapped 3x in settings */
    u8  boss_gifts;     /* bitmask: bosses whose free life has been handed out */
    u32 chubbcoin;      /* story-only currency */
} StorySave;

extern StorySave g_story;

void story_init(void);            /* clamp/repair loaded save fields */
void story_reset_progress(void);  /* wipe the campaign back to level 1 */

int  story_current_level(void);   /* 1..70 - the level the map cursor sits on */
void story_set_current_level(int level);
int  story_highest_unlocked(void);/* furthest level the player may fly */
bool story_is_cleared(int level);
bool story_is_unlocked(int level);

/* Bank a clear. Pays the reward (halved when the level was already beaten)
 * and advances the unlock frontier. Returns the chubbcoin actually paid. */
int  story_complete_level(int level);

/* Life pool. Losing the last life bumps the player back to the level right
 * after the previous boss and refills the pool. */
int  story_lives(void);
void story_add_lives(int n);
int  story_lose_life(void);       /* returns the level to resume at */

u32  story_chubbcoin(void);
void story_award(int amount);
bool story_spend(int amount);

bool story_is_finished(void);     /* level 70 cleared */
bool story_content_unlocked(void);/* finished OR "LET ME BE FREE" used */
void story_free_everything(void); /* the settings escape hatch */
bool story_intro_seen(void);
void story_mark_intro_seen(void);

/* ── Mr Chubbs' Shop ─────────────────────────────────────────────────────
 * His little ship docks every 5 levels with a small rotating stock. You
 * never see him; the shop is a radio window with a docked-ship silhouette.
 * Anything bought here also unlocks in the arcade hangar afterwards. */

#define STORY_SHOP_SLOTS 4

typedef enum {
    SSTOCK_EMPTY = 0,
    SSTOCK_LIFE,     /* +1 spare life, limited quantity per dock */
    SSTOCK_WEAPON,   /* a weapon rig  */
    SSTOCK_LASER,    /* a laser crystal */
    SSTOCK_PAINT,    /* a paint */
    SSTOCK_UPGRADE   /* one level of a stat upgrade */
} StoryStockKind;

typedef struct {
    u8  kind;      /* StoryStockKind */
    u8  item;      /* rig / laser / paint / upgrade index */
    u8  qty;       /* remaining stock (lives are limited) */
    u16 price;     /* chubbcoin */
} StoryStockItem;

/* Open the dock for a level. Idempotent for a given level: re-entering the
 * same dock shows the same shelf. Stock you did NOT buy stays on the shelf
 * at the next dock too - only sold/claimed slots are restocked. */
void story_shop_open(int level);
const StoryStockItem* story_shop_slot(int i);
const char* story_shop_slot_name(int i);
const char* story_shop_slot_desc(int i);
/* True when this slot was already sitting on the shelf at the last dock. */
bool story_shop_slot_held_over(int i);
/* 0 = bought, 1 = too poor, 2 = sold out / already owned. */
int  story_shop_buy(int i);

/* The dock currently open (0 when none). */
int  story_shop_level(void);
/* True when the level you fly next out of this dock is a boss. */
bool story_shop_is_boss_dock(void);
/* Mr Chubbs' two lines of radio for this dock - a pep talk before a boss,
 * a shopkeeper's grumble otherwise. */
const char* story_shop_line1(void);
const char* story_shop_line2(void);
/* Boss docks hand over one free life, once per boss. Returns 1 the frame
 * the gift lands so the UI can announce it. */
int  story_shop_take_gift(void);


/* ── The opening speech ───────────────────────────────────────────────────
 * Typed out one page at a time over the starfield. Two lines per page. */
#define STORY_INTRO_PAGES 14
extern const char* const g_story_intro[STORY_INTRO_PAGES][2];

#endif
