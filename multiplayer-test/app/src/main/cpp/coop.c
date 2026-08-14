#include "coop.h"
#include "game.h"
#include "menu.h"
#include "platform.h"
#include "eos_online.h"
#include "audio.h"
#include <string.h>

/* Only meaningful on the host build (this whole layer is Android-only). */
#ifdef PLATFORM_HOST

/* Packet types (first byte of every P2P packet). */
#define COOP_P_INPUT            0x10 /* guest -> host: local input + loadout */
#define COOP_P_SNAPSHOT_FRAG    0x20 /* host -> guest: one snapshot chunk    */
#define COOP_P_GAME_START       0x30 /* host -> guest: "enter co-op playing" */
#define COOP_P_LEAVE            0x41 /* either -> other: leaving the session */

/* Reuse the game.c constants for cadence / chunk size. */
#define COOP_SNAPSHOT_EVERY 3
#define COOP_CHUNK_MAX 1100
#define COOP_RELIABLE_CH 0
#define COOP_INPUT_CH 1

#define COOP_SNAPSHOT_MAX 2800

static int s_in_session = 0;
static int s_is_host = 0;
static int s_coop_frame = 0;
static u16 s_guest_keys = 0;

/* Host-side snapshot sender. */
static u8 s_tx_snap[COOP_SNAPSHOT_MAX];
/* Guest-side fragment reassembly. */
static u8 s_rcv_snap[COOP_SNAPSHOT_MAX];
static int s_rcv_len = 0;
static int s_rcv_frag_count = 0;
static int s_rcv_frag_index = 0;
static u16 s_rcv_seq = 0;

static u8 s_laser_sfx_cd = 0; /* small rate limiter for local laser SFX */

static void send_input_packet(void) {
    u8 buf[16];
    int idx = 0;
    buf[idx++] = COOP_P_INPUT;
    u16 k = 0;
    if (key_is_down(KEY_LEFT))  k |= KEY_LEFT;
    if (key_is_down(KEY_RIGHT)) k |= KEY_RIGHT;
    if (key_is_down(KEY_UP))    k |= KEY_UP;
    if (key_is_down(KEY_DOWN))  k |= KEY_DOWN;
    if (key_is_down(KEY_A))     k |= KEY_A;
    if (key_is_down(KEY_B))     k |= KEY_B;
    if (key_is_down(KEY_L))     k |= KEY_L;
    if (key_is_down(KEY_R))     k |= KEY_R;
    buf[idx++] = (u8)(k & 0xFF);
    buf[idx++] = (u8)((k >> 8) & 0xFF);
    buf[idx++] = (u8)g_settings.accent_index;
    buf[idx++] = (u8)g_settings.laser_index;
    buf[idx++] = (u8)g_settings.weapon_rig;
    buf[idx++] = (u8)g_settings.trail_index;
    for (int i = 0; i < NUM_UPGRADES && idx < 16; i++) {
        buf[idx++] = g_settings.upgrade_levels[i];
    }
    eos_online_send_packet(buf, (uint32_t)idx, COOP_INPUT_CH, 0);

    /* Local audio feedback: the guest doesn't simulate, so its own laser SFX
     * would never play otherwise.  Pulse it here while firing. */
    if ((k & KEY_A) && s_laser_sfx_cd == 0) {
        audio_play_sfx(SFX_LASER);
        s_laser_sfx_cd = 5;
    }
    if (s_laser_sfx_cd > 0) s_laser_sfx_cd--;
}

static void send_game_start(void) {
    u8 start[2];
    start[0] = COOP_P_GAME_START;
    start[1] = (u8)g_game.mode;
    eos_online_send_packet(start, 2, COOP_RELIABLE_CH, 1);
}

static void send_snapshot(void) {
    int len = game_coop_serialize(s_tx_snap, (int)sizeof(s_tx_snap));
    if (len <= 0) return;
    /* Fragment over reliable-ordered packets so reassembly is in-order. */
    int frag_count = (len + (COOP_CHUNK_MAX - 1)) / COOP_CHUNK_MAX;
    if (frag_count > 255) frag_count = 255;
    u16 seq = (u16)((s_coop_frame * 7) + (s_coop_frame >> 8)); /* monotonic-ish */
    for (int f = 0; f < frag_count; f++) {
        int off = f * COOP_CHUNK_MAX;
        int chunk = len - off;
        if (chunk > COOP_CHUNK_MAX) chunk = COOP_CHUNK_MAX;
        u8 hdr[7];
        hdr[0] = COOP_P_SNAPSHOT_FRAG;
        hdr[1] = (u8)(seq & 0xFF);
        hdr[2] = (u8)((seq >> 8) & 0xFF);
        hdr[3] = (u8)f;
        hdr[4] = (u8)frag_count;
        hdr[5] = (u8)(chunk & 0xFF);
        hdr[6] = (u8)((chunk >> 8) & 0xFF);
        /* Combine header + chunk into one packet buffer. */
        u8 pkt[COOP_CHUNK_MAX + 8];
        memcpy(pkt, hdr, 7);
        memcpy(pkt + 7, s_tx_snap + off, (size_t)chunk);
        eos_online_send_packet(pkt, (uint32_t)(7 + chunk), COOP_RELIABLE_CH, 1);
    }
}

static void handle_fragment(const u8* data, int len) {
    if (len < 7) return;
    u16 seq = (u16)(data[1] | (data[2] << 8));
    int frag = data[3];
    int frag_count = data[4];
    int chunk = data[5] | (data[6] << 8);
    const u8* body = data + 7;
    int body_len = len - 7;
    if (body_len > chunk) body_len = chunk;

    if (frag == 0) {
        s_rcv_seq = seq;
        s_rcv_frag_count = frag_count;
        s_rcv_len = 0;
        s_rcv_frag_index = 0;
    } else if (seq != s_rcv_seq) {
        return; /* stale fragment for an aborted snapshot */
    }

    if (s_rcv_len + body_len <= (int)sizeof(s_rcv_snap)) {
        memcpy(s_rcv_snap + s_rcv_len, body, (size_t)body_len);
        s_rcv_len += body_len;
        s_rcv_frag_index = frag;
    }
    if (s_rcv_frag_count > 0 && s_rcv_frag_index == s_rcv_frag_count - 1) {
        game_coop_apply(s_rcv_snap, s_rcv_len);
        s_rcv_len = 0;
        s_rcv_frag_count = 0;
    }
}

static void handle_input(const u8* data, int len) {
    if (len < 7) return;
    u16 k = (u16)(data[1] | (data[2] << 8));
    s_guest_keys = k;
    CoopLoadout lo;
    lo.accent_index = (int)data[3];
    lo.laser_index = (int)data[4];
    lo.weapon_rig = (WeaponRig)data[5];
    lo.trail_index = (int)data[6];
    for (int i = 0; i < NUM_UPGRADES && (7 + i) < len; i++) {
        lo.upgrade_levels[i] = data[7 + i];
    }
    game_coop_set_guest_loadout(&lo);
    game_coop_set_guest_keys(k);
}

static void handle_leave(void) {
    /* Peer is leaving. As host we drop the guest ship and continue
     * single-player; as guest we return to the main menu. */
    if (s_is_host) {
        game_coop_set_guest_active(0);
    } else {
        game_coop_set_render_only(0);
        menu_open(SCREEN_MAIN_MENU);
    }
    s_in_session = 0;
}

static void handle_packet(const u8* data, int len, int channel) {
    if (!data || len < 1) return;
    u8 type = data[0];
    if (channel == COOP_INPUT_CH) {
        if (type == COOP_P_INPUT) handle_input(data, len);
        return;
    }
    switch (type) {
        case COOP_P_SNAPSHOT_FRAG:
            if (!s_is_host) handle_fragment(data, len);
            break;
        case COOP_P_GAME_START:
            if (!s_is_host) {
                GameMode mode = (data[1] <= GAME_MODE_OVERDRIVE) ? (GameMode)data[1] : GAME_MODE_WAVES;
                game_set_mode(mode);
                game_start();
                game_coop_set_render_only(1);
                menu_open(SCREEN_PLAYING);
            }
            break;
        case COOP_P_LEAVE:
            handle_leave();
            break;
        default:
            break;
    }
}

static void drain_incoming(void) {
    u8 buf[COOP_SNAPSHOT_MAX + 8];
    uint8_t ch = 0;
    int n = eos_online_receive_packet(buf, (uint32_t)sizeof(buf), &ch);
    while (n > 0) {
        if (n > (int)sizeof(buf)) n = (int)sizeof(buf);
        handle_packet(buf, n, (int)ch);
        ch = 0;
        n = eos_online_receive_packet(buf, (uint32_t)sizeof(buf), &ch);
    }
}

void coop_init(void) {
    s_in_session = 0;
    s_is_host = 0;
    s_coop_frame = 0;
    s_rcv_len = 0;
    s_rcv_frag_count = 0;
}

void coop_on_matched(int is_host) {
    s_in_session = 1;
    s_is_host = is_host;
    s_coop_frame = 0;
    s_rcv_len = 0;
    s_rcv_frag_count = 0;
}

void coop_on_unmatched(void) {
    handle_leave();
    s_in_session = 0;
    s_is_host = 0;
}

int coop_in_session(void) {
    return s_in_session;
}

static int s_host_was_playing = 0;

void coop_tick(void) {
    if (!s_in_session) return;
    s_coop_frame++;
    drain_incoming();

    if (s_is_host) {
        /* The host is authoritative.  When it is playing and the guest ship
         * isn't simulated yet, begin the co-op session.  On a host restart
         * (GAME_OVER -> PLAYING) a fresh GAME_START drags the guest into the
         * new run. */
        GameScreen scr = menu_current_screen();
        if (scr == SCREEN_PLAYING || scr == SCREEN_PAUSED) {
            if (!game_coop_is_guest_active()) {
                game_coop_set_guest_active(1);
                send_game_start();
                s_host_was_playing = 1;
            } else if (scr == SCREEN_PLAYING && !s_host_was_playing) {
                /* Host started a brand new run — pull the guest along. */
                send_game_start();
                s_host_was_playing = 1;
            }
            if (game_coop_is_guest_active() && (s_coop_frame % COOP_SNAPSHOT_EVERY) == 0) {
                send_snapshot();
            }
        } else if (scr == SCREEN_GAME_OVER) {
            s_host_was_playing = 0;
            if (game_coop_is_guest_active() && (s_coop_frame % COOP_SNAPSHOT_EVERY) == 0) {
                send_snapshot(); // final frozen state so the guest shows game-over
            }
        } else {
            /* Host left the run — drop the guest ship and tell it. */
            s_host_was_playing = 0;
            if (game_coop_is_guest_active()) {
                u8 leave[1];
                leave[0] = COOP_P_LEAVE;
                eos_online_send_packet(leave, 1, COOP_RELIABLE_CH, 1);
                game_coop_set_guest_active(0);
            }
        }
    } else {
        /* Guest: stream input every frame. */
        send_input_packet();
    }
}

#endif /* PLATFORM_HOST */
