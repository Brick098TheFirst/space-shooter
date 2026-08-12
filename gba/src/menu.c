#include "menu.h"
#include "renderer.h"
#include "game.h"
#include "audio.h"
#include "save.h"
#include "starfield.h"
#include <string.h>

static GameScreen s_current_screen = SCREEN_MAIN_MENU;
static int s_menu_selected = 0;
static int s_anim_frame = 0;

// Shop state: now 4 categories (0=PAINTS,1=TRAILS,2=WEAPONS,3=LASERS)
#define SHOP_CAT_COUNT 4
static int s_shop_category = 0;
static int s_shop_selected[SHOP_CAT_COUNT] = { 0, 0, 0, 0 };
static int s_shop_scroll[SHOP_CAT_COUNT]   = { 0, 0, 0, 0 };

// Upgrades state: 8 core upgrades, 5 visible
static int s_upg_selected = 0;
static int s_upg_scroll = 0;

static int s_shop_msg_timer = 0;
static char s_shop_msg[36] = {0};
static u8 s_shop_msg_col = PAL_TEXT_GOLD;

static bool s_static_valid = false;

static int s_tap_x = -1;
static int s_tap_y = -1;
static bool s_tap_pending = false;

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
    s_tap_pending = false;
    menu_static_invalidate();
    if (screen == SCREEN_MAIN_MENU || screen == SCREEN_HANGAR || screen == SCREEN_SETTINGS ||
        screen == SCREEN_CONTROLS || screen == SCREEN_CREDITS || screen == SCREEN_MODE_SELECT) {
        audio_play_bgm(BGM_MENU);
    }
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
        case SCREEN_CONTROLS:
        case SCREEN_CREDITS:
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
        default: return 0;
    }
}

static void request_play(void) {
#ifdef PLATFORM_HOST
    menu_open(SCREEN_MODE_SELECT);
#else
    game_set_mode(GAME_MODE_WAVES);
    game_start();
    s_current_screen = SCREEN_PLAYING;
#endif
}

static void launch_mode(GameMode mode) {
    game_set_mode(mode);
    game_start();
    s_current_screen = SCREEN_PLAYING;
}

#define SHOP_TAB_W 56

static int shop_tab_x(int t) {
    int tab_gap = (SCREEN_WIDTH - 8 - 4 * SHOP_TAB_W) / 3;
    if (tab_gap < 0) tab_gap = 0;
    if (t == 3) return SCREEN_WIDTH - 4 - SHOP_TAB_W;
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
    menu_static_invalidate();
}

static void format_price(char* dst, int price) {
    char tmp[12];
    if (price >= 1000000) siprintf(tmp, "%dM", price / 1000000);
    else if (price >= 1000) {
        if (price % 1000 == 0) siprintf(tmp, "%dk", price / 1000);
        else siprintf(tmp, "%d.%dk", price / 1000, (price % 1000) / 100);
    } else siprintf(tmp, "%dc", price);
    strcpy(dst, tmp);
}

static void main_menu_activate(int index) {
    switch (index) {
        case 0: request_play(); break;
        case 1: menu_open(SCREEN_HANGAR); break;
        case 2: menu_open(SCREEN_SETTINGS); break; // now UPGRADES
        case 3: menu_open(SCREEN_CONTROLS); break;
        case 4: menu_open(SCREEN_CREDITS); break;
        default: break;
    }
}

static void update_main_menu(void) {
    const int count = 5;
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
                menu_static_invalidate();
            }
            return;
        }
        for (int i = 0; i < 5; i++) {
            int idx = s_shop_scroll[cat] + i;
            if (idx >= count) break;
            if (in_rect(tx, ty, 6, 34 + i * 21, list_w - 4, 19)) {
                if (s_shop_selected[cat] == idx) hangar_activate();
                else hangar_select_item(idx);
                return;
            }
        }
        if (in_rect(tx, ty, 4, 31, list_w, 12) && s_shop_scroll[cat] > 0) {
            s_shop_scroll[cat]--;
            menu_static_invalidate();
            return;
        }
        if (in_rect(tx, ty, 4, 132, list_w, 12) && s_shop_scroll[cat] + 5 < count) {
            s_shop_scroll[cat]++;
            menu_static_invalidate();
            return;
        }
        if (in_rect(tx, ty, right_x + 4, 116, right_w - 8, 24)) {
            hangar_activate();
            return;
        }
        if (ty >= 144) {
            save_write();
            menu_open(SCREEN_MAIN_MENU);
            return;
        }
    }

    if (key_hit(KEY_L)) {
        s_shop_category = (s_shop_category + SHOP_CAT_COUNT - 1) % SHOP_CAT_COUNT;
        menu_static_invalidate(); return;
    }
    if (key_hit(KEY_R)) {
        s_shop_category = (s_shop_category + 1) % SHOP_CAT_COUNT;
        menu_static_invalidate(); return;
    }
    if (key_hit(KEY_LEFT)) {
        s_shop_category = (s_shop_category + SHOP_CAT_COUNT - 1) % SHOP_CAT_COUNT;
        menu_static_invalidate(); return;
    }
    if (key_hit(KEY_RIGHT)) {
        s_shop_category = (s_shop_category + 1) % SHOP_CAT_COUNT;
        menu_static_invalidate(); return;
    }

    if (key_hit(KEY_UP)) {
        s_shop_selected[cat] = (s_shop_selected[cat] + count - 1) % count;
        int sel = s_shop_selected[cat];
        if (sel < s_shop_scroll[cat]) s_shop_scroll[cat] = sel;
        else if (sel >= s_shop_scroll[cat] + 5) s_shop_scroll[cat] = sel - 4;
        menu_static_invalidate();
    }
    if (key_hit(KEY_DOWN)) {
        s_shop_selected[cat] = (s_shop_selected[cat] + 1) % count;
        int sel = s_shop_selected[cat];
        if (sel < s_shop_scroll[cat]) s_shop_scroll[cat] = sel;
        else if (sel >= s_shop_scroll[cat] + 5) s_shop_scroll[cat] = sel - 4;
        menu_static_invalidate();
    }

    if (key_hit(KEY_A)) hangar_activate();

    if (key_hit(KEY_START)) { save_write(); request_play(); }
    if (key_hit(KEY_B)) { save_write(); menu_open(SCREEN_MAIN_MENU); }
}

static void upgrades_ensure_visible(void) {
    if (s_upg_selected < s_upg_scroll) s_upg_scroll = s_upg_selected;
    else if (s_upg_selected >= s_upg_scroll + 5) s_upg_scroll = s_upg_selected - 4;
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
    const int count = NUM_UPGRADES; // 8

    int tx, ty;
    if (consume_tap(&tx, &ty)) {
        int list_w = shop_list_width();
        int right_x = 4 + list_w + 2;
        int right_w = SCREEN_WIDTH - 4 - right_x;
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
        if (in_rect(tx, ty, 4, 18, list_w, 14) && s_upg_scroll > 0) {
            s_upg_scroll--;
            menu_static_invalidate();
            return;
        }
        if (in_rect(tx, ty, 4, 126, list_w, 14) && s_upg_scroll + 5 < count) {
            s_upg_scroll++;
            menu_static_invalidate();
            return;
        }
        if (in_rect(tx, ty, right_x + 4, 94, right_w - 8, 24)) {
            upgrades_activate();
            return;
        }
        if (ty >= 144) {
            save_write();
            menu_open(SCREEN_MAIN_MENU);
            return;
        }
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
static void update_credits(void) {
    int tx, ty;
    if (consume_tap(&tx, &ty)) { menu_open(SCREEN_MAIN_MENU); return; }
    if (key_hit(KEY_A) || key_hit(KEY_B) || key_hit(KEY_START)) menu_open(SCREEN_MAIN_MENU);
}

#ifdef PLATFORM_HOST
static const char* s_mode_titles[3] = { "WAVES", "ENDLESS", "OVERDRIVE" };
static const char* s_mode_lines[3] = {
    "Clear waves. Classic run.",
    "No waves. Random hunters.",
    "90s score rush. Max chaos."
};

static int mode_card_y(int i) { return 28 + i * 36; }

static void update_mode_select(void) {
    const int count = 3;
    int tx, ty;
    if (consume_tap(&tx, &ty)) {
        int card_w = 220;
        int card_x = (SCREEN_WIDTH - card_w) / 2;
        for (int i = 0; i < count; i++) {
            if (in_rect(tx, ty, card_x, mode_card_y(i), card_w, 32)) {
                s_menu_selected = i;
                launch_mode((GameMode)i);
                return;
            }
        }
        if (ty >= 144) {
            menu_open(SCREEN_MAIN_MENU);
            return;
        }
    }
    if (key_hit(KEY_UP)) s_menu_selected = (s_menu_selected + count - 1) % count;
    if (key_hit(KEY_DOWN)) s_menu_selected = (s_menu_selected + 1) % count;
    if (key_hit(KEY_A) || key_hit(KEY_START)) launch_mode((GameMode)s_menu_selected);
    if (key_hit(KEY_B)) menu_open(SCREEN_MAIN_MENU);
}
#endif

static void pause_activate(int index) {
    switch (index) {
        case 0: s_current_screen = SCREEN_PLAYING; break;
        case 1: save_write(); game_start(); s_current_screen = SCREEN_PLAYING; break;
        case 2: save_write(); menu_open(SCREEN_MAIN_MENU); break;
        default: break;
    }
}

static void update_paused(void) {
    const int count = 3;
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
    switch (index) {
        case 0: game_start(); s_current_screen = SCREEN_PLAYING; break;
        case 1: menu_open(SCREEN_HANGAR); break;
        case 2: menu_open(SCREEN_MAIN_MENU); break;
        default: break;
    }
}

static void update_game_over(void) {
    const int count = 3;
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
        case SCREEN_CREDITS:   starfield_update(); update_credits(); break;
#ifdef PLATFORM_HOST
        case SCREEN_MODE_SELECT: starfield_update(); update_mode_select(); break;
#endif
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
    gfx_draw_text(card_x + 6, card_y + 13, "Cyber Mk I", PAL_TEXT_WHITE);
    gfx_fill_rect(card_x + 4, card_y + 45, card_w - 8, 1, 20);
    gfx_draw_text(card_x + 6, card_y + 49, "PAINT", PAL_TEXT_CYAN);
    gfx_draw_text(card_x + 36, card_y + 49, gfx_get_accent_name(g_settings.accent_index), PAL_TEXT_WHITE);
    gfx_draw_text(card_x + 6, card_y + 59, "TRAIL", PAL_TEXT_CYAN);
    gfx_draw_text(card_x + 36, card_y + 59, gfx_get_trail_name(g_settings.trail_index), PAL_TEXT_WHITE);
    gfx_draw_text(card_x + 6, card_y + 69, "RIG", PAL_TEXT_CYAN);
    gfx_draw_text(card_x + 36, card_y + 69, gfx_get_weapon_name(g_settings.weapon_rig), PAL_TEXT_WHITE);
    gfx_draw_text(card_x + 6, card_y + 79, "LASER", PAL_TEXT_CYAN);
    gfx_draw_text(card_x + 36, card_y + 79, gfx_get_laser_name(g_settings.laser_index), PAL_TEXT_WHITE);
    gfx_draw_text(card_x + 6, card_y + 89, "COINS", PAL_TEXT_GOLD);
    char buf[16]; siprintf(buf, "%u", (unsigned int)g_settings.coins);
    gfx_draw_text(card_x + 36, card_y + 89, buf, PAL_TEXT_WHITE);
    gfx_draw_text(card_x + 6, card_y + 99, "BEST", PAL_TEXT_GOLD);
    siprintf(buf, "%06u", (unsigned int)g_settings.high_score);
    gfx_draw_text(card_x + 36, card_y + 99, buf, PAL_TEXT_WHITE);
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
    int accent = g_settings.accent_index;
    if (accent < 0 || accent >= NUM_ACCENTS) accent = 1;
    gfx_draw_ship(ship_x, ship_y, accent, s_anim_frame);
    draw_preview_engine_trail(ship_x, ship_y, g_settings.trail_index);
}

static void render_main_menu_static(void) {
    starfield_draw_base(0, 0);
    gfx_draw_text(10, 8, "SPACE UNLIMITED", PAL_TEXT_CYAN);
    gfx_draw_text(10, 18, "Recharged", PAL_TEXT_WHITE);
    gfx_fill_rect(10, 28, 45, 1, PAL_TEXT_CYAN);
    gfx_draw_text(10, 32, "GBA Edition", 17);

    const char* items[] = { "Play", "Shop", "Upgrades", "Controls", "Credits" };
    int start_y = 44; int step_y = 19;
    for (int i = 0; i < 5; i++) gfx_draw_button(10, start_y + i * step_y, 90, 16, items[i], false);

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
        (const char*[]){"Play","Shop","Upgrades","Controls","Credits"}[s_menu_selected], true);
    int card_w = 126;
    int card_x = SCREEN_WIDTH - card_w - 6;
    draw_ship_preview_dynamic(card_x, 10, card_w);
}

static void render_hangar_static(void) {
    starfield_draw_base(0, 0);
    gfx_draw_text(6, 4, "UPGRADE HANGAR", PAL_TEXT_CYAN);
    char coin_buf[24]; siprintf(coin_buf, "$%u COINS", (unsigned int)g_settings.coins);
    int coin_x = SCREEN_WIDTH - 6 - (int)strlen(coin_buf) * 6;
    gfx_draw_text(coin_x, 4, coin_buf, PAL_TEXT_GOLD);
    gfx_fill_rect(4, 15, SCREEN_WIDTH - 8, 1, 20);

    const char* tab_names[4] = { "PAINTS", "TRAILS", "WEAPONS", "LASERS" };
    for (int t = 0; t < 4; t++) {
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
    int scroll = s_shop_scroll[cat];

    int list_w = 116 + (SCREEN_WIDTH - 240) / 2;
    int right_x = 4 + list_w + 2;
    int right_w = SCREEN_WIDTH - 4 - right_x;

    for (int i = 0; i < 5; i++) {
        int item_idx = scroll + i;
        if (item_idx >= count) break;
        int row_y = 34 + i * 21;
        bool is_sel = (item_idx == selected);
        u8 border = is_sel ? PAL_TEXT_CYAN : 20;
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
        }
        if (is_sel) { gfx_draw_char(9, row_y + 6, '>', PAL_TEXT_CYAN); gfx_draw_text(16, row_y + 6, name_buf, PAL_TEXT_WHITE); }
        else gfx_draw_text(10, row_y + 6, name_buf, PAL_TEXT_CYAN);
        int b_x = 4 + list_w - 6 - (int)strlen(badge_buf) * 6;
        gfx_draw_text(b_x, row_y + 6, badge_buf, badge_col);
    }
    if (scroll > 0) gfx_draw_char(4 + list_w - 10, 32, '^', PAL_TEXT_CYAN);
    if (scroll + 5 < count) gfx_draw_char(4 + list_w - 10, 134, 'v', PAL_TEXT_CYAN);

    // Right panel preview
    gfx_draw_glass_card(right_x + 2, 33, right_w - 4, 35, 20, PAL_SPACE_BLACK);
    int preview_accent = (cat == 0) ? selected : g_settings.accent_index;
    int preview_trail  = (cat == 1) ? selected : g_settings.trail_index;
    int preview_laser  = (cat == 3) ? selected : g_settings.laser_index;
    if (preview_accent < 0 || preview_accent >= NUM_ACCENTS) preview_accent = 1;
    int ship_x = right_x + (right_w - 20) / 2;
    int ship_y = 47;
    gfx_draw_ship(ship_x, ship_y, preview_accent, s_anim_frame);
    draw_preview_engine_trail(ship_x, ship_y, preview_trail);
    if (cat == 2 || cat == 3) {
        int travel = (s_anim_frame * 2) % 18;
        int laser_center_y = ship_y - 2 - travel;
        if (laser_center_y >= 38) {
            gfx_draw_laser(ship_x + 6, laser_center_y, false, preview_laser, s_anim_frame, false);
            gfx_draw_laser(ship_x + 14, laser_center_y, false, preview_laser, s_anim_frame, false);
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
    }

    gfx_draw_text_centered(right_x, 73, right_w, full_name, PAL_TEXT_WHITE);
    if (is_equipped) { siprintf(status_buf, "[EQUIPPED]"); status_col = PAL_TEXT_GREEN; }
    else if (is_owned) { siprintf(status_buf, "[OWNED]"); status_col = PAL_TEXT_CYAN; }
    else { char pbuf[16]; format_price(pbuf, item_price); siprintf(status_buf, "COST: %s", pbuf); status_col = PAL_TEXT_GOLD; }
    gfx_draw_text_centered(right_x, 83, right_w, status_buf, status_col);
    gfx_draw_text_centered(right_x, 94, right_w, desc1, PAL_TEXT_CYAN);
    gfx_draw_text_centered(right_x, 104, right_w, desc2, 17);

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
        bool can_afford = (g_settings.coins >= (u32)item_price);
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
    char coin_buf[24]; siprintf(coin_buf, "$%u COINS", (unsigned int)g_settings.coins);
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
    int count = NUM_UPGRADES;
    int selected = s_upg_selected;
    int scroll = s_upg_scroll;

    int list_w = 116 + (SCREEN_WIDTH - 240) / 2;
    int right_x = 4 + list_w + 2;
    int right_w = SCREEN_WIDTH - 4 - right_x;

    // Left list 5 visible
    for (int i = 0; i < 5; i++) {
        int idx = scroll + i;
        if (idx >= count) break;
        int row_y = 21 + i * 21;
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
    if (scroll > 0) gfx_draw_char(4 + list_w - 10, 19, '^', PAL_TEXT_CYAN);
    if (scroll + 5 < count) gfx_draw_char(4 + list_w - 10, 132, 'v', PAL_TEXT_CYAN);

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
        case UPG_ENGINE: limit_info = "Max 2x speed!"; break;
        case UPG_FIRE_RATE: limit_info = "Start 2/sec MAX 10/sec"; break;
        case UPG_DAMAGE: limit_info = "Start 1dmg Max 6dmg+"; break;
        case UPG_SHIELD: limit_info = "Cap 6 shields"; break;
        case UPG_HULL: limit_info = "Cap 7 lives - hard start 2"; break;
        case UPG_DASH: limit_info = "CD 1.4s -> 0.4s"; break;
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
        bool can = g_settings.coins >= (u32)price;
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
    gfx_draw_ship(ship_x, ship_y, g_settings.accent_index, s_anim_frame);
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
    gfx_draw_text(12, 60, "B / R: Dash burst", PAL_TEXT_WHITE);
    gfx_draw_text(12, 72, "START: Pause", PAL_TEXT_WHITE);
    gfx_draw_text(12, 84, "SELECT: Reset", PAL_TEXT_WHITE);
    gfx_draw_text(12, 100, "Starter: Slow 0.7x", 17);
    gfx_draw_text(12, 110, "2 bullets/sec only!", 17);
    gfx_draw_glass_card(right_x, 20, right_w, 120, PAL_BTN_BORDER, 14);
    gfx_draw_text(right_x + 4, 24, "UPGRADE GUIDE", PAL_TEXT_CYAN);
    gfx_draw_text(right_x + 4, 36, "Engine: 0.7x->2x", PAL_TEXT_WHITE);
    gfx_draw_text(right_x + 4, 48, "Fire: 2/s->10/s", PAL_TEXT_GOLD);
    gfx_draw_text(right_x + 4, 60, "Start Single weak", PAL_TEXT_GREEN);
    gfx_draw_text(right_x + 4, 72, "Final Nova = GOD!", PAL_TEXT_GOLD);
    gfx_draw_text(right_x + 4, 86, "Wave4 = HARD!", PAL_TEXT_RED);
    gfx_draw_text(right_x + 4, 98, "Omega Prism ultimate", PAL_TEXT_CYAN);
    gfx_draw_text(right_x + 4, 110, "Shop+Rigs 8 total", PAL_TEXT_WHITE);
    gfx_draw_text_centered(0, 146, SCREEN_WIDTH, "Press A or B to return", PAL_TEXT_WHITE);
}
static void render_controls_dynamic(void) { menu_draw_base(); }

static void render_credits_static(void) {
    starfield_draw_base(0, 0);
    int cw = 200 + (SCREEN_WIDTH - 240);
    int cx = (SCREEN_WIDTH - cw) / 2;
    gfx_draw_glass_card(cx, 16, cw, 124, PAL_BTN_BORDER, 14);
    gfx_draw_text_centered(cx, 22, cw, "SPACE UNLIMITED", PAL_TEXT_CYAN);
    gfx_draw_text_centered(cx, 34, cw, "Recharged: GBA Edition", PAL_TEXT_WHITE);
    gfx_fill_rect(cx + 10, 46, cw - 20, 1, 20);
    gfx_draw_text_centered(cx, 54, cw, "Upgrades Overhaul v4", 17);
    gfx_draw_text_centered(cx, 66, cw, "8 Rigs 12 Lasers", PAL_TEXT_WHITE);
    gfx_draw_text_centered(cx, 78, cw, "5 Level Caps 2x Speed", PAL_TEXT_CYAN);
    gfx_draw_text_centered(cx, 90, cw, "Nova Annihilator Final", PAL_TEXT_GOLD);
    gfx_draw_text_centered(cx, 102, cw, "W4 Hard Core", PAL_TEXT_GREEN);
    gfx_draw_text_centered(0, 146, SCREEN_WIDTH, "Press A or B to return", PAL_TEXT_WHITE);
}
static void render_credits_dynamic(void) { menu_draw_base(); }

#ifdef PLATFORM_HOST
static void render_mode_select_static(void) {
    starfield_draw_base(0, 0);
    gfx_draw_text(10, 6, "SELECT MODE", PAL_TEXT_CYAN);
    gfx_fill_rect(10, 16, SCREEN_WIDTH - 20, 1, 20);
    gfx_draw_text_centered(0, 148, SCREEN_WIDTH, "Tap a mode    BACK to cancel", PAL_TEXT_WHITE);
}

static void render_mode_select_dynamic(void) {
    menu_draw_base();
    int card_w = 220;
    int card_x = (SCREEN_WIDTH - card_w) / 2;
    for (int i = 0; i < 3; i++) {
        bool sel = (s_menu_selected == i);
        u8 border = sel ? PAL_TEXT_CYAN : PAL_BTN_BORDER;
        u8 bg = sel ? PAL_BTN_HOVER : PAL_BTN_BG;
        int y = mode_card_y(i);
        gfx_draw_glass_card(card_x, y, card_w, 32, border, bg);
        gfx_draw_text_centered(card_x, y + 6, card_w, s_mode_titles[i], sel ? PAL_TEXT_WHITE : PAL_TEXT_CYAN);
        gfx_draw_text_centered(card_x, y + 18, card_w, s_mode_lines[i], sel ? PAL_TEXT_GOLD : 17);
    }
}
#endif

static void render_paused(void) {
    game_draw();
    int w = 110; int h = 88;
    int x = (SCREEN_WIDTH - w) / 2;
    int y = (SCREEN_HEIGHT - h) / 2;
    gfx_draw_glass_card(x, y, w, h, PAL_TEXT_WHITE, 15);
    gfx_draw_text_centered(x, y + 6, w, "PAUSED", PAL_TEXT_CYAN);
    const char* opts[] = { "Resume", "Restart", "Main Menu" };
    for (int i = 0; i < 3; i++) gfx_draw_button(x + 10, y + 20 + i * 18, w - 20, 15, opts[i], s_menu_selected == i);
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
    siprintf(buf, "Coins: $%u", (unsigned int)g_settings.coins);
    gfx_draw_text_centered(x, y + 28, w, buf, PAL_TEXT_GOLD);
    if (g_game.is_new_high_score) gfx_draw_badge(x + (w - 74) / 2, y + 39, "NEW BEST!", PAL_TEXT_GOLD);
    else { siprintf(buf, "Best:  %06u", (unsigned int)g_settings.high_score); gfx_draw_text_centered(x, y + 40, w, buf, 17); }
    const char* opts[] = { "Retry", "Shop", "Main Menu" };
    for (int i = 0; i < 3; i++) gfx_draw_button(x + 10, y + 54 + i * 18, w - 20, 15, opts[i], s_menu_selected == i);
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
        case SCREEN_CREDITS:
            if (!s_static_valid) { menu_static_begin(); render_credits_static(); menu_static_end(); }
            render_credits_dynamic(); break;
#ifdef PLATFORM_HOST
        case SCREEN_MODE_SELECT:
            if (!s_static_valid) { menu_static_begin(); render_mode_select_static(); menu_static_end(); }
            render_mode_select_dynamic(); break;
#endif
        case SCREEN_PLAYING: game_draw(); break;
        case SCREEN_PAUSED: render_paused(); break;
        case SCREEN_GAME_OVER: render_game_over(); break;
    }
}
