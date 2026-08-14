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
#define COOP_P_REQUEST_STATE    0x51 /* guest -> host: "resend the world now" */

/* Bump whenever the packet layouts change.  The lobby bucket is versioned
 * too, so builds with different protocols never matchmake together.
 * v3: hull-style in loadout/INPUT, synced explosion/fx tail in snapshots,
 *     spectate-on-death semantics. */
#define COOP_PROTOCOL_VERSION 3

/* Reuse the game.c constants for cadence / chunk size. */
#define COOP_SNAPSHOT_EVERY 3
#define COOP_CHUNK_MAX 1100
#define COOP_RELIABLE_CH 0
#define COOP_INPUT_CH 1

#define COOP_SNAPSHOT_MAX 2800

/* If no snapshot has been applied for this many guest ticks (~1.3 s at
 * 90 Hz), ask the host to resend the world.  Self-heals drops caused by
 * NAT warm-up, queue overflow or a slow frame. */
#define COOP_STATE_STALE_TICKS 120

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
/* Guest-side stale-state watchdog (see COOP_STATE_STALE_TICKS). */
static int s_ticks_since_snapshot = 0;
static int s_request_cooldown = 0;

/* The guest doesn't simulate, so it plays its own laser SFX locally.  To
 * avoid the old behaviour — pulsing every 5 frames (18/s at 90 Hz) no
 * matter how slow the guest's real weapon is — mirror the HOST's exact
 * simulation cadence: the same cooldown formula and big-laser timing. */
static int s_guest_fire_cd = 0;      /* ticks until the host fires our next shot */
static int s_guest_beam_charge = 0;  /* big-laser charge mirror */
static int s_guest_beam_timer = 0;   /* big-laser firing mirror */
static int s_guest_beam_firing = 0;
static int s_input_packets_sent = 0; /* first packets go reliable (NAT warm-up) */

static void guest_local_audio(u16 k) {
    /* Dead guest pilot = spectating: our ship isn't firing anymore, so the
     * local cadence mirror must go quiet too (the run continues while the
     * partner fights on). */
    if (game_coop_local_player_dead()) {
        s_guest_fire_cd = 0;
        s_guest_beam_charge = 0;
        s_guest_beam_firing = 0;
        return;
    }
    if (s_guest_fire_cd > 0) s_guest_fire_cd--;
    if ((k & KEY_A) && s_guest_fire_cd == 0) {
        audio_play_sfx(SFX_LASER);
        s_guest_fire_cd = game_coop_local_shot_cooldown();
    }

    bool beam_held = (k & (KEY_B | KEY_R | KEY_L)) != 0;
    int charge_ticks = game_coop_beam_charge_ticks();
    if (s_guest_beam_firing) {
        s_guest_beam_timer--;
        if (s_guest_beam_timer <= 0) {
            s_guest_beam_firing = 0;
            s_guest_beam_charge = 0;
        }
    } else if (beam_held) {
        if (s_guest_beam_charge < charge_ticks) {
            s_guest_beam_charge++;
            if (s_guest_beam_charge >= charge_ticks) {
                s_guest_beam_firing = 1;
                s_guest_beam_timer = game_coop_beam_duration_ticks();
                audio_play_sfx(SFX_LASER);
            }
        }
    } else {
        s_guest_beam_charge -= 2;
        if (s_guest_beam_charge < 0) s_guest_beam_charge = 0;
    }
}

static void send_input_packet(void) {
    u8 buf[24];
    int idx = 0;
    buf[idx++] = COOP_P_INPUT;
    buf[idx++] = COOP_PROTOCOL_VERSION;
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
    buf[idx++] = (u8)g_settings.ship_index; /* hull style (protocol v3) */
    for (int i = 0; i < NUM_UPGRADES && idx < 24; i++) {
        buf[idx++] = g_settings.upgrade_levels[i];
    }

    /* Local prediction: the guest's own ship renders from host snapshots,
     * which lag one round-trip. Feeding the live input into the renderer
     * lets the local ship respond instantly between corrections. */
    game_coop_set_local_input(k);

    /* The first few input packets are reliable: the very first packets on a
     * fresh P2P connection can be dropped while NAT traversal / relay is
     * still warming up, and if the host never learns the guest's loadout the
     * guest ship fights with starter gear (wrong colours, wrong damage).
     * After that, plain unreliable input is fine — a dropped frame is
     * overwritten by the next one 11 ms later. */
    int reliable = (s_input_packets_sent < 3) ? 1 : 0;
    s_input_packets_sent++;
    eos_online_send_packet(buf, (uint32_t)idx, COOP_INPUT_CH, reliable);

    guest_local_audio(k);
}

static void send_game_start(void) {
    u8 start[3];
    start[0] = COOP_P_GAME_START;
    start[1] = COOP_PROTOCOL_VERSION;
    start[2] = (u8)g_game.mode;
    eos_online_send_packet(start, 3, COOP_RELIABLE_CH, 1);
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
        s_ticks_since_snapshot = 0; /* fresh world: reset the stale watchdog */
    }
}

static void handle_input(const u8* data, int len) {
    if (len < 10) return;
    if (data[1] != COOP_PROTOCOL_VERSION) return; /* mismatched build: ignore */
    u16 k = (u16)(data[2] | (data[3] << 8));
    s_guest_keys = k;
    CoopLoadout lo;
    lo.accent_index = (int)data[4];
    lo.laser_index = (int)data[5];
    lo.weapon_rig = (WeaponRig)data[6];
    lo.trail_index = (int)data[7];
    lo.ship_index = (int)data[8]; /* hull style (protocol v3) */
    if (lo.ship_index < 0 || lo.ship_index >= NUM_SHIP_STYLES) lo.ship_index = 0;
    for (int i = 0; i < NUM_UPGRADES && (9 + i) < len; i++) {
        lo.upgrade_levels[i] = data[9 + i];
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
        game_coop_set_local_input(0);
        menu_open(SCREEN_MAIN_MENU);
    }
    s_in_session = 0;
}

static void send_state_request(void) {
    u8 req[2];
    req[0] = COOP_P_REQUEST_STATE;
    req[1] = COOP_PROTOCOL_VERSION;
    eos_online_send_packet(req, 2, COOP_RELIABLE_CH, 1);
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
            if (!s_is_host && len >= 3 && data[1] == COOP_PROTOCOL_VERSION) {
                GameMode mode = (data[2] <= GAME_MODE_OVERDRIVE) ? (GameMode)data[2] : GAME_MODE_WAVES;
                game_set_mode(mode);
                game_start();
                game_coop_set_render_only(1);
                game_coop_set_local_input(0);
                menu_open(SCREEN_PLAYING);
                /* Ask for the world right away so the very first frames of
                 * the guest's screen are not an empty local state. */
                s_ticks_since_snapshot = 0;
                s_request_cooldown = 0;
                send_state_request();
            }
            break;
        case COOP_P_REQUEST_STATE:
            /* Guest lost the stream (drop/overflow): resend the whole world
             * immediately instead of waiting for the next cadence tick. */
            if (s_is_host && game_coop_is_guest_active()) {
                send_snapshot();
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

static void reset_session_state(void) {
    s_rcv_len = 0;
    s_rcv_frag_count = 0;
    s_rcv_frag_index = 0;
    s_ticks_since_snapshot = 0;
    s_request_cooldown = 0;
    s_guest_fire_cd = 0;
    s_guest_beam_charge = 0;
    s_guest_beam_timer = 0;
    s_guest_beam_firing = 0;
    s_input_packets_sent = 0;
}

void coop_init(void) {
    s_in_session = 0;
    s_is_host = 0;
    s_coop_frame = 0;
    reset_session_state();
}

void coop_on_matched(int is_host) {
    s_in_session = 1;
    s_is_host = is_host;
    s_coop_frame = 0;
    reset_session_state();
}

void coop_on_unmatched(void) {
    handle_leave();
    s_in_session = 0;
    s_is_host = 0;
}

int coop_in_session(void) {
    return s_in_session;
}

int coop_is_host(void) {
    return s_is_host;
}

void coop_leave_session(void) {
    if (!s_in_session) return;
    /* Tell the peer first, then tear down locally (same as receiving its
     * LEAVE). Host: the guest ship just vanishes and the run continues.
     * Guest: we stop rendering the streamed world and stop sending input. */
    u8 leave[1];
    leave[0] = COOP_P_LEAVE;
    eos_online_send_packet(leave, 1, COOP_RELIABLE_CH, 1);
    if (s_is_host) {
        game_coop_set_guest_active(0);
    } else {
        game_coop_set_render_only(0);
        game_coop_set_local_input(0);
    }
    reset_session_state();
    s_in_session = 0;
    /* Leaving the co-op session also leaves the match lobby so the host
     * can't silently drag us into another run, and Quick Match is ready
     * for a fresh search right away. */
    eos_online_cancel_match();
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
                /* Push the whole world immediately — the guest should not
                 * wait a cadence tick (or worse, for a stale-state request)
                 * before it can render anything. */
                send_snapshot();
                s_host_was_playing = 1;
            } else if (scr == SCREEN_PLAYING && !s_host_was_playing) {
                /* Host started a brand new run — pull the guest along. */
                send_game_start();
                send_snapshot();
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

        /* Self-healing sync: if no snapshot has been applied for ~1.3 s
         * (packets dropped during NAT warm-up, queue overflow, UI jank),
         * ask the host to resend the world instead of sitting frozen. */
        s_ticks_since_snapshot++;
        if (s_request_cooldown > 0) s_request_cooldown--;
        if (s_ticks_since_snapshot > COOP_STATE_STALE_TICKS && s_request_cooldown == 0) {
            send_state_request();
            s_request_cooldown = COOP_STATE_STALE_TICKS;
        }
    }
}

#endif /* PLATFORM_HOST */
