#ifndef PLATFORM_HOST_H
#define PLATFORM_HOST_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;

#define EWRAM_BSS
#define IWRAM_CODE

#define KEY_A      0x0001
#define KEY_B      0x0002
#define KEY_SELECT 0x0004
#define KEY_START  0x0008
#define KEY_RIGHT  0x0010
#define KEY_LEFT   0x0020
#define KEY_UP     0x0040
#define KEY_DOWN   0x0080
#define KEY_R      0x0100
#define KEY_L      0x0200

#define PLATFORM_SRAM_SIZE 256
extern volatile u8 platform_sram[PLATFORM_SRAM_SIZE];
#define SRAM_BASE platform_sram

extern volatile u16 REG_VCOUNT;

void key_poll(void);
u32 key_hit(u32 key);
u32 key_is_down(u32 key);
void platform_set_keys(u16 keys);

static inline void dma3_cpy(void* dst, const void* src, u32 bytes) {
    memcpy(dst, src, bytes);
}

/* tonc BAM sine/cosine: 16-bit turn, 4.12 output (4096 == 1.0) */
static inline s32 lu_sin(u32 theta) {
    double a = ((theta >> 7) & 0x1FF) * (6.283185307179586 / 512.0);
    return (s32)(sin(a) * 4096.0);
}
static inline s32 lu_cos(u32 theta) {
    double a = ((theta >> 7) & 0x1FF) * (6.283185307179586 / 512.0);
    return (s32)(cos(a) * 4096.0);
}

/* tonc's siprintf takes no size argument; on host platforms we map it to a
 * bounds-checked snprintf using the destination array's compile-time size. */
#define siprintf(dst, ...) snprintf((dst), sizeof(dst), __VA_ARGS__)

void platform_host_init(void);
void platform_set_save_dir(const char* dir);
void platform_persist_save(void);
bool platform_restore_save(void);
const u8* gfx_get_framebuffer(void);
void gfx_present_argb8888(u32* dst);

/* Haptic feedback queue (Android only).  The game core enqueues events and
 * the Kotlin layer drains them each frame and fires the vibrator. */
#define HAPTIC_HIT    0 // player took damage
#define HAPTIC_CHARGE 1 // big laser finished charging
#define HAPTIC_BEAM   2 // big laser fired
void platform_queue_haptic(int type);
int platform_take_haptics(int* out, int max);

#endif
