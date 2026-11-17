/*
 * Replicated log and state machine (Lab 5, Parts 3 and 4). Plain C, no
 * Zephyr: the Pi and both STM32s compile the same raftlog.c.
 *
 * Not thread-safe on purpose: each node calls it under its own lock.
 *
 * Indexes: the first entry is index 1. Index 0 means "no entry" (an empty
 * APPEND, or "nothing before the first entry").
 */
#ifndef RAFTLOG_H
#define RAFTLOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "catalog.h"

/* Network state, built by applying committed entries in order (Part 4) */
struct network {
	bool cockpit_ok;		/* false = failure mode */
	bool node_up[RAFT_N + 1];	/* by node ID; [0] unused */
	bool link_up[RAFT_N + 1][2];	/* [node ID][bus], bus 0 = A, 1 = B */
};

/*
 * Empty log. The network starts as the full topology (every node and link
 * up) with the cockpit in error, since every node boots in error. The
 * leader then logs only changes.
 */
void raftlog_init(void);

/* ---- Log ---- */

uint16_t log_last_index(void);

/* Term and entry at an index; 0 for index 0 or past the end */
uint8_t log_term(uint16_t index);
uint8_t log_entry(uint16_t index);

/* Add one entry at the end. False if the log is full or the entry is malformed */
bool log_append(uint8_t term, uint8_t entry);

/* Drop `index` and everything after it (a follower's conflicting entries).
 * Committed entries are never dropped. */
void log_truncate(uint16_t index);

uint16_t log_commit_index(void);

/* Raise the commit index (never lowers it, never past the last entry) */
void log_set_commit(uint16_t index);

/*
 * Apply the next committed entry to the network. Returns its index, or 0
 * when every committed entry is applied. Call it in a loop, outside any
 * CAN receive path, and print each one.
 */
uint16_t log_apply_next(void);

/* ---- State machine ---- */

/* The current network (the only read, Part 4) */
struct network get_network(void);

/* Printing, the same text on every node */
const char *entry_name(uint8_t entry);	/* "LINK_A_DOWN" */
void network_format(const struct network *net, char *buf, size_t size);	/* "N1=up(A,B) N2=..." */

#endif
