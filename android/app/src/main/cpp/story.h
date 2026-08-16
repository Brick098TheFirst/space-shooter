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
 * spawning once the quota is met, and survive/timed levels end on a clock. */
typedef enum {
    OBJ_CLEAR   = 0,  /* destroy every rock and hunter in the field */
    OBJ_HUNT    = 1,  /* destroy N hunters (rocks keep coming as pressure) */
    OBJ_SURVIVE = 2,  /* stay alive for N seconds against endless spawns */
    OBJ_BOSS    = 3,  /* single unique boss, no field spawns */
    /* Break the big ones only: the mediums and debris they shed are just
     * weather. Quota is the number of LARGE rocks to crack. */
    OBJ_BIGGAME = 4,
    /* Clear the field against a countdown. The only objective with a real
     * fail state that is not "you died", so it plays quite differently. */
    OBJ_TIMED   = 5,
    /* Puzzle levels: ten of them are scattered through the campaign as a
     * change of pace.  The puzzle variant lives in the level's `modifier`
     * (MOD_PZ_*) and `quota` carries the variant's number (ammo budget,
     * signal count, or gauntlet seconds).  See story_update_objective(). */
    OBJ_PUZZLE  = 6
} StoryObjective;

/* ── Level modifiers ──────────────────────────────────────────────────────
 * A twist layered on top of the objective so two CLEAR levels never feel
 * like the same level with more rocks.  Applied by the spawners and the
 * objective tick in game.c. */
typedef enum {
    MOD_NONE     = 0,
    MOD_BOULDERS = 1,  /* the field is nearly all big rocks               */
    MOD_SHARDS   = 2,  /* no big rocks at all - fast mediums, everywhere  */
    MOD_SWARM    = 3,  /* hunters keep coming, well past the usual cap    */
    MOD_SWIFT    = 4,  /* everything moves a lot faster                   */
    MOD_TOUGH    = 5,  /* armoured rocks: far more hits to break          */
    MOD_TRICKLE  = 6,  /* the field arrives slowly, a few at a time       */
    MOD_STORM    = 7,  /* heavy continuous reinforcement                  */
    MOD_SNIPERS  = 8,  /* fewer hunters, but they fire much more often    */
    /* ── Puzzle variants (only valid with OBJ_PUZZLE) ────────────────── */
    MOD_PZ_SALVO    = 9,   /* LIMITED AMMO: clear the field on a shot budget */
    MOD_PZ_SIGNAL   = 10,  /* SIGNAL HUNT: only the marked rock counts       */
    MOD_PZ_GAUNTLET = 11,  /* GUNS OFFLINE: weave a scripted bullet maze     */
    MOD_COUNT    = 12
} StoryModifier;

/* Short label for the HUD banner and the map card, e.g. "BOULDER FIELD". */
const char* story_modifier_name(int mod);

typedef struct {
    const char* name;
    /* Two lines of radio chatter shown on the map card and again as the
     * level opens, so the campaign reads as a story and not a level list. */
    const char* brief1;
    const char* brief2;
    u8  objective;    /* StoryObjective */
    u8  rocks;        /* asteroids in the opening field */
    u8  drones;       /* hunters in the opening field */
    u8  quota;        /* HUNT: kills. SURVIVE/TIMED: seconds. BIGGAME: bigs. */
    u8  speed_pct;    /* enemy speed scalar, 100 = baseline */
    u16 hp_pct;       /* enemy HP scalar, 100 = baseline */
    u16 reward;       /* chubbcoin paid for a first clear */
    u8  modifier;     /* StoryModifier - the level's twist */
} StoryLevel;

/* ── The seven bosses ─────────────────────────────────────────────────────
 * Levels 10/20/30/40/50/60/70.  Each has its own attack script, movement
 * and a KEY MECHANIC no other boss uses — see story_boss_ai() in game.c. */
typedef enum {
    SBOSS_IRONMAW = 0,   /* L10 - BITE: jaws lunge and clamp a no-fly zone */
    SBOSS_GEMINI,        /* L20 - SPLIT: two mirrored hulls, shared pool */
    SBOSS_FROSTBITE,     /* L30 - FREEZE: engine icing slows your ship */
    SBOSS_JUGGERNAUT,    /* L40 - CRUSH: armour plates + magnet drag */
    SBOSS_INFERNO,       /* L50 - BURN: ever-rotating fire whips */
    SBOSS_AEGIS,         /* L60 - SEAL: invulnerable until nodes fall */
    SBOSS_EMPRESS        /* L70 - UNMAKE: reality tears, 3 stage finale */
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
 * previous boss (1, 11, 21, ...).  Kept for save repair and older callers;
 * losing a run no longer sends you back here (see story_wreck_ship()). */
static inline int story_checkpoint_for(int level) {
    return story_sector_of(level) * STORY_SECTOR_LEVELS + 1;
}

/* ── Losing the run: the repair yard ─────────────────────────────────────
 * Running the life pool dry no longer throws the campaign back to a
 * checkpoint.  Instead the wreck costs you ground, money and time:
 *
 *   - the last TWO levels you flew are RE-LOCKED (their clears are wiped and
 *     the unlock frontier walks back two), so you fly them again;
 *   - the chubbcoin those two levels paid is taken back off the balance;
 *   - the ship is grounded for fifteen real minutes while it is repaired.
 *
 * The repair clock is wall-clock, so it keeps ticking with the app closed. */
#define STORY_RELOCK_LEVELS 2
#define STORY_REPAIR_SECONDS (15 * 60)

/* ── Mr Chubbs' docking schedule ──────────────────────────────────────────
 * He is not a shop you can wander back into.  His ship catches up with you
 * once every five levels and that dock is a ONE TIME visit: when you leave
 * it, he undocks and you fly the next five levels alone.  Anything you did
 * not buy stays on his shelf and is still there at the next dock.
 *
 * A dock opens on the clear of levels 4, 9, 14, 19, ... - i.e. it sits in
 * the FIFTH slot of every group of five, which also puts one immediately
 * before each boss (9 -> Ironmaw, 19 -> Gemini, and so on) so the
 * pre-boss pep talk and its free life still happen. */
#define STORY_SHOP_INTERVAL 5

static inline bool story_shop_at(int level) {
    return level > 0 && level <= STORY_LEVEL_COUNT &&
           (level % STORY_SHOP_INTERVAL) == (STORY_SHOP_INTERVAL - 1);
}

/* The dock before a boss is the big one: he talks you up and hands over a
 * spare life for nothing. */
static inline bool story_boss_dock(int next_level) {
    return next_level > 0 && next_level <= STORY_LEVEL_COUNT &&
           (next_level % STORY_SECTOR_LEVELS) == 0;
}

/* ── Kingdom backdrops ────────────────────────────────────────────────────
 * Each of the seven sectors flies over its own sky (see gba/src/starfield.c).
 * Sector 0 -> SF_THEME_BELT, and so on in order, so adding a sector needs no
 * second table. */
static inline int story_theme_for_sector(int sector) {
    if (sector < 0) sector = 0;
    if (sector >= STORY_SECTOR_COUNT) sector = STORY_SECTOR_COUNT - 1;
    return 1 + sector;   /* SF_THEME_BELT .. SF_THEME_REALITY */
}

static inline int story_theme_for_level(int level) {
    return story_theme_for_sector(story_sector_of(level));
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
    /* Wall-clock second (epoch) the repair yard hands the ship back.  0 when
     * the ship is spaceworthy.  See story_wreck_ship(). */
    u32 repair_until;
    /* One bit per dock (level 5, 10, ... 70).  Set the moment the player
     * leaves that dock: he does not come back for it. */
    u16 docks_used;
    u16 pad;
} StorySave;

/* 70 levels / one dock every 5 = 14 docks, so the mask fits a u16. */
#define STORY_DOCK_COUNT (STORY_LEVEL_COUNT / STORY_SHOP_INTERVAL)

/* 0-based dock index for a dock level (-1 when that level has no dock).
 * Level 4 -> 0, level 9 -> 1, ... level 69 -> 13. */
static inline int story_dock_index(int level) {
    if (!story_shop_at(level)) return -1;
    return level / STORY_SHOP_INTERVAL;
}

extern StorySave g_story;

void story_init(void);            /* clamp/repair loaded save fields */
void story_reset_progress(void);  /* wipe the campaign back to level 1 */

int  story_current_level(void);   /* 1..70 - the level the map cursor sits on */
void story_set_current_level(int level);
int  story_highest_unlocked(void);/* furthest level the player may fly */
bool story_is_cleared(int level);
bool story_is_unlocked(int level);
/* Android-only code entry uses this to open the map without marking levels
 * cleared or paying their rewards. */
void story_unlock_all_levels(void);

/* ── Dynamic payouts ─────────────────────────────────────────────────────
 * A level's `reward` is a floor, not a flat fee.  What you actually bank
 * depends on how you flew it, so two clears of the same level are worth
 * different money:
 *
 *   SPEED    finishing well inside the level's par time pays up to +60%.
 *            (Timed/clear/hunt/big-game levels: the quicker, the richer.)
 *   COMBAT   what you actually destroyed, measured against the level's own
 *            expected body count.  This is what stops SURVIVE levels paying
 *            full price for hiding in a corner for the whole clock: idling
 *            out the timer banks the floor and nothing else.
 *   PRECISION  accuracy over the level, up to +20%.
 *   CLEAN    finishing without losing a single life, +25%.
 *
 * Replays still halve the whole payout. */
typedef struct {
    u16 secs;        /* how long the clear took, in seconds */
    u16 par_secs;    /* the level's par time (0 = no speed bonus) */
    u16 kills;       /* rocks + hunters + boss parts destroyed */
    u16 par_kills;   /* the body count the level expects (0 = no bonus) */
    u16 shots;       /* projectiles fired */
    u16 hits;        /* projectiles that connected */
    u8  hits_taken;  /* lives lost during the level */
} StoryPerf;

/* Bank a clear. Pays the dynamic reward (halved when the level was already
 * beaten) and advances the unlock frontier. `perf` may be NULL, in which
 * case only the floor is paid. Returns the chubbcoin actually paid. */
int  story_complete_level(int level, const StoryPerf* perf);

/* Breakdown of the most recent payout, for the result card. */
int  story_pay_base(void);
int  story_pay_speed(void);
int  story_pay_combat(void);
int  story_pay_precision(void);
int  story_pay_clean(void);

/* Life pool. Losing the last life bumps the player back to the level right
 * after the previous boss and refills the pool. */
int  story_lives(void);
void story_add_lives(int n);
int  story_lose_life(void);       /* returns the level to resume at */

/* Total wreck: re-lock the last two levels, claw back what they paid, and
 * ground the ship for STORY_REPAIR_SECONDS.  Returns the level the player
 * will resume at once the repairs finish. */
int  story_wreck_ship(void);
/* Seconds until the ship is flyable again (0 when it already is). */
int  story_repair_seconds_left(void);
/* True while the ship is in the yard: no level may be launched. */
bool story_is_grounded(void);
/* Chubbcoin the last wreck cost, for the result card. */
int  story_last_repair_bill(void);
/* How many levels the last wreck re-locked. */
int  story_last_relocked(void);
/* Debug/settings escape hatch: hand the ship back early. */
void story_finish_repairs(void);
/* "12:34" - the repair countdown, for the map and the result card. */
void story_format_repair(char* dst, int cap);

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
 * Anything bought here also unlocks in the arcade hangar afterwards.
 *
 * One dock, one visit: leaving closes it for good and you fly on to the next
 * five levels alone.  The shelf itself is not lost - whatever you left on it
 * is still there when he catches up again. */

/* A full travelling inventory. The dock UI shows five rows at a time and
 * scrolls through the rest, so Chubbs can carry a useful mix instead of
 * arriving with the same tiny four-item shelf. */
#define STORY_SHOP_SLOTS 8

typedef enum {
    SSTOCK_EMPTY = 0,
    SSTOCK_LIFE,     /* +1 spare life, limited quantity per dock */
    SSTOCK_WEAPON,   /* a weapon rig  */
    SSTOCK_LASER,    /* a laser crystal */
    SSTOCK_PAINT,    /* a paint */
    SSTOCK_UPGRADE,  /* one level of a stat upgrade */
    SSTOCK_TRAIL,    /* an engine trail */
    SSTOCK_SHIP      /* a complete hull style */
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

/* One-time-visit bookkeeping.  story_shop_close() is called the moment the
 * player leaves the dock; after that story_shop_can_open() reports false for
 * that dock forever, so there is no walking back in for a second look. */
void story_shop_close(void);
bool story_shop_can_open(int level);
/* The next level at which he will catch up with you again (0 when the
 * campaign has no more docks ahead). */
int  story_shop_next_dock(int from_level);
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
 * Typed out one page at a time over the starfield, character by character.
 * Every page is exactly two lines: line 0 in white, line 1 in blue.
 *
 * Lines may carry two inline markers, stripped before they are drawn:
 *   *bold*   - heavier, shadowed text
 *   !faint!  - dim grey text
 * Markers never count as characters, so the typewriter paces identically on
 * marked-up and plain pages. */
#define STORY_INTRO_PAGES 14
extern const char* const g_story_intro[STORY_INTRO_PAGES][2];

/* Per-character styles produced by story_intro_markup(). */
#define STORY_MK_PLAIN 0
#define STORY_MK_BOLD  1
#define STORY_MK_FAINT 2

/* Longest plain story line + terminator; keeps every caller's buffer sane. */
#define STORY_INTRO_LINE_MAX 64

/* Strip markers from src into dst (cap bytes incl. terminator), optionally
 * filling spans[] with one STORY_MK_* byte per emitted character.  Returns
 * the plain-text length written. */
int story_intro_markup(const char* src, char* dst, u8* spans, int cap);
/* Plain, marker-free length of a story line. */
int story_intro_len(const char* src);

#endif
