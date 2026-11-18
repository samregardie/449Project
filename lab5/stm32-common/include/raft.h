#ifndef RAFT_H
#define RAFT_H

/*
 * Raft on the STM32s (Lab 5): CAN handlers, the raft thread and printing
 * around the shared agent (raftnode.h), which holds the Raft rules.
 */

/* Call before canbus_init() (registers the APPEND, APPEND_ACK and CLIENT handlers) */
int raft_init(void);

/* One test-button press: ask the leader to log an EVENT for this node.
 * Any context, ISRs included. */
void raft_event(void);

#endif
