#include "audio.h"
#include "types.h"
#include <string.h>

/*
 * GBA DirectSound is an 8-bit signed PCM FIFO.  An older implementation used
 * DMA with a source pointer that was changed once per frame, but FIFO DMA has
 * no automatic end-of-buffer wrap.  Once that pointer ran off the end of the
 * small mix buffer, the hardware read unrelated ROM/RAM and produced a short
 * burst of static followed by silence.
 *
 * Instead, keep a small producer/consumer ring in EWRAM.  Timer 0 runs at
 * exactly 32,768 Hz (/256 prescaler, reload 0xFFFE: 65,536/2 Hz).  Per
 * GBATEK's DirectSound documentation the GBA re-samples all audio internally
 * to 32.768 kHz and best accuracy comes from timer rates that divide into it
 * exactly, so this is 1:1 with the hardware output rate and no resampling
 * distortion is introduced by the DAC path.  The IRQ writes one 32-bit word
 * (four samples) to FIFO A every four timer overflows.  The main loop fills
 * the ring with mixed music and effects ahead of the interrupt, so changing
 * music or triggering an effect never races a DMA transfer or leaves the
 * FIFO pointing outside a buffer.
 *
 * The FIFO only holds ~16-32 samples (~0.5-1 ms), so the CPU may never be
 * blocked longer than that; gfx_flip() therefore copies the frame with the
 * CPU instead of one long blocking DMA3 burst (GBATEK's DMA section warns
 * exactly against letting long DMAs starve the sound FIFO).
 */
#define AUDIO_RING_SAMPLES 4096u
#define AUDIO_RING_MASK (AUDIO_RING_SAMPLES - 1u)
#define AUDIO_TARGET_SAMPLES 1024u
#define MAX_ACTIVE_SFX 4

/* Fixed mix gains (Q7).  Previously music and every effect were summed at
 * full scale and hard-clamped, so the first laser over a loud music passage
 * produced crunchy clipping distortion.  Music gets ~72% and each effect
 * ~37.5% of full scale; the residual peaks are rounded off by a soft-clip
 * curve instead of the DAC rail. */
#define MUSIC_GAIN_Q7 92
#define SFX_GAIN_Q7   48

/* Largest possible |mixed| value: music 127*92/128 = 91, sfx 4 x 127*48/128 =
 * 4 x 47 = 188 -> 279 total. */
#define MIX_CLIP_LIMIT 279
#define MIX_KNEE 96

#if (AUDIO_RING_SAMPLES & AUDIO_RING_MASK) != 0
#error "AUDIO_RING_SAMPLES must be a power of two"
#endif

typedef struct {
    const s8* data;
    u32 length;
    u32 position;
    bool active;
} SfxChannel;

EWRAM_BSS static s8 s_audio_ring[AUDIO_RING_SAMPLES] __attribute__((aligned(4)));
static volatile u32 s_read_pos = 0;
static volatile u32 s_write_pos = 0;
static volatile u32 s_fifo_phase = 0;
static bool s_audio_started = false;

static BgmTrack s_current_bgm = BGM_NONE;
static const s8* s_bgm_data = NULL;
static u32 s_bgm_len = 0;
static u32 s_bgm_pos = 0;

static SfxChannel s_sfx_channels[MAX_ACTIVE_SFX];

/* Sync block mirrored by the web player (see audio.h).  EWRAM so it lands in
 * the libretro system-RAM memory region. */
EWRAM_BSS volatile WebAudioSync g_web_audio_sync;

/* Soft-clip table: s_soft_clip[v + MIX_CLIP_LIMIT] for v in
 * [-MIX_CLIP_LIMIT, MIX_CLIP_LIMIT].  Linear up to MIX_KNEE, then a smooth
 * quadratic shoulder that approaches +/-127 asymptotically. */
static s8 s_soft_clip[MIX_CLIP_LIMIT * 2 + 1];

/* Refresh everything the web player reads.  Called every frame from
 * audio_update() and immediately after any state change. */
static void audio_sync_push(void) {
    g_web_audio_sync.bgm_id = (s8)s_current_bgm;
    g_web_audio_sync.bgm_playing = (s_bgm_data != NULL);
    g_web_audio_sync.music_vol = (u8)g_settings.music_volume;
    g_web_audio_sync.sfx_vol = (u8)g_settings.sfx_volume;
    g_web_audio_sync.bgm_pos = s_bgm_pos;
    g_web_audio_sync.bgm_len = s_bgm_len;
}

static void audio_init_soft_clip(void) {
    const float range = (float)(MIX_CLIP_LIMIT - MIX_KNEE);
    for (int v = -MIX_CLIP_LIMIT; v <= MIX_CLIP_LIMIT; v++) {
        int av = v < 0 ? -v : v;
        if (av <= MIX_KNEE) {
            s_soft_clip[v + MIX_CLIP_LIMIT] = (s8)v;
        } else {
            float t = ((float)av - (float)MIX_KNEE) / range;
            float y = 127.0f - (127.0f - (float)MIX_KNEE) * (1.0f - t) * (1.0f - t);
            s_soft_clip[v + MIX_CLIP_LIMIT] = (s8)(v < 0 ? -(int)y : (int)y);
        }
    }
}

static int audio_ring_level(u32 write_pos) {
    return (int)((write_pos - s_read_pos) & AUDIO_RING_MASK);
}

/* Mix one signed 8-bit sample.  Playback cursors advance even when a volume
 * slider is at zero, so muting does not pause or later replay an effect. */
static s8 audio_mix_sample(void) {
    int mixed = 0;
    int music_vol = g_settings.music_volume;
    int sfx_vol = g_settings.sfx_volume;

    if (s_bgm_data && s_bgm_len > 0) {
        int sample = s_bgm_data[s_bgm_pos];
        mixed += (sample * music_vol * MUSIC_GAIN_Q7) / 12800;
        s_bgm_pos++;
        if (s_bgm_pos >= s_bgm_len) {
            s_bgm_pos = 0;
        }
    }

    for (int ch = 0; ch < MAX_ACTIVE_SFX; ch++) {
        if (s_sfx_channels[ch].active) {
            int sample = s_sfx_channels[ch].data[s_sfx_channels[ch].position];
            mixed += (sample * sfx_vol * SFX_GAIN_Q7) / 12800;
            s_sfx_channels[ch].position++;
            if (s_sfx_channels[ch].position >= s_sfx_channels[ch].length) {
                s_sfx_channels[ch].active = false;
            }
        }
    }

    if (mixed > MIX_CLIP_LIMIT) mixed = MIX_CLIP_LIMIT;
    if (mixed < -MIX_CLIP_LIMIT) mixed = -MIX_CLIP_LIMIT;
    return s_soft_clip[mixed + MIX_CLIP_LIMIT];
}

/* This runs from the Timer 0 IRQ.  It is deliberately short: interrupts on
 * the GBA should acknowledge their source and return quickly.  Acknowledge
 * FIRST: if the write lands after the FIFO work, a level-triggered source
 * can re-enter the handler and double-pop the ring. */
IWRAM_CODE static void audio_timer_isr(void) {
    REG_IF = IRQ_TIMER0;
    if ((s_fifo_phase & 3u) == 3u) {
        u32 word = 0;
        for (u32 i = 0; i < 4; i++) {
            u32 read_pos = s_read_pos;
            s8 sample = 0;
            if (read_pos != s_write_pos) {
                sample = s_audio_ring[read_pos];
                s_read_pos = (read_pos + 1u) & AUDIO_RING_MASK;
            }
            word |= ((u32)(u8)sample) << (i * 8u);
        }
        REG_FIFO_A = word;
    }
    s_fifo_phase++;
}

void audio_init(void) {
    s_current_bgm = BGM_NONE;
    s_bgm_data = NULL;
    s_bgm_len = 0;
    s_bgm_pos = 0;
    s_read_pos = 0;
    s_write_pos = 0;
    s_fifo_phase = 0;
    s_audio_started = false;

    for (int i = 0; i < MAX_ACTIVE_SFX; i++) {
        s_sfx_channels[i].data = NULL;
        s_sfx_channels[i].length = 0;
        s_sfx_channels[i].position = 0;
        s_sfx_channels[i].active = false;
    }
    memset(s_audio_ring, 0, sizeof(s_audio_ring));

    g_web_audio_sync.magic = WEB_AUDIO_SYNC_MAGIC;
    g_web_audio_sync.version = WEB_AUDIO_SYNC_VERSION;
    g_web_audio_sync.frame = 0;
    for (int i = 0; i < WEB_AUDIO_SYNC_MAX_SFX; i++) {
        g_web_audio_sync.sfx_seq[i] = 0;
        g_web_audio_sync.sfx_last[i] = -1;
    }
    audio_sync_push();

    /* Stop a previous timer, then enable the sound master before touching
     * the remaining sound registers (required by the hardware). */
    REG_TM0CNT = 0;
    REG_SOUNDCNT_X = SSTAT_ENABLE;
    REG_SOUNDCNT_L = 0;

    /* DirectSound A to both speakers, 100% ratio, FIFO reset pulse. */
    REG_SOUNDCNT_H = SDS_A100 | SDS_AR | SDS_AL | SDS_ARESET;
    REG_SOUNDCNT_H = SDS_A100 | SDS_AR | SDS_AL;
    for (int i = 0; i < 4; i++) {
        REG_FIFO_A = 0;
    }

    audio_init_soft_clip();

    /* Timer 0 / 256 with reload 0xFFFE overflows every 2 ticks:
     * (16,777,216 / 256) / 2 = exactly 32,768 Hz, matching the GBA's
     * internal audio re-sample rate (GBATEK DirectSound guidance). */
    REG_TM0CNT_L = 0xfffe;
    REG_IF = IRQ_TIMER0;
    irq_add(II_TIMER0, audio_timer_isr);
}

void audio_start(void) {
    if (s_audio_started) return;

    /* Prime the hardware FIFO before the first timer overflow. */
    s_fifo_phase = 0;
    for (int i = 0; i < 4; i++) {
        u32 word = 0;
        for (u32 j = 0; j < 4; j++) {
            u32 read_pos = s_read_pos;
            s8 sample = 0;
            if (read_pos != s_write_pos) {
                sample = s_audio_ring[read_pos];
                s_read_pos = (read_pos + 1u) & AUDIO_RING_MASK;
            }
            word |= ((u32)(u8)sample) << (j * 8u);
        }
        REG_FIFO_A = word;
    }

    s_audio_started = true;
    REG_TM0CNT_H = TM_FREQ_256 | TM_IRQ | TM_ENABLE;
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

    /* Do not let the tail of the previous track remain queued. */
    s_read_pos = s_write_pos;
    audio_sync_push();
}

void audio_stop_bgm(void) {
    s_current_bgm = BGM_NONE;
    s_bgm_data = NULL;
    s_bgm_len = 0;
    s_bgm_pos = 0;
    s_read_pos = s_write_pos;
    audio_sync_push();
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

    if (chosen < WEB_AUDIO_SYNC_MAX_SFX) {
        g_web_audio_sync.sfx_seq[chosen]++;
        g_web_audio_sync.sfx_last[chosen] = (s8)sfx;
    }
    audio_sync_push();
}

void audio_stop_all(void) {
    audio_stop_bgm();
    for (int i = 0; i < MAX_ACTIVE_SFX; i++) {
        s_sfx_channels[i].active = false;
    }
    audio_sync_push();
}

void audio_update(void) {
    u32 write_pos = s_write_pos;
    while (audio_ring_level(write_pos) < (int)AUDIO_TARGET_SAMPLES) {
        s_audio_ring[write_pos] = audio_mix_sample();
        write_pos = (write_pos + 1u) & AUDIO_RING_MASK;
    }
    /* Publish only after each newly mixed sample is in the ring. */
    s_write_pos = write_pos;
    g_web_audio_sync.frame++;
    audio_sync_push();
}
