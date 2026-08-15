#include <stdio.h>
#include <string.h>
#include "save.h"
#include "story.h"
static int fails=0;
#define CHK(c,msg) do{ if(!(c)){ printf("FAIL: %s\n", msg); fails++; } }while(0)
int main(void){
    printf("StorySave=%zu bytes\n", sizeof(StorySave));
    save_init_defaults();
    story_init();
    CHK(story_current_level()==1,"start level 1");
    CHK(story_lives()==3,"start 3 lives");
    CHK(!story_content_unlocked(),"content locked at start");

    /* every level must be flyable: sane fields, non-zero reward */
    for(int i=0;i<STORY_LEVEL_COUNT;i++){
        const StoryLevel* L=&g_story_levels[i];
        int lv=i+1;
        CHK(L->name && L->name[0], "level has a name");
        CHK(L->reward>0, "level pays out");
        if(L->objective==OBJ_BOSS){
            CHK(lv%10==0, "boss only on 10s");
            CHK(story_boss_for_level(lv)>=0, "boss id resolves");
        } else {
            CHK(lv%10!=0, "non-boss off the 10s");
            CHK(L->rocks<=34 && L->drones<=6, "field within engine budget");
            if(L->objective==OBJ_HUNT) CHK(L->quota>0 && L->quota<=90,"hunt quota sane");
            if(L->objective==OBJ_SURVIVE) CHK(L->quota>=15 && L->quota<=90,"survive secs sane");
        }
    }
    /* monotonic difficulty */
    for(int i=1;i<STORY_LEVEL_COUNT;i++){
        CHK(g_story_levels[i].speed_pct>=g_story_levels[i-1].speed_pct,"speed never drops");
        CHK(g_story_levels[i].hp_pct>=g_story_levels[i-1].hp_pct,"hp never drops");
    }
    /* unique names */
    for(int i=0;i<STORY_LEVEL_COUNT;i++)
      for(int j=i+1;j<STORY_LEVEL_COUNT;j++)
        if(!strcmp(g_story_levels[i].name,g_story_levels[j].name)){printf("FAIL: dup name %s\n",g_story_levels[i].name);fails++;}

    /* progression + replay half pay */
    int paid = story_complete_level(1);
    CHK(paid==g_story_levels[0].reward,"first clear full reward");
    CHK(story_highest_unlocked()==2,"level 2 unlocked");
    CHK(story_current_level()==2,"cursor advanced");
    story_set_current_level(1);
    int paid2 = story_complete_level(1);
    CHK(paid2==g_story_levels[0].reward/2,"replay pays half");

    /* checkpoint on death */
    for(int lv=1; lv<25; lv++){ story_complete_level(lv); }
    story_set_current_level(24);
    story_lose_life(); story_lose_life();
    CHK(story_lives()==1,"lives burn down");
    int resume = story_lose_life();
    CHK(resume==21,"reset to level after previous boss");
    CHK(story_lives()==3,"lives refill at checkpoint");

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

    /* shop determinism + stock */
    story_shop_open(10);
    const StoryStockItem* a=story_shop_slot(0);
    CHK(a->kind==SSTOCK_LIFE,"slot 0 sells lives");
    CHK(a->qty>=1,"lives in stock");
    u16 p1=story_shop_slot(2)->price; u8 k1=story_shop_slot(2)->kind;
    story_shop_open(15); story_shop_open(10);
    CHK(story_shop_slot(2)->price==p1 && story_shop_slot(2)->kind==k1,"shelf is deterministic per dock");

    /* escape hatch */
    story_free_everything();
    CHK(story_content_unlocked(),"LET ME BE FREE unlocks content");

    printf(fails? "\n%d CHECK(S) FAILED\n" : "\nALL CHECKS PASSED\n", fails);
    return fails?1:0;
}
