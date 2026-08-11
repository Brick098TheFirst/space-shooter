#include "audio.h"
#include "types.h"
#include <string.h>

#define SAMPLES_PER_FRAME 272
#define MAX_ACTIVE_SFX 4

typedef struct {
    const s8* data;
    u32 length;
    u32 position;
    bool active;
} SfxChannel;

EWRAM_BSS static s8 s_audio_buffers[2][SAMPLES_PER_FRAME] __attribute__((aligned(4)));
static u32 s_active_buffer = 0;

static BgmTrack s_current_bgm = BGM_NONE;
static const s8* s_bgm_data = NULL;
static u32 s_bgm_len = 0;
static u32 s_bgm_pos = 0;

static SfxChannel s_sfx_channels[MAX_ACTIVE_SFX];

void audio_init(void) {
    s_current_bgm = BGM_NONE;
    s_bgm_data = NULL;
    s_bgm_len = 0;
    s_bgm_pos = 0;
    for (int i = 0; i < MAX_ACTIVE_SFX; i++) {
        s_sfx_channels[i].active = false;
    }
    memset(s_audio_buffers, 0, sizeof(s_audio_buffers));

    REG_SOUNDCNT_X = SSTAT_ENABLE;
    REG_SOUNDCNT_H = SDS_A100 | SDS_AR | SDS_AL | SDS_ARESET | SDS_ATMR0;

    REG_TM0CNT_L = 64488;
    REG_TM0CNT_H = TM_ENABLE;

    REG_DMA[1].cnt = 0;
    REG_DMA[1].src = (const void*)s_audio_buffers[0];
    REG_DMA[1].dst = (void*)&REG_FIFO_A;
    REG_DMA[1].cnt = DMA_DST_FIXED | DMA_SRC_INC | DMA_REPEAT | DMA_32 | DMA_AT_FIFO | DMA_ENABLE;
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
    }
}

void audio_update(void) {
    u32 write_buf = 1 - s_active_buffer;
    s8* dst = s_audio_buffers[write_buf];

    int music_vol = g_settings.music_volume;
    int sfx_vol = g_settings.sfx_volume;

    for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
        int mixed = 0;

        if (s_bgm_data && s_bgm_len > 0 && music_vol > 0) {
            int sample = s_bgm_data[s_bgm_pos];
            mixed += (sample * music_vol) / 100;
            s_bgm_pos++;
            if (s_bgm_pos >= s_bgm_len) {
                s_bgm_pos = 0;
            }
        }

        if (sfx_vol > 0) {
            for (int ch = 0; ch < MAX_ACTIVE_SFX; ch++) {
                if (s_sfx_channels[ch].active) {
                    int sample = s_sfx_channels[ch].data[s_sfx_channels[ch].position];
                    mixed += (sample * sfx_vol) / 100;
                    s_sfx_channels[ch].position++;
                    if (s_sfx_channels[ch].position >= s_sfx_channels[ch].length) {
                        s_sfx_channels[ch].active = false;
                    }
                }
            }
        }

        if (mixed > 127) mixed = 127;
        else if (mixed < -128) mixed = -128;

        dst[i] = (s8)mixed;
    }

    s_active_buffer = write_buf;
    REG_DMA[1].src = (const void*)s_audio_buffers[s_active_buffer];
}
