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

#ifdef PLATFORM_HOST
/* Smooth pixel-precise list scrolling (Android touch). Offset is in game
 * pixels: 0 = top of the list, max = last row fully visible. */
float menu_scroll_get(void);
float menu_scroll_max(void);
void  menu_scroll_to(float px);
#endif

#endif
