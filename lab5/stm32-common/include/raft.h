#ifndef RAFT_H
#define RAFT_H

/*
 * Raft agent on the STM32s (Lab 5). So far the follower side of log
 * replication: APPEND -> local log (raftlog.h) -> APPEND_ACK, and a
 * low-priority thread that applies committed entries and prints them.
 */

/* Call before canbus_init() (registers the APPEND handlers) */
int raft_init(void);

#endif
