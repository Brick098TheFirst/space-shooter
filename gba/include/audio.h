#ifndef AUDIO_H
#define AUDIO_H

#include "platform.h"

#ifdef PLATFORM_HOST
#ifndef AUDIO_SAMPLES_PER_FRAME
#define AUDIO_SAMPLES_PER_FRAME 202
#endif
#endif

#include "audio_data.h"

#ifndef AUDIO_SAMPLES_PER_FRAME
#define AUDIO_SAMPLES_PER_FRAME 304
#endif

typedef enum {
    BGM_NONE,
    BGM_MENU,
    BGM_GAME,
    BGM_BOSS,
    /* Story Mode's own track (Assets/Audio/story_mode.mp3).  Android only -
     * on the GBA it falls back to BGM_MENU, since the ROM has no campaign
     * and no room for a fourth long sample. */
    BGM_STORY
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
/* Boss cue: fade the normal track out, leave one second of real silence for
 * the entrance, then fade boss.wav in.  Ending a boss resumes the gameplay
 * track at the exact sample where it was paused. */
void audio_begin_boss_music(void);
void audio_end_boss_music(void);
void audio_stop_bgm(void);
void audio_play_sfx(SfxId sfx);
void audio_stop_all(void);

#ifdef PLATFORM_HOST
const s8* audio_host_mix_buffer(void);
#endif

#endif
