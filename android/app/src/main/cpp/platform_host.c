#include "platform.h"
#include "renderer.h"
#include "gfx_data.h"

volatile u8 platform_sram[PLATFORM_SRAM_SIZE];
volatile u16 REG_VCOUNT = 0;

static u16 s_keys_in = 0;
static u16 s_keys_held = 0;
static u16 s_keys_prev = 0;

void platform_set_keys(u16 keys) {
    s_keys_in = keys;
}

void key_poll(void) {
    s_keys_prev = s_keys_held;
    s_keys_held = s_keys_in;
}

u32 key_hit(u32 key) {
    return (s_keys_held & ~s_keys_prev) & key;
}

u32 key_is_down(u32 key) {
    return s_keys_held & key;
}

void platform_host_init(void) {
    memset((void*)platform_sram, 0, PLATFORM_SRAM_SIZE);
    REG_VCOUNT = 0;
    s_keys_in = s_keys_held = s_keys_prev = 0;
}

void gfx_present_argb8888(u32* dst) {
    const u8* src = gfx_get_framebuffer();
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        u16 c = master_palette[src[i]];
        u32 r = (c & 0x1F) << 3;
        u32 g = ((c >> 5) & 0x1F) << 3;
        u32 b = ((c >> 10) & 0x1F) << 3;
        dst[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
}
