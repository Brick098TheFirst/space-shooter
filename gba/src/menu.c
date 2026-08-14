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
    if (screen == SCREEN_MAIN_MENU || screen == SCREEN_HANGAR || screen == SCREEN_SETTINGS ||
        screen == SCREEN_CONTROLS || screen == SCREEN_OPTIONS || screen == SCREEN_MODE_SELECT ||
        screen == SCREEN_MULTIPLAYER) {
        audio_play_bgm(BGM_MENU);
    }
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

static void request_play(void) {
    /* Both platforms go through mode select (Waves / Endless / Overdrive). */
    menu_open(SCREEN_MODE_SELECT);
}

static void launch_mode(GameMode mode) {
    game_set_mode(mode);
    game_start();
    s_current_screen = SCREEN_PLAYING;
}

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
#define OPT_ROW_ERASE   6
#define OPT_ROW_H       16
#define OPT_ROW_Y0      22
#else
#define OPT_ROW_H       20
#define OPT_ROW_Y0      24
#endif

static int options_row_count(void) {
#ifdef PLATFORM_HOST
    return 7; // + haptics, codes, erase (Android only)
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

static const char* s_mode_titles[3] = { "WAVES", "ENDLESS", "OVERDRIVE" };
static const char* s_mode_lines[3] = {
    "Clear waves. Classic run.",
    "No waves. Random hunters.",
    "90s score rush. Max chaos."
};

static int mode_card_y(int i) { return 28 + i * 36; }

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
    const int count = 3;
    int tx, ty;
    if (consume_tap(&tx, &ty)) {
        int card_w = mode_card_w();
        int card_x = (SCREEN_WIDTH - card_w) / 2;
        for (int i = 0; i < count; i++) {
            if (in_rect(tx, ty, card_x, mode_card_y(i), card_w, 32)) {
                s_menu_selected = i;
                launch_mode((GameMode)i);
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
    if (key_hit(KEY_A) || key_hit(KEY_START)) launch_mode((GameMode)s_menu_selected);
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
        case SCREEN_PLAYING:
            s_tap_pending = false;
            if (key_hit(KEY_START)) { s_current_screen = SCREEN_PAUSED; s_menu_selected = 0; }
            else { game_update(); if (g_game.is_game_over) { s_current_screen = SCREEN_GAME_OVER; s_menu_selected = 0; } }
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

static void render_main_menu_static(void) {
    starfield_draw_base(0, 0);
    gfx_draw_text(10, 8, "SPACE UNLIMITED", PAL_TEXT_CYAN);
    gfx_draw_text(10, 18, "Recharged", PAL_TEXT_WHITE);
    gfx_fill_rect(10, 28, 45, 1, PAL_TEXT_CYAN);
    gfx_draw_text(10, 32, "GBA Edition", 17);

    const char* items[] = { "Play", "Multiplayer", "Shop", "Upgrades", "Controls", "Settings" };
    int start_y = 44; int step_y = 19;
    for (int i = 0; i < 6; i++) gfx_draw_button(10, start_y + i * step_y, 90, 16, items[i], false);

    int card_w = 126;
    int card_x = SCREEN_WIDTH - card_w - 6;
    draw_ship_preview_static(card_x, 10, card_w, 116);
    gfx_draw_glass_card(card_x, 128, card_w, 24, PAL_BTN_BORDER, 14);
    gfx_draw_text_centered(card_x, 132, card_w, "Tap to select", PAL_TEXT_WHITE);
    gfx_draw_text_centered(card_x, 140, card_w, "A / D-PAD ok too", PAL_TEXT_CYAN);
}
static void render_main_menu_dynamic(void) {
    menu_draw_base();
    gfx_draw_button(10, 44 + s_menu_selected * 19, 90, 16,
        (const char*[]){"Play","Multiplayer","Shop","Upgrades","Controls","Settings"}[s_menu_selected], true);
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
    gfx_draw_text(10, 6, "SELECT MODE", PAL_TEXT_CYAN);
    gfx_fill_rect(10, 16, SCREEN_WIDTH - 20, 1, 20);
#ifdef PLATFORM_HOST
    gfx_draw_text_centered(0, 148, SCREEN_WIDTH, "Tap a mode    BACK to cancel", PAL_TEXT_WHITE);
#else
    gfx_draw_text_centered(0, 148, SCREEN_WIDTH, "D-PAD: Pick   A: Start   B: Back", PAL_TEXT_WHITE);
#endif
}

static void render_mode_select_dynamic(void) {
    menu_draw_base();
    int card_w = mode_card_w();
    int card_x = (SCREEN_WIDTH - card_w) / 2;
    for (int i = 0; i < 3; i++) {
        bool sel = (s_menu_selected == i);
        u8 border = sel ? PAL_TEXT_CYAN : PAL_BTN_BORDER;
        u8 bg = sel ? PAL_BTN_HOVER : PAL_BTN_BG;
        int y = mode_card_y(i);
        gfx_draw_glass_card(card_x, y, card_w, 32, border, bg);
        gfx_draw_text_centered(card_x, y + 6, card_w, s_mode_titles[i], sel ? PAL_TEXT_WHITE : PAL_TEXT_CYAN);
        gfx_draw_text_centered(card_x, y + 18, card_w, s_mode_lines[i], sel ? PAL_TEXT_GOLD : 17);
#ifndef PLATFORM_HOST
        /* D-pad caret: the GBA has no touch input, so make the highlighted
         * card unmistakable. Android drives this screen by tapping. */
        if (sel) gfx_draw_char(card_x + 4, y + 6, '>', PAL_TEXT_CYAN);
#endif
    }
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
        case SCREEN_PLAYING: game_draw(); break;
        case SCREEN_PAUSED: render_paused(); break;
        case SCREEN_GAME_OVER: render_game_over(); break;
    }
}
