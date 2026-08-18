#include "menu.h"
#include "renderer.h"
#include "game.h"
#include "audio.h"
#include "save.h"
#include "starfield.h"
#include <string.h>

#ifdef PLATFORM_HOST
/* The Multiplayer tab talks directly to the EOS quick-match layer - taps on
 * the FIND MATCH / LEAVE buttons drive matchmaking from the menu itself. */
#include "eos_online.h"
#include "coop.h"
/* Story Mode: the 70-level campaign, its map, and Mr Chubbs' shop. */
#include "story.h"
#endif

static GameScreen s_current_screen = SCREEN_MAIN_MENU;
static int s_menu_selected = 0;
static int s_mp_selected = 0;   // Multiplayer tab button focus
static int s_anim_frame = 0;

// Shop state: 5 categories (0=PAINTS,1=TRAILS,2=WEAPONS,3=LASERS,4=SHIPS)
#define SHOP_CAT_COUNT 5
#define SHOP_CAT_SHIPS 4
static int s_shop_category = 0;
static int s_shop_selected[SHOP_CAT_COUNT] = { 0, 0, 0, 0, 0 };
static int s_shop_scroll[SHOP_CAT_COUNT]   = { 0, 0, 0, 0, 0 };

// Upgrades state: 8 core upgrades, 5 visible
static int s_upg_selected = 0;
static int s_upg_scroll = 0;

// Settings (options) state
static int s_opt_selected = 0;

static int s_shop_msg_timer = 0;
static char s_shop_msg[36] = {0};
static u8 s_shop_msg_col = PAL_TEXT_GOLD;

static bool s_static_valid = false;

static int s_tap_x = -1;
static int s_tap_y = -1;
static bool s_tap_pending = false;

#ifdef PLATFORM_HOST
/* Raised when the player activates the Settings -> CODES row; drained by the
 * Android layer which opens the system text dialog. */
static int s_code_request = 0;
static int s_erase_request = 0;

int menu_take_code_request(void) {
    int r = s_code_request;
    s_code_request = 0;
    return r;
}

int menu_take_erase_request(void) {
    int r = s_erase_request;
    s_erase_request = 0;
    return r;
}
#endif

static void menu_static_invalidate(void);

#ifdef PLATFORM_HOST
/* Story Mode screens live further down this file; menu_open() and the
 * update/draw dispatchers need them early. */
static void intro_reset(void);
static void map_focus_current(void);
static void story_shop_reset_ui(void);
static void update_story_intro(void);
static void update_story_map(void);
static void update_story_shop(void);
static void update_story_result(void);
static void render_story_intro(void);
static void render_story_map(void);
static void render_story_shop(void);
static void render_story_result(void);
static void story_enter_result(void);
#endif
static void draw_preview_engine_trail(int ship_x, int ship_y, int trail_idx);

void menu_request_full_redraw(void) {
    menu_static_invalidate();
}

static int shop_get_category_count(int cat);

// ── Smooth scroll (Android host only) ──────────────────────────────────
// The two scrollable lists (hangar shop, upgrades) use a pixel-precise
// offset on the host so drags and flings glide 1:1 instead of snapping one
// whole row at a time.  The GBA build keeps the existing integer row scroll.
#define LIST_ROW_H 21

static int hangar_list_top(void) { return 34; }   // first row baseline (game px)
static int upgrades_list_top(void) { return 21; }

/* Rows listed on the Upgrades screen. On Android the rapid-fire powerup no
 * longer drops, so the "Rapid" duration tech (UPG_OVERDRIVE, the last entry)
 * would be a dead purchase — it is hidden. It stays in the save data. */
static int upgrades_row_count(void) {
#ifdef PLATFORM_HOST
    return NUM_UPGRADES - 1;
#else
    return NUM_UPGRADES;
#endif
}

#ifdef PLATFORM_HOST
static float s_shop_scroll_px = 0;
static float s_upg_scroll_px = 0;

static int shop_scroll_offs(void) { return (int)s_shop_scroll_px; }
static int upg_scroll_offs(void) { return (int)s_upg_scroll_px; }

static int shop_max_scroll(void) {
    int count = shop_get_category_count(s_shop_category);
    int max_i = count - 5; if (max_i < 0) max_i = 0;
    return max_i * LIST_ROW_H;
}

static int upg_max_scroll(void) {
    int max_i = upgrades_row_count() - 5; if (max_i < 0) max_i = 0;
    return max_i * LIST_ROW_H;
}

float menu_scroll_get(void) {
    if (s_current_screen == SCREEN_HANGAR) return s_shop_scroll_px;
    if (s_current_screen == SCREEN_SETTINGS) return s_upg_scroll_px;
    return 0;
}

float menu_scroll_max(void) {
    if (s_current_screen == SCREEN_HANGAR) return (float)shop_max_scroll();
    if (s_current_screen == SCREEN_SETTINGS) return (float)upg_max_scroll();
    return 0;
}

void menu_scroll_to(float px) {
    float maxp = menu_scroll_max();
    if (px < 0) px = 0;
    if (px > maxp) px = maxp;
    if (s_current_screen == SCREEN_HANGAR) {
        s_shop_scroll_px = px;
        s_shop_scroll[s_shop_category] = (int)((px + LIST_ROW_H / 2) / LIST_ROW_H);
    } else if (s_current_screen == SCREEN_SETTINGS) {
        s_upg_scroll_px = px;
        s_upg_scroll = (int)((px + LIST_ROW_H / 2) / LIST_ROW_H);
    }
}
#else
static int shop_scroll_offs(void) { return s_shop_scroll[s_shop_category] * LIST_ROW_H; }
static int upg_scroll_offs(void) { return s_upg_scroll * LIST_ROW_H; }
#endif

static void menu_static_invalidate(void) { s_static_valid = false; }
static void menu_static_begin(void) { gfx_set_target(gfx_static_layer); }
static void menu_static_end(void) { gfx_set_target(NULL); s_static_valid = true; }
static void menu_draw_base(void) { gfx_apply_static(); starfield_draw_stars(0, 0); }

void menu_init(void) {
    s_current_screen = SCREEN_MAIN_MENU;
    s_menu_selected = 0;
    s_anim_frame = 0;
    s_shop_msg_timer = 0;
    audio_play_bgm(BGM_MENU);
}

void menu_open(GameScreen screen) {
    s_current_screen = screen;
    s_menu_selected = 0;
    s_shop_msg_timer = 0;
    s_upg_selected = 0;
    s_upg_scroll = 0;
#ifdef PLATFORM_HOST
    s_upg_scroll_px = 0;
#endif
    s_tap_pending = false;
    s_mp_selected = 0;
    menu_static_invalidate();
#ifdef PLATFORM_HOST
    /* The campaign has its own soundtrack (Assets/Audio/story_mode.mp3): it
     * starts under the opening speech and keeps playing across the map, the
     * dock and the result cards, so Story Mode never sounds like the arcade
     * front end. */
    if (screen == SCREEN_STORY_INTRO || screen == SCREEN_STORY_MAP ||
        screen == SCREEN_STORY_SHOP  || screen == SCREEN_STORY_RESULT) {
        audio_play_bgm(BGM_STORY);
    } else
#endif
    if (screen == SCREEN_MAIN_MENU || screen == SCREEN_HANGAR || screen == SCREEN_SETTINGS ||
        screen == SCREEN_CONTROLS || screen == SCREEN_OPTIONS || screen == SCREEN_MODE_SELECT ||
        screen == SCREEN_MULTIPLAYER) {
        audio_play_bgm(BGM_MENU);
    }
#ifdef PLATFORM_HOST
    if (screen == SCREEN_STORY_INTRO) intro_reset();
    if (screen == SCREEN_STORY_MAP)   map_focus_current();
    if (screen == SCREEN_STORY_SHOP)  story_shop_reset_ui();
    /* Each kingdom flies over its own sky; the campaign menus preview the
     * sector the cursor is parked in. */
    if (screen == SCREEN_STORY_INTRO) {
        starfield_set_theme(SF_THEME_ARCADE);
    } else if (screen == SCREEN_STORY_MAP || screen == SCREEN_STORY_SHOP ||
               screen == SCREEN_STORY_RESULT) {
        starfield_set_theme(story_theme_for_level(story_current_level()));
    } else {
        starfield_set_theme(SF_THEME_ARCADE);
    }
#endif
    s_opt_selected = 0;
}

GameScreen menu_current_screen(void) {
    return s_current_screen;
}

void menu_queue_tap(int x, int y) {
    s_tap_x = x;
    s_tap_y = y;
    s_tap_pending = true;
}

void menu_go_back(void) {
    switch (s_current_screen) {
        case SCREEN_HANGAR:
        case SCREEN_SETTINGS:
            save_write();
            menu_open(SCREEN_MAIN_MENU);
            break;
        case SCREEN_MULTIPLAYER:
            /* Backing out never cancels a live match - matchmaking runs in
             * the background and the menu chip keeps showing its status. */
            menu_open(SCREEN_MAIN_MENU);
            break;
        case SCREEN_CONTROLS:
        case SCREEN_OPTIONS:
            save_write();
            menu_open(SCREEN_MAIN_MENU);
            break;
        case SCREEN_MODE_SELECT:
            menu_open(SCREEN_MAIN_MENU);
            break;
#ifdef PLATFORM_HOST
        case SCREEN_STORY_INTRO:
            story_mark_intro_seen();
            menu_open(SCREEN_STORY_MAP);
            break;
        case SCREEN_STORY_SHOP:
            /* Backing out of the dock is still leaving it: one visit only. */
            story_shop_close();
            menu_open(SCREEN_STORY_MAP);
            break;
        case SCREEN_STORY_RESULT:
            menu_open(SCREEN_STORY_MAP);
            break;
        case SCREEN_STORY_MAP:
            save_write();
            menu_open(SCREEN_MAIN_MENU);
            break;
#endif
        case SCREEN_PAUSED:
            s_current_screen = SCREEN_PLAYING;
            break;
        default:
            break;
    }
}

static bool in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static bool consume_tap(int* x, int* y) {
    if (!s_tap_pending) return false;
    *x = s_tap_x;
    *y = s_tap_y;
    s_tap_pending = false;
    return true;
}

static int shop_list_width(void) {
    return 116 + (SCREEN_WIDTH - 240) / 2;
}

static void shop_set_msg(const char* msg, u8 col) {
    strncpy(s_shop_msg, msg, sizeof(s_shop_msg)-1);
    s_shop_msg[sizeof(s_shop_msg)-1] = '\0';
    s_shop_msg_col = col;
    s_shop_msg_timer = 90;
}

static int shop_get_category_count(int cat) {
    switch (cat) {
        case 0: return NUM_ACCENTS;
        case 1: return NUM_TRAILS;
        case 2: return NUM_RIGS;
        case 3: return NUM_LASERS;
        case SHOP_CAT_SHIPS: return NUM_SHIP_STYLES;
        default: return 0;
    }
}

#ifdef PLATFORM_HOST
/* Everything outside Story Mode stays sealed until the campaign is finished
 * (or the player taps "LET ME BE FREE" three times in Settings). */
static bool story_gate_open(void) { return story_content_unlocked(); }

static void story_locked_msg(void) {
    shop_set_msg("FINISH STORY MODE FIRST", PAL_TEXT_RED);
}
#endif

static void request_play(void) {
    /* Both platforms go through the PLAY tab. On Android that tab leads
     * with Story Mode; Waves and Endless stay locked until it is beaten. */
    menu_open(SCREEN_MODE_SELECT);
}

static void launch_mode(GameMode mode) {
    game_set_mode(mode);
    game_start();
    s_current_screen = SCREEN_PLAYING;
}

#ifdef PLATFORM_HOST
/* Jump into the campaign: the opening speech the first time, the map after. */
static void enter_story_mode(void) {
    menu_open(story_intro_seen() ? SCREEN_STORY_MAP : SCREEN_STORY_INTRO);
}
#endif

#define SHOP_TAB_W 45

static int shop_tab_x(int t) {
    int tab_gap = (SCREEN_WIDTH - 8 - SHOP_CAT_COUNT * SHOP_TAB_W) / (SHOP_CAT_COUNT - 1);
    if (tab_gap < 1) tab_gap = 1;
    return 4 + t * (SHOP_TAB_W + tab_gap);
}

static void hangar_activate(void) {
    int cat = s_shop_category;
    int sel = s_shop_selected[cat];
    bool ok = false;
    switch (cat) {
        case 0:
            if (shop_is_accent_owned(sel)) { shop_equip_accent(sel); shop_set_msg("EQUIPPED PAINT!", PAL_TEXT_GREEN); }
            else { ok = shop_try_purchase_accent(sel); shop_set_msg(ok ? "PURCHASED PAINT!" : "NEED MORE COINS!", ok ? PAL_TEXT_GOLD : PAL_TEXT_RED); if (ok) audio_play_sfx(SFX_PICKUP); }
            break;
        case 1:
            if (shop_is_trail_owned(sel)) { shop_equip_trail(sel); shop_set_msg("EQUIPPED TRAIL!", PAL_TEXT_GREEN); }
            else { ok = shop_try_purchase_trail(sel); shop_set_msg(ok ? "PURCHASED TRAIL!" : "NEED MORE COINS!", ok ? PAL_TEXT_GOLD : PAL_TEXT_RED); if (ok) audio_play_sfx(SFX_PICKUP); }
            break;
        case 2: {
            WeaponRig rig = (WeaponRig)sel;
            if (shop_is_rig_owned(rig)) { shop_equip_rig(rig); shop_set_msg("EQUIPPED WEAPON!", PAL_TEXT_GREEN); }
            else { ok = shop_try_purchase_rig(rig); shop_set_msg(ok ? "PURCHASED WEAPON!" : "NEED MORE COINS!", ok ? PAL_TEXT_GOLD : PAL_TEXT_RED); if (ok) audio_play_sfx(SFX_PICKUP); }
            break;
        }
        case 3:
            if (shop_is_laser_owned(sel)) { shop_equip_laser(sel); shop_set_msg("EQUIPPED LASER!", PAL_TEXT_GREEN); }
            else { ok = shop_try_purchase_laser(sel); shop_set_msg(ok ? "PURCHASED LASER!" : "NEED MORE COINS!", ok ? PAL_TEXT_GOLD : PAL_TEXT_RED); if (ok) audio_play_sfx(SFX_PICKUP); }
            break;
        case SHOP_CAT_SHIPS:
            if (shop_is_ship_owned(sel)) { shop_equip_ship(sel); shop_set_msg("HULL EQUIPPED!", PAL_TEXT_GREEN); }
            else { ok = shop_try_purchase_ship(sel); shop_set_msg(ok ? "NEW HULL! LAUNCH READY" : "NEED MORE COINS!", ok ? PAL_TEXT_GOLD : PAL_TEXT_RED); if (ok) audio_play_sfx(SFX_PICKUP); }
            break;
    }
    menu_static_invalidate();
}

static void hangar_select_item(int idx) {
    int cat = s_shop_category;
    int count = shop_get_category_count(cat);
    if (idx < 0 || idx >= count) return;
    s_shop_selected[cat] = idx;
    if (idx < s_shop_scroll[cat]) s_shop_scroll[cat] = idx;
    else if (idx >= s_shop_scroll[cat] + 5) s_shop_scroll[cat] = idx - 4;
#ifdef PLATFORM_HOST
    s_shop_scroll_px = (float)(s_shop_scroll[cat] * LIST_ROW_H);
#endif
    menu_static_invalidate();
}

static void format_price(char* dst, int price) {
    char tmp[12];
    if (price >= 1000000000) {
        siprintf(tmp, "%dB", price / 1000000000);
#ifdef PLATFORM_HOST
    } else if (price >= 1000000) {
        int m = price / 1000000;
        int tenth = (price % 1000000) / 100000;
        if (tenth && m < 100) siprintf(tmp, "%d.%dM", m, tenth);
        else siprintf(tmp, "%dM", m);
    } else if (price >= 1000) {
#else
    } else if (price >= 1000000) siprintf(tmp, "%dM", price / 1000000);
    else if (price >= 1000) {
#endif
        if (price % 1000 == 0) siprintf(tmp, "%dk", price / 1000);
        else siprintf(tmp, "%d.%dk", price / 1000, (price % 1000) / 100);
    } else siprintf(tmp, "%dc", price);
    strcpy(dst, tmp);
}

static void main_menu_activate(int index) {
#ifdef PLATFORM_HOST
    /* Shop, Upgrades and Multiplayer are campaign rewards. */
    if (!story_gate_open() && (index == 1 || index == 2 || index == 3)) {
        story_locked_msg();
        return;
    }
#endif
    switch (index) {
        case 0: request_play(); break;
        case 1: menu_open(SCREEN_MULTIPLAYER); break; // 2P co-op quick match
        case 2: menu_open(SCREEN_HANGAR); break;
        case 3: menu_open(SCREEN_SETTINGS); break; // now UPGRADES
        case 4: menu_open(SCREEN_CONTROLS); break;
        case 5: menu_open(SCREEN_OPTIONS); break;
        default: break;
    }
}

static void update_main_menu(void) {
    const int count = 6;
    int tx, ty;
    if (consume_tap(&tx, &ty)) {
        for (int i = 0; i < count; i++) {
            if (in_rect(tx, ty, 8, 42 + i * 19, 96, 19)) {
                s_menu_selected = i;
                main_menu_activate(i);
                return;
            }
        }
    }
    if (key_hit(KEY_UP)) s_menu_selected = (s_menu_selected + count - 1) % count;
    if (key_hit(KEY_DOWN)) s_menu_selected = (s_menu_selected + 1) % count;
    if (key_hit(KEY_A) || key_hit(KEY_START)) {
        main_menu_activate(s_menu_selected);
    }
}

// ── HANGAR / SHOP (4 tabs now) ────────────────────────────────────────
static void update_hangar(void) {
    if (s_shop_msg_timer > 0) s_shop_msg_timer--;
    int cat = s_shop_category;
    int count = shop_get_category_count(cat);
    int list_w = shop_list_width();
    int right_x = 4 + list_w + 2;
    int right_w = SCREEN_WIDTH - 4 - right_x;

    int tx, ty;
    if (consume_tap(&tx, &ty)) {
        // Full-width tab strip: four equal touch zones so native taps never miss.
        if (ty >= 14 && ty < 33) {
            int zone = (SCREEN_WIDTH - 8) / SHOP_CAT_COUNT;
            if (zone < 1) zone = 1;
            int tapped = (tx - 4) / zone;
            if (tx < 4) tapped = 0;
            if (tapped < 0) tapped = 0;
            if (tapped >= SHOP_CAT_COUNT) tapped = SHOP_CAT_COUNT - 1;
            if (tapped != s_shop_category) {
                s_shop_category = tapped;
#ifdef PLATFORM_HOST
                s_shop_scroll_px = 0;   // fresh tab starts at top
#endif
                menu_static_invalidate();
            }
            return;
        }
#ifdef PLATFORM_HOST
        // Smooth-scroll aware item hit test (tap y maps through the pixel offset).
        {
            int off = shop_scroll_offs();
            int top = hangar_list_top();
            int idx = (ty - top + off) / LIST_ROW_H;
            if (idx >= 0 && idx < count) {
                int item_y = top + idx * LIST_ROW_H - off;
                if (in_rect(tx, ty, 6, item_y, list_w - 4, 19)) {
                    if (s_shop_selected[cat] == idx) hangar_activate();
                    else hangar_select_item(idx);
                    return;
                }
            }
        }
#else
        for (int i = 0; i < 5; i++) {
            int idx = s_shop_scroll[cat] + i;
            if (idx >= count) break;
            if (in_rect(tx, ty, 6, 34 + i * 21, list_w - 4, 19)) {
                if (s_shop_selected[cat] == idx) hangar_activate();
                else hangar_select_item(idx);
                return;
            }
        }
        // Scroll buttons - bigger touch targets for mobile (20px tall instead of 12)
        if (in_rect(tx, ty, 4, 31, list_w, 20) && s_shop_scroll[cat] > 0) {
            s_shop_scroll[cat]--;
            menu_static_invalidate();
            return;
        }
        if (in_rect(tx, ty, 4, 124, list_w, 20) && s_shop_scroll[cat] + 5 < count) {
            s_shop_scroll[cat]++;
            menu_static_invalidate();
            return;
        }
#endif
        if (in_rect(tx, ty, right_x + 4, 116, right_w - 8, 24)) {
            hangar_activate();
            return;
        }
#ifndef PLATFORM_HOST
        if (ty >= 144) {
            save_write();
            menu_open(SCREEN_MAIN_MENU);
            return;
        }
#endif
    }

    if (key_hit(KEY_L)) {
        s_shop_category = (s_shop_category + SHOP_CAT_COUNT - 1) % SHOP_CAT_COUNT;
#ifdef PLATFORM_HOST
        s_shop_scroll_px = 0;
#endif
        menu_static_invalidate(); return;
    }
    if (key_hit(KEY_R)) {
        s_shop_category = (s_shop_category + 1) % SHOP_CAT_COUNT;
#ifdef PLATFORM_HOST
        s_shop_scroll_px = 0;
#endif
        menu_static_invalidate(); return;
    }
    if (key_hit(KEY_LEFT)) {
        s_shop_category = (s_shop_category + SHOP_CAT_COUNT - 1) % SHOP_CAT_COUNT;
#ifdef PLATFORM_HOST
        s_shop_scroll_px = 0;
#endif
        menu_static_invalidate(); return;
    }
    if (key_hit(KEY_RIGHT)) {
        s_shop_category = (s_shop_category + 1) % SHOP_CAT_COUNT;
#ifdef PLATFORM_HOST
        s_shop_scroll_px = 0;
#endif
        menu_static_invalidate(); return;
    }

    if (key_hit(KEY_UP)) {
        s_shop_selected[cat] = (s_shop_selected[cat] + count - 1) % count;
        int sel = s_shop_selected[cat];
        if (sel < s_shop_scroll[cat]) s_shop_scroll[cat] = sel;
        else if (sel >= s_shop_scroll[cat] + 5) s_shop_scroll[cat] = sel - 4;
#ifdef PLATFORM_HOST
        s_shop_scroll_px = (float)(s_shop_scroll[cat] * LIST_ROW_H);
#endif
        menu_static_invalidate();
    }
    if (key_hit(KEY_DOWN)) {
        s_shop_selected[cat] = (s_shop_selected[cat] + 1) % count;
        int sel = s_shop_selected[cat];
        if (sel < s_shop_scroll[cat]) s_shop_scroll[cat] = sel;
        else if (sel >= s_shop_scroll[cat] + 5) s_shop_scroll[cat] = sel - 4;
#ifdef PLATFORM_HOST
        s_shop_scroll_px = (float)(s_shop_scroll[cat] * LIST_ROW_H);
#endif
        menu_static_invalidate();
    }

    if (key_hit(KEY_A)) hangar_activate();

    if (key_hit(KEY_START)) { save_write(); request_play(); }
    if (key_hit(KEY_B)) { save_write(); menu_open(SCREEN_MAIN_MENU); }
}

static void upgrades_ensure_visible(void) {
    if (s_upg_selected < s_upg_scroll) s_upg_scroll = s_upg_selected;
    else if (s_upg_selected >= s_upg_scroll + 5) s_upg_scroll = s_upg_selected - 4;
#ifdef PLATFORM_HOST
    s_upg_scroll_px = (float)(s_upg_scroll * LIST_ROW_H);
#endif
}

static void upgrades_activate(void) {
    UpgradeType upg = (UpgradeType)s_upg_selected;
    int cur = shop_get_upgrade_level(upg);
    if (cur >= UPG_MAX_LEVEL) {
        shop_set_msg("MAX LEVEL REACHED!", PAL_TEXT_GOLD);
    } else {
        bool ok = shop_try_purchase_upgrade(upg);
        if (ok) { shop_set_msg("UPGRADED! +POWER", PAL_TEXT_GOLD); audio_play_sfx(SFX_PICKUP); }
        else {
            char tmp[32]; char pbuf[16];
            format_price(pbuf, shop_get_upgrade_price(upg, cur));
            siprintf(tmp, "NEED %s COINS!", pbuf);
            shop_set_msg(tmp, PAL_TEXT_RED);
        }
    }
    menu_static_invalidate();
}

// ── NEW UPGRADES SCREEN (replaces settings) ───────────────────────────
static void update_upgrades(void) {
    if (s_shop_msg_timer > 0) s_shop_msg_timer--;
    const int count = upgrades_row_count(); // 8 on GBA, 7 on Android (no Rapid)

    int tx, ty;
    if (consume_tap(&tx, &ty)) {
        int list_w = shop_list_width();
        int right_x = 4 + list_w + 2;
        int right_w = SCREEN_WIDTH - 4 - right_x;
#ifdef PLATFORM_HOST
        {
            int off = upg_scroll_offs();
            int top = upgrades_list_top();
            int idx = (ty - top + off) / LIST_ROW_H;
            if (idx >= 0 && idx < count) {
                int item_y = top + idx * LIST_ROW_H - off;
                if (in_rect(tx, ty, 6, item_y, list_w - 4, 19)) {
                    if (s_upg_selected == idx) upgrades_activate();
                    else {
                        s_upg_selected = idx;
                        menu_static_invalidate();
                    }
                    return;
                }
            }
        }
#else
        for (int i = 0; i < 5; i++) {
            int idx = s_upg_scroll + i;
            if (idx >= count) break;
            if (in_rect(tx, ty, 6, 21 + i * 21, list_w - 4, 19)) {
                if (s_upg_selected == idx) upgrades_activate();
                else {
                    s_upg_selected = idx;
                    menu_static_invalidate();
                }
                return;
            }
        }
        // Scroll buttons - bigger touch targets for mobile (20px tall instead of 14)
        if (in_rect(tx, ty, 4, 18, list_w, 20) && s_upg_scroll > 0) {
            s_upg_scroll--;
            menu_static_invalidate();
            return;
        }
        if (in_rect(tx, ty, 4, 120, list_w, 20) && s_upg_scroll + 5 < count) {
            s_upg_scroll++;
            menu_static_invalidate();
            return;
        }
#endif
        if (in_rect(tx, ty, right_x + 4, 94, right_w - 8, 24)) {
            upgrades_activate();
            return;
        }
#ifndef PLATFORM_HOST
        if (ty >= 144) {
            save_write();
            menu_open(SCREEN_MAIN_MENU);
            return;
        }
#endif
    }

    if (key_hit(KEY_UP)) {
        s_upg_selected = (s_upg_selected + count - 1) % count;
        upgrades_ensure_visible();
        menu_static_invalidate();
    }
    if (key_hit(KEY_DOWN)) {
        s_upg_selected = (s_upg_selected + 1) % count;
        upgrades_ensure_visible();
        menu_static_invalidate();
    }

    if (key_hit(KEY_A)) upgrades_activate();
    if (key_hit(KEY_START)) { save_write(); request_play(); }
    if (key_hit(KEY_B)) { save_write(); menu_open(SCREEN_MAIN_MENU); }
}

static void update_controls(void) {
    int tx, ty;
    if (consume_tap(&tx, &ty)) { menu_open(SCREEN_MAIN_MENU); return; }
    if (key_hit(KEY_A) || key_hit(KEY_B) || key_hit(KEY_START)) menu_open(SCREEN_MAIN_MENU);
}

// ── SETTINGS screen (replaces credits) ────────────────────────────────
#ifdef PLATFORM_HOST
/* Android extra rows: haptics, cheat-code entry, wipe-all-data. */
#define OPT_ROW_HAPTICS 4
#define OPT_ROW_CODES   5
#define OPT_ROW_FREE    6   /* "LET ME BE FREE" - tap 3x to skip the story */
#define OPT_ROW_ERASE   7
#define OPT_ROW_H       16
#define OPT_ROW_Y0      18
#else
#define OPT_ROW_H       20
#define OPT_ROW_Y0      24
#endif

#ifdef PLATFORM_HOST
/* Consecutive taps on the LET ME BE FREE row. Three of them opens
 * everything the campaign is gating. */
static int s_free_taps = 0;
#endif

static int options_row_count(void) {
#ifdef PLATFORM_HOST
    return 8; // + haptics, codes, let-me-be-free, erase (Android only)
#else
    return 4;
#endif
}

static void options_set_difficulty(int diff) {
    if (diff < 0) diff = 2;
    if (diff > 2) diff = 0;
    g_settings.difficulty = (Difficulty)diff;
}

static void options_cycle(int row) {
    switch (row) {
        case 0: options_set_difficulty((int)g_settings.difficulty + 1); break;
        case 1:
            g_settings.music_volume += 10;
            if (g_settings.music_volume > 100) g_settings.music_volume = 0;
            break;
        case 2:
            g_settings.sfx_volume += 10;
            if (g_settings.sfx_volume > 100) g_settings.sfx_volume = 0;
            break;
        case 3: g_settings.screen_shake = !g_settings.screen_shake; break;
#ifdef PLATFORM_HOST
        case OPT_ROW_HAPTICS: g_settings.haptics = !g_settings.haptics; break;
        case OPT_ROW_CODES: s_code_request = 1; return; // opens the Android dialog; nothing to save
        case OPT_ROW_FREE:
            /* Three taps unlocks the shop, multiplayer and the arcade modes
             * without finishing the campaign. */
            if (story_content_unlocked()) return;
            s_free_taps++;
            if (s_free_taps >= 3) {
                story_free_everything();
                s_free_taps = 0;
                menu_static_invalidate();
            }
            return;
        case OPT_ROW_ERASE: s_erase_request = 1; return; // confirm dialog in Kotlin
#endif
        default: break;
    }
    save_write();
}

static void options_step(int row, int dir) {
    switch (row) {
        case 0: options_set_difficulty((int)g_settings.difficulty + dir); break;
        case 1: {
            int v = g_settings.music_volume + dir * 10;
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            g_settings.music_volume = v;
            break;
        }
        case 2: {
            int v = g_settings.sfx_volume + dir * 10;
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            g_settings.sfx_volume = v;
            break;
        }
        case 3: g_settings.screen_shake = !g_settings.screen_shake; break;
#ifdef PLATFORM_HOST
        case OPT_ROW_HAPTICS: g_settings.haptics = !g_settings.haptics; break;
#endif
        default: break;
    }
    save_write();
}

static void update_options(void) {
    const int count = options_row_count();
    int tx, ty;
    if (consume_tap(&tx, &ty)) {
        for (int i = 0; i < count; i++) {
            if (in_rect(tx, ty, 8, OPT_ROW_Y0 + i * OPT_ROW_H, SCREEN_WIDTH - 16, OPT_ROW_H - 2)) {
#ifdef PLATFORM_HOST
                if (i == OPT_ROW_CODES) {
                    // Cheat codes: a single tap opens the native text dialog.
                    s_opt_selected = i;
                    s_code_request = 1;
                    return;
                }
                if (i == OPT_ROW_ERASE) {
                    s_opt_selected = i;
                    s_erase_request = 1;
                    return;
                }
                if (i == OPT_ROW_FREE) {
                    /* Counts taps directly - no need to select the row first. */
                    s_opt_selected = i;
                    options_cycle(i);
                    return;
                }
#endif
                if (s_opt_selected == i) options_cycle(i);
                else s_opt_selected = i;
                return;
            }
        }
#ifndef PLATFORM_HOST
        if (ty >= 144) {
            save_write();
            menu_open(SCREEN_MAIN_MENU);
            return;
        }
#endif
    }
    if (key_hit(KEY_UP)) s_opt_selected = (s_opt_selected + count - 1) % count;
    if (key_hit(KEY_DOWN)) s_opt_selected = (s_opt_selected + 1) % count;
    if (key_hit(KEY_LEFT)) options_step(s_opt_selected, -1);
    if (key_hit(KEY_RIGHT)) options_step(s_opt_selected, 1);
    if (key_hit(KEY_A)) options_cycle(s_opt_selected);
    if (key_hit(KEY_START) || key_hit(KEY_B)) {
        save_write();
        menu_open(SCREEN_MAIN_MENU);
    }
}

/* ══ STORY MODE ═══════════════════════════════════════════════════════════
 * Android only. Four screens: the opening speech, the level map you fly a
 * little ship around, Mr Chubbs' docked shop, and the level result card. */
#ifdef PLATFORM_HOST

/* ── Intro speech ─────────────────────────────────────────────────────────
 * The opening cinematic: 14 pages of two lines each, typed out one character
 * at a time over the starfield with Story Mode's own track (story_mode.mp3)
 * underneath.  A tap fills the current page instantly; a second tap turns
 * the page. Pages NEVER advance on their own. The SKIP target in the corner
 * drops straight into the level map.
 *
 * It plays once, on the very first launch of the campaign: menu_go_back(),
 * SKIP and the final page all call story_mark_intro_seen(), which sets
 * g_story.intro_seen in the save. */

/* One character every N frames (the host ticks at 90Hz, so ~45 chars/s). */
#define INTRO_TYPE_FRAMES 2

static int s_intro_page = 0;
static int s_intro_chars = 0;    /* plain characters revealed on this page */
static int s_intro_hold = 0;     /* pause once a page finishes typing */
static int s_intro_tick = 0;     /* frame counter driving the typewriter */

/* Plain (marker-free) character count of a page: both lines together. */
static int intro_page_len(int page) {
    if (page < 0 || page >= STORY_INTRO_PAGES) return 0;
    return story_intro_len(g_story_intro[page][0]) +
           story_intro_len(g_story_intro[page][1]);
}

static void intro_reset(void) {
    s_intro_page = 0;
    s_intro_chars = 0;
    s_intro_hold = 0;
    s_intro_tick = 0;
}

/* The SKIP target: a real button in the bottom-right corner. */
static int intro_skip_x(void) { return SCREEN_WIDTH - 58; }
static int intro_skip_y(void) { return SCREEN_HEIGHT - 22; }
#define INTRO_SKIP_W 50
#define INTRO_SKIP_H 16

static void intro_finish(void) {
    story_mark_intro_seen();
    menu_open(SCREEN_STORY_MAP);
}

/* Advance to the next page, or finish and drop into the map. */
static void intro_advance(void) {
    if (s_intro_chars < intro_page_len(s_intro_page)) {
        /* Tapping mid-type reveals the rest of the page instantly. */
        s_intro_chars = intro_page_len(s_intro_page);
        s_intro_hold = 0;
        return;
    }
    s_intro_page++;
    s_intro_chars = 0;
    s_intro_hold = 0;
    s_intro_tick = 0;
    if (s_intro_page >= STORY_INTRO_PAGES) intro_finish();
}

static void update_story_intro(void) {
    int len = intro_page_len(s_intro_page);
    if (s_intro_chars < len) {
        /* Typewriter: a frame counter meters the reveal so the speed does not
         * depend on how long the page is. */
        if (++s_intro_tick >= INTRO_TYPE_FRAMES) {
            s_intro_tick = 0;
            s_intro_chars++;
        }
    }

    int tx, ty;
    if (consume_tap(&tx, &ty)) {
        if (in_rect(tx, ty, intro_skip_x() - 4, intro_skip_y() - 4,
                    INTRO_SKIP_W + 8, INTRO_SKIP_H + 8)) {
            intro_finish();
            return;
        }
        intro_advance();
        return;
    }
    if (key_hit(KEY_A) || key_hit(KEY_START)) { intro_advance(); return; }
    if (key_hit(KEY_B) || key_hit(KEY_SELECT)) intro_finish();
}

/* Draw a marked-up story line, centered, one character at a time.
 * `shown` is how many of this line's plain characters have been typed. */
static void intro_draw_line(int y, const char* src, int shown, u8 color) {
    char text[STORY_INTRO_LINE_MAX];
    u8   style[STORY_INTRO_LINE_MAX];
    int n = story_intro_markup(src, text, style, STORY_INTRO_LINE_MAX);
    if (shown < n) n = shown;
    if (n <= 0) return;

    /* Centre on the FULL line so the text does not crawl sideways as it
     * types - it grows out from a fixed left edge instead. */
    int full = story_intro_len(src);
    if (full > STORY_INTRO_LINE_MAX - 1) full = STORY_INTRO_LINE_MAX - 1;
    int x = (SCREEN_WIDTH - (full * 6 - 1)) / 2;
    if (x < 2) x = 2;

    for (int i = 0; i < n; i++) {
        if (style[i] == STORY_MK_BOLD) {
            /* Bold: the glyph plus a one-pixel offset copy. */
            gfx_draw_char(x + 1, y, text[i], color);
            gfx_draw_char(x, y, text[i], PAL_TEXT_WHITE);
        } else if (style[i] == STORY_MK_FAINT) {
            gfx_draw_char(x, y, text[i], 18);   /* dim grey */
        } else {
            gfx_draw_char(x, y, text[i], color);
        }
        x += 6;
    }
}

static void render_story_intro(void) {
    starfield_draw_base(0, 0);
    starfield_draw_stars(0, 0);

    if (s_intro_page >= STORY_INTRO_PAGES) return;

    const char* a = g_story_intro[s_intro_page][0];
    const char* b = g_story_intro[s_intro_page][1];
    int alen = story_intro_len(a);
    int shown = s_intro_chars;

    /* Upper-middle of the screen: the starfield keeps the top and the
     * bottom rows are left to the page counter and the SKIP prompt. */
    intro_draw_line(56, a, shown, PAL_TEXT_WHITE);
    intro_draw_line(70, b, shown - alen, PAL_TEXT_CYAN);

    /* Blinking "more" caret once the page has finished typing. */
    if (s_intro_chars >= intro_page_len(s_intro_page) && ((s_anim_frame >> 4) & 1))
        gfx_draw_text(SCREEN_WIDTH / 2 - 3, 84, ">", PAL_TEXT_GOLD);

    char pbuf[16];
    siprintf(pbuf, "%d / %d", s_intro_page + 1, STORY_INTRO_PAGES);
    gfx_draw_text_centered(0, 110, SCREEN_WIDTH, pbuf, 17);
    gfx_draw_text(8, SCREEN_HEIGHT - 14, "TAP TO CONTINUE", PAL_TEXT_WHITE);
    gfx_draw_button(intro_skip_x(), intro_skip_y(), INTRO_SKIP_W, INTRO_SKIP_H,
                    "SKIP", false);
}

/* ── Level map ────────────────────────────────────────────────────────────
 * One sector page at a time: ten nodes on a gentle wave, and a little ship
 * you fly between them. Simpler than Mario's overworld - the ship glides to
 * whichever node you pick and A launches the level. */

static int   s_map_sector = 0;      /* page being viewed */
static int   s_map_cursor = 1;      /* level under the ship (1..70) */
static float s_map_ship_x = 0;      /* smooth ship position, game px */
static float s_map_ship_y = 0;
static int   s_map_msg_timer = 0;
static char  s_map_msg[40];

/* Declared with the shop below; the map needs to reset the "fly on" mode
 * when the player walks into the dock from the map rather than a clear. */
static int s_shopz_fly_on;

#define MAP_NODE_R 5

static int map_node_x(int idx_in_sector) {
    int span = SCREEN_WIDTH - 44;
    return 22 + (span * idx_in_sector) / (STORY_SECTOR_LEVELS - 1);
}

static int map_node_y(int idx_in_sector) {
    /* A gentle zig-zag so the path reads as a route, not a ruler. */
    static const int wave[STORY_SECTOR_LEVELS] = { 0, -12, -18, -8, 4, 12, 6, -6, -14, 0 };
    return 102 + wave[idx_in_sector];
}

static void map_set_msg(const char* m) {
    strncpy(s_map_msg, m, sizeof(s_map_msg) - 1);
    s_map_msg[sizeof(s_map_msg) - 1] = '\0';
    s_map_msg_timer = 120;
}

/* Where Mr Chubbs is relative to the player's progress. He catches up on
 * the clear of every fifth level (4, 9, 14, ...) and each dock is spent the
 * moment you leave it, so the map can only ever report, never open. */
/* The next dock the player will actually fly into: the earliest one that is
 * both unspent and still ahead of them (not already cleared). */
static int map_next_dock(void) {
    for (int lv = 1; lv <= STORY_LEVEL_COUNT; lv++)
        if (story_shop_can_open(lv) && !story_is_cleared(lv)) return lv;
    return story_shop_next_dock(0);
}

static const char* map_chubbs_hint(void) {
    static char buf[40];
    int next = map_next_dock();
    if (next <= 0) return "MR CHUBBS HAS NO STOCK LEFT";
    siprintf(buf, "HE DOCKS WHEN LV %d IS CLEAR", next);
    return buf;
}

static void map_snap_ship(void) {
    int i = (s_map_cursor - 1) % STORY_SECTOR_LEVELS;
    s_map_ship_x = (float)map_node_x(i);
    s_map_ship_y = (float)(map_node_y(i) - 12);
}

static void map_focus_current(void) {
    s_map_cursor = story_current_level();
    if (s_map_cursor < 1) s_map_cursor = 1;
    s_map_sector = story_sector_of(s_map_cursor);
    map_snap_ship();
}

static void map_page_sector(int delta) {
    int next = s_map_sector + delta;
    if (next < 0 || next >= STORY_SECTOR_COUNT) return;
    int first = next * STORY_SECTOR_LEVELS + 1;
    if (first > story_highest_unlocked()) {
        map_set_msg("KINGDOM LOCKED - CLEAR THE PATH");
        return;
    }
    s_map_sector = next;
    /* Paging is also navigation: put the cursor in the kingdom being shown,
     * rather than leaving the ship and level card stranded on the old page. */
    int last = first + STORY_SECTOR_LEVELS - 1;
    int frontier = story_highest_unlocked();
    s_map_cursor = (delta > 0 && frontier < last) ? frontier : first;
    if (s_map_cursor < first) s_map_cursor = first;
    if (s_map_cursor > last) s_map_cursor = last;
    map_snap_ship();
    starfield_set_theme(story_theme_for_sector(s_map_sector));
}

static int map_chubbs_chip_x(void) { return SCREEN_WIDTH - 104; }

static void map_move_cursor(int delta) {
    int want = s_map_cursor + delta;
    if (want < 1) want = 1;
    if (want > STORY_LEVEL_COUNT) want = STORY_LEVEL_COUNT;
    /* You may inspect any unlocked level, and peek one past the frontier. */
    if (want > story_highest_unlocked()) {
        map_set_msg("LOCKED - CLEAR THE PATH FIRST");
        return;
    }
    s_map_cursor = want;
    int old_sector = s_map_sector;
    s_map_sector = story_sector_of(s_map_cursor);
    if (s_map_sector != old_sector)
        starfield_set_theme(story_theme_for_sector(s_map_sector));
}

static void map_launch(void) {
    if (!story_is_unlocked(s_map_cursor)) { map_set_msg("LOCKED"); return; }
    /* A wrecked ship is in the yard: nothing flies until the repairs are
     * done.  The countdown is real time, so it keeps running with the game
     * closed. */
    if (story_is_grounded()) {
        char rbuf[16];
        story_format_repair(rbuf, sizeof(rbuf));
        siprintf(s_map_msg, "SHIP IN REPAIR - %s LEFT", rbuf);
        s_map_msg_timer = 120;
        return;
    }
    if (story_lives() <= 0) { map_set_msg("NO LIVES - SEE MR CHUBBS"); return; }
    story_set_current_level(s_map_cursor);
    game_story_set_level(s_map_cursor);
    game_set_mode(GAME_MODE_STORY);
    game_start();
    s_current_screen = SCREEN_PLAYING;
}

static void update_story_map(void) {
    if (s_map_msg_timer > 0) s_map_msg_timer--;

    /* Glide the ship toward its node. */
    int i = (s_map_cursor - 1) % STORY_SECTOR_LEVELS;
    float tx_f = (float)map_node_x(i);
    float ty_f = (float)(map_node_y(i) - 12);
    s_map_ship_x += (tx_f - s_map_ship_x) * 0.25f;
    s_map_ship_y += (ty_f - s_map_ship_y) * 0.25f;

    int tx, ty;
    if (consume_tap(&tx, &ty)) {
        /* Sector arrows get first claim on their hit areas. The old right
         * arrow sat underneath the Chubbs chip, making forward paging
         * impossible by touch even though backward paging worked. */
        if (in_rect(tx, ty, 4, 20, 24, 22)) { map_page_sector(-1); return; }
        if (in_rect(tx, ty, SCREEN_WIDTH - 28, 20, 24, 22)) { map_page_sector(1); return; }
        /* Chubbs is a status chip, not a second route into the shop. */
        if (in_rect(tx, ty, map_chubbs_chip_x(), 18, 76, 22)) {
            map_set_msg(map_chubbs_hint());
            return;
        }
        /* LAUNCH button. */
        if (in_rect(tx, ty, (SCREEN_WIDTH - 96) / 2, SCREEN_HEIGHT - 26, 96, 20)) {
            map_launch();
            return;
        }
        /* Tap a node to select it (and tap again to launch). */
        for (int n = 0; n < STORY_SECTOR_LEVELS; n++) {
            int lv = s_map_sector * STORY_SECTOR_LEVELS + n + 1;
            int nx = map_node_x(n), ny = map_node_y(n);
            if (in_rect(tx, ty, nx - 10, ny - 10, 20, 20)) {
                if (!story_is_unlocked(lv)) { map_set_msg("LOCKED"); return; }
                if (s_map_cursor == lv) map_launch();
                else { s_map_cursor = lv; }
                return;
            }
        }
        return;
    }

    if (key_hit(KEY_LEFT))  map_move_cursor(-1);
    if (key_hit(KEY_RIGHT)) map_move_cursor(1);
    if (key_hit(KEY_UP))    map_move_cursor(-STORY_SECTOR_LEVELS);
    if (key_hit(KEY_DOWN))  map_move_cursor(STORY_SECTOR_LEVELS);
    if (key_hit(KEY_L)) map_page_sector(-1);
    if (key_hit(KEY_R)) map_page_sector(1);
    if (key_hit(KEY_A) || key_hit(KEY_START)) map_launch();
    if (key_hit(KEY_SELECT)) map_set_msg(map_chubbs_hint());
    if (key_hit(KEY_B)) menu_open(SCREEN_MAIN_MENU);
}

static void render_story_map(void) {
    starfield_draw_base(0, 0);
    starfield_draw_stars(0, 0);

    /* Header: sector, Chubbcoin, lives. */
    gfx_draw_text(6, 4, story_sector_name(s_map_sector), PAL_TEXT_CYAN);
    char buf[40];
    siprintf(buf, "%lu CHUBBCOIN", (unsigned long)story_chubbcoin());
    gfx_draw_text(SCREEN_WIDTH - 6 - (int)strlen(buf) * 6, 4, buf, PAL_TEXT_GOLD);
    gfx_fill_rect(4, 14, SCREEN_WIDTH - 8, 1, 20);

    for (int l = 0; l < story_lives() && l < STORY_MAX_LIVES; l++)
        gfx_draw_char(6 + l * 7, 18, '^', PAL_TEXT_GREEN);

    /* Sector paging arrows. */
    if (s_map_sector > 0) gfx_draw_text(8, 26, "<", PAL_TEXT_WHITE);
    if (s_map_sector < STORY_SECTOR_COUNT - 1 &&
        story_highest_unlocked() > (s_map_sector + 1) * STORY_SECTOR_LEVELS)
        gfx_draw_text(SCREEN_WIDTH - 20, 26, ">", PAL_TEXT_WHITE);
    siprintf(buf, "SECTOR %d/%d", s_map_sector + 1, STORY_SECTOR_COUNT);
    gfx_draw_text_centered(0, 24, SCREEN_WIDTH, buf, 17);

    /* Mr Chubbs' status chip. He docks on the clear of every fifth level and
     * only once, so this reports where he is - it is not a way in. */
    {
        int next_dock = map_next_dock();
        bool boss_dock = (next_dock > 0) && story_boss_dock(next_dock + 1);
        int chip_x = map_chubbs_chip_x();
        gfx_draw_glass_card(chip_x, 18, 76, 22,
                            boss_dock ? PAL_TEXT_RED : 20, 14);
        gfx_draw_text_centered(chip_x, 21, 76, "MR CHUBBS", PAL_TEXT_GOLD);
        if (next_dock <= 0) {
            gfx_draw_text_centered(chip_x, 30, 76, "GONE", 18);
        } else {
            siprintf(buf, "DOCKS LV %d", next_dock);
            gfx_draw_text_centered(chip_x, 30, 76, buf,
                                   boss_dock ? PAL_TEXT_RED : PAL_TEXT_WHITE);
        }
    }

    /* The path: draw connecting lines first, then the nodes on top. */
    for (int n = 0; n < STORY_SECTOR_LEVELS - 1; n++) {
        int x0 = map_node_x(n), y0 = map_node_y(n);
        int x1 = map_node_x(n + 1), y1 = map_node_y(n + 1);
        int lv_next = s_map_sector * STORY_SECTOR_LEVELS + n + 2;
        u8 col = story_is_unlocked(lv_next) ? PAL_TEXT_CYAN : 18;
        int steps = 10;
        for (int t = 1; t < steps; t++) {
            int px = x0 + (x1 - x0) * t / steps;
            int py = y0 + (y1 - y0) * t / steps;
            gfx_fill_rect(px, py, 2, 2, col);
        }
    }

    for (int n = 0; n < STORY_SECTOR_LEVELS; n++) {
        int lv = s_map_sector * STORY_SECTOR_LEVELS + n + 1;
        int nx = map_node_x(n), ny = map_node_y(n);
        bool boss = (story_boss_for_level(lv) >= 0);
        bool unlocked = story_is_unlocked(lv);
        bool cleared = story_is_cleared(lv);
        int r = boss ? MAP_NODE_R + 2 : MAP_NODE_R;

        u8 fill = !unlocked ? 18 : (cleared ? PAL_TEXT_GREEN : (boss ? PAL_TEXT_RED : PAL_TEXT_GOLD));
        gfx_fill_rect(nx - r, ny - r, r * 2, r * 2, fill);
        gfx_draw_rect(nx - r - 1, ny - r - 1, r * 2 + 2, r * 2 + 2,
                      unlocked ? PAL_TEXT_WHITE : 20);
        if (cleared) gfx_draw_char(nx - 2, ny - 3, '*', PAL_SPACE_BLACK);
        else if (!unlocked) gfx_draw_char(nx - 2, ny - 3, '-', PAL_TEXT_WHITE);

        /* Only the every-fifth-level docks get a pip, and it greys out once
         * that dock has been used - he does not come back for it. */
        if (story_shop_at(lv)) {
            bool spent = !story_shop_can_open(lv);
            bool pre_boss = story_boss_dock(lv + 1);
            gfx_draw_char(nx - 2, ny + r + 2, pre_boss ? '+' : '$',
                          spent ? (u8)18 : (pre_boss ? PAL_TEXT_GREEN : PAL_TEXT_GOLD));
        }
    }

    /* The player's little ship, hovering over the selected node. */
    int sx = (int)s_map_ship_x;
    int sy = (int)s_map_ship_y + ((s_anim_frame >> 4) & 1);
    gfx_draw_ship_styled(sx - 10, sy - 10, g_settings.accent_index, s_anim_frame, g_settings.ship_index);

    /* Selected level info card. */
    const StoryLevel* L = &g_story_levels[s_map_cursor - 1];
    int card_w = SCREEN_WIDTH - 24;
    gfx_draw_glass_card(12, 44, card_w, 34, PAL_BTN_BORDER, 14);
    siprintf(buf, "LEVEL %d", s_map_cursor);
    gfx_draw_text(18, 47, buf, PAL_TEXT_WHITE);
    gfx_draw_text(18, 57, L->name, PAL_TEXT_CYAN);

    const char* goal;
    switch (L->objective) {
        case OBJ_HUNT:    goal = "HUNT THE FIGHTERS"; break;
        case OBJ_SURVIVE: goal = "SURVIVE THE FIELD"; break;
        case OBJ_BIGGAME: goal = "CRACK THE BIG ONES"; break;
        case OBJ_TIMED:   goal = "CLEAR IT ON THE CLOCK"; break;
        case OBJ_PUZZLE:
            goal = (L->modifier == MOD_PZ_SALVO)  ? "PUZZLE: LIMITED AMMO" :
                   (L->modifier == MOD_PZ_SIGNAL) ? "PUZZLE: SIGNAL HUNT" :
                                                    "PUZZLE: GUNS OFFLINE";
            break;
        case OBJ_BOSS:    goal = story_boss_name(story_boss_for_level(s_map_cursor)); break;
        default:          goal = "CLEAR EVERYTHING"; break;
    }
    gfx_draw_text(SCREEN_WIDTH - 18 - (int)strlen(goal) * 6, 57, goal,
                  L->objective == OBJ_BOSS ? PAL_TEXT_RED : PAL_TEXT_GOLD);
    siprintf(buf, "PAYS %d CHUBBCOIN", story_is_cleared(s_map_cursor) ? L->reward / 2 : L->reward);
    gfx_draw_text(SCREEN_WIDTH - 18 - (int)strlen(buf) * 6, 47, buf, PAL_TEXT_GOLD);
    /* The level's own line of story, so the map reads as a narrative - and
     * its twist, so you can see at a glance that no two levels are alike. */
    {
        const char* modn = story_modifier_name(L->modifier);
        if (modn[0]) {
            /* Clip the story line so it stops before the twist tag instead
             * of drawing straight through it. */
            int mod_x = SCREEN_WIDTH - 18 - (int)strlen(modn) * 6;
            int room = (mod_x - 6 - 18) / 6;
            if (room < 0) room = 0;
            if (room > 39) room = 39;
            char brief[40];
            strncpy(brief, L->brief1, room);
            brief[room] = '\0';
            gfx_draw_text(18, 67, brief, PAL_TEXT_WHITE);
            gfx_draw_text(mod_x, 67, modn, PAL_TEXT_VIOLET);
        } else {
            gfx_draw_text_centered(12, 67, card_w, L->brief1, PAL_TEXT_WHITE);
        }
    }

    /* Launch button - or the repair countdown when the ship is grounded. */
    int bx = (SCREEN_WIDTH - 96) / 2;
    if (story_is_grounded()) {
        char rbuf[16];
        story_format_repair(rbuf, sizeof(rbuf));
        siprintf(buf, "REPAIR %s", rbuf);
        gfx_draw_glass_card(bx, SCREEN_HEIGHT - 26, 96, 20, PAL_TEXT_RED, PAL_BTN_BG);
        gfx_draw_text_centered(bx, SCREEN_HEIGHT - 19, 96, buf, PAL_TEXT_RED);
    } else {
        gfx_draw_button(bx, SCREEN_HEIGHT - 26, 96, 20,
                        story_is_cleared(s_map_cursor) ? "REPLAY" : "LAUNCH", true);
    }

    if (s_map_msg_timer > 0)
        gfx_draw_text_centered(0, SCREEN_HEIGHT - 38, SCREEN_WIDTH, s_map_msg, PAL_TEXT_RED);
}

/* ── Mr Chubbs' Shop ──────────────────────────────────────────────────────
 * His little ship, docked. You never see him or any other Chubb - just the
 * hull, the radio text and the prices. */

static int s_shopz_sel = 0;
static int s_shopz_scroll = 0;
#define SHOPZ_VISIBLE_ROWS 4
static int s_shopz_msg_timer = 0;
static u8  s_shopz_msg_col = PAL_TEXT_GOLD;
static char s_shopz_msg[40];
static int s_shopz_gift = 0;        /* frames left on the "free life" banner */
static int s_shopz_flash = 0;       /* purchase flash on the selected row */
/* Where LEAVE goes: 0 = back to the map, 1 = straight into the next level. */
static int s_shopz_next_level = 1;
/* 0 = travelling STOCK (chubbcoin), 1 = GARAGE (free loadout swap). */
static int s_shopz_tab = 0;
static int s_gar_cat = 0;       /* 0 lasers, 1 weapons, 2 paints, 3 trails */
static int s_gar_sel = 0;
static int s_gar_scroll = 0;
#define GAR_CAT_COUNT 4
/* Garage cosmetics: a slice of the hangar, not the million-credit rainbows. */
static const u8 s_gar_paints[] = { 0, 1, 2, 3, 4 };
static const u8 s_gar_trails[] = { 0, 1, 2, 3 };
#define GAR_PAINT_COUNT ((int)(sizeof(s_gar_paints) / sizeof(s_gar_paints[0])))
#define GAR_TRAIL_COUNT ((int)(sizeof(s_gar_trails) / sizeof(s_gar_trails[0])))

static void shopz_msg(const char* m, u8 col, int timer) {
    strncpy(s_shopz_msg, m, sizeof(s_shopz_msg) - 1);
    s_shopz_msg[sizeof(s_shopz_msg) - 1] = '\0';
    s_shopz_msg_col = col;
    s_shopz_msg_timer = timer;
}

static int garage_cat_count(int cat) {
    switch (cat) {
        case 0: return NUM_LASERS;
        case 1: return NUM_RIGS;
        case 2: return GAR_PAINT_COUNT;
        case 3: return GAR_TRAIL_COUNT;
        default: return 0;
    }
}

static int garage_item_id(int cat, int idx) {
    if (idx < 0) return 0;
    switch (cat) {
        case 2: return (idx < GAR_PAINT_COUNT) ? s_gar_paints[idx] : 0;
        case 3: return (idx < GAR_TRAIL_COUNT) ? s_gar_trails[idx] : 0;
        default: return idx;
    }
}

static bool garage_owned(int cat, int idx) {
    int id = garage_item_id(cat, idx);
    switch (cat) {
        case 0: return shop_is_laser_owned(id);
        case 1: return shop_is_rig_owned((WeaponRig)id);
        case 2: return shop_is_accent_owned(id);
        case 3: return shop_is_trail_owned(id);
        default: return false;
    }
}

static bool garage_equipped(int cat, int idx) {
    int id = garage_item_id(cat, idx);
    switch (cat) {
        case 0: return g_settings.laser_index == id;
        case 1: return g_settings.weapon_rig == id;
        case 2: return g_settings.accent_index == id;
        case 3: return g_settings.trail_index == id;
        default: return false;
    }
}

static const char* garage_item_name(int cat, int idx) {
    int id = garage_item_id(cat, idx);
    switch (cat) {
        case 0: return gfx_get_laser_name(id);
        case 1: return gfx_get_weapon_name((WeaponRig)id);
        case 2: return gfx_get_accent_name(id);
        case 3: return gfx_get_trail_name(id);
        default: return "";
    }
}

static const char* garage_item_desc(int cat, int idx) {
    int id = garage_item_id(cat, idx);
    switch (cat) {
        case 0: return gfx_get_laser_desc(id);
        case 1: return gfx_get_weapon_desc((WeaponRig)id);
        case 2: return gfx_get_accent_desc(id);
        case 3: return gfx_get_trail_desc(id);
        default: return "";
    }
}

static const char* garage_cat_name(int cat) {
    switch (cat) {
        case 0: return "LASERS";
        case 1: return "WEAPONS";
        case 2: return "PAINTS";
        case 3: return "TRAILS";
        default: return "";
    }
}

static int garage_icon_kind(int cat) {
    switch (cat) {
        case 0: return SSTOCK_LASER;
        case 1: return SSTOCK_WEAPON;
        case 2: return SSTOCK_PAINT;
        case 3: return SSTOCK_TRAIL;
        default: return SSTOCK_EMPTY;
    }
}

static void garage_ensure_visible(void) {
    int count = garage_cat_count(s_gar_cat);
    if (s_gar_sel < 0) s_gar_sel = 0;
    if (s_gar_sel >= count) s_gar_sel = count > 0 ? count - 1 : 0;
    if (s_gar_sel < s_gar_scroll) s_gar_scroll = s_gar_sel;
    if (s_gar_sel >= s_gar_scroll + SHOPZ_VISIBLE_ROWS)
        s_gar_scroll = s_gar_sel - SHOPZ_VISIBLE_ROWS + 1;
    if (s_gar_scroll < 0) s_gar_scroll = 0;
}

static void garage_set_cat(int cat) {
    if (cat < 0) cat = GAR_CAT_COUNT - 1;
    if (cat >= GAR_CAT_COUNT) cat = 0;
    s_gar_cat = cat;
    s_gar_sel = 0;
    s_gar_scroll = 0;
}

static void garage_equip(void) {
    if (!garage_owned(s_gar_cat, s_gar_sel)) {
        shopz_msg("NOT IN THE BAY", PAL_TEXT_RED, 110);
        return;
    }
    int id = garage_item_id(s_gar_cat, s_gar_sel);
    switch (s_gar_cat) {
        case 0: shop_equip_laser(id); break;
        case 1: shop_equip_rig((WeaponRig)id); break;
        case 2: shop_equip_accent(id); break;
        case 3: shop_equip_trail(id); break;
        default: break;
    }
    shopz_msg("EQUIPPED.", PAL_TEXT_GREEN, 90);
    s_shopz_flash = 20;
    audio_play_sfx(SFX_PICKUP);
}

static int shopz_tab_w(void) { return (SCREEN_WIDTH - 12) / 2; }

static void story_shop_reset_ui(void) {
    s_shopz_sel = 0;
    s_shopz_scroll = 0;
    s_shopz_tab = 0;
    s_gar_cat = 0;
    s_gar_sel = 0;
    s_gar_scroll = 0;
    s_shopz_msg_timer = 0;
    s_shopz_flash = 0;
    /* Boss docks: Mr Chubbs talks you up and hands over a life, free. */
    if (story_shop_take_gift()) {
        s_shopz_gift = 240;
        audio_play_sfx(SFX_PICKUP);
    } else {
        s_shopz_gift = 0;
    }
}

/* ── Layout ───────────────────────────────────────────────────────────────
 * The dock now uses the SAME geometry as the arcade Upgrade Hangar rather
 * than four squashed full-width strips: a header line, a stock list on the
 * left and a detail panel on the right, with the action button under the
 * detail card.  Every constant below is derived from the hangar's so the two
 * shops line up pixel for pixel.
 *
 *   hangar: tabs at y=17 h=13, panels at y=31 h=112, rows 21 px, msg y=148
 *   dock:   radio at y=17 h=13, panels at y=31 h=112, rows 21 px, msg y=148
 *
 * Mr Chubbs has no tab strip (one scrolling shelf), so the row that the
 * hangar spends on tabs becomes his radio line - the layout below it is
 * identical. */
#define SHOPZ_ROW_H     LIST_ROW_H   /* 21: the hangar's row pitch */
#define SHOPZ_PANEL_Y   31
#define SHOPZ_PANEL_H   112
#define SHOPZ_ROW_TOP   34           /* == hangar_list_top() */

static int shopz_list_w(void)  { return shop_list_width(); }              /* 116 + slack */
static int shopz_right_x(void) { return 4 + shopz_list_w() + 2; }
static int shopz_right_w(void) { return SCREEN_WIDTH - 4 - shopz_right_x(); }

static int shopz_row_x(void) { return 6; }
static int shopz_row_w(void) { return shopz_list_w() - 4; }
static int shopz_row_y(int i) { return SHOPZ_ROW_TOP + i * SHOPZ_ROW_H; }

/* BUY sits inside the right panel where the hangar puts its BUY / EQUIP
 * button.  The four stock rows only reach y=118, so LEAVE gets the empty
 * space at the bottom of the LIST panel - no button ever overlaps text. */
#define SHOPZ_BTN_H 21
static int shopz_btn_y(void)   { return 115; }
static int shopz_leave_y(void) { return 120; }
static int shopz_leave_h(void) { return 19; }

static void shopz_leave(void) {
    /* Leaving spends the dock. He undocks and flies off; there is no second
     * look at this shelf until he catches up again five levels later. */
    story_shop_close();
    if (s_shopz_fly_on && story_is_unlocked(s_shopz_next_level) && story_lives() > 0) {
        story_set_current_level(s_shopz_next_level);
        game_story_set_level(s_shopz_next_level);
        game_set_mode(GAME_MODE_STORY);
        game_start();
        s_current_screen = SCREEN_PLAYING;
        return;
    }
    menu_open(SCREEN_STORY_MAP);
}

static void shopz_buy(int i) {
    int r = story_shop_buy(i);
    if (r == 0) {
        shopz_msg("SOLD. NO REFUNDS.", PAL_TEXT_GREEN, 110);
        s_shopz_flash = 20;
        audio_play_sfx(SFX_PICKUP);
    } else if (r == 1) {
        shopz_msg("NOT ENOUGH CHUBBCOIN", PAL_TEXT_RED, 110);
    } else {
        shopz_msg("SOLD OUT OR ALREADY YOURS", 18, 110);
    }
}

static void update_story_shop(void) {
    if (s_shopz_msg_timer > 0) s_shopz_msg_timer--;
    if (s_shopz_gift > 0) s_shopz_gift--;
    if (s_shopz_flash > 0) s_shopz_flash--;
    int tw = shopz_tab_w();
    int tx, ty;
    if (consume_tap(&tx, &ty)) {
        /* STOCK / GARAGE tabs sit where the hangar keeps its category strip. */
        if (ty >= 17 && ty < 31) {
            int tab = (tx >= 6 + tw) ? 1 : 0;
            if (tab != s_shopz_tab) {
                s_shopz_tab = tab;
                s_shopz_flash = 0;
            }
            return;
        }
        if (s_shopz_tab == 1) {
            int count = garage_cat_count(s_gar_cat);
            for (int row = 0; row < SHOPZ_VISIBLE_ROWS; row++) {
                int i = s_gar_scroll + row;
                if (i >= count) break;
                if (in_rect(tx, ty, shopz_row_x(), shopz_row_y(row), shopz_row_w(), SHOPZ_ROW_H)) {
                    if (s_gar_sel == i) garage_equip();
                    else {
                        s_gar_sel = i; s_shopz_flash = 0;
                        if (row == SHOPZ_VISIBLE_ROWS - 1 &&
                            s_gar_scroll + SHOPZ_VISIBLE_ROWS < count) s_gar_scroll++;
                        else if (row == 0 && s_gar_scroll > 0) s_gar_scroll--;
                    }
                    return;
                }
            }
            if (in_rect(tx, ty, shopz_right_x() + 4, shopz_btn_y(),
                        shopz_right_w() - 8, SHOPZ_BTN_H)) { garage_equip(); return; }
            if (in_rect(tx, ty, shopz_row_x(), shopz_leave_y(),
                        shopz_row_w(), shopz_leave_h())) { shopz_leave(); return; }
            return;
        }
        for (int row = 0; row < SHOPZ_VISIBLE_ROWS; row++) {
            int i = s_shopz_scroll + row;
            if (i >= STORY_SHOP_SLOTS) break;
            if (in_rect(tx, ty, shopz_row_x(), shopz_row_y(row), shopz_row_w(), SHOPZ_ROW_H)) {
                /* Tap to select, tap the selected row again to buy - the
                 * same double-tap the hangar list uses. */
                if (s_shopz_sel == i) shopz_buy(i);
                else {
                    s_shopz_sel = i; s_shopz_flash = 0;
                    /* Edge rows act like a phone list: selecting the bottom
                     * or top item reveals more of the eight-item shelf. */
                    if (row == SHOPZ_VISIBLE_ROWS - 1 &&
                        s_shopz_scroll + SHOPZ_VISIBLE_ROWS < STORY_SHOP_SLOTS) s_shopz_scroll++;
                    else if (row == 0 && s_shopz_scroll > 0) s_shopz_scroll--;
                }
                return;
            }
        }
        if (in_rect(tx, ty, shopz_right_x() + 4, shopz_btn_y(),
                    shopz_right_w() - 8, SHOPZ_BTN_H)) { shopz_buy(s_shopz_sel); return; }
        if (in_rect(tx, ty, shopz_row_x(), shopz_leave_y(),
                    shopz_row_w(), shopz_leave_h())) { shopz_leave(); return; }
        return;
    }
    if (key_hit(KEY_SELECT)) {
        s_shopz_tab = 1 - s_shopz_tab;
        s_shopz_flash = 0;
        return;
    }
    if (s_shopz_tab == 1 && (key_hit(KEY_L) || key_hit(KEY_LEFT))) {
        garage_set_cat(s_gar_cat - 1);
        return;
    }
    if (s_shopz_tab == 1 && (key_hit(KEY_R) || key_hit(KEY_RIGHT))) {
        garage_set_cat(s_gar_cat + 1);
        return;
    }
    if (s_shopz_tab == 1) {
        int count = garage_cat_count(s_gar_cat);
        if (count < 1) count = 1;
        if (key_hit(KEY_UP)) {
            s_gar_sel = (s_gar_sel + count - 1) % count;
            garage_ensure_visible();
            s_shopz_flash = 0;
        }
        if (key_hit(KEY_DOWN)) {
            s_gar_sel = (s_gar_sel + 1) % count;
            garage_ensure_visible();
            s_shopz_flash = 0;
        }
        if (key_hit(KEY_A)) garage_equip();
        if (key_hit(KEY_B) || key_hit(KEY_START)) shopz_leave();
        return;
    }
    if (key_hit(KEY_UP)) {
        s_shopz_sel = (s_shopz_sel + STORY_SHOP_SLOTS - 1) % STORY_SHOP_SLOTS;
        if (s_shopz_sel < s_shopz_scroll) s_shopz_scroll = s_shopz_sel;
        if (s_shopz_sel == STORY_SHOP_SLOTS - 1) s_shopz_scroll = STORY_SHOP_SLOTS - SHOPZ_VISIBLE_ROWS;
        s_shopz_flash = 0;
    }
    if (key_hit(KEY_DOWN)) {
        s_shopz_sel = (s_shopz_sel + 1) % STORY_SHOP_SLOTS;
        if (s_shopz_sel >= s_shopz_scroll + SHOPZ_VISIBLE_ROWS)
            s_shopz_scroll = s_shopz_sel - SHOPZ_VISIBLE_ROWS + 1;
        if (s_shopz_sel == 0) s_shopz_scroll = 0;
        s_shopz_flash = 0;
    }
    if (key_hit(KEY_A)) shopz_buy(s_shopz_sel);
    if (key_hit(KEY_B) || key_hit(KEY_START)) shopz_leave();
}

/* A little stock icon per kind, so the shelf reads at a glance. */
static void shopz_draw_icon(int x, int y, int kind, bool dim) {
    u8 c = dim ? (u8)18 : PAL_TEXT_CYAN;
    switch (kind) {
        case SSTOCK_LIFE:                       /* heart-ish pip */
            gfx_fill_rect(x + 1, y + 2, 3, 3, dim ? (u8)18 : PAL_TEXT_GREEN);
            gfx_fill_rect(x + 5, y + 2, 3, 3, dim ? (u8)18 : PAL_TEXT_GREEN);
            gfx_fill_rect(x + 2, y + 5, 5, 3, dim ? (u8)18 : PAL_TEXT_GREEN);
            gfx_fill_rect(x + 3, y + 8, 3, 2, dim ? (u8)18 : PAL_TEXT_GREEN);
            break;
        case SSTOCK_WEAPON:                     /* barrel + muzzle */
            gfx_fill_rect(x + 3, y + 1, 3, 8, c);
            gfx_fill_rect(x + 1, y + 7, 7, 3, c);
            break;
        case SSTOCK_LASER:                      /* beam bolt */
            gfx_fill_rect(x + 4, y + 1, 2, 9, dim ? (u8)18 : PAL_TEXT_VIOLET);
            gfx_fill_rect(x + 3, y + 3, 4, 5, dim ? (u8)18 : PAL_TEXT_WHITE);
            break;
        case SSTOCK_PAINT:                      /* swatch */
            gfx_fill_rect(x + 1, y + 2, 8, 7, dim ? (u8)18 : PAL_TEXT_GOLD);
            gfx_draw_rect(x + 1, y + 2, 8, 7, PAL_TEXT_WHITE);
            break;
        case SSTOCK_UPGRADE:                    /* rising bars */
            gfx_fill_rect(x + 1, y + 7, 2, 3, c);
            gfx_fill_rect(x + 4, y + 4, 2, 6, c);
            gfx_fill_rect(x + 7, y + 1, 2, 9, c);
            break;
        case SSTOCK_TRAIL:                      /* exhaust streak */
            gfx_fill_rect(x + 1, y + 2, 7, 2, c);
            gfx_fill_rect(x + 3, y + 5, 6, 2, dim ? (u8)18 : PAL_TEXT_GOLD);
            gfx_fill_rect(x + 5, y + 8, 4, 2, c);
            break;
        case SSTOCK_SHIP:                       /* tiny winged hull */
            gfx_fill_rect(x + 3, y + 1, 4, 9, c);
            gfx_fill_rect(x + 1, y + 5, 8, 3, dim ? (u8)18 : PAL_TEXT_WHITE);
            break;
        default: break;
    }
}

/* Split `src` onto two lines of at most `cols` characters, breaking on a
 * space so the item blurbs sit inside the detail card instead of running off
 * the edge of it. Both outputs are always NUL-terminated. */
static void shopz_wrap(const char* src, int cols, char* l1, char* l2, int cap) {
    if (cols > cap - 1) cols = cap - 1;
    if (cols < 1) cols = 1;
    l1[0] = l2[0] = '\0';
    int len = (int)strlen(src);
    if (len <= cols) { strncpy(l1, src, cap - 1); l1[cap - 1] = '\0'; return; }

    /* Last space at or before the column limit; hard-split if there is none. */
    int brk = -1;
    for (int i = 0; i < len && i <= cols; i++) if (src[i] == ' ') brk = i;
    if (brk <= 0) brk = cols;

    int n1 = brk; if (n1 > cap - 1) n1 = cap - 1;
    memcpy(l1, src, n1); l1[n1] = '\0';

    const char* rest = src + brk;
    while (*rest == ' ') rest++;
    int n2 = (int)strlen(rest);
    if (n2 > cols) n2 = cols;
    if (n2 > cap - 1) n2 = cap - 1;
    memcpy(l2, rest, n2); l2[n2] = '\0';
}

/* One-word category for the detail card, mirroring the hangar's "Hull skin
 * finish" / "Primary ordnance" second line. */
static const char* shopz_slot_kind_line(int kind) {
    switch (kind) {
        case SSTOCK_LIFE:    return "Spare life";
        case SSTOCK_WEAPON:  return "Primary ordnance";
        case SSTOCK_LASER:   return "Laser crystal core";
        case SSTOCK_PAINT:   return "Hull skin finish";
        case SSTOCK_UPGRADE: return "Permanent stat upgrade";
        case SSTOCK_TRAIL:   return "Engine exhaust trail";
        case SSTOCK_SHIP:    return "Complete hull style";
        default:             return "";
    }
}

static void render_story_shop(void) {
    starfield_draw_base(0, 0);
    starfield_draw_stars(0, 0);

    char buf[48];
    int list_w = shopz_list_w();
    int right_x = shopz_right_x();
    int right_w = shopz_right_w();
    bool boss_dock = story_shop_is_boss_dock();

    /* ── Header: name on the left, the purse on the right ──
     * Same 15 px strip + rule the hangar uses. */
    gfx_draw_text(6, 4, "MR CHUBBS", PAL_TEXT_GOLD);
    gfx_draw_text(6 + 10 * 6, 4, "TRADING POST", 17);
    siprintf(buf, "%lu CHUBBCOIN", (unsigned long)story_chubbcoin());
    gfx_draw_text(SCREEN_WIDTH - 6 - (int)strlen(buf) * 6, 4, buf, PAL_TEXT_GOLD);
    gfx_fill_rect(4, 15, SCREEN_WIDTH - 8, 1, 20);

    /* ── STOCK / GARAGE tabs (same strip the hangar uses for categories) ── */
    {
        int tw = shopz_tab_w();
        for (int t = 0; t < 2; t++) {
            int tx = 4 + t * (tw + 4);
            bool on = (s_shopz_tab == t);
            gfx_draw_glass_card(tx, 17, tw, 13,
                                on ? PAL_TEXT_CYAN : 20,
                                on ? PAL_BTN_HOVER : PAL_BTN_BG);
            gfx_draw_text_centered(tx, 19, tw, t ? "GARAGE" : "STOCK",
                                   on ? PAL_TEXT_WHITE : PAL_TEXT_CYAN);
        }
    }

    /* ── The two panels ── */
    gfx_draw_glass_card(4, SHOPZ_PANEL_Y, list_w, SHOPZ_PANEL_H, PAL_BTN_BORDER, 14);
    gfx_draw_glass_card(right_x, SHOPZ_PANEL_Y, right_w, SHOPZ_PANEL_H, PAL_BTN_BORDER, 14);

    if (s_shopz_tab == 1) {
        int count = garage_cat_count(s_gar_cat);
        for (int row = 0; row < SHOPZ_VISIBLE_ROWS; row++) {
            int i = s_gar_scroll + row;
            if (i >= count) break;
            int y = shopz_row_y(row);
            int rx = shopz_row_x(), rw = shopz_row_w();
            bool sel = (s_gar_sel == i);
            bool own = garage_owned(s_gar_cat, i);
            bool eq = garage_equipped(s_gar_cat, i);
            u8 border = sel ? (((s_anim_frame >> 4) & 1) ? PAL_TEXT_WHITE : PAL_TEXT_CYAN) : (u8)20;
            u8 bg = (sel && s_shopz_flash > 0 && ((s_shopz_flash >> 2) & 1)) ? PAL_TEXT_GREEN
                  : (sel ? PAL_BTN_HOVER : PAL_BTN_BG);
            gfx_draw_glass_card(rx, y, rw, SHOPZ_ROW_H - 2, border, bg);
            shopz_draw_icon(rx + 4, y + 4, garage_icon_kind(s_gar_cat), !own);
            char name_buf[13] = {0};
            strncpy(name_buf, garage_item_name(s_gar_cat, i), 12);
            if (sel) {
                gfx_draw_char(rx + 15, y + 6, '>', PAL_TEXT_CYAN);
                gfx_draw_text(rx + 22, y + 6, name_buf, own ? PAL_TEXT_WHITE : (u8)18);
            } else {
                gfx_draw_text(rx + 16, y + 6, name_buf, own ? PAL_TEXT_CYAN : (u8)18);
            }
            const char* badge = eq ? "[EQ]" : (own ? "OWN" : "LOCK");
            u8 badge_col = eq ? PAL_TEXT_GREEN : (own ? PAL_TEXT_CYAN : (u8)18);
            gfx_draw_text(rx + rw - 5 - (int)strlen(badge) * 6, y + 6, badge, badge_col);
        }

        gfx_draw_glass_card(right_x + 2, SHOPZ_PANEL_Y + 2, right_w - 4, 35, 20, PAL_SPACE_BLACK);
        {
            int ship_x = right_x + (right_w - 20) / 2;
            int ship_y = SHOPZ_PANEL_Y + 10;
            int preview_paint = (s_gar_cat == 2) ? garage_item_id(2, s_gar_sel) : g_settings.accent_index;
            int preview_trail = (s_gar_cat == 3) ? garage_item_id(3, s_gar_sel) : g_settings.trail_index;
            if (preview_paint < 0 || preview_paint >= NUM_ACCENTS) preview_paint = 1;
            gfx_draw_ship_styled(ship_x, ship_y, preview_paint, s_anim_frame, g_settings.ship_index);
            draw_preview_engine_trail(ship_x, ship_y, preview_trail);
            if (s_gar_cat == 0 || s_gar_cat == 1) {
                int preview_laser = (s_gar_cat == 0) ? garage_item_id(0, s_gar_sel) : g_settings.laser_index;
                int travel = (s_anim_frame * 2) % 18;
                int ly = ship_y - 2 - travel;
                if (ly >= SHOPZ_PANEL_Y + 4)
                    gfx_draw_laser(ship_x + 10, ly, false, preview_laser, s_anim_frame, false);
            }
        }
        gfx_fill_rect(right_x + 4, 70, right_w - 8, 1, 20);

        {
            bool own = garage_owned(s_gar_cat, s_gar_sel);
            bool eq = garage_equipped(s_gar_cat, s_gar_sel);
            gfx_draw_text_centered(right_x, 73, right_w, garage_item_name(s_gar_cat, s_gar_sel),
                                   own ? PAL_TEXT_WHITE : (u8)18);
            gfx_draw_text_centered(right_x, 82, right_w,
                                   eq ? "[EQUIPPED]" : (own ? "[OWNED]" : "[LOCKED]"),
                                   eq ? PAL_TEXT_GREEN : (own ? PAL_TEXT_CYAN : PAL_TEXT_RED));
            char l1[24], l2[24];
            shopz_wrap(garage_item_desc(s_gar_cat, s_gar_sel), (right_w - 8) / 6, l1, l2,
                       (int)sizeof(l1));
            gfx_draw_text_centered(right_x, 91, right_w, l1, own ? PAL_TEXT_CYAN : (u8)18);
            gfx_draw_text_centered(right_x, 99, right_w, l2, own ? PAL_TEXT_CYAN : (u8)18);
            siprintf(buf, "BAY  %s", garage_cat_name(s_gar_cat));
            gfx_draw_text_centered(right_x, 107, right_w, buf, 17);
        }

        if (s_shopz_fly_on) {
            siprintf(buf, "FLY ON  LV %d", s_shopz_next_level);
            gfx_draw_button(shopz_row_x(), shopz_leave_y(), shopz_row_w(),
                            shopz_leave_h(), buf, false);
        } else {
            gfx_draw_button(shopz_row_x(), shopz_leave_y(), shopz_row_w(),
                            shopz_leave_h(), "LEAVE DOCK", false);
        }
        {
            bool own = garage_owned(s_gar_cat, s_gar_sel);
            bool eq = garage_equipped(s_gar_cat, s_gar_sel);
            int btn_x = right_x + 4;
            int btn_w = right_w - 8;
            if (eq) {
                gfx_draw_glass_card(btn_x, shopz_btn_y(), btn_w, SHOPZ_BTN_H, PAL_TEXT_GREEN, PAL_BTN_HOVER);
                gfx_draw_text_centered(btn_x, shopz_btn_y() + 7, btn_w, "EQUIPPED", PAL_TEXT_GREEN);
            } else if (own) {
                gfx_draw_glass_card(btn_x, shopz_btn_y(), btn_w, SHOPZ_BTN_H, PAL_TEXT_CYAN, PAL_BTN_HOVER);
                gfx_draw_text_centered(btn_x, shopz_btn_y() + 7, btn_w, "[A] EQUIP", PAL_TEXT_WHITE);
            } else {
                gfx_draw_glass_card(btn_x, shopz_btn_y(), btn_w, SHOPZ_BTN_H, 20, PAL_BTN_BG);
                gfx_draw_text_centered(btn_x, shopz_btn_y() + 7, btn_w, "LOCKED", 18);
            }
        }
        if (s_shopz_msg_timer > 0) {
            int w = (int)strlen(s_shopz_msg) * 6 + 12;
            int x = (SCREEN_WIDTH - w) / 2;
            gfx_draw_glass_card(x, 146, w, 12, s_shopz_msg_col, 15);
            gfx_draw_text_centered(x, 148, w, s_shopz_msg, s_shopz_msg_col);
        } else {
            gfx_draw_text_centered(0, 148, SCREEN_WIDTH,
                                   "L/R BAY   TAP EQUIP   NO COINS", 20);
        }
        return;
    }

    /* ── Left panel: four visible rows from the larger travelling shelf ── */
    for (int row = 0; row < SHOPZ_VISIBLE_ROWS; row++) {
        int i = s_shopz_scroll + row;
        if (i >= STORY_SHOP_SLOTS) break;
        const StoryStockItem* it = story_shop_slot(i);
        int y = shopz_row_y(row);
        int rx = shopz_row_x(), rw = shopz_row_w();
        bool sel = (s_shopz_sel == i);
        bool gone = (it->qty == 0);
        bool afford = story_chubbcoin() >= it->price;

        u8 border = gone ? (u8)20
                  : (sel ? (((s_anim_frame >> 4) & 1) ? PAL_TEXT_WHITE : PAL_TEXT_CYAN) : (u8)20);
        u8 bg = (sel && s_shopz_flash > 0 && ((s_shopz_flash >> 2) & 1)) ? PAL_TEXT_GREEN
              : (sel ? PAL_BTN_HOVER : PAL_BTN_BG);
        gfx_draw_glass_card(rx, y, rw, SHOPZ_ROW_H - 2, border, bg);   /* 19 tall, 2 px gutter */

        shopz_draw_icon(rx + 4, y + 4, it->kind, gone);

        /* Name, then the badge hard-right - exactly the hangar's row shape. */
        char name_buf[13] = {0};
        strncpy(name_buf, story_shop_slot_name(i), 12);
        if (sel) {
            gfx_draw_char(rx + 15, y + 6, '>', PAL_TEXT_CYAN);
            gfx_draw_text(rx + 22, y + 6, name_buf, gone ? (u8)18 : PAL_TEXT_WHITE);
        } else {
            gfx_draw_text(rx + 16, y + 6, name_buf, gone ? (u8)18 : PAL_TEXT_CYAN);
        }

        char badge[12];
        u8 badge_col;
        if (gone) { strncpy(badge, "SOLD", sizeof(badge) - 1); badge[sizeof(badge)-1] = '\0'; badge_col = 18; }
        else { siprintf(badge, "%d", (int)it->price); badge_col = afford ? PAL_TEXT_GOLD : PAL_TEXT_RED; }
        gfx_draw_text(rx + rw - 5 - (int)strlen(badge) * 6, y + 6, badge, badge_col);
    }

    /* ── Right panel: the docked hull, then the detail card ── */
    gfx_draw_glass_card(right_x + 2, SHOPZ_PANEL_Y + 2, right_w - 4, 35, 20, PAL_SPACE_BLACK);
    {
        /* His ship, centred in the preview chamber. No faces, ever - just a
         * hull, a blinking dock light and a voice. */
        int cx = right_x + right_w / 2;
        int sy = SHOPZ_PANEL_Y + 10;
        u8 hull = boss_dock ? PAL_TEXT_RED : PAL_TEXT_GOLD;
        gfx_fill_rect(cx - 11, sy + 5, 22, 9, hull);
        gfx_fill_rect(cx - 7, sy + 2, 13, 4, PAL_TEXT_WHITE);
        gfx_fill_rect(cx - 15, sy + 8, 4, 5, PAL_TEXT_CYAN);
        gfx_fill_rect(cx + 11, sy + 8, 4, 5, PAL_TEXT_CYAN);
        if ((s_anim_frame >> 4) & 1)
            gfx_fill_rect(cx - 1, sy, 3, 2, PAL_TEXT_GREEN);
    }
    gfx_fill_rect(right_x + 4, 70, right_w - 8, 1, 20);

    /* Detail rows sit on the hangar's baselines: name 80, status 89,
     * desc 98, kind 107 - four clear lines instead of two crammed ones. */
    {
        const StoryStockItem* it = story_shop_slot(s_shopz_sel);
        bool gone = (it->qty == 0);
        bool afford = story_chubbcoin() >= it->price;

        gfx_draw_text_centered(right_x, 73, right_w, story_shop_slot_name(s_shopz_sel),
                               gone ? (u8)18 : PAL_TEXT_WHITE);
        if (gone) {
            gfx_draw_text_centered(right_x, 82, right_w, "[SOLD OUT]", 18);
        } else if (it->kind == SSTOCK_LIFE) {
            siprintf(buf, "COST: %d   x%d LEFT", (int)it->price, (int)it->qty);
            gfx_draw_text_centered(right_x, 82, right_w, buf, afford ? PAL_TEXT_GOLD : PAL_TEXT_RED);
        } else {
            siprintf(buf, "COST: %d CHUBBCOIN", (int)it->price);
            gfx_draw_text_centered(right_x, 82, right_w, buf, afford ? PAL_TEXT_GOLD : PAL_TEXT_RED);
        }

        /* The item's own blurb, word-wrapped onto the two lines the panel
         * has room for - the old single line ran straight off the card. */
        {
            char l1[24], l2[24];
            shopz_wrap(story_shop_slot_desc(s_shopz_sel), (right_w - 8) / 6, l1, l2,
                       (int)sizeof(l1));
            gfx_draw_text_centered(right_x, 91, right_w, l1, gone ? (u8)18 : PAL_TEXT_CYAN);
            gfx_draw_text_centered(right_x, 99, right_w, l2, gone ? (u8)18 : PAL_TEXT_CYAN);
        }
        /* What kind of thing it is - or, if he could not sell it last time,
         * that it has been sitting on the shelf since the previous dock. */
        gfx_draw_text_centered(right_x, 107, right_w,
                               story_shop_slot_held_over(s_shopz_sel)
                                   ? "Held over from last dock"
                                   : shopz_slot_kind_line(it->kind), 17);
    }

    /* ── LEAVE, then the BUY button on the hangar's own baseline ── */
    {
        const StoryStockItem* it = story_shop_slot(s_shopz_sel);
        bool gone = (it->qty == 0);
        bool afford = story_chubbcoin() >= it->price;
        int btn_x = right_x + 4;
        int btn_w = right_w - 8;

        /* LEAVE lives under the shelf, in the list panel's spare space. */
        if (s_shopz_fly_on) {
            siprintf(buf, "FLY ON  LV %d", s_shopz_next_level);
            gfx_draw_button(shopz_row_x(), shopz_leave_y(), shopz_row_w(),
                            shopz_leave_h(), buf, false);
        } else {
            gfx_draw_button(shopz_row_x(), shopz_leave_y(), shopz_row_w(),
                            shopz_leave_h(), "LEAVE DOCK", false);
        }

        if (gone) {
            gfx_draw_glass_card(btn_x, shopz_btn_y(), btn_w, SHOPZ_BTN_H, 20, PAL_BTN_BG);
            gfx_draw_text_centered(btn_x, shopz_btn_y() + 7, btn_w, "SOLD OUT", 18);
        } else if (afford) {
            gfx_draw_glass_card(btn_x, shopz_btn_y(), btn_w, SHOPZ_BTN_H, PAL_TEXT_GOLD, PAL_BTN_HOVER);
            siprintf(buf, "[A] BUY %d", (int)it->price);
            gfx_draw_text_centered(btn_x, shopz_btn_y() + 7, btn_w, buf, PAL_TEXT_GOLD);
        } else {
            gfx_draw_glass_card(btn_x, shopz_btn_y(), btn_w, SHOPZ_BTN_H, PAL_TEXT_RED, PAL_BTN_BG);
            siprintf(buf, "NEED %d", (int)it->price);
            gfx_draw_text_centered(btn_x, shopz_btn_y() + 7, btn_w, buf, PAL_TEXT_RED);
        }
    }

    /* ── Bottom line (the hangar's y=148 message row) ──
     * The free-life gift wins it, then purchase messages, then his second
     * line of radio - and the standing warning that this is a one-time dock. */
    if (s_shopz_gift > 0) {
        siprintf(buf, "+1 LIFE ON THE HOUSE   LIVES %d", story_lives());
        gfx_draw_text_centered(0, 148, SCREEN_WIDTH, buf,
                               ((s_shopz_gift >> 3) & 1) ? PAL_TEXT_GREEN : PAL_TEXT_WHITE);
    } else if (s_shopz_msg_timer > 0) {
        int w = (int)strlen(s_shopz_msg) * 6 + 12;
        int x = (SCREEN_WIDTH - w) / 2;
        gfx_draw_glass_card(x, 146, w, 12, s_shopz_msg_col, 15);
        gfx_draw_text_centered(x, 148, w, s_shopz_msg, s_shopz_msg_col);
    } else {
        gfx_draw_text_centered(0, 148, SCREEN_WIDTH, story_shop_line2(), 20);
    }
}

/* ── Level result card ────────────────────────────────────────────────── */
static int  s_result_sel = 0;
static int  s_result_level = 1;
static int  s_result_win = 0;
static int  s_result_earned = 0;
static int  s_result_resume = 1;
static bool s_result_lost_run = false;
/* What the wreck actually cost: levels taken back, chubbcoin clawed back and
 * the repair countdown. Filled in when the failure card opens. */
static int  s_result_relocked = 0;
static int  s_result_bill = 0;
static bool s_result_finale = false;
static int  s_result_auto = 0;      /* frames until the card moves on itself */
/* Mr Chubbs only catches up every fifth level, so a clear either ends at his
 * dock or flies straight on. Decided once when the card opens. */
static bool s_result_dock = false;
static bool s_result_fly_on = false;
static int  s_result_next = 1;

/* Called by menu_update when a story level ends. */
static void story_enter_result(void) {
    s_result_level = game_story_level();
    s_result_win = (game_story_outcome() == 1);
    s_result_earned = game_story_earned();
    s_result_sel = 0;
    s_result_lost_run = false;
    s_result_finale = false;
    s_result_auto = 0;

    s_result_dock = false;
    s_result_fly_on = false;
    s_result_next = 1;

    if (s_result_win) {
        s_result_resume = story_current_level();
        s_result_next = s_result_level + 1;
        if (s_result_next > STORY_LEVEL_COUNT) s_result_next = STORY_LEVEL_COUNT;
        /* Does Mr Chubbs catch up on this clear? Only every fifth level, and
         * only if that dock has not already been spent. */
        s_result_dock = story_shop_can_open(s_result_level);
        s_result_fly_on = (s_result_next != s_result_level) &&
                          story_is_unlocked(s_result_next) && story_lives() > 0 &&
                          !story_is_grounded();
        if (s_result_level >= STORY_LEVEL_COUNT) s_result_finale = true;
        else s_result_auto = 200;   /* ~2.2s to read the card, then move on */
    } else {
        int before = story_lives();
        s_result_resume = story_lose_life();
        s_result_lost_run = (before <= 1);   /* the pool ran dry */
        s_result_relocked = s_result_lost_run ? story_last_relocked() : 0;
        s_result_bill = s_result_lost_run ? story_last_repair_bill() : 0;
    }
    save_write();
    menu_open(SCREEN_STORY_RESULT);
}

/* Open Mr Chubbs' dock for the level just cleared, if he is actually there:
 * he only catches up every fifth level and each dock is a single visit.
 * Returns false when there is no dock, so the caller falls back to the map.
 * When the next level is unlocked, LEAVE becomes FLY ON. */
static bool story_open_dock(int cleared_level) {
    if (!story_shop_can_open(cleared_level)) return false;
    int next = cleared_level + 1;
    if (next > STORY_LEVEL_COUNT) next = STORY_LEVEL_COUNT;
    s_shopz_next_level = next;
    s_shopz_fly_on = (next != cleared_level) && story_is_unlocked(next) &&
                     story_lives() > 0 && !story_is_grounded();
    story_shop_open(cleared_level);
    menu_open(SCREEN_STORY_SHOP);
    return true;
}

static void result_activate(int idx) {
    if (s_result_win) {
        /* 0 = into the dock when Mr Chubbs is actually there (every fifth
         * level, once each); otherwise straight on to the next level. */
        if (idx == 0) {
            if (s_result_dock && story_open_dock(s_result_level)) return;
            if (s_result_fly_on) {
                story_set_current_level(s_result_next);
                game_story_set_level(s_result_next);
                game_set_mode(GAME_MODE_STORY);
                game_start();
                s_current_screen = SCREEN_PLAYING;
                return;
            }
        }
        menu_open(SCREEN_STORY_MAP);
        return;
    }
    /* No retry while the ship is in the yard - it is not flyable. */
    if (idx == 0 && !s_result_lost_run && story_lives() > 0 && !story_is_grounded()) {
        story_set_current_level(s_result_resume);
        game_story_set_level(s_result_resume);
        game_set_mode(GAME_MODE_STORY);
        game_start();
        s_current_screen = SCREEN_PLAYING;
        return;
    }
    menu_open(SCREEN_STORY_MAP);
}

static int result_option_count(void) { return 2; }

static void update_story_result(void) {
    /* The card holds for a beat so the payout is readable, then it moves on
     * by itself: into Mr Chubbs' dock on the every-fifth-level clears, and
     * back to the map otherwise. */
    if (s_result_win && s_result_auto > 0 && !s_result_finale) {
        if (--s_result_auto == 0) {
            if (!(s_result_dock && story_open_dock(s_result_level)))
                menu_open(SCREEN_STORY_MAP);
            return;
        }
    }
    int tx, ty;
    if (consume_tap(&tx, &ty)) {
        s_result_auto = 0;
        for (int i = 0; i < result_option_count(); i++) {
            if (in_rect(tx, ty, (SCREEN_WIDTH - 120) / 2, 104 + i * 20, 120, 18)) {
                result_activate(i);
                return;
            }
        }
        return;
    }
    int n = result_option_count();
    if (key_hit(KEY_UP) || key_hit(KEY_DOWN)) s_result_auto = 0;
    if (key_hit(KEY_UP)) s_result_sel = (s_result_sel + n - 1) % n;
    if (key_hit(KEY_DOWN)) s_result_sel = (s_result_sel + 1) % n;
    if (key_hit(KEY_A) || key_hit(KEY_START)) result_activate(s_result_sel);
    if (key_hit(KEY_B)) menu_open(SCREEN_STORY_MAP);
}

static void render_story_result(void) {
    starfield_draw_base(0, 0);
    starfield_draw_stars(0, 0);

    char buf[48];
    int card_w = SCREEN_WIDTH - 40;
    gfx_draw_glass_card(20, 24, card_w, 72, s_result_win ? PAL_TEXT_GOLD : PAL_TEXT_RED, 15);

    if (s_result_finale) {
        gfx_draw_text_centered(20, 30, card_w, "THE CUBE QUEEN FALLS", PAL_TEXT_GOLD);
        gfx_draw_text_centered(20, 42, card_w, "Revenge, finally.", PAL_TEXT_WHITE);
        gfx_draw_text_centered(20, 54, card_w, "Everything is unlocked.", PAL_TEXT_CYAN);
        siprintf(buf, "+%d CHUBBCOIN", s_result_earned);
        gfx_draw_text_centered(20, 68, card_w, buf, PAL_TEXT_GOLD);
        gfx_draw_text_centered(20, 82, card_w, "SHOP, MULTIPLAYER, ALL MODES", 17);
    } else if (s_result_win) {
        siprintf(buf, "LEVEL %d CLEARED", s_result_level);
        gfx_draw_text_centered(20, 32, card_w, buf, PAL_TEXT_GOLD);
        gfx_draw_text_centered(20, 46, card_w, g_story_levels[s_result_level - 1].name, PAL_TEXT_WHITE);
        siprintf(buf, "+%d CHUBBCOIN", s_result_earned);
        gfx_draw_text_centered(20, 58, card_w, buf, PAL_TEXT_GOLD);
        /* Where the money came from: the payout is earned, so show the work.
         * Only the bonuses that actually paid are listed. */
        {
            char bd[48];
            int n = 0;
            bd[0] = '\0';
            if (story_pay_speed() > 0)     { siprintf(bd, "FAST +%d", story_pay_speed()); n++; }
            if (story_pay_combat() > 0) {
                char t[24]; siprintf(t, "%sKILLS +%d", n ? "  " : "", story_pay_combat());
                strncat(bd, t, sizeof(bd) - strlen(bd) - 1); n++;
            }
            if (story_pay_precision() > 0) {
                char t[24]; siprintf(t, "%sAIM +%d", n ? "  " : "", story_pay_precision());
                strncat(bd, t, sizeof(bd) - strlen(bd) - 1); n++;
            }
            if (story_pay_clean() > 0) {
                char t[24]; siprintf(t, "%sCLEAN +%d", n ? "  " : "", story_pay_clean());
                strncat(bd, t, sizeof(bd) - strlen(bd) - 1); n++;
            }
            if (n) gfx_draw_text_centered(20, 68, card_w, bd, PAL_TEXT_CYAN);
        }
        siprintf(buf, "LIVES %d", story_lives());
        gfx_draw_text_centered(20, 80, card_w, buf, PAL_TEXT_GREEN);
    } else {
        gfx_draw_text_centered(20, 32, card_w, "SHIP DOWN", PAL_TEXT_RED);
        if (s_result_lost_run) {
            /* The wreck is spelled out: what got re-locked, what it cost and
             * how long the yard has the ship for. */
            gfx_draw_text_centered(20, 44, card_w, "SHIP WRECKED", PAL_TEXT_WHITE);
            if (s_result_relocked > 0) {
                siprintf(buf, "LAST %d LEVELS RELOCKED", s_result_relocked);
                gfx_draw_text_centered(20, 55, card_w, buf, PAL_TEXT_RED);
            } else {
                gfx_draw_text_centered(20, 55, card_w, "NOTHING LEFT TO RELOCK", PAL_TEXT_RED);
            }
            if (s_result_bill > 0) {
                siprintf(buf, "-%d CHUBBCOIN", s_result_bill);
                gfx_draw_text_centered(20, 65, card_w, buf, PAL_TEXT_GOLD);
            }
            char rbuf[16];
            story_format_repair(rbuf, sizeof(rbuf));
            siprintf(buf, "REPAIRS: %s LEFT", rbuf);
            gfx_draw_text_centered(20, 78, card_w, buf, PAL_TEXT_CYAN);
        } else {
            siprintf(buf, "LIVES LEFT %d", story_lives());
            gfx_draw_text_centered(20, 50, card_w, buf, PAL_TEXT_GOLD);
            gfx_draw_text_centered(20, 66, card_w, "TRY IT AGAIN", PAL_TEXT_WHITE);
        }
    }

    const char* opts[2];
    char opt0[24];
    if (s_result_win) {
        if (s_result_dock) {
            opts[0] = "MR CHUBBS";
        } else if (s_result_fly_on) {
            siprintf(opt0, "FLY ON  LV %d", s_result_next);
            opts[0] = opt0;
        } else {
            opts[0] = "MAP";
        }
        opts[1] = "MAP";
    } else {
        opts[0] = (!s_result_lost_run && story_lives() > 0 && !story_is_grounded())
                  ? "RETRY" : "MAP";
        opts[1] = "MAP";
    }
    for (int i = 0; i < 2; i++)
        gfx_draw_button((SCREEN_WIDTH - 120) / 2, 104 + i * 20, 120, 18, opts[i], s_result_sel == i);
}

#endif /* PLATFORM_HOST */

/* ── PLAY tab ─────────────────────────────────────────────────────────────
 * Android: STORY sits at the top and is always playable; WAVES and ENDLESS
 * are campaign rewards and stay padlocked until Story Mode is finished
 * (or "LET ME BE FREE" is used). Overdrive is retired on Android.
 * GBA: the original arcade trio. */
#define PLAY_CARD_COUNT 3

#ifdef PLATFORM_HOST
static const char* s_mode_titles[PLAY_CARD_COUNT] = { "STORY", "WAVES", "ENDLESS" };
static const char* s_mode_lines[PLAY_CARD_COUNT] = {
    "70 levels. Jack RK's revenge.",
    "Clear waves. Classic run.",
    "No waves. Endless hunters."
};
static const GameMode s_mode_ids[PLAY_CARD_COUNT] = {
    GAME_MODE_STORY, GAME_MODE_WAVES, GAME_MODE_ENDLESS
};
/* Card 0 (Story) is never locked; the arcade cards are. */
static bool play_card_locked(int i) { return i > 0 && !story_gate_open(); }
#else
static const char* s_mode_titles[PLAY_CARD_COUNT] = { "WAVES", "ENDLESS", "OVERDRIVE" };
static const char* s_mode_lines[PLAY_CARD_COUNT] = {
    "Clear waves. Classic run.",
    "No waves. Random hunters.",
    "90s score rush. Max chaos."
};
static const GameMode s_mode_ids[PLAY_CARD_COUNT] = {
    GAME_MODE_WAVES, GAME_MODE_ENDLESS, GAME_MODE_OVERDRIVE
};
static bool play_card_locked(int i) { (void)i; return false; }
#endif

static int mode_card_y(int i) { return 28 + i * 36; }

/* Activate a PLAY card: story opens the campaign, arcade cards launch. */
static void play_card_activate(int i) {
    if (i < 0 || i >= PLAY_CARD_COUNT) return;
    if (play_card_locked(i)) {
#ifdef PLATFORM_HOST
        story_locked_msg();
#endif
        return;
    }
#ifdef PLATFORM_HOST
    if (s_mode_ids[i] == GAME_MODE_STORY) { enter_story_mode(); return; }
#endif
    launch_mode(s_mode_ids[i]);
}

/* Cards span the screen minus a small margin so they fit the 240px GBA
 * display as well as the wider Android surface. */
static int mode_card_w(void) {
    int w = SCREEN_WIDTH - 20;
    if (w > 220) w = 220;
    return w;
}

/* ── MULTIPLAYER tab (2-player co-op Quick Match) ─────────────────────
 * Android: drives EOS matchmaking directly - FIND MATCH / CANCEL / LAUNCH,
 * live status, player count and role. GBA: informational page. */
#ifdef PLATFORM_HOST
#define MP_ACT_FIND   0
#define MP_ACT_CANCEL 1
#define MP_ACT_LAUNCH 2

typedef struct { int y; const char* label; int act; } MpButton;

static const char* mp_headline(int status) {
    switch (status) {
        case EOS_ONLINE_CONFIG_REQUIRED:    return "ONLINE SETUP NEEDED";
        case EOS_ONLINE_INITIALIZING:       return "STARTING ONLINE...";
        case EOS_ONLINE_SIGNING_IN:         return "SIGNING IN TO EPIC...";
        case EOS_ONLINE_READY:              return "READY TO FIGHT TOGETHER";
        case EOS_ONLINE_MATCHMAKING:        return "SEARCHING FOR A PILOT...";
        case EOS_ONLINE_WAITING_FOR_PLAYER: return "LOBBY OPEN - WAITING";
        case EOS_ONLINE_MATCHED:            return "CO-PILOT FOUND!";
        case EOS_ONLINE_ERROR:              return "ONLINE ERROR";
        default:                            return "ONLINE CO-OP";
    }
}

/* Current button row(s) for the live matchmaking state. Returns count. */
static int mp_buttons(MpButton* out) {
    int status = eos_online_status();
    int n = 0;
    switch (status) {
        case EOS_ONLINE_READY:
            out[n++] = (MpButton){ 116, "FIND MATCH  (2P)", MP_ACT_FIND };
            break;
        case EOS_ONLINE_MATCHMAKING:
            out[n++] = (MpButton){ 116, "CANCEL SEARCH", MP_ACT_CANCEL };
            break;
        case EOS_ONLINE_WAITING_FOR_PLAYER:
            out[n++] = (MpButton){ 116, "CLOSE LOBBY", MP_ACT_CANCEL };
            break;
        case EOS_ONLINE_MATCHED:
            if (eos_online_is_host()) {
                out[n++] = (MpButton){ 100, "LAUNCH CO-OP GAME", MP_ACT_LAUNCH };
                out[n++] = (MpButton){ 134, "LEAVE LOBBY", MP_ACT_CANCEL };
            } else {
                out[n++] = (MpButton){ 116, "LEAVE LOBBY", MP_ACT_CANCEL };
            }
            break;
        case EOS_ONLINE_ERROR:
            out[n++] = (MpButton){ 116, "RESET ONLINE", MP_ACT_CANCEL };
            break;
        default:
            break;
    }
    return n;
}

static void mp_activate(int act) {
    switch (act) {
        case MP_ACT_FIND:
            if (!eos_online_quick_match()) {
                shop_set_msg("NOT READY YET!", PAL_TEXT_RED);
            }
            break;
        case MP_ACT_CANCEL:
            eos_online_cancel_match();
            break;
        case MP_ACT_LAUNCH:
            request_play(); /* host picks the mode; guest follows via GAME_START */
            break;
    }
    menu_static_invalidate();
}

static void update_multiplayer(void) {
    if (s_shop_msg_timer > 0) s_shop_msg_timer--;
    MpButton btns[2];
    int count = mp_buttons(btns);
    if (s_mp_selected >= count) s_mp_selected = count > 0 ? count - 1 : 0;

    int tx, ty;
    if (consume_tap(&tx, &ty)) {
        int bw = 160;
        int bx = (SCREEN_WIDTH - bw) / 2;
        for (int i = 0; i < count; i++) {
            if (in_rect(tx, ty, bx, btns[i].y, bw, 16)) {
                s_mp_selected = i;
                mp_activate(btns[i].act);
                return;
            }
        }
    }
    if (key_hit(KEY_UP) && count > 0) s_mp_selected = (s_mp_selected + count - 1) % count;
    if (key_hit(KEY_DOWN) && count > 0) s_mp_selected = (s_mp_selected + 1) % count;
    if (key_hit(KEY_A) && count > 0) mp_activate(btns[s_mp_selected].act);
    if (key_hit(KEY_B)) menu_open(SCREEN_MAIN_MENU);
}

static void render_multiplayer(void) {
    gfx_draw_text(10, 6, "2P CO-OP MULTIPLAYER", PAL_TEXT_CYAN);
    gfx_fill_rect(10, 16, SCREEN_WIDTH - 20, 1, 20);

    int status = eos_online_status();

    /* Live status card */
    gfx_draw_glass_card(8, 20, SCREEN_WIDTH - 16, 27, PAL_BTN_BORDER, 14);
    gfx_draw_text(14, 24, mp_headline(status),
                  status == EOS_ONLINE_MATCHED ? PAL_TEXT_GREEN :
                  (status == EOS_ONLINE_ERROR ? PAL_TEXT_RED : PAL_TEXT_WHITE));
    {
        const char* detail = eos_online_status_text();
        char dbuf[44];
        int i = 0;
        while (detail[i] && i < (int)sizeof(dbuf) - 1) { dbuf[i] = detail[i]; i++; }
        dbuf[i] = '\0';
        gfx_draw_text(14, 34, dbuf, PAL_TEXT_CYAN);
    }

    /* Players / role card */
    gfx_draw_glass_card(8, 51, SCREEN_WIDTH - 16, 17, PAL_BTN_BORDER, 14);
    {
        char pbuf[40];
        int members = eos_online_member_count();
        const char* role = (status == EOS_ONLINE_MATCHED || status == EOS_ONLINE_WAITING_FOR_PLAYER)
            ? (eos_online_is_host() ? "HOST" : "GUEST") : "-";
        siprintf(pbuf, "PLAYERS: %d/2   ROLE: %s", members, role);
        gfx_draw_text(14, 56, pbuf, PAL_TEXT_GOLD);
    }

    /* How co-op works */
    gfx_draw_glass_card(8, 72, SCREEN_WIDTH - 16, 40, PAL_BTN_BORDER, 14);
    if (status == EOS_ONLINE_CONFIG_REQUIRED) {
        gfx_draw_text(14, 76, "Needs Epic credentials in", PAL_TEXT_WHITE);
        gfx_draw_text(14, 85, "android/eos.properties -", PAL_TEXT_WHITE);
        gfx_draw_text(14, 94, "see android/README.md and", PAL_TEXT_CYAN);
        gfx_draw_text(14, 103, "rebuild to enable Quick Match", PAL_TEXT_CYAN);
    } else if (status == EOS_ONLINE_MATCHED && !eos_online_is_host()) {
        gfx_draw_text(14, 76, "CONNECTED TO HOST!", PAL_TEXT_GREEN);
        gfx_draw_text(14, 85, "Waiting for the host to", PAL_TEXT_WHITE);
        gfx_draw_text(14, 94, "launch the game. You join", PAL_TEXT_WHITE);
        gfx_draw_text(14, 103, "automatically - hang on!", PAL_TEXT_CYAN);
    } else {
        gfx_draw_text(14, 76, "HOST RUNS WORLD, GUEST JOINS", PAL_TEXT_WHITE);
        gfx_draw_text(14, 85, "LASERS + EFFECTS SYNC 2-WAYS", PAL_TEXT_WHITE);
        gfx_draw_text(14, 94, "DIE AND YOU SPECTATE UNTIL", PAL_TEXT_CYAN);
        gfx_draw_text(14, 103, "BOTH SHIPS ARE DOWN!", PAL_TEXT_CYAN);
    }

    /* Action button(s) */
    MpButton btns[2];
    int count = mp_buttons(btns);
    int bw = 160;
    int bx = (SCREEN_WIDTH - bw) / 2;
    for (int i = 0; i < count; i++) {
        gfx_draw_button(bx, btns[i].y, bw, 15, btns[i].label, i == s_mp_selected);
    }
    if (count == 0 && status != EOS_ONLINE_CONFIG_REQUIRED) {
        gfx_draw_text_centered(0, 120, SCREEN_WIDTH, "CONNECTING", PAL_TEXT_CYAN);
        /* animated dots */
        int dots = (s_anim_frame >> 4) % 4;
        for (int d = 0; d < dots; d++) gfx_draw_char(SCREEN_WIDTH / 2 + 30 + d * 6, 120, '.', PAL_TEXT_CYAN);
    }
    gfx_draw_text_centered(0, 150, SCREEN_WIDTH, "B / BACK: return", 17);
}
#else
static void update_multiplayer(void) {
    int tx, ty;
    if (consume_tap(&tx, &ty)) { menu_open(SCREEN_MAIN_MENU); return; }
    if (key_hit(KEY_A) || key_hit(KEY_B) || key_hit(KEY_START)) menu_open(SCREEN_MAIN_MENU);
}

static void render_multiplayer(void) {
    gfx_draw_text(10, 6, "2P CO-OP MULTIPLAYER", PAL_TEXT_CYAN);
    gfx_fill_rect(10, 16, SCREEN_WIDTH - 20, 1, 20);
    gfx_draw_glass_card(8, 24, SCREEN_WIDTH - 16, 88, PAL_BTN_BORDER, 14);
    gfx_draw_text(14, 30, "ONLINE CO-OP LIVES IN THE", PAL_TEXT_WHITE);
    gfx_draw_text(14, 39, "ANDROID EDITION OF THIS", PAL_TEXT_WHITE);
    gfx_draw_text(14, 48, "GAME (EPIC QUICK MATCH).", PAL_TEXT_WHITE);
    gfx_draw_text(14, 62, "HOW IT PLAYS THERE:", PAL_TEXT_CYAN);
    gfx_draw_text(14, 71, "- HOST RUNS WORLD, GUEST JOINS", PAL_TEXT_WHITE);
    gfx_draw_text(14, 80, "- LASERS + EFFECTS SYNC BOTH WAYS", PAL_TEXT_WHITE);
    gfx_draw_text(14, 89, "- DIE AND YOU SPECTATE UNTIL", PAL_TEXT_WHITE);
    gfx_draw_text(14, 98, "  BOTH SHIPS ARE DOWN!", PAL_TEXT_WHITE);
    gfx_draw_text_centered(0, 146, SCREEN_WIDTH, "Press A or B to return", PAL_TEXT_WHITE);
}
#endif

static void update_mode_select(void) {
    const int count = PLAY_CARD_COUNT;
    if (s_shop_msg_timer > 0) s_shop_msg_timer--;
    int tx, ty;
    if (consume_tap(&tx, &ty)) {
        int card_w = mode_card_w();
        int card_x = (SCREEN_WIDTH - card_w) / 2;
        for (int i = 0; i < count; i++) {
            if (in_rect(tx, ty, card_x, mode_card_y(i), card_w, 32)) {
                s_menu_selected = i;
                play_card_activate(i);
                return;
            }
        }
#ifndef PLATFORM_HOST
        if (ty >= 144) {
            menu_open(SCREEN_MAIN_MENU);
            return;
        }
#endif
    }
    if (key_hit(KEY_UP)) s_menu_selected = (s_menu_selected + count - 1) % count;
    if (key_hit(KEY_DOWN)) s_menu_selected = (s_menu_selected + 1) % count;
    if (key_hit(KEY_A) || key_hit(KEY_START)) play_card_activate(s_menu_selected);
    if (key_hit(KEY_B)) menu_open(SCREEN_MAIN_MENU);
}

#ifdef PLATFORM_HOST
static bool coop_guest_active(void) { return coop_in_session() && !coop_is_host(); }
#else
static bool coop_guest_active(void) { return false; }
#endif

static void pause_activate(int index) {
    if (coop_guest_active()) {
        /* Guest has no authority: pause only resumes locally or leaves the
         * session (the host's world keeps running regardless). */
        switch (index) {
            case 0: s_current_screen = SCREEN_PLAYING; break;
#ifdef PLATFORM_HOST
            case 1: coop_leave_session(); save_write(); menu_open(SCREEN_MAIN_MENU); break;
#endif
            default: break;
        }
        return;
    }
    switch (index) {
        case 0: s_current_screen = SCREEN_PLAYING; break;
        case 1: save_write(); game_start(); s_current_screen = SCREEN_PLAYING; break;
        case 2: save_write(); menu_open(SCREEN_MAIN_MENU); break;
        default: break;
    }
}

static void update_paused(void) {
    const int count = coop_guest_active() ? 2 : 3;
    int tx, ty;
    if (consume_tap(&tx, &ty)) {
        int w = 110; int h = 88;
        int x = (SCREEN_WIDTH - w) / 2;
        int y = (SCREEN_HEIGHT - h) / 2;
        for (int i = 0; i < count; i++) {
            if (in_rect(tx, ty, x + 8, y + 18 + i * 18, w - 16, 17)) {
                s_menu_selected = i;
                pause_activate(i);
                return;
            }
        }
    }
    if (key_hit(KEY_UP)) s_menu_selected = (s_menu_selected + count - 1) % count;
    if (key_hit(KEY_DOWN)) s_menu_selected = (s_menu_selected + 1) % count;
    if (key_hit(KEY_START) || key_hit(KEY_B)) { s_current_screen = SCREEN_PLAYING; return; }
    if (key_hit(KEY_A)) pause_activate(s_menu_selected);
}

static void game_over_activate(int index) {
    if (coop_guest_active()) {
        /* Guest can't restart the shared run - only the host decides
         * whether a rematch happens. */
        menu_open(SCREEN_MAIN_MENU);
        return;
    }
    switch (index) {
        case 0: game_start(); s_current_screen = SCREEN_PLAYING; break;
        case 1: menu_open(SCREEN_HANGAR); break;
        case 2: menu_open(SCREEN_MAIN_MENU); break;
        default: break;
    }
}

static void update_game_over(void) {
    const int count = coop_guest_active() ? 1 : 3;
    int tx, ty;
    if (consume_tap(&tx, &ty)) {
        int w = 130; int h = 122;
        int x = (SCREEN_WIDTH - w) / 2;
        int y = (SCREEN_HEIGHT - h) / 2;
        for (int i = 0; i < count; i++) {
            if (in_rect(tx, ty, x + 8, y + 52 + i * 18, w - 16, 17)) {
                s_menu_selected = i;
                game_over_activate(i);
                return;
            }
        }
    }
    if (key_hit(KEY_UP)) s_menu_selected = (s_menu_selected + count - 1) % count;
    if (key_hit(KEY_DOWN)) s_menu_selected = (s_menu_selected + 1) % count;
    if (key_hit(KEY_A) || key_hit(KEY_START)) game_over_activate(s_menu_selected);
}

void menu_update(void) {
    s_anim_frame++;
    key_poll();
    switch (s_current_screen) {
        case SCREEN_MAIN_MENU: starfield_update(); update_main_menu(); break;
        case SCREEN_HANGAR:    starfield_update(); update_hangar(); break;
        case SCREEN_SETTINGS:  starfield_update(); update_upgrades(); break; // upgrades
        case SCREEN_CONTROLS:  starfield_update(); update_controls(); break;
        case SCREEN_OPTIONS:   starfield_update(); update_options(); break;
        case SCREEN_MODE_SELECT: starfield_update(); update_mode_select(); break;
        case SCREEN_MULTIPLAYER: starfield_update(); update_multiplayer(); break;
#ifdef PLATFORM_HOST
        case SCREEN_STORY_INTRO:  starfield_update(); update_story_intro(); break;
        case SCREEN_STORY_MAP:    starfield_update(); update_story_map(); break;
        case SCREEN_STORY_SHOP:   starfield_update(); update_story_shop(); break;
        case SCREEN_STORY_RESULT: starfield_update(); update_story_result(); break;
#endif
        case SCREEN_PLAYING:
#ifdef PLATFORM_HOST
            if (game_story_waiting_for_start()) {
                /* The story brief is a real pause, not a timed overlay. Keep
                 * the sky alive, but do not advance enemies, timers, or player
                 * invulnerability until a tap (or controller confirm). */
                starfield_update();
                int tx, ty;
                bool tapped = consume_tap(&tx, &ty);
                if (tapped || key_hit(KEY_A) || key_hit(KEY_START))
                    game_story_continue();
                break;
            }
#endif
            s_tap_pending = false;
            if (key_hit(KEY_START)) { s_current_screen = SCREEN_PAUSED; s_menu_selected = 0; }
            else {
                game_update();
#ifdef PLATFORM_HOST
                /* Story levels end on their own objective, not the arcade
                 * game-over screen: hand off to the result card instead. */
                if (game_get_mode() == GAME_MODE_STORY && game_story_outcome() != 0) {
                    story_enter_result();
                    break;
                }
#endif
                if (g_game.is_game_over) { s_current_screen = SCREEN_GAME_OVER; s_menu_selected = 0; }
            }
            break;
        case SCREEN_PAUSED: update_paused(); break;
        case SCREEN_GAME_OVER: update_game_over(); break;
    }
}

// ── Rendering helpers ──────────────────────────────────────────────────
static void draw_ship_preview_static(int card_x, int card_y, int card_w, int card_h) {
    gfx_draw_glass_card(card_x, card_y, card_w, card_h, PAL_BTN_BORDER, 14);
    gfx_draw_text(card_x + 6, card_y + 4, "SHIP PROFILE", PAL_TEXT_CYAN);
    gfx_draw_text(card_x + 6, card_y + 13, gfx_get_ship_style_name(g_settings.ship_index), PAL_TEXT_WHITE);
    gfx_fill_rect(card_x + 4, card_y + 45, card_w - 8, 1, 20);
    gfx_draw_text(card_x + 6, card_y + 49, "HULL", PAL_TEXT_CYAN);
    gfx_draw_text(card_x + 36, card_y + 49, gfx_get_ship_style_name(g_settings.ship_index), PAL_TEXT_WHITE);
    gfx_draw_text(card_x + 6, card_y + 58, "PAINT", PAL_TEXT_CYAN);
    gfx_draw_text(card_x + 36, card_y + 58, gfx_get_accent_name(g_settings.accent_index), PAL_TEXT_WHITE);
    gfx_draw_text(card_x + 6, card_y + 67, "TRAIL", PAL_TEXT_CYAN);
    gfx_draw_text(card_x + 36, card_y + 67, gfx_get_trail_name(g_settings.trail_index), PAL_TEXT_WHITE);
    gfx_draw_text(card_x + 6, card_y + 76, "RIG", PAL_TEXT_CYAN);
    gfx_draw_text(card_x + 36, card_y + 76, gfx_get_weapon_name(g_settings.weapon_rig), PAL_TEXT_WHITE);
    gfx_draw_text(card_x + 6, card_y + 85, "LASER", PAL_TEXT_CYAN);
    gfx_draw_text(card_x + 36, card_y + 85, gfx_get_laser_name(g_settings.laser_index), PAL_TEXT_WHITE);
    gfx_draw_text(card_x + 6, card_y + 94, "COINS", PAL_TEXT_GOLD);
    char buf[24]; save_format_coins(buf, sizeof(buf));
    gfx_draw_text(card_x + 36, card_y + 94, buf, PAL_TEXT_WHITE);
    gfx_draw_text(card_x + 6, card_y + 103, "BEST", PAL_TEXT_GOLD);
    siprintf(buf, "%06u", (unsigned int)g_settings.high_score);
    gfx_draw_text(card_x + 36, card_y + 103, buf, PAL_TEXT_WHITE);
}
static void draw_preview_engine_trail(int ship_x, int ship_y, int trail_idx) {
    if (trail_idx == 7) {
        for (int row = 0; row < 4; row++) {
            u8 col = gfx_get_rainbow_color((s_anim_frame >> 2) + row * 2);
            int w = (row < 2) ? 4 : 2;
            gfx_fill_rect(ship_x + 10 - w / 2, ship_y + 16 + row, w, 1, col);
        }
    } else {
        u8 trail_col = gfx_get_trail_color_animated(trail_idx, s_anim_frame);
        if ((s_anim_frame & 4) == 0) gfx_fill_rect(ship_x + 8, ship_y + 16, 4, 3, trail_col);
        else gfx_fill_rect(ship_x + 7, ship_y + 16, 6, 2, trail_col);
    }
}
static void draw_ship_preview_dynamic(int card_x, int card_y, int card_w) {
    int ship_x = card_x + (card_w - 20) / 2;
    int ship_y = card_y + 25;
#ifdef PLATFORM_HOST
    ship_y += ((s_anim_frame >> 4) & 1); /* 1px idle bob */
#endif
    int accent = g_settings.accent_index;
    if (accent < 0 || accent >= NUM_ACCENTS) accent = 1;
    gfx_draw_ship_styled(ship_x, ship_y, accent, s_anim_frame, g_settings.ship_index);
    draw_preview_engine_trail(ship_x, ship_y, g_settings.trail_index);
}

#ifdef PLATFORM_HOST
/* Play always opens the PLAY tab (Story / Waves / Endless). */
static const char* main_menu_label(int i) {
    static const char* base[6] = { "Play", "Multiplayer", "Shop", "Upgrades", "Controls", "Settings" };
    return base[i];
}
#endif

static void render_main_menu_static(void) {
    starfield_draw_base(0, 0);
    gfx_draw_text(10, 8, "SPACE UNLIMITED", PAL_TEXT_CYAN);
    gfx_draw_text(10, 18, "Recharged", PAL_TEXT_WHITE);
    gfx_fill_rect(10, 28, 45, 1, PAL_TEXT_CYAN);
    gfx_draw_text(10, 32, "GBA Edition", 17);

    const char* items[] = { "Play", "Multiplayer", "Shop", "Upgrades", "Controls", "Settings" };
    int start_y = 44; int step_y = 19;
#ifdef PLATFORM_HOST
    for (int i = 0; i < 6; i++) {
        gfx_draw_button(10, start_y + i * step_y, 90, 16, main_menu_label(i), false);
        /* A small padlock marks what the campaign still owes you. */
        if (!story_content_unlocked() && (i == 1 || i == 2 || i == 3))
            gfx_draw_char(102, start_y + i * step_y + 4, '#', 18);
    }
    if (!story_content_unlocked()) {
        char pbuf[32];
        siprintf(pbuf, "STORY %d/%d", (int)g_story.cleared_count, STORY_LEVEL_COUNT);
        gfx_draw_text(10, start_y + 6 * step_y + 2, pbuf, PAL_TEXT_GOLD);
    }
#else
    for (int i = 0; i < 6; i++) gfx_draw_button(10, start_y + i * step_y, 90, 16, items[i], false);
#endif
    (void)items;

    int card_w = 126;
    int card_x = SCREEN_WIDTH - card_w - 6;
    draw_ship_preview_static(card_x, 10, card_w, 116);
    gfx_draw_glass_card(card_x, 128, card_w, 24, PAL_BTN_BORDER, 14);
    gfx_draw_text_centered(card_x, 132, card_w, "Tap to select", PAL_TEXT_WHITE);
    gfx_draw_text_centered(card_x, 140, card_w, "A / D-PAD ok too", PAL_TEXT_CYAN);
}
static void render_main_menu_dynamic(void) {
    menu_draw_base();
#ifdef PLATFORM_HOST
    gfx_draw_button(10, 44 + s_menu_selected * 19, 90, 16,
        main_menu_label(s_menu_selected), true);
    if (s_shop_msg_timer > 0) {
        s_shop_msg_timer--;
        gfx_draw_text_centered(0, SCREEN_HEIGHT - 12, SCREEN_WIDTH, s_shop_msg, s_shop_msg_col);
    }
#else
    gfx_draw_button(10, 44 + s_menu_selected * 19, 90, 16,
        (const char*[]){"Play","Multiplayer","Shop","Upgrades","Controls","Settings"}[s_menu_selected], true);
#endif
    int card_w = 126;
    int card_x = SCREEN_WIDTH - card_w - 6;
    draw_ship_preview_dynamic(card_x, 10, card_w);
}

static void render_hangar_static(void) {
    starfield_draw_base(0, 0);
    gfx_draw_text(6, 4, "UPGRADE HANGAR", PAL_TEXT_CYAN);
    char cnum[24]; save_format_coins(cnum, sizeof(cnum));
    char coin_buf[32]; siprintf(coin_buf, "$%s COINS", cnum);
    int coin_x = SCREEN_WIDTH - 6 - (int)strlen(coin_buf) * 6;
    gfx_draw_text(coin_x, 4, coin_buf, PAL_TEXT_GOLD);
    gfx_fill_rect(4, 15, SCREEN_WIDTH - 8, 1, 20);

    const char* tab_names[SHOP_CAT_COUNT] = { "PAINTS", "TRAILS", "WEAPONS", "LASERS", "SHIPS" };
    for (int t = 0; t < SHOP_CAT_COUNT; t++) {
        int tx = shop_tab_x(t);
        bool is_active = (t == s_shop_category);
        u8 border = is_active ? PAL_TEXT_CYAN : 20;
        u8 bg = is_active ? PAL_BTN_HOVER : PAL_BTN_BG;
        u8 txt = is_active ? PAL_TEXT_WHITE : PAL_TEXT_CYAN;
        gfx_draw_glass_card(tx, 17, SHOP_TAB_W, 13, border, bg);
        gfx_draw_text_centered(tx, 19, SHOP_TAB_W, tab_names[t], txt);
    }
    int list_w = 116 + (SCREEN_WIDTH - 240) / 2;
    int right_x = 4 + list_w + 2;
    int right_w = SCREEN_WIDTH - 4 - right_x;
    gfx_draw_glass_card(4, 31, list_w, 112, PAL_BTN_BORDER, 14);
    gfx_draw_glass_card(right_x, 31, right_w, 112, PAL_BTN_BORDER, 14);
}

static void render_hangar_dynamic(void) {
    menu_draw_base();
    int cat = s_shop_category;
    int count = shop_get_category_count(cat);
    int selected = s_shop_selected[cat];
    int off = shop_scroll_offs();
    int first = off / LIST_ROW_H;

    int list_w = 116 + (SCREEN_WIDTH - 240) / 2;
    int right_x = 4 + list_w + 2;
    int right_w = SCREEN_WIDTH - 4 - right_x;

    // Clip list rows to the list card so smooth (sub-row) scrolling never
    // bleeds into the tab strip or the bottom message area.
    gfx_set_clip(5, hangar_list_top() - 1, list_w - 2, 5 * LIST_ROW_H);
    for (int i = first; i < first + 6; i++) {
        int item_idx = i;
        if (item_idx < 0 || item_idx >= count) continue;
        int row_y = hangar_list_top() + item_idx * LIST_ROW_H - off;
        bool is_sel = (item_idx == selected);
#ifdef PLATFORM_HOST
        u8 border = is_sel ? (((s_anim_frame >> 4) & 1) ? PAL_TEXT_WHITE : PAL_TEXT_CYAN) : 20;
#else
        u8 border = is_sel ? PAL_TEXT_CYAN : 20;
#endif
        u8 bg = is_sel ? PAL_BTN_HOVER : PAL_BTN_BG;
        gfx_draw_glass_card(6, row_y, list_w - 4, 19, border, bg);
        char name_buf[16] = {0}; char badge_buf[12] = {0}; u8 badge_col = PAL_TEXT_GOLD;
        switch (cat) {
            case 0: {
                strncpy(name_buf, gfx_get_accent_name(item_idx), 11);
                bool eq = (g_settings.accent_index == item_idx);
                bool own = shop_is_accent_owned(item_idx);
                if (eq) { strncpy(badge_buf, "[EQ]", sizeof(badge_buf)-1); badge_col = PAL_TEXT_GREEN; }
                else if (own) { strncpy(badge_buf, "OWN", sizeof(badge_buf)-1); badge_col = PAL_TEXT_CYAN; }
                else { format_price(badge_buf, shop_get_accent_price(item_idx)); }
                break;
            }
            case 1: {
                strncpy(name_buf, gfx_get_trail_name(item_idx), 11);
                bool eq = (g_settings.trail_index == item_idx);
                bool own = shop_is_trail_owned(item_idx);
                if (eq) { strncpy(badge_buf, "[EQ]", sizeof(badge_buf)-1); badge_col = PAL_TEXT_GREEN; }
                else if (own) { strncpy(badge_buf, "OWN", sizeof(badge_buf)-1); badge_col = PAL_TEXT_CYAN; }
                else { format_price(badge_buf, shop_get_trail_price(item_idx)); }
                break;
            }
            case 2: {
                strncpy(name_buf, gfx_get_weapon_name((WeaponRig)item_idx), 11);
                bool eq = (g_settings.weapon_rig == item_idx);
                bool own = shop_is_rig_owned((WeaponRig)item_idx);
                if (eq) { strncpy(badge_buf, "[EQ]", sizeof(badge_buf)-1); badge_col = PAL_TEXT_GREEN; }
                else if (own) { strncpy(badge_buf, "OWN", sizeof(badge_buf)-1); badge_col = PAL_TEXT_CYAN; }
                else { format_price(badge_buf, shop_get_rig_price((WeaponRig)item_idx)); }
                break;
            }
            case 3: {
                strncpy(name_buf, gfx_get_laser_name(item_idx), 11);
                bool eq = (g_settings.laser_index == item_idx);
                bool own = shop_is_laser_owned(item_idx);
                if (eq) { strncpy(badge_buf, "[EQ]", sizeof(badge_buf)-1); badge_col = PAL_TEXT_GREEN; }
                else if (own) { strncpy(badge_buf, "OWN", sizeof(badge_buf)-1); badge_col = PAL_TEXT_CYAN; }
                else { format_price(badge_buf, shop_get_laser_price(item_idx)); }
                break;
            }
            case SHOP_CAT_SHIPS: {
                strncpy(name_buf, gfx_get_ship_style_name(item_idx), 11);
                bool eq = (g_settings.ship_index == item_idx);
                bool own = shop_is_ship_owned(item_idx);
                if (eq) { strncpy(badge_buf, "[EQ]", sizeof(badge_buf)-1); badge_col = PAL_TEXT_GREEN; }
                else if (own) { strncpy(badge_buf, "OWN", sizeof(badge_buf)-1); badge_col = PAL_TEXT_CYAN; }
                else { format_price(badge_buf, shop_get_ship_price(item_idx)); }
                break;
            }
        }
        if (is_sel) { gfx_draw_char(9, row_y + 6, '>', PAL_TEXT_CYAN); gfx_draw_text(16, row_y + 6, name_buf, PAL_TEXT_WHITE); }
        else gfx_draw_text(10, row_y + 6, name_buf, PAL_TEXT_CYAN);
        int b_x = 4 + list_w - 6 - (int)strlen(badge_buf) * 6;
        gfx_draw_text(b_x, row_y + 6, badge_buf, badge_col);
    }
    gfx_clear_clip();
    if (off > 0) gfx_draw_char(4 + list_w - 10, 36, '^', PAL_TEXT_CYAN);
    if (off < (count - 5) * LIST_ROW_H) gfx_draw_char(4 + list_w - 10, 130, 'v', PAL_TEXT_CYAN);

    // Right panel preview
    gfx_draw_glass_card(right_x + 2, 33, right_w - 4, 35, 20, PAL_SPACE_BLACK);
    int preview_accent = (cat == 0) ? selected : g_settings.accent_index;
    int preview_trail  = (cat == 1) ? selected : g_settings.trail_index;
    int preview_laser  = (cat == 3) ? selected : g_settings.laser_index;
    WeaponRig preview_rig = (cat == 2) ? (WeaponRig)selected : g_settings.weapon_rig;
    int preview_hull   = (cat == SHOP_CAT_SHIPS) ? selected : g_settings.ship_index;
    if (preview_accent < 0 || preview_accent >= NUM_ACCENTS) preview_accent = 1;
    int ship_x = right_x + (right_w - 20) / 2;
    int ship_y = 47;
    gfx_draw_ship_styled(ship_x, ship_y, preview_accent, s_anim_frame, preview_hull);
    draw_preview_engine_trail(ship_x, ship_y, preview_trail);
    if (cat == 2 || cat == 3) {
        if (preview_rig == WEAPON_INFINITY) {
            u8 col = (preview_laser == LASER_RAINBOW_IDX)
                ? gfx_get_rainbow_color(s_anim_frame >> 1)
                : gfx_get_laser_color(preview_laser);
            int bw = (s_anim_frame & 2) ? 4 : 3;
            gfx_fill_rect(ship_x + 10 - bw / 2, 34, bw, ship_y - 38, col);
            gfx_fill_rect(ship_x + 10, 34, 1, ship_y - 38, PAL_TEXT_WHITE);
        } else {
            int travel = (s_anim_frame * 2) % 18;
            int laser_center_y = ship_y - 2 - travel;
            if (laser_center_y >= 38) {
                gfx_draw_laser(ship_x + 6, laser_center_y, false, preview_laser, s_anim_frame, false);
                gfx_draw_laser(ship_x + 14, laser_center_y, false, preview_laser, s_anim_frame, false);
            }
        }
    }
    gfx_fill_rect(right_x + 4, 70, right_w - 8, 1, 20);

    const char* full_name = "";
    const char* desc1 = "";
    const char* desc2 = "";
    char status_buf[32] = {0}; u8 status_col = PAL_TEXT_GOLD;
    bool is_owned = false; bool is_equipped = false; int item_price = 0;

    switch (cat) {
        case 0: full_name = gfx_get_accent_name(selected); desc1 = gfx_get_accent_desc(selected); desc2 = "Hull skin finish"; is_owned = shop_is_accent_owned(selected); is_equipped = (g_settings.accent_index == selected); item_price = shop_get_accent_price(selected); break;
        case 1: full_name = gfx_get_trail_name(selected); desc1 = gfx_get_trail_desc(selected); desc2 = "Drive exhaust wake"; is_owned = shop_is_trail_owned(selected); is_equipped = (g_settings.trail_index == selected); item_price = shop_get_trail_price(selected); break;
        case 2: full_name = gfx_get_weapon_name((WeaponRig)selected); desc1 = gfx_get_weapon_desc((WeaponRig)selected); desc2 = "Primary ordnance"; is_owned = shop_is_rig_owned((WeaponRig)selected); is_equipped = (g_settings.weapon_rig == selected); item_price = shop_get_rig_price((WeaponRig)selected); break;
        case 3: full_name = gfx_get_laser_name(selected); desc1 = gfx_get_laser_desc(selected); desc2 = "Laser crystal core"; is_owned = shop_is_laser_owned(selected); is_equipped = (g_settings.laser_index == selected); item_price = shop_get_laser_price(selected); break;
        case SHOP_CAT_SHIPS: full_name = gfx_get_ship_style_name(selected); desc1 = gfx_get_ship_style_desc(selected); desc2 = "Works with every paint"; is_owned = shop_is_ship_owned(selected); is_equipped = (g_settings.ship_index == selected); item_price = shop_get_ship_price(selected); break;
    }

#ifdef PLATFORM_HOST
    int name_y = 80, status_y = 89, desc1_y = 98, desc2_y = 107;
#else
    int name_y = 73, status_y = 83, desc1_y = 94, desc2_y = 104;
#endif
    gfx_draw_text_centered(right_x, name_y, right_w, full_name, PAL_TEXT_WHITE);
    if (is_equipped) { siprintf(status_buf, "[EQUIPPED]"); status_col = PAL_TEXT_GREEN; }
    else if (is_owned) { siprintf(status_buf, "[OWNED]"); status_col = PAL_TEXT_CYAN; }
    else { char pbuf[16]; format_price(pbuf, item_price); siprintf(status_buf, "COST: %s", pbuf); status_col = PAL_TEXT_GOLD; }
    gfx_draw_text_centered(right_x, status_y, right_w, status_buf, status_col);
    gfx_draw_text_centered(right_x, desc1_y, right_w, desc1, PAL_TEXT_CYAN);
    gfx_draw_text_centered(right_x, desc2_y, right_w, desc2, 17);

    int btn_w = right_w - 8;
    int btn_x = right_x + 4;
    if (is_equipped) {
        gfx_draw_glass_card(btn_x, 118, btn_w, 21, PAL_TEXT_GREEN, PAL_BTN_HOVER);
        gfx_draw_text_centered(btn_x, 125, btn_w, "EQUIPPED", PAL_TEXT_GREEN);
    } else if (is_owned) {
        gfx_draw_glass_card(btn_x, 118, btn_w, 21, PAL_TEXT_CYAN, PAL_BTN_HOVER);
#ifdef PLATFORM_HOST
        gfx_draw_text_centered(btn_x, 125, btn_w, "TAP EQUIP", PAL_TEXT_WHITE);
#else
        gfx_draw_text_centered(btn_x, 125, btn_w, "[A] EQUIP", PAL_TEXT_WHITE);
#endif
    } else {
        bool can_afford = (g_settings.coins >= (coin_t)item_price);
        char pbuf[16]; format_price(pbuf, item_price); char btn_text[28];
        if (can_afford) { siprintf(btn_text, "[A] BUY %s", pbuf); gfx_draw_glass_card(btn_x, 118, btn_w, 21, PAL_TEXT_GOLD, PAL_BTN_HOVER); gfx_draw_text_centered(btn_x, 125, btn_w, btn_text, PAL_TEXT_GOLD); }
        else { siprintf(btn_text, "NEED %s", pbuf); gfx_draw_glass_card(btn_x, 118, btn_w, 21, PAL_TEXT_RED, PAL_BTN_BG); gfx_draw_text_centered(btn_x, 125, btn_w, btn_text, PAL_TEXT_RED); }
    }

    if (s_shop_msg_timer > 0) {
        int w = (int)strlen(s_shop_msg) * 6 + 12;
        int x = (SCREEN_WIDTH - w) / 2;
        gfx_draw_glass_card(x, 146, w, 12, s_shop_msg_col, 15);
        gfx_draw_text_centered(x, 148, w, s_shop_msg, s_shop_msg_col);
    } else {
#ifdef PLATFORM_HOST
        gfx_draw_text_centered(0, 148, SCREEN_WIDTH, "Tap tabs / items / BUY    BACK to exit", PAL_TEXT_WHITE);
#else
        gfx_draw_text_centered(0, 148, SCREEN_WIDTH, "L/R: Tab  D-PAD: Pick  A: Buy/Equip  B: Exit", PAL_TEXT_WHITE);
#endif
    }
}

// ── Upgrades screen ───────────────────────────────────────────────────
static void render_upgrades_static(void) {
    starfield_draw_base(0, 0);
    gfx_draw_text(6, 4, "SHIP UPGRADES", PAL_TEXT_CYAN);
    char cnum[24]; save_format_coins(cnum, sizeof(cnum));
    char coin_buf[32]; siprintf(coin_buf, "$%s COINS", cnum);
    int coin_x = SCREEN_WIDTH - 6 - (int)strlen(coin_buf) * 6;
    gfx_draw_text(coin_x, 4, coin_buf, PAL_TEXT_GOLD);
    gfx_fill_rect(4, 15, SCREEN_WIDTH - 8, 1, 20);

    int list_w = 116 + (SCREEN_WIDTH - 240) / 2;
    int right_x = 4 + list_w + 2;
    int right_w = SCREEN_WIDTH - 4 - right_x;

    gfx_draw_glass_card(4, 18, list_w, 122, PAL_BTN_BORDER, 14);
    gfx_draw_glass_card(right_x, 18, right_w, 122, PAL_BTN_BORDER, 14);
    gfx_draw_text(right_x + 4, 22, "DETAILS", PAL_TEXT_CYAN);
}

static void render_upgrades_dynamic(void) {
    menu_draw_base();
    int count = upgrades_row_count();
    int selected = s_upg_selected;
    int off = upg_scroll_offs();
    int first = off / LIST_ROW_H;

    int list_w = 116 + (SCREEN_WIDTH - 240) / 2;
    int right_x = 4 + list_w + 2;
    int right_w = SCREEN_WIDTH - 4 - right_x;

    // Clip list rows to the list card so smooth scrolling stays contained.
    gfx_set_clip(5, upgrades_list_top() - 1, list_w - 2, 5 * LIST_ROW_H);
    for (int i = first; i < first + 6; i++) {
        int idx = i;
        if (idx < 0 || idx >= count) continue;
        int row_y = upgrades_list_top() + idx * LIST_ROW_H - off;
        bool is_sel = (idx == selected);
        u8 border = is_sel ? PAL_TEXT_CYAN : 20;
        u8 bg = is_sel ? PAL_BTN_HOVER : PAL_BTN_BG;
        gfx_draw_glass_card(6, row_y, list_w - 4, 19, border, bg);

        const char* name = shop_get_upgrade_name((UpgradeType)idx);
        char name_buf[12]; strncpy(name_buf, name, 11); name_buf[11]='\0';

        int cur_lv = shop_get_upgrade_level((UpgradeType)idx);
        char lvl_buf[10];
        if (cur_lv >= UPG_MAX_LEVEL) siprintf(lvl_buf, "MAX");
        else siprintf(lvl_buf, "L%d/%d", cur_lv, UPG_MAX_LEVEL);

        u8 lvl_col = (cur_lv >= UPG_MAX_LEVEL) ? PAL_TEXT_GOLD : (cur_lv>0 ? PAL_TEXT_GREEN : PAL_TEXT_CYAN);

        if (is_sel) { gfx_draw_char(9, row_y + 6, '>', PAL_TEXT_CYAN); gfx_draw_text(16, row_y + 6, name_buf, PAL_TEXT_WHITE); }
        else gfx_draw_text(10, row_y + 6, name_buf, PAL_TEXT_CYAN);

        int b_x = 4 + list_w - 6 - (int)strlen(lvl_buf) * 6;
        gfx_draw_text(b_x, row_y + 6, lvl_buf, lvl_col);
    }
    gfx_clear_clip();
    if (off > 0) gfx_draw_char(4 + list_w - 10, 23, '^', PAL_TEXT_CYAN);
    if (off < (count - 5) * LIST_ROW_H) gfx_draw_char(4 + list_w - 10, 126, 'v', PAL_TEXT_CYAN);

    // Right details panel
    UpgradeType upg = (UpgradeType)selected;
    int cur_lv = shop_get_upgrade_level(upg);
    const char* full = shop_get_upgrade_name(upg);
    const char* line1 = shop_get_upgrade_desc_line1(upg);
    const char* line2 = shop_get_upgrade_desc_line2(upg, cur_lv);

    gfx_draw_text_centered(right_x, 33, right_w, full, PAL_TEXT_WHITE);
    gfx_draw_text_centered(right_x, 43, right_w, line1, PAL_TEXT_CYAN);
    gfx_draw_text_centered(right_x, 53, right_w, line2, 17);

    // Progress bar for level
    gfx_draw_progress_bar(right_x + 4, 65, right_w - 8, 6, cur_lv, UPG_MAX_LEVEL, PAL_TEXT_GREEN, 20);
    char prog_txt[16]; siprintf(prog_txt, "%d / %d", cur_lv, UPG_MAX_LEVEL);
    gfx_draw_text_centered(right_x, 73, right_w, prog_txt, PAL_TEXT_WHITE);

    // Limits info
    const char* limit_info = "";
    switch (upg) {
        case UPG_ENGINE: limit_info = "Max 2x speed"; break;
        case UPG_FIRE_RATE: limit_info = "2/sec up to 10/sec"; break;
        case UPG_DAMAGE: limit_info = "Start 1, max +5"; break;
        case UPG_SHIELD: limit_info = "Up to 6 shields"; break;
        case UPG_HULL: limit_info = "Up to 7 lives"; break;
        case UPG_DASH: limit_info = "Beam +25% per lv"; break;
        case UPG_SCAVENGER: limit_info = "+275% coins max"; break;
        case UPG_OVERDRIVE: limit_info = "8s -> 26s rapid"; break;
        default: break;
    }
    gfx_draw_text_centered(right_x, 84, right_w, limit_info, PAL_TEXT_GOLD);

    // Buy button
    int btn_w = right_w - 8;
    int btn_x = right_x + 4;
    if (cur_lv >= UPG_MAX_LEVEL) {
        gfx_draw_glass_card(btn_x, 96, btn_w, 21, PAL_TEXT_GOLD, PAL_BTN_HOVER);
        gfx_draw_text_centered(btn_x, 103, btn_w, "MAX LEVEL", PAL_TEXT_GOLD);
    } else {
        int price = shop_get_upgrade_price(upg, cur_lv);
        bool can = g_settings.coins >= (coin_t)price;
        char pbuf[16]; format_price(pbuf, price);
        char btn[28];
        if (can) {
            siprintf(btn, "[A] UP %s", pbuf);
            gfx_draw_glass_card(btn_x, 96, btn_w, 21, PAL_TEXT_GOLD, PAL_BTN_HOVER);
            gfx_draw_text_centered(btn_x, 103, btn_w, btn, PAL_TEXT_GOLD);
        } else {
            siprintf(btn, "NEED %s", pbuf);
            gfx_draw_glass_card(btn_x, 96, btn_w, 21, PAL_TEXT_RED, PAL_BTN_BG);
            gfx_draw_text_centered(btn_x, 103, btn_w, btn, PAL_TEXT_RED);
        }
    }

    // Mini preview of ship with current speed?
    int ship_x = right_x + (right_w - 20) / 2;
    int ship_y = 118;
    gfx_draw_ship_styled(ship_x, ship_y, g_settings.accent_index, s_anim_frame, g_settings.ship_index);
    draw_preview_engine_trail(ship_x, ship_y, g_settings.trail_index);

    if (s_shop_msg_timer > 0) {
        int w = (int)strlen(s_shop_msg) * 6 + 12;
        int x = (SCREEN_WIDTH - w) / 2;
        gfx_draw_glass_card(x, 146, w, 12, s_shop_msg_col, 15);
        gfx_draw_text_centered(x, 148, w, s_shop_msg, s_shop_msg_col);
    } else {
        gfx_draw_text_centered(0, 146, SCREEN_WIDTH, "A: Upgrade  B: Back  START: Play", PAL_TEXT_WHITE);
    }
}

static void render_controls_static(void) {
    starfield_draw_base(0, 0);
    gfx_draw_text(10, 6, "Controls & Guide", PAL_TEXT_WHITE);
    gfx_fill_rect(10, 16, SCREEN_WIDTH - 20, 1, 20);
    int half_w = 108 + (SCREEN_WIDTH - 240) / 2;
    int right_x = 8 + half_w + 6;
    int right_w = SCREEN_WIDTH - 8 - right_x;
    gfx_draw_glass_card(8, 20, half_w, 120, PAL_BTN_BORDER, 14);
    gfx_draw_text(12, 24, "GBA CONTROLS", PAL_TEXT_CYAN);
    gfx_draw_text(12, 36, "D-PAD: Move ship", PAL_TEXT_WHITE);
    gfx_draw_text(12, 48, "A: Fire lasers", PAL_TEXT_WHITE);
    gfx_draw_text(12, 60, "B: HOLD 2s = BEAM!", PAL_TEXT_GOLD);
    gfx_draw_text(12, 72, "START: Pause", PAL_TEXT_WHITE);
    gfx_draw_text(12, 84, "SELECT: Reset", PAL_TEXT_WHITE);
#ifdef PLATFORM_HOST
    gfx_draw_text(12, 100, "Starter: Slow 0.7x", 17);
    gfx_draw_text(12, 110, "2 bullets/sec only!", 17);
#else
    gfx_draw_text(12, 100, "3 modes: Waves /", 17);
    gfx_draw_text(12, 110, "Endless / Overdrive", 17);
#endif
    gfx_draw_glass_card(right_x, 20, right_w, 120, PAL_BTN_BORDER, 14);
    gfx_draw_text(right_x + 4, 24, "UPGRADE GUIDE", PAL_TEXT_CYAN);
    gfx_draw_text(right_x + 4, 36, "Speed: 0.7x->2x", PAL_TEXT_WHITE);
    gfx_draw_text(right_x + 4, 48, "Fire: 2/s->10/s", PAL_TEXT_GOLD);
    gfx_draw_text(right_x + 4, 60, "Hold B: MEGA BEAM", PAL_TEXT_GREEN);
    gfx_draw_text(right_x + 4, 72, "Final Nova = GOD!", PAL_TEXT_GOLD);
#ifdef PLATFORM_HOST
    gfx_draw_text(right_x + 4, 86, "Later waves HARD!", PAL_TEXT_RED);
#else
    gfx_draw_text(right_x + 4, 86, "W5 mini / W10 BOSS", PAL_TEXT_RED);
#endif
    gfx_draw_text(right_x + 4, 98, "Beam: +25% dmg/lv", PAL_TEXT_CYAN);
    gfx_draw_text(right_x + 4, 110, "15x combo = $$$", PAL_TEXT_WHITE);
    gfx_draw_text_centered(0, 146, SCREEN_WIDTH, "Press A or B to return", PAL_TEXT_WHITE);
}
static void render_controls_dynamic(void) { menu_draw_base(); }

// ── Settings screen ──────────────────────────────────────────────────
static const char* options_label(int row) {
    switch (row) {
        case 0: return "DIFFICULTY";
        case 1: return "MUSIC";
        case 2: return "SFX";
        case 3: return "SCREEN SHAKE";
#ifdef PLATFORM_HOST
        case OPT_ROW_HAPTICS: return "HAPTICS";
        case OPT_ROW_CODES:   return "CODES";
        case OPT_ROW_FREE:    return "LET ME BE FREE";
        case OPT_ROW_ERASE:   return "ERASE DATA";
#endif
        default: return "";
    }
}

static void render_options_static(void) {
    starfield_draw_base(0, 0);
    gfx_draw_text(10, 6, "SETTINGS", PAL_TEXT_CYAN);
    gfx_fill_rect(10, 16, SCREEN_WIDTH - 20, 1, 20);
#ifdef PLATFORM_HOST
    gfx_draw_text_centered(0, 148, SCREEN_WIDTH, "Tap a row to change    BACK to exit", PAL_TEXT_WHITE);
#else
    gfx_draw_text_centered(0, 148, SCREEN_WIDTH, "A: Change  L/R: Value  B: Back", PAL_TEXT_WHITE);
#endif
}

static void render_options_dynamic(void) {
    menu_draw_base();
    const int count = options_row_count();
    for (int i = 0; i < count; i++) {
        int row_y = OPT_ROW_Y0 + i * OPT_ROW_H;
        bool sel = (i == s_opt_selected);
#ifdef PLATFORM_HOST
        u8 border = sel ? (((s_anim_frame >> 4) & 1) ? PAL_TEXT_WHITE : PAL_TEXT_CYAN) : 20;
#else
        u8 border = sel ? PAL_TEXT_CYAN : 20;
#endif
        u8 bg = sel ? PAL_BTN_HOVER : PAL_BTN_BG;
        gfx_draw_glass_card(8, row_y, SCREEN_WIDTH - 16, OPT_ROW_H - 2, border, bg);

        char val[16];
        const char* value = "";
        switch (i) {
            case 0: value = gfx_get_diff_name(g_settings.difficulty); break;
            case 1: siprintf(val, "%d%%", g_settings.music_volume); value = val; break;
            case 2: siprintf(val, "%d%%", g_settings.sfx_volume); value = val; break;
            case 3: value = g_settings.screen_shake ? "ON" : "OFF"; break;
#ifdef PLATFORM_HOST
            case OPT_ROW_HAPTICS: value = g_settings.haptics ? "ON" : "OFF"; break;
            case OPT_ROW_CODES:   value = "ENTER"; break;
            case OPT_ROW_FREE:
                /* Counts down the remaining taps so it is discoverable. */
                if (story_content_unlocked()) value = "FREE";
                else if (s_free_taps == 0) value = "TAP x3";
                else if (s_free_taps == 1) value = "TAP x2";
                else value = "ONCE MORE";
                break;
            case OPT_ROW_ERASE:   value = "RESET"; break;
#endif
            default: break;
        }

        u8 label_col = sel ? PAL_TEXT_WHITE : PAL_TEXT_CYAN;
        gfx_draw_text(14, row_y + 4, options_label(i), label_col);
        int vx = SCREEN_WIDTH - 14 - 6 - (int)strlen(value) * 6;
        gfx_draw_text(vx, row_y + 4, value, PAL_TEXT_GOLD);
        if (sel) gfx_draw_char(8, row_y + 4, '>', PAL_TEXT_CYAN);
    }
}

static void render_mode_select_static(void) {
    starfield_draw_base(0, 0);
#ifdef PLATFORM_HOST
    gfx_draw_text(10, 6, "PLAY", PAL_TEXT_CYAN);
    gfx_fill_rect(10, 16, SCREEN_WIDTH - 20, 1, 20);
    gfx_draw_text_centered(0, 148, SCREEN_WIDTH, "Tap a mode    BACK to cancel", PAL_TEXT_WHITE);
#else
    gfx_draw_text(10, 6, "SELECT MODE", PAL_TEXT_CYAN);
    gfx_fill_rect(10, 16, SCREEN_WIDTH - 20, 1, 20);
    gfx_draw_text_centered(0, 148, SCREEN_WIDTH, "D-PAD: Pick   A: Start   B: Back", PAL_TEXT_WHITE);
#endif
}

static void render_mode_select_dynamic(void) {
    menu_draw_base();
    int card_w = mode_card_w();
    int card_x = (SCREEN_WIDTH - card_w) / 2;
    for (int i = 0; i < PLAY_CARD_COUNT; i++) {
        bool sel = (s_menu_selected == i);
        bool locked = play_card_locked(i);
        u8 border = locked ? (u8)20 : (sel ? PAL_TEXT_CYAN : PAL_BTN_BORDER);
        u8 bg = sel && !locked ? PAL_BTN_HOVER : PAL_BTN_BG;
        int y = mode_card_y(i);
        gfx_draw_glass_card(card_x, y, card_w, 32, border, bg);

        u8 title_col = locked ? (u8)18 : (sel ? PAL_TEXT_WHITE : PAL_TEXT_CYAN);
        u8 line_col  = locked ? (u8)18 : (sel ? PAL_TEXT_GOLD : (u8)17);
        gfx_draw_text_centered(card_x, y + 6, card_w, s_mode_titles[i], title_col);
        /* Locked cards show the reason instead of the blurb. */
        gfx_draw_text_centered(card_x, y + 18, card_w,
                               locked ? "LOCKED - FINISH STORY MODE" : s_mode_lines[i],
                               line_col);

#ifdef PLATFORM_HOST
        if (locked) {
            gfx_draw_char(card_x + card_w - 12, y + 6, '#', 18);   /* padlock */
        } else if (s_mode_ids[i] == GAME_MODE_STORY) {
            /* Story card carries the campaign progress badge. */
            char pbuf[24];
            siprintf(pbuf, "%d/%d", (int)g_story.cleared_count, STORY_LEVEL_COUNT);
            gfx_draw_text(card_x + card_w - 6 - (int)strlen(pbuf) * 6, y + 6, pbuf, PAL_TEXT_GOLD);
            siprintf(pbuf, "LV %d", story_current_level());
            gfx_draw_text(card_x + 6, y + 6, pbuf, PAL_TEXT_GREEN);
        }
#else
        /* D-pad caret: the GBA has no touch input, so make the highlighted
         * card unmistakable. Android drives this screen by tapping. */
        if (sel) gfx_draw_char(card_x + 4, y + 6, '>', PAL_TEXT_CYAN);
#endif
    }
#ifdef PLATFORM_HOST
    if (s_shop_msg_timer > 0)
        gfx_draw_text_centered(0, SCREEN_HEIGHT - 24, SCREEN_WIDTH, s_shop_msg, s_shop_msg_col);
#endif
}

static void render_paused(void) {
    game_draw();
    int w = 110; int h = 88;
    int x = (SCREEN_WIDTH - w) / 2;
    int y = (SCREEN_HEIGHT - h) / 2;
    gfx_draw_glass_card(x, y, w, h, PAL_TEXT_WHITE, 15);
    gfx_draw_text_centered(x, y + 6, w, "PAUSED", PAL_TEXT_CYAN);
    if (coop_guest_active()) {
        const char* opts[] = { "Resume", "Leave Co-op" };
        for (int i = 0; i < 2; i++) gfx_draw_button(x + 10, y + 20 + i * 18, w - 20, 15, opts[i], s_menu_selected == i);
    } else {
        const char* opts[] = { "Resume", "Restart", "Main Menu" };
        for (int i = 0; i < 3; i++) gfx_draw_button(x + 10, y + 20 + i * 18, w - 20, 15, opts[i], s_menu_selected == i);
    }
}
static void render_game_over(void) {
    game_draw();
    int w = 130; int h = 122;
    int x = (SCREEN_WIDTH - w) / 2;
    int y = (SCREEN_HEIGHT - h) / 2;
    gfx_draw_glass_card(x, y, w, h, g_game.time_up ? PAL_TEXT_GOLD : PAL_TEXT_RED, 15);
    if (g_game.time_up) gfx_draw_text_centered(x, y + 6, w, "TIME UP!", PAL_TEXT_GOLD);
    else gfx_draw_text_centered(x, y + 6, w, "GAME OVER", PAL_TEXT_RED);
    char buf[32];
    siprintf(buf, "Score: %06u", (unsigned int)g_game.score);
    gfx_draw_text_centered(x, y + 18, w, buf, PAL_TEXT_WHITE);
    char go_coins[24]; save_format_coins(go_coins, sizeof(go_coins));
    siprintf(buf, "Coins: $%s", go_coins);
    gfx_draw_text_centered(x, y + 28, w, buf, PAL_TEXT_GOLD);
    if (g_game.is_new_high_score) gfx_draw_badge(x + (w - 74) / 2, y + 39, "NEW BEST!", PAL_TEXT_GOLD);
    else { siprintf(buf, "Best:  %06u", (unsigned int)g_settings.high_score); gfx_draw_text_centered(x, y + 40, w, buf, 17); }
    if (coop_guest_active()) {
        gfx_draw_button(x + 10, y + 54, w - 20, 15, "Main Menu", s_menu_selected == 0);
        gfx_draw_text_centered(x, y + 74, w, "Host decides on a rematch", 17);
    } else {
        const char* opts[] = { "Retry", "Shop", "Main Menu" };
        for (int i = 0; i < 3; i++) gfx_draw_button(x + 10, y + 54 + i * 18, w - 20, 15, opts[i], s_menu_selected == i);
    }
}

void menu_draw(void) {
    switch (s_current_screen) {
        case SCREEN_MAIN_MENU:
            if (!s_static_valid) { menu_static_begin(); render_main_menu_static(); menu_static_end(); }
            render_main_menu_dynamic(); break;
        case SCREEN_HANGAR:
            if (!s_static_valid) { menu_static_begin(); render_hangar_static(); menu_static_end(); }
            render_hangar_dynamic(); break;
        case SCREEN_SETTINGS: // upgrades
            if (!s_static_valid) { menu_static_begin(); render_upgrades_static(); menu_static_end(); }
            render_upgrades_dynamic(); break;
        case SCREEN_CONTROLS:
            if (!s_static_valid) { menu_static_begin(); render_controls_static(); menu_static_end(); }
            render_controls_dynamic(); break;
        case SCREEN_OPTIONS:
            if (!s_static_valid) { menu_static_begin(); render_options_static(); menu_static_end(); }
            render_options_dynamic(); break;
        case SCREEN_MODE_SELECT:
            if (!s_static_valid) { menu_static_begin(); render_mode_select_static(); menu_static_end(); }
            render_mode_select_dynamic(); break;
        case SCREEN_MULTIPLAYER:
            /* Fully dynamic: matchmaking status can change on any frame. */
            starfield_draw_base(0, 0);
            starfield_draw_stars(0, 0);
            render_multiplayer(); break;
#ifdef PLATFORM_HOST
        case SCREEN_STORY_INTRO:  render_story_intro(); break;
        case SCREEN_STORY_MAP:    render_story_map(); break;
        case SCREEN_STORY_SHOP:   render_story_shop(); break;
        case SCREEN_STORY_RESULT: render_story_result(); break;
#endif
        case SCREEN_PLAYING: game_draw(); break;
        case SCREEN_PAUSED: render_paused(); break;
        case SCREEN_GAME_OVER: render_game_over(); break;
    }
}
