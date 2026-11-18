#ifndef RAFT_H
#define RAFT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Raft on the STM32s (Lab 5): CAN handlers, the raft thread and printing
 * around the shared agent (raftnode.h), which holds the Raft rules.
 */

/* Call before canbus_init() (registers the Raft handlers) */
int raft_init(void);

/* One test-button press: ask the leader to log an EVENT for this node.
 * Any context, ISRs included. */
void raft_event(void);

/* A REF (globaltime.c, dispatch thread). True: it is our leader's, follow its clock */
bool raft_on_ref(int from, const uint8_t d[8]);

/* True while we lead; *term is our term (for REF) */
bool raft_leading(uint8_t *term);

/* Leader: every RAFT_APPEND_MS, the keep-alive APPEND */
void raft_keepalive(void);

#endif
