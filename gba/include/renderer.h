#ifndef RENDERER_H
#define RENDERER_H

#include "platform.h"
#include "types.h"
#include "gfx_data.h"

void gfx_init(void);
void gfx_flip(void);
const u8* gfx_get_framebuffer(void);
void gfx_clear(u8 color);

/* Static-layer caching: draw the unchanging screen parts into the cache with
 * gfx_set_target(), then blit them into the frame with gfx_apply_static(). */
void gfx_set_target(u8* buf);
void gfx_apply_static(void);

extern u8 gfx_static_layer[SCREEN_WIDTH * SCREEN_HEIGHT];

void gfx_draw_pixel(int x, int y, u8 color);
void gfx_fill_rect(int x, int y, int w, int h, u8 color);
void gfx_draw_rect(int x, int y, int w, int h, u8 color);
void gfx_draw_glass_card(int x, int y, int w, int h, u8 border_color, u8 fill_color);

void gfx_draw_sprite(int x, int y, int w, int h, const u8* data);
void gfx_draw_ship(int x, int y, int accent_idx, int anim_frame);
void gfx_draw_enemy_ship(int x, int y);
void gfx_draw_laser(int center_x, int center_y, bool heavy, int laser_idx, int anim_frame, bool downward);
void gfx_draw_sprite_rotated(int cx, int cy, int w, int h, const u8* data, int angle_deg);
void gfx_draw_sprite_clipped(int x, int y, int w, int h, const u8* data, int clip_x, int clip_y, int clip_w, int clip_h);

void gfx_draw_char(int x, int y, char c, u8 color);
void gfx_draw_text(int x, int y, const char* str, u8 color);
void gfx_draw_text_shadow(int x, int y, const char* str, u8 color, u8 shadow_color);
void gfx_draw_text_centered(int x, int y, int w, const char* str, u8 color);

void gfx_draw_button(int x, int y, int w, int h, const char* label, bool selected);
void gfx_draw_badge(int x, int y, const char* label, u8 accent_color);
void gfx_draw_swatch(int x, int y, int size, u8 color_idx, const char* label);
void gfx_draw_progress_bar(int x, int y, int w, int h, int current, int max_val, u8 fg_color, u8 bg_color);

u8 gfx_get_accent_color(int accent_idx);
u8 gfx_get_rainbow_color(int phase);
u8 gfx_get_trail_color(int trail_idx);
u8 gfx_get_trail_color_animated(int trail_idx, int anim_frame);
u8 gfx_get_laser_color(int laser_idx);

const char* gfx_get_accent_name(int accent_idx);
const char* gfx_get_accent_desc(int accent_idx);
const char* gfx_get_trail_name(int trail_idx);
const char* gfx_get_trail_desc(int trail_idx);
const char* gfx_get_weapon_name(WeaponRig rig);
const char* gfx_get_weapon_desc(WeaponRig rig);
const char* gfx_get_laser_name(int laser_idx);
const char* gfx_get_laser_desc(int laser_idx);
const char* gfx_get_diff_name(Difficulty diff);

// Laser variant sprites (generated at init)
const u8* gfx_get_laser_standard_sprite(int laser_idx);
const u8* gfx_get_laser_heavy_sprite(int laser_idx);

#endif
