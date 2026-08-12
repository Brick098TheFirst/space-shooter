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

#endif
