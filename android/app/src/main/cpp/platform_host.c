#include "platform.h"
#include "renderer.h"
#include "gfx_data.h"
#include <time.h>

volatile u8 platform_sram[PLATFORM_SRAM_SIZE];
volatile u16 REG_VCOUNT = 0;

/* Adaptive widescreen width (see platform_host.h). */
static int s_host_w = HOST_SCREEN_W_DEFAULT;

int host_screen_width(void) { return s_host_w; }

int host_set_screen_width(int w) {
    if (w < HOST_SCREEN_W_MIN) w = HOST_SCREEN_W_MIN;
    if (w > HOST_SCREEN_W_MAX) w = HOST_SCREEN_W_MAX;
    if (w == s_host_w) return 0;
    s_host_w = w;
    return 1;
}

static u16 s_keys_in = 0;
static u16 s_keys_held = 0;
static u16 s_keys_prev = 0;
static char s_save_path[512];

#define HAPTIC_QUEUE_MAX 8
static s32 s_haptic_queue[HAPTIC_QUEUE_MAX];
static int s_haptic_head = 0;
static int s_haptic_count = 0;

void platform_queue_haptic(int type) {
    if (s_haptic_count >= HAPTIC_QUEUE_MAX) return;
    /* Don't flood the vibrator when the beam shreds a packed field. */
    if (type == HAPTIC_KILL && s_haptic_count > 0) return;
    s_haptic_queue[(s_haptic_head + s_haptic_count) % HAPTIC_QUEUE_MAX] = type;
    s_haptic_count++;
}

int platform_take_haptics(int* out, int max) {
    int n = 0;
    while (n < max && s_haptic_count > 0) {
        out[n++] = s_haptic_queue[s_haptic_head];
        s_haptic_head = (s_haptic_head + 1) % HAPTIC_QUEUE_MAX;
        s_haptic_count--;
    }
    return n;
}

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
    s_save_path[0] = '\0';
    s_haptic_head = 0;
    s_haptic_count = 0;
}

/* Wall clock for Story Mode's fifteen-minute repair timer.  Uses the real
 * calendar clock so the yard keeps working while the app is backgrounded or
 * shut down entirely. */
u32 platform_epoch_seconds(void) {
    return (u32)time(NULL);
}

void platform_set_save_dir(const char* dir) {
    s_save_path[0] = '\0';
    if (!dir || !dir[0]) return;
    /* Context.getFilesDir()/saves/save.sav — app-private, always RW, no perms. */
    snprintf(s_save_path, sizeof(s_save_path), "%s/save.sav", dir);
}

void platform_persist_save(void) {
    if (!s_save_path[0]) return;
    FILE* f = fopen(s_save_path, "wb");
    if (!f) return;
    fwrite((const void*)platform_sram, 1, PLATFORM_SRAM_SIZE, f);
    fclose(f);
}

bool platform_restore_save(void) {
    if (!s_save_path[0]) return false;
    FILE* f = fopen(s_save_path, "rb");
    if (!f) return false;
    size_t n = fread((void*)platform_sram, 1, PLATFORM_SRAM_SIZE, f);
    fclose(f);
    return n > 0;
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
