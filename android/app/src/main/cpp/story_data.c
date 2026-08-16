#include "story.h"

/* ── The 70 levels ────────────────────────────────────────────────────────
 * Difficulty rises monotonically and every tenth level is a boss, but the
 * shape of a level is deliberately never twice the same.  Two axes do the
 * work:
 *
 *   OBJECTIVE  clear / hunt / survive / big-game / timed - five ways to end
 *              a level, so "kill everything" is only one level in four.
 *   MODIFIER   a twist on the field itself (boulders, shards, swarm, swift,
 *              tough, trickle, storm, snipers) that changes what you are
 *              actually flying through rather than just how much of it.
 *
 * Each sector runs its own hand-picked sequence of the two (see PLANS in the
 * generator note below), chosen so no objective+modifier pair repeats back
 * to back and no sector plays like the one before it.  Field sizes stay
 * inside the engine's MAX_ASTEROIDS / MAX_DRONES budgets so every level can
 * actually spawn, and tools/story_sim proves every one still terminates. */
const StoryLevel g_story_levels[STORY_LEVEL_COUNT] = {
    { "HOME ORBIT",
      "Jack checks the starter ship.",
      "The Chubbs watch him launch.",
      OBJ_CLEAR, 6, 1, 0, 102, 104, 52, MOD_NONE },
    { "IRON DRIFT",
      "Old Chubb satellites drift here.",
      "Clear Jack a path through the scrap.",
      OBJ_CLEAR, 6, 1, 0, 104, 108, 65, MOD_TRICKLE },
    { "ALIEN SCOUTS",
      "Alien scouts circle the system.",
      "Keep them away from the Chubbs.",
      OBJ_HUNT, 5, 3, 12, 106, 112, 72, MOD_NONE },
    { "DENSE BELT",
      "Dense rocks block Jack's route.",
      "Break the largest ones first.",
      OBJ_BIGGAME, 6, 1, 4, 108, 116, 80, MOD_BOULDERS },
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
    { "LAUNCH WINDOW",
      "The safe route is closing fast.",
      "Get Jack through before it shuts.",
      OBJ_TIMED, 15, 2, 85, 116, 132, 130, MOD_NONE },
    { "SYSTEM EDGE",
      "The Chubb System ends ahead.",
      "One last patrol blocks the way.",
      OBJ_CLEAR, 20, 4, 0, 118, 136, 160, MOD_STORM },
    { "IRONMAW",
      "Ironmaw guards the way out.",
      "Jack, break those iron jaws.",
      OBJ_BOSS, 0, 0, 0, 120, 140, 500, MOD_NONE },
    { "YARD GATES",
      "The Rust Yards start with a wall.",
      "Scrap crews have sealed the route.",
      OBJ_CLEAR, 15, 3, 0, 122, 144, 140, MOD_TOUGH },
    { "FALLING SCRAP",
      "Broken hull plates rain from above.",
      "Break the largest plates apart.",
      OBJ_BIGGAME, 10, 2, 4, 124, 148, 155, MOD_BOULDERS },
    { "HUNTER NEST",
      "Alien hunters roost in the wrecks.",
      "Do not let them box Jack in.",
      OBJ_SURVIVE, 15, 8, 25, 126, 152, 165, MOD_SWARM },
    { "RUST SHARDS",
      "Sharp metal spins between the hulks.",
      "Thread the gap before it closes.",
      OBJ_TIMED, 22, 4, 88, 128, 156, 180, MOD_SHARDS },
    { "DEAD ENGINES",
      "One engine signal hides in the scrap.",
      "Shoot only what the scanner marks.",
      OBJ_PUZZLE, 14, 0, 6, 130, 160, 200, MOD_PZ_SIGNAL },
    { "SHIP GRAVEYARD",
      "Old Chubb ships rest in this yard.",
      "Clear the weapons from their graves.",
      OBJ_CLEAR, 22, 4, 0, 132, 164, 215, MOD_STORM },
    { "CUTTING CREW",
      "Scrap cutters strip every hull.",
      "Jack is next on their list.",
      OBJ_BIGGAME, 16, 3, 4, 134, 168, 230, MOD_TOUGH },
    { "MAGNET WELL",
      "Yard magnets short Jack's guns out.",
      "Fly the well. Dodge everything.",
      OBJ_PUZZLE, 0, 0, 35, 136, 172, 250, MOD_PZ_GAUNTLET },
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
    { "ICE SHARDS",
      "Frozen shards race across the field.",
      "One bad turn will split the ship.",
      OBJ_TIMED, 25, 5, 92, 144, 188, 320, MOD_SWIFT },
    { "WHITEOUT WING",
      "Hunter trails vanish in the snow.",
      "Jack tracks them by engine noise.",
      OBJ_HUNT, 22, 8, 36, 146, 192, 340, MOD_SWARM },
    { "DEEP FREEZE",
      "The starter ship is losing heat.",
      "Hold out until the systems recover.",
      OBJ_SURVIVE, 32, 5, 48, 148, 196, 360, MOD_SHARDS },
    { "GLACIER RUN",
      "Great slabs of ice block the route.",
      "Break the thickest ones apart.",
      OBJ_BIGGAME, 30, 3, 4, 150, 200, 380, MOD_NONE },
    { "SNOWBLIND",
      "Ice dust blinds everything but radar.",
      "Trust the mark. Ignore the rest.",
      OBJ_PUZZLE, 16, 0, 7, 152, 204, 400, MOD_PZ_SIGNAL },
    { "FROST HUNTERS",
      "Cold-wing fighters hunt in packs.",
      "Thin their numbers before they close.",
      OBJ_HUNT, 25, 8, 40, 154, 208, 420, MOD_SWIFT },
    { "COLD CHASE",
      "The ice storm is gaining on Jack.",
      "Do not stop for anything.",
      OBJ_SURVIVE, 36, 6, 45, 156, 212, 450, MOD_STORM },
    { "WIDOW'S WEB",
      "Frozen rocks form a tightening web.",
      "Cut a route to Frostbite.",
      OBJ_TIMED, 22, 8, 105, 158, 216, 480, MOD_BOULDERS },
    { "FROSTBITE",
      "Frostbite waits under the ice.",
      "Jack brings the heat.",
      OBJ_BOSS, 0, 0, 0, 160, 220, 1000, MOD_NONE },
    { "SCRAPLINE GATE",
      "The Scrapline never stops moving.",
      "Jack enters between two haulers.",
      OBJ_BIGGAME, 32, 3, 5, 162, 224, 500, MOD_TOUGH },
    { "MAGNET STORM",
      "Charged scrap jumps across the lane.",
      "Hold the ship on course.",
      OBJ_CLEAR, 36, 6, 0, 164, 228, 530, MOD_STORM },
    { "BOLT RUN",
      "Bolt storms jam every weapon rail.",
      "Nothing to shoot. Everything to dodge.",
      OBJ_PUZZLE, 0, 0, 40, 166, 232, 560, MOD_PZ_GAUNTLET },
    { "CRUSHER PASS",
      "Old crushers still pound the line.",
      "Learn the rhythm and push through.",
      OBJ_TIMED, 35, 6, 108, 168, 236, 590, MOD_TOUGH },
    { "BROKEN LINK",
      "Chubb cargo is chained in transit.",
      "Break the chain and free the ships.",
      OBJ_SURVIVE, 36, 8, 48, 170, 240, 620, MOD_SWARM },
    { "PATCHWORK WING",
      "Fighters rebuilt from Chubb scrap.",
      "Take back what they stole.",
      OBJ_CLEAR, 22, 8, 0, 172, 244, 650, MOD_BOULDERS },
    { "SUPPLY LINE",
      "Parts flow toward the kingdom.",
      "Jack cuts off the supply.",
      OBJ_HUNT, 32, 8, 52, 174, 248, 680, MOD_SWIFT },
    { "IRON LOAD",
      "Iron rocks ride the cargo stream.",
      "Survive until the load passes.",
      OBJ_SURVIVE, 38, 8, 50, 176, 252, 710, MOD_TOUGH },
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
    { "SOLAR WIND",
      "Hot winds throw sparks at the ship.",
      "Clear the field before shields fail.",
      OBJ_TIMED, 38, 8, 110, 184, 268, 840, MOD_STORM },
    { "ASH HUNTERS",
      "Fire-dark fighters cross the glow.",
      "Jack hunts them through the ash.",
      OBJ_HUNT, 34, 8, 58, 186, 272, 880, MOD_SWARM },
    { "MOLTEN DRIFT",
      "The heat cooks Jack's capacitors.",
      "Fifteen shots left. Choose them well.",
      OBJ_PUZZLE, 10, 0, 15, 188, 276, 920, MOD_PZ_SALVO },
    { "HOT SEAT",
      "Heat alarms fill the cockpit.",
      "Stay alive until the flare passes.",
      OBJ_SURVIVE, 38, 8, 55, 190, 280, 960, MOD_SHARDS },
    { "CINDER CREW",
      "Veteran pilots hide in the embers.",
      "Clear them out for the Chubbs.",
      OBJ_CLEAR, 38, 8, 0, 190, 284, 1000, MOD_TOUGH },
    { "HEAT TRAP",
      "Flare plasma floods the gun rails.",
      "Ride the firestorm out, hands off.",
      OBJ_PUZZLE, 0, 0, 45, 190, 288, 1050, MOD_PZ_GAUNTLET },
    { "IRONFALL",
      "Molten iron pours across the route.",
      "Keep above it and keep firing.",
      OBJ_SURVIVE, 38, 8, 60, 190, 292, 1100, MOD_STORM },
    { "LASH GUARD",
      "Inferno's guard holds the gate.",
      "Break their formation.",
      OBJ_HUNT, 38, 7, 65, 190, 296, 1200, MOD_SNIPERS },
    { "INFERNO",
      "Inferno burns across the stars.",
      "Jack will not turn back.",
      OBJ_BOSS, 0, 0, 0, 190, 300, 1500, MOD_NONE },
    { "VAULT APPROACH",
      "The Cold Vault has no front door.",
      "Jack makes one through the ice.",
      OBJ_CLEAR, 32, 8, 0, 190, 304, 1300, MOD_TRICKLE },
    { "DARK STORAGE",
      "Frozen cargo drifts without lights.",
      "Search the crates for Chubb tech.",
      OBJ_BIGGAME, 38, 4, 6, 190, 308, 1350, MOD_TOUGH },
    { "SILENT GUARD",
      "Vault fighters cut their engines.",
      "Jack hunts by the cold signals.",
      OBJ_HUNT, 34, 7, 70, 190, 312, 1400, MOD_SNIPERS },
    { "LOCKED IN",
      "Steel doors close behind the ship.",
      "Hold out while Jack cracks the lock.",
      OBJ_SURVIVE, 38, 8, 65, 190, 316, 1450, MOD_SWARM },
    { "ICE DEPOT",
      "Stolen machines sleep under frost.",
      "Clear the depot before it wakes.",
      OBJ_TIMED, 25, 8, 125, 190, 320, 1500, MOD_BOULDERS },
    { "OLD RECORDS",
      "The records hide inside one vault rock.",
      "Break only what the scanner marks.",
      OBJ_PUZZLE, 18, 0, 8, 190, 324, 1550, MOD_PZ_SIGNAL },
    { "COLD CORE",
      "Armed hunters guard the frozen core.",
      "Jack tears through their patrol.",
      OBJ_HUNT, 34, 8, 75, 190, 328, 1600, MOD_SWARM },
    { "BLACK ICE",
      "Dark ice hides the deepest lock.",
      "Jack lights a way through.",
      OBJ_SURVIVE, 38, 8, 62, 190, 332, 1650, MOD_TOUGH },
    { "WARDEN'S WATCH",
      "Aegis sees every move you make.",
      "Break the eyes watching Jack.",
      OBJ_BIGGAME, 25, 4, 8, 190, 336, 1700, MOD_BOULDERS },
    { "AEGIS",
      "Aegis seals the final vault.",
      "Jack refuses to stay buried.",
      OBJ_BOSS, 0, 0, 0, 190, 340, 1750, MOD_NONE },
    { "KINGDOM RIFT",
      "The Reality kingdom bends space.",
      "Jack holds the starter ship steady.",
      OBJ_TIMED, 38, 8, 130, 190, 344, 1800, MOD_SWIFT },
    { "FOLDED STARS",
      "Stars repeat along the royal road.",
      "Break the loop and fly on.",
      OBJ_CLEAR, 38, 8, 0, 190, 348, 1850, MOD_STORM },
    { "MIRROR FIELD",
      "Every wasted bolt comes back doubled.",
      "Eighteen shots. No second chances.",
      OBJ_PUZZLE, 12, 0, 18, 190, 352, 1900, MOD_PZ_SALVO },
    { "UNRAVELING",
      "The kingdom pulls space apart.",
      "Hunt the anchors holding it open.",
      OBJ_HUNT, 38, 8, 75, 190, 356, 1950, MOD_SWARM },
    { "ROYAL GUARD",
      "The Void Empress sends her best.",
      "The Chubbs call Jack onward.",
      OBJ_SURVIVE, 38, 8, 65, 190, 360, 2000, MOD_SHARDS },
    { "BROKEN SKY",
      "Reality static kills Jack's triggers.",
      "Weave the cracks until the sky heals.",
      OBJ_PUZZLE, 0, 0, 50, 190, 364, 2050, MOD_PZ_GAUNTLET },
    { "LAST ALLIES",
      "The Chubbs rally behind Jack.",
      "Clear their road to the throne.",
      OBJ_CLEAR, 22, 8, 0, 190, 368, 2100, MOD_BOULDERS },
    { "THRONE ROAD",
      "The Empress' last defense arrives.",
      "Jack punches through the line.",
      OBJ_HUNT, 38, 7, 70, 190, 372, 2150, MOD_SNIPERS },
    { "REALITY GATE",
      "The throne room waits beyond.",
      "Jack, settle the old score.",
      OBJ_SURVIVE, 38, 8, 75, 190, 376, 2200, MOD_STORM },
    { "THE VOID EMPRESS",
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
    /* Puzzle variants (OBJ_PUZZLE) */
    "LIMITED AMMO",
    "SIGNAL HUNT",
    "GUNS OFFLINE"
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
    "The Void Empress"
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
    "JACK ARKEY. COME BE UNMADE."
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
      "The of the Chubbs." },
    { "The Chubbs tried to fight back.",
      "back against the Reality King." },
    { "They threw everything they had at them",
      "everthing." },
    { "But he was too strong.",
      "Way too strong." },
    { "A wise person once said:",
      "if you can't beat em, join em." },
    { "So the Chubbs decided to do something wierd",
      "they befriended the Reality King." },
    { "They called him Jack RK.",
      "Jack Arkey," },
    { "He knew a lot about tech",
      "He built a bunch on his last planet." },
    { ".",
      "." },
    { "He wanted *revenge*.",
      "He wanted it badly." },
    { "So he built a ship.",
      "one that can defeat the ones that invaded." },
    { "one that can defeat",
      "!one that can kill!" },
    { "he set aflight,",
      "..." }
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
