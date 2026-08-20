#ifndef MENU_H
#define MENU_H

#include "types.h"

void menu_init(void);
void menu_open(GameScreen screen);
void menu_update(void);
void menu_draw(void);

GameScreen menu_current_screen(void);
void menu_queue_tap(int x, int y);
void menu_go_back(void);

/* Rebuilds the cached static layer (call after the Android viewport width
 * changes mid-session). */
void menu_request_full_redraw(void);

#ifdef PLATFORM_HOST
/* Smooth pixel-precise list scrolling (Android touch). Offset is in game
 * pixels: 0 = top of the list, max = last row fully visible. */
float menu_scroll_get(void);
float menu_scroll_max(void);
void  menu_scroll_to(float px);
/* Settings -> CODES: the native UI raises this once per activation; the
 * Kotlin layer drains it and opens the cheat-code text dialog. */
int menu_take_code_request(void);
/* Settings -> ERASE DATA: drained by Kotlin to show a confirm dialog. */
int menu_take_erase_request(void);
/* Story endings request a controlled activity close after the save is durable.
 * This is intentionally a clean close, not an unsafe native crash: the next
 * launch can read the ending phase and continue the coda. */
int menu_take_exit_request(void);
#endif

#endif
