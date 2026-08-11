#ifndef STARFIELD_H
#define STARFIELD_H

#include <tonc.h>

void starfield_init(void);
void starfield_update(void);

/* Static layer: clear + nebula blobs (drawn once per screen change). */
void starfield_draw_base(int offset_x, int offset_y);
/* Dynamic layer: the scrolling stars (drawn every frame on top of the base). */
void starfield_draw_stars(int offset_x, int offset_y);

#endif
