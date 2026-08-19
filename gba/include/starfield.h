#ifndef STARFIELD_H
#define STARFIELD_H

#include "platform.h"

/* ── Backdrop themes ──────────────────────────────────────────────────────
 * Story Mode's kingdoms each fly over their own sky: the nebula colours,
 * their density, the star palette and an extra per-theme drifting layer
 * (dust, sleet, embers, rust motes...) all change.  The eighth kingdom (the
 * Drone Skies) wraps back to the Chubb Belt sky - the flight home.  Theme 0
 * is the original arcade starfield, so Waves/Endless/Overdrive look exactly
 * as they always did. */
#define SF_THEME_ARCADE   0   /* deep space blue-black (arcade default)   */
#define SF_THEME_BELT     1   /* THE CHUBB BELT   - dusty warm gold       */
#define SF_THEME_RUST     2   /* THE RUST YARDS   - orange iron haze      */
#define SF_THEME_ICE      3   /* THE ICE FIELDS   - pale blue whiteout    */
#define SF_THEME_SCRAP    4   /* THE SCRAPLINE    - green sodium glare    */
#define SF_THEME_EMBER    5   /* EMBER REACH      - red heat + embers     */
#define SF_THEME_VAULT    6   /* THE COLD VAULT   - near black, no stars  */
#define SF_THEME_REALITY  7   /* THE REALITY GATE - violet folded space   */
#define SF_THEME_COUNT    8

void starfield_init(void);
void starfield_update(void);

/* Swap the backdrop.  Cheap and idempotent: re-seeds the nebulae and the
 * theme's drifting layer only when the theme actually changes. */
void starfield_set_theme(int theme);
int  starfield_theme(void);

/* Static layer: clear + nebula blobs (drawn once per screen change). */
void starfield_draw_base(int offset_x, int offset_y);
/* Dynamic layer: the scrolling stars (drawn every frame on top of the base). */
void starfield_draw_stars(int offset_x, int offset_y);

#endif
