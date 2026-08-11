#ifndef AUDIO_DATA_H
#define AUDIO_DATA_H

#include <tonc.h>

/* Timer 0 / 256 with a 0xFFFE reload overflows at exactly 65,536/2 = 32,768
 * Hz, 1:1 with the GBA's internal audio re-sampler (GBATEK lists 32,768 Hz
 * as a best-accuracy DirectSound timer rate). */
#define AUDIO_SAMPLE_RATE 32768

extern const s8 snd_menu_pcm[];
extern const u32 snd_menu_len;

extern const s8 snd_game_pcm[];
extern const u32 snd_game_len;

extern const s8 snd_laser_pcm[];
extern const u32 snd_laser_len;

extern const s8 snd_explosion_pcm[];
extern const u32 snd_explosion_len;

extern const s8 snd_pickup_pcm[];
extern const u32 snd_pickup_len;

#endif
