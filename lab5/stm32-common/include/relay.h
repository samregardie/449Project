#ifndef RELAY_H
#define RELAY_H

#include <stdbool.h>

/*
 * RELAY / routing (N2 and N3; the Pi has its own): when the other two zones
 * have no bus in common, forward their frames to the other bus unchanged
 * (same ID and seq, so receivers dedup). See relay.c for the two rules.
 */

/* Call after errorstate_init() and before canbus_init() */
int relay_init(void);

/* The routing table's answer, from the committed network (network_relay()).
 * Called by the raft thread after it applies entries. */
void relay_set_network(bool relay);

#endif
