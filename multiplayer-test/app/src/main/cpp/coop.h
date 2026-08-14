#ifndef SPACE_UNLIMITED_COOP_H
#define SPACE_UNLIMITED_COOP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Co-op networking glue.  This layer sits between the EOS P2P bridge
 * (eos_online.c) and the forked host-authoritative game simulation
 * (game.c), and is only compiled into the multiplayer-test edition.
 *
 *    HOST  : simulates both ships, broadcasts full world snapshots.
 *    GUEST : streams input, renders the host's snapshots (no local sim).
 */

void coop_init(void);

/* Drive the network layer.  Call once per frame, after the local keys have
 * been polled (native-lib calls it between menu_update and menu_draw). */
void coop_tick(void);

/* Called from the EOS glue when a match is joined / left so the coop layer
 * can start / tear down a session. */
void coop_on_matched(int is_host);
void coop_on_unmatched(void);

int coop_in_session(void);

#ifdef __cplusplus
}
#endif

#endif
