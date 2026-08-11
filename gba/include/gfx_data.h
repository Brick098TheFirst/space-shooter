#ifndef GFX_DATA_H
#define GFX_DATA_H

#include <tonc.h>

#define PAL_SPACE_BLACK 0
#define PAL_TEXT_WHITE 16
#define PAL_TEXT_CYAN 21
#define PAL_TEXT_GOLD 24
#define PAL_TEXT_RED 26
#define PAL_TEXT_GREEN 27
#define PAL_TEXT_VIOLET 28
#define PAL_BTN_BG 29
#define PAL_BTN_HOVER 30
#define PAL_BTN_BORDER 31

extern const u16 master_palette[256];

extern const u8 spr_ship[5][20 * 16];
extern const u8 spr_ast_large[24 * 24];
extern const u8 spr_ast_med_a[16 * 16];
extern const u8 spr_ast_med_b[16 * 16];
extern const u8 spr_ast_small[10 * 10];
extern const u8 spr_ast_tiny[6 * 6];

extern const u8 spr_drone[18 * 14];
extern const u8 spr_laser_standard[4 * 10];
extern const u8 spr_laser_heavy[6 * 14];
extern const u8 spr_laser_enemy[6 * 6];

extern const u8 spr_shield_bubble[24 * 24];
extern const u8 spr_pwr_shield[10 * 10];
extern const u8 spr_pwr_rapid[10 * 10];
extern const u8 spr_pwr_repair[10 * 10];

extern const u8 spr_explosion[9][24 * 24];

extern const u8 font_5x7[96][7];

#endif
