#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef PLATFORM_HOST
#include "platform_host.h"
#else
#include <tonc.h>

/* Haptics are an Android-only capability (there is no vibration motor on a
 * GBA).  The shared game core still calls platform_queue_haptic() from the
 * gameplay code that both platforms run, so provide a no-op here instead of
 * sprinkling #ifdefs through game logic. */
#define HAPTIC_HIT    0
#define HAPTIC_CHARGE 1
#define HAPTIC_BEAM   2
#define HAPTIC_KILL   3
static inline void platform_queue_haptic(int type) { (void)type; }
#endif

#endif
