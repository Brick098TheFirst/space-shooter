#include "story.h"

/* ── The 80 levels ────────────────────────────────────────────────────────
 * Difficulty rises monotonically and every tenth level is a boss. Five nodes
 * in every sector are puzzles, so 40 of the 80 levels are puzzle levels - and
 * they run on 33 DIFFERENT rules, not a handful of rules wearing 33 names.
 *
 * The rule of the campaign: a rule is used once wherever possible and never
 * more than twice, and the "the radar lit a rock, go shoot it" family is
 * capped at two levels in the whole game (TARGET ORDER on L2, GHOST SIGNAL on
 * L52).  Everything else asks for something genuinely different - collecting,
 * gate flying, pushing, scanning, escorting, arithmetic, memory, stealth,
 * polarity, gravity, fuses, chain detonations or plain evasion - and fifteen
 * missions never let the player pull the trigger at all.
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
    { "SCRAP HARVEST",
      "Chubb fuel cells broke loose in the drift.",
      "No guns. Scoop fourteen of them.",
      OBJ_PUZZLE, 12, 0, 14, 108, 116, 80, MOD_PZ_COLLECT },
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
    { "RUNWAY LIGHTS",
      "The old landing gates still light up.",
      "No guns. Fly all twelve in blink order.",
      OBJ_PUZZLE, 0, 0, 12, 116, 132, 130, MOD_PZ_GATES },
    { "CORNER MAZE",
      "The route closes behind Jack.",
      "No guns. Learn the three safe figures.",
      OBJ_PUZZLE, 0, 0, 32, 118, 136, 160, MOD_PZ_GAUNTLET },
    { "ALIEN",
      "An alien blocks the way out.",
      "Shoot it down, Jack. Nothing clever.",
      OBJ_BOSS, 0, 0, 0, 120, 140, 500, MOD_NONE },
    { "YARD GATES",
      "The Rust Yards start with a wall.",
      "Scrap crews have sealed the route.",
      OBJ_CLEAR, 15, 3, 0, 122, 144, 140, MOD_TOUGH },
    { "PAINTED GATES",
      "The yard flashes three hull colours.",
      "Only the coded colour is safe to break.",
      OBJ_PUZZLE, 10, 0, 6, 124, 148, 155, MOD_PZ_COLOR },
    { "HUNTER NEST",
      "Alien hunters roost in the wrecks.",
      "Do not let them box Jack in.",
      OBJ_SURVIVE, 15, 8, 25, 126, 152, 165, MOD_SWARM },
    { "SLOW CONVOY",
      "A Chubb transport limps through the yard.",
      "Nothing gets past Jack to its hull.",
      OBJ_PUZZLE, 20, 0, 0, 128, 156, 180, MOD_PZ_ESCORT },
    { "WEIGH STATION",
      "The dock scale wants an exact load.",
      "Big five, medium three, chips one.",
      OBJ_PUZZLE, 14, 0, 18, 130, 160, 200, MOD_PZ_EXACT },
    { "SHIP GRAVEYARD",
      "Old Chubb ships rest in this yard.",
      "Clear the weapons from their graves.",
      OBJ_CLEAR, 22, 4, 0, 132, 164, 215, MOD_STORM },
    { "TUG OF WAR",
      "This pod has no engine of its own.",
      "No guns. Shove it into the lit dock.",
      OBJ_PUZZLE, 8, 0, 3, 134, 168, 230, MOD_PZ_HERD },
    { "DRONE PASSWORD",
      "The yard fighters carry a live code.",
      "Decode enough hulls before the signal shifts.",
      OBJ_PUZZLE, 0, 5, 10, 136, 172, 250, MOD_PZ_DRONECODE },
    { "DOUBLE SIGNAL",
      "Two command signals fill the yard.",
      "Jack follows them to their source.",
      OBJ_CLEAR, 26, 5, 0, 138, 176, 280, MOD_SWIFT },
    { "SPLINTER",
      "Splinter runs the Rust Yards.",
      "Break it and you get two problems.",
      OBJ_BOSS, 0, 0, 0, 140, 180, 750, MOD_NONE },
    { "FROZEN DRIFT",
      "The Ice Fields swallow all heat.",
      "Frost creeps across Jack's hull.",
      OBJ_CLEAR, 30, 5, 0, 142, 184, 300, MOD_SHARDS },
    { "TIDE LOCK",
      "The ice tide answers port, then starboard.",
      "Alternate sides or the lock resets.",
      OBJ_PUZZLE, 14, 0, 8, 144, 188, 320, MOD_PZ_ALTERNATE },
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
    { "COLD PROBE",
      "A survey probe runs from anything warm.",
      "No guns. Hold the scan on it twelve seconds.",
      OBJ_PUZZLE, 6, 0, 12, 152, 204, 400, MOD_PZ_SCAN },
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
    { "COLDSNAP",
      "Coldsnap waits under the ice.",
      "Keep moving or it takes your engines.",
      OBJ_BOSS, 0, 0, 0, 160, 220, 1000, MOD_NONE },
    { "SCRAPLINE GATE",
      "The Scrapline never stops moving.",
      "Jack enters between two haulers.",
      OBJ_BIGGAME, 32, 3, 5, 162, 224, 500, MOD_TOUGH },
    { "MAGNET COMBO",
      "Charged scrap pulls every loose shot.",
      "Build a clean chain of ten breaks.",
      OBJ_PUZZLE, 16, 0, 10, 164, 228, 530, MOD_PZ_COMBO },
    { "CHAIN BLAST",
      "Scrapline charges cook off together.",
      "Six shots. Let the blast do the rest.",
      OBJ_PUZZLE, 15, 0, 6, 166, 232, 560, MOD_PZ_BLAST },
    { "SIEVE THE SCRAP",
      "The useful signal is smaller than dust.",
      "Break only the tiny pieces.",
      OBJ_PUZZLE, 16, 0, 8, 168, 236, 590, MOD_PZ_SIEVE },
    { "BROKEN LINK",
      "Chubb cargo is chained in transit.",
      "Break the chain and free the ships.",
      OBJ_SURVIVE, 36, 8, 48, 170, 240, 620, MOD_SWARM },
    { "POLARITY LOCK",
      "The rail spits red fire and blue fire.",
      "No guns. Wear the colour that is hitting you.",
      OBJ_PUZZLE, 0, 0, 40, 172, 244, 650, MOD_PZ_POLARITY },
    { "SUPPLY LINE",
      "Parts flow toward the kingdom.",
      "Jack cuts off the supply.",
      OBJ_HUNT, 32, 8, 52, 174, 248, 680, MOD_SWIFT },
    { "BOUNCE VECTOR",
      "The hot lane folds its side walls.",
      "Use the returning shot path.",
      OBJ_PUZZLE, 14, 0, 10, 176, 252, 710, MOD_PZ_RICOCHET },
    { "TITAN TRACKS",
      "Huge tracks scar the Scrapline.",
      "Follow them to the machine.",
      OBJ_BIGGAME, 38, 4, 6, 178, 256, 750, MOD_STORM },
    { "SLEDGE",
      "Sledge fills the whole lane.",
      "Strip the plates before the hull feels a thing.",
      OBJ_BOSS, 0, 0, 0, 180, 260, 1250, MOD_NONE },
    { "EMBER BORDER",
      "Ember Reach glows red ahead.",
      "Jack opens the cooling vents.",
      OBJ_CLEAR, 38, 8, 0, 182, 264, 800, MOD_SWIFT },
    { "FRAGILE HAUL",
      "Chubb cargo is stacked behind the rocks.",
      "Nothing may reach the hold below.",
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
    { "BURNING FUSES",
      "Every rock out here is lit and counting.",
      "Break each one before its own fuse ends.",
      OBJ_PUZZLE, 14, 0, 0, 190, 296, 1200, MOD_PZ_FUSE },
    { "WILDFIRE",
      "Wildfire burns across the stars.",
      "Hit it when it vents, not when it glows.",
      OBJ_BOSS, 0, 0, 0, 190, 300, 1500, MOD_NONE },
    { "VAULT APPROACH",
      "The Cold Vault has no front door.",
      "Jack makes one through the ice.",
      OBJ_CLEAR, 32, 8, 0, 190, 304, 1300, MOD_TRICKLE },
    { "GHOST STORAGE",
      "Vault signals blink out when approached.",
      "Shoot the fading mark before it moves.",
      OBJ_PUZZLE, 16, 0, 7, 190, 308, 1350, MOD_PZ_GHOST },
    { "SILENT GUARD",
      "Vault fighters cut their engines.",
      "Jack hunts by the cold signals.",
      OBJ_HUNT, 34, 7, 70, 190, 312, 1400, MOD_SNIPERS },
    { "OPEN SIDE",
      "Vault rock wears a spinning armour plate.",
      "Hit the half the plate has left open.",
      OBJ_PUZZLE, 12, 0, 8, 190, 316, 1450, MOD_PZ_SHIELDARC },
    { "ICE DEPOT",
      "Stolen machines sleep under frost.",
      "Clear the depot before it wakes.",
      OBJ_TIMED, 25, 8, 125, 190, 320, 1500, MOD_BOULDERS },
    { "LIGHTS OUT",
      "The vault kills every lamp and sensor.",
      "Remember where they were, then shoot.",
      OBJ_PUZZLE, 12, 0, 0, 190, 324, 1550, MOD_PZ_BLACKOUT },
    { "COLD CORE",
      "Armed hunters guard the frozen core.",
      "Jack tears through their patrol.",
      OBJ_HUNT, 34, 8, 75, 190, 328, 1600, MOD_SWARM },
    { "ZIGZAG VAULT",
      "The vault rains in alternating columns.",
      "No guns. Follow the open column.",
      OBJ_PUZZLE, 0, 0, 54, 190, 332, 1650, MOD_PZ_ZIGZAG },
    { "SILENT RUN",
      "A scanner beam sweeps the last corridor.",
      "No guns. Never be caught in the light.",
      OBJ_PUZZLE, 0, 0, 45, 190, 336, 1700, MOD_PZ_STEALTH },
    { "BULWARK",
      "Bulwark seals the final vault.",
      "Shoot the nodes off. The hull is sealed.",
      OBJ_BOSS, 0, 0, 0, 190, 340, 1750, MOD_NONE },
    { "KINGDOM RIFT",
      "The Reality kingdom bends space.",
      "Jack holds the starter ship steady.",
      OBJ_TIMED, 38, 8, 130, 190, 344, 1800, MOD_SWIFT },
    { "GRAVITY WELL",
      "Reality sags into a hole out here.",
      "No guns. Fly a sky that pulls back.",
      OBJ_PUZZLE, 0, 0, 45, 190, 348, 1850, MOD_PZ_GRAVITY },
    { "CROSSED WIRES",
      "Reality static rewires Jack's steering.",
      "Clear it with the controls fighting you.",
      OBJ_PUZZLE, 16, 0, 0, 190, 352, 1900, MOD_PZ_REVERSE },
    { "FLAWLESS",
      "The throne road forgives nothing.",
      "Clear the field without taking a hit.",
      OBJ_PUZZLE, 8, 0, 0, 190, 356, 1950, MOD_PZ_PERFECT },
    { "ROYAL GUARD",
      "The Reality Queen sends her best.",
      "The Chubbs call Jack onward.",
      OBJ_SURVIVE, 38, 8, 65, 190, 360, 2000, MOD_SHARDS },
    { "THRONE APPROACH",
      "The last gates open once, in order.",
      "No guns. Sixteen of them, and they drift.",
      OBJ_PUZZLE, 0, 0, 16, 190, 364, 2050, MOD_PZ_GATES },
    { "LAST ALLIES",
      "The Chubbs rally behind Jack.",
      "Clear their road to the throne.",
      OBJ_CLEAR, 22, 8, 0, 190, 368, 2100, MOD_BOULDERS },
    { "LAST CHARGE",
      "The queen's mines are stacked to blow.",
      "Eight shots. Aim where it will spread.",
      OBJ_PUZZLE, 22, 0, 8, 190, 372, 2150, MOD_PZ_BLAST },
    { "REALITY GATE",
      "The throne room waits beyond.",
      "Jack, settle the old score.",
      OBJ_SURVIVE, 38, 8, 75, 190, 376, 2200, MOD_STORM },
    { "REALITY QUEEN",
      "The Reality Queen waits beyond the fold.",
      "Jack. End the old invasion.",
      OBJ_BOSS, 0, 0, 0, 190, 380, 2500, MOD_NONE },

    /* ── NULL HORIZON: Director's Cut epilogue sector ──────────────────
     * Destroying the Queen did not close the fold. It exposed the machine
     * that had been rewriting her kingdom from outside ordinary time. Five
     * puzzle nodes return familiar rules in harder combinations; each rule
     * still appears no more than twice across the complete campaign. */
    { "AFTER THE CROWN",
      "The Queen falls. The stars do not return.",
      "Something behind reality is still running.",
      OBJ_CLEAR, 40, 8, 0, 190, 388, 2350, MOD_STORM },
    { "DEAD AIR",
      "A black scanner erases whatever it sees.",
      "No guns. Stay between its silent sweeps.",
      OBJ_PUZZLE, 0, 0, 55, 190, 396, 2400, MOD_PZ_STEALTH },
    { "ECHO WRECKAGE",
      "Wrecks arrive before their impact is heard.",
      "Clear the doubled field before it repeats.",
      OBJ_TIMED, 44, 8, 110, 190, 404, 2450, MOD_TOUGH },
    { "HORIZON PACK",
      "Hunters with no shadows pour from the seam.",
      "Break their formation before it doubles.",
      OBJ_HUNT, 34, 8, 82, 190, 412, 2500, MOD_SNIPERS },
    { "BORROWED COLOUR",
      "The horizon fires yesterday's red and blue.",
      "No guns. Match the bolt that has not fired yet.",
      OBJ_PUZZLE, 0, 0, 55, 190, 420, 2550, MOD_PZ_POLARITY },
    { "TIME DEBT",
      "Every second Jack stole is coming due.",
      "Outfly the longest storm in the campaign.",
      OBJ_SURVIVE, 40, 8, 85, 190, 428, 2600, MOD_SWIFT },
    { "THE LAST SIGNAL",
      "One Chubb probe crossed the end of time.",
      "No guns. Hold its echo in the lens.",
      OBJ_PUZZLE, 8, 0, 15, 190, 436, 2700, MOD_PZ_SCAN },
    { "RETURN VECTOR",
      "The seam sends every laser home backwards.",
      "Clear the field through the mirrored path.",
      OBJ_PUZZLE, 20, 0, 14, 190, 444, 2800, MOD_PZ_MIRROR },
    { "ZERO HOUR",
      "The engine notices Jack at last.",
      "One hit resets the final approach.",
      OBJ_PUZZLE, 10, 0, 0, 190, 452, 3000, MOD_PZ_PERFECT },
    { "PARADOX ENGINE",
      "The machine behind the invasion wakes.",
      "Keep moving. It remembers where you were.",
      OBJ_BOSS, 0, 0, 0, 190, 460, 4000, MOD_NONE },
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
    /* Puzzle rules (OBJ_PUZZLE), in StoryModifier order. */
    "LIMITED AMMO",     /* MOD_PZ_SALVO */
    "LAST SHOT",        /* MOD_PZ_LASTSHOT */
    "TARGET ORDER",     /* MOD_PZ_ORDER */
    "GHOST SIGNAL",     /* MOD_PZ_GHOST */
    "COLOR CODE",       /* MOD_PZ_COLOR */
    "SIEVE",            /* MOD_PZ_SIEVE */
    "RICOCHET RUN",     /* MOD_PZ_RICOCHET */
    "MIRROR AIM",       /* MOD_PZ_MIRROR */
    "CLEAN COMBO",      /* MOD_PZ_COMBO */
    "CLOCKWORK",        /* MOD_PZ_CLOCK */
    "FRAGILE CARGO",    /* MOD_PZ_FRAGILE */
    "DRONE CODE",       /* MOD_PZ_DRONECODE */
    "SAFE LANE",        /* MOD_PZ_LANE */
    "ESCORT",           /* MOD_PZ_ESCORT */
    "EXACT LOAD",       /* MOD_PZ_EXACT */
    "TIDE LOCK",        /* MOD_PZ_ALTERNATE */
    "CHAIN BLAST",      /* MOD_PZ_BLAST */
    "MEMORY RUN",       /* MOD_PZ_BLACKOUT */
    "FUSE RUN",         /* MOD_PZ_FUSE */
    "OPEN SIDE",        /* MOD_PZ_SHIELDARC */
    "FLAWLESS",         /* MOD_PZ_PERFECT */
    "CROSSED WIRES",    /* MOD_PZ_REVERSE */
    "GUNS OFFLINE",     /* MOD_PZ_GAUNTLET */
    "RING MAZE",        /* MOD_PZ_RINGS */
    "SPIRAL STEP",      /* MOD_PZ_SPIRAL */
    "ZIGZAG RAIN",      /* MOD_PZ_ZIGZAG */
    "SALVAGE RUN",      /* MOD_PZ_COLLECT */
    "GATE RUN",         /* MOD_PZ_GATES */
    "TUG OF WAR",       /* MOD_PZ_HERD */
    "SCAN LOCK",        /* MOD_PZ_SCAN */
    "GRAVITY WELL",     /* MOD_PZ_GRAVITY */
    "POLARITY",         /* MOD_PZ_POLARITY */
    "SILENT RUN"        /* MOD_PZ_STEALTH */
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
    "The Reality kingdom",
    "Null Horizon"
};

static const char* const s_boss_names[STORY_SECTOR_COUNT] = {
    "Alien",
    "Splinter",
    "Coldsnap",
    "Sledge",
    "Wildfire",
    "Bulwark",
    "Reality Queen",
    "Paradox Engine"
};

/* Nobody's face is ever shown - the radio voices guide you,
 * and the bosses only ever get one line of text each. */
static const char* const s_boss_taunts[STORY_SECTOR_COUNT] = {
    "AN ALIEN. NOTHING MORE. SHOOT IT.",
    "ONE OF US IS ALWAYS BEHIND YOU.",
    "STOP MOVING AND YOU BELONG TO ME.",
    "PLATE BY PLATE, JACK. GOOD LUCK.",
    "I BURN. THEN I VENT. TRY TO COUNT.",
    "THE SEAL DOES NOT BREAK. THE NODES MIGHT.",
    "JACK ARKEY. THIS IS THE REALITY QUEEN.",
    "I REMEMBER EVERYWHERE YOU HAVE EVER BEEN."
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
