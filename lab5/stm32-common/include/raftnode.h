/*
 * Raft agent (Lab 5, Parts 1-3), shared by the Pi and both STM32s. Plain C,
 * like raftlog.c: every node runs the same rules.
 *
 * Elections, log replication (leader and follower side) and the Raft client
 * (EVENT requests, REDIRECT, OK). The leader logs network changes from what
 * it sees in everyone's HB.
 *
 * Not thread-safe: the caller holds one lock around every call (the same one
 * it uses for raftlog). It never sends or prints either. The handlers only
 * update state; raftnode_poll() returns the frames to send and
 * raftnode_notes() the text to print, and the caller does both after
 * dropping its lock. A send can block for ms on a dead bus, and a printk at
 * 115200 baud takes ~5 ms per line: neither may hold up the thread that
 * delivers CAN frames.
 *
 * REF is the Raft heartbeat, but it carries the leader's clock, so the
 * caller sends it (reading the clock right before sending) every
 * RAFT_HEARTBEAT_MS while raftnode_is_leader().
 */
#ifndef RAFTNODE_H
#define RAFTNODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "catalog.h"
#include "raftlog.h"

/* One frame to send. Byte 0's seq nibble is filled in by the sender */
struct raft_frame {
	uint16_t id;
	uint8_t data[8];
};

/* At most one ACK, REDIRECT, VOTE, REQ_VOTE, APPEND and CLIENT REQUEST,
 * plus one OK per other node */
#define RAFT_FRAMES_MAX (6 + RAFT_N - 1)

/* What this node sees of each node: from its HB, or our own state for us */
struct node_seen {
	bool up;		/* its HB heard on either bus lately */
	bool fault;		/* HB byte 1: its own fault */
	uint8_t buses;		/* HB byte 2: bit0 = it hears others on A, bit1 = on B */
};

/* Empty log, term and leader from the config. `me` is this node's ID */
void raftnode_init(int me, int64_t now_ms);

/* Received frames. `from` is the sender's node ID (CAN ID - base) */
void raftnode_on_append(int from, const uint8_t d[8]);
void raftnode_on_ack(int from, const uint8_t d[8]);
void raftnode_on_client(int from, const uint8_t d[8]);
void raftnode_on_req_vote(int from, const uint8_t d[8]);
void raftnode_on_vote(int from, const uint8_t d[8]);

/* REF from `from`. True: it is our leader's, so follow its clock */
bool raftnode_on_ref(int from, const uint8_t d[8]);

/* One EVENT from this node (test button, or 'e' on the Pi), logged exactly once */
void raftnode_event(void);

/* Leader: call every RAFT_APPEND_MS, so followers hear an APPEND even when
 * nothing changes (it carries the commit index) */
void raftnode_keepalive(void);

/*
 * Does the timed work (election timeout, client retries, OKs, commits, and
 * as leader, logging network changes) and returns the frames to send now,
 * in `out`. Call it after every handler and at least every
 * RAFT_HEARTBEAT_MS. seen[n]: what we see of node n, ourselves included.
 */
int raftnode_poll(int64_t now_ms, const struct node_seen seen[RAFT_N + 1],
		  struct raft_frame out[RAFT_FRAMES_MAX]);

/* Lines to print since the last call ("" if none), then cleared */
void raftnode_notes(char *buf, size_t size);

uint8_t raftnode_term(void);
bool raftnode_is_leader(void);
bool raftnode_log_full(void);	/* rule 7: our own append was refused, stay in error */

/* Entry `index` was already committed when we received it (checkoff 7:
 * print it marked replayed) */
bool raftnode_replayed(uint16_t index);

#endif
