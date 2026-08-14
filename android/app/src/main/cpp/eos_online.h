#ifndef SPACE_UNLIMITED_EOS_ONLINE_H
#define SPACE_UNLIMITED_EOS_ONLINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EOS_ONLINE_CONFIG_REQUIRED = 0,
    EOS_ONLINE_INITIALIZING = 1,
    EOS_ONLINE_SIGNING_IN = 2,
    EOS_ONLINE_READY = 3,
    EOS_ONLINE_MATCHMAKING = 4,
    EOS_ONLINE_WAITING_FOR_PLAYER = 5,
    EOS_ONLINE_MATCHED = 6,
    EOS_ONLINE_ERROR = 7
} EosOnlineStatus;

typedef struct {
    const char* internal_dir;
    const char* external_dir;
    const char* product_id;
    const char* sandbox_id;
    const char* deployment_id;
    const char* client_id;
    const char* client_secret;
    const char* display_name;
} EosOnlineConfig;

int eos_online_initialize(const EosOnlineConfig* config);
void eos_online_tick(void);
void eos_online_set_foreground(int foreground);
void eos_online_shutdown(void);

int eos_online_status(void);
const char* eos_online_status_text(void);
int eos_online_is_host(void);
int eos_online_member_count(void);

int eos_online_quick_match(void);
void eos_online_cancel_match(void);

/* Packet bridge used by the upcoming host-authoritative co-op simulation.
 * EOS P2P limits a packet to 1170 bytes. Returns EOS_Success as 1/0. */
int eos_online_send_packet(const void* data, uint32_t size, uint8_t channel, int reliable);
int eos_online_receive_packet(void* data, uint32_t capacity, uint8_t* channel);

#ifdef __cplusplus
}
#endif

#endif
