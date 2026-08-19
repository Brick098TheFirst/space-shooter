#include <stdio.h>
#include <string.h>
#include "save.h"
#include "story.h"
#include "game.h"
extern u32 g_fake_epoch;   /* the harness' hand-driven wall clock */
static int fails=0;
#define CHK(c,msg) do{ if(!(c)){ printf("FAIL: %s\n", msg); fails++; } }while(0)
int main(void){
    printf("StorySave=%zu bytes\n", sizeof(StorySave));
    save_init_defaults();
    story_init();
    CHK(story_current_level()==1,"start level 1");
    CHK(story_lives()==3,"start 3 lives");
    CHK(!story_content_unlocked(),"content locked at start");

    /* every level must be flyable: sane fields, non-zero reward. The
     * campaign variety contract is checked here too, so a future table edit
     * cannot quietly drift back to ten puzzles or repeat one rule endlessly. */
    int puzzle_count = 0;
    int puzzle_variants[MOD_COUNT] = {0};
    for(int i=0;i<STORY_LEVEL_COUNT;i++){
        const StoryLevel* L=&g_story_levels[i];
        int lv=i+1;
        CHK(L->name && L->name[0], "level has a name");
        CHK(L->reward>0, "level pays out");
        if(L->objective==OBJ_BOSS){
            CHK(lv%10==0, "boss only on 10s");
            CHK(story_boss_for_level(lv)>=0, "boss id resolves");
        } else {
            /* Kingdom 8's finale (level 80) is a drone attack on a 10-level,
             * not a boss, so the off-the-10s rule only binds the classic
             * kingdoms. */
            if(lv <= STORY_CLASSIC_LEVELS) CHK(lv%10!=0, "non-boss off the 10s");
            CHK(L->rocks<=MAX_ASTEROIDS && L->drones<=MAX_DRONES,
                "field within engine budget");
            if(L->objective==OBJ_HUNT) CHK(L->quota>0 && L->quota<=90,"hunt quota sane");
            if(L->objective==OBJ_SURVIVE) CHK(L->quota>=15 && L->quota<=90,"survive secs sane");
            if(L->objective==OBJ_DRONES){
                CHK(L->rocks==0,"drone skies have no rocks");
                CHK(L->quota>=10 && L->quota<=100,"drone quota 10..100");
            }
        }
        if(L->objective==OBJ_PUZZLE){
            puzzle_count++;
            CHK(lv <= STORY_CLASSIC_LEVELS, "puzzles stay in the classic kingdoms");
            CHK(L->modifier >= MOD_PZ_FIRST && L->modifier <= MOD_PZ_LAST,
                "puzzle has a named variant");
            puzzle_variants[L->modifier]++;
            CHK(story_modifier_name(L->modifier)[0], "puzzle variant has a label");
        }
    }
    /* The puzzle half of the campaign lives in the seven classic kingdoms;
     * kingdom 8 is a straight drone attack and adds no puzzles. */
    CHK(puzzle_count == STORY_PUZZLE_COUNT, "the campaign keeps its 35 puzzles");
    CHK(puzzle_count >= (STORY_CLASSIC_LEVELS + 1) / 2, "half the classic campaign is puzzles");
    /* Kingdom 8: ten drone attacks, quota climbing by ten to one hundred,
     * and no boss anywhere in the last kingdom. */
    for(int k=0;k<10;k++){
        int lv = STORY_CLASSIC_LEVELS + k + 1;
        const StoryLevel* D=&g_story_levels[lv-1];
        CHK(D->objective==OBJ_DRONES,"kingdom 8 level is a drone attack");
        CHK(D->quota==(k+1)*10,"drone quota climbs 10,20,..,100");
        CHK(story_boss_for_level(lv)<0,"no boss in kingdom 8");
    }
    CHK(!strcmp(g_story_levels[STORY_LEVEL_COUNT-1].name,"ONE HUNDRED"),
        "the last sky is ONE HUNDRED");
    /* The variety contract.  These are the numbers the campaign promises the
     * player: lots of rules, none of them worn out, and the "radar lit a
     * rock" family kept to a strict minimum. */
    int distinct = 0, radar_levels = 0, no_gun_rules = 0;
    for(int i=MOD_PZ_FIRST;i<=MOD_PZ_LAST;i++){
        CHK(puzzle_variants[i] <= 2, "no puzzle rule repeats more than twice");
        if(puzzle_variants[i]) distinct++;
        if(story_mod_is_no_guns(i) && puzzle_variants[i]) no_gun_rules++;
    }
    radar_levels = puzzle_variants[MOD_PZ_ORDER] + puzzle_variants[MOD_PZ_GHOST];
    printf("puzzles=%d distinct rules=%d no-gun rules=%d radar levels=%d\n",
           puzzle_count, distinct, no_gun_rules, radar_levels);
    CHK(distinct >= 30, "the campaign runs at least 30 different puzzle rules");
    CHK(radar_levels <= 2, "at most two scanner-target levels in the campaign");
    CHK(no_gun_rules >= 8, "plenty of puzzles never ask for the trigger");
    for(int i=MOD_PZ_FIRST;i<=MOD_PZ_LAST;i++)
        CHK(story_modifier_name(i)[0], "every puzzle rule has a label");
    CHK(!strcmp(story_boss_name(0), "Alien"), "first boss is named Alien");
    /* The Reality Queen is the FINAL boss: she falls in kingdom 7 (level 70)
     * and kingdom 8 has no boss at all, only the mid-level Big Drone. */
    CHK(!strcmp(story_boss_name(SBOSS_REALITY_QUEEN), "Reality Queen"),
        "the Reality Queen is still named");
    CHK(!strcmp(story_boss_name(SBOSS_DRONE_OVERLORD), "Big Drone"),
        "the kingdom 8 big drone is named");
    /* Every boss name must be different - no reskins. */
    for(int i=0;i<STORY_SECTOR_COUNT;i++)
      for(int j=i+1;j<STORY_SECTOR_COUNT;j++)
        CHK(strcmp(story_boss_name(i), story_boss_name(j)), "boss names are unique");
    CHK(!strcmp(g_story_levels[9].name, "ALIEN"), "level 10 uses the Alien name");
    CHK(!strcmp(g_story_levels[69].name, "REALITY QUEEN"),
        "level 70 uses the Reality Queen name");
    /* monotonic difficulty */
    for(int i=1;i<STORY_LEVEL_COUNT;i++){
        CHK(g_story_levels[i].speed_pct>=g_story_levels[i-1].speed_pct,"speed never drops");
        CHK(g_story_levels[i].hp_pct>=g_story_levels[i-1].hp_pct,"hp never drops");
    }
    /* unique names */
    for(int i=0;i<STORY_LEVEL_COUNT;i++)
      for(int j=i+1;j<STORY_LEVEL_COUNT;j++)
        if(!strcmp(g_story_levels[i].name,g_story_levels[j].name)){printf("FAIL: dup name %s\n",g_story_levels[i].name);fails++;}

    /* progression + replay half pay (no perf data = the floor only) */
    int paid = story_complete_level(1, NULL);
    CHK(paid==g_story_levels[0].reward,"first clear full reward");
    CHK(story_highest_unlocked()==2,"level 2 unlocked");
    CHK(story_current_level()==2,"cursor advanced");
    story_set_current_level(1);
    int paid2 = story_complete_level(1, NULL);
    CHK(paid2==g_story_levels[0].reward/2,"replay pays half");

    /* ── Dynamic payouts: the same level pays differently by how it flew ── */
    {
        const StoryLevel* L3 = &g_story_levels[2];
        int floor3 = L3->reward;
        /* Idler: ran the clock out, shot nothing, took a hit. Floor only. */
        StoryPerf idle = {0};
        idle.secs = 200; idle.par_secs = 100; idle.par_kills = 20;
        idle.hits_taken = 2;
        g_story.cleared[0]=0; g_story.cleared_count=0;
        int lazy = story_complete_level(3, &idle);
        CHK(lazy==floor3, "doing nothing pays only the floor");
        CHK(story_pay_combat()==0, "no kills, no combat bonus");
        CHK(story_pay_speed()==0,  "over par, no speed bonus");

        /* Ace: half par, everything destroyed, sharp shooting, untouched. */
        StoryPerf ace = {0};
        ace.secs = 40; ace.par_secs = 100;
        ace.kills = 40; ace.par_kills = 20;
        ace.shots = 100; ace.hits = 90; ace.hits_taken = 0;
        g_story.cleared[0]=0; g_story.cleared_count=0;
        int good = story_complete_level(3, &ace);
        CHK(good > lazy, "flying it well pays more than idling");
        CHK(story_pay_speed()>0 && story_pay_combat()>0 &&
            story_pay_precision()>0 && story_pay_clean()>0, "every bonus paid");
        CHK(good <= floor3*3, "bonuses stay bounded");

        /* Slow but thorough sits in between. */
        StoryPerf mid = {0};
        mid.secs = 95; mid.par_secs = 100;
        mid.kills = 20; mid.par_kills = 20;
        mid.shots = 100; mid.hits = 40; mid.hits_taken = 1;
        g_story.cleared[0]=0; g_story.cleared_count=0;
        int okay = story_complete_level(3, &mid);
        CHK(okay > lazy && okay < good, "a middling clear pays in between");
    }

    /* ── Dying: the wreck, not a reset ──────────────────────────────────── */
    save_init_defaults();
    story_init();
    for(int lv=1; lv<25; lv++){ story_complete_level(lv, NULL); }
    story_set_current_level(24);
    u32 purse = story_chubbcoin();
    int unlocked_before = story_highest_unlocked();
    story_lose_life(); story_lose_life();
    CHK(story_lives()==1,"lives burn down");
    CHK(!story_is_grounded(),"losing a life alone does not ground the ship");
    int resume = story_lose_life();
    CHK(story_last_relocked()==2,"the wreck relocks the last two levels");
    CHK(!story_is_cleared(24) && !story_is_cleared(23),"those two clears are wiped");
    CHK(story_is_cleared(22),"the level before them is untouched");
    CHK(story_highest_unlocked()==unlocked_before-2,"the frontier walks back two");
    CHK(resume==23,"you resume at the first relocked level");
    CHK(story_chubbcoin() < purse,"the wreck costs money");
    CHK(story_last_repair_bill() ==
        (int)(purse - story_chubbcoin()),"the bill matches what was taken");
    CHK(story_lives()==3,"lives restocked for the repaired ship");

    /* Fifteen real minutes in the yard, and nothing flies until it is up. */
    CHK(story_is_grounded(),"the ship is grounded after a wreck");
    CHK(story_repair_seconds_left() > 14*60,"about fifteen minutes to wait");
    CHK(story_repair_seconds_left() <= 15*60,"and no more than fifteen");
    { char rb[16]; story_format_repair(rb,sizeof rb); CHK(rb[0],"countdown formats"); }

    /* The clock is wall-clock, so it runs while the game is closed. */
    save_write();
    memset(&g_story,0,sizeof(g_story));
    save_load();
    CHK(story_is_grounded(),"the repair deadline survives a save/load");
    g_fake_epoch += 14*60;
    CHK(story_is_grounded(),"still grounded after fourteen minutes");
    g_fake_epoch += 2*60;
    CHK(!story_is_grounded(),"the ship comes back after fifteen");
    CHK(story_repair_seconds_left()==0,"and the countdown reads zero");

    /* A device clock jumping backwards must not strand the player. */
    story_wreck_ship();
    g_fake_epoch -= 60*60;
    CHK(story_repair_seconds_left() <= 15*60,"a backwards clock is clamped");
    story_finish_repairs();
    CHK(!story_is_grounded(),"repairs can be handed back early");

    /* Wrecking at the very start cannot take level 1 away. */
    save_init_defaults();
    story_init();
    story_set_current_level(1);
    int r1 = story_wreck_ship();
    CHK(r1==1,"a wreck on level 1 resumes at level 1");
    CHK(story_is_unlocked(1),"level 1 is never relocked");
    CHK(story_last_relocked()==0,"nothing to relock that early");
    story_finish_repairs();

    save_init_defaults();
    story_init();
    for(int lv=1; lv<25; lv++){ story_complete_level(lv, NULL); }

    /* save round-trip through SRAM */
    g_story.chubbcoin=4242; g_story.level=21; g_story.unlocked=25;
    save_write();
    StorySave before=g_story;
    memset(&g_story,0,sizeof(g_story));
    save_load();
    CHK(g_story.chubbcoin==before.chubbcoin,"chubbcoin persisted");
    CHK(g_story.unlocked==before.unlocked,"unlock frontier persisted");
    CHK(g_story.cleared_count==before.cleared_count,"clears persisted");
    CHK(story_is_cleared(20),"boss clear persisted");

    /* ── Mr Chubbs docks every FIFTH level, and each dock is one visit ── */
    g_story.docks_used = 0;
    for(int lv=1; lv<=STORY_LEVEL_COUNT; lv++)
        CHK(story_shop_at(lv) == ((lv % 5) == 4), "dock only every fifth level");
    CHK(story_shop_at(4) && story_shop_at(9) && story_shop_at(69), "docks at 4/9/69");
    CHK(story_shop_at(74) && story_shop_at(79), "kingdom 8 gets docks too");
    CHK(!story_shop_at(5) && !story_shop_at(10) && !story_shop_at(70), "no dock off the fives");
    CHK(!story_shop_at(80), "no dock after the last level");
    /* Every dock sits immediately before a boss on the tenth levels.  Level
     * 80 keeps the pre-run pep dock even though it is a drone attack. */
    CHK(story_boss_dock(10) && story_boss_dock(20) && story_boss_dock(70) &&
        story_boss_dock(80),"boss docks on the 10s");
    CHK(!story_boss_dock(9) && !story_boss_dock(11),"no boss dock off the 10s");
    CHK(story_dock_index(4)==0 && story_dock_index(9)==1 && story_dock_index(69)==13 &&
        story_dock_index(74)==14 && story_dock_index(79)==15,
        "dock indices run 0..15");
    CHK(story_dock_index(7)<0, "non-dock levels have no index");

    story_shop_open(9);
    const StoryStockItem* a=story_shop_slot(0);
    CHK(a->kind==SSTOCK_LIFE,"slot 0 sells lives");
    CHK(a->qty>=1,"lives in stock");
    CHK(story_shop_level()==9,"dock level reported");
    u16 p1=story_shop_slot(2)->price; u8 k1=story_shop_slot(2)->kind;
    story_shop_open(9);
    CHK(story_shop_slot(2)->price==p1 && story_shop_slot(2)->kind==k1,"re-entering a dock keeps the shelf");

    /* Leaving spends the dock for good, but the unsold shelf rides along. */
    story_shop_close();
    CHK(story_shop_level()==0, "leaving closes the dock");
    CHK(!story_shop_can_open(9), "a spent dock never reopens");
    story_shop_open(9);
    CHK(story_shop_level()==0, "reopening a spent dock is refused");

    CHK(story_shop_can_open(14), "the next dock is still ahead");
    CHK(story_shop_next_dock(9)==14, "next dock after 9 is 14");
    story_shop_open(14);
    CHK(story_shop_level()==14, "the next dock opens normally");
    CHK(story_shop_slot(2)->price==p1 && story_shop_slot(2)->kind==k1,"unsold stock carries over");
    CHK(story_shop_slot_held_over(2),"carried stock is flagged HELD OVER");
    CHK(!story_shop_slot_held_over(0),"lives restock every dock");

    /* Spent docks survive a save/load round trip. */
    save_write();
    memset(&g_story,0,sizeof(g_story));
    save_load();
    CHK(!story_shop_can_open(9), "spent dock persisted");
    CHK(story_shop_can_open(19), "unspent dock persisted");

    /* the shelf never offers something already owned */
    g_story.docks_used = 0;
    for(int lv=4; lv<=40; lv+=5){
        story_shop_close();
        g_story.docks_used = 0;
        story_shop_open(lv);
        for(int i=1;i<STORY_SHOP_SLOTS;i++){
            const StoryStockItem* it=story_shop_slot(i);
            if(it->kind==SSTOCK_WEAPON) CHK(!(g_settings.owned_rigs&(1u<<it->item)),"no owned rig on the shelf");
            if(it->kind==SSTOCK_PAINT)  CHK(!(g_settings.owned_accents&(1u<<it->item)),"no owned paint on the shelf");
        }
    }

    /* boss docks hand over exactly one free life, once per boss */
    g_story.boss_gifts=0; g_story.lives=3;
    story_shop_open(4);                        /* level 5 next: not a boss */
    CHK(!story_shop_is_boss_dock(),"level 5 is not a boss dock");
    CHK(story_shop_take_gift()==0,"no gift off a boss dock");
    story_shop_open(9);                        /* level 10 next: Alien */
    CHK(story_shop_is_boss_dock(),"dock before level 10 is a boss dock");
    CHK(story_shop_take_gift()==1,"boss dock gifts a life");
    CHK(story_lives()==4,"the free life landed");
    CHK(story_shop_take_gift()==0,"the gift is one per boss");
    story_shop_open(19);                       /* level 20 next: Gemini */
    CHK(story_shop_take_gift()==1,"each boss has its own gift");
    CHK(story_lives()==5,"second free life landed");
    CHK(story_shop_line1()[0] && story_shop_line2()[0],"Mr Chubbs has something to say");

    /* every level carries its own two lines of story */
    for(int i=0;i<STORY_LEVEL_COUNT;i++){
        CHK(g_story_levels[i].brief1 && g_story_levels[i].brief1[0],"level has a story line");
        CHK(g_story_levels[i].brief2 && g_story_levels[i].brief2[0],"level has a second story line");
    }

    /* ── The opening speech ─────────────────────────────────────────────
     * Fourteen pages, two lines each, and the markers must be invisible to
     * the typewriter (they are styling, not characters). */
    for(int i=0;i<STORY_INTRO_PAGES;i++){
        CHK(g_story_intro[i][0] && g_story_intro[i][0][0], "intro page has a first line");
        CHK(g_story_intro[i][1] && g_story_intro[i][1][0], "intro page has a second line");
        for(int l=0;l<2;l++){
            char plain[STORY_INTRO_LINE_MAX]; u8 sp[STORY_INTRO_LINE_MAX];
            int n = story_intro_markup(g_story_intro[i][l], plain, sp, sizeof plain);
            CHK(n == story_intro_len(g_story_intro[i][l]), "markup length matches");
            CHK(n < STORY_INTRO_LINE_MAX, "line fits the draw buffer");
            for(int c=0;c<n;c++) CHK(plain[c] != '*' && plain[c] != '!', "markers stripped");
        }
    }
    CHK(STORY_INTRO_PAGES==14, "fourteen pages of story");
    {
        char plain[STORY_INTRO_LINE_MAX]; u8 sp[STORY_INTRO_LINE_MAX];
        int n = story_intro_markup("He wanted *revenge*.", plain, sp, sizeof plain);
        CHK(!strcmp(plain,"He wanted revenge."), "bold markers vanish from the text");
        CHK(sp[0]==STORY_MK_PLAIN && sp[10]==STORY_MK_BOLD, "the bold span is marked");
        (void)n;
        story_intro_markup("!one that can kill!", plain, sp, sizeof plain);
        CHK(!strcmp(plain,"one that can kill"), "faint markers vanish too");
        CHK(sp[0]==STORY_MK_FAINT, "the faint span is marked");
    }
    /* ── The outro ──────────────────────────────────────────────────────
     * Two pages: "YOU DID IT, JACK." and then "WELCOME HOME."  The slow
     * fade to white and the trip back to the main menu belong to menu.c. */
    CHK(STORY_OUTRO_PAGES == 2, "the outro is two pages");
    CHK(!strcmp(g_story_outro[0][0], "YOU DID IT, JACK."), "the outro tells Jack he did it");
    CHK(!strcmp(g_story_outro[1][0], "WELCOME HOME."), "the outro welcomes Jack home");
    for(int i=0;i<STORY_OUTRO_PAGES;i++){
        char plain[STORY_INTRO_LINE_MAX]; u8 sp[STORY_INTRO_LINE_MAX];
        for(int l=0;l<2;l++){
            int n = story_intro_markup(g_story_outro[i][l], plain, sp, sizeof plain);
            CHK(n == story_intro_len(g_story_outro[i][l]), "outro markup length matches");
            CHK(n < STORY_INTRO_LINE_MAX, "outro line fits the draw buffer");
        }
    }

    /* It plays once and once only. */
    g_story.intro_seen = 0;
    CHK(!story_intro_seen(), "a fresh save has not seen the intro");
    story_mark_intro_seen();
    CHK(story_intro_seen(), "watching it sets the flag");
    save_write();
    memset(&g_story,0,sizeof(g_story));
    save_load();
    CHK(story_intro_seen(), "the flag persists, so it never replays");

    /* escape hatch */
    story_free_everything();
    CHK(story_content_unlocked(),"LET ME BE FREE unlocks content");

    printf(fails? "\n%d CHECK(S) FAILED\n" : "\nALL CHECKS PASSED\n", fails);
    return fails?1:0;
}
