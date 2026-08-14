#include "audio.h"
#include "types.h"
#include <string.h>

/*
 * GBA DirectSound Engine (18,157 Hz Double-Buffered DirectSound via DMA 1)
 *
 * DirectSound hardware playback operates at 18,157 Hz (exactly 304 samples
 * per video frame: 280,896 CPU cycles / 924 cycles per sample = 304 samples).
 *
 * Timer 0 clocks DirectSound Channel A at 924-cycle intervals (reload 64,612).
 * DMA 1 runs in DirectSound FIFO mode (DMA_AT_SPECIAL), automatically copying
 * 16 bytes into REG_FIFO_A whenever the FIFO is half-empty (19 transfers / frame).
 *
 * Audio is double-buffered (2 x 304 = 608 bytes) in fast 32-bit IWRAM.
 * On each VBlank interrupt, the buffer index toggles and DMA restarts seamlessly
 * at buffer 0 after buffer 1 finishes.
 *
 * Real-time software mixing runs with zero software division for high performance.
 */

#ifndef AUDIO_SAMPLES_PER_FRAME
#define AUDIO_SAMPLES_PER_FRAME 304
#endif
#define AUDIO_DOUBLE_BUF_SIZE (AUDIO_SAMPLES_PER_FRAME * 2)
#define MAX_ACTIVE_SFX 4

typedef struct {
    const s8* data;
    u32 length;
    u32 position;
    bool active;
} SfxChannel;

static s8 s_audio_buf[AUDIO_DOUBLE_BUF_SIZE] __attribute__((aligned(4)));
static volatile u8 s_active_buf = 0;
static s8* volatile s_mix_buf = &s_audio_buf[0];
static bool s_audio_started = false;

static BgmTrack s_current_bgm = BGM_NONE;
static const s8* s_bgm_data = NULL;
static u32 s_bgm_len = 0;
static u32 s_bgm_pos = 0;

static SfxChannel s_sfx_channels[MAX_ACTIVE_SFX];

#ifndef PLATFORM_HOST
IWRAM_CODE static void audio_vblank_isr(void) {
    if (!s_audio_started) return;

    if (s_active_buf == 1) {
        // Buffer 1 completed: restart DMA at Buffer 0
        REG_DMA1CNT = 0;
        REG_DMA1SAD = (u32)&s_audio_buf[0];
        REG_DMA1DAD = (u32)&REG_FIFO_A;
        REG_DMA1CNT = DMA_ENABLE | DMA_REPEAT | DMA_32 | DMA_AT_SPECIAL | DMA_SRC_INC | DMA_DST_FIXED;
        s_mix_buf = &s_audio_buf[AUDIO_SAMPLES_PER_FRAME];
        s_active_buf = 0;
    } else {
        // Buffer 0 completed: DMA seamlessly continues reading Buffer 1
        s_mix_buf = &s_audio_buf[0];
        s_active_buf = 1;
    }
}
#endif

void audio_init(void) {
    s_current_bgm = BGM_NONE;
    s_bgm_data = NULL;
    s_bgm_len = 0;
    s_bgm_pos = 0;
    s_audio_started = false;
    s_active_buf = 0;
    s_mix_buf = &s_audio_buf[0];

    for (int i = 0; i < MAX_ACTIVE_SFX; i++) {
        s_sfx_channels[i].data = NULL;
        s_sfx_channels[i].length = 0;
        s_sfx_channels[i].position = 0;
        s_sfx_channels[i].active = false;
    }
    memset(s_audio_buf, 0, sizeof(s_audio_buf));

#ifdef PLATFORM_HOST
    s_audio_started = true;
#else
    // Turn off existing DMA and Timer
    REG_DMA1CNT = 0;
    REG_TM0CNT = 0;

    // Enable Sound Master
    REG_SOUNDCNT_X = SSTAT_ENABLE;
    REG_SOUNDCNT_L = 0;

    // DirectSound A: 100% volume, Left & Right speakers, Timer 0, FIFO reset
    REG_SOUNDCNT_H = SDS_A100 | SDS_AR | SDS_AL | SDS_ARESET | SDS_ATMR0;
    REG_SOUNDCNT_H = SDS_A100 | SDS_AR | SDS_AL | SDS_ATMR0;

    // Timer 0: 18,157 Hz (65536 - 924 = 64612 = 0xFC64)
    REG_TM0CNT_L = 64612;

    // Register VBlank audio buffer swap handler
    irq_add(II_VBLANK, audio_vblank_isr);
#endif
}

void audio_start(void) {
    if (s_audio_started) return;

    // Initial mix for both buffers
    s_mix_buf = &s_audio_buf[0];
    audio_update();
    s_mix_buf = &s_audio_buf[AUDIO_SAMPLES_PER_FRAME];
    audio_update();

#ifndef PLATFORM_HOST
    s_active_buf = 0;
    s_mix_buf = &s_audio_buf[AUDIO_SAMPLES_PER_FRAME];

    // Start DMA 1 in FIFO mode (Destination is REG_FIFO_A)
    REG_DMA1SAD = (u32)&s_audio_buf[0];
    REG_DMA1DAD = (u32)&REG_FIFO_A;
    REG_DMA1CNT = DMA_ENABLE | DMA_REPEAT | DMA_32 | DMA_AT_SPECIAL | DMA_SRC_INC | DMA_DST_FIXED;

    // Start Timer 0 (CPU frequency, no IRQ)
    REG_TM0CNT_H = TM_FREQ_1 | TM_ENABLE;
#endif

    s_audio_started = true;
}

void audio_play_bgm(BgmTrack track) {
    if (s_current_bgm == track) return;

    s_current_bgm = track;
    s_bgm_pos = 0;
    if (track == BGM_MENU) {
        s_bgm_data = snd_menu_pcm;
        s_bgm_len = snd_menu_len;
    } else if (track == BGM_GAME) {
        s_bgm_data = snd_game_pcm;
        s_bgm_len = snd_game_len;
    } else {
        s_bgm_data = NULL;
        s_bgm_len = 0;
    }
}

void audio_stop_bgm(void) {
    s_current_bgm = BGM_NONE;
    s_bgm_data = NULL;
    s_bgm_len = 0;
    s_bgm_pos = 0;
}

void audio_play_sfx(SfxId sfx) {
    const s8* data = NULL;
    u32 len = 0;
    switch (sfx) {
        case SFX_LASER:
            data = snd_laser_pcm;
            len = snd_laser_len;
            break;
        case SFX_EXPLOSION:
            data = snd_explosion_pcm;
            len = snd_explosion_len;
            break;
        case SFX_PICKUP:
            data = snd_pickup_pcm;
            len = snd_pickup_len;
            break;
    }
    if (!data || len == 0) return;

    /* Same-sample retrigger suppression: if this exact SFX is already
     * playing, let it finish instead of restarting it.  Without this,
     * every shot during fast fire (or two ships firing in co-op) restarts
     * the sample mid-play, turning the laser into a stuttering machine-gun
     * buzz that sounds way faster than the actual fire rate. */
    for (int i = 0; i < MAX_ACTIVE_SFX; i++) {
        if (s_sfx_channels[i].active && s_sfx_channels[i].data == data) {
            return;
        }
    }

    int chosen = -1;
    for (int i = 0; i < MAX_ACTIVE_SFX; i++) {
        if (!s_sfx_channels[i].active) {
            chosen = i;
            break;
        }
    }
    if (chosen < 0) chosen = 0;

    s_sfx_channels[chosen].data = data;
    s_sfx_channels[chosen].length = len;
    s_sfx_channels[chosen].position = 0;
    s_sfx_channels[chosen].active = true;
}

void audio_stop_all(void) {
    audio_stop_bgm();
    for (int i = 0; i < MAX_ACTIVE_SFX; i++) {
        s_sfx_channels[i].active = false;
        s_sfx_channels[i].data = NULL;
        s_sfx_channels[i].length = 0;
        s_sfx_channels[i].position = 0;
    }
}

#ifdef PLATFORM_HOST
const s8* audio_host_mix_buffer(void) {
    return s_mix_buf;
}
#endif

IWRAM_CODE void audio_update(void) {
    if (!s_audio_started) return;

    s8* dst = s_mix_buf;
    if (!dst) return;

    const s8* bgm = s_bgm_data;
    u32 bgm_len = s_bgm_len;
    u32 bgm_pos = s_bgm_pos;

    // Collect active SFX channels
    int active_sfx[MAX_ACTIVE_SFX];
    int active_sfx_count = 0;
    for (int ch = 0; ch < MAX_ACTIVE_SFX; ch++) {
        if (s_sfx_channels[ch].active && s_sfx_channels[ch].data && s_sfx_channels[ch].length > 0) {
            active_sfx[active_sfx_count++] = ch;
        }
    }

    if (!bgm && active_sfx_count == 0) {
        memset(dst, 0, AUDIO_SAMPLES_PER_FRAME);
        return;
    }

    // Convert 0..100 volume scale to 0..256 fixed-point multiplier (0 division)
    int music_scale = (g_settings.music_volume * 655) >> 8;
    int sfx_scale = (g_settings.sfx_volume * 655) >> 8;
#ifdef PLATFORM_HOST
    /* Menu WAV is much louder/bassier than the in-game track after the shared
     * peak-normalize. Duck it and shave a little off gameplay BGM so the host
     * mixer is not a wall of boom. */
    if (s_current_bgm == BGM_MENU) {
        music_scale = (music_scale * 150) >> 8;
    } else {
        music_scale = (music_scale * 210) >> 8;
    }
#endif

    for (int i = 0; i < AUDIO_SAMPLES_PER_FRAME; i++) {
        int mixed = 0;

        if (bgm && bgm_len > 0) {
            mixed += (bgm[bgm_pos] * music_scale) >> 8;
            bgm_pos++;
            if (bgm_pos >= bgm_len) {
                bgm_pos = 0;
            }
        }

        for (int j = 0; j < active_sfx_count; j++) {
            int ch = active_sfx[j];
            SfxChannel* sc = &s_sfx_channels[ch];
            if (sc->position < sc->length) {
                mixed += (sc->data[sc->position] * sfx_scale) >> 8;
                sc->position++;
            } else {
                sc->active = false;
            }
        }

        if (mixed > 127) mixed = 127;
        else if (mixed < -128) mixed = -128;

        dst[i] = (s8)mixed;
    }

    s_bgm_pos = bgm_pos;
}
