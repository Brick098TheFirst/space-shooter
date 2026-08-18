#include "story.h"

/* ── The 70 levels ────────────────────────────────────────────────────────
 * Difficulty rises monotonically and every tenth level is a boss. Five nodes
 * in every sector are deliberately puzzles, making 35/70 levels puzzle
 * levels overall. The puzzle rules are hand-picked so the same rule appears
 * at most twice: target order, type locks, paired signals, a drone-only code,
 * shot-path tricks, moving safe zones, fragile cargo and seven different
 * learnable no-guns patterns all get their own nodes.
 *
 * Field sizes stay inside the engine's MAX_ASTEROIDS / MAX_DRONES budgets,
 * and tools/story_sim proves every level still has a terminating objective. */
const StoryLevel g_story_levels[STORY_LEVEL_COUNT] = {
    { "HOME ORBIT",
      "Jack checks the starter ship.",
      "The Chubbs watch him launch.",
      OBJ_CLEAR, 6, 1, 0, 102, 104, 52, MOD_NONE },
    { "DRIFTING KEYS",
      "Old satellites still answer in code.",
      "Break the locks in their lit order.",
      OBJ_PUZZLE, 8, 0, 5, 104, 108, 65, MOD_PZ_ORDER },
    { "ALIEN SCOUTS",
      "Alien scouts circle the system.",
      "Keep them away from the Chubbs.",
      OBJ_HUNT, 5, 3, 12, 106, 112, 72, MOD_NONE },
    { "PAINTED GATES",
      "The belt flashes three hull colours.",
      "Only the cyan code is safe to break.",
      OBJ_PUZZLE, 10, 0, 6, 108, 116, 80, MOD_PZ_COLOR },
    { "CHUBB CROSSING",
      "Chubb freighters need this lane.",
      "Hold the aliens off as they pass.",
      OBJ_SURVIVE, 15, 2, 28, 110, 120, 95, MOD_SHARDS },
    { "LOST RELAY",
      "The relay spared nine charge cells.",
      "Make every single shot count.",
      OBJ_PUZZLE, 6, 0, 9, 112, 124, 105, MOD_PZ_SALVO },
    { "LONG SHOTS",
      "Alien hunters guard the lane.",
      "Stay moving, Jack.",
      OBJ_HUNT, 8, 4, 16, 114, 128, 115, MOD_SNIPERS },
    { "BOUNCE VECTOR",
      "The launch window folds its walls.",
      "Use the returning shot path.",
      OBJ_PUZZLE, 12, 0, 10, 116, 132, 130, MOD_PZ_RICOCHET },
    { "CORNER MAZE",
      "The route closes behind Jack.",
      "No guns. Learn the three safe figures.",
      OBJ_PUZZLE, 0, 0, 32, 118, 136, 160, MOD_PZ_GAUNTLET },
    { "IRONMAW",
      "Ironmaw guards the way out.",
      "Jack, break those iron jaws.",
      OBJ_BOSS, 0, 0, 0, 120, 140, 500, MOD_NONE },
    { "YARD GATES",
      "The Rust Yards start with a wall.",
      "Scrap crews have sealed the route.",
      OBJ_CLEAR, 15, 3, 0, 122, 144, 140, MOD_TOUGH },
    { "ANCHOR RIDDLE",
      "Cargo anchors blink through the rust.",
      "Break the marked signal, then scan again.",
      OBJ_PUZZLE, 12, 0, 6, 124, 148, 155, MOD_PZ_SIGNAL },
    { "HUNTER NEST",
      "Alien hunters roost in the wrecks.",
      "Do not let them box Jack in.",
      OBJ_SURVIVE, 15, 8, 25, 126, 152, 165, MOD_SWARM },
    { "TWIN BEACONS",
      "Two beacons pulse in the yard.",
      "Clear both lights before the next pair wakes.",
      OBJ_PUZZLE, 12, 0, 5, 128, 156, 180, MOD_PZ_TWIN },
    { "DEAD ENGINES",
      "One engine signal hides in the scrap.",
      "Break the lowest anchor before it fades.",
      OBJ_PUZZLE, 14, 0, 6, 130, 160, 200, MOD_PZ_ANCHOR },
    { "SHIP GRAVEYARD",
      "Old Chubb ships rest in this yard.",
      "Clear the weapons from their graves.",
      OBJ_CLEAR, 22, 4, 0, 132, 164, 215, MOD_STORM },
    { "LINK BY LINK",
      "Scrap chains cross the lane.",
      "Follow the next link, not the tempting one.",
      OBJ_PUZZLE, 14, 0, 7, 134, 168, 230, MOD_PZ_CHAIN },
    { "DRONE PASSWORD",
      "The yard fighters carry a live code.",
      "Decode enough hulls before the signal shifts.",
      OBJ_PUZZLE, 0, 5, 10, 136, 172, 250, MOD_PZ_DRONECODE },
    { "DOUBLE SIGNAL",
      "Two command signals fill the yard.",
      "Jack follows them to their source.",
      OBJ_CLEAR, 26, 5, 0, 138, 176, 280, MOD_SWIFT },
    { "GEMINI",
      "Gemini runs the Rust Yards.",
      "Two hulls, one way through.",
      OBJ_BOSS, 0, 0, 0, 140, 180, 750, MOD_NONE },
    { "FROZEN DRIFT",
      "The Ice Fields swallow all heat.",
      "Frost creeps across Jack's hull.",
      OBJ_CLEAR, 30, 5, 0, 142, 184, 300, MOD_SHARDS },
    { "SPLIT DECISION",
      "Ice slabs hide thin decoys.",
      "Crack only the large slabs.",
      OBJ_PUZZLE, 14, 0, 5, 144, 188, 320, MOD_PZ_SPLIT },
    { "WHITEOUT WING",
      "Hunter trails vanish in the snow.",
      "Jack tracks them by engine noise.",
      OBJ_HUNT, 22, 8, 36, 146, 192, 340, MOD_SWARM },
    { "SAFE-LANE SHIFT",
      "A cold corridor slides across the field.",
      "Shoot while you stay inside the moving gap.",
      OBJ_PUZZLE, 12, 0, 8, 148, 196, 360, MOD_PZ_LANE },
    { "GLACIER RUN",
      "Great slabs of ice block the route.",
      "Break the thickest ones apart.",
      OBJ_BIGGAME, 30, 3, 4, 150, 200, 380, MOD_NONE },
    { "SNOWBLIND",
      "Ice dust blinds everything but radar.",
      "Trust the mark. Ignore the rest.",
      OBJ_PUZZLE, 14, 0, 7, 152, 204, 400, MOD_PZ_SIGNAL },
    { "CLOCKWORK ICE",
      "Frozen gears turn the whole field.",
      "Empty the chamber before the clock locks.",
      OBJ_PUZZLE, 14, 0, 90, 154, 208, 420, MOD_PZ_CLOCK },
    { "COLD CHASE",
      "The ice storm is gaining on Jack.",
      "Do not stop for anything.",
      OBJ_SURVIVE, 36, 6, 45, 156, 212, 450, MOD_STORM },
    { "RING FROST",
      "The web opens in perfect circles.",
      "No guns. Read the gaps in the rings.",
      OBJ_PUZZLE, 0, 0, 36, 158, 216, 480, MOD_PZ_RINGS },
    { "FROSTBITE",
      "Frostbite waits under the ice.",
      "Jack brings the heat.",
      OBJ_BOSS, 0, 0, 0, 160, 220, 1000, MOD_NONE },
    { "SCRAPLINE GATE",
      "The Scrapline never stops moving.",
      "Jack enters between two haulers.",
      OBJ_BIGGAME, 32, 3, 5, 162, 224, 500, MOD_TOUGH },
    { "MAGNET COMBO",
      "Charged scrap pulls every loose shot.",
      "Build a clean chain of ten breaks.",
      OBJ_PUZZLE, 16, 0, 10, 164, 228, 530, MOD_PZ_COMBO },
    { "BOLT RUN",
      "Bolt storms jam every weapon rail.",
      "Nothing to shoot. Find the moving gaps.",
      OBJ_PUZZLE, 0, 0, 42, 166, 232, 560, MOD_PZ_WALLS },
    { "SIEVE THE SCRAP",
      "The useful signal is smaller than dust.",
      "Break only the tiny marked pieces.",
      OBJ_PUZZLE, 16, 0, 8, 168, 236, 590, MOD_PZ_SIEVE },
    { "BROKEN LINK",
      "Chubb cargo is chained in transit.",
      "Break the chain and free the ships.",
      OBJ_SURVIVE, 36, 8, 48, 170, 240, 620, MOD_SWARM },
    { "ORBITAL YARD",
      "A beacon circles the scrapline.",
      "Stay in its orbit while you clear the rocks.",
      OBJ_PUZZLE, 16, 0, 8, 172, 244, 650, MOD_PZ_ORBIT },
    { "SUPPLY LINE",
      "Parts flow toward the kingdom.",
      "Jack cuts off the supply.",
      OBJ_HUNT, 32, 8, 52, 174, 248, 680, MOD_SWIFT },
    { "CHAINED CROSSING",
      "Two cutting lines cross the hot lane.",
      "No guns. Thread the diagonal scissors.",
      OBJ_PUZZLE, 0, 0, 48, 176, 252, 710, MOD_PZ_SCISSOR },
    { "TITAN TRACKS",
      "Huge tracks scar the Scrapline.",
      "Follow them to the machine.",
      OBJ_BIGGAME, 38, 4, 6, 178, 256, 750, MOD_STORM },
    { "JUGGERNAUT",
      "The Juggernaut blocks every lane.",
      "Bring the whole machine down.",
      OBJ_BOSS, 0, 0, 0, 180, 260, 1250, MOD_NONE },
    { "EMBER BORDER",
      "Ember Reach glows red ahead.",
      "Jack opens the cooling vents.",
      OBJ_CLEAR, 38, 8, 0, 182, 264, 800, MOD_SWIFT },
    { "FRAGILE HAUL",
      "Chubb cargo is stacked behind the rocks.",
      "Clear the route before anything reaches the hold.",
      OBJ_PUZZLE, 18, 0, 12, 184, 268, 840, MOD_PZ_FRAGILE },
    { "ASH HUNTERS",
      "Fire-dark fighters cross the glow.",
      "Jack hunts them through the ash.",
      OBJ_HUNT, 34, 8, 58, 186, 272, 880, MOD_SWARM },
    { "MOLTEN DRIFT",
      "The heat cooks Jack's capacitors.",
      "Twelve shots. No spare trigger pulls.",
      OBJ_PUZZLE, 10, 0, 12, 188, 276, 920, MOD_PZ_LASTSHOT },
    { "HOT SEAT",
      "Heat alarms fill the cockpit.",
      "Stay alive until the flare passes.",
      OBJ_SURVIVE, 38, 8, 55, 190, 280, 960, MOD_SHARDS },
    { "MIRROR CARGO",
      "Every shot sees a second sky.",
      "Use the folded path to clear the hold.",
      OBJ_PUZZLE, 18, 0, 12, 190, 284, 1000, MOD_PZ_MIRROR },
    { "HEAT SPIRAL",
      "Flare plasma winds around the gate.",
      "No guns. Step through the rotating lane.",
      OBJ_PUZZLE, 0, 0, 50, 190, 288, 1050, MOD_PZ_SPIRAL },
    { "IRONFALL",
      "Molten iron pours across the route.",
      "Keep above it and keep firing.",
      OBJ_SURVIVE, 38, 8, 60, 190, 292, 1100, MOD_STORM },
    { "BOMB IN THE ASH",
      "One hot rock is a live charge.",
      "Defuse the field before the timer, never guess.",
      OBJ_PUZZLE, 12, 0, 120, 190, 296, 1200, MOD_PZ_BOMB },
    { "INFERNO",
      "Inferno burns across the stars.",
      "Jack will not turn back.",
      OBJ_BOSS, 0, 0, 0, 190, 300, 1500, MOD_NONE },
    { "VAULT APPROACH",
      "The Cold Vault has no front door.",
      "Jack makes one through the ice.",
      OBJ_CLEAR, 32, 8, 0, 190, 304, 1300, MOD_TRICKLE },
    { "GHOST STORAGE",
      "Vault signals blink out when approached.",
      "Shoot the fading mark before it moves.",
      OBJ_PUZZLE, 18, 0, 9, 190, 308, 1350, MOD_PZ_GHOST },
    { "SILENT GUARD",
      "Vault fighters cut their engines.",
      "Jack hunts by the cold signals.",
      OBJ_HUNT, 34, 7, 70, 190, 312, 1400, MOD_SNIPERS },
    { "HEAVY KEY",
      "The Cold Vault is full of false doors.",
      "Only the armoured giants unlock it.",
      OBJ_PUZZLE, 18, 0, 7, 190, 316, 1450, MOD_PZ_HEAVY },
    { "ICE DEPOT",
      "Stolen machines sleep under frost.",
      "Clear the depot before it wakes.",
      OBJ_TIMED, 25, 8, 125, 190, 320, 1500, MOD_BOULDERS },
    { "OLD RECORDS",
      "The records hide inside one vault rock.",
      "Unlock them from shallow to deep.",
      OBJ_PUZZLE, 20, 0, 10, 190, 324, 1550, MOD_PZ_LOCKSTEP },
    { "COLD CORE",
      "Armed hunters guard the frozen core.",
      "Jack tears through their patrol.",
      OBJ_HUNT, 34, 8, 75, 190, 328, 1600, MOD_SWARM },
    { "ZIGZAG VAULT",
      "The vault rains in alternating columns.",
      "No guns. Follow the open column.",
      OBJ_PUZZLE, 0, 0, 54, 190, 332, 1650, MOD_PZ_ZIGZAG },
    { "QUIET COLD",
      "The last lock kills every trigger.",
      "Pacifist flight: movement is the answer.",
      OBJ_PUZZLE, 0, 0, 58, 190, 336, 1700, MOD_PZ_PACIFIST },
    { "AEGIS",
      "Aegis seals the final vault.",
      "Jack refuses to stay buried.",
      OBJ_BOSS, 0, 0, 0, 190, 340, 1750, MOD_NONE },
    { "KINGDOM RIFT",
      "The Reality kingdom bends space.",
      "Jack holds the starter ship steady.",
      OBJ_TIMED, 38, 8, 130, 190, 344, 1800, MOD_SWIFT },
    { "BEACON RIFT",
      "Reality bends around a royal beacon.",
      "Keep the signal under Jack while you shoot.",
      OBJ_PUZZLE, 20, 0, 12, 190, 348, 1850, MOD_PZ_BEACON },
    { "MIRROR FIELD",
      "Every wasted bolt comes back doubled.",
      "Sixteen shots. No second chances.",
      OBJ_PUZZLE, 12, 0, 16, 190, 352, 1900, MOD_PZ_SALVO },
    { "SWEEPING STARS",
      "The throne road lights one row at a time.",
      "Sweep the marks from left to right.",
      OBJ_PUZZLE, 20, 0, 12, 190, 356, 1950, MOD_PZ_SWEEP },
    { "ROYAL GUARD",
      "The Cube Queen sends her best.",
      "The Chubbs call Jack onward.",
      OBJ_SURVIVE, 38, 8, 65, 190, 360, 2000, MOD_SHARDS },
    { "BROKEN SKY",
      "Reality static kills Jack's triggers.",
      "Weave the cracks until the sky heals.",
      OBJ_PUZZLE, 0, 0, 60, 190, 364, 2050, MOD_PZ_GAUNTLET },
    { "LAST ALLIES",
      "The Chubbs rally behind Jack.",
      "Clear their road to the throne.",
      OBJ_CLEAR, 22, 8, 0, 190, 368, 2100, MOD_BOULDERS },
    { "LAST ORDER",
      "The throne room writes one final sequence.",
      "Every mark must fall in order.",
      OBJ_PUZZLE, 22, 0, 12, 190, 372, 2150, MOD_PZ_ORDER },
    { "REALITY GATE",
      "The throne room waits beyond.",
      "Jack, settle the old score.",
      OBJ_SURVIVE, 38, 8, 75, 190, 376, 2200, MOD_STORM },
    { "THE CUBE QUEEN",
      "Jack. Your revenge ends here.",
      "Victory or oblivion.",
      OBJ_BOSS, 0, 0, 0, 190, 380, 2500, MOD_NONE },
};

/* Modifier labels for the level banner and the map card. Kept short so they
 * fit beside the objective on a 240 px line. */
static const char* const s_modifier_names[MOD_COUNT] = {
    "",                 /* MOD_NONE - nothing to announce */
    "BOULDER FIELD",
    "SHARD STORM",
    "HUNTER SWARM",
    "FAST SPACE",
    "ARMOURED ROCK",
    "SLOW DRIP",
    "CONSTANT STORM",
    "SHARPSHOOTERS",
    /* Puzzle variants (OBJ_PUZZLE). */
    "LIMITED AMMO",     /* MOD_PZ_SALVO */
    "SIGNAL HUNT",      /* MOD_PZ_SIGNAL */
    "GUNS OFFLINE",     /* MOD_PZ_GAUNTLET */
    "TARGET ORDER",
    "COLOR CODE",
    "RICOCHET RUN",
    "CLEAN COMBO",
    "BIG OR SMALL",
    "SAFE LANE",
    "ORBITAL LOCK",
    "CHAIN LINK",
    "FRAGILE CARGO",
    "MIRROR AIM",
    "TWIN LOCK",
    "DRONE CODE",
    "ANCHOR BREAK",
    "GHOST SIGNAL",
    "CLOCKWORK",
    "SIEVE",
    "BOMB DEFUSAL",
    "RING MAZE",
    "WALL WALK",
    "SCISSOR CROSS",
    "SPIRAL STEP",
    "ZIGZAG RAIN",
    "PACIFIST",
    "SWEEP CODE",
    "ARMOUR KEY",
    "LOCKSTEP",
    "BEACON RUN",
    "LAST SHOT"
};

const char* story_modifier_name(int mod) {
    if (mod <= MOD_NONE || mod >= MOD_COUNT) return "";
    return s_modifier_names[mod];
}

static const char* const s_sector_names[STORY_SECTOR_COUNT] = {
    "The Chubb system",
    "The Rust Yards",
    "The Ice Fields",
    "The Scrapline",
    "Ember Reach",
    "The Cold Vault",
    "The Reality kingdom"
};

static const char* const s_boss_names[STORY_SECTOR_COUNT] = {
    "Ironmaw",
    "Gemini",
    "Frostbite",
    "Juggernaut",
    "Inferno",
    "Aegis",
    "The Cube Queen"
};

/* Nobody's face is ever shown - the radio voices guide you,
 * and the bosses only ever get one line of text each. */
static const char* const s_boss_taunts[STORY_SECTOR_COUNT] = {
    "MY JAWS HAVE CRUSHED BETTER SHIPS.",
    "WE SEE YOU, JACK. BOTH OF US.",
    "YOUR ENGINES FREEZE WITH YOU.",
    "JACK WILL BE PRESSED INTO PLATE.",
    "YOUR LITTLE SHIP WILL BURN.",
    "THE SHIELD DOES NOT BREAK.",
    "JACK ARKEY. FOLD, OR BE FOLDED."
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

/* ── The opening speech ───────────────────────────────────────────────────
 * The original Jack RK / Chubb story, restored verbatim over 14 pages of
 * two lines each: line 0 types out in white, line 1 in blue.
 *
 * Two inline markers travel with the text and are stripped before drawing
 * (see story_intro_markup() and menu.c's typewriter):
 *
 *   *bold*   - drawn with a one-pixel shadow so the word reads heavier
 *   !faint!  - drawn in a dim grey, for the lines Jack barely says aloud
 *
 * The markers are never counted as characters by the typewriter, so a page
 * types at the same pace whether or not it is marked up. */
const char* const g_story_intro[STORY_INTRO_PAGES][2] = {
    { "Once upon a time,",
      "in another universe..." },
    { "Aliens invaded the planet",
      "of the Chubbs." },
    { "The Chubbs tried to fight back",
      "against the Reality King." },
    { "They threw everything they had",
      "at him." },
    { "But he was too strong.",
      "Way too strong." },
    { "A wise person once said:",
      "if you can't beat 'em, join 'em." },
    { "So the Chubbs did something weird:",
      "they befriended the Reality King." },
    { "They called him Jack RK.",
      "Jack Arkey." },
    { "He knew a lot about tech.",
      "He built a bunch on his last planet." },
    { "He could not let it go.",
      "Not this time." },
    { "He wanted *revenge*.",
      "He wanted it badly." },
    { "So he built a ship.",
      "one that can defeat the ones that invaded." },
    { "one that can defeat...",
      "!one that can kill!" },
    { "He set it ready for flight,",
      "and headed for the stars." }
};

/* Strip the *bold* / !faint! markers out of a story line.
 *
 * Writes the plain text into dst and, when spans is non-NULL, one style byte
 * per emitted character: STORY_MK_PLAIN, STORY_MK_BOLD or STORY_MK_FAINT.
 * Returns the number of plain characters written (always <= cap-1).  Callers
 * use this both to type the line out and to measure it, so the typewriter
 * never stalls on a marker the player cannot see. */
int story_intro_markup(const char* src, char* dst, u8* spans, int cap) {
    if (!dst || cap <= 0) return 0;
    int n = 0;
    u8 style = STORY_MK_PLAIN;
    for (const char* p = src; p && *p && n < cap - 1; p++) {
        if (*p == '*') {
            style = (style == STORY_MK_BOLD) ? STORY_MK_PLAIN : STORY_MK_BOLD;
            continue;
        }
        if (*p == '!') {
            style = (style == STORY_MK_FAINT) ? STORY_MK_PLAIN : STORY_MK_FAINT;
            continue;
        }
        dst[n] = *p;
        if (spans) spans[n] = style;
        n++;
    }
    dst[n] = '\0';
    return n;
}

/* Plain (marker-free) length of a story line. */
int story_intro_len(const char* src) {
    int n = 0;
    for (const char* p = src; p && *p; p++)
        if (*p != '*' && *p != '!') n++;
    return n;
}
