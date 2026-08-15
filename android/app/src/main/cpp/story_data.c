#include "story.h"

/* ── The 70 levels ────────────────────────────────────────────────────────
 * Generated to a strict curve: difficulty rises monotonically, every tenth
 * level is a boss, and the mix rotates clear / hunt / survive so no sector
 * plays like the last one.  Field sizes are capped below the engine's
 * MAX_ASTEROIDS / MAX_DRONES budgets so every level can actually spawn. */
const StoryLevel g_story_levels[STORY_LEVEL_COUNT] = {
    { "FIRST LIGHT", OBJ_CLEAR, 6, 1, 0, 102, 104, 52 },
    { "SCRAP DRIFT", OBJ_CLEAR, 7, 1, 0, 104, 108, 59 },
    { "THIN AIR", OBJ_HUNT, 4, 3, 11, 106, 112, 66 },
    { "CHUBB RUN", OBJ_CLEAR, 8, 1, 0, 108, 116, 73 },
    { "THE NARROWS", OBJ_SURVIVE, 11, 1, 27, 110, 120, 80 },
    { "HOLLOW ROCKS", OBJ_CLEAR, 10, 1, 0, 112, 124, 87 },
    { "SIGNAL LOST", OBJ_HUNT, 7, 4, 15, 114, 128, 94 },
    { "DEBRIS TIDE", OBJ_SURVIVE, 13, 2, 29, 116, 132, 101 },
    { "THE APPROACH", OBJ_CLEAR, 16, 3, 0, 118, 136, 138 },
    { "RUSTJAW", OBJ_BOSS, 0, 0, 0, 120, 140, 500 },
    { "OPEN YARD", OBJ_CLEAR, 13, 2, 0, 122, 144, 122 },
    { "IRON RAIN", OBJ_CLEAR, 14, 2, 0, 124, 148, 129 },
    { "BENT GIRDERS", OBJ_HUNT, 11, 4, 21, 126, 152, 136 },
    { "HUNTER PATROL", OBJ_CLEAR, 16, 3, 0, 128, 156, 143 },
    { "DEAD ENGINES", OBJ_SURVIVE, 18, 3, 32, 130, 160, 150 },
    { "THE PILE UP", OBJ_CLEAR, 17, 3, 0, 132, 164, 157 },
    { "CUTTING LANE", OBJ_HUNT, 14, 5, 25, 134, 168, 164 },
    { "RED SHIFT", OBJ_SURVIVE, 20, 3, 34, 136, 172, 171 },
    { "TWIN SIGNALS", OBJ_CLEAR, 23, 4, 0, 138, 176, 208 },
    { "THE TWINS", OBJ_BOSS, 0, 0, 0, 140, 180, 750 },
    { "COLD START", OBJ_CLEAR, 21, 4, 0, 142, 184, 192 },
    { "GLASS SHARDS", OBJ_CLEAR, 21, 4, 0, 144, 188, 199 },
    { "MIRROR DRIFT", OBJ_HUNT, 18, 6, 31, 146, 192, 206 },
    { "SLOW BURN", OBJ_CLEAR, 23, 4, 0, 148, 196, 213 },
    { "FROST LINE", OBJ_SURVIVE, 26, 4, 37, 150, 200, 220 },
    { "WHITE OUT", OBJ_CLEAR, 24, 4, 0, 152, 204, 227 },
    { "SPLINTERS", OBJ_HUNT, 21, 6, 35, 154, 208, 234 },
    { "THE LONG COLD", OBJ_SURVIVE, 28, 5, 39, 156, 212, 241 },
    { "WEB OF ICE", OBJ_CLEAR, 30, 6, 0, 158, 216, 278 },
    { "FROSTWIDOW", OBJ_BOSS, 0, 0, 0, 160, 220, 1000 },
    { "SALVAGE RUN", OBJ_CLEAR, 28, 5, 0, 162, 224, 262 },
    { "MAGNET FIELD", OBJ_CLEAR, 29, 5, 0, 164, 228, 269 },
    { "BOLT STORM", OBJ_HUNT, 25, 6, 41, 166, 232, 276 },
    { "CRUSHER LANE", OBJ_CLEAR, 30, 5, 0, 168, 236, 283 },
    { "TOWED UNDER", OBJ_SURVIVE, 33, 6, 42, 170, 240, 290 },
    { "SCRAP HUNTERS", OBJ_CLEAR, 31, 6, 0, 172, 244, 297 },
    { "THE CONVEYOR", OBJ_HUNT, 28, 6, 45, 174, 248, 304 },
    { "HEAVY METAL", OBJ_SURVIVE, 34, 6, 44, 176, 252, 311 },
    { "TITAN SHADOW", OBJ_CLEAR, 34, 6, 0, 178, 256, 348 },
    { "SCRAP TITAN", OBJ_BOSS, 0, 0, 0, 180, 260, 1250 },
    { "FIRST BURN", OBJ_CLEAR, 34, 6, 0, 182, 264, 332 },
    { "SOLAR WIND", OBJ_CLEAR, 34, 6, 0, 184, 268, 339 },
    { "ASH CLOUD", OBJ_HUNT, 30, 6, 51, 186, 272, 346 },
    { "FLARE UP", OBJ_CLEAR, 34, 6, 0, 188, 276, 353 },
    { "THE FURNACE", OBJ_SURVIVE, 34, 6, 47, 190, 280, 360 },
    { "CINDER RUN", OBJ_CLEAR, 34, 6, 0, 190, 284, 367 },
    { "HEAT DEATH", OBJ_HUNT, 30, 6, 55, 190, 288, 374 },
    { "MELTLINE", OBJ_SURVIVE, 34, 6, 49, 190, 292, 381 },
    { "EMBER GATE", OBJ_CLEAR, 34, 6, 0, 190, 296, 418 },
    { "EMBERLASH", OBJ_BOSS, 0, 0, 0, 190, 300, 1500 },
    { "VAULT DOOR", OBJ_CLEAR, 34, 6, 0, 190, 304, 402 },
    { "DARK CORRIDOR", OBJ_CLEAR, 34, 6, 0, 190, 308, 409 },
    { "SILENT ALARM", OBJ_HUNT, 30, 6, 61, 190, 312, 416 },
    { "LOCKDOWN", OBJ_CLEAR, 34, 6, 0, 190, 316, 423 },
    { "THE STACKS", OBJ_SURVIVE, 34, 6, 52, 190, 320, 430 },
    { "GHOST FLEET", OBJ_CLEAR, 34, 6, 0, 190, 324, 437 },
    { "DEEP STORAGE", OBJ_HUNT, 30, 6, 65, 190, 328, 444 },
    { "NO LIGHT", OBJ_SURVIVE, 34, 6, 54, 190, 332, 451 },
    { "WARDEN WAKES", OBJ_CLEAR, 34, 6, 0, 190, 336, 488 },
    { "VAULT WARDEN", OBJ_BOSS, 0, 0, 0, 190, 340, 1750 },
    { "THIN REALITY", OBJ_CLEAR, 34, 6, 0, 190, 344, 472 },
    { "FOLDED SPACE", OBJ_CLEAR, 34, 6, 0, 190, 348, 479 },
    { "ECHO RUN", OBJ_HUNT, 30, 6, 71, 190, 352, 486 },
    { "THE UNMAKING", OBJ_CLEAR, 34, 6, 0, 190, 356, 493 },
    { "QUEENS GUARD", OBJ_SURVIVE, 34, 6, 57, 190, 360, 500 },
    { "SHATTERED SKY", OBJ_CLEAR, 34, 6, 0, 190, 364, 507 },
    { "NO TOMORROW", OBJ_HUNT, 30, 6, 75, 190, 368, 514 },
    { "THE LAST MILE", OBJ_SURVIVE, 34, 6, 59, 190, 372, 521 },
    { "GATE OPEN", OBJ_CLEAR, 34, 6, 0, 190, 376, 558 },
    { "REALITY QUEEN", OBJ_BOSS, 0, 0, 0, 190, 380, 2000 },
};

static const char* const s_sector_names[STORY_SECTOR_COUNT] = {
    "THE CHUBB BELT",
    "THE RUST YARDS",
    "THE ICE FIELDS",
    "THE SCRAPLINE",
    "EMBER REACH",
    "THE COLD VAULT",
    "THE REALITY GATE"
};

static const char* const s_boss_names[STORY_SECTOR_COUNT] = {
    "RUSTJAW",
    "THE TWINS",
    "FROSTWIDOW",
    "SCRAP TITAN",
    "EMBERLASH",
    "VAULT WARDEN",
    "THE REALITY QUEEN"
};

/* Nobody's face is ever shown - the Chubbs are only ever voices on the radio,
 * so the bosses only ever get one line of text each. */
static const char* const s_boss_taunts[STORY_SECTOR_COUNT] = {
    "SO THE LITTLE SHIP FLIES",
    "WE ARE TWO. YOU ARE ONE.",
    "HOLD STILL. IT IS WARMER FROZEN.",
    "YOU ARE SCRAP THAT MOVES",
    "BURN LIKE YOUR LAST PLANET DID",
    "THE VAULT DOES NOT OPEN",
    "JACK. MY JACK. COME HOME."
};

const char* story_sector_name(int sector) {
    if (sector < 0 || sector >= STORY_SECTOR_COUNT) return "UNKNOWN SPACE";
    return s_sector_names[sector];
}

const char* story_boss_name(int boss_id) {
    if (boss_id < 0 || boss_id >= STORY_SECTOR_COUNT) return "UNKNOWN";
    return s_boss_names[boss_id];
}

const char* story_boss_taunt(int boss_id) {
    if (boss_id < 0 || boss_id >= STORY_SECTOR_COUNT) return "";
    return s_boss_taunts[boss_id];
}

int story_boss_for_level(int level) {
    if (level <= 0 || level > STORY_LEVEL_COUNT) return -1;
    if ((level % STORY_SECTOR_LEVELS) != 0) return -1;
    return (level / STORY_SECTOR_LEVELS) - 1;
}

/* The opening speech. Kept in the player's own voice - plain, a bit rambling,
 * the way it was told - just given room to breathe across 14 pages. */
const char* const g_story_intro[STORY_INTRO_PAGES][2] = {
    { "Once upon a time,",            "in another universe..." },
    { "Aliens invaded",               "the Chubbs." },
    { "The Chubbs tried to fight",    "back against the Reality King." },
    { "They threw everything",        "they had at him." },
    { "But he was too strong.",       "Way too strong." },
    { "A wise person once said:",     "if you can't beat em, join em." },
    { "So the Chubbs decided",        "to befriend the Reality King." },
    { "Jack RK.",                     "Jack Arkey, they called him." },
    { "He knew a lot about tech",     "from his last planet." },
    { "So he knows how",              "to build a space ship." },
    { "And he wanted revenge.",       "He wanted it badly." },
    { "So he built the ship.",        "The starter ship." },
    { "Nothing special. It flies,",   "it shoots, and it is his." },
    { "And he set aflight,",          "to try and get revenge..." }
};
