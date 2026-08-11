#ifndef AUDIO_H
#define AUDIO_H

#include <tonc.h>
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

/*
 * Web player sync block.  The space-shooter web player runs this ROM in a
 * WASM mGBA core whose DirectSound path cannot deliver a continuous stream;
 * the frontend therefore mirrors music/SFX itself by polling this struct via
 * the libretro memory API and playing the original WAV assets with WebAudio.
 * It lives in EWRAM (RETRO_MEMORY_SYSTEM_RAM) next to the audio ring and is
 * located by scanning for WEB_AUDIO_SYNC_MAGIC, so no symbol table is needed.
 */
#define WEB_AUDIO_SYNC_MAGIC   0x53554153u /* 'SUAS' */
#define WEB_AUDIO_SYNC_VERSION 1u
#define WEB_AUDIO_SYNC_MAX_SFX 4u

typedef struct {
    u32 magic;      /* WEB_AUDIO_SYNC_MAGIC */
    u32 version;    /* WEB_AUDIO_SYNC_VERSION */
    u32 frame;      /* bumped once per audio_update() */
    s8  bgm_id;     /* BgmTrack: 0 none, 1 menu, 2 game */
    u8  bgm_playing;
    u8  music_vol;  /* 0..100 */
    u8  sfx_vol;    /* 0..100 */
    u32 bgm_pos;    /* position in samples @ 32768 Hz */
    u32 bgm_len;    /* track length in samples */
    u32 sfx_seq[WEB_AUDIO_SYNC_MAX_SFX];  /* increments on every play */
    s8  sfx_last[WEB_AUDIO_SYNC_MAX_SFX]; /* SfxId of the most recent play */
} WebAudioSync;

extern volatile WebAudioSync g_web_audio_sync;

#endif
