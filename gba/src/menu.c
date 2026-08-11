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

// Shop state
static int s_shop_category = 0; // 0=PAINTS, 1=TRAILS, 2=WEAPONS, 3=LASERS, 4=TECH
static int s_shop_selected[5] = { 0, 0, 0, 0, 0 };
static int s_shop_scroll[5]   = { 0, 0, 0, 0, 0 };

static int s_shop_msg_timer = 0;
static char s_shop_msg[36] = {0};
static u8 s_shop_msg_col = PAL_TEXT_GOLD;

static bool s_static_valid = false;

static void menu_static_invalidate(void) {
    s_static_valid = false;
}

static void menu_static_begin(void) {
    gfx_set_target(gfx_static_layer);
}

static void menu_static_end(void) {
    gfx_set_target(NULL);
    s_static_valid = true;
}

static void menu_draw_base(void) {
    gfx_apply_static();
    starfield_draw_stars(0, 0);
}

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
    menu_static_invalidate();
    if (screen == SCREEN_MAIN_MENU || screen == SCREEN_HANGAR || screen == SCREEN_SETTINGS || screen == SCREEN_CONTROLS || screen == SCREEN_CREDITS) {
        audio_play_bgm(BGM_MENU);
    }
}

static void shop_set_msg(const char* msg, u8 col) {
    strncpy(s_shop_msg, msg, sizeof(s_shop_msg)-1);
    s_shop_msg[sizeof(s_shop_msg)-1] = '\0';
    s_shop_msg_col = col;
    s_shop_msg_timer = 90; // 1.5s
}

static int shop_get_category_count(int cat) {
    switch (cat) {
        case 0: return NUM_ACCENTS;
        case 1: return NUM_TRAILS;
        case 2: return NUM_RIGS;
        case 3: return NUM_LASERS;
        case 4: return NUM_UPGRADES;
        default: return 0;
    }
}

static void format_price(char* dst, int price) {
    if (price >= 1000000) {
        siprintf(dst, "%dM", price / 1000000);
    } else if (price >= 100000) {
        siprintf(dst, "%dk", price / 1000);
    } else if (price >= 10000) {
        siprintf(dst, "%dk", price / 1000);
    } else if (price >= 1000) {
        if (price % 1000 == 0) siprintf(dst, "%dk", price / 1000);
        else siprintf(dst, "%d.%dk", price / 1000, (price % 1000) / 100);
    } else {
        siprintf(dst, "%dc", price);
    }
}

static void update_main_menu(void) {
    const int count = 5;
    if (key_hit(KEY_UP)) {
        s_menu_selected = (s_menu_selected + count - 1) % count;
    }
    if (key_hit(KEY_DOWN)) {
        s_menu_selected = (s_menu_selected + 1) % count;
    }
    if (key_hit(KEY_A) || key_hit(KEY_START)) {
        switch (s_menu_selected) {
            case 0:
                game_start();
                s_current_screen = SCREEN_PLAYING;
                break;
            case 1:
                menu_open(SCREEN_HANGAR);
                break;
            case 2:
                menu_open(SCREEN_SETTINGS);
                break;
            case 3:
                menu_open(SCREEN_CONTROLS);
                break;
            case 4:
                menu_open(SCREEN_CREDITS);
                break;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────
// HANGAR / SHOP UPDATE
// ──────────────────────────────────────────────────────────────────────────
static void update_hangar(void) {
    if (s_shop_msg_timer > 0) s_shop_msg_timer--;

    int cat = s_shop_category;
    int count = shop_get_category_count(cat);

    // Switch Category Tabs via L/R triggers OR Left/Right
    if (key_hit(KEY_L)) {
        s_shop_category = (s_shop_category + 4) % 5;
        menu_static_invalidate();
        return;
    }
    if (key_hit(KEY_R)) {
        s_shop_category = (s_shop_category + 1) % 5;
        menu_static_invalidate();
        return;
    }
    if (key_hit(KEY_LEFT)) {
        s_shop_category = (s_shop_category + 4) % 5;
        menu_static_invalidate();
        return;
    }
    if (key_hit(KEY_RIGHT)) {
        s_shop_category = (s_shop_category + 1) % 5;
        menu_static_invalidate();
        return;
    }

    // Browse items within current category
    if (key_hit(KEY_UP)) {
        s_shop_selected[cat] = (s_shop_selected[cat] + count - 1) % count;
        // Keep in view (5 visible)
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

    // Action (A button): Buy or Equip
    if (key_hit(KEY_A)) {
        int sel = s_shop_selected[cat];
        switch (cat) {
            case 0: { // Paints
                if (shop_is_accent_owned(sel)) {
                    shop_equip_accent(sel);
                    shop_set_msg("EQUIPPED PAINT!", PAL_TEXT_GREEN);
                } else {
                    if (shop_try_purchase_accent(sel)) {
                        shop_set_msg("PURCHASED PAINT!", PAL_TEXT_GOLD);
                        audio_play_sfx(SFX_PICKUP);
                    } else {
                        char tmp[32];
                        char pbuf[16];
                        format_price(pbuf, shop_get_accent_price(sel));
                        siprintf(tmp, "NEED %s COINS!", pbuf);
                        shop_set_msg(tmp, PAL_TEXT_RED);
                    }
                }
                break;
            }
            case 1: { // Trails
                if (shop_is_trail_owned(sel)) {
                    shop_equip_trail(sel);
                    shop_set_msg("EQUIPPED TRAIL!", PAL_TEXT_GREEN);
                } else {
                    if (shop_try_purchase_trail(sel)) {
                        shop_set_msg("PURCHASED TRAIL!", PAL_TEXT_GOLD);
                        audio_play_sfx(SFX_PICKUP);
                    } else {
                        char tmp[32];
                        char pbuf[16];
                        format_price(pbuf, shop_get_trail_price(sel));
                        siprintf(tmp, "NEED %s COINS!", pbuf);
                        shop_set_msg(tmp, PAL_TEXT_RED);
                    }
                }
                break;
            }
            case 2: { // Weapon Rigs
                WeaponRig rig = (WeaponRig)sel;
                if (shop_is_rig_owned(rig)) {
                    shop_equip_rig(rig);
                    shop_set_msg("EQUIPPED WEAPON!", PAL_TEXT_GREEN);
                } else {
                    if (shop_try_purchase_rig(rig)) {
                        shop_set_msg("PURCHASED WEAPON!", PAL_TEXT_GOLD);
                        audio_play_sfx(SFX_PICKUP);
                    } else {
                        char tmp[32];
                        char pbuf[16];
                        format_price(pbuf, shop_get_rig_price(rig));
                        siprintf(tmp, "NEED %s COINS!", pbuf);
                        shop_set_msg(tmp, PAL_TEXT_RED);
                    }
                }
                break;
            }
            case 3: { // Laser Crystals
                if (shop_is_laser_owned(sel)) {
                    shop_equip_laser(sel);
                    shop_set_msg("EQUIPPED LASER!", PAL_TEXT_GREEN);
                } else {
                    if (shop_try_purchase_laser(sel)) {
                        shop_set_msg("PURCHASED LASER!", PAL_TEXT_GOLD);
                        audio_play_sfx(SFX_PICKUP);
                    } else {
                        char tmp[32];
                        char pbuf[16];
                        format_price(pbuf, shop_get_laser_price(sel));
                        siprintf(tmp, "NEED %s COINS!", pbuf);
                        shop_set_msg(tmp, PAL_TEXT_RED);
                    }
                }
                break;
            }
            case 4: { // Tech Upgrades
                UpgradeType upg = (UpgradeType)sel;
                int current_lv = shop_get_upgrade_level(upg);
                if (current_lv >= 3) {
                    shop_set_msg("MAX LEVEL REACHED!", PAL_TEXT_GOLD);
                } else {
                    if (shop_try_purchase_upgrade(upg)) {
                        shop_set_msg("TECH UPGRADED!", PAL_TEXT_GOLD);
                        audio_play_sfx(SFX_PICKUP);
                    } else {
                        char tmp[32];
                        char pbuf[16];
                        format_price(pbuf, shop_get_upgrade_price(upg, current_lv));
                        siprintf(tmp, "NEED %s COINS!", pbuf);
                        shop_set_msg(tmp, PAL_TEXT_RED);
                    }
                }
                break;
            }
        }
        menu_static_invalidate();
    }

    if (key_hit(KEY_START)) {
        save_write();
        game_start();
        s_current_screen = SCREEN_PLAYING;
    }

    if (key_hit(KEY_B)) {
        save_write();
        menu_open(SCREEN_MAIN_MENU);
    }
}

static void update_settings(void) {
    const int count = 6;
    if (key_hit(KEY_UP)) {
        s_menu_selected = (s_menu_selected + count - 1) % count;
    }
    if (key_hit(KEY_DOWN)) {
        s_menu_selected = (s_menu_selected + 1) % count;
    }

    int delta = 0;
    if (key_hit(KEY_LEFT))  delta = -1;
    if (key_hit(KEY_RIGHT)) delta = 1;

    if (delta != 0) {
        switch (s_menu_selected) {
            case 0:
                g_settings.difficulty = (Difficulty)((g_settings.difficulty + delta + 3) % 3);
                save_write();
                menu_static_invalidate();
                break;
            case 1:
                g_settings.music_volume += delta * 10;
                if (g_settings.music_volume < 0) g_settings.music_volume = 0;
                if (g_settings.music_volume > 100) g_settings.music_volume = 100;
                save_write();
                menu_static_invalidate();
                break;
            case 2:
                g_settings.sfx_volume += delta * 10;
                if (g_settings.sfx_volume < 0) g_settings.sfx_volume = 0;
                if (g_settings.sfx_volume > 100) g_settings.sfx_volume = 100;
                save_write();
                menu_static_invalidate();
                break;
            case 3:
                g_settings.screen_shake = !g_settings.screen_shake;
                save_write();
                menu_static_invalidate();
                break;
        }
    }

    if (key_hit(KEY_A)) {
        if (s_menu_selected == 0) {
            g_settings.difficulty = (Difficulty)((g_settings.difficulty + 1) % 3);
            save_write();
            menu_static_invalidate();
        } else if (s_menu_selected == 3) {
            g_settings.screen_shake = !g_settings.screen_shake;
            save_write();
            menu_static_invalidate();
        } else if (s_menu_selected == 4) {
            g_settings.high_score = 0;
            save_write();
            menu_static_invalidate();
        } else if (s_menu_selected == 5) {
            menu_open(SCREEN_MAIN_MENU);
        }
    }

    if (key_hit(KEY_B)) {
        menu_open(SCREEN_MAIN_MENU);
    }
}

static void update_controls(void) {
    if (key_hit(KEY_A) || key_hit(KEY_B) || key_hit(KEY_START)) {
        menu_open(SCREEN_MAIN_MENU);
    }
}

static void update_credits(void) {
    if (key_hit(KEY_A) || key_hit(KEY_B) || key_hit(KEY_START)) {
        menu_open(SCREEN_MAIN_MENU);
    }
}

static void update_paused(void) {
    const int count = 3;
    if (key_hit(KEY_UP)) {
        s_menu_selected = (s_menu_selected + count - 1) % count;
    }
    if (key_hit(KEY_DOWN)) {
        s_menu_selected = (s_menu_selected + 1) % count;
    }

    if (key_hit(KEY_START) || key_hit(KEY_B)) {
        s_current_screen = SCREEN_PLAYING;
        return;
    }

    if (key_hit(KEY_A)) {
        switch (s_menu_selected) {
            case 0: // Resume
                s_current_screen = SCREEN_PLAYING;
                break;
            case 1: // Restart
                save_write();
                game_start();
                s_current_screen = SCREEN_PLAYING;
                break;
            case 2: // Main Menu
                save_write();
                menu_open(SCREEN_MAIN_MENU);
                break;
        }
    }
}

static void update_game_over(void) {
    const int count = 3;
    if (key_hit(KEY_UP)) {
        s_menu_selected = (s_menu_selected + count - 1) % count;
    }
    if (key_hit(KEY_DOWN)) {
        s_menu_selected = (s_menu_selected + 1) % count;
    }

    if (key_hit(KEY_A) || key_hit(KEY_START)) {
        switch (s_menu_selected) {
            case 0: // Retry
                game_start();
                s_current_screen = SCREEN_PLAYING;
                break;
            case 1: // Shop
                menu_open(SCREEN_HANGAR);
                break;
            case 2: // Main Menu
                menu_open(SCREEN_MAIN_MENU);
                break;
        }
    }
}

void menu_update(void) {
    s_anim_frame++;
    key_poll();

    switch (s_current_screen) {
        case SCREEN_MAIN_MENU:
            starfield_update();
            update_main_menu();
            break;
        case SCREEN_HANGAR:
            starfield_update();
            update_hangar();
            break;
        case SCREEN_SETTINGS:
            starfield_update();
            update_settings();
            break;
        case SCREEN_CONTROLS:
            starfield_update();
            update_controls();
            break;
        case SCREEN_CREDITS:
            starfield_update();
            update_credits();
            break;
        case SCREEN_PLAYING:
            if (key_hit(KEY_START)) {
                s_current_screen = SCREEN_PAUSED;
                s_menu_selected = 0;
            } else {
                game_update();
                if (g_game.is_game_over) {
                    s_current_screen = SCREEN_GAME_OVER;
                    s_menu_selected = 0;
                }
            }
            break;
        case SCREEN_PAUSED:
            update_paused();
            break;
        case SCREEN_GAME_OVER:
            update_game_over();
            break;
    }
}

// ──────────────────────────────────────────────────────────────────────────
// RENDERING
// ──────────────────────────────────────────────────────────────────────────

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
    char buf[16];
    siprintf(buf, "%u", (unsigned int)g_settings.coins);
    gfx_draw_text(card_x + 36, card_y + 89, buf, PAL_TEXT_WHITE);

    gfx_draw_text(card_x + 6, card_y + 99, "BEST", PAL_TEXT_GOLD);
    siprintf(buf, "%06u", (unsigned int)g_settings.high_score);
    gfx_draw_text(card_x + 36, card_y + 99, buf, PAL_TEXT_WHITE);
}

static void draw_preview_engine_trail(int ship_x, int ship_y, int trail_idx) {
    if (trail_idx == 7) {
        /* Keep several hues visible together in the hangar so Rainbow Trail's
         * real in-game spectrum is clear before purchase. */
        for (int row = 0; row < 4; row++) {
            u8 col = gfx_get_rainbow_color((s_anim_frame >> 2) + row * 2);
            int w = (row < 2) ? 4 : 2;
            gfx_fill_rect(ship_x + 10 - w / 2, ship_y + 16 + row, w, 1, col);
        }
    } else {
        u8 trail_col = gfx_get_trail_color_animated(trail_idx, s_anim_frame);
        if ((s_anim_frame & 4) == 0) {
            gfx_fill_rect(ship_x + 8, ship_y + 16, 4, 3, trail_col);
        } else {
            gfx_fill_rect(ship_x + 7, ship_y + 16, 6, 2, trail_col);
        }
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

    const char* items[] = { "Play", "Shop", "Settings", "Controls", "Credits" };
    int start_y = 44;
    int step_y = 19;
    for (int i = 0; i < 5; i++) {
        gfx_draw_button(10, start_y + i * step_y, 90, 16, items[i], false);
    }

    draw_ship_preview_static(108, 10, 126, 116);

    gfx_draw_glass_card(108, 128, 126, 24, PAL_BTN_BORDER, 14);
    gfx_draw_text_centered(108, 132, 126, "D-PAD Navigate", PAL_TEXT_WHITE);
    gfx_draw_text_centered(108, 140, 126, "A Select", PAL_TEXT_CYAN);
}

static void render_main_menu_dynamic(void) {
    menu_draw_base();

    const char* items[] = { "Play", "Shop", "Settings", "Controls", "Credits" };
    gfx_draw_button(10, 44 + s_menu_selected * 19, 90, 16, items[s_menu_selected], true);
    draw_ship_preview_dynamic(108, 10, 126);
}

// ──────────────────────────────────────────────────────────────────────────
// SHOP / HANGAR RENDERER (Clean, spacious, non-cramped layout)
// ──────────────────────────────────────────────────────────────────────────
static void render_hangar_static(void) {
    starfield_draw_base(0, 0);

    // Top Header
    gfx_draw_text(6, 4, "UPGRADE HANGAR", PAL_TEXT_CYAN);

    char coin_buf[24];
    siprintf(coin_buf, "$%u COINS", (unsigned int)g_settings.coins);
    int coin_x = SCREEN_WIDTH - 6 - (int)strlen(coin_buf) * 6;
    gfx_draw_text(coin_x, 4, coin_buf, PAL_TEXT_GOLD);
    gfx_fill_rect(4, 15, SCREEN_WIDTH - 8, 1, 20);

    // Category Tabs (PAINTS, TRAILS, WEAPON, LASERS, TECH)
    const char* tab_names[5] = { "PAINTS", "TRAILS", "WEAPON", "LASERS", "TECH" };
    const int tab_x[5] = { 6, 52, 98, 144, 190 };
    const int tab_w = 44;

    for (int t = 0; t < 5; t++) {
        bool is_active = (t == s_shop_category);
        u8 border = is_active ? PAL_TEXT_CYAN : 20;
        u8 bg = is_active ? PAL_BTN_HOVER : PAL_BTN_BG;
        u8 txt = is_active ? PAL_TEXT_WHITE : PAL_TEXT_CYAN;
        gfx_draw_glass_card(tab_x[t], 17, tab_w, 12, border, bg);
        gfx_draw_text_centered(tab_x[t], 19, tab_w, tab_names[t], txt);
    }

    // Left Panel Card (Catalog List)
    gfx_draw_glass_card(4, 31, 116, 112, PAL_BTN_BORDER, 14);

    // Right Panel Card (Preview & Item Stats)
    gfx_draw_glass_card(122, 31, 114, 112, PAL_BTN_BORDER, 14);
}

static void render_hangar_dynamic(void) {
    menu_draw_base();

    int cat = s_shop_category;
    int count = shop_get_category_count(cat);
    int selected = s_shop_selected[cat];
    int scroll = s_shop_scroll[cat];

    // Draw Left Panel Catalog Rows (5 visible)
    for (int i = 0; i < 5; i++) {
        int item_idx = scroll + i;
        if (item_idx >= count) break;

        int row_y = 34 + i * 21;
        bool is_sel = (item_idx == selected);

        u8 border = is_sel ? PAL_TEXT_CYAN : 20;
        u8 bg = is_sel ? PAL_BTN_HOVER : PAL_BTN_BG;
        gfx_draw_glass_card(6, row_y, 112, 19, border, bg);

        // Get item name & status
        char name_buf[16] = {0};
        char badge_buf[12] = {0};
        u8 badge_col = PAL_TEXT_GOLD;

        switch (cat) {
            case 0: { // Paints
                strncpy(name_buf, gfx_get_accent_name(item_idx), 11);
                bool eq = (g_settings.accent_index == item_idx);
                bool own = shop_is_accent_owned(item_idx);
                if (eq) { strncpy(badge_buf, "[EQ]", sizeof(badge_buf)-1); badge_col = PAL_TEXT_GREEN; }
                else if (own) { strncpy(badge_buf, "OWN", sizeof(badge_buf)-1); badge_col = PAL_TEXT_CYAN; }
                else { format_price(badge_buf, shop_get_accent_price(item_idx)); badge_col = PAL_TEXT_GOLD; }
                break;
            }
            case 1: { // Trails
                strncpy(name_buf, gfx_get_trail_name(item_idx), 11);
                bool eq = (g_settings.trail_index == item_idx);
                bool own = shop_is_trail_owned(item_idx);
                if (eq) { strncpy(badge_buf, "[EQ]", sizeof(badge_buf)-1); badge_col = PAL_TEXT_GREEN; }
                else if (own) { strncpy(badge_buf, "OWN", sizeof(badge_buf)-1); badge_col = PAL_TEXT_CYAN; }
                else { format_price(badge_buf, shop_get_trail_price(item_idx)); badge_col = PAL_TEXT_GOLD; }
                break;
            }
            case 2: { // Weapons
                strncpy(name_buf, gfx_get_weapon_name((WeaponRig)item_idx), 11);
                bool eq = (g_settings.weapon_rig == item_idx);
                bool own = shop_is_rig_owned((WeaponRig)item_idx);
                if (eq) { strncpy(badge_buf, "[EQ]", sizeof(badge_buf)-1); badge_col = PAL_TEXT_GREEN; }
                else if (own) { strncpy(badge_buf, "OWN", sizeof(badge_buf)-1); badge_col = PAL_TEXT_CYAN; }
                else { format_price(badge_buf, shop_get_rig_price((WeaponRig)item_idx)); badge_col = PAL_TEXT_GOLD; }
                break;
            }
            case 3: { // Lasers
                strncpy(name_buf, gfx_get_laser_name(item_idx), 11);
                bool eq = (g_settings.laser_index == item_idx);
                bool own = shop_is_laser_owned(item_idx);
                if (eq) { strncpy(badge_buf, "[EQ]", sizeof(badge_buf)-1); badge_col = PAL_TEXT_GREEN; }
                else if (own) { strncpy(badge_buf, "OWN", sizeof(badge_buf)-1); badge_col = PAL_TEXT_CYAN; }
                else { format_price(badge_buf, shop_get_laser_price(item_idx)); badge_col = PAL_TEXT_GOLD; }
                break;
            }
            case 4: { // Tech Upgrades
                strncpy(name_buf, shop_get_upgrade_name((UpgradeType)item_idx), 11);
                int lv = shop_get_upgrade_level((UpgradeType)item_idx);
                if (lv >= 3) { strncpy(badge_buf, "MAX", sizeof(badge_buf)-1); badge_col = PAL_TEXT_GOLD; }
                else { siprintf(badge_buf, "L%d/3", lv); badge_col = lv > 0 ? PAL_TEXT_GREEN : PAL_TEXT_CYAN; }
                break;
            }
        }

        if (is_sel) {
            gfx_draw_char(9, row_y + 6, '>', PAL_TEXT_CYAN);
            gfx_draw_text(16, row_y + 6, name_buf, PAL_TEXT_WHITE);
        } else {
            gfx_draw_text(10, row_y + 6, name_buf, PAL_TEXT_CYAN);
        }

        int b_x = 114 - (int)strlen(badge_buf) * 6;
        gfx_draw_text(b_x, row_y + 6, badge_buf, badge_col);
    }

    // Scroll indicators
    if (scroll > 0) {
        gfx_draw_char(110, 32, '^', PAL_TEXT_CYAN);
    }
    if (scroll + 5 < count) {
        gfx_draw_char(110, 134, 'v', PAL_TEXT_CYAN);
    }

    // ── Right Panel: Live Ship Preview Chamber ──
    gfx_draw_glass_card(124, 33, 110, 35, 20, PAL_SPACE_BLACK);
    int preview_accent = (cat == 0) ? selected : g_settings.accent_index;
    int preview_trail  = (cat == 1) ? selected : g_settings.trail_index;
    int preview_laser  = (cat == 3) ? selected : g_settings.laser_index;

    if (preview_accent < 0 || preview_accent >= NUM_ACCENTS) preview_accent = 1;
    int ship_x = 124 + (110 - 20) / 2;
    int ship_y = 47;
    gfx_draw_ship(ship_x, ship_y, preview_accent, s_anim_frame);

    draw_preview_engine_trail(ship_x, ship_y, preview_trail);

    // Animated, full-size laser bolts firing upward in weapon/laser tabs.
    // Rainbow Laser uses the same flowing multi-hue renderer as gameplay.
    if (cat == 2 || cat == 3) {
        int travel = (s_anim_frame * 2) % 18;
        int laser_center_y = ship_y - 2 - travel;
        if (laser_center_y >= 38) {
            gfx_draw_laser(ship_x + 6, laser_center_y, false,
                           preview_laser, s_anim_frame, false);
            gfx_draw_laser(ship_x + 14, laser_center_y, false,
                           preview_laser, s_anim_frame, false);
        }
    }

    gfx_fill_rect(126, 70, 106, 1, 20);

    // Right Panel: Item Title & Subtitle
    const char* full_name = "";
    const char* desc1 = "";
    const char* desc2 = "";
    char status_buf[32] = {0};
    u8 status_col = PAL_TEXT_GOLD;

    bool is_owned = false;
    bool is_equipped = false;
    int item_price = 0;

    switch (cat) {
        case 0: // Paints
            full_name = gfx_get_accent_name(selected);
            desc1 = gfx_get_accent_desc(selected);
            desc2 = "Hull skin finish";
            is_owned = shop_is_accent_owned(selected);
            is_equipped = (g_settings.accent_index == selected);
            item_price = shop_get_accent_price(selected);
            break;
        case 1: // Trails
            full_name = gfx_get_trail_name(selected);
            desc1 = gfx_get_trail_desc(selected);
            desc2 = "Drive exhaust wake";
            is_owned = shop_is_trail_owned(selected);
            is_equipped = (g_settings.trail_index == selected);
            item_price = shop_get_trail_price(selected);
            break;
        case 2: // Weapons
            full_name = gfx_get_weapon_name((WeaponRig)selected);
            desc1 = gfx_get_weapon_desc((WeaponRig)selected);
            desc2 = "Primary ordnance";
            is_owned = shop_is_rig_owned((WeaponRig)selected);
            is_equipped = (g_settings.weapon_rig == selected);
            item_price = shop_get_rig_price((WeaponRig)selected);
            break;
        case 3: // Lasers
            full_name = gfx_get_laser_name(selected);
            desc1 = gfx_get_laser_desc(selected);
            desc2 = "Laser crystal core";
            is_owned = shop_is_laser_owned(selected);
            is_equipped = (g_settings.laser_index == selected);
            item_price = shop_get_laser_price(selected);
            break;
        case 4: // Tech Upgrades
            full_name = shop_get_upgrade_name((UpgradeType)selected);
            desc1 = shop_get_upgrade_desc_line1((UpgradeType)selected);
            int cur_lv = shop_get_upgrade_level((UpgradeType)selected);
            desc2 = shop_get_upgrade_desc_line2((UpgradeType)selected, cur_lv);
            if (cur_lv >= 3) {
                is_owned = true;
                item_price = 0;
            } else {
                is_owned = false;
                item_price = shop_get_upgrade_price((UpgradeType)selected, cur_lv);
            }
            break;
    }

    gfx_draw_text_centered(122, 73, 114, full_name, PAL_TEXT_WHITE);

    // Status subtitle line
    if (cat == 4) {
        int cur_lv = shop_get_upgrade_level((UpgradeType)selected);
        if (cur_lv >= 3) {
            siprintf(status_buf, "LEVEL MAX (3/3)");
            status_col = PAL_TEXT_GOLD;
        } else {
            char pbuf[16];
            format_price(pbuf, item_price);
            siprintf(status_buf, "LVL %d/3 - COST: %s", cur_lv, pbuf);
            status_col = PAL_TEXT_GOLD;
        }
    } else {
        if (is_equipped) {
            siprintf(status_buf, "[EQUIPPED]");
            status_col = PAL_TEXT_GREEN;
        } else if (is_owned) {
            siprintf(status_buf, "[OWNED]");
            status_col = PAL_TEXT_CYAN;
        } else {
            char pbuf[16];
            format_price(pbuf, item_price);
            siprintf(status_buf, "COST: %s", pbuf);
            status_col = PAL_TEXT_GOLD;
        }
    }
    gfx_draw_text_centered(122, 83, 114, status_buf, status_col);

    // Description lines
    gfx_draw_text_centered(122, 94, 114, desc1, PAL_TEXT_CYAN);
    gfx_draw_text_centered(122, 104, 114, desc2, 17);

    // Action Button at Bottom Right (x=126, y=118, w=106, h=21)
    if (cat == 4) {
        int cur_lv = shop_get_upgrade_level((UpgradeType)selected);
        if (cur_lv >= 3) {
            gfx_draw_glass_card(126, 118, 106, 21, PAL_TEXT_GOLD, PAL_BTN_HOVER);
            gfx_draw_text_centered(126, 125, 106, "MAX LEVEL", PAL_TEXT_GOLD);
        } else {
            bool can_afford = (g_settings.coins >= (u32)item_price);
            char pbuf[16];
            format_price(pbuf, item_price);
            char btn_text[28];
            if (can_afford) {
                siprintf(btn_text, "[A] UPGRADE %s", pbuf);
                gfx_draw_glass_card(126, 118, 106, 21, PAL_TEXT_GOLD, PAL_BTN_HOVER);
                gfx_draw_text_centered(126, 125, 106, btn_text, PAL_TEXT_GOLD);
            } else {
                siprintf(btn_text, "NEED %s", pbuf);
                gfx_draw_glass_card(126, 118, 106, 21, PAL_TEXT_RED, PAL_BTN_BG);
                gfx_draw_text_centered(126, 125, 106, btn_text, PAL_TEXT_RED);
            }
        }
    } else {
        if (is_equipped) {
            gfx_draw_glass_card(126, 118, 106, 21, PAL_TEXT_GREEN, PAL_BTN_HOVER);
            gfx_draw_text_centered(126, 125, 106, "EQUIPPED", PAL_TEXT_GREEN);
        } else if (is_owned) {
            gfx_draw_glass_card(126, 118, 106, 21, PAL_TEXT_CYAN, PAL_BTN_HOVER);
            gfx_draw_text_centered(126, 125, 106, "[A] EQUIP", PAL_TEXT_WHITE);
        } else {
            bool can_afford = (g_settings.coins >= (u32)item_price);
            char pbuf[16];
            format_price(pbuf, item_price);
            char btn_text[28];
            if (can_afford) {
                siprintf(btn_text, "[A] BUY %s", pbuf);
                gfx_draw_glass_card(126, 118, 106, 21, PAL_TEXT_GOLD, PAL_BTN_HOVER);
                gfx_draw_text_centered(126, 125, 106, btn_text, PAL_TEXT_GOLD);
            } else {
                siprintf(btn_text, "NEED %s", pbuf);
                gfx_draw_glass_card(126, 118, 106, 21, PAL_TEXT_RED, PAL_BTN_BG);
                gfx_draw_text_centered(126, 125, 106, btn_text, PAL_TEXT_RED);
            }
        }
    }

    // Bottom Toast / Navigation Hint
    if (s_shop_msg_timer > 0) {
        int w = (int)strlen(s_shop_msg) * 6 + 12;
        int x = (SCREEN_WIDTH - w) / 2;
        gfx_draw_glass_card(x, 146, w, 12, s_shop_msg_col, 15);
        gfx_draw_text_centered(x, 148, w, s_shop_msg, s_shop_msg_col);
    } else {
        gfx_draw_text_centered(0, 148, SCREEN_WIDTH, "L/R: Tab  D-PAD: Pick  A: Buy/Equip  B: Exit", PAL_TEXT_WHITE);
    }
}

static void render_settings_static(void) {
    starfield_draw_base(0, 0);

    gfx_draw_text(10, 6, "Settings", PAL_TEXT_WHITE);
    gfx_draw_text(10, 16, "Auto-saved to cartridge SRAM", 17);
    gfx_fill_rect(10, 26, SCREEN_WIDTH - 20, 1, 20);

    char buf[32];

    siprintf(buf, "Difficulty: %s", gfx_get_diff_name(g_settings.difficulty));
    gfx_draw_button(10, 32, 110, 16, buf, false);

    siprintf(buf, "Music: %d%%", g_settings.music_volume);
    gfx_draw_button(10, 50, 110, 16, buf, false);

    siprintf(buf, "SFX: %d%%", g_settings.sfx_volume);
    gfx_draw_button(10, 68, 110, 16, buf, false);

    siprintf(buf, "Shake: %s", g_settings.screen_shake ? "On" : "Off");
    gfx_draw_button(10, 86, 110, 16, buf, false);

    gfx_draw_button(10, 104, 110, 16, "Reset High Score", false);
    gfx_draw_button(10, 122, 110, 16, "Back", false);

    gfx_draw_glass_card(126, 32, 104, 106, PAL_BTN_BORDER, 14);
    gfx_draw_text(130, 36, "DETAILS", PAL_TEXT_CYAN);
    gfx_draw_badge(130, 120, "AUTO-SAVE", PAL_TEXT_GREEN);

    gfx_draw_text_centered(0, 146, SCREEN_WIDTH, "LEFT/RIGHT Adjust   A Confirm   B Back", PAL_TEXT_WHITE);
}

static void render_settings_dynamic(void) {
    menu_draw_base();

    char buf[32];
    switch (s_menu_selected) {
        case 0:
            siprintf(buf, "Difficulty: %s", gfx_get_diff_name(g_settings.difficulty));
            gfx_draw_button(10, 32, 110, 16, buf, true);
            break;
        case 1:
            siprintf(buf, "Music: %d%%", g_settings.music_volume);
            gfx_draw_button(10, 50, 110, 16, buf, true);
            break;
        case 2:
            siprintf(buf, "SFX: %d%%", g_settings.sfx_volume);
            gfx_draw_button(10, 68, 110, 16, buf, true);
            break;
        case 3:
            siprintf(buf, "Shake: %s", g_settings.screen_shake ? "On" : "Off");
            gfx_draw_button(10, 86, 110, 16, buf, true);
            break;
        case 4:
            gfx_draw_button(10, 104, 110, 16, "Reset High Score", true);
            break;
        default:
            gfx_draw_button(10, 122, 110, 16, "Back", true);
            break;
    }

    const char* desc = "";
    switch (s_menu_selected) {
        case 0:
            if (g_settings.difficulty == DIFF_CADET) desc = "Cadet: 4 lives,\nslower speed,\nstarts with shield";
            else if (g_settings.difficulty == DIFF_ACE) desc = "Ace: 2 lives,\nfaster enemies,\nextra asteroids";
            else desc = "Pilot: 3 lives,\nstandard speed,\nbalanced waves";
            break;
        case 1: desc = "Soundtrack from\noriginal game\nproject."; break;
        case 2: desc = "Lasers, impacts,\nexplosions and\npickup sound FX."; break;
        case 3: desc = "Camera kick on\nhits & heavy\nexplosions."; break;
        case 4: desc = "Clears saved\nhigh score from\nSRAM memory."; break;
        default: desc = "Return to the\nmain menu."; break;
    }
    gfx_draw_text(130, 48, desc, PAL_TEXT_WHITE);
}

static void render_controls_static(void) {
    starfield_draw_base(0, 0);

    gfx_draw_text(10, 6, "Controls & Guide", PAL_TEXT_WHITE);
    gfx_fill_rect(10, 16, SCREEN_WIDTH - 20, 1, 20);

    gfx_draw_glass_card(8, 20, 108, 120, PAL_BTN_BORDER, 14);
    gfx_draw_text(12, 24, "GBA CONTROLS", PAL_TEXT_CYAN);
    gfx_draw_text(12, 36, "D-PAD: Move ship", PAL_TEXT_WHITE);
    gfx_draw_text(12, 48, "A: Fire lasers", PAL_TEXT_WHITE);
    gfx_draw_text(12, 60, "B / R: Dash burst", PAL_TEXT_WHITE);
    gfx_draw_text(12, 72, "START: Pause", PAL_TEXT_WHITE);
    gfx_draw_text(12, 84, "SELECT: Reset", PAL_TEXT_WHITE);
    gfx_draw_text(12, 100, "Dash gives brief", 17);
    gfx_draw_text(12, 110, "invulnerability!", 17);

    gfx_draw_glass_card(122, 20, 110, 120, PAL_BTN_BORDER, 14);
    gfx_draw_text(126, 24, "PICKUPS & COMBO", PAL_TEXT_CYAN);
    gfx_draw_text(126, 36, "[*] Shield (max 6)", 164);
    gfx_draw_text(126, 48, "[>] Rapid fire 12s", PAL_TEXT_GOLD);
    gfx_draw_text(126, 60, "[+] Repair +1 life", PAL_TEXT_GREEN);
    gfx_draw_text(126, 76, "SHOP TECH TREE", PAL_TEXT_GOLD);
    gfx_draw_text(126, 86, "7 Upgrades x Lv3", PAL_TEXT_WHITE);
    gfx_draw_text(126, 96, "6 Weapon Rigs", PAL_TEXT_WHITE);
    gfx_draw_text(126, 108, "COMBO up to x20", PAL_TEXT_CYAN);

    gfx_draw_text_centered(0, 146, SCREEN_WIDTH, "Press A or B to return", PAL_TEXT_WHITE);
}

static void render_controls_dynamic(void) {
    menu_draw_base();
}

static void render_credits_static(void) {
    starfield_draw_base(0, 0);

    gfx_draw_glass_card(20, 16, 200, 124, PAL_BTN_BORDER, 14);
    gfx_draw_text_centered(20, 22, 200, "SPACE UNLIMITED", PAL_TEXT_CYAN);
    gfx_draw_text_centered(20, 34, 200, "Recharged: GBA Edition", PAL_TEXT_WHITE);
    gfx_fill_rect(30, 46, 180, 1, 20);

    gfx_draw_text_centered(20, 54, 200, "Original Scratch Project", 17);
    gfx_draw_text_centered(20, 66, 200, "Game Boy Advance Port", PAL_TEXT_WHITE);
    gfx_draw_text_centered(20, 78, 200, "Native ARMv4T / Tonc", PAL_TEXT_CYAN);
    gfx_draw_text_centered(20, 90, 200, "DirectSound 18.157kHz", PAL_TEXT_GOLD);
    gfx_draw_text_centered(20, 102, 200, "Full Shop & Tech Tree", PAL_TEXT_GREEN);

    gfx_draw_text_centered(0, 146, SCREEN_WIDTH, "Press A or B to return", PAL_TEXT_WHITE);
}

static void render_credits_dynamic(void) {
    menu_draw_base();
}

static void render_paused(void) {
    game_draw();

    int w = 110;
    int h = 88;
    int x = (SCREEN_WIDTH - w) / 2;
    int y = (SCREEN_HEIGHT - h) / 2;
    gfx_draw_glass_card(x, y, w, h, PAL_TEXT_WHITE, 15);

    gfx_draw_text_centered(x, y + 6, w, "PAUSED", PAL_TEXT_CYAN);

    const char* opts[] = { "Resume", "Restart", "Main Menu" };
    for (int i = 0; i < 3; i++) {
        gfx_draw_button(x + 10, y + 20 + i * 18, w - 20, 15, opts[i], s_menu_selected == i);
    }
}

static void render_game_over(void) {
    game_draw();

    int w = 130;
    int h = 122;
    int x = (SCREEN_WIDTH - w) / 2;
    int y = (SCREEN_HEIGHT - h) / 2;
    gfx_draw_glass_card(x, y, w, h, PAL_TEXT_RED, 15);

    gfx_draw_text_centered(x, y + 6, w, "GAME OVER", PAL_TEXT_RED);

    char buf[32];
    siprintf(buf, "Score: %06u", (unsigned int)g_game.score);
    gfx_draw_text_centered(x, y + 18, w, buf, PAL_TEXT_WHITE);
    siprintf(buf, "Coins: $%u", (unsigned int)g_settings.coins);
    gfx_draw_text_centered(x, y + 28, w, buf, PAL_TEXT_GOLD);

    if (g_game.is_new_high_score) {
        gfx_draw_badge(x + (w - 74) / 2, y + 39, "NEW BEST!", PAL_TEXT_GOLD);
    } else {
        siprintf(buf, "Best:  %06u", (unsigned int)g_settings.high_score);
        gfx_draw_text_centered(x, y + 40, w, buf, 17);
    }

    const char* opts[] = { "Retry", "Shop", "Main Menu" };
    for (int i = 0; i < 3; i++) {
        gfx_draw_button(x + 10, y + 54 + i * 18, w - 20, 15, opts[i], s_menu_selected == i);
    }
}

void menu_draw(void) {
    switch (s_current_screen) {
        case SCREEN_MAIN_MENU:
            if (!s_static_valid) {
                menu_static_begin();
                render_main_menu_static();
                menu_static_end();
            }
            render_main_menu_dynamic();
            break;
        case SCREEN_HANGAR:
            if (!s_static_valid) {
                menu_static_begin();
                render_hangar_static();
                menu_static_end();
            }
            render_hangar_dynamic();
            break;
        case SCREEN_SETTINGS:
            if (!s_static_valid) {
                menu_static_begin();
                render_settings_static();
                menu_static_end();
            }
            render_settings_dynamic();
            break;
        case SCREEN_CONTROLS:
            if (!s_static_valid) {
                menu_static_begin();
                render_controls_static();
                menu_static_end();
            }
            render_controls_dynamic();
            break;
        case SCREEN_CREDITS:
            if (!s_static_valid) {
                menu_static_begin();
                render_credits_static();
                menu_static_end();
            }
            render_credits_dynamic();
            break;
        case SCREEN_PLAYING:   game_draw(); break;
        case SCREEN_PAUSED:    render_paused(); break;
        case SCREEN_GAME_OVER: render_game_over(); break;
    }
}
