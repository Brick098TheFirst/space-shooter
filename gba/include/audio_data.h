#ifndef AUDIO_DATA_H
#define AUDIO_DATA_H

#include "platform.h"

#define AUDIO_SAMPLE_RATE 18157
#ifndef AUDIO_SAMPLES_PER_FRAME
#define AUDIO_SAMPLES_PER_FRAME 304
#endif

extern const s8 snd_menu_pcm[];
extern const u32 snd_menu_len;

extern const s8 snd_game_pcm[];
extern const u32 snd_game_len;

extern const s8 snd_boss_pcm[];
extern const u32 snd_boss_len;

extern const s8 snd_laser_pcm[];
extern const u32 snd_laser_len;

extern const s8 snd_explosion_pcm[];
extern const u32 snd_explosion_len;

extern const s8 snd_pickup_pcm[];
extern const u32 snd_pickup_len;

#endif
