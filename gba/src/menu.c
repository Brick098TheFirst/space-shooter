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
static char s_detail_buf[128];
static int s_shop_msg_timer = 0;
static char s_shop_msg[32] = {0};
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

// Shop replaces Hangar — buy cosmetics with coins earned in game
static void update_hangar(void) {
    const int count = 6; // Paint, Trail, Rig, Laser, Launch, Back
    if (s_shop_msg_timer > 0) s_shop_msg_timer--;
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
            case 0: // Paint
                g_settings.accent_index = (g_settings.accent_index + delta + NUM_ACCENTS) % NUM_ACCENTS;
                menu_static_invalidate();
                break;
            case 1: // Trail
                g_settings.trail_index = (g_settings.trail_index + delta + NUM_TRAILS) % NUM_TRAILS;
                menu_static_invalidate();
                break;
            case 2: // Rig
                g_settings.weapon_rig = (WeaponRig)((g_settings.weapon_rig + delta + NUM_RIGS) % NUM_RIGS);
                menu_static_invalidate();
                break;
            case 3: // Laser colour
                g_settings.laser_index = (g_settings.laser_index + delta + NUM_LASERS) % NUM_LASERS;
                menu_static_invalidate();
                break;
            default: break;
        }
    }

    if (key_hit(KEY_A) || key_hit(KEY_START)) {
        bool consumed = false;
        switch (s_menu_selected) {
            case 0: { // Paint buy/equip
                int idx = g_settings.accent_index;
                if (shop_is_accent_owned(idx)) {
                    shop_equip_accent(idx);
                    shop_set_msg("EQUIPPED", PAL_TEXT_GREEN);
                } else {
                    if (shop_try_purchase_accent(idx)) {
                        shop_set_msg("BOUGHT!", PAL_TEXT_GOLD);
                        audio_play_sfx(SFX_PICKUP);
                    } else {
                        char tmp[24];
                        siprintf(tmp, "NEED %dc", shop_get_accent_price(idx));
                        shop_set_msg(tmp, PAL_TEXT_RED);
                    }
                }
                consumed = true;
                break;
            }
            case 1: { // Trail
                int idx = g_settings.trail_index;
                if (shop_is_trail_owned(idx)) {
                    shop_equip_trail(idx);
                    shop_set_msg("EQUIPPED", PAL_TEXT_GREEN);
                } else {
                    if (shop_try_purchase_trail(idx)) {
                        shop_set_msg("BOUGHT!", PAL_TEXT_GOLD);
                        audio_play_sfx(SFX_PICKUP);
                    } else {
                        char tmp[24];
                        siprintf(tmp, "NEED %dc", shop_get_trail_price(idx));
                        shop_set_msg(tmp, PAL_TEXT_RED);
                    }
                }
                consumed = true;
                break;
            }
            case 2: { // Rig
                WeaponRig rig = g_settings.weapon_rig;
                if (shop_is_rig_owned(rig)) {
                    shop_equip_rig(rig);
                    shop_set_msg("EQUIPPED", PAL_TEXT_GREEN);
                } else {
                    if (shop_try_purchase_rig(rig)) {
                        shop_set_msg("BOUGHT!", PAL_TEXT_GOLD);
                        audio_play_sfx(SFX_PICKUP);
                    } else {
                        char tmp[24];
                        siprintf(tmp, "NEED %dc", shop_get_rig_price(rig));
                        shop_set_msg(tmp, PAL_TEXT_RED);
                    }
                }
                consumed = true;
                break;
            }
            case 3: { // Laser
                int idx = g_settings.laser_index;
                if (shop_is_laser_owned(idx)) {
                    shop_equip_laser(idx);
                    shop_set_msg("EQUIPPED", PAL_TEXT_GREEN);
                } else {
                    if (shop_try_purchase_laser(idx)) {
                        shop_set_msg("BOUGHT!", PAL_TEXT_GOLD);
                        audio_play_sfx(SFX_PICKUP);
                    } else {
                        char tmp[24];
                        siprintf(tmp, "NEED %dc", shop_get_laser_price(idx));
                        shop_set_msg(tmp, PAL_TEXT_RED);
                    }
                }
                consumed = true;
                break;
            }
            case 4: { // Launch Run
                // Ensure we don't launch with a locked cosmetic equipped for free
                if (!shop_is_accent_owned(g_settings.accent_index)) g_settings.accent_index = 1;
                if (!shop_is_trail_owned(g_settings.trail_index)) g_settings.trail_index = 1;
                if (!shop_is_rig_owned(g_settings.weapon_rig)) g_settings.weapon_rig = WEAPON_TWIN;
                if (!shop_is_laser_owned(g_settings.laser_index)) g_settings.laser_index = 0;
                save_write();
                game_start();
                s_current_screen = SCREEN_PLAYING;
                consumed = true;
                break;
            }
            case 5: { // Back
                if (!shop_is_accent_owned(g_settings.accent_index)) g_settings.accent_index = 1;
                if (!shop_is_trail_owned(g_settings.trail_index)) g_settings.trail_index = 1;
                if (!shop_is_rig_owned(g_settings.weapon_rig)) g_settings.weapon_rig = WEAPON_TWIN;
                if (!shop_is_laser_owned(g_settings.laser_index)) g_settings.laser_index = 0;
                menu_open(SCREEN_MAIN_MENU);
                consumed = true;
                break;
            }
        }
        if (consumed) menu_static_invalidate();
    }

    if (key_hit(KEY_B)) {
        if (!shop_is_accent_owned(g_settings.accent_index)) g_settings.accent_index = 1;
        if (!shop_is_trail_owned(g_settings.trail_index)) g_settings.trail_index = 1;
        if (!shop_is_rig_owned(g_settings.weapon_rig)) g_settings.weapon_rig = WEAPON_TWIN;
        if (!shop_is_laser_owned(g_settings.laser_index)) g_settings.laser_index = 0;
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
            // keep coins
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
                save_write(); // persist coins earned this run
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
            case 1: // Hangar (Shop)
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

/* Static half of the ship preview card: frame, labels and info rows. */
static void draw_ship_preview_static(int card_x, int card_y, int card_w, int card_h) {
    gfx_draw_glass_card(card_x, card_y, card_w, card_h, PAL_BTN_BORDER, 14);
    gfx_draw_text(card_x + 6, card_y + 4, "SHIP PREVIEW", PAL_TEXT_CYAN);
    gfx_draw_text(card_x + 6, card_y + 13, "Original Mk I", PAL_TEXT_WHITE);

    gfx_draw_rect(card_x + 4, card_y + 45, card_w - 8, 1, 20);

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

/* Dynamic half: the ship sprite and its flickering engine flare. */
static void draw_ship_preview_dynamic(int card_x, int card_y, int card_w) {
    int ship_x = card_x + (card_w - 20) / 2;
    int ship_y = card_y + 25;
    int accent = g_settings.accent_index;
    if (accent < 0 || accent > 4) accent = 1;
    gfx_draw_sprite(ship_x, ship_y, 20, 16, spr_ship[accent]);

    u8 trail_col = gfx_get_trail_color(g_settings.trail_index);
    if ((s_anim_frame & 4) == 0) {
        gfx_fill_rect(ship_x + 8, ship_y + 16, 4, 3, trail_col);
    } else {
        gfx_fill_rect(ship_x + 7, ship_y + 16, 6, 2, trail_col);
    }
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

// Shop: 4 purchasable categories + Launch + Back
static void render_hangar_static(void) {
    starfield_draw_base(0, 0);

    gfx_draw_text(10, 6, "SHOP", PAL_TEXT_WHITE);
    gfx_draw_text(10, 14, "Buy paint, trails, rigs, lasers", 17);
    // Coins top-right
    char coin_buf[16];
    siprintf(coin_buf, "$%u", (unsigned int)g_settings.coins);
    gfx_draw_text(SCREEN_WIDTH - 6 - (int)strlen(coin_buf)*6, 6, coin_buf, PAL_TEXT_GOLD);
    gfx_fill_rect(10, 24, SCREEN_WIDTH - 20, 1, 20);

    // Buttons: Paint, Trail, Rig, Laser, Launch, Back
    // We draw them as unselected; dynamic pass will highlight selected
    char buf[32];
    // Price suffix handling
    const char* suffix;

    // Paint
    if (shop_is_accent_owned(g_settings.accent_index)) suffix = "OWNED";
    else { siprintf(buf, "%dc", shop_get_accent_price(g_settings.accent_index)); suffix = buf; }
    siprintf(buf, "Paint: %s %s", gfx_get_accent_name(g_settings.accent_index), suffix);
    // To keep button width, we just show name, price drawn separately in card?
    siprintf(buf, "Paint: %s", gfx_get_accent_name(g_settings.accent_index));
    gfx_draw_button(10, 28, 116, 14, buf, false);
    // Trail
    siprintf(buf, "Trail: %s", gfx_get_trail_name(g_settings.trail_index));
    gfx_draw_button(10, 44, 116, 14, buf, false);
    // Rig
    siprintf(buf, "Rig: %s", gfx_get_weapon_name(g_settings.weapon_rig));
    gfx_draw_button(10, 60, 116, 14, buf, false);
    // Laser
    siprintf(buf, "Laser: %s", gfx_get_laser_name(g_settings.laser_index));
    gfx_draw_button(10, 76, 116, 14, buf, false);

    gfx_draw_button(10, 98, 116, 14, "Launch Run", false);
    gfx_draw_button(10, 114, 116, 14, "Back", false);

    draw_ship_preview_static(132, 28, 100, 112);

    gfx_draw_text_centered(0, 146, SCREEN_WIDTH, "LEFT/RIGHT Cycle  A Buy/Equip  B Back", PAL_TEXT_WHITE);
}

static void render_hangar_dynamic(void) {
    menu_draw_base();

    char buf[40];
    char price_buf[20];
    // Highlight selected button with price logic
    switch (s_menu_selected) {
        case 0: {
            bool owned = shop_is_accent_owned(g_settings.accent_index);
            if (!owned) siprintf(price_buf, "%dc", shop_get_accent_price(g_settings.accent_index));
            else siprintf(price_buf, "OWNED");
            siprintf(buf, "Paint: %s", gfx_get_accent_name(g_settings.accent_index));
            gfx_draw_button(10, 28, 116, 14, buf, true);
            // price badge near preview
            gfx_draw_badge(134, 128, price_buf, owned ? PAL_TEXT_GREEN : PAL_TEXT_GOLD);
            break;
        }
        case 1: {
            bool owned = shop_is_trail_owned(g_settings.trail_index);
            if (!owned) siprintf(price_buf, "%dc", shop_get_trail_price(g_settings.trail_index));
            else siprintf(price_buf, "OWNED");
            siprintf(buf, "Trail: %s", gfx_get_trail_name(g_settings.trail_index));
            gfx_draw_button(10, 44, 116, 14, buf, true);
            gfx_draw_badge(134, 128, price_buf, owned ? PAL_TEXT_GREEN : PAL_TEXT_GOLD);
            break;
        }
        case 2: {
            bool owned = shop_is_rig_owned(g_settings.weapon_rig);
            if (!owned) siprintf(price_buf, "%dc", shop_get_rig_price(g_settings.weapon_rig));
            else siprintf(price_buf, "OWNED");
            siprintf(buf, "Rig: %s", gfx_get_weapon_name(g_settings.weapon_rig));
            gfx_draw_button(10, 60, 116, 14, buf, true);
            gfx_draw_badge(134, 128, price_buf, owned ? PAL_TEXT_GREEN : PAL_TEXT_GOLD);
            break;
        }
        case 3: {
            bool owned = shop_is_laser_owned(g_settings.laser_index);
            if (!owned) siprintf(price_buf, "%dc", shop_get_laser_price(g_settings.laser_index));
            else siprintf(price_buf, "OWNED");
            siprintf(buf, "Laser: %s", gfx_get_laser_name(g_settings.laser_index));
            gfx_draw_button(10, 76, 116, 14, buf, true);
            gfx_draw_badge(134, 128, price_buf, owned ? PAL_TEXT_GREEN : PAL_TEXT_GOLD);
            break;
        }
        case 4:
            gfx_draw_button(10, 98, 116, 14, "Launch Run", true);
            break;
        default:
            gfx_draw_button(10, 114, 116, 14, "Back", true);
            break;
    }
    draw_ship_preview_dynamic(132, 28, 100);

    // Shop message popup if active (centered above hint)
    if (s_shop_msg_timer > 0) {
        int w = (int)strlen(s_shop_msg)*6 + 10;
        int x = (SCREEN_WIDTH - w)/2;
        int y = 132;
        gfx_draw_glass_card(x, y, w, 12, s_shop_msg_col, 15);
        gfx_draw_text_centered(x, y+3, w, s_shop_msg, s_shop_msg_col);
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

    // Detail text follows the selection
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
    gfx_draw_text(126, 36, "[*] Shield (max 3)", 164);
    gfx_draw_text(126, 48, "[>] Rapid fire 9s", PAL_TEXT_GOLD);
    gfx_draw_text(126, 60, "[+] Repair +1 life", PAL_TEXT_GREEN);
    gfx_draw_text(126, 76, "POWERUPS ARE RARE", PAL_TEXT_GOLD);
    gfx_draw_text(126, 86, "4% asteroids", PAL_TEXT_WHITE);
    gfx_draw_text(126, 96, "7% drones", PAL_TEXT_WHITE);
    gfx_draw_text(126, 108, "COMBO up to x8", PAL_TEXT_CYAN);

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
    gfx_draw_text_centered(20, 90, 200, "DirectSound 16.384kHz", PAL_TEXT_GOLD);
    gfx_draw_text_centered(20, 102, 200, "Coins + Shop System", PAL_TEXT_GREEN);

    gfx_draw_text_centered(0, 146, SCREEN_WIDTH, "Press A or B to return", PAL_TEXT_WHITE);
}

static void render_credits_dynamic(void) {
    menu_draw_base();
}

static void render_paused(void) {
    // Draw running game frame in background
    game_draw();

    // Translucent pause overlay (rebuilt every frame: cheap, rare screen)
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
    siprintf(buf, "Coins $%u", (unsigned int)g_settings.coins);
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
        case SCREEN_PLAYING:   game_draw(); break; // marker inside game_draw
        case SCREEN_PAUSED:    render_paused(); break;
        case SCREEN_GAME_OVER: render_game_over(); break;
    }
}
