/*
 * Raft agent (Lab 5, Part 1), shared by the Pi and both STM32s. Plain C,
 * like raftlog.c: every node runs the same rules.
 *
 * So far: log replication (leader and follower side) and the Raft client
 * (EVENT requests, REDIRECT, OK). The leader is fixed by RAFT_DEFAULT_LEADER;
 * elections come next.
 *
 * Not thread-safe: the caller holds one lock around every call (the same one
 * it uses for raftlog). It never sends or prints either. The handlers only
 * update state; raftnode_poll() returns the frames to send and
 * raftnode_notes() the text to print, and the caller does both after
 * dropping its lock. A send can block for ms on a dead bus, and a printk at
 * 115200 baud takes ~5 ms per line: neither may hold up the thread that
 * delivers CAN frames.
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

/* At most one ACK, REDIRECT, APPEND and CLIENT REQUEST, plus one OK per other node */
#define RAFT_FRAMES_MAX (4 + RAFT_N - 1)

/* Empty log, term and leader from the config. `me` is this node's ID */
void raftnode_init(int me);

/* Received frames. `from` is the sender's node ID (CAN ID - base) */
void raftnode_on_append(int from, const uint8_t d[8]);
void raftnode_on_ack(int from, const uint8_t d[8]);
void raftnode_on_client(int from, const uint8_t d[8]);

/* One EVENT from this node (test button, or 'e' on the Pi), logged exactly once */
void raftnode_event(void);

/* Leader: call every RAFT_APPEND_MS, so followers hear an APPEND even when
 * nothing changes (it carries the commit index) */
void raftnode_keepalive(void);

/* Leader: append whatever takes the log's newest network to `want` (rules 1, 4) */
void raftnode_log_network(const struct network *want);

/*
 * Does the timed work (client retries, OKs, commits) and returns the frames
 * to send now, in `out`. Call it after every handler and at least every
 * RAFT_HEARTBEAT_MS. alive[n]: we heard node n's HB recently; the leader
 * doesn't wait for nodes it can't hear.
 */
int raftnode_poll(int64_t now_ms, const bool alive[RAFT_N + 1],
		  struct raft_frame out[RAFT_FRAMES_MAX]);

/* Lines to print since the last call ("" if none), then cleared */
void raftnode_notes(char *buf, size_t size);

uint8_t raftnode_term(void);
int raftnode_leader(void);	/* node ID, RAFT_NO_LEADER if none known */
bool raftnode_log_full(void);	/* rule 7: our own append was refused, stay in error */

#endif
