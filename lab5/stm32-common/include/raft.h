#ifndef RAFT_H
#define RAFT_H

/*
 * Raft agent on the STM32s (Lab 5). So far:
 * - follower side of log replication: APPEND -> local log (raftlog.h) ->
 *   APPEND_ACK, and COMMIT/NET prints
 * - Raft client: EVENT requests to the leader, and REDIRECT for a client
 *   that asks us although we don't lead. Link and node changes go to the
 *   leader in our HB (our view of our own buses), not through here.
 */

/* Call before canbus_init() (registers the APPEND and CLIENT handlers) */
int raft_init(void);

/* One test-button press: ask the leader to log an EVENT for this node.
 * Any context, ISRs included. */
void raft_event(void);

#endif
