#ifndef AUDIO_H
#define AUDIO_H

#include "platform.h"
#include "audio_data.h"

typedef enum {
    BGM_NONE,
    BGM_MENU,
    BGM_GAME
} BgmTrack;

typedef enum {
    SFX_LASER,
    SFX_EXPLOSION,
    SFX_PICKUP
} SfxId;

void audio_init(void);
void audio_start(void);
void audio_update(void);
void audio_play_bgm(BgmTrack track);
void audio_stop_bgm(void);
void audio_play_sfx(SfxId sfx);
void audio_stop_all(void);

#ifdef PLATFORM_HOST
const s8* audio_host_mix_buffer(void);
#endif

#endif
