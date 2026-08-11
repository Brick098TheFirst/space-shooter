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

void menu_init(void) {
    s_current_screen = SCREEN_MAIN_MENU;
    s_menu_selected = 0;
    s_anim_frame = 0;
    audio_play_bgm(BGM_MENU);
}

void menu_open(GameScreen screen) {
    s_current_screen = screen;
    s_menu_selected = 0;
    if (screen == SCREEN_MAIN_MENU || screen == SCREEN_HANGAR || screen == SCREEN_SETTINGS || screen == SCREEN_CONTROLS || screen == SCREEN_CREDITS) {
        audio_play_bgm(BGM_MENU);
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

static void update_hangar(void) {
    const int count = 5;
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
                g_settings.accent_index = (g_settings.accent_index + delta + 5) % 5;
                save_write();
                break;
            case 1:
                g_settings.trail_index = (g_settings.trail_index + delta + 4) % 4;
                save_write();
                break;
            case 2:
                g_settings.weapon_rig = (WeaponRig)((g_settings.weapon_rig + delta + 3) % 3);
                save_write();
                break;
        }
    }

    if (key_hit(KEY_A)) {
        if (s_menu_selected < 3) {
            // Cycle on A button
            if (s_menu_selected == 0) g_settings.accent_index = (g_settings.accent_index + 1) % 5;
            else if (s_menu_selected == 1) g_settings.trail_index = (g_settings.trail_index + 1) % 4;
            else if (s_menu_selected == 2) g_settings.weapon_rig = (WeaponRig)((g_settings.weapon_rig + 1) % 3);
            save_write();
        } else if (s_menu_selected == 3) {
            game_start();
            s_current_screen = SCREEN_PLAYING;
        } else if (s_menu_selected == 4) {
            menu_open(SCREEN_MAIN_MENU);
        }
    }

    if (key_hit(KEY_B)) {
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
                break;
            case 1:
                g_settings.music_volume += delta * 10;
                if (g_settings.music_volume < 0) g_settings.music_volume = 0;
                if (g_settings.music_volume > 100) g_settings.music_volume = 100;
                save_write();
                break;
            case 2:
                g_settings.sfx_volume += delta * 10;
                if (g_settings.sfx_volume < 0) g_settings.sfx_volume = 0;
                if (g_settings.sfx_volume > 100) g_settings.sfx_volume = 100;
                save_write();
                break;
            case 3:
                g_settings.screen_shake = !g_settings.screen_shake;
                save_write();
                break;
        }
    }

    if (key_hit(KEY_A)) {
        if (s_menu_selected == 0) {
            g_settings.difficulty = (Difficulty)((g_settings.difficulty + 1) % 3);
            save_write();
        } else if (s_menu_selected == 3) {
            g_settings.screen_shake = !g_settings.screen_shake;
            save_write();
        } else if (s_menu_selected == 4) {
            g_settings.high_score = 0;
            save_write();
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
                game_start();
                s_current_screen = SCREEN_PLAYING;
                break;
            case 2: // Main Menu
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
            case 1: // Hangar
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

static void draw_menu_ship_preview(int card_x, int card_y, int card_w, int card_h) {
    gfx_draw_glass_card(card_x, card_y, card_w, card_h, PAL_BTN_BORDER, 14);

    gfx_draw_text(card_x + 6, card_y + 4, "SHIP PREVIEW", PAL_TEXT_CYAN);
    gfx_draw_text(card_x + 6, card_y + 13, "Original Mk I", PAL_TEXT_WHITE);

    // Ship display
    int ship_x = card_x + (card_w - 20) / 2;
    int ship_y = card_y + 25;
    int accent = g_settings.accent_index;
    if (accent < 0 || accent > 4) accent = 1;
    gfx_draw_sprite(ship_x, ship_y, 20, 16, spr_ship[accent]);

    // Engine glow flare
    u8 trail_col = gfx_get_trail_color(g_settings.trail_index);
    if ((s_anim_frame & 4) == 0) {
        gfx_fill_rect(ship_x + 8, ship_y + 16, 4, 3, trail_col);
    } else {
        gfx_fill_rect(ship_x + 7, ship_y + 16, 6, 2, trail_col);
    }

    // Info rows
    gfx_draw_rect(card_x + 4, card_y + 45, card_w - 8, 1, 20);

    gfx_draw_text(card_x + 6, card_y + 49, "PAINT", PAL_TEXT_CYAN);
    gfx_draw_text(card_x + 36, card_y + 49, gfx_get_accent_name(accent), PAL_TEXT_WHITE);

    gfx_draw_text(card_x + 6, card_y + 59, "TRAIL", PAL_TEXT_CYAN);
    gfx_draw_text(card_x + 36, card_y + 59, gfx_get_trail_name(g_settings.trail_index), PAL_TEXT_WHITE);

    gfx_draw_text(card_x + 6, card_y + 69, "RIG", PAL_TEXT_CYAN);
    gfx_draw_text(card_x + 36, card_y + 69, gfx_get_weapon_name(g_settings.weapon_rig), PAL_TEXT_WHITE);

    gfx_draw_text(card_x + 6, card_y + 79, "BEST", PAL_TEXT_GOLD);
    char buf[16];
    siprintf(buf, "%06u", (unsigned int)g_settings.high_score);
    gfx_draw_text(card_x + 36, card_y + 79, buf, PAL_TEXT_WHITE);

    gfx_draw_badge(card_x + 6, card_y + 91, "READY", PAL_TEXT_GREEN);
}

static void render_main_menu(void) {
    starfield_draw(0, 0);

    // Left Column: Header & Buttons
    gfx_draw_text(10, 8, "SPACE UNLIMITED", PAL_TEXT_CYAN);
    gfx_draw_text(10, 18, "Recharged", PAL_TEXT_WHITE);
    gfx_fill_rect(10, 28, 45, 1, PAL_TEXT_CYAN);
    gfx_draw_text(10, 32, "GBA Edition", 17);

    const char* items[] = { "Play", "Hangar", "Settings", "Controls", "Credits" };
    int start_y = 44;
    int step_y = 19;
    for (int i = 0; i < 5; i++) {
        gfx_draw_button(10, start_y + i * step_y, 90, 16, items[i], s_menu_selected == i);
    }

    // Right Column: Ship Preview Card
    draw_menu_ship_preview(108, 10, 126, 106);

    // Footer
    gfx_draw_glass_card(108, 120, 126, 32, PAL_BTN_BORDER, 14);
    gfx_draw_text_centered(108, 124, 126, "D-PAD Navigate", PAL_TEXT_WHITE);
    gfx_draw_text_centered(108, 136, 126, "A Select", PAL_TEXT_CYAN);
}

static void render_hangar(void) {
    starfield_draw(0, 0);

    // Header
    gfx_draw_text(10, 6, "Hangar", PAL_TEXT_WHITE);
    gfx_draw_text(10, 16, "Customize ship & weapon rig", 17);
    gfx_fill_rect(10, 26, SCREEN_WIDTH - 20, 1, 20);

    // Left options
    char buf[32];

    siprintf(buf, "Paint: %s", gfx_get_accent_name(g_settings.accent_index));
    gfx_draw_button(10, 32, 116, 16, buf, s_menu_selected == 0);

    siprintf(buf, "Trail: %s", gfx_get_trail_name(g_settings.trail_index));
    gfx_draw_button(10, 52, 116, 16, buf, s_menu_selected == 1);

    siprintf(buf, "Rig: %s", gfx_get_weapon_name(g_settings.weapon_rig));
    gfx_draw_button(10, 72, 116, 16, buf, s_menu_selected == 2);

    gfx_draw_button(10, 96, 116, 16, "Launch Run", s_menu_selected == 3);
    gfx_draw_button(10, 116, 116, 16, "Back", s_menu_selected == 4);

    // Right Preview Card
    draw_menu_ship_preview(132, 32, 100, 100);

    // Footer
    gfx_draw_text_centered(0, 146, SCREEN_WIDTH, "LEFT/RIGHT Adjust   A Select   B Back", PAL_TEXT_WHITE);
}

static void render_settings(void) {
    starfield_draw(0, 0);

    // Header
    gfx_draw_text(10, 6, "Settings", PAL_TEXT_WHITE);
    gfx_draw_text(10, 16, "Auto-saved to cartridge SRAM", 17);
    gfx_fill_rect(10, 26, SCREEN_WIDTH - 20, 1, 20);

    char buf[32];

    siprintf(buf, "Difficulty: %s", gfx_get_diff_name(g_settings.difficulty));
    gfx_draw_button(10, 32, 110, 16, buf, s_menu_selected == 0);

    siprintf(buf, "Music: %d%%", g_settings.music_volume);
    gfx_draw_button(10, 50, 110, 16, buf, s_menu_selected == 1);

    siprintf(buf, "SFX: %d%%", g_settings.sfx_volume);
    gfx_draw_button(10, 68, 110, 16, buf, s_menu_selected == 2);

    siprintf(buf, "Shake: %s", g_settings.screen_shake ? "On" : "Off");
    gfx_draw_button(10, 86, 110, 16, buf, s_menu_selected == 3);

    gfx_draw_button(10, 104, 110, 16, "Reset High Score", s_menu_selected == 4);
    gfx_draw_button(10, 122, 110, 16, "Back", s_menu_selected == 5);

    // Right Description Card
    gfx_draw_glass_card(126, 32, 104, 106, PAL_BTN_BORDER, 14);
    gfx_draw_text(130, 36, "DETAILS", PAL_TEXT_CYAN);

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
    gfx_draw_badge(130, 120, "AUTO-SAVE", PAL_TEXT_GREEN);

    // Footer
    gfx_draw_text_centered(0, 146, SCREEN_WIDTH, "LEFT/RIGHT Adjust   A Confirm   B Back", PAL_TEXT_WHITE);
}

static void render_controls(void) {
    starfield_draw(0, 0);

    gfx_draw_text(10, 6, "Controls & Guide", PAL_TEXT_WHITE);
    gfx_fill_rect(10, 16, SCREEN_WIDTH - 20, 1, 20);

    // Left card: GBA Controls
    gfx_draw_glass_card(8, 20, 108, 120, PAL_BTN_BORDER, 14);
    gfx_draw_text(12, 24, "GBA CONTROLS", PAL_TEXT_CYAN);
    gfx_draw_text(12, 36, "D-PAD: Move ship", PAL_TEXT_WHITE);
    gfx_draw_text(12, 48, "A: Fire lasers", PAL_TEXT_WHITE);
    gfx_draw_text(12, 60, "B / R: Dash burst", PAL_TEXT_WHITE);
    gfx_draw_text(12, 72, "START: Pause", PAL_TEXT_WHITE);
    gfx_draw_text(12, 84, "SELECT: Reset", PAL_TEXT_WHITE);
    gfx_draw_text(12, 100, "Dash gives brief", 17);
    gfx_draw_text(12, 110, "invulnerability!", 17);

    // Right card: Pickups & Guide
    gfx_draw_glass_card(122, 20, 110, 120, PAL_BTN_BORDER, 14);
    gfx_draw_text(126, 24, "PICKUPS & COMBO", PAL_TEXT_CYAN);
    gfx_draw_text(126, 36, "[*] Shield (max 3)", 164);
    gfx_draw_text(126, 48, "[>] Rapid fire 9s", PAL_TEXT_GOLD);
    gfx_draw_text(126, 60, "[+] Repair +1 life", PAL_TEXT_GREEN);
    gfx_draw_text(126, 76, "COMBO SYSTEM:", PAL_TEXT_CYAN);
    gfx_draw_text(126, 88, "Kill fast to reach", PAL_TEXT_WHITE);
    gfx_draw_text(126, 98, "up to x8 combo!", PAL_TEXT_GOLD);

    gfx_draw_text_centered(0, 146, SCREEN_WIDTH, "Press A or B to return", PAL_TEXT_WHITE);
}

static void render_credits(void) {
    starfield_draw(0, 0);

    gfx_draw_glass_card(20, 16, 200, 124, PAL_BTN_BORDER, 14);
    gfx_draw_text_centered(20, 22, 200, "SPACE UNLIMITED", PAL_TEXT_CYAN);
    gfx_draw_text_centered(20, 34, 200, "Recharged: GBA Edition", PAL_TEXT_WHITE);
    gfx_fill_rect(30, 46, 180, 1, 20);

    gfx_draw_text_centered(20, 54, 200, "Original Scratch Project", 17);
    gfx_draw_text_centered(20, 66, 200, "Game Boy Advance Port", PAL_TEXT_WHITE);
    gfx_draw_text_centered(20, 78, 200, "Native ARMv4T / Tonc", PAL_TEXT_CYAN);
    gfx_draw_text_centered(20, 90, 200, "DirectSound 16kHz Audio", PAL_TEXT_GOLD);
    gfx_draw_text_centered(20, 102, 200, "Full Controller & SRAM", PAL_TEXT_GREEN);

    gfx_draw_text_centered(0, 146, SCREEN_WIDTH, "Press A or B to return", PAL_TEXT_WHITE);
}

static void render_paused(void) {
    // Draw running game frame in background
    game_draw();

    // Translucent pause overlay
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
    int h = 114;
    int x = (SCREEN_WIDTH - w) / 2;
    int y = (SCREEN_HEIGHT - h) / 2;
    gfx_draw_glass_card(x, y, w, h, PAL_TEXT_RED, 15);

    gfx_draw_text_centered(x, y + 6, w, "GAME OVER", PAL_TEXT_RED);

    char buf[32];
    siprintf(buf, "Score: %06u", (unsigned int)g_game.score);
    gfx_draw_text_centered(x, y + 18, w, buf, PAL_TEXT_WHITE);

    if (g_game.is_new_high_score) {
        gfx_draw_badge(x + (w - 74) / 2, y + 29, "NEW BEST!", PAL_TEXT_GOLD);
    } else {
        siprintf(buf, "Best:  %06u", (unsigned int)g_settings.high_score);
        gfx_draw_text_centered(x, y + 30, w, buf, 17);
    }

    const char* opts[] = { "Retry", "Hangar", "Main Menu" };
    for (int i = 0; i < 3; i++) {
        gfx_draw_button(x + 10, y + 44 + i * 18, w - 20, 15, opts[i], s_menu_selected == i);
    }
}

void menu_draw(void) {
    switch (s_current_screen) {
        case SCREEN_MAIN_MENU: render_main_menu(); break;
        case SCREEN_HANGAR:    render_hangar(); break;
        case SCREEN_SETTINGS:  render_settings(); break;
        case SCREEN_CONTROLS:  render_controls(); break;
        case SCREEN_CREDITS:   render_credits(); break;
        case SCREEN_PLAYING:   game_draw(); break;
        case SCREEN_PAUSED:    render_paused(); break;
        case SCREEN_GAME_OVER: render_game_over(); break;
    }
}
